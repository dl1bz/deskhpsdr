UNAME := $(shell uname)

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -O2 -I/usr/include/libxml2
LIBS = -lcurl -lxml2

ifeq ($(UNAME), Darwin)
	CFLAGS += -I/opt/homebrew/include
	LIBS += -L/opt/homebrew/lib
endif

all: libsolar.a example

libsolar.a: solar.o
	$(AR) rcs libsolar.a solar.o

solar.o: solar.c solar.h
	$(CC) $(CFLAGS) -c solar.c -o solar.o

example: main.c libsolar.a
	$(CC) $(CFLAGS) main.c -L. -lsolar $(LIBS) -o example

clean:
	rm -f *.o *.a example

