/*-------------------------------------------------------------------------
 *
 * parse_type.h
 *		handle type operations for parser
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/parser/parse_type.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef PARSE_TYPE_H
#define PARSE_TYPE_H

#include "access/htup.h"
#include "parser/parse_node.h"


typedef HeapTuple Type;

extern Type LookupTypeName(ParseState *pstate, const typename__ *typename__,
			   int32 *typmod_p, bool missing_ok);
extern Oid LookupTypeNameOid(ParseState *pstate, const typename__ *typename__,
				  bool missing_ok);
extern Type typenameType(ParseState *pstate, const typename__ *typename__,
			 int32 *typmod_p);
extern Oid	typenameTypeId(ParseState *pstate, const typename__ *typename__);
extern void typenameTypeIdAndMod(ParseState *pstate, const typename__ *typename__,
					 Oid *typeid_p, int32 *typmod_p);

extern char *TypeNameToString(const typename__ *typename__);
extern char *TypeNameListToString(List *typenames);

extern Oid	LookupCollation(ParseState *pstate, List *collnames, int location);
extern Oid	GetColumnDefCollation(ParseState *pstate, ColumnDef *coldef, Oid typeOid);

extern Type typeidType(Oid id);

extern Oid	typeTypeId(Type tp);
extern int16 typeLen(Type t);
extern bool typeByVal(Type t);
extern char *typeTypeName(Type t);
extern Oid	typeTypeRelid(Type typ);
extern Oid	typeTypeCollation(Type typ);
extern Datum stringTypeDatum(Type tp, char *string, int32 atttypmod);

extern Oid	typeidTypeRelid(Oid type_id);

extern typename__ *typeStringToTypeName(const char *str);
extern void parseTypeString(const char *str, Oid *typeid_p, int32 *typmod_p, bool missing_ok);

#define ISCOMPLEX(typeid__) (typeidTypeRelid(typeid__) != InvalidOid)

#endif   /* PARSE_TYPE_H */
