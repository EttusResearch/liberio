BINS = chdr-recvcmdresponse chdr-sendcmd

all: $(BINS)

%.o: %.c
	$(CC) -c $(CFLAGS) -I../include -std=gnu99 -Wall -o $@ $<

chdr-recvcmdresponse : chdr-recvcmdresponse.o ../src/liberio.la
	$(CC) -o $@ $+

chdr-sendcmd : chdr-sendcmd.o ../src/liberio.la
	$(CC) -o $@ $+

clean:
	rm -f $(BINS) *.o

