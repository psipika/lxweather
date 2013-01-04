lib_LTLIBRARIES= libweather.la

libweather_la_SOURCES=         \
 $(top_srcdir)/src/logutil.c   \
 $(top_srcdir)/src/yahooutil.c \
 $(top_srcdir)/src/fileutil.c  \
 $(top_srcdir)/src/httputil.c  \
 $(top_srcdir)/src/location.c  \
 $(top_srcdir)/src/forecast.c  \
 $(top_srcdir)/src/weatherwidget.c \
 weather.c

INCLUDES=  \
 -I$(top_srcdir)/src

libweather_la_CPPFLAGS=     \
 $(DEPENDENCIES_CFLAGS)

libweather_la_LDFLAGS=      \
 $(DEPENDENCIES_LIBS)

install-exec-hook:
	find . -regex ".*.\(l\)?a"
