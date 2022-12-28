#include "geteuid.h"
#include <iostream>
#include "include_first.h"
#include <windows.h>

bool process_is_elevated(void) {
  HANDLE h = NULL;

  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &h)) {
     std::cerr << "OpenProcessToken failed with " << GetLastError() << std::endl;
     if (h) CloseHandle(h);
     return false;
     }

  TOKEN_ELEVATION elevation;
  DWORD dwSize;
  if (!GetTokenInformation(h, TokenElevation, &elevation, sizeof(elevation), &dwSize)) {
     std::cerr << "GetTokenInformation failed with " << GetLastError() << std::endl;
     if (h) CloseHandle(h);
     return false;
     }

  return elevation.TokenIsElevated != 0;
}

int geteuid(void) {
  if (process_is_elevated())
     return 0;

  return 1000; // anything, but non-zero.
}
