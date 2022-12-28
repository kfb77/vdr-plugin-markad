#include "sysconf.h"
#include <errno.h>

long sysconf(int name) {
  switch(name) {
     case _SC_LONG_BIT:
        // Inquire about the number of bits in a variable of type long int.
        errno = 0;
        return sizeof(long) << 3;
     default:
        errno = EINVAL;
        return -1;
     }
}
