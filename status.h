/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>
#include "recv.h"

// --- cStatusMarkAd
class cStatusMarkAd : public cStatus
{
private:
    cMarkAdReceiver *recv[MAXDEVICES*MAXRECEIVERS];
    int FindReceiver(const char *FileName);
    int GetFreeReceiver();
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
public:
    cStatusMarkAd();
};

#endif
