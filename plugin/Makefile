#
# Makefile for a Video Disk Recorder plugin
# Adapted to the new VDR makefile environment by Stefan Hofmann
#
# $Id: $

# The official name of this plugin.
# This name will be used in the '-P...' option of VDR to load the plugin.
# By default the main source file also carries this name.

PLUGIN = markad

### Dependencies:
PKG_CONFIG ?= pkg-config

STRIP ?= strip

ifdef VERBOSE
Q =
else
Q = @
endif
export Q

# use this, if you want to debug heap memory consumption
# DEBUG_MEM_PLUGIN=1


### The version number of this plugin (taken from the version header file):
VERSION = $(shell grep 'static const char \*VERSION *=' version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

### The directory environment:
# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell $(PKG_CONFIG) --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../../.." $(PKG_CONFIG) --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
#
TMPDIR ?= /tmp

### The compiler options:
export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

### compiler version dependent options
CXXVERSION=$(shell $(CXX) -dumpversion | cut -d"." -f1)
$(info version:  ${CXXVERSION})
ifeq ($(CXXVERSION),4)
	export CXXFLAGS += -std=c++11   # to prevent a lot of compiler warnings with g++ V4
endif
ifeq ($(CXXVERSION),5)
        export CXXFLAGS += -std=c++11   # to prevent compiler errors with g++ V5
endif
ifeq ($(CXXVERSION),13)                 # prevent "warning: "_FORTIFY_SOURCE" redefined"
        ifneq (,$(findstring D_FORTIFY_SOURCE,$(CXXFLAGS)))
                export CXXFLAGS := -U_FORTIFY_SOURCE $(CXXFLAGS)   # append before options from vdr.pc
        endif
endif


### The version number of VDR's plugin API:
APIVERSION = $(call PKGCFG,apiversion)
$(info VDR plugin API version is ${APIVERSION})
APINUM = $(shell echo $(APIVERSION) | awk -F . '{printf "%d%02d%02d", $$1, $$2, $$3}')

# current minimum VDR API version 2.2.0
ifeq ($(shell expr ${APINUM} \< 20200), 1)
        $(error VDR plugin API version not supported, please update VDR)
        exit
endif
ifeq ($(shell expr ${APINUM} \< 20301), 1)
        $(warning VDR plugin API version is deprecated, please update VDR)
endif


### The name of the distribution archive:
ARCHIVE = $(PLUGIN)-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The name of the shared object file:
SOFILE = libvdr-$(PLUGIN).so

### Includes and Defines (add further entries here):
INCLUDES +=

DEFINES += -DPLUGIN_NAME_I18N='"$(PLUGIN)"'
ifdef DEBUG_MEM_PLUGIN
DEFINES += -DDEBUG_MEM
$(info option DEBUG_MEM_PLUGIN is set)
endif

### The object files (add further files here):
OBJS = $(PLUGIN).o status.o menu.o setup.o debug.o

### The main target:
all: $(SOFILE) i18n

### Implicit rules:
%.o: %.cpp
	@echo CC $@
	$(Q)$(CXX) $(CXXFLAGS) -c $(DEFINES) $(INCLUDES) -o $@ $<

### Dependencies:
MAKEDEP = $(CXX) -MM -MG
DEPFILE = .dependencies
$(DEPFILE): Makefile
	@$(MAKEDEP) $(DEFINES) $(INCLUDES) $(OBJS:%.o=%.cpp) > $@

-include $(DEPFILE)

### Internationalization (I18N):
PODIR     = po
I18Npo    = $(wildcard $(PODIR)/*.po)
I18Nmo    = $(addsuffix .mo, $(foreach file, $(I18Npo), $(basename $(file))))
I18Nmsgs  = $(addprefix $(DESTDIR)$(LOCDIR)/, $(addsuffix /LC_MESSAGES/vdr-$(PLUGIN).mo, $(notdir $(foreach file, $(I18Npo), $(basename $(file))))))
I18Npot   = $(PODIR)/$(PLUGIN).pot

%.mo: %.po
	@echo MO $@
	$(Q)msgfmt -c -o $@ $<

$(I18Npot): $(wildcard *.cpp *.h)
	@echo GT $@
	$(Q)xgettext -C -cTRANSLATORS --no-wrap --no-location -k -ktr -ktrNOOP --package-name=vdr-$(PLUGIN) --package-version=$(VERSION) --msgid-bugs-address='<vdr@dolze.de>' -o $@ `ls $^`

%.po: $(I18Npot)
	@echo PO $@
	$(Q)msgmerge -U --no-wrap --no-location --backup=none -q -N $@ $<
	@touch $@

$(I18Nmsgs): $(DESTDIR)$(LOCDIR)/%/LC_MESSAGES/vdr-$(PLUGIN).mo: $(PODIR)/%.mo
	@echo IN $@
	$(Q)install -D -m644 $< $@

.PHONY: i18n
i18n: $(I18Nmo) $(I18Npot)

install-i18n: $(I18Nmsgs)

### Targets:
$(SOFILE): $(OBJS)
	@echo LD $@
	$(Q)$(CXX) $(CXXFLAGS) $(LDFLAGS) -shared $(OBJS) $(LIBS) -o $@

install-lib: $(SOFILE)
	@echo IN $@
	$(Q)install -D $^ $(DESTDIR)$(LIBDIR)/$^.$(APIVERSION)

install: install-lib install-i18n

clean:
	@-rm -f $(PODIR)/*.mo $(PODIR)/*.pot
	@-rm -f $(OBJS) $(DEPFILE) *.so *.tgz core* *~
