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
    bindir=strdup(DEF_BINDIR);
    logodir=strdup(DEF_LOGODIR);
}

cPluginMarkAd::~cPluginMarkAd()
{
    // Clean up after yourself!
    if (statusMonitor) delete statusMonitor;
    if (bindir) free(bindir);
    if (logodir) free(logodir);
}

const char *cPluginMarkAd::CommandLineHelp(void)
{
    // Return a string that describes all known command line options.
    return "  -b DIR,   --bindir=DIR        use DIR as location for markad executable\n"
           "                                (default: /usr/bin)\n"
           "  -l DIR    --logocachedir=DIR  use DIR as location for markad logos\n"
           "                                (default: /var/lib/markad)\n";
}

bool cPluginMarkAd::ProcessArgs(int argc, char *argv[])
{
    // Command line argument processing
    static struct option long_options[] =
    {
        { "bindir",      required_argument, NULL, 'b'
        },
        { "logocachedir",      required_argument, NULL, 'l'},
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "b:l:", long_options, NULL)) != -1)
    {
        switch (c)
        {
        case 'b':
            if ((access(optarg,R_OK | X_OK))!=-1)
            {
                if (bindir) free(bindir);
                bindir=strdup(optarg);
            }
            else
            {
                fprintf(stderr,"markad: can't access bin directory: %s\n",
                        optarg);
                return false;
            }
            break;

        case 'l':
            if ((access(optarg,R_OK))!=-1)
            {
                if (logodir) free(logodir);
                logodir=strdup(optarg);
            }
            else
            {
                fprintf(stderr,"markad: can't access logo directory: %s\n",
                        optarg);
                return false;
            }
            break;
        default:
            return false;
        }
    }
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
    statusMonitor = new cStatusMarkAd(bindir,logodir);
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
    if (statusMonitor->MarkAdRunning())
        return tr("markad still running");
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

bool cPluginMarkAd::SetupParse(const char *UNUSED(Name), const char *UNUSED(Value))
{
    // Parse your own setup parameters and store their values.
    return false;
}

bool cPluginMarkAd::Service(const char *UNUSED(Id), void *UNUSED(Data))
{
    // Handle custom service requests from other plugins
    return false;
}

const char **cPluginMarkAd::SVDRPHelpPages(void)
{
    // Return help text for SVDRP commands this plugin implements
    return NULL;
}

cString cPluginMarkAd::SVDRPCommand(const char *UNUSED(Command), const char *UNUSED(Option),
                                    int &UNUSED(ReplyCode))
{
    // Process SVDRP commands this plugin implements
    return NULL;
}


VDRPLUGINCREATOR(cPluginMarkAd) // Don't touch this!
