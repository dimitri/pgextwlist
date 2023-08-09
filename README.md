# PostgreSQL Extension Whitelist

This extension implements extension whitelisting, and will actively prevent
users from installing extensions not in the provided list. Also, this
extension implements a form of `sudo` facility in that the whitelisted
extensions will get installed as if *superuser*. Privileges are dropped
before handing the control back to the user.

The operations `CREATE EXTENSION`, `DROP EXTENSION`, `ALTER EXTENSION ...
UPDATE`, and `COMMENT ON EXTENSION` are run by *superuser*.
The `ALTER EXTENSION ... ADD|DROP` command is intentionally not supported so
as not to allow users to modify an already installed extension. That means
that it's not currently possible to `CREATE EXTENSION ... FROM 'unpackaged';`.

Note that the extension script is running as if run by a stored procedure
owned by your *bootstrap superuser* and with `SECURITY DEFINER`, meaning
that the extension and all its objects are owned by this *superuser*.

PostgreSQL versions 10 and later are supported.

## Licence

The `pgextwlist` PostgreSQL extension is released under
[The PostgreSQL Licence](http://www.postgresql.org/about/licence/), a
liberal Open Source license, similar to the BSD or MIT licenses.

## Install

 1. Install the server development packages (on Ubuntu, this would look like
    `apt-get install postgresql-server-dev-all`)

 2. then:

        make
        sudo make install

This will generate a `pgextwlist.so` shared library that you will have to
install in

    `pg_config --pkglibdir`/plugins

so that your backend loads it automatically.

## Setup

You need to define the list of extensions that are whitelisted, the user
that performs the extension installing, and the error behavior.

* `local_preload_libraries`

  Add `pgextwlist` to the `local_preload_libraries` setting. Don't forget to
  add the module in the `$plugin` directory.

* `extwlist.extensions`

  List of extensions allowed for installation.

  To allow only certain users to use the whitelist, use `ALTER ROLE` instead of
  setting this parameter globally:

  `ALTER ROLE adminuser SET extwlist.extensions = 'pg_stat_statements, postgis';`

* `extwlist.custom_path`

  Filesystem path where to look for *custom scripts*.

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

## Custom Scripts

Some extensions are installing objects that only the *superuser* can make
use of by default, it's then a good idea to tweak permissions and grant
usage to the *current_user* or even the *database owner*, depending.

The custom scripts feature allows to do that by providing scripts to be run
around the execution of the extension's script itself.

#### `create extension` custom scripts

For the creation of extension `extname` version `1.0` the following scripts
will be used when they do exist, as shown here:

  - `${extwlist.custom_path}/extname/before--1.0.sql`

  - `${extwlist.custom_path}/extname/before-create.sql`, only when the
     previous one, specific to the version being installed, does not exists.

  - The `CREATE EXTENSION` command now runs normally

  - `${extwlist.custom_path}/extname/after--1.0.sql`

  - `${extwlist.custom_path}/extname/after-create.sql`, only when the
    specific one does not exist

#### `alter extension update` custom scripts

For the update of extension `extname` from version `1.0` to version `1.1`
the following scripts will be used when they do exist, as shown here:

  - `${extwlist.custom_path}/extname/before--1.0--1.1.sql`

  - `${extwlist.custom_path}/extname/before-update.sql`, only when the
     specific one does not exists.

  - The `ALTER EXTENSION UPDATE` command now runs normally

  - `${extwlist.custom_path}/extname/after--1.0--1.1.sql`

  - `${extwlist.custom_path}/extname/after-update.sql` only when the
     specific one does not exists.

#### `comment on extension` and `drop extension` scripts

Similarly:

   - `${extwlist.custom_path}/extname/before-comment.sql`
   - `${extwlist.custom_path}/extname/after-comment.sql`

   - `${extwlist.custom_path}/extname/before-drop.sql`
   - `${extwlist.custom_path}/extname/after-drop.sql`

Version-specific hook files are not supported here.

#### custom scripts templating

Before executing them, the *extwlist* extension applies the following
substitutions to the *custom scripts*:

  - any line that begins with `\echo` is removed,

  - the literal `@extschema@` is unconditionally replaced by the current
    schema being used to create the extension objects,

  - the literal `@current_user@` is replaced by the name of the current
    user,

  - the literal `@database_owner@` is replaced by the name of the current
    database owner.

Tip: remember that you can execute `DO` blocks if you need dynamic SQL.

## Internals

The whitelisting works by overloading the `ProcessUtility_hook` and gaining
control each time a utility statement is issued. When this statement is a
`CREATE EXTENSION`, the extension's name is extracted from the `parsetree`
and checked against the whitelist. *Superuser* is obtained as in the usual
`SECURITY DEFINER` case, except hard coded to target the *bootstrap user*.
