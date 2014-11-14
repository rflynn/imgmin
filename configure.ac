#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.59])
AC_INIT([imgmin], [0.7], [https://github.com/rflynn/imgmin/issues])
AC_CONFIG_SRCDIR([src])

AM_INIT_AUTOMAKE([imgmin], [0.7])

AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
# check for -lm
AC_CHECK_LIB([m], [log])
# check for imagemagick
# don't bother checking directly for the lib, it is called MagickWand on Ubuntu but 'Wand' on Redhat,
# instead just find MAGICK_CONFIG
#PKG_CHECK_MODULES([MagickWand], [MagickWand])
AC_CHECK_PROGS(MAGICK_CONFIG, MagickWand-config Magick-config, "")

# check for apache's apxs/apxs2 tool...
AC_CHECK_PROGS(APXS, apxs2 apxs, "")

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h float.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
# works for autoconf v2.68 but not v2.59
#AC_TYPE_SSIZE_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_STRTOD
AC_CHECK_FUNCS([mkdir])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/apache2/Makefile])

AC_OUTPUT
