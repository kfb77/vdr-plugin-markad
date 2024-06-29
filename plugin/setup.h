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
    int ProcessDuring      = PROCESS_NEVER;
    bool useVPS            = false;
    bool logVPS            = false;
    bool whileRecording    = false;;
    bool whileReplaying    = false;;
    bool OSDMessage        = false;
    int  svdrPort          = 6419;
    bool Verbose           = false;
    bool HideMainMenuEntry = false;
    bool Log2Rec           = false;
    bool LogoOnly          = false;
    bool DeferredShutdown  = false;
    const char *LogoDir    = nullptr;
    char *LogLevel         = nullptr;
    char *aStopOffs        = nullptr;
    bool MarkadCut         = false;
    bool ac3ReEncode       = false;
    int autoLogoConf       = -1;
    int autoLogoMenu       = 2;
    bool fulldecode        = false;
    int hwaccel            = 0;
    const char *PluginName = nullptr;
#define MAX_HWACCEL 5
    const char *hwaccelTexts[MAX_HWACCEL] = {tr("off"), "vaapi", "vdpau", "vdpau", "cuda"};
};


class cSetupMarkAd : public cMenuSetupPage {
private:
    void write(void);

    const char *processTexts[3]           = {};
    const char *autoLogoTexts[3]          = {};

    struct setup *setup          = nullptr;
    int autologomenu             = 0;
    int fulldecode               = 0;
    int hwaccel                  = 0;
    int processduring            = 0;
    int usevps                   = 0;
    int logvps                   = 0;
    int whilerecording           = 0;
    int whilereplaying           = 0;
    int osdmsg                   = 0;
    int svdrPort                 = 6419;
    int verbose                  = 0;
    int hidemainmenuentry        = 0;
    int log2rec                  = 0;
    int logoonly                 = 0;
    int deferredshutdown         = 0;
    int lpos                     = 0;
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
