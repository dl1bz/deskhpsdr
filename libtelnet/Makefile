#   Copyright (C)
#   2024,2025 - Heiko Amft, DL1BZ (Project deskHPSDR)
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

UNAME := $(shell uname)
UNAME_S := $(shell uname -s)

# Build-Target für statische libtelnet.a
CFLAGS  := -Wall -Wextra -O3 -Wno-parentheses
AR      := ar
ARFLAGS := rcs

ifeq ($(UNAME_S),Darwin)
# SDKROOT := $(shell xcrun --sdk macosx --show-sdk-path)
# ARCH_FLAGS := -arch arm64 -arch x86_64
# CFLAGS += -mmacosx-version-min=13.0 $(ARCH_FLAGS) -isysroot $(SDKROOT) -I/usr/local/include
CFLAGS += -mmacosx-version-min=13.0
endif

OBJS := libtelnet.o

all: libtelnet.a

libtelnet.o: libtelnet.c libtelnet.h
	$(CC) $(CFLAGS) -c libtelnet.c -o libtelnet.o

libtelnet.a: $(OBJS)
	$(AR) $(ARFLAGS) libtelnet.a $(OBJS)

clean:
	rm -f $(OBJS) libtelnet.a
