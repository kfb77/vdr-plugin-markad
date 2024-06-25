#
# Makefile for a Video Disk Recorder addon
#

ifdef VERBOSE
Q =
else
Q = @
endif
export Q

# use this, if you want to debug heap memory consumption
# DEBUG_MEM_MARKAD=1

### Dependencies:
PKG_CONFIG ?= pkg-config

STRIP ?= strip

### The version number of this plugin (taken from the main source file):
$(shell GITVERSION=`git rev-parse --short HEAD 2> /dev/null`; if [ "$$GITVERSION" ]; then sed "s/\";/ ($$GITVERSION)\";/" ../version.dist > ../version.h; else cp ../version.dist ../version.h; fi)
VERSION = $(shell grep 'static const char \*VERSION *=' ../version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

# check if we want to build without vdr
ifndef NO_VDR
# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG   = $(if $(VDRDIR),$(shell $(PKG_CONFIG) --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../../.." $(PKG_CONFIG) --variable=$(1) vdr))
BINDIR   = $(call PKGCFG,bindir)
MANDIR   = $(call PKGCFG,mandir)
LIBDIR   = $(call PKGCFG,libdir)
LOCDIR   = $(call PKGCFG,locdir)
PLGCFG   = $(call PKGCFG,plgcfg)
CFGDIR   = $(call PKGCFG,configdir)
CXXFLAGS = $(call PKGCFG,cxxflags)
endif

### set defaults
BINDIR   ?= /usr/bin
LOCDIR   ?= /usr/share/locale
MANDIR   ?= /usr/share/man
CXX      ?= g++
CXXFLAGS ?= -g -fPIC -rdynamic -O3 -funroll-loops -Wall -Woverloaded-virtual -Wno-parentheses

### compiler version dependent options
CXXVERSION=$(shell $(CXX) -dumpversion | cut -d"." -f1)
$(info compiler: ${CXX})
$(info version:  ${CXXVERSION})
DEFINES += -DCXXVERSION=${CXXVERSION}
ifeq ($(CXXVERSION),4)
	export CXXFLAGS += -std=c++11 -Wno-missing-field-initializers # to prevent a lot of compiler warnings with g++ V4
        $(warning your compiler is deprecated, please update)
endif
ifeq ($(CXXVERSION),5)
	export CXXFLAGS += -std=c++11   # to prevent compiler errors with g++ V5
endif


### Includes and Defines (add further entries here):
PKG-LIBS += libavcodec libavutil libavformat libavfilter libswresample libswscale
PKG-INCLUDES += libavcodec libavutil libavformat libavfilter libswresample libswscale

DEFINES += -D_GNU_SOURCE
DEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
DEFINES += -D__STDC_CONSTANT_MACROS -D__USE_XOPEN_EXTENDED


# more debug compiler options
ifdef DEBUG_MEM_MARKAD
DEFINES += -DDEBUG_MEM
$(info option DEBUG_MEM_MARKAD is set)
endif

ifdef DEBUG_MARK_FRAMES
DEFINES += -DDEBUG_MARK_FRAMES=$(DEBUG_MARK_FRAMES)
$(info option DEBUG_MARK_FRAMES is set)
endif

ifdef DEBUG_SANITIZER
export CXXFLAGS += -fsanitize=address -fsanitize=leak -fsanitize=undefined
$(info sanitize option is set)
endif


INCLUDES += $(shell $(PKG_CONFIG) --cflags $(PKG-INCLUDES))
LIBS     += $(shell $(PKG_CONFIG) --libs $(PKG-LIBS)) -pthread


### The object files (add further files here):
OBJS = markad-standalone.o marks.o video.o audio.o decoder.o encoder.o logo.o debug.o index.o evaluate.o osd.o criteria.o vps.o tools.o overlap.o sobel.o

### The main target:
all: markad i18n

### Implicit rules:
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<


### debug heap allocations
ifdef DEBUG_MEM_MARKAD
DEFINES += -DDEBUG_MEM
$(info option DEBUG_MEM_MARKAD is set)
endif

INCLUDES += $(shell $(PKG_CONFIG) --cflags $(PKG-INCLUDES))
LIBS     += $(shell $(PKG_CONFIG) --libs $(PKG-LIBS)) -pthread


### The main target:
all: markad i18n

### Implicit rules:
%.o: %.cpp
	@echo CC $@
	$(Q)$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) $<

### Dependencies:
MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

### Internationalization (I18N):
PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmsgs  = $(addprefix  $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/markad.mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/markad.pot

%.mo: %.po
	@echo MO $@
	$(Q)msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.cpp *.h)
	@echo GT $@
	$(Q)xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<see README>' -o $@ $^

%.po: $(I18Npot)
	@echo PO $@
	$(Q)msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/markad.mo: $(PODIR)/%.mo
	@echo IN $@
	$(Q)install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Npot)

### Targets:
markad: $(OBJS)
	@echo CC $@
	$(Q)$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJS) $(LIBS) -o $@


install-doc:
	@mkdir -p $(DESTDIR)$(MANDIR)/man1
	@gzip -c markad.1 > $(DESTDIR)$(MANDIR)/man1/markad.1.gz

install: install-doc markad $(I18Nmsgs)
	@mkdir -p $(DESTDIR)$(BINDIR)
	install -D markad $(DESTDIR)$(BINDIR)/markad
	@mkdir -p $(DESTDIR)/var/lib/markad
	install -D logos/* $(DESTDIR)/var/lib/markad
	@echo markad installed

clean:
	@-rm -f $(OBJS) $(DEPFILE) markad *.so *.so.* *.tgz core* *~ $(PODIR)/*.mo $(PODIR)/*.pot

doxygen:
	doxygen doxygen.conf
