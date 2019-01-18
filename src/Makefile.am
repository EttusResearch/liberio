lib_LTLIBRARIES = liberio.la

liberio_la_SOURCES = log.c liberio.c liberio-util.c liberio-userptr.c liberio-mmap.c
liberio_la_CPPFLAGS = -I$(top_srcdir)/include -D_GNU_SOURCE
liberio_la_LDFLAGS = -version-info 3:5:0 -ludev
