/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>

#define UNUSED(v) UNUSED_ ## v __attribute__((unused))

// --- cStatusMarkAd
class cStatusMarkAd : public cStatus
{
private:
    char *recs[MAXDEVICES*MAXRECEIVERS];
    const char *bindir;
    const char *logodir;
    void Add(const char *FileName);
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
public:
    cStatusMarkAd(const char *BinDir,const char *LogoDir);
    ~cStatusMarkAd();
    bool MarkAdRunning(void);
};

#endif
