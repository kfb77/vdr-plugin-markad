#include "mntent.h"
#include <errno.h>


FILE* setmntent(const char* filename, const char* type) {
  (void)filename;
  (void)type;
  errno = 0;
  return nullptr;
}

struct mntent* getmntent(FILE* fp) {
  (void)fp;
  errno = 0;
  return nullptr;
}

int endmntent(FILE* fp) {
  (void)fp;
  errno = 0;
  return 0;
}

/*******************************************************************************

 The code tries to find out, if the recording dir is mounted using
 option noatime. On WIN32, we dont have this concept of noatime mounts.

 It is enough, to
  - define _PATH_MOUNTED to some const char*
  - let setmntent return nullptr
  - let getmntent return nullptr
  - let endmntent return 0
  - set errno = 0 in all funcs
  - define struct mntent with members mnt_dir, mnt_opts

<snip>
time_t cMarkAdStandalone::GetRecordingStart(time_t start, int fd) {
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat statbuf;
    FILE *mounts = setmntent(_PATH_MOUNTED, "r");
    int mlen;
    int oldmlen = 0;
    bool useatime = false;
    while ((ent = getmntent(mounts)) != nullptr) {
        if (strstr(directory, ent->mnt_dir)) {
            mlen = strlen(ent->mnt_dir);
            if (mlen > oldmlen) {
                if (strstr(ent->mnt_opts, "noatime")) {
                    useatime = true;
                }
                else {
                    useatime = false;
                }
            }
            oldmlen = mlen;
        }
    }
    endmntent(mounts);
</snap>

 *******************************************************************************/


