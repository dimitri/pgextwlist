select case
  when setting::int >= 160000 then 'PG 16+'
  end as "regression output for PG version"
from pg_settings where name = 'server_version_num';

set client_min_messages = debug;
set extwlist.custom_path = '/dummy';
set role mere_mortal;

-- in the hope refint stays at version 1.0 in PostgreSQL
create extension refint;
alter extension refint update;
comment on extension refint is 'snarky remark';
drop extension refint, refint;
