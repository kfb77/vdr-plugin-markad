/*
 * markad.c: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <vdr/plugin.h>

#include "markad.h"
#include "debug.h"

#define DEF_BINDIR "/usr/bin"
#define DEF_LOGODIR "/var/lib/markad"


cPluginMarkAd::cPluginMarkAd(void) {
    // Initialize any member variables here.
    // DON'T DO ANYTHING ELSE THAT MAY HAVE SIDE EFFECTS, REQUIRE GLOBAL
    // VDR OBJECTS TO EXIST OR PRODUCE ANY OUTPUT!
    statusMonitor = NULL;
    bindir = strdup(DEF_BINDIR);
    ALLOC(strlen(bindir)+1, "bindir");
    logodir = strdup(DEF_LOGODIR);
    ALLOC(strlen(logodir)+1, "logodir");
    title[0]                = 0;
    setup.ProcessDuring     = PROCESS_NEVER;
    setup.whileRecording    = true;
    setup.whileReplaying    = true;
    setup.OSDMessage        = false;
    setup.Verbose           = false;
    setup.NoMargins         = false;
    setup.HideMainMenuEntry = false;
    setup.SecondPass        = true;
    setup.Log2Rec           = false;
    setup.LogoOnly          = true;
    setup.DeferredShutdown  = true;
}


cPluginMarkAd::~cPluginMarkAd() {
    // Clean up after yourself!
    if (statusMonitor) {
        FREE(sizeof(*statusMonitor), "statusMonitor");
        delete statusMonitor;
    }
    if (bindir) {
        FREE(strlen(bindir)+1, "bindir");
        free(bindir);
    }
    if (logodir) {
        FREE(strlen(logodir)+1, "logodir");
        free(logodir);
    }
    if (setup.aStopOffs) {
        FREE(strlen(setup.aStopOffs)+1, "setup.aStopOffs");
        free(setup.aStopOffs);
    }
    if (setup.LogLevel) {
        FREE(strlen(setup.LogLevel)+1, "setup.LogLevel");
        free(setup.LogLevel);
    }
#ifdef DEBUG_MEM
    memList();
    memClear();
#endif
}


const char *cPluginMarkAd::CommandLineHelp(void) {
    // Return a string that describes all known command line options.
    return "  -b DIR,   --bindir=DIR         use DIR as location for markad executable\n"
           "                                 (default: /usr/bin)\n"
           "  -l DIR    --logocachedir=DIR   use DIR as location for markad logos\n"
           "                                 (default: /var/lib/markad)\n"
           "            --loglevel=<level>   sets log level of started markad process (standalone, not the plugin) to the specified value\n"
           "                                 <level>: 1=error 2=info 3=debug 4=trace\n"
           "            --astopoffs=<value>  assumed stop offset (to start + length of broadcast) in seconds, range from 0 to 240\n"
           "                                 (default is 0)\n"
           "            --cut                cut video based on marks and store it in the recording directory)\n"
           "            --ac3reencode        re-encode AC3 stream to fix low audio level of cutted video on same devices\n"
           "                                 requires --cut\n"
           "            --autologo=<option>  0 = disable, only use logos from logo cache directory\n"
           "                                 1 = deprecated, do not use\n"
           "                                 2 = enable (default)\n"
           "                                     if there is no suitable logo in the logo cache directory markad will\n"
           "                                     try to find the logo from recording and store it in the recording directory\n"
           "                                     If this option is set you can not configure this feature from the VDR menu\n";
}


bool cPluginMarkAd::ProcessArgs(int argc, char *argv[]) {
    // Command line argument processing
    static struct option long_options[] = {
        { "bindir",       required_argument, NULL, 'b'},
        { "logocachedir", required_argument, NULL, 'l'},
        { "loglevel",     required_argument, NULL, '1'},
        { "astopoffs",    required_argument, NULL, '2'},
        { "cDecoder",     no_argument,       NULL, '3'},
        { "cut",          no_argument,       NULL, '4'},
        { "ac3reencode",  no_argument,       NULL, '5'},
        { "autologo",     required_argument, NULL, '6'},
        { NULL, 0, NULL, 0 }
    };

    int c;
    while ((c = getopt_long(argc, argv, "b:l:", long_options, NULL)) != -1) {
        switch (c) {
        case 'b':
            if ((access(optarg,R_OK | X_OK)) != -1) {
                if (bindir) {
                    FREE(strlen(bindir)+1, "bindir");
                    free(bindir);
                }
                bindir=strdup(optarg);
                ALLOC(strlen(bindir)+1, "bindir");
            }
            else {
                fprintf(stderr,"markad: can't access bin directory: %s\n", optarg);
                return false;
            }
            break;
        case 'l':
            if ((access(optarg,R_OK)) != -1) {
                if (logodir) {
                    FREE(strlen(logodir)+1, "logodir");
                    free(logodir);
                }
                logodir=strdup(optarg);
                ALLOC(strlen(logodir)+1, "logodir");
            }
            else {
                fprintf(stderr,"markad: can't access logo directory: %s\n", optarg);
                return false;
            }
            break;
        case '1':
            loglevel = atoi(optarg);
            break;
        case '2':
            astopoffs = atoi(optarg);
            break;
        case '3':
            fprintf(stderr,"markad: parameter --cDecoder: is depreciated, please remove it from your configuration\n");
            break;
        case '4':
            MarkadCut = true;
            break;
        case '5':
            ac3ReEncode = true;
            break;
        case '6':
            autoLogoConf = atoi(optarg);
            break;
        default:
            return false;
        }
    }
    return true;
}


bool cPluginMarkAd::Initialize(void) {
    // Initialize any background activities the plugin shall perform.
    dsyslog("markad: cPluginMarkAd::Initialize() called");
    char *path;
    if (asprintf(&path, "%s/markad", bindir) == -1) return false;
    ALLOC(strlen(path)+1, "path");

    struct stat statbuf;
    if (stat(path, &statbuf) == -1) {
        esyslog("markad: cannot find %s, please install", path);

        FREE(strlen(path)+1, "path");
        free(path);
        return false;
    }
    FREE(strlen(path)+1, "path");
    free(path);

    dsyslog("markad: cPluginMarkAd::Initialize(): create status monitor");
    statusMonitor = new cStatusMarkAd(bindir, logodir, &setup);
    ALLOC(sizeof(*statusMonitor), "statusMonitor");
    return (statusMonitor!=NULL);
}


bool cPluginMarkAd::Start(void) {
    // Start any background activities the plugin shall perform.
    dsyslog("markad: cPluginMarkAd::Start() called");
    lastcheck = 0;
    setup.PluginName = Name();
    if (loglevel) {
        if(! asprintf(&setup.LogLevel, " --loglevel=%i ", loglevel)) esyslog("markad: asprintf out of memory");
        ALLOC(strlen(setup.LogLevel)+1, "setup.LogLevel");
    }

    if (astopoffs >= 0) {
        if(! asprintf(&setup.aStopOffs, " --astopoffs=%i ", astopoffs)) esyslog("markad: asprintf out of memory");
        ALLOC(strlen(setup.aStopOffs)+1, "setup.aStopOffs");
    }
    setup.MarkadCut    = MarkadCut;
    setup.ac3ReEncode  = ac3ReEncode;
    setup.autoLogoConf = autoLogoConf;
    setup.LogoDir      = logodir;

    return true;
}


void cPluginMarkAd::Stop(void) {
    // Stop any background activities the plugin is performing.
    dsyslog("markad: cPluginMarkAd::Stop() called");
}


void cPluginMarkAd::Housekeeping(void) {
    // Perform any cleanup or other regular tasks.
}


void cPluginMarkAd::MainThreadHook(void) {
    // Perform actions in the context of the main program thread.
    // WARNING: Use with great care - see PLUGINS.html!
    time_t now = time(NULL);
    if (now > (lastcheck + 5)) {
        statusMonitor->Check();
        lastcheck = now;
    }
}


cString cPluginMarkAd::Active(void) {
    // Return a message string if shutdown should be postponed
    dsyslog("markad: got shutdown request");
    if (statusMonitor->MarkAdRunning() && (setup.DeferredShutdown))
        return tr("markad still running");
    return NULL;
}


time_t cPluginMarkAd::WakeupTime(void) {
    // Return custom wakeup time for shutdown script
    return 0;
}


cOsdObject *cPluginMarkAd::MainMenuAction(void) {
    // Perform the action when selected from the main VDR menu.
    return new cMenuMarkAd(statusMonitor);  // this should be freed from VDR after meue closed
}


cMenuSetupPage *cPluginMarkAd::SetupMenu(void) {
    // Return the setup menu
    return new cSetupMarkAd(&setup);   // this should be freed from VDR after meue closed
}


bool cPluginMarkAd::SetupParse(const char *Name, const char *Value) {
    // Parse setup parameters and store their values.
    if (!strcasecmp(Name,"Execution")) setup.ProcessDuring = atoi(Value);
    else if (!strcasecmp(Name,"useVPS")) setup.useVPS = atoi(Value);
    else if (!strcasecmp(Name,"logVPS")) setup.logVPS = atoi(Value);
    else if (!strcasecmp(Name,"whileRecording")) setup.whileRecording = atoi(Value);
    else if (!strcasecmp(Name,"whileReplaying")) setup.whileReplaying = atoi(Value);
    else if (!strcasecmp(Name,"OSDMessage")) setup.OSDMessage = atoi(Value);
    else if (!strcasecmp(Name,"svdrPort")) setup.svdrPort = atoi(Value);
    else if (!strcasecmp(Name,"Verbose")) setup.Verbose = atoi(Value);
    else if (!strcasecmp(Name,"IgnoreMargins")) setup.NoMargins = atoi(Value);
    else if (!strcasecmp(Name,"HideMainMenuEntry")) setup.HideMainMenuEntry = atoi(Value)?true:false;
    else if (!strcasecmp(Name,"SecondPass")) setup.SecondPass = atoi(Value);
    else if (!strcasecmp(Name,"Log2Rec")) setup.Log2Rec = atoi(Value);
    else if (!strcasecmp(Name,"LogoOnly")) setup.LogoOnly = atoi(Value);
    else if (!strcasecmp(Name,"DeferredShutdown")) setup.DeferredShutdown = atoi(Value);
    else if (!strcasecmp(Name,"AutoLogoExtraction")) setup.autoLogoMenu = atoi(Value);
    else if (!strcasecmp(Name,"FullDecode")) setup.fulldecode = atoi(Value);
    else return false;
    return true;
}


const char *cPluginMarkAd::MainMenuEntry(void) {
    if (setup.HideMainMenuEntry)
        return NULL;
    else
        return tr("markad status");
}


bool cPluginMarkAd::Service(const char *UNUSED(Id), void *UNUSED(Data)) {
    // Handle custom service requests from other plugins
    return false;
}


const char **cPluginMarkAd::SVDRPHelpPages(void) {
    // Return help text for SVDRP
    static const char *HelpPage[] = {
        "MARK <filename>\n"
        "     Start markad for the recording with the given filename.",
        "STATUS\n"
        "     show active recordings with running markad",
#ifdef DEBUG_MEM
        "DEBUG_MEM\n"
        "     show current allocated heap memory",
#endif
        NULL
    };
    return HelpPage;
}


bool cPluginMarkAd::ReadTitle(const char *Directory) {
    usleep(1000000); // wait 1 second
    memset(&title, 0, sizeof(title));
    char *buf;
    if (asprintf(&buf, "%s/info",Directory) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    FILE *f;
    f = fopen(buf,"r");
    FREE(strlen(buf)+1, "buf");
    free(buf);
    buf = NULL;
    if (!f) {
        if (asprintf(&buf, "%s/info.vdr",Directory) == -1) return false;
        ALLOC(strlen(buf)+1, "buf");
        f = fopen(buf,"r");
        FREE(strlen(buf)+1, "buf");
        free(buf);
        if (!f) return false;
    }

    char *line = NULL;
    size_t length;
    while (getline(&line, &length, f) != -1) {
        if (line[0] == 'T') {
            int result = sscanf(line, "%*c %79c", title);
            if ((result == 0) || (result == EOF)) {
                title[0] = 0;
            }
            else {
                char *lf = strchr(title, 10);
                if (lf) *lf = 0;
                char *cr = strchr(title, 13);
                if (cr) *cr = 0;
            }
        }
    }
    if (line) {
        FREE(strlen(line)+1, "line");
        free(line);
    }

    fclose(f);
    return (title[0] != 0);
}


cString cPluginMarkAd::SVDRPCommand(const char *Command, const char *Option, int &ReplyCode) {
    // Process SVDRP command
    if (strcasecmp(Command, "MARK") == 0) {
        if (Option) {
            const char *Title = NULL;
            if (ReadTitle(Option)) Title = reinterpret_cast<char *>(&title);
            tChannelID channelID;
            if (statusMonitor->Start(Option, Title, 0, 0, channelID, 0, 0, false, true)) { // start markad via SVDRP command, timerVPS will be detected by markad, we don't know this here
                return cString::sprintf("Started markad for %s", Option);
            }
            else {
                ReplyCode = 451;
                return cString::sprintf("Failed to start markad for %s", Option);
            }
        }
        else {
            ReplyCode = 501;
            return cString::sprintf("Missing filename");
        }
    }
    if (strcasecmp(Command, "STATUS") == 0) {
        return statusMonitor->GetStatus();
    }
#ifdef DEBUG_MEM
    if (strcasecmp(Command,"DEBUG_MEM") == 0) {
        return memListSVDR();
    }
#endif
    return NULL;
}
VDRPLUGINCREATOR(cPluginMarkAd) // Don't touch this!
