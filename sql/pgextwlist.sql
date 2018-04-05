CREATE ROLE mere_mortal;
ALTER DATABASE contrib_regression OWNER TO mere_mortal;
SET ROLE mere_mortal;

SHOW extwlist.extensions;

SELECT extname FROM pg_extension ORDER BY 1;

-- pre-existing extension
CREATE EXTENSION plpgsql;
SELECT extname FROM pg_extension ORDER BY 1;

-- non-whitelisted extension
CREATE EXTENSION hstore;
SELECT extname FROM pg_extension ORDER BY 1;

-- whitelisted extensions
CREATE EXTENSION citext;
CREATE EXTENSION pg_trgm;
SELECT extname FROM pg_extension ORDER BY 1;

-- whitelisted extension, but dependency is missing
CREATE EXTENSION earthdistance;
SELECT extname FROM pg_extension ORDER BY 1;

-- drop whitelisted extension
DROP EXTENSION pg_trgm;
SELECT extname FROM pg_extension ORDER BY 1;

-- drop non-whitelisted extension
DROP EXTENSION plpgsql;
SELECT extname FROM pg_extension ORDER BY 1;
