/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "status.h"

void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    if (!Device) return; // just to be safe
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir) return; // we cannot operate without bindir
    if (!logodir) return; // we dont want to operate without logodir

    if (On)
    {
        // Start the standalone version
        cString cmd = cString::sprintf("\"%s\"/markad --online=2 -l \"%s\" before \"%s\"",bindir,
                                       logodir,FileName);
        SystemExec(cmd);
    }
    else
    {
        // TODO: Start second pass?
    }

}

cStatusMarkAd::cStatusMarkAd(const char *BinDir, const char *LogoDir)
{
    bindir=BinDir;
    logodir=LogoDir;
}
