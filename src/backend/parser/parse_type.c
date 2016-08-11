/*-------------------------------------------------------------------------
 *
 * parse_type.c
 *		handle type operations for parser
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/parser/parse_type.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "lib/stringinfo.h"
#include "nodes/makefuncs.h"
#include "parser/parser.h"
#include "parser/parse_type.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"


static int32 typenameTypeMod(ParseState *pstate, const TypeName *typename__,
				Type typ);


/*
 * LookupTypeName
 *		Given a typename__ object, lookup the pg_type syscache entry of the type.
 *		Returns NULL if no such type can be found.  If the type is found,
 *		the typmod value represented in the typename__ struct is computed and
 *		stored into *typmod_p.
 *
 * NB: on success, the caller must ReleaseSysCache the type tuple when done
 * with it.
 *
 * NB: direct callers of this function MUST check typisdefined before assuming
 * that the type is fully valid.  Most code should go through typenameType
 * or typenameTypeId instead.
 *
 * typmod_p can be passed as NULL if the caller does not care to know the
 * typmod value, but the typmod decoration (if any) will be validated anyway,
 * except in the case where the type is not found.  Note that if the type is
 * found but is a shell, and there is typmod decoration, an error will be
 * thrown --- this is intentional.
 *
 * pstate is only used for error location info, and may be NULL.
 */
Type
LookupTypeName(ParseState *pstate, const TypeName *typename__,
			   int32 *typmod_p, bool missing_ok)
{
	Oid			typoid;
	HeapTuple	tup;
	int32		typmod;

	if (typename__->names == NIL)
	{
		/* We have the OID already if it's an internally generated typename__ */
		typoid = typename__->typeOid;
	}
	else if (typename__->pct_type)
	{
		/* Handle %TYPE reference to type of an existing field */
		RangeVar   *rel = makeRangeVar(NULL, NULL, typename__->location);
		char	   *field = NULL;
		Oid			relid;
		AttrNumber	attnum;

		/* deconstruct the name list */
		switch (list_length(typename__->names))
		{
			case 1:
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
				errmsg("improper %%TYPE reference (too few dotted names): %s",
					   NameListToString(typename__->names)),
						 parser_errposition(pstate, typename__->location)));
				break;
			case 2:
				rel->relname = strVal(linitial(typename__->names));
				field = strVal(lsecond(typename__->names));
				break;
			case 3:
				rel->schemaname = strVal(linitial(typename__->names));
				rel->relname = strVal(lsecond(typename__->names));
				field = strVal(lthird(typename__->names));
				break;
			case 4:
				rel->catalogname = strVal(linitial(typename__->names));
				rel->schemaname = strVal(lsecond(typename__->names));
				rel->relname = strVal(lthird(typename__->names));
				field = strVal(lfourth(typename__->names));
				break;
			default:
				ereport(ERROR,
						(errcode(ERRCODE_SYNTAX_ERROR),
						 errmsg("improper %%TYPE reference (too many dotted names): %s",
								NameListToString(typename__->names)),
						 parser_errposition(pstate, typename__->location)));
				break;
		}

		/*
		 * Look up the field.
		 *
		 * XXX: As no lock is taken here, this might fail in the presence of
		 * concurrent DDL.  But taking a lock would carry a performance
		 * penalty and would also require a permissions check.
		 */
		relid = RangeVarGetRelid(rel, NoLock, missing_ok);
		attnum = get_attnum(relid, field);
		if (attnum == InvalidAttrNumber)
		{
			if (missing_ok)
				typoid = InvalidOid;
			else
				ereport(ERROR,
						(errcode(ERRCODE_UNDEFINED_COLUMN),
					errmsg("column \"%s\" of relation \"%s\" does not exist",
						   field, rel->relname),
						 parser_errposition(pstate, typename__->location)));
		}
		else
		{
			typoid = get_atttype(relid, attnum);

			/* this construct should never have an array indicator */
			Assert(TypeName->arrayBounds == NIL);

			/* emit nuisance notice (intentionally not errposition'd) */
			ereport(NOTICE,
					(errmsg("type reference %s converted to %s",
							TypeNameToString(typename__),
							format_type_be(typoid))));
		}
	}
	else
	{
		/* Normal reference to a type name */
		char	   *schemaname;
		char	   *typname;

		/* deconstruct the name list */
		DeconstructQualifiedName(typename__->names, &schemaname, &typname);

		if (schemaname)
		{
			/* Look in specific schema only */
			Oid			namespaceId;
			ParseCallbackState pcbstate;

			setup_parser_errposition_callback(&pcbstate, pstate, typename__->location);

			namespaceId = LookupExplicitNamespace(schemaname, missing_ok);
			if (OidIsValid(namespaceId))
				typoid = GetSysCacheOid2(TYPENAMENSP,
										 PointerGetDatum(typname),
										 ObjectIdGetDatum(namespaceId));
			else
				typoid = InvalidOid;

			cancel_parser_errposition_callback(&pcbstate);
		}
		else
		{
			/* Unqualified type name, so search the search path */
			typoid = TypenameGetTypid(typname);
		}

		/* If an array reference, return the array type instead */
		if (typename__->arrayBounds != NIL)
			typoid = get_array_type(typoid);
	}

	if (!OidIsValid(typoid))
	{
		if (typmod_p)
			*typmod_p = -1;
		return NULL;
	}

	tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(typoid));
	if (!HeapTupleIsValid(tup)) /* should not happen */
		elog(ERROR, "cache lookup failed for type %u", typoid);

	typmod = typenameTypeMod(pstate, typename__, (Type) tup);

	if (typmod_p)
		*typmod_p = typmod;

	return (Type) tup;
}

