/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>
#include "setup.h"

#if __GNUC__ > 3
#define UNUSED(v) UNUSED_ ## v __attribute__((unused))
#else
#define UNUSED(x) x
#endif

struct recs
{
    char *Name;
    char *FileName;
    pid_t Pid;
    char Status;
    bool ChangedbyUser;
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
    int Get(const char *FileName, const char *Name=NULL);
    int Add(const char *FileName, const char *Name);
    void Remove(int Position, bool Kill=false);
    void Remove(const char *Name, bool Kill=false);
    void Pause(const char *FileName);
    void Continue(const char *FileName);
    bool LogoExists(const cDevice *Device, const char *FileName);
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
    virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On);
    virtual void TimerChange(const cTimer *Timer, eTimerChange Change);
public:
    cStatusMarkAd(const char *BinDir,const char *LogoDir, struct setup *Setup);
    ~cStatusMarkAd();
    bool MarkAdRunning(void);
    void ResetActPos(void)
    {
        actpos=0;
    }
    void Check(void);
    bool GetNextActive(struct recs **RecEntry);
    bool Start(const char *FileName, const char *Name, const bool Direct=false);
};

#endif
