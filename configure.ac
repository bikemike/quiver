dnl Process this file with autoconf to produce a configure script.
dnl Created by Anjuta application wizard.

AC_INIT([quiver],[0.2.0],[http://mike.yi.org/quiver])
AC_CONFIG_SRCDIR([src/Quiver.cpp])

AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([subdir-objects]) 

AC_LANG_PUSH([C++])
AC_CHECK_HEADER([ext/hash_map],[AC_DEFINE([HAVE_EXT],[],["have ext"])],[]) 
AC_CHECK_HEADER([tr1/unordered_map],[AC_DEFINE([HAVE_TR1],[],["have tr1"])],[]) 
AC_CHECK_HEADER([unordered_map],[AC_DEFINE([HAVE_CXX0X],[],["have cxx0x"])],[]) 
AC_LANG_POP([C++])

AM_CONFIG_HEADER([config.h])
LT_INIT
dnl AM_MAINTAINER_MODE

AM_PROG_CC_C_O

AC_SEARCH_LIBS([strerror],[cposix])
dnl AC_PROG_CC
m4_warn([obsolete],
[The preprocessor macro `STDC_HEADERS' is obsolete.
  Except in unusual embedded environments, you can safely include all
  ISO C90 headers unconditionally.])dnl
# Autoupdate added the next two lines to ensure that your configure
# script's behavior did not change.  They are probably safe to remove.
AC_CHECK_INCLUDES_DEFAULT
AC_PROG_EGREP


AC_CHECK_FUNCS([gettimeofday])
AC_CHECK_HEADERS([sys/time.h])

AC_CHECK_HEADER([jpeglib.h],, [AC_MSG_ERROR([Could not find jpeglib.h])])
AC_CHECK_HEADER([pthread.h],, [AC_MSG_ERROR([Could not find pthread.h])])


AC_C_CONST
AC_HEADER_STDBOOL
m4_warn([obsolete],
[Update your code to rely only on HAVE_SYS_TIME_H,
then remove this warning and the obsolete code below it.
All current systems provide time.h; it need not be checked for.
Not all systems provide sys/time.h, but those that do, all allow
you to include it and time.h simultaneously.])dnl
AC_CHECK_HEADERS_ONCE([sys/time.h])
# Obsolete code to be removed.
if test $ac_cv_header_sys_time_h = yes; then
  AC_DEFINE([TIME_WITH_SYS_TIME],[1],[Define to 1 if you can safely include both <sys/time.h>
	     and <time.h>.  This macro is obsolete.])
fi
# End of obsolete code.



AC_PROG_CPP
AC_PROG_CXX

AC_PATH_PROG([GLIB_GENMARSHAL], [glib-genmarshal])

AC_PATH_PROG([GDK_PIXBUF_CSOURCE], [gdk-pixbuf-csource])

dnl Set gettext package name
GETTEXT_PACKAGE=quiver
AC_SUBST([GETTEXT_PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE],["$GETTEXT_PACKAGE"], [GETTEXT package name])

dnl Add the languages which your application supports here.
ALL_LINGUAS="fr"
AM_GLIB_GNU_GETTEXT

IT_PROG_INTLTOOL([0.21])

QUIVER_MODULES="gtk4 gio-2.0"

PKG_CHECK_MODULES([LIBEXIF], [libexif],[have_libexif=yes],[have_libexif=no])

PKG_CHECK_MODULES([LIBSQLITE], [sqlite3], [have_sqlite3=yes],[have_sqlite3=no])

PKG_CHECK_MODULES([GSTREAMER], [gstreamer-1.0 gstreamer-plugins-base-1.0 gstreamer-video-1.0 gstreamer-app-1.0])


AC_ARG_WITH(
	[build_config], 
	[AS_HELP_STRING(
		[--with-build-config=CFG],
		[where CFG is one of the following alternate build configurations: maemo])
	],
	[build_config=$withval],
	[])

	QUIVER_MODULES="$QUIVER_MODULES"

AC_SUBST([PACKAGE_CFLAGS])

PKG_CHECK_MODULES([PACKAGE], [$QUIVER_MODULES])

AX_BOOST_BASE([1.33.1])


AC_SUBST([PACKAGE_CFLAGS])
AC_SUBST([PACKAGE_LIBS])

AC_SUBST([LIBEXIF_CFLAGS])
AC_SUBST([LIBEXIF_LIBS])

AC_SUBST([LIBSQLITE_CFLAGS])
AC_SUBST([LIBSQLITE_LIBS])

AC_SUBST([GSTREAMER_CFLAGS])
AC_SUBST([GSTREAMER_LIBS])

AC_ARG_WITH(
	[data-dir], 
	[AS_HELP_STRING(
		[--with-data-dir=DATA_DIR],
		[where DATA_DIR is the directory that quiver.ui lives])
	],
	[quiver_datadir=$withval],
	[quiver_datadir=${datadir}/quiver/data])

AC_SUBST([quiver_datadir])

AC_CONFIG_FILES([
Makefile
po/Makefile.in
libquiver/Makefile
src/Makefile
src/icons/Makefile
data/Makefile
])
AC_OUTPUT
