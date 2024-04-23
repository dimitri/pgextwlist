ALTER FUNCTION pg_stat_statements_reset OWNER TO mere_mortal;
CREATE ROLE stat_resetters2;
GRANT EXECUTE ON FUNCTION pg_stat_statements_reset TO stat_resetters2;
