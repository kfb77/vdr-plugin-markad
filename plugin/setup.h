/*
 * setup.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __setup_h_
#define __setup_h_

#include <vdr/menuitems.h>
#include "status.h"

struct setup
{
    bool ProcessDuring;
    bool whileRecording;
    bool whilePlaying;
};

class cSetupMarkAd : public cMenuSetupPage
{
private:
    const char *processTexts[2];
    struct setup *setup;
    int processduring;
    int whilerecording;
    int whileplaying;
    void write(void);
protected:
    virtual void Store(void);
public:
    cSetupMarkAd(struct setup *setup);
    eOSState ProcessKey(eKeys Key);
};

#endif