/*
 * LookupTypeNameOid
 *		Given a typename__ object, lookup the pg_type syscache entry of the type.
 *		Returns InvalidOid if no such type can be found.  If the type is found,
 *		return its Oid.
 *
 * NB: direct callers of this function need to be aware that the type OID
 * returned may correspond to a shell type.  Most code should go through
 * typenameTypeId instead.
 *
 * pstate is only used for error location info, and may be NULL.
 */
Oid
LookupTypeNameOid(ParseState *pstate, const TypeName *typename__, bool missing_ok)
{
	Oid			typoid;
	Type		tup;

	tup = LookupTypeName(pstate, typename__, NULL, missing_ok);
	if (tup == NULL)
	{
		if (!missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" does not exist",
							TypeNameToString(typename__)),
					 parser_errposition(pstate, typename__->location)));

		return InvalidOid;
	}

	typoid = HeapTupleGetOid(tup);
	ReleaseSysCache(tup);

	return typoid;
}

/*
 * typenameType - given a typename__, return a Type structure and typmod
 *
 * This is equivalent to LookupTypeName, except that this will report
 * a suitable error message if the type cannot be found or is not defined.
 * Callers of this can therefore assume the result is a fully valid type.
 */
Type
typenameType(ParseState *pstate, const TypeName *typename__, int32 *typmod_p)
{
	Type		tup;

	tup = LookupTypeName(pstate, typename__, typmod_p, false);
	if (tup == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("type \"%s\" does not exist",
						TypeNameToString(typename__)),
				 parser_errposition(pstate, typename__->location)));
	if (!((Form_pg_type) GETSTRUCT(tup))->typisdefined)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("type \"%s\" is only a shell",
						TypeNameToString(typename__)),
				 parser_errposition(pstate, typename__->location)));
	return tup;
}

/*
 * typenameTypeId - given a typename__, return the type's OID
 *
 * This is similar to typenameType, but we only hand back the type OID
 * not the syscache entry.
 */
Oid
typenameTypeId(ParseState *pstate, const TypeName *typename__)
{
	Oid			typoid;
	Type		tup;

	tup = typenameType(pstate, typename__, NULL);
	typoid = HeapTupleGetOid(tup);
	ReleaseSysCache(tup);

	return typoid;
}

