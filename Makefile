MODULES = pgextwlist
DOCS    = README.md

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
