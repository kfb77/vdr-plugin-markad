#include "localtime_r.h"

#if __cplusplus <= CPLUSPLUS_20
#include <time.h>
#include <errno.h>
#include "../global.h"

#if defined (WINDOWS)
struct tm* localtime_r(const time_t* timer, struct tm* buf) {

  /* NOTE: careful here!
   *
   * MSDN :  errno_t    localtime_s(struct tm* const tmDest, time_t const* const sourceTime);
   * C++11:  struct tm* localtime_s(const time_t* restrict timer, struct tm* restrict buf);
   */

  errno_t r = localtime_s(buf, timer);

  if (r != 0)
     errno = EINVAL;
  else
     errno = 0;
     
  return buf;
}
#endif
#endif
