#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "utils.h"

static int debug_enabled = 1;

void
debug(const char* fmt, ...)
{
  if (!debug_enabled)
    return;

  time_t now = time(NULL);
  char timestamp[64];
  strftime(timestamp, sizeof(timestamp), "%H:%M:%S", localtime(&now));

  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[%s] ", timestamp);
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
}

void
die(const char* msg)
{
  fprintf(stderr, "Error: %s\n", msg);
  exit(1);
}