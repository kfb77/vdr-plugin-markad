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


cOsdMarkAd::cOsdMarkAd(struct sRecording *Entry) {
    entry = Entry;
    const char *status;

    switch (entry->status) {
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

    char *buf = nullptr;
    if (asprintf(&buf, "%s\t %s", entry->title ? entry->title : entry->fileName,status) != -1) {
        ALLOC(strlen(buf)+1, "buf");
        SetText(buf,true);
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    else {
        // fallback
        SetText(entry->title ? entry->title : entry->fileName, true);
    }
}


cMenuMarkAd::cMenuMarkAd(cStatusMarkAd *Status):cOsdMenu(tr("markad status"), 15) {
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
        SetHelpText(nullptr);
    }
    lastpos = 0;
}


bool cMenuMarkAd::write() {
    Clear();

    bool header=false;
    struct sRecording *Entry=nullptr;
    status->ResetActPos();
    do {
        status->GetNextActive(&Entry);
        if (Entry) {
            if (!header) {
                header=true;
                Add(new cOsdItem(tr("Recording\t Status"),osUnknown,false));  // freed by vdr on menu close
            }
            cOsdMarkAd *osd = new cOsdMarkAd(Entry);  // freed by vdr on menu close
            if (osd) {
                Add(osd);
                if (osd->Index()==lastpos) SetCurrent(osd);
            }
        }
    }
    while (Entry);

    if (!header) {
        Add(new cOsdItem(tr("no running markad found"),osUnknown,false),true);  // freed by vdr on menu close
        lastpos=0;
    }
    Display();
    return header;
}


void cMenuMarkAd::SetHelpText(struct sRecording *Entry) {
    if (!Entry) {
        SetHelp(nullptr,nullptr);
        return;
    }

    switch (Entry->status) {
    case 'R':
    case 'S':
        SetHelp(tr("Pause"),nullptr);
        break;
    case 'D':
    case 'Z':
        SetHelp(nullptr,nullptr);
        break;
    case 'T':
        SetHelp(nullptr,tr("Continue"));
        break;
    default:
        SetHelp(nullptr,nullptr);
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
            struct sRecording *entry=osd->GetEntry();
            if ((entry) && (entry->pid) && (entry->status!='T')) {
                dsyslog("sending TSTP to %i",entry->pid);
                kill(entry->pid,SIGTSTP);
                entry->changedByUser=true;
                SetHelp(nullptr,tr("Continue"));
            }
        }
        break;
    case kGreen:
        osd=static_cast<cOsdMarkAd *>(Get(Current()));
        if ((osd) && (osd->Selectable())) {
            struct sRecording *entry=osd->GetEntry();
            if ((entry) && (entry->pid)) {
                dsyslog("sending CONT to %i",entry->pid);
                kill(entry->pid,SIGCONT);
                entry->changedByUser=true;
                SetHelp(tr("Pause"),nullptr);
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
        if (time(nullptr)>(last+2)) {
            if (write()) {
                cOsdMarkAd *osdCurrent = static_cast<cOsdMarkAd *>(Get(Current()));
                if ((osdCurrent) && (osdCurrent->Selectable())) {
                    SetHelpText(osdCurrent->GetEntry());
                }
            }
            else {
                SetHelpText(nullptr);
            }
            last = time(nullptr);
        }
        break;
    default:
        state = cOsdMenu::ProcessKey(Key);
        break;
    }
    return state;
}
