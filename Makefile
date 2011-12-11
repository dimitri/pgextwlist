MODULES = pgextwlist
DOCS    = README.asciidoc

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
