#include "chown.h"
#include <errno.h>

int chown(const char* pathname, int owner, int group) {
  (void)pathname;
  (void)owner;
  (void)group;
  errno = 0;
  return 0;
}
