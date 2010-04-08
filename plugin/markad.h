/*
 * markad.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_h_
#define __markad_h_

#include <unistd.h>
#include <getopt.h>

#include "version.h"
#include "status.h"
#include "menu.h"
#include "setup.h"

#define DEF_BINDIR "/usr/bin"
#define DEF_LOGODIR "/var/lib/markad"

extern const char *VERSION;
static const char *DESCRIPTION    = trNOOP("Mark advertisements");
static const char *MAINMENUENTRY  = trNOOP("markad status");

class cPluginMarkAd : public cPlugin
{
private:
    // Add any member variables or functions you may need here.
    cStatusMarkAd *statusMonitor;
    char *bindir;
    char *logodir;
    struct setup setup;
public:
    cPluginMarkAd(void);
    virtual ~cPluginMarkAd();
    virtual const char *Version(void)
    {
        return VERSION;
    }
    virtual const char *Description(void)
    {
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
    virtual const char *MainMenuEntry(void)
    {
        return tr(MAINMENUENTRY);
    }
    virtual cOsdObject *MainMenuAction(void);
    virtual cMenuSetupPage *SetupMenu(void);
    virtual bool SetupParse(const char *Name, const char *Value);
    virtual bool Service(const char *Id, void *Data = NULL);
    virtual const char **SVDRPHelpPages(void);
    virtual cString SVDRPCommand(const char *Command, const char *Option, int &ReplyCode);
};

#endif
