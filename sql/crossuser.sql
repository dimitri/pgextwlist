GRANT USAGE, CREATE ON SCHEMA public TO public;

DO LANGUAGE plpgsql $$
BEGIN
    CREATE ROLE evil_user;
EXCEPTION WHEN duplicate_object THEN
    NULL;
END $$;

SET ROLE mere_mortal;
CREATE TABLE mere_table (t citext);

SET ROLE evil_user;
DROP EXTENSION citext;
DROP EXTENSION citext CASCADE;
