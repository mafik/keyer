// Jim Tcl autoconf header for ESP32-S3
#ifndef JIMAUTOCONF_H
#define JIMAUTOCONF_H

#define HAVE_LONG_LONG 1
#define HAVE_SYS_TIME_H 1
#define HAVE_UNISTD_H 1
#define HAVE_ISASCII 1
#define HAVE_MKSTEMP 1
#define HAVE_STRPTIME 1
#define jim_wide long long
#define HAVE_DECL_ISNAN 1
#define HAVE_DECL_ISINF 1
#define JIM_WIDE_MIN LLONG_MIN
#define JIM_WIDE_MAX LLONG_MAX
#define JIM_WIDE_MODIFIER "lld"
#define JIM_VERSION 80
#define JIM_REFERENCES 1
#define JIM_REGEXP 1
#define JIM_UTF8 1
#define USE_UTF8 1
#define TCL_PLATFORM_OS "esp32"
#define TCL_PLATFORM_PATH_SEPARATOR ":"
#define TCL_LIBRARY "/spiffs"

#define JIM_MATH_FUNCTIONS 1
#define HAVE_DIRENT_H 1
#define HAVE_USLEEP 1

// Extensions compiled separately (suppress stubs in jim.c)
#define jim_ext_aio 1
#define jim_ext_eventloop 1
#define jim_ext_readdir 1

// Not available on ESP32
// #define HAVE_BACKTRACE 1
// #define HAVE_EXECINFO_H 1
// #define HAVE_CRT_EXTERNS_H 1
// #define HAVE_UMASK 1
// #define HAVE_CLOCK_GETTIME 1

#endif
