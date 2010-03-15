/*
 * markad.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <vdr/plugin.h>

#include "markad.h"

cPluginMarkAd::cPluginMarkAd(void)
{
    // Initialize any member variables here.
    // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
    // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
    statusMonitor=NULL;
}

cPluginMarkAd::~cPluginMarkAd()
{
    // Clean up after yourself!
    if (statusMonitor) delete statusMonitor;
}

const char *cPluginMarkAd::CommandLineHelp(void)
{
    // Return a string that describes all known command line options.
    return NULL;
}

bool cPluginMarkAd::ProcessArgs(int argc, char *argv[])
{
    // Implement command line argument processing here if applicable.
    return true;
}

bool cPluginMarkAd::Initialize(void)
{
    // Initialize any background activities the plugin shall perform.
    return true;
}

bool cPluginMarkAd::Start(void)
{
    // Start any background activities the plugin shall perform.
    statusMonitor = new cStatusMarkAd();
    return true;
}

void cPluginMarkAd::Stop(void)
{
    // Stop any background activities the plugin is performing.
}

void cPluginMarkAd::Housekeeping(void)
{
    // Perform any cleanup or other regular tasks.
}

const char *cPluginMarkAd::MainMenuEntry(void)
{
    return NULL;
}

void cPluginMarkAd::MainThreadHook(void)
{
    // Perform actions in the context of the main program thread.
    // WARNING: Use with great care - see PLUGINS.html!
}

cString cPluginMarkAd::Active(void)
{
    // Return a message string if shutdown should be postponed
    return NULL;
}

time_t cPluginMarkAd::WakeupTime(void)
{
    // Return custom wakeup time for shutdown script
    return 0;
}

cOsdObject *cPluginMarkAd::MainMenuAction(void)
{
    // Perform the action when selected from the main VDR menu.
    return NULL;
}

cMenuSetupPage *cPluginMarkAd::SetupMenu(void)
{
    // Return a setup menu in case the plugin supports one.
    return NULL;
}

bool cPluginMarkAd::SetupParse(const char *Name, const char *Value)
{
    // Parse your own setup parameters and store their values.
    return false;
}

bool cPluginMarkAd::Service(const char *Id, void *Data)
{
    // Handle custom service requests from other plugins
    return false;
}

const char **cPluginMarkAd::SVDRPHelpPages(void)
{
    // Return help text for SVDRP commands this plugin implements
    return NULL;
}

cString cPluginMarkAd::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode)
{
    // Process SVDRP commands this plugin implements
    return NULL;
}

VDRPLUGINCREATOR(cPluginMarkAd); // Don't touch this!
