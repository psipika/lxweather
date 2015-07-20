AC_COPYRIGHT([
Copyright (c) 2012-2015 Piotr Sipika; see the AUTHORS file for more information.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software Foundation,
Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

See the COPYRIGHT file for more information.
])

# Initialize Autoconf and Automake
AC_INIT([lxweather], 0.0.4, [Piotr.Sipika AT gmail DOT com])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([1.9.6 -Wall])
AC_CONFIG_MACRO_DIR([m4])

# Check for programs we'll be using
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_INTLTOOL
#AC_PROG_LIBTOOL
AC_LANG([C])
AM_PROG_CC_C_O

m4_ifdef([AM_PROG_AR], [AM_PROG_AR])

# Initialize libtool
LT_PREREQ([2.2])
LT_INIT

#PKG_PROG_PKG_CONFIG

CPPFLAGS="$CPPFLAGS -W -Wall"

RELDATE=`date +'%a %b %e %Y'`
AC_SUBST(RELDATE)

# Check for packages we depend on
PKG_CHECK_MODULES([GLIB2], [glib-2.0])
AC_SUBST(GLIB2_CFLAGS)
AC_SUBST(GLIB2_LIBS)

PKG_CHECK_MODULES([GIO2], [gio-2.0])
AC_SUBST(GIO2_CFLAGS)
AC_SUBST(GIO2_LIBS)

# Note that gtk+-2.0 is hard-coded, gtk+3.0 support will come later...maybe
PKG_CHECK_MODULES([GTK2], [gtk+-2.0 >= 2.12])
AC_SUBST(GTK2_CFLAGS)
AC_SUBST(GTK2_LIBS)

PKG_CHECK_MODULES([LIBXML2], [libxml-2.0])
AC_SUBST(LIBXML2_CFLAGS)
AC_SUBST(LIBXML2_LIBS)

DEPENDENCIES_CFLAGS="$GLIB2_CFLAGS $GIO2_CFLAGS $GTK2_CFLAGS $LIBXML2_CFLAGS"
DEPENDENCIES_LIBS="$GLIB2_LIBS $GIO2_LIBS $GTK2_LIBS $LIBXML2_LIBS"
AC_SUBST(DEPENDENCIES_CFLAGS)
AC_SUBST(DEPENDENCIES_LIBS)

# Check for rpmbuild, deb
AC_CHECK_PROG(HAVE_RPMBUILD, rpmbuild, true, false)
AM_CONDITIONAL(HAVE_RPMBUILD, $HAVE_RPMBUILD)

AC_CHECK_PROG(HAVE_DEB, dh_clean, true, false)
AM_CONDITIONAL(HAVE_DEB, $HAVE_DEB)

#@TODO: possibly check for doxygen and pdflatex to build proper docs

# Check for debugging option
AC_ARG_ENABLE([debug],
              [AS_HELP_STRING([--enable-debug],
                             [enable debug symbols (default: no)])])

if test "$enable_debug" = "yes"
then
CPPFLAGS="$CPPFLAGS -g -O0 -DDEBUG"
LDFLAGS="$LDFLAGS -g -O0 -DDEBUG"
else
# By default, automake adds -g to compilation flags, we don't want that
CFLAGS="-O2"
fi

AC_OUTPUT(
 lxweather.spec
 Makefile
 src/Makefile
 doc/Makefile
 po/Makefile.in
)
