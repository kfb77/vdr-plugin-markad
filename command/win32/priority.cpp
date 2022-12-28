#include "priority.h"
#include <errno.h>

/* None of those below is supported on WIN32.
 *
 * As no functionality is expected and not an error,
 * zero is returned and errno set to zero.
 *
 * Otherwise, these functions are just dummies. 
 */

int getpriority(int which, int who) {
  (void)which;
  (void)who;
  errno = 0;
  return 0;
}

int setpriority(int which, int who, int prio) {
  (void)which;
  (void)who;
  (void)prio;
  errno = 0;
  return 0;
}

int ioprio_get(int which, int who) {
  return getpriority(which, who);
}

int ioprio_set(int which, int who, int ioprio) {
  return setpriority(which, who, ioprio);
}
