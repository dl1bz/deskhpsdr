UNAME := $(shell uname)

CFLAGS?=-O3 -D_GNU_SOURCE -Wno-parentheses
CFLAGS+= `pkg-config --cflags libcurl libxml-2.0`

LIBS = `pkg-config --libs libcurl libxml-2.0`

all: libsolar.a

libsolar.a: solar.o
	ar rv libsolar.a solar.o
	ranlib libsolar.a

solar.o: solar.c solar.h
	$(CC) $(CFLAGS) -c solar.c -o solar.o

example: main.c libsolar.a
	$(CC) $(CFLAGS) main.c -L. -lsolar $(LIBS) -o example

clean:
	rm -f *.o *.a example
