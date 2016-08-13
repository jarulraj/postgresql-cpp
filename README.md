# PostgreSQL-CPP 
<img src="http://db.cs.cmu.edu/wordpress/wp-content/uploads/2015/01/pgc.jpg" width="200"></img>
[![PostgreSQL license](https://img.shields.io/badge/license-postgresql-green.svg?style=flat)](https://www.postgresql.org/about/licence/) [![Version](https://img.shields.io/badge/version-9.5.3-red.svg)](https://www.postgresql.org/docs/9.5/static/release-9-5-3.html)
-----------------

This is a port of the [PostgreSQL Database Management System](https://www.postgresql.org/) to the C++ language ([C++11 standard](https://gcc.gnu.org/projects/cxx-status.html#cxx11)).

## Dependencies

- **g++ >= 4.8** (C++11 support)

The following packages are needed for building Postgres, including ssl support: 

### Redhat

```
$ sudo yum install -y bison-devel readline-devel zlib-devel openssl-devel wget
$ sudo yum groupinstall -y 'Development Tools'
```

### Ubuntu 

```
$ sudo apt-get install build-essential libreadline-dev zlib1g-dev flex bison libxml2-dev libxslt-dev libssl-dev
```

More information is available in the [PostgreSQL manual](https://www.postgresql.org/docs/9.5/static/install-procedure.html) and the [PostgreSQL wiki](https://wiki.postgresql.org/wiki/Compile_and_Install_from_source_code).

## Building the DBMS

```
$  mkdir build
$  cd build
$  ../configure
$  make CC=g++ CPPFLAGS+="-std=c++11 -fpermissive -w"
$  make install
```

## Using the DBMS

### Create a data directory

```
$ cd build
$ ./bin/initdb ./data
```

We use `initdb` to create a new database cluster which is a collection of databases that are managed by a single server instance. Here, `./data` is the location of the data directory for this new database cluster.

### Start the server

```
$ ./bin/pg_ctl -D ./data start
```

`pg_ctl` is a program that can be used to start, stop, and control a Peloton server. It is installed along with Peloton, by default in `usr/local/peloton/bin`. Here, we use it to start the server. 

### Create a default user

```
$ ./bin/createuser -s -r postgres
```

Let's create a default user using `createuser` utility.

### Connect to the server

```
$ ./bin/psql postgres
```

Finally, we can connect to the Peloton server using the `psql` terminal utility. Here, we connect to the `postgres` database. We can also connect to any other database in the cluster by altering this command. To get a list of `psql` shortcuts, just type `help` and then press enter.

### Stop the server

```
$ ./bin/pg_ctl -D ./data stop
```

Now, we can use `pg_ctl` to stop the server. 

### More information on using the terminal

[PSQL manual](http://www.postgresql.org/docs/9.5/static/app-psql.html)  
[PSQL common usage](http://postgresguide.com/utilities/psql.html)

## Testing the DBMS

```
$  cd build
$  cp ../src/test/regress/expected/security_label.out ./src/test/regress/results/
$  make check
```

## Porting Notes

Here's a list of the key changes:

1. Refactored identifiers that conflict with C++ reserved keywords. Appended the identifiers with "__" to resolve this problem. Here's a list of the keywords that we refactored:

  * `new`, `this`, `namespace`, `friend`, `public`, `private`
  * `typename`, `typeid`, `constexpr`, `operator`, `class`, `template`

2. Defined the assignment operator for structures with volatile instances.

    * `RelFileNode` at `include/storage/relfilnode.h`
    * `QueuePosition` at `backend/commands/async.cpp`
    * `BufferTag` at `include/storage/buf_internals.h`

3. Defined the constructor for unions that contain a non-POD member.

    * `SharedInvalidationMessage` ar `include/storage/sinval.h`

4. Refactored the missing increment operator. Changed `forkNum++` to `forkNum = forkNum + 1`.

5. Explicity declared that a enum value belongs to the particular enumeration type.

    * `JsonbValue`

7. Refactored forward declaration of static arrays using an anonymous namespace.

    * `pg_crc32c_table` at `port/pg_crc32c_sb8.cpp`

8. Handled identifiers with both extern and const qualifiers.

    * `sync_method_options` at `backend/access/transam/xlog.cpp`
    * `wal_level_options` at `backend/access/rmgrdesc/xlogdesc.cpp`
    * `dynamic_shared_memory_options` at `backend/access/transam/xlog.cpp`
    * `archive_mode_options` at `backend/access/transam/xlog.cpp`

9. Declared functions with varying number of arguments by explicity defining function pointer types.

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

## Credits

[Ming Fang](https://github.com/mindbergh)	
