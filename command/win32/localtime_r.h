#pragma once

#include "include_first.h"
#include <ctime>

#if __cplusplus <= CPLUSPLUS_20
  struct tm* localtime_r(const time_t* timer, struct tm* buf);
#else
  /* part of C++ since 2023 */
  using std::localtime_r;
#endif
