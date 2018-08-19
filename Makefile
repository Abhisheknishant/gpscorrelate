# Makefile for gpscorrelate
# Written by Daniel Foote.

PACKAGE_VERSION = 1.6.2git

CC = gcc
CXX = g++

COBJS    = main-command.o unixtime.o gpx-read.o correlate.o exif-gps.o
GOBJS    = main-gui.o gui.o unixtime.o gpx-read.o correlate.o exif-gps.o
CFLAGS   = -Wall -O2
CFLAGSINC := $(shell pkg-config --cflags libxml-2.0 exiv2)
# Add the gtk+ flags only when building the GUI
gpscorrelate-gui: CFLAGSINC += $(shell pkg-config --cflags gtk+-2.0)
LIBS :=
LDFLAGS   = -Wall -O2
LDFLAGSALL := $(shell pkg-config --libs libxml-2.0 exiv2) -lm
LDFLAGSGUI := $(shell pkg-config --libs gtk+-2.0)

# Put --nonet here to avoid downloading DTDs while building documentation
XSLTFLAGS =

prefix   = /usr/local
bindir   = $(prefix)/bin
datadir  = $(prefix)/share
mandir   = $(datadir)/man
docdir   = $(datadir)/doc/gpscorrelate
applicationsdir = $(datadir)/applications

DEFS = -DPACKAGE_VERSION=\"$(PACKAGE_VERSION)\"

TARGETS = gpscorrelate-gui gpscorrelate gpscorrelate.1

all:	$(TARGETS)

gpscorrelate: $(COBJS)
	$(CXX) -o $@ $(COBJS) $(LDFLAGS) $(LDFLAGSALL) $(LIBS)

gpscorrelate-gui: $(GOBJS)
	$(CXX) -o $@ $(GOBJS) $(LDFLAGS) $(LDFLAGSGUI) $(LDFLAGSALL) $(LIBS)

.c.o:
	$(CC) $(CFLAGS) $(CFLAGSINC) $(DEFS) -c -o $@ $<

.cpp.o:
	$(CXX) $(CFLAGS) $(CFLAGSINC) $(DEFS) -c -o $@ $<

# Hack to recompile everything if a header changes
*.o: *.h

check: gpscorrelate
	(cd tests && ./testsuite)

clean:
	rm -f *.o gpscorrelate{,.exe} gpscorrelate-gui{,.exe} doc/gpscorrelate-manpage.xml gpscorrelate.html tests/log/* $(TARGETS)

install: all
	install -d $(DESTDIR)$(bindir)
	install gpscorrelate gpscorrelate-gui $(DESTDIR)$(bindir)
	install -d $(DESTDIR)$(mandir)/man1
	install -m 0644 gpscorrelate.1 $(DESTDIR)$(mandir)/man1
	install -d $(DESTDIR)$(docdir)
	install -m 0644 doc/*.html doc/*.png $(DESTDIR)$(docdir)

install-desktop-file:
	desktop-file-install --vendor="" --dir="$(DESTDIR)$(applicationsdir)" gpscorrelate.desktop
	install -p -m0644 -D gpscorrelate-gui.svg $(DESTDIR)$(datadir)/icons/hicolor/scalable/apps/gpscorrelate-gui.svg

doc/gpscorrelate-manpage.xml: doc/gpscorrelate-manpage.xml.in
	sed -e 's,@DOCDIR@,$(docdir),' -e 's,@PACKAGE_VERSION@,$(PACKAGE_VERSION),' $< > $@

gpscorrelate.1: doc/gpscorrelate-manpage.xml
	xsltproc $(XSLTFLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

gpscorrelate.html: doc/gpscorrelate-manpage.xml
	xsltproc $(XSLTFLAGS) http://docbook.sourceforge.net/release/xsl/current/html/docbook.xsl $< > $@

build-po:
	(cd po && $(MAKE) VERSION="$(PACKAGE_VERSION)" prefix="$(prefix)" top_srcdir="$(PWD)")

install-po: build-po
	(cd po && $(MAKE) VERSION="$(PACKAGE_VERSION)" prefix="$(prefix)" top_srcdir="$(PWD)" install)

clean-po:
	(cd po && $(MAKE) VERSION="$(PACKAGE_VERSION)" prefix="$(prefix)" top_srcdir="$(PWD)" clean)

dist:
	mkdir gpscorrelate-$(PACKAGE_VERSION)
	git archive --prefix=gpscorrelate-$(PACKAGE_VERSION)/ HEAD | tar xf -
	-rm gpscorrelate-$(PACKAGE_VERSION)/po/stamp-po
	cd gpscorrelate-$(PACKAGE_VERSION)/po && $(MAKE) gpscorrelate.pot-update clean
	-rm gpscorrelate-$(PACKAGE_VERSION)/po/stamp-po
	tar cf gpscorrelate-$(PACKAGE_VERSION).tar gpscorrelate-$(PACKAGE_VERSION)
	-rm gpscorrelate-$(PACKAGE_VERSION).tar.gz
	gzip -9 gpscorrelate-$(PACKAGE_VERSION).tar
	rm -r gpscorrelate-$(PACKAGE_VERSION)
