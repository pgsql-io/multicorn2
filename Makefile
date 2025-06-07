srcdir       = .
MODULE_big   = multicorn
OBJS         =  src/errors.o src/python.o src/query.o src/multicorn.o


DATA         = $(filter-out $(wildcard sql/*--*.sql),$(wildcard sql/*.sql))

DOCS         = $(wildcard $(srcdir)/doc/*.md)

EXTENSION    = multicorn
EXTVERSION   = $(shell grep default_version $(srcdir)/$(EXTENSION).control | sed -e "s/default_version[[:space:]]*=[[:space:]]*'\([^']*\)'/\1/")

all: sql/$(EXTENSION)--$(EXTVERSION).sql

directories.stamp:
	[ -d sql ] || mkdir sql
	[ -d src ] || mkdir src
	touch $@

$(OBJS): directories.stamp

install: python_code

sql/$(EXTENSION)--$(EXTVERSION).sql: sql/$(EXTENSION).sql directories.stamp
	cp $< $@

preflight-check:
	$(srcdir)/preflight-check.sh

python_code: setup.py pyproject.toml
	$(eval python_major_version := $(shell echo ${python_version} | cut -d '.' -f 1))
	$(eval PIP ?= $(shell ([ -x "$$(command -v pip${python_version})" ] && echo pip${python_version}) || ([ -x "$$(command -v pip${python_major_version})" ] && echo pip${python_major_version}) || echo pip))
	#
	# Strictly speaking, --break-system-packages arrived in 23.0.1, but that's
	# too hard to check for.
	#
	$(eval PIP_VERSION := $(shell $(PIP) --version 2>&1 | cut -d ' ' -f 2 | cut -d '.' -f 1))
	$(eval PIP_FLAGS := $(shell [ $(PIP_VERSION) -ge 23 ] && echo "--break-system-packages --ignore-installed"))
	#
	# Workaround https://github.com/pgsql-io/multicorn2/issues/34, and then
	# re-evaluate PIP_VERSION/PIP_FLAGS.
	#
	$(PIP) install $(PIP_FLAGS) --upgrade 'pip>=23'
	$(eval PIP_VERSION := $(shell $(PIP) --version 2>&1 | cut -d ' ' -f 2 | cut -d '.' -f 1))
	$(eval PIP_FLAGS := $(shell [ $(PIP_VERSION) -ge 23 ] && echo "--break-system-packages --ignore-installed"))
	$(PIP) install $(PIP_FLAGS) .

release-zip: all
	git archive --format zip --prefix=multicorn-$(EXTVERSION)/ --output ./multicorn-$(EXTVERSION).zip HEAD
	unzip ./multicorn-$(EXTVERSION).zip
	rm ./multicorn-$(EXTVERSION).zip
	sed -i -e "s/__VERSION__/$(EXTVERSION)/g"  ./multicorn-$(EXTVERSION)/META.json ./multicorn-$(EXTVERSION)/python/multicorn/__init__.py
	zip -r ./multicorn-$(EXTVERSION).zip ./multicorn-$(EXTVERSION)/
	rm ./multicorn-$(EXTVERSION) -rf

coverage:
	lcov -d . -c -o lcov.info --no-external
	genhtml --show-details --legend --output-directory=coverage --title="Multicorn Code Coverage" --no-branch-coverage --num-spaces=4 --prefix=./src/ `find . -name lcov.info -print`

DATA = sql/$(EXTENSION)--$(EXTVERSION).sql
EXTRA_CLEAN = sql/$(EXTENSION)--$(EXTVERSION).sql ./multicorn-$(EXTVERSION).zip directories.stamp
PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
REGRESS      = virtual_tests

include $(PGXS)

with_python_no_override = no

ifeq ($(with_python),yes)
	with_python_no_override = yes
endif

ifdef PYTHON_OVERRIDE
	with_python_no_override = no
endif


ifeq ($(with_python_no_override),yes)
	SHLIB_LINK = $(python_libspec) $(python_additional_libs) $(filter -lintl,$(LIBS))
	override CPPFLAGS := -I. -I$(srcdir) $(python_includespec) $(CPPFLAGS)
	override PYTHON = python${python_version}
else
	ifdef PYTHON_OVERRIDE
		override PYTHON = ${PYTHON_OVERRIDE}
	endif

	ifeq (${PYTHON}, )
		override PYTHON = python
	endif


	python_version = $(shell ${PYTHON} --version 2>&1 | cut -d ' ' -f 2 | cut -d '.' -f 1-2)
	PYTHON_CONFIG ?= python${python_version}-config

	PY_LIBSPEC = $(shell ${PYTHON_CONFIG} --embed >/dev/null && ${PYTHON_CONFIG} --libs --embed || ${PYTHON_CONFIG} --libs)
	PY_INCLUDESPEC = $(shell ${PYTHON_CONFIG} --includes)
	PY_CFLAGS = $(shell ${PYTHON_CONFIG} --cflags)
	PY_LDFLAGS = $(shell ${PYTHON_CONFIG} --ldflags)
	SHLIB_LINK += $(PY_LIBSPEC) $(PY_LDFLAGS) $(PY_ADDITIONAL_LIBS) $(filter -lintl,$(LIBS))
	override PG_CPPFLAGS  := $(PY_INCLUDESPEC) $(PG_CPPFLAGS)
	override CPPFLAGS := $(PG_CPPFLAGS) $(CPPFLAGS)
endif

ifeq ($(PORTNAME),Darwin)
	override LDFLAGS += -undefined dynamic_lookup -bundle_loader $(shell $(PG_CONFIG) --bindir)/postgres
endif

PYTHON_TEST_VERSION ?= $(python_version)
PG_TEST_VERSION ?= $(MAJORVERSION)
UNSUPPORTS_SQLALCHEMY=$(shell python -c "import sqlalchemy;import psycopg2"  1> /dev/null 2>&1; echo $$?)

# Check if Python version is 3.12 or greater
PYTHON_VERSION_MAJOR_MINOR=$(shell python -c "import sys; print(f'{sys.version_info.major}.{sys.version_info.minor}')")
PYTHON_GE_312=$(shell python -c "import sys; print(1 if (sys.version_info.major, sys.version_info.minor) >= (3, 12) else 0)")

TESTS        = test-$(PYTHON_TEST_VERSION)/sql/multicorn_cache_invalidation.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_column_options_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_error_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_logger_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_planner_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_regression_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_sequence_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_test_date.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_test_dict.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_test_list.sql \
  test-$(PYTHON_TEST_VERSION)/sql/multicorn_test_sort.sql \
  test-$(PYTHON_TEST_VERSION)/sql/write_savepoints.sql \
  test-$(PYTHON_TEST_VERSION)/sql/write_test.sql \
  test-$(PYTHON_TEST_VERSION)/sql/import_test.sql

# Only include write_filesystem.sql for Python versions < 3.12
ifneq ($(PYTHON_GE_312), 1)
  TESTS += test-$(PYTHON_TEST_VERSION)/sql/write_filesystem.sql
endif

ifeq (${UNSUPPORTS_SQLALCHEMY}, 0)
  TESTS += test-$(PYTHON_TEST_VERSION)/sql/multicorn_alchemy_test.sql \
  	test-$(PYTHON_TEST_VERSION)/sql/write_sqlalchemy.sql \
  	test-$(PYTHON_TEST_VERSION)/sql/import_sqlalchemy.sql
endif

# Add tests for features only supported in PG14+
ifeq ($(shell test $(PG_TEST_VERSION) -ge 14; echo $$?),0)
  TESTS += test-$(PYTHON_TEST_VERSION)/sql/write_batch_test.sql
endif

REGRESS      = $(patsubst test-$(PYTHON_TEST_VERSION)/sql/%.sql,%,$(TESTS))
REGRESS_OPTS = --inputdir=test-$(PYTHON_TEST_VERSION) --encoding=UTF8 --host=localhost

$(info Python version is $(python_version))

# This is a copy of the "check" target from pgxs.mk, but it doesn't build the extension, just runs the tests.
easycheck:
		$(pg_regress_check) $(REGRESS_OPTS) $(REGRESS)
