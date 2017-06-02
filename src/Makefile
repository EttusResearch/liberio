OBJS = dma.o log.o
SHARED_LIB = liberio.so
STATIC_LIB = liberio.la
CFLAGS += -D_GNU_SOURCE

all: $(SHARED_LIB) $(STATIC_LIB)

%.o: %.c
	$(CC) -c -I../include -D_GNU_SOURCE -fPIC $(CFLAGS) -Wall -o $@ $<

$(STATIC_LIB): $(OBJS)
	$(AR) rcs $@ $+

$(SHARED_LIB): $(OBJS)
	$(CC) -shared -o $@ $+

clean:
	rm -f $(OBJS) $(SHARED_LIB) $(STATIC_LIB)

