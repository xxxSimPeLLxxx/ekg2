dnl $Id$

AC_DEFUN(AC_CHECK_EXPAT,
[
	AC_SUBST(EXPAT_LIBS)
	AC_SUBST(EXPAT_CFLAGS)

	AC_ARG_WITH(expat, [[  --with-expat[=dir]    Compile with expat/locate base dir]], [
		if test "x$withval" = "xno"; then
			without_expat=yes
		elif test "x$withval" != "xyes"; then
			EXPAT_CFLAGS="-I$withval/include"
			EXPAT_LIBS="-I$withval/lib"
		fi
	])

	if test "x$without_expat" != "xyes"; then
		cflags="$CFLAGS $EXPAT_CFLAGS"
		ldflags="$LDFLAGS $EXPAT_LIBS"
		AC_CHECK_HEADERS([expat.h],
		[
			AC_CHECK_LIB([expat], [XML_ParserCreate],
			[
				AC_DEFINE([HAVE_EXPAT], 1, [define if you have expat])
				EXPAT_LIBS="$EXPAT_LIBS -lexpat"
				have_expat="yes"
			])
		])
		CFLAGS="$cflags"
		LDFLAGS="$ldflags"
	fi
])

