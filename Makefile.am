## Process this file with automake to produce Makefile.in

@SET_MAKE@
RECURSIVE_TARGETS = all clean

$(RECURSIVE_TARGETS):
	cd unix; $(MAKE) $@

dist_pkgdata_DATA = license.terms ChangeLog changes

##  Need all the source code in the subdirectories for a distribution.
EXTRA_DIST = $(subdirs)

subdirs = bitmaps compat doc generic library mac macosx tests \
          unix win xlib

##  Before distributing subdirectories we need to clean then up (we get all
##  the files in these directories). Also remove softlinks.
dist-hook:
	rm -rf `find $(distdir) -type l`
	(cd $(distdir)/unix; make distclean)
