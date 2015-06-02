ACLOCAL_AMFLAGS= -I m4

SUBDIRS=            \
 src                \
 doc

dist-hook: lxweather.spec

cleantar:
	rm -f $(PACKAGE)*.tar.gz

# RPM-specific definitions
if HAVE_RPMBUILD
cleanrpm:
	rm -fr .rpmbuild

RPMARCH=`rpm --showrc | awk '/^build arch/ {print $$4}'`

rpm:	clean-local dist-gzip
	mkdir -p .rpmbuild/BUILD \
	.rpmbuild/SPECS \
	.rpmbuild/SOURCES \
	.rpmbuild/SRPMS \
	.rpmbuild/RPMS/$(RPMARCH) \
	.rpmbuild/tmp
	cp -f $(PACKAGE)-$(VERSION).tar.gz .rpmbuild/SOURCES
	rpmbuild --clean -ba $(top_srcdir)/lxweather.spec \
           --define "_topdir $$PWD/.rpmbuild"       \
           --define "_tmppath $$PWD/.rpmbuild/tmp"
	@echo "============================================================"
	@echo "RPMs located in .rpmbuild/RPMS and .rpmbuild/SRPMS"

else
cleanrpm:
endif

# DEB-specific definitions TODO: actually make debs, when asked
if HAVE_DEB
cleandeb:
#	dh_clean

deb: #debian/changelog
#	dpkg-buildpackage -b -us -uc

else
cleandeb:
deb:
endif

clean-local: cleantar cleandeb cleanrpm
	find . -name "*~" -delete

EXTRA_DIST =         \
 AUTHORS             \
 COPYING             \
 COPYRIGHT           \
 ChangeLog           \
 NEWS                \
 README              \
 lxweather.spec.in   \
 lxweather.spec 
