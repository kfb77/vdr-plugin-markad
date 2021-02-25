#
# Makefile for a Video Disk Recorder plugin + program
#
# dont remove the next line, its needed for the VDR Makefile
# $(LIBDIR)/$@.$(APIVERSION)

### The version number of this plugin (taken from the main source file):

VERSION = $(shell grep 'static const char \*VERSION *=' version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')
GITTAG  = $(shell git describe --always 2>/dev/null)
$(shell GITVERSION=`git rev-parse --short HEAD 2> /dev/null`; if [ "$$GITVERSION" ]; then sed "s/\";/ ($$GITVERSION)\";/" version.dist > version.h; else cp version.dist version.h; fi)

### The directory environment:

# Use package data if installed...otherwise assume we're under the VDR source directory:
PKGCFG = $(if $(VDRDIR),$(shell pkg-config --variable=$(1) $(VDRDIR)/vdr.pc),$(shell PKG_CONFIG_PATH="$$PKG_CONFIG_PATH:../../.." pkg-config --variable=$(1) vdr))
LIBDIR = $(call PKGCFG,libdir)
LOCDIR = $(call PKGCFG,locdir)
PLGCFG = $(call PKGCFG,plgcfg)
CFGDIR = $(call PKGCFG,configdir)
#
TMPDIR ?= /tmp
DIRS = command plugin

### The compiler options:
export CFLAGS   = $(call PKGCFG,cflags)
export CXXFLAGS = $(call PKGCFG,cxxflags)

ARCHIVE = markad-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

### The version number of VDR's plugin API:
APIVERSION = $(call PKGCFG,apiversion)

### Allow user defined options to overwrite defaults:
-include $(PLGCFG)


all:
	@for i in $(DIRS); do \
		$(MAKE) -C $$i; \
		if [ $$? -ne 0 ]; then \
		        echo "make failed on directory $$i"; \
			exit 1; \
		fi; \
	done

install:
	for i in $(DIRS); do $(MAKE) -C $$i install; done

dist:
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)
	@mkdir $(TMPDIR)/$(ARCHIVE)/plugin
	@mkdir $(TMPDIR)/$(ARCHIVE)/plugin/po
	@mkdir $(TMPDIR)/$(ARCHIVE)/plugin/dist
	@mkdir $(TMPDIR)/$(ARCHIVE)/command
	@mkdir $(TMPDIR)/$(ARCHIVE)/command/po
	@mkdir $(TMPDIR)/$(ARCHIVE)/command/logos
	@cp -a plugin/*.cpp plugin/*.h plugin/Makefile $(TMPDIR)/$(ARCHIVE)/plugin
	@cp -a plugin/dist/* $(TMPDIR)/$(ARCHIVE)/plugin/dist
	@cp -a plugin/po/*.po $(TMPDIR)/$(ARCHIVE)/plugin/po
	@cp -a command/*.cpp command/*.h command/*.1 command/Makefile $(TMPDIR)/$(ARCHIVE)/command
	@cp -u command/logos/*.pgm $(TMPDIR)/$(ARCHIVE)/command/logos
	@cp -a command/po/*.po $(TMPDIR)/$(ARCHIVE)/command/po
	@cp -a *.dist *.h COPYING HISTORY README INSTALL Makefile $(TMPDIR)/$(ARCHIVE)
	@tar czf $(PACKAGE).tgz -C $(TMPDIR) $(ARCHIVE)
	@-rm -rf $(TMPDIR)/$(ARCHIVE)
	@echo Distribution package created as $(PACKAGE).tgz

clean:
	for i in $(DIRS); do make -C $$i clean; done
	@-rm -f version.h $(PACKAGE).tgz

cppcheck: 
	cppcheck --enable=all --suppress=missingIncludeSystem --suppress=unusedFunction:plugin/markad.cpp --suppress=unusedFunction:plugin/status.cpp -DLIBAVCODEC_VERSION_INT=3763044 -DDEBUGMEM=1 --error-exitcode=1 . > /dev/null
