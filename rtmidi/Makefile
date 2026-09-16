# Static RtMidi library for deskHPSDR.
# Builds the native MIDI backend for macOS or Linux.

UNAME_S := $(shell uname -s)
ARCH    := $(shell uname -m)

CXX     ?= c++
AR      ?= ar
RANLIB  ?= ranlib

CXXFLAGS ?= -O2
CXXFLAGS += -std=c++11 -I.

ifeq ($(ARCH),arm64)
  CXXFLAGS += -mcpu=native
else ifeq ($(ARCH),aarch64)
  CXXFLAGS += -mcpu=native
else ifeq ($(ARCH),x86_64)
  CXXFLAGS += -march=native
endif

ifeq ($(UNAME_S),Darwin)
  CXXFLAGS += -D__MACOSX_CORE__
else ifeq ($(UNAME_S),Linux)
  CXXFLAGS += -D__LINUX_ALSA__
else
  $(error Unsupported host OS '$(UNAME_S)' for this RtMidi Makefile)
endif

TARGET = librtmidi.a
OBJS   = RtMidi.o rtmidi_c.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

RtMidi.o: RtMidi.cpp RtMidi.h
	$(CXX) $(CXXFLAGS) -c RtMidi.cpp -o $@

rtmidi_c.o: rtmidi_c.cpp rtmidi_c.h RtMidi.h
	$(CXX) $(CXXFLAGS) -c rtmidi_c.cpp -o $@

clean:
	rm -f $(OBJS) $(TARGET)

