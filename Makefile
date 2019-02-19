short_ver = 1.8
long_ver = $(shell (git describe --tags --long '--match=v*' 2>/dev/null || echo $(short_ver)-0-unknown) | cut -c2-)

MODULE_big = pgextwlist
OBJS       = utils.o pgextwlist.o
DOCS       = README.md
REGRESS    = pgextwlist crossuser

PG_CONFIG = pg_config
PGXS = $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

DEBUILD_ROOT = /tmp/pgextwlist

deb:
	mkdir -p $(DEBUILD_ROOT) && rm -rf $(DEBUILD_ROOT)/*
	rsync -Ca --exclude=build/* ./ $(DEBUILD_ROOT)/
	cd $(DEBUILD_ROOT) && make -f debian/rules orig
	cd $(DEBUILD_ROOT) && debuild -us -uc -sa
	cp -a /tmp/pgextwlist_* /tmp/postgresql-9.* build/

rpm:
	git archive --output=pgextwlist-rpm-src.tar.gz --prefix=pgextwlist/ HEAD
	rpmbuild -bb pgextwlist.spec \
		--define '_sourcedir $(CURDIR)' \
		--define 'package_prefix $(package_prefix)' \
		--define 'pkglibdir $(shell $(PG_CONFIG) --pkglibdir)' \
		--define 'major_version $(short_ver)' \
		--define 'minor_version $(subst -,.,$(subst $(short_ver)-,,$(long_ver)))'
	$(RM) pgextwlist-rpm-src.tar.gz
