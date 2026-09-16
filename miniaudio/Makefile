# Static miniaudio library for deskHPSDR.
# Builds only the device I/O subset required by deskHPSDR.

UNAME_S := $(shell uname -s)
ARCH    := $(shell uname -m)

CC      ?= cc
AR      ?= ar
RANLIB  ?= ranlib

CFLAGS  ?= -O2
CFLAGS  += -I. -DMA_NO_DECODING -DMA_NO_ENCODING \
           -DMA_NO_RESOURCE_MANAGER -DMA_NO_NODE_GRAPH -DMA_NO_ENGINE \
           -DMA_NO_GENERATION -DMA_ENABLE_ONLY_SPECIFIC_BACKENDS

ifeq ($(ARCH),arm64)
  CFLAGS += -mcpu=native
else ifeq ($(ARCH),aarch64)
  CFLAGS += -mcpu=native
else ifeq ($(ARCH),x86_64)
  CFLAGS += -march=native
endif

ifeq ($(UNAME_S),Darwin)
  CFLAGS += -DMA_NO_RUNTIME_LINKING -DMA_ENABLE_COREAUDIO
else ifeq ($(UNAME_S),Linux)
  CFLAGS += -DMA_ENABLE_ALSA -DMA_ENABLE_PULSEAUDIO
else
  $(error Unsupported host OS '$(UNAME_S)' for this miniaudio Makefile)
endif

TARGET = libminiaudio.a
OBJS   = miniaudio.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

miniaudio.o: miniaudio.c miniaudio.h
	$(CC) $(CFLAGS) -c miniaudio.c -o $@

clean:
	rm -f $(OBJS) $(TARGET)
