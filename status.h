/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>

// --- cStatusMarkAd
class cStatusMarkAd : public cStatus
{
private:
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
public:
};

#endif
