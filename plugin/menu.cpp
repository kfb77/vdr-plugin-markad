/*
 * menu.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <signal.h>
#include <vdr/menu.h>
#include <vdr/font.h>

#include "menu.h"
#include "debug.h"


cOsdMarkAd::cOsdMarkAd(struct recs *Entry) {
    entry = Entry;
    const char *status;

    switch (entry->Status) {
        case 'R':
            status = tr("running");
            break;
        case 'S':
            status = tr("sleeping");
            break;
        case 'D':
            status = tr("inactive");
            break;
        case 'Z':
            status = tr("zombie");
            break;
        case 'T':
            status = tr("stopped");
            break;
        default:
           status = tr("unknown");
           break;
    }

    char *buf = NULL;
    if (asprintf(&buf, "%s\t %s", entry->Name ? entry->Name : entry->FileName,status) != -1) {
        ALLOC(strlen(buf)+1, "buf");
        SetText(buf,true);
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    else {
        // fallback
        SetText(entry->Name ? entry->Name : entry->FileName,true);
    }
}


cMenuMarkAd::cMenuMarkAd(cStatusMarkAd *Status):cOsdMenu(tr("markad status"),15) {
    status = Status;
    int width = 0;

    cSkinDisplayMenu *disp=DisplayMenu();
    if (disp) {
        width = disp->GetTextAreaWidth();
    }
    if (!width) width = Setup.OSDWidth;

    int AvgCharWidth = Setup.FontOsdSize * 3 / 5; // see skins.c
    if (AvgCharWidth < 1) AvgCharWidth = 1;
    int tab = (width - 10 * AvgCharWidth) / AvgCharWidth;
    SetCols(tab);

    if (write()) {
        cOsdMarkAd *osd = static_cast<cOsdMarkAd *>(Get(Current()));
        if ((osd) && (osd->Selectable())) {
            SetHelpText(osd->GetEntry());
        }
    }
    else {
        SetHelpText(NULL);
    }
    lastpos = 0;
}


bool cMenuMarkAd::write() {
    Clear();

    bool header=false;
    struct recs *Entry=NULL;
    status->ResetActPos();
    do {
        status->GetNextActive(&Entry);
        if (Entry) {
            if (!header) {
                header=true;
                Add(new cOsdItem(tr("Recording\t Status"),osUnknown,false));  // freed by vdr on menue close
            }
            cOsdMarkAd *osd = new cOsdMarkAd(Entry);  // freed by vdr on menue close
            if (osd) {
                Add(osd);
                if (osd->Index()==lastpos) SetCurrent(osd);
            }
        }
    }
    while (Entry);

    if (!header) {
        Add(new cOsdItem(tr("no running markad found"),osUnknown,false),true);  // freed by vdr on menue close
        lastpos=0;
    }
    Display();
    return header;
}


void cMenuMarkAd::SetHelpText(struct recs *Entry) {
    if (!Entry) {
        SetHelp(NULL,NULL);
        return;
    }

    switch (Entry->Status) {
        case 'R':
        case 'S':
            SetHelp(tr("Pause"),NULL);
            break;
        case 'D':
        case 'Z':
            SetHelp(NULL,NULL);
            break;
        case 'T':
            SetHelp(NULL,tr("Continue"));
            break;
        default:
            SetHelp(NULL,NULL);
            break;
    }
}


eOSState cMenuMarkAd::ProcessKey(eKeys Key) {
    cOsdMarkAd *osd;

    eOSState state=osUnknown;
    switch (Key) {
        case kRed:
            osd=static_cast<cOsdMarkAd *>(Get(Current()));
            if ((osd) && (osd->Selectable())) {
                struct recs *entry=osd->GetEntry();
                if ((entry) && (entry->Pid) && (entry->Status!='T')) {
                    dsyslog("sending TSTP to %i",entry->Pid);
                    kill(entry->Pid,SIGTSTP);
                    entry->ChangedbyUser=true;
                    SetHelp(NULL,tr("Continue"));
                }
            }
            break;
        case kGreen:
            osd=static_cast<cOsdMarkAd *>(Get(Current()));
            if ((osd) && (osd->Selectable())) {
                struct recs *entry=osd->GetEntry();
                if ((entry) && (entry->Pid)) {
                    dsyslog("sending CONT to %i",entry->Pid);
                    kill(entry->Pid,SIGCONT);
                    entry->ChangedbyUser=true;
                    SetHelp(tr("Pause"),NULL);
                }
            }
            break;
        case kUp:
            CursorUp();
            osd=static_cast<cOsdMarkAd *>(Get(Current()));
            if ((osd) && (osd->Selectable())) {
                SetHelpText(osd->GetEntry());
                lastpos=Current();
            }
            break;
        case kDown:
            CursorDown();
            osd=static_cast<cOsdMarkAd *>(Get(Current()));
            if ((osd) && (osd->Selectable())) {
                SetHelpText(osd->GetEntry());
                lastpos=Current();
            }
            break;
        case kOk:
            state = osBack;
            break;
        case kNone:
            if (time(NULL)>(last+2)) {
                if (write()) {
                    cOsdMarkAd *osdCurrent = static_cast<cOsdMarkAd *>(Get(Current()));
                    if ((osdCurrent) && (osdCurrent->Selectable())) {
                        SetHelpText(osdCurrent->GetEntry());
                    }
                }
                else {
                    SetHelpText(NULL);
                }
                last = time(NULL);
            }
            break;
        default:
            state = cOsdMenu::ProcessKey(Key);
            break;
    }
    return state;
}
