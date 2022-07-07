LOAD 'pgextwlist';
ALTER SYSTEM SET session_preload_libraries='pgextwlist';
ALTER SYSTEM SET extwlist.extensions='citext,earthdistance,pg_trgm,pg_stat_statements';
\set testdir `pwd` '/test-scripts'
ALTER SYSTEM SET extwlist.custom_path=:'testdir';
SELECT pg_reload_conf();
