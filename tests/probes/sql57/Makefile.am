EXTRA_DIST = $(top_srcdir)/tests/test_common.sh

DISTCLEANFILES = \
	*.log \
	oscap_debug.log.* \
	results.xml
CLEANFILES = \
	*.log \
	oscap_debug.log.* \
	*results.xml

TESTS_ENVIRONMENT = \
		builddir=$(top_builddir) \
		OSCAP_FULL_VALIDATION=1 \
		$(top_builddir)/run

TESTS = all.sh

EXTRA_DIST += \
	all.sh \
	unsupported_engine.oval.xml \
	unsupported_engine.sh
