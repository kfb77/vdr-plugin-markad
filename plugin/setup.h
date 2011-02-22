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
    bool whileRecording;
    bool whileReplaying;
    bool OSDMessage;
    bool Verbose;
    bool GenIndex;
    bool NoMargins;
    bool SecondPass;
    bool SaveInfo;
    bool HideMainMenuEntry;
    bool Log2Rec;
    bool LogoOnly;
    const char *LogoDir;
    const char *PluginName;
};

class cSetupMarkAd : public cMenuSetupPage
{
private:
    const char *processTexts[2];
    struct setup *setup;
    int processduring;
    int whilerecording;
    int whilereplaying;
    int osdmsg;
    int verbose;
    int genindex;
    int nomargins;
    int secondpass;
    int hidemainmenuentry;
    int log2rec;
    int logoonly;
    int saveinfo;
    void write(void);
    int lpos;
protected:
    virtual void Store(void);
public:
    cSetupMarkAd(struct setup *Setup);
    eOSState ProcessKey(eKeys Key);
};

class cSetupMarkAdList : public cOsdMenu
{
public:
    cSetupMarkAdList(struct setup *Setup);
    eOSState ProcessKey(eKeys Key);
};

#endif
