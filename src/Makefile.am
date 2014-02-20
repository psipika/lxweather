bin_PROGRAMS= lxweather

lxweather_SOURCES= \
 main.c            \
 logutil.c         \
 yahooutil.c       \
 fileutil.c        \
 httputil.c        \
 location.c        \
 forecast.c        \
 weatherwidget.c 

lxweather_CPPFLAGS=     \
 $(DEPENDENCIES_CFLAGS)

lxweather_LDADD=      \
 $(DEPENDENCIES_LIBS) \
 -lpthread

EXTRA_DIST =         \
 logutil.h           \
 yahooutil.h         \
 httputil.h          \
 fileutil.h          \
 location.h          \
 forecast.h          \
 weatherwidget.h
