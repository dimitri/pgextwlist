CREATE ROLE evil_user;

SET ROLE mere_mortal;
CREATE TABLE mere_table (t citext);

SET ROLE evil_user;
DROP EXTENSION citext;
DROP EXTENSION citext CASCADE;