/*
 * typenameTypeIdAndMod - given a typename__, return the type's OID and typmod
 *
 * This is equivalent to typenameType, but we only hand back the type OID
 * and typmod, not the syscache entry.
 */
void
typenameTypeIdAndMod(ParseState *pstate, const TypeName *typename__,
					 Oid *typeid_p, int32 *typmod_p)
{
	Type		tup;

	tup = typenameType(pstate, typename__, typmod_p);
	*typeid_p = HeapTupleGetOid(tup);
	ReleaseSysCache(tup);
}

/*
 * typenameTypeMod - given a typename__, return the internal typmod value
 *
 * This will throw an error if the typename__ includes type modifiers that are
 * illegal for the data type.
 *
 * The actual type OID represented by the typename__ must already have been
 * looked up, and is passed as "typ".
 *
 * pstate is only used for error location info, and may be NULL.
 */
static int32
typenameTypeMod(ParseState *pstate, const TypeName *typename__, Type typ)
{
	int32		result;
	Oid			typmodin;
	Datum	   *datums;
	int			n;
	ListCell   *l;
	ArrayType  *arrtypmod;
	ParseCallbackState pcbstate;

	/* Return prespecified typmod if no typmod expressions */
	if (typename__->typmods == NIL)
		return typename__->typemod;

	/*
	 * Else, type had better accept typmods.  We give a special error message
	 * for the shell-type case, since a shell couldn't possibly have a
	 * typmodin function.
	 */
	if (!((Form_pg_type) GETSTRUCT(typ))->typisdefined)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
			errmsg("type modifier cannot be specified for shell type \"%s\"",
				   TypeNameToString(typename__)),
				 parser_errposition(pstate, typename__->location)));

	typmodin = ((Form_pg_type) GETSTRUCT(typ))->typmodin;

	if (typmodin == InvalidOid)
		ereport(ERROR,
				(errcode(ERRCODE_SYNTAX_ERROR),
				 errmsg("type modifier is not allowed for type \"%s\"",
						TypeNameToString(typename__)),
				 parser_errposition(pstate, typename__->location)));

	/*
	 * Convert the list of raw-grammar-output expressions to a cstring array.
	 * Currently, we allow simple numeric constants, string literals, and
	 * identifiers; possibly this list could be extended.
	 */
	datums = (Datum *) palloc(list_length(typename__->typmods) * sizeof(Datum));
	n = 0;
	foreach(l, typename__->typmods)
	{
		Node	   *tm = (Node *) lfirst(l);
		char	   *cstr = NULL;

		if (IsA(tm, A_Const))
		{
			A_Const    *ac = (A_Const *) tm;

			if (IsA(&ac->val, Integer))
			{
				cstr = psprintf("%ld", (long) ac->val.val.ival);
			}
			else if (IsA(&ac->val, Float) ||
					 IsA(&ac->val, String))
			{
				/* we can just use the str field directly. */
				cstr = ac->val.val.str;
			}
		}
		else if (IsA(tm, ColumnRef))
		{
			ColumnRef  *cr = (ColumnRef *) tm;

			if (list_length(cr->fields) == 1 &&
				IsA(linitial(cr->fields), String))
				cstr = strVal(linitial(cr->fields));
		}
		if (!cstr)
			ereport(ERROR,
					(errcode(ERRCODE_SYNTAX_ERROR),
			errmsg("type modifiers must be simple constants or identifiers"),
					 parser_errposition(pstate, typename__->location)));
		datums[n++] = CStringGetDatum(cstr);
	}

	/* hardwired knowledge about cstring's representation details here */
	arrtypmod = construct_array(datums, n, CSTRINGOID,
								-2, false, 'c');

	/* arrange to report location if type's typmodin function fails */
	setup_parser_errposition_callback(&pcbstate, pstate, typename__->location);

	result = DatumGetInt32(OidFunctionCall1(typmodin,
											PointerGetDatum(arrtypmod)));

	cancel_parser_errposition_callback(&pcbstate);

	pfree(datums);
	pfree(arrtypmod);

	return result;
}

