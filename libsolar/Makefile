# Copyright (C)
# 2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
#
#   This program is free software: you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation, either version 3 of the License, or
#   (at your option) any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program.  If not, see <https://www.gnu.org/licenses/>.
#
UNAME := $(shell uname)
UNAME_S := $(shell uname -s)

CFLAGS?=-O3 -D_GNU_SOURCE -Wno-parentheses
CFLAGS+= `pkg-config --cflags libcurl libxml-2.0`
LIBS = `pkg-config --libs libcurl libxml-2.0`

ifeq ($(UNAME_S),Darwin)
# SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path)
# ARCH_FLAGS := -arch arm64 -arch x86_64
# CFLAGS += -mmacosx-version-min=13.0 $(ARCH_FLAGS) -isysroot $(SDKROOT) -I/usr/local/include
CFLAGS += -mmacosx-version-min=14.0
endif

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
