CREATE ROLE evil_user;
ALTER DATABASE contrib_regression OWNER TO mere_mortal;

SET ROLE mere_mortal;
CREATE TABLE mere_table (t citext);

SET ROLE evil_user;
DROP EXTENSION citext;
DROP EXTENSION citext CASCADE;
