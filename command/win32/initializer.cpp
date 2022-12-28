#include "initializer.h"

#undef PLANES
#include <winsock2.h>  // WSAStartup()
#include <windows.h>   // just to be shure.

w32init::w32init(void) {
  auto wsadata = WSADATA();

  // startup winsock 2.2 (latest)
  WSAStartup(MAKEWORD(2, 2), &wsadata);
}

w32init::~w32init() {
  WSACleanup();
}
