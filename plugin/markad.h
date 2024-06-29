/*
 * markad.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_h_
#define __markad_h_

#include "version.h"
#include "status.h"
#include "menu.h"
#include "setup.h"

extern const char *VERSION;
static const char *DESCRIPTION    = trNOOP("Mark advertisements");

class cPluginMarkAd : public cPlugin {
private:
    bool ReadTitle(const char *Directory);

    cStatusMarkAd *statusMonitor = nullptr;
    char *bindir                 = nullptr;
    char *logodir                = nullptr;
    int loglevel                 = 0;
    int astopoffs                = -1;
    bool MarkadCut               = false;
    bool ac3ReEncode             = false;
    int autoLogoConf             = -1;
    struct setup setup           = {};
    char title[80]               = {};
    time_t lastcheck             = 0;
public:
    cPluginMarkAd(void);
    virtual ~cPluginMarkAd();
    virtual const char *Version(void) {
        return VERSION;
    }
    virtual const char *Description(void) {
        return tr(DESCRIPTION);
    }
    virtual const char *CommandLineHelp(void);
    virtual bool ProcessArgs(int argc, char *argv[]);
    virtual bool Initialize(void);
    virtual bool Start(void);
    virtual void Stop(void);
    virtual void Housekeeping(void);
    virtual void MainThreadHook(void);
    virtual cString Active(void);
    virtual time_t WakeupTime(void);
    virtual const char *MainMenuEntry(void);
    virtual cOsdObject *MainMenuAction(void);
    virtual cMenuSetupPage *SetupMenu(void);
    virtual bool SetupParse(const char *Name, const char *Value);
    virtual bool Service(const char *Id, void *Data = nullptr);
    virtual const char **SVDRPHelpPages(void);
    virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
};
#endif
