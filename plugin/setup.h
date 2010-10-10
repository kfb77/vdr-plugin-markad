/*
 * setup.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __setup_h_
#define __setup_h_

#include <vdr/menuitems.h>

struct setup
{
    bool ProcessDuring;
    int  IOPrioClass;
    bool whileRecording;
    bool whileReplaying;
    bool OSDMessage;
    bool BackupMarks;
    bool Verbose;
    bool GenIndex;
    bool NoMargins;
    bool SecondPass;
    bool AC3Always;
    bool HideMainMenuEntry;
};

class cSetupMarkAd : public cMenuSetupPage
{
private:
    const char *processTexts[2];
    const char *ioprioTexts[3];
    struct setup *setup;
    int processduring;
    int ioprioclass;
    int whilerecording;
    int whilereplaying;
    int osdmsg;
    int backupmarks;
    int verbose;
    int genindex;
    int nomargins;
    int secondpass;
    int hidemainmenuentry;
    int ac3always;
    void write(void);
protected:
    virtual void Store(void);
public:
    cSetupMarkAd(struct setup *setup);
    eOSState ProcessKey(eKeys Key);
};

#endif
