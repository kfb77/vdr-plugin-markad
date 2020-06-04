#
# Makefile for a Video Disk Recorder addon
#

# use this, if you want to build with the new cDecoder class only
ONLY_WITH_CDECODER=1

# use this, if you want to debug heap memory consumption
DEBUGMEMMARKAD=1

# use this, if you use libavcodec51
# WITH_OLD_FFMPEG_HEADERS=1

### The version number of this plugin (taken from the main source file):

$(shell GITVERSION=`git rev-parse --short HEAD 2> /dev/null`; if [ "$$GITVERSION" ]; then sed "s/\";/ ($$GITVERSION)\";/" ../version.dist > ../version.h; else cp ../version.dist ../version.h; fi)
VERSION = $(shell grep 'static const char \*VERSION *=' ../version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../../.." pkg-config --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
CFGDIR = $(call PKGCFG,configdir)

### The C++ compiler and options:
CXX      ?= g++
export CXXFLAGS ?= $(call PKGCFG,cxxflags)
export CXXFLAGS ?= -g -rdynamic -O3 -funroll-loops -Wall -Wextra -Woverloaded-virtual -Wno-parentheses

DEBUG = $(filter -g ,$(CXXFLAGS))   # add -rdynamic if debug -g is set
ifneq ($(DEBUG),)
	export CXXFLAGS += -rdynamic
endif

CXXVERION=$(shell $(CXX) -dumpversion | cut -d"." -f1)
ifeq ($(CXXVERION),4)
	export CXXFLAGS += -std=c++11   # to prevent a lot of compiler warnings with g++ V4
endif
ifeq ($(CXXVERION),5)
	export CXXFLAGS += -std=c++11   # to prevent compiler errors with g++ V5
endif
#
PKG-CONFIG ?= pkg-config
STRIP ?= strip

### Includes and Defines (add further entries here):
PKG-LIBS += libavcodec libavutil libavformat libavfilter
PKG-INCLUDES += libavcodec libavutil libavformat libavfilter

DEFINES += -D_GNU_SOURCE
DEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
DEFINES += -D__STDC_CONSTANT_MACROS -D__USE_XOPEN_EXTENDED


ifdef DEBUGMEMMARKAD
DEFINES += -DDEBUGMEM
$(info option DEBUGMEMMARKAD is set)
endif


ifdef WITH_OLD_FFMPEG_HEADERS
	DEFINES += -DUSE_OLD_FFMPEG_HEADERS
endif


INCLUDES += $(shell $(PKG-CONFIG) --cflags $(PKG-INCLUDES))
LIBS     += $(shell $(PKG-CONFIG) --libs $(PKG-LIBS)) -pthread


### The object files (add further files here):
OBJS = markad-standalone.o marks.o video.o audio.o decoder_new.o encoder_new.o logo.o debug.o
ifdef ONLY_WITH_CDECODER
        DEFINES += -DONLY_WITH_CDECODER
else
	OBJS += demux.o decoder.o streaminfo.o
endif


### The main target:

all: markad i18n

### Implicit rules:

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Dependencies:

MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

### Internationalization (I18N):

PODIR     = po
LOCALEDIR = /usr/share/locale
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmsgs  = $(addprefix  $(DESTDIR)$(LOCALEDIR)/, $(addsuffix /LC_MESSAGES/markad.mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/markad.pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.cpp *.h)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<see README>' -o $@ $^

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCALEDIR)/%/LC_MESSAGES/markad.mo: $(PODIR)/%.mo
	install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Npot)

### Targets:

markad: $(OBJS)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $@


MANDIR	= $(DESTDIR)/usr/share/man
install-doc:
	@mkdir -p $(MANDIR)/man1
	@gzip -c markad.1 > $(MANDIR)/man1/markad.1.gz

install: install-doc markad $(I18Nmsgs)
	@mkdir -p $(DESTDIR)/usr/bin
	install -D markad $(DESTDIR)/usr/bin/markad
	@mkdir -p $(DESTDIR)/var/lib/markad
	install -D logos/* $(DESTDIR)/var/lib/markad
	@echo markad installed

clean:
	@-rm -f $(OBJS) $(DEPFILE) markad *.so *.so.* *.tgz core* *~ $(PODIR)/*.mo $(PODIR)/*.pot
