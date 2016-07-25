bin_PROGRAMS = dma-loopback dma_test dma_test_recv

dma_loopback_SOURCES = dma-loopback.c
dma_loopback_LDADD = -lusrpdma -lpthread

dma_test_SOURCES = dma_test.c
dma_test_LDADD = -lusrpdma -lpthread

dma_test_recv_SOURCES = dma_test_recv.c
dma_test_recv_LDADD = -lusrpdma -lpthread

lib_LTLIBRARIES = libusrpdma.la
libusrpdma_la_SOURCES = dma.c log.c crc32.c
libusrpdma_la_CFLAGS = -D_GNU_SOURCE

