AC_INIT([tuxbox-tools], [0.0.1])
AM_INIT_AUTOMAKE
AM_SILENT_RULES([yes])

AC_PROG_CC
AC_PROG_CXX
AM_PROG_LIBTOOL

AC_ARG_WITH(multicom,
	[  --with-multicom  path to multicom sources [[NONE]]],
	[MULTICOM="$withval"],[MULTICOM=""])

AC_SUBST(MULTICOM)

if test "$MULTICOM"; then
	CPPFLAGS="$CPPFLAGS -I$MULTICOM/include"
fi

AC_OUTPUT([
Makefile
fp_control/Makefile
libeplayer3/Makefile
libmme_host/Makefile
libmmeimage/Makefile
stfbcontrol/Makefile
ustslave/Makefile
])
