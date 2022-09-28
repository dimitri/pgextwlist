SET ROLE mere_mortal;

SHOW extwlist.extensions;

SELECT extname FROM pg_extension ORDER BY 1;

-- whitelisted extensions
CREATE EXTENSION citext;
CREATE EXTENSION pg_trgm;
COMMENT ON EXTENSION pg_trgm IS 'pg_trgm comment';
SELECT extname FROM pg_extension ORDER BY 1;
SELECT d.description FROM pg_extension e JOIN pg_description d ON d.objoid = e.oid WHERE e.extname = 'pg_trgm';

-- drop whitelisted extension
DROP EXTENSION pg_trgm;
SELECT extname FROM pg_extension ORDER BY 1;

-- whitelisted extension with custom scripts
CREATE EXTENSION pg_stat_statements;
SELECT extname FROM pg_extension ORDER BY 1;
SELECT proacl FROM pg_proc WHERE proname = 'pg_stat_statements_reset';
\du stat_resetters
DROP EXTENSION pg_stat_statements;
\du stat_resetters
