# POSTGRES-CPP 

# Description

This is a port of the [PostgreSQL Database Management System](https://www.postgresql.org/) that can be compiled with a C++ compiler that supports the C++11 standard.

# Dependencies

- **g++ 4.8** (C++11 support)

The following packages are needed for building Postgres, including ssl support: 

```
	sudo apt-get install build-essential libreadline-dev zlib1g-dev flex bison libxml2-dev libxslt-dev libssl-dev
```

# Building the DBMS

```
    mkdir build
    cd build
    ../configure
    make CC=g++ CPPFLAGS+="-std=c++11 -fpermissive -w"
```

# Testing the DBMS

```
    cd build
    cp ../src/test/regress/expected/security_label.out ./src/test/regress/results/
    make check
```

# Porting Notes

Here's a list of the key porting changes:

1. Refactor identifiers that conflict with C++ reserved keywords. Appended the identifiers with "__" to resolve this problem. Here's a list of the keywords that we refactored:

  * `new`
  * `this`
  * `namespace`
  * `friend`
  * `public`
  * `private`
  * `typename`
  * `typeid`
  * `constexpr`
  * `operator`
  * `class`
  * `template`

2. Define the assignment operator for structures with volatile instances.

    * `RelFileNode` at `include/storage/relfilnode.h`
    * `QueuePosition` at `backend/commands/async.cpp`
    * `BufferTag` at `include/storage/buf_internals.h`

3. Define constructor for unions that contain a non-POD member.

    * `SharedInvalidationMessage` ar `include/storage/sinval.h`

4. Refactor missing increment operator. Changed `forkNum++` to `forkNum = forkNum + 1`.

5. Explicity declare that a enum value belongs to the particular enumeration type.

    * `JsonbValue`

7. Refactor forward declaration of static arrays using an anonymous namespace.

    * `pg_crc32c_table` at `port/pg_crc32c_sb8.cpp`

8. Handle identifiers with both extern and const qualifiers.

    * `sync_method_options` at `backend/access/transam/xlog.cpp`
    * `wal_level_options` at `backend/access/rmgrdesc/xlogdesc.cpp`
    * `dynamic_shared_memory_options` at `backend/access/transam/xlog.cpp`
    * `archive_mode_options` at `backend/access/transam/xlog.cpp`

9. Declare functions with varying number of arguments by explicity defining function pointer types.

    * `func_ptr0` at `backend/utils/fmgr/fmgr.c`
    * `func_ptr1` at `backend/utils/fmgr/fmgr.c`
    * ...
    * `func_ptr16` at `backend/utils/fmgr/fmgr.c`
    * `expression_tree_walker` at `include/nodes/nodeFunc.h`
    * `expression_tree_mutator` at `include/nodes/nodeFunc.h`
    * `query_tree_walker` at `include/nodes/nodeFunc.h`
    * `query_tree_mutator` at `include/nodes/nodeFunc.h`
    * `range_table_walker` at `include/nodes/nodeFunc.h`
    * `range_table_mutator` at `include/nodes/nodeFunc.h`
    * `query_or_expression_tree_walker` at `include/nodes/nodeFunc.h`
    * `query_or_expression_tree_mutator` at `include/nodes/nodeFunc.h`
    * `raw_expression_tree_walker` at `include/nodes/nodeFunc.h`

# Credits

	* Ming Fang (@mindbergh)	