/*
 * appendTypeNameToBuffer
 *		Append a string representing the name of a typename__ to a StringInfo.
 *		This is the shared guts of TypeNameToString and TypeNameListToString.
 *
 * NB: this must work on TypeNames that do not describe any actual type;
 * it is mostly used for reporting lookup errors.
 */
static void
appendTypeNameToBuffer(const TypeName *typename__, StringInfo string)
{
	if (typename__->names != NIL)
	{
		/* Emit possibly-qualified name as-is */
		ListCell   *l;

		foreach(l, typename__->names)
		{
			if (l != list_head(typename__->names))
				appendStringInfoChar(string, '.');
			appendStringInfoString(string, strVal(lfirst(l)));
		}
	}
	else
	{
		/* Look up internally-specified type */
		appendStringInfoString(string, format_type_be(typename__->typeOid));
	}

	/*
	 * Add decoration as needed, but only for fields considered by
	 * LookupTypeName
	 */
	if (typename__->pct_type)
		appendStringInfoString(string, "%TYPE");

	if (typename__->arrayBounds != NIL)
		appendStringInfoString(string, "[]");
}

/*
 * TypeNameToString
 *		Produce a string representing the name of a typename__.
 *
 * NB: this must work on TypeNames that do not describe any actual type;
 * it is mostly used for reporting lookup errors.
 */
char *
TypeNameToString(const TypeName *typename__)
{
	StringInfoData string;

	initStringInfo(&string);
	appendTypeNameToBuffer(typename__, &string);
	return string.data;
}

/*
 * TypeNameListToString
 *		Produce a string representing the name(s) of a List of TypeNames
 */
char *
TypeNameListToString(List *typenames)
{
	StringInfoData string;
	ListCell   *l;

	initStringInfo(&string);
	foreach(l, typenames)
	{
		TypeName   *typename__ = (TypeName *) lfirst(l);

		Assert(IsA(TypeName, TypeName));
		if (l != list_head(typenames))
			appendStringInfoChar(&string, ',');
		appendTypeNameToBuffer(typename__, &string);
	}
	return string.data;
}

/*
 * LookupCollation
 *
 * Look up collation by name, return OID, with support for error location.
 */
Oid
LookupCollation(ParseState *pstate, List *collnames, int location)
{
	Oid			colloid;
	ParseCallbackState pcbstate;

	if (pstate)
		setup_parser_errposition_callback(&pcbstate, pstate, location);

	colloid = get_collation_oid(collnames, false);

	if (pstate)
		cancel_parser_errposition_callback(&pcbstate);

	return colloid;
}

/*
 * GetColumnDefCollation
 *
 * Get the collation to be used for a column being defined, given the
 * ColumnDef node and the previously-determined column type OID.
 *
 * pstate is only used for error location purposes, and can be NULL.
 */
Oid
GetColumnDefCollation(ParseState *pstate, ColumnDef *coldef, Oid typeOid)
{
	Oid			result;
	Oid			typcollation = get_typcollation(typeOid);
	int			location = coldef->location;

	if (coldef->collClause)
	{
		/* We have a raw COLLATE clause, so look up the collation */
		location = coldef->collClause->location;
		result = LookupCollation(pstate, coldef->collClause->collname,
								 location);
	}
	else if (OidIsValid(coldef->collOid))
	{
		/* Precooked collation spec, use that */
		result = coldef->collOid;
	}
	else
	{
		/* Use the type's default collation if any */
		result = typcollation;
	}

	/* Complain if COLLATE is applied to an uncollatable type */
	if (OidIsValid(result) && !OidIsValid(typcollation))
		ereport(ERROR,
				(errcode(ERRCODE_DATATYPE_MISMATCH),
				 errmsg("collations are not supported by type %s",
						format_type_be(typeOid)),
				 parser_errposition(pstate, location)));

	return result;
}

