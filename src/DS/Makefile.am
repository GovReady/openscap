
noinst_LTLIBRARIES = libds.la

libds_la_SOURCES = ds_common.c \
		   ds_common.h \
		   sds.c \
		   sds_index.c \
		   rds.c \
		   rds_index.c

libds_la_CPPFLAGS  = @xml2_CFLAGS@ \
		     -I$(top_srcdir)/src \
		     -I$(top_srcdir)/src/DS/public \
		     -I$(top_srcdir)/src/DS \
		     -I$(top_srcdir)/src/XCCDF \
		     -I$(top_srcdir)/src/OVAL \
		     -I$(top_srcdir)/src/CPE/public \
		     -I$(top_srcdir)/src/common \
		     -I$(top_srcdir)/src/common/public \
		     -I$(top_srcdir)/lib

libds_la_LDFLAGS = @xml2_LIBS@

pkginclude_HEADERS  = public/scap_ds.h

