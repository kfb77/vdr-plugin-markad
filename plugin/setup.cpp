/*
 * setup.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "setup.h"
#include "debug.h"


cSetupMarkAd::cSetupMarkAd(struct setup *Setup) {
    setup=Setup;

    processduring=setup->ProcessDuring;
    usevps=setup->useVPS;
    logvps=setup->logVPS;
    whilerecording=setup->whileRecording;
    whilereplaying=setup->whileReplaying;
    osdmsg=setup->OSDMessage;
    verbose=setup->Verbose;
    genindex=setup->GenIndex;
    nomargins=setup->NoMargins;
    hidemainmenuentry=setup->HideMainMenuEntry;
    secondpass=setup->SecondPass;
    log2rec=setup->Log2Rec;
    logoonly=setup->LogoOnly;
    saveinfo=setup->SaveInfo;
    deferredshutdown=setup->DeferredShutdown;
    autologomenue=setup->autoLogoMenue;

    processTexts[PROCESS_AFTER]=tr("after");
    processTexts[PROCESS_DURING]=tr("during");
    processTexts[PROCESS_NEVER]=tr("never");

    autoLogoTexts[0]=tr("disable");
    autoLogoTexts[1]=tr("enable for low memory systems");
    autoLogoTexts[2]=tr("enable");

    lpos=0;

    write();
}


void cSetupMarkAd::write(void) {
    int current=Current();
    Clear();
    cMenuEditStraItem *first=new cMenuEditStraItem(tr("execution"),&processduring,3,processTexts);
    if (!first) return;
    Add(first);
    Add(new cMenuEditBoolItem(tr("use VPS"),&usevps));
    if (usevps) Add(new cMenuEditBoolItem(tr("log VPS events"),&logvps));
    if (processduring < PROCESS_NEVER) {
        if (!processduring)
        {
            Add(new cMenuEditBoolItem(tr("  during another recording"),&whilerecording));
            Add(new cMenuEditBoolItem(tr("  while replaying"),&whilereplaying));
        }
        Add(new cMenuEditBoolItem(tr("scan only channels with logo"),&logoonly),true);
        lpos=Current();
        Add(new cMenuEditBoolItem(tr("deferred shutdown"),&deferredshutdown));
        Add(new cMenuEditBoolItem(tr("ignore timer margins"),&nomargins));
        Add(new cMenuEditBoolItem(tr("detect overlaps"),&secondpass));
        Add(new cMenuEditBoolItem(tr("recreate index"),&genindex));
        Add(new cMenuEditBoolItem(tr("correct info file"),&saveinfo));
        Add(new cMenuEditBoolItem(tr("OSD message"),&osdmsg));
        Add(new cMenuEditBoolItem(tr("verbose logging"),&verbose));
        Add(new cMenuEditBoolItem(tr("log to recording directory"),&log2rec));
        Add(new cMenuEditBoolItem(tr("hide mainmenu entry"),&hidemainmenuentry));
        if (setup->autoLogoConf < 0) Add(new cMenuEditStraItem(tr("extract logos from recording"),&autologomenue,3,autoLogoTexts));

        if (current==-1) {
            SetCurrent(first);
        }
        else {
            SetCurrent(Get(current));
        }
    }
    else {
        lpos=-1;
    }
    Display();
}


eOSState cSetupMarkAd::ProcessKey(eKeys Key) {
    eOSState state = cOsdMenu::ProcessKey(Key);
    if (HasSubMenu()) return osContinue;
    switch (state) {
        case osContinue:
            if (((Key==kLeft) || (Key==kRight)) && (Current()==0)) write();
            if ((Key==kDown) || (Key==kUp)) {
                if (Current()==lpos) {
                    SetHelp(NULL,NULL,NULL,tr("show list"));
                }
                else {
                    SetHelp(NULL,NULL,NULL,NULL);
                }
            }
            break;
        case osUnknown:
            if ((Key==kBlue) && (Current()==lpos)) return AddSubMenu(new cSetupMarkAdList(setup));
            if (Key==kOk) {
                Store();
                state=osBack;
            }
            break;
        default:
            break;
    }
    return state;
}


void cSetupMarkAd::Store(void) {
    SetupStore("Execution",processduring);
    if (processduring!=0) {
        whilerecording=1;
        whilereplaying=1;
    }
    SetupStore("useVPS",usevps);
    SetupStore("logVPS",logvps);
    SetupStore("whileRecording",whilerecording);
    SetupStore("whileReplaying",whilereplaying);
    SetupStore("IgnoreMargins",nomargins);
    SetupStore("GenIndex",genindex);
    SetupStore("SecondPass",secondpass);
    SetupStore("OSDMessage",osdmsg);
    SetupStore("Verbose",verbose);
    SetupStore("HideMainMenuEntry",hidemainmenuentry);
    SetupStore("Log2Rec",log2rec);
    SetupStore("LogoOnly",logoonly);
    SetupStore("SaveInfo",saveinfo);
    SetupStore("DeferredShutdown",deferredshutdown);
    SetupStore("AutoLogoExtraction",autologomenue);

    setup->ProcessDuring=(int) processduring;
    setup->useVPS=(bool) usevps;
    setup->logVPS=(bool) logvps;
    setup->whileRecording=(bool) whilerecording;
    setup->whileReplaying=(bool) whilereplaying;
    setup->OSDMessage=(bool) osdmsg;
    setup->GenIndex=(bool) genindex;
    setup->SecondPass=(bool) secondpass;
    setup->Verbose=(bool) verbose;
    setup->NoMargins=(bool) nomargins;
    setup->HideMainMenuEntry=(bool) hidemainmenuentry;
    setup->DeferredShutdown=(bool) deferredshutdown;
    setup->autoLogoMenue=(int) autologomenue;
    setup->Log2Rec=log2rec;
    setup->LogoOnly=logoonly;
    setup->SaveInfo=saveinfo;
}


#if APIVERSNUM>=20301
#define CHNUMWIDTH (numdigits(cChannels::MaxNumber())+1)
#else
#define CHNUMWIDTH (numdigits(Channels.MaxNumber())+1)
#endif


cSetupMarkAdList::cSetupMarkAdList(struct setup *Setup) :cOsdMenu("",CHNUMWIDTH) {
    SetTitle(cString::sprintf("%s - %s '%s' %s",trVDR("Setup"),trVDR("Plugin"),Setup->PluginName,tr("list")));
    SetHelp(NULL,NULL,NULL,tr("back"));

    DIR *dir=opendir(Setup->LogoDir);
    if (!dir) return;
    struct dirent *dirent;
    while (dirent=readdir(dir)) {
        if (dirent->d_name[0]=='.') continue;
        if (strstr(dirent->d_name,"-P0.pgm")) {
            char *name=strdup(dirent->d_name);
            if (name) {
                ALLOC(strlen(name)+1, "name");
                char *m=strchr(name,'-');
                if (m) *m=0;

#if APIVERSNUM>=20301
                cStateKey StateKey;
                if (const cChannels *Channels = cChannels::GetChannelsRead(StateKey)) {
                    for (const cChannel *channel=Channels->First(); channel; channel=Channels->Next(channel))
#else
                for (cChannel *channel = Channels.First(); channel; channel = Channels.Next(channel))
#endif
                    {
                        if (channel->Name()) {
                            char *cname=strdup(channel->Name());
                            if (cname) {
                                ALLOC(strlen(cname)+1, "cname");
                                for (int i=0; i<(int) strlen(cname); i++) {
                                    if (cname[i]==' ') cname[i]='_';
                                    if (cname[i]=='.') cname[i]='_';
                                    if (cname[i]=='/') cname[i]='_';
                                }
                                if (!strcmp(name,cname)) {
                                    Add(new cSetupMarkAdListItem(cString::sprintf("%i\t%s",channel->Number(),channel->Name())));
                                    FREE(strlen(cname)+1, "cname");
                                    free(cname);
                                    break;
                                }
                                FREE(strlen(cname)+1, "cname");
                                free(cname);
                            }
                        }
                    }
                    FREE(strlen(name)+1, "name");
                    free(name);
#if APIVERSNUM>=20301
                    StateKey.Remove();
                }
#endif
            }
        }
    }
    Sort();
    closedir(dir);
}


int cSetupMarkAdListItem::Compare(const cListObject &ListObject) const {
    const cSetupMarkAdListItem *la=(cSetupMarkAdListItem *) &ListObject;
    const char *t1=strchr(Text(),'\t');
    const char *t2=strchr(la->Text(),'\t');
    if ((t1) && (t2)) {
        return strcasecmp(t1,t2);
    }
    else {
        return 0;
    }
}


eOSState cSetupMarkAdList::ProcessKey (eKeys Key) {
    eOSState state = cOsdMenu::ProcessKey(Key);
    if (HasSubMenu()) return osContinue;
    if (state==osUnknown) {
        switch (Key) {
            case kOk:
            case kBlue:
            case kBack:
                state=osBack;
                break;
            default:
                break;
        }
    }
    return state;
}
