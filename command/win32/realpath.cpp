#include "realpath.h"
#include <sys/stat.h>  // struct stat, stat()
#include <windef.h>    // PATH_MAX




char* realpath(const char* path, char* resolved_path) {

  /* EINVAL path is NULL. */
  if (path == NULL) {
     errno = EINVAL;
     return NULL;
     }

  /* If resolved_path is specified as NULL, then realpath() uses
   * malloc(3) to allocate a buffer of up to PATH_MAX bytes to hold
   * the resolved pathname, and returns a pointer to this buffer.
   * The caller should deallocate this buffer using free(3).
   */
  if (resolved_path == NULL) {
     resolved_path = static_cast<char*>(malloc(PATH_MAX));
     if (resolved_path == NULL) {
        errno = ENOMEM;
        return NULL;
        }
     }

  struct stat statbuf;
  if (stat(path, &statbuf) != 0) {
     return NULL; // stat() sets errno
     }

  char* fp = _fullpath(resolved_path, path, PATH_MAX);
  if (fp == NULL) {
     return NULL; // assume valid errno from _fullpath()
     }

  errno = 0;
  return fp;
}
