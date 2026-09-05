# Makefile for the WDSP library on Linux/macOS.
# Creates libwdsp.a for static linking.

CC ?= cc
AR ?= ar
RANLIB ?= ranlib

CFLAGS ?= -O3
CFLAGS += -pthread -D_GNU_SOURCE -Wno-parentheses -Wcast-align
CFLAGS += -I../wdsp-libs/include

FFTW_LOCAL_PREFIX ?= ../fftw-3.3.11/build

ifneq ($(wildcard $(FFTW_LOCAL_PREFIX)/include/fftw3.h),)
$(info Local FFTW 3.3.11 found, using project header.)
FFTWINCLUDE := -I$(FFTW_LOCAL_PREFIX)/include
else
$(info Local FFTW 3.3.11 not found, using system FFTW via pkg-config.)
FFTWINCLUDE := $(shell pkg-config --cflags fftw3)
endif
COMPILE = $(CC) $(CFLAGS) $(FFTWINCLUDE)

SOURCES = \
	linux_port.c \
	RXA.c TXA.c amd.c ammod.c amsq.c analyzer.c anf.c anr.c apfshadow.c \
	bandpass.c calcc.c calculus.c cblock.c cfcomp.c cfir.c channel.c cmath.c \
	compress.c delay.c dexp.c div.c doublepole.c eer.c emnr.c emph.c eq.c \
	extrapolate.c fcurve.c fir.c firmin.c fmd.c fmmod.c fmsq.c gain.c gaussian.c \
	gen.c icfir.c iir.c impulse_cache.c iobuffs.c iqc.c lmath.c main.c matchedCW.c \
	meter.c meterlog10.c nbp.c nnet.c nnio.c nnr.c nnr_model_0.c nnr_model_1.c \
	rnnr.c sbnr.c \
	nob.c nobII.c nurbs.c nurbs_fit.c nurbs_spline.c osctrl.c patchpanel.c phrot.c \
	resample.c reshb.c rmatch.c sender.c shift.c siphon.c slew.c snb.c ssql.c \
	syncbuffs.c utilities.c varsamp.c version.c wbfm.c wcpAGC.c wisdom.c zetaHat.c

OBJS = $(SOURCES:.c=.o)

all: libwdsp.a

libwdsp.a: $(OBJS)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

%.o: %.c
	$(COMPILE) -c -o $@ $<

make_interface: make_interface.c
	$(CC) $(CFLAGS) -o $@ $<

interface: make_interface
	./make_interface $(filter-out linux_port.c make_interface.c,$(SOURCES)) > wdsp-interface.h

clean:
	$(RM) libwdsp.a $(OBJS) make_interface wdsp-interface.h

.PHONY: all clean interface
