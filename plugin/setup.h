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
    int ProcessDuring;
    bool useVPS = false;
    bool logVPS = false;
    bool whileRecording;
    bool whileReplaying;
    bool OSDMessage;
    int  svdrPort = 6419;
    bool Verbose;
    bool GenIndex;
    bool NoMargins;
    bool SecondPass;
    bool SaveInfo;
    bool HideMainMenuEntry;
    bool Log2Rec;
    bool LogoOnly;
    bool DeferredShutdown;
    const char *LogoDir;
    char *LogLevel = NULL;
    char *aStopOffs = NULL;
    bool cDecoder = false;
    bool MarkadCut = false;
    bool ac3ReEncode = false;
    int autoLogoConf = -1;
    int autoLogoMenue = 2;
    bool fulldecode = false;
    const char *PluginName;
};


class cSetupMarkAd : public cMenuSetupPage {
    private:
        const char *processTexts[3];
        const char *autoLogoTexts[3];
        struct setup *setup;
        int autologomenue;
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
        int saveinfo;
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
