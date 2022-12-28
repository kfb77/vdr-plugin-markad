#include "getline.h"
#include <cstdlib> // malloc()
#include <errno.h> // errno


ssize_t getline(char** lineptr, size_t* n, FILE* stream) {
  if ((n == nullptr) or
      (lineptr == nullptr) or
      (stream == nullptr)) {
     /* Bad arguments (n or lineptr is NULL, or stream is not valid). */
     errno = EINVAL;
     return -1;
     }

  /* If *lineptr is set to NULL before the call, then getline() will
   * allocate a buffer for storing the line.  This buffer should be
   * freed by the user program even if getline() failed.
   */
  const size_t buflen = 128;
  char* bufptr = *lineptr;
  ssize_t size = *n;
  if (bufptr == nullptr) {
     bufptr = static_cast<char*>(malloc(buflen));
     if (bufptr == nullptr) {
        /* Allocation or reallocation of the line buffer failed. */
        errno = ENOMEM;
        return -1;
        }
     size = buflen;
     }

  int c = fgetc(stream);
  if (c == EOF) {
     /* Bad arguments (stream is not valid). */
     errno = EINVAL;
     return -1;
     }

  char* p = bufptr;
  while (c != EOF) {
     if ((p - bufptr) > (size - 1)) {
        size += buflen;
        bufptr = static_cast<char*>(realloc(bufptr, size));
        if (bufptr == nullptr) {
           /* Allocation or reallocation of the line buffer failed. */
           errno = ENOMEM;
           return -1;
           }
        }
     *p++ = c;
     if (c == '\n')
        break;
     c = fgetc(stream);
     }
  *p++ = 0;

  /* on a successful call, *lineptr and *n will be updated to reflect the
   * buffer address and allocated size respectively.
   */
  errno = 0;
  *lineptr = bufptr;
  *n = size;

  /* On success, getline() returns the number of characters read, including
   * the delimiter character, but not including the terminating null byte ('\0').
   */
  return p - bufptr - 1;
}
