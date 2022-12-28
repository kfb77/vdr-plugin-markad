#pragma once

/*******************************************************************************
 * No replacement for utime.h on WIN32.
 *
 * It is enough, to
 *
 *  - define _PATH_MOUNTED to some const char*
 *  - let setmntent return nullptr
 *  - let getmntent return nullptr
 *  - let endmntent return 0
 *  - set errno = 0 in all funcs
 *  - define struct mntent with members mnt_dir, mnt_opts
 ******************************************************************************/

#include <cstdio> // type 'FILE'

#define _PATH_MOUNTED ""

struct mntent {
  char* mnt_dir;
  char* mnt_opts;
};

FILE* setmntent(const char*, const char*);
struct mntent* getmntent(FILE*);
int endmntent(FILE*);
