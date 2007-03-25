## Process this file with automake to produce Makefile.in
## the fikkiwubg kube us needed to include local m4 scripts for detecting boost
ACLOCAL_AMFLAGS= -I m4


SUBDIRS =      \
	po         \
	libquiver  \
	data \
	src        

quiverdocdir = ${datadir}/doc/quiver

quiverdoc_DATA = \
	README\
	COPYING\
	AUTHORS\
	ChangeLog\
	INSTALL\
	NEWS

if MAEMO
maemo_3rdparty = 3rdparty 
endif

EXTRA_DIST = ${quiverdoc_DATA} \
     intltool-extract.in \
	 intltool-merge.in \
	 intltool-update.in ${maemo_3rdparty}


dist-hook:
	rm -rf `find ${distdir}/3rdparty -name .svn`

