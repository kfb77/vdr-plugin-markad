#
# Makefile for a Video Disk Recorder addon
#

# use this, if you use libavcodec51
#WITH_OLD_FFMPEG_HEADERS=1

### The version number of this plugin (taken from the main source file):

$(shell GITVERSION=`git rev-parse --short HEAD 2> /dev/null`; if [ "$$GITVERSION" ]; then sed "s/\";/ ($$GITVERSION)\";/" ../version.dist > ../version.h; else cp ../version.dist ../version.h; fi)
VERSION = $(shell grep 'static const char \*VERSION *=' ../version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The C++ compiler and options:

CXX      ?= g++
CXXFLAGS ?= -g -rdynamic -O3 -funroll-loops -Wall -Wextra -Woverloaded-virtual -Wno-parentheses
PKG-CONFIG ?= pkg-config
STRIP ?= strip

### Includes and Defines (add further entries here):

PKG-LIBS += libavcodec libavutil
PKG-INCLUDES += libavcodec libavutil

DEFINES += -D_GNU_SOURCE 
DEFINES += -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE
DEFINES += -D__STDC_CONSTANT_MACROS -D__USE_XOPEN_EXTENDED

ifdef WITH_OLD_FFMPEG_HEADERS
	DEFINES += -DUSE_OLD_FFMPEG_HEADERS
endif


INCLUDES += $(shell $(PKG-CONFIG) --cflags $(PKG-INCLUDES))
LIBS     += $(shell $(PKG-CONFIG) --libs $(PKG-LIBS)) -pthread

### The object files (add further files here):

OBJS = markad-standalone.o decoder.o marks.o streaminfo.o video.o audio.o demux.o 

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
LOCALEDIR = $(DESTDIR)/usr/share/locale
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmsgs  = $(addprefix $(LOCALEDIR)/, $(addsuffix /LC_MESSAGES/markad.mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/markad.pot

%.mo: %.po
	msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.cpp *.h)
	xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --msgid-bugs-address='<see README>' -o $@ $^

%.po: $(I18Npot)
	msgmerge -U --no-wrap --no-location --backup=none -q $@ $<
	@touch $@

$(I18Nmsgs): $(LOCALEDIR)/%/LC_MESSAGES/markad.mo: $(PODIR)/%.mo
	@mkdir -p $(dir $@)
	cp $< $@

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
	@cp --remove-destination markad $(DESTDIR)/usr/bin/markad
	@$(STRIP) $(DESTDIR)/usr/bin/markad
	@mkdir -p $(DESTDIR)/var/lib/markad
	@cp -u logos/* $(DESTDIR)/var/lib/markad
	@echo markad installed

clean:
	@-rm -f $(OBJS) $(DEPFILE) markad *.so *.so.* *.tgz core* *~ $(PODIR)/*.mo $(PODIR)/*.pot
