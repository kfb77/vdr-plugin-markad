#
# Makefile for a Video Disk Recorder plugin + program
#
# dont remove the next line, its needed for the VDR Makefile
# $(LIBDIR)/$@.$(APIVERSION)

DIRS = command plugin

$(shell GITVERSION=`git rev-parse --short HEAD 2> /dev/null`; if [ "$$GITVERSION" ]; then sed "s/\";/ ($$GITVERSION)\";/" version.dist > version.h; else cp version.dist version.h; fi)
VERSION = $(shell grep 'static const char \*VERSION *=' version.h | awk '{ print $$6 }' | sed -e 's/[";]//g')

TMPDIR = /tmp
ARCHIVE = markad-$(VERSION)
PACKAGE = vdr-$(ARCHIVE)

all:
	for i in $(DIRS); do $(MAKE) -C $$i; done

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
