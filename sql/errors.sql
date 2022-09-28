SELECT case
  when setting::int >= 130000 then 'PG 13+'
  when setting::int >= 100000 then 'PG 10..12'
  end as "regression output for PG version"
FROM pg_settings where name = 'server_version_num';

SET ROLE mere_mortal;

SELECT extname FROM pg_extension ORDER BY 1;

-- pre-existing extension
CREATE EXTENSION plpgsql;
COMMENT ON EXTENSION hstore IS 'plpgsql comment';
SELECT extname FROM pg_extension ORDER BY 1;

-- non-whitelisted extension
CREATE EXTENSION hstore;
COMMENT ON EXTENSION hstore IS 'hstore comment';
SELECT extname FROM pg_extension ORDER BY 1;

-- whitelisted extension, but dependency is missing
CREATE EXTENSION earthdistance;
SELECT extname FROM pg_extension ORDER BY 1;

-- drop non-whitelisted extension
DROP EXTENSION plpgsql;
SELECT extname FROM pg_extension ORDER BY 1;
