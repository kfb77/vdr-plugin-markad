/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <signal.h>
#include <vdr/status.h>
#include "setup.h"

#define UNUSED(v) UNUSED_ ## v __attribute__((unused))

struct recs
{
    char *Name;
    char *FileName;
    pid_t Pid;
    char Status;
};

// --- cStatusMarkAd
class cStatusMarkAd : public cStatus
{
private:
    struct recs recs[MAXDEVICES*MAXRECEIVERS];
    struct setup *setup;

    const char *bindir;
    const char *logodir;

    int actpos;

    bool getPid(int Position);
    bool getStatus(int Position);
    int Recording();
    bool Replaying();
    int Add(const char *FileName, const char *Name);
    void Remove(int Position);
    void Pause(const char *FileName);
    void Continue(const char *FileName);
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
    virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On);
public:
    cStatusMarkAd(const char *BinDir,const char *LogoDir, struct setup *Setup);
    ~cStatusMarkAd();
    bool MarkAdRunning(void);
    void ResetActPos(void)
    {
        actpos=0;
    }
    bool GetNextActive(struct recs **RecEntry);
};

#endif
