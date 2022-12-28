#include "sleep.h"     // forward decl
#include <thread>      // std::this_thread{,::sleep_for()}  
#include <chrono>      // std::chrono::seconds

unsigned sleep(size_t s) {
  std::this_thread::sleep_for(std::chrono::seconds(s));
  return 0;
}
