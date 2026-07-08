-- CVE-2023-39417 mitigation: @-substitution values containing characters
-- that could enable SQL injection (" $ ' \) must be rejected before the
-- replacement is inserted into the script. Verify each forbidden char.

-- pg_stat_statements is whitelisted in setup.sql and has a custom after-create
-- script under test-scripts/, so CREATE EXTENSION always reaches
-- execute_custom_script and the substitution check.

-- 1. Single quote in schema name.
CREATE SCHEMA "weird'name";
SET ROLE mere_mortal;
CREATE EXTENSION pg_stat_statements WITH SCHEMA "weird'name";
RESET ROLE;
DROP SCHEMA "weird'name";

-- 2. Dollar sign in schema name.
CREATE SCHEMA "weird$name";
SET ROLE mere_mortal;
CREATE EXTENSION pg_stat_statements WITH SCHEMA "weird$name";
RESET ROLE;
DROP SCHEMA "weird$name";

-- 3. Backslash in schema name.
CREATE SCHEMA "weird\name";
SET ROLE mere_mortal;
CREATE EXTENSION pg_stat_statements WITH SCHEMA "weird\name";
RESET ROLE;
DROP SCHEMA "weird\name";

-- 4. Double quote in schema name (embedded via SQL "" doubling).
CREATE SCHEMA "weird""name";
SET ROLE mere_mortal;
CREATE EXTENSION pg_stat_statements WITH SCHEMA "weird""name";
RESET ROLE;
DROP SCHEMA "weird""name";

-- Sanity: a plain schema name should not trip the CVE check. Clean up any
-- leftovers from earlier regressions in the same cluster first so the
-- after-create script can run cleanly.
DROP EXTENSION IF EXISTS pg_stat_statements;
DROP ROLE IF EXISTS stat_resetters;
CREATE SCHEMA ok_schema;
SET ROLE mere_mortal;
CREATE EXTENSION pg_stat_statements WITH SCHEMA ok_schema;
RESET ROLE;
DROP EXTENSION pg_stat_statements;
DROP ROLE stat_resetters;
DROP SCHEMA ok_schema;
