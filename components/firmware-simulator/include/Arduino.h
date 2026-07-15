#pragma once

// Minimal host replacement for the Arduino declarations used by the shared
// firmware headers.  It deliberately does not emulate board peripherals.

#include <stddef.h>
#include <string.h>

inline size_t strlcpy(char* destination, const char* source, size_t size) {
  const size_t source_length = strlen(source);
  if (size != 0) {
    const size_t copied = source_length < size - 1 ? source_length : size - 1;
    memcpy(destination, source, copied);
    destination[copied] = '\0';
  }
  return source_length;
}
