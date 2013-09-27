MODULE_big = pgextwlist
OBJS       = utils.o pgextwlist.o
DOCS       = README.md

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