/* return a Type structure, given a type id */
/* NB: caller must ReleaseSysCache the type tuple when done with it */
Type
typeidType(Oid id)
{
	HeapTuple	tup;

	tup = SearchSysCache1(TYPEOID, ObjectIdGetDatum(id));
	if (!HeapTupleIsValid(tup))
		elog(ERROR, "cache lookup failed for type %u", id);
	return (Type) tup;
}

/* given type (as type struct), return the type OID */
Oid
typeTypeId(Type tp)
{
	if (tp == NULL)				/* probably useless */
		elog(ERROR, "typeTypeId() called with NULL type struct");
	return HeapTupleGetOid(tp);
}

/* given type (as type struct), return the length of type */
int16
typeLen(Type t)
{
	Form_pg_type typ;

	typ = (Form_pg_type) GETSTRUCT(t);
	return typ->typlen;
}

/* given type (as type struct), return its 'byval' attribute */
bool
typeByVal(Type t)
{
	Form_pg_type typ;

	typ = (Form_pg_type) GETSTRUCT(t);
	return typ->typbyval;
}

/* given type (as type struct), return the type's name */
char *
typeTypeName(Type t)
{
	Form_pg_type typ;

	typ = (Form_pg_type) GETSTRUCT(t);
	/* pstrdup here because result may need to outlive the syscache entry */
	return pstrdup(NameStr(typ->typname));
}

/* given type (as type struct), return its 'typrelid' attribute */
Oid
typeTypeRelid(Type typ)
{
	Form_pg_type typtup;

	typtup = (Form_pg_type) GETSTRUCT(typ);
	return typtup->typrelid;
}

/* given type (as type struct), return its 'typcollation' attribute */
Oid
typeTypeCollation(Type typ)
{
	Form_pg_type typtup;

	typtup = (Form_pg_type) GETSTRUCT(typ);
	return typtup->typcollation;
}

/*
 * Given a type structure and a string, returns the internal representation
 * of that string.  The "string" can be NULL to perform conversion of a NULL
 * (which might result in failure, if the input function rejects NULLs).
 */
Datum
stringTypeDatum(Type tp, char *string, int32 atttypmod)
{
	Form_pg_type typform = (Form_pg_type) GETSTRUCT(tp);
	Oid			typinput = typform->typinput;
	Oid			typioparam = getTypeIOParam(tp);

	return OidInputFunctionCall(typinput, string, typioparam, atttypmod);
}

/* given a typeid__, return the type's typrelid (associated relation, if any) */
Oid
typeidTypeRelid(Oid type_id)
{
	HeapTuple	typeTuple;
	Form_pg_type type;
	Oid			result;

	typeTuple = SearchSysCache1(TYPEOID, ObjectIdGetDatum(type_id));
	if (!HeapTupleIsValid(typeTuple))
		elog(ERROR, "cache lookup failed for type %u", type_id);

	type = (Form_pg_type) GETSTRUCT(typeTuple);
	result = type->typrelid;
	ReleaseSysCache(typeTuple);
	return result;
}

/*
 * error context callback for parse failure during parseTypeString()
 */
static void
pts_error_callback(void *arg)
{
	const char *str = (const char *) arg;

	errcontext("invalid type name \"%s\"", str);

	/*
	 * Currently we just suppress any syntax error position report, rather
	 * than transforming to an "internal query" error.  It's unlikely that a
	 * type name is complex enough to need positioning.
	 */
	errposition(0);
}

/*
 * Given a string that is supposed to be a SQL-compatible type declaration,
 * such as "int4" or "integer" or "character varying(32)", parse
 * the string and return the result as a typename__.
 * If the string cannot be parsed as a type, an error is raised.
 */
