# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([obmc-ikvm], [1.0], [https://github.com/openbmc/obmc-ikvm/issues])

AM_INIT_AUTOMAKE([subdir-objects -Wall -Werror foreign dist-xz])

AC_CONFIG_SRCDIR([src/ikvm_manager.hpp])

# Checks for programs.
AC_PROG_CXX
AC_PROG_LN_S

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])
AC_CHECK_LIB([vncserver], [rfbGetScreen])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h sys/ioctl.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([strerror strtol])

AC_CONFIG_FILES([Makefile src/Makefile scripts/Makefile])
AC_OUTPUT
