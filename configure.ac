#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([imgmin], [0.7], [https://github.com/rflynn/imgmin/issues])
AC_CONFIG_SRCDIR([src/imgmin.c])

AM_INIT_AUTOMAKE([imgmin], [0.7])

AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
# check for imagemagick
AC_CHECK_LIB([m], [log])
AC_CHECK_LIB([MagickWand], [NewMagickWand])

# check for apache's apxs/apxs2 tool...
AC_CHECK_PROGS(APXS, apxs2 apxs, "")

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h float.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([mkdir])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/apache2/Makefile])

AC_OUTPUT
