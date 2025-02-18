#ifndef UTILS_H
#define UTILS_H

#include <stdarg.h>

// Print debug message with timestamp
void
debug(const char* fmt, ...);

// Print error message and exit
void
die(const char* fmt, ...);

#endif /* UTILS_H */