TypeName *
typeStringToTypeName(const char *str)
{
	StringInfoData buf;
	List	   *raw_parsetree_list;
	SelectStmt *stmt;
	ResTarget  *restarget;
	TypeCast   *typecast;
	TypeName   *typename__;
	ErrorContextCallback ptserrcontext;

	/* make sure we give useful error for empty input */
	if (strspn(str, " \t\n\r\f") == strlen(str))
		goto fail;

	initStringInfo(&buf);
	appendStringInfo(&buf, "SELECT NULL::%s", str);

	/*
	 * Setup error traceback support in case of ereport() during parse
	 */
	ptserrcontext.callback = pts_error_callback;
	ptserrcontext.arg = (void *) str;
	ptserrcontext.previous = error_context_stack;
	error_context_stack = &ptserrcontext;

	raw_parsetree_list = raw_parser(buf.data);

	error_context_stack = ptserrcontext.previous;

	/*
	 * Make sure we got back exactly what we expected and no more; paranoia is
	 * justified since the string might contain anything.
	 */
	if (list_length(raw_parsetree_list) != 1)
		goto fail;
	stmt = (SelectStmt *) linitial(raw_parsetree_list);
	if (stmt == NULL ||
		!IsA(stmt, SelectStmt) ||
		stmt->distinctClause != NIL ||
		stmt->intoClause != NULL ||
		stmt->fromClause != NIL ||
		stmt->whereClause != NULL ||
		stmt->groupClause != NIL ||
		stmt->havingClause != NULL ||
		stmt->windowClause != NIL ||
		stmt->valuesLists != NIL ||
		stmt->sortClause != NIL ||
		stmt->limitOffset != NULL ||
		stmt->limitCount != NULL ||
		stmt->lockingClause != NIL ||
		stmt->withClause != NULL ||
		stmt->op != SETOP_NONE)
		goto fail;
	if (list_length(stmt->targetList) != 1)
		goto fail;
	restarget = (ResTarget *) linitial(stmt->targetList);
	if (restarget == NULL ||
		!IsA(restarget, ResTarget) ||
		restarget->name != NULL ||
		restarget->indirection != NIL)
		goto fail;
	typecast = (TypeCast *) restarget->val;
	if (typecast == NULL ||
		!IsA(typecast, TypeCast) ||
		typecast->arg == NULL ||
		!IsA(typecast->arg, A_Const))
		goto fail;

	typename__ = typecast->typename__;
	if (typename__ == NULL ||
		!IsA(typename__, TypeName))
		goto fail;
	if (typename__->setof)
		goto fail;

	pfree(buf.data);

	return typename__;

fail:
	ereport(ERROR,
			(errcode(ERRCODE_SYNTAX_ERROR),
			 errmsg("invalid type name \"%s\"", str)));
	return NULL;				/* keep compiler quiet */
}

/*
 * Given a string that is supposed to be a SQL-compatible type declaration,
 * such as "int4" or "integer" or "character varying(32)", parse
 * the string and convert it to a type OID and type modifier.
 * If missing_ok is true, InvalidOid is returned rather than raising an error
 * when the type name is not found.
 */
void
parseTypeString(const char *str, Oid *typeid_p, int32 *typmod_p, bool missing_ok)
{
	TypeName   *typename__;
	Type		tup;

	typename__ = typeStringToTypeName(str);

	tup = LookupTypeName(NULL, typename__, typmod_p, missing_ok);
	if (tup == NULL)
	{
		if (!missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" does not exist",
							TypeNameToString(typename__)),
					 parser_errposition(NULL, typename__->location)));
		*typeid_p = InvalidOid;
	}
	else
	{
		if (!((Form_pg_type) GETSTRUCT(tup))->typisdefined)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" is only a shell",
							TypeNameToString(typename__)),
					 parser_errposition(NULL, typename__->location)));
		*typeid_p = HeapTupleGetOid(tup);
		ReleaseSysCache(tup);
	}
}
