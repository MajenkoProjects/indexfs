AC_PREREQ([2.10])
AC_INIT([indexfs], [1.0.0], [matt@majenko.co.uk])
AM_INIT_AUTOMAKE([subdir-objects no-dependencies])
AC_CONFIG_MACRO_DIR([m4])

AM_SILENT_RULES([yes])

# Checks for programs.
AC_PROG_CC

AC_SEARCH_LIBS([fuse_main], [fuse], [], [
    AC_MSG_ERROR([libfuse-dev is required])
])

AC_SEARCH_LIBS([curl_easy_init], [curl], [], [
    AC_MSG_ERROR([libcurl4-<?>-dev is required])
])

AC_CHECK_HEADERS([fuse.h])
AC_CHECK_HEADERS([curl/curl.h])

AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT


