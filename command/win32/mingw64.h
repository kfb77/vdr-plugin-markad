#pragma once

// folder win32
#include "include_first.h"
#include "missing_types.h"
#include "chown.h"
#include "getdtablesize.h"
#include "geteuid.h"
#include "getline.h"
#include "initializer.h"
#include "localtime_r.h"
#include "mntent.h"
#include "priority.h"
#include "realpath.h"
#include "sleep.h"
#include "sysconf.h"
#include "ws2parts.h"

// common headers
#include <libintl.h>   // LC_MESSAGES (gettext)


#include <iostream>    // std::{cout,cerr,endl}
//#include <string>      // std::string

//#include <cstdio>      // fclose()
//#include <cctype>      // isdigit()
//#include <cstdlib>     // realpath()
#include <time.h>      // struct tm, localtime_*
#include <dirent.h>    // struct DIR
#include <locale.h>    // LC_MESSAGES, ...
#include <getopt.h>    // struct option, getoptlong
#include <signal.h>    // signal(), SIG_*
#include <sys/stat.h>  // struct stat, stat()
#include <sys/utime.h> // struct utimbuf, utime()
//#include <winsock2.h>  // getservbyname()
//#include <windows.h>   // just to be shure.
//#undef PLANES



#define LOG_ERR 0             // WIN32 doesnt have an syslog


















/* OSD is unsupported. Just put a dummy here.
 */
#define tr(s) dgettext("markad",s)
class cOSDMessage {
private:
public:
  cOSDMessage(std::string hostName, size_t portNumber) { (void)hostName; (void)portNumber; }
  ~cOSDMessage() {}
  void Send(const char* fmt, ...) { (void)fmt; }
};


#define SIGHUP  SIGINT /* SIGINT is overwritten later. */
