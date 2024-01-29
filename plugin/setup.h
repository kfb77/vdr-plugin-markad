/*
 * setup.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __setup_h_
#define __setup_h_

#include <vdr/menuitems.h>


#define PROCESS_AFTER 0
#define PROCESS_DURING 1
#define PROCESS_NEVER 2


struct setup {
    int ProcessDuring     = PROCESS_NEVER;
    bool useVPS           = false;
    bool logVPS           = false;
    bool whileRecording;
    bool whileReplaying;
    bool OSDMessage;
    int  svdrPort          = 6419;
    bool Verbose;
    bool NoMargins;
    bool SecondPass;
    bool HideMainMenuEntry;
    bool Log2Rec;
    bool LogoOnly;
    bool DeferredShutdown;
    const char *LogoDir    = NULL;
    char *LogLevel         = NULL;
    char *aStopOffs        = NULL;
    bool cDecoder          = false;
    bool MarkadCut         = false;
    bool ac3ReEncode       = false;
    int autoLogoConf       = -1;
    int autoLogoMenu      = 2;
    bool fulldecode        = false;
    const char *PluginName = NULL;
};


class cSetupMarkAd : public cMenuSetupPage {
private:
    const char *processTexts[3];
    const char *autoLogoTexts[3];
    struct setup *setup;
    int autologomenu;
    int fulldecode;
    int processduring;
    int usevps = 0;
    int logvps = 0;
    int whilerecording;
    int whilereplaying;
    int osdmsg;
    int svdrPort = 6419;
    int verbose;
    int nomargins;
    int secondpass;
    int hidemainmenuentry;
    int log2rec;
    int logoonly;
    int deferredshutdown;
    void write(void);
    int lpos;
protected:
    virtual void Store(void);
public:
    explicit cSetupMarkAd(struct setup *Setup);
    eOSState ProcessKey(eKeys Key);
};


class cSetupMarkAdList : public cOsdMenu {
public:
    explicit cSetupMarkAdList(struct setup *Setup);
    eOSState ProcessKey(eKeys Key);
};


class cSetupMarkAdListItem : public cOsdItem {
public:
    explicit cSetupMarkAdListItem(const char *Text, eOSState State=osUnknown,bool Selectable=true):cOsdItem(Text,State,Selectable) { }
    virtual int Compare(const cListObject &ListObject) const;
};
#endif
