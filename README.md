# PostgreSQL Extension Whitelist

This extension implements extension whitelisting, and will actively prevent
users from installing extensions not in the provided list. Also, this
extension implements a form of `sudo` facility in that the whitelisted
extensions will get installed as if *superuser*. Privileges are droped
before handing the control back to the user.

The operations `CREATE EXTENSION`, `DROP EXTENSION` and `ALTER EXTENSION ...
UPDATE` are run by *superuser*. The `ALTER EXTENSION ... ADD|DROP` command
is intentionnaly not supported so as not to allow users to modify an already
installed extension. That means that it's not currently possible to `CREATE
EXTENSION ... FROM 'unpackaged';`.

Note that the extension script is running as if run by a stored procedure
owned by your *bootstrap superuser* and with `SECURITY DEFINER`, meaning
that the extension and all its objects are owned by this *superuser*.

## Install

You should have received that as a debian package or equivalent:

    apt-get install postgresql-9.1-extension-whitelist

If that's not the case:

1. install the server development packages (on Ubuntu, this would look like `apt-get install postgresql-server-dev-all`)
2. then:

    make USE_PGXS=1
    sudo make USE_PGXS=1 install

This will generate a `pgextwlist.so` shared library that you will have to
install in

    `pg_config --libdir`/plugins

so that your backend loads it automatically.

This is the preferred approach to installing pgextwlist. Alternatively, you may
build pgextwlist from the contrib subdirectory of the main PostgreSQL source
code tree. In that case, the pgextlist directory should be moved into contrib/
before building, and the above instructions should be followed with
`USE_PGXS=1` ommitted.

## Setup

You need to define the list of extensions that are whitelisted, the user
that performs the extension installing, and the error behavior.

* `local_preload_libraries`

  Add `pgextwlist` to the `local_preload_libraries` setting. Don't forget to
  add the module in the `$plugin` directory.

* `custom_variable_classes`

  Add `extwlist` to the `custom_variable_classes` setting if you're using
  9.1, in 9.2 this setting disapeared.

* `extwlist.extensions`

  List of extensions allowed for installation.

## Usage

That's quite simple:

    $ edit postgresql.conf to add local_preload_libraries, custom_variable_classes and extwlist.extensions

    dim=# show extwlist.extensions;
    show extwlist.extensions;
     extwlist.extensions 
    ---------------------
     hstore,cube
    (1 row)

    dim=# create extension foo;
    create extension foo;
    ERROR:  extension "foo" is not whitelisted
    DETAIL:  Installing the extension "foo" failed, because it is not on the whitelist of user-installable extensions.
    HINT:  Your system administrator has allowed users to install certain extensions. See: SHOW extwlist.extensions;

    dim=# create extension hstore;
    create extension hstore;
    WARNING:  => is deprecated as an operator name
    DETAIL:  This name may be disallowed altogether in future versions of PostgreSQL.
    CREATE EXTENSION

    dim=# \dx
    \dx
                               List of installed extensions
      Name   | Version |   Schema   |                   Description                    
    ---------+---------+------------+--------------------------------------------------
     hstore  | 1.0     | public     | data type for storing sets of (key, value) pairs
     plpgsql | 1.0     | pg_catalog | PL/pgSQL procedural language
    (2 rows)

Even if you're not superuser:

    dim=> select rolsuper from pg_roles where rolname = current_user;
    select rolsuper from pg_roles where rolname = current_user;
     rolsuper 
    ----------
     f
    (1 row)
    
    dim=> create extension hstore;
    create extension hstore;
    WARNING:  => is deprecated as an operator name
    DETAIL:  This name may be disallowed altogether in future versions of PostgreSQL.
    CREATE EXTENSION

    dim=> create extension earthdistance;
    create extension earthdistance;
    ERROR:  extension "earthdistance" is not whitelisted
    DETAIL:  Installing the extension "earthdistance" failed, because it is not on the whitelist of user-installable extensions.
    HINT:  Your system administrator has allowed users to install certain extensions. SHOW extwlist.extensions;

    dim=> \dx
    \dx
                               List of installed extensions
      Name   | Version |   Schema   |                   Description                    
    ---------+---------+------------+--------------------------------------------------
     hstore  | 1.0     | public     | data type for storing sets of (key, value) pairs
     plpgsql | 1.0     | pg_catalog | PL/pgSQL procedural language
    (2 rows)

    dim=> drop extension hstore;
    drop extension hstore;
    DROP EXTENSION
  
## Internals

The whitelisting works by overloading the `ProcessUtility_hook` and gaining
control each time a utility statement is issued. When this statement is a
`CREATE EXTENSION`, the extension's name is extracted from the `parsetree`
and checked against the whitelist. *Superuser* is obtained as in the usual
`SECURITY DEFINER` case, except hard coded to target the *bootstrap user*.
