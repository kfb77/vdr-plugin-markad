/*
 * menu.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "menu.h"

cOsdMarkAd::cOsdMarkAd(struct recs *Entry)
{
    entry=Entry;

    const char *status;

    switch (entry->Status)
    {
    case 'R':
        status=tr("running");
        break;
    case 'S':
        status=tr("sleeping");
        break;
    case 'D':
        status=tr("inactive");
        break;
    case 'Z':
        status=tr("zombie");
        break;
    case 'T':
        status=tr("stopped");
        break;
    default:
        status=tr("unknown");
        break;
    }

    char *buf=NULL;
    if (asprintf(&buf,"%s\t %s",entry->Name,status)!=-1)
    {
        SetText(buf,true);
        free(buf);
    }
    else
    {
        // fallback
        SetText(entry->Name,true);
    }
}

cMenuMarkAd::cMenuMarkAd(cStatusMarkAd *Status):cOsdMenu(tr("markad status"),15)
{
    status=Status;
    last=time(NULL);

    int width;
    cSkinDisplayMenu *disp=DisplayMenu();
    if (disp)
    {
        width=disp->GetTextAreaWidth();
    }
    else
    {
        width=Setup.OSDWidth;
    }

    int AvgCharWidth = Setup.FontOsdSize * 3 / 5; // see skins.c
    int tab=(width-10*AvgCharWidth)/AvgCharWidth;
    SetCols(tab);

    if (write())
    {
        cOsdMarkAd *osd=(cOsdMarkAd *) Get(Current());
        if ((osd) && (osd->Selectable()))
        {
            SetHelpText(osd->GetEntry());
        }
    }
    else
    {
        SetHelpText(NULL);
    }
}


bool cMenuMarkAd::write()
{
    Clear();

    bool header=false,first=true;
    struct recs *Entry=NULL;
    status->ResetActPos();
    do
    {
        status->GetNextActive(&Entry);
        if ((Entry) && (Entry->Name))
        {
            if (!header)
            {
                header=true;
                Add(new cOsdItem(tr("Recording\t Status"),osUnknown,false));
            }
            Add(new cOsdMarkAd(Entry),first);
            first=false;
        }
    }
    while (Entry);

    if (!header)
    {
        Add(new cOsdItem(tr("no running markad found"),osUnknown,false),true);
    }
    Display();
    return header;
}

void cMenuMarkAd::SetHelpText(struct recs *Entry)
{
    if (!Entry)
    {
        SetHelp(NULL,NULL);
        return;
    }

    switch (Entry->Status)
    {
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

eOSState cMenuMarkAd::ProcessKey(eKeys Key)
{
    cOsdMarkAd *osd;

    eOSState state=osUnknown;
    switch (Key)
    {
    case kRed:
        osd=(cOsdMarkAd *) Get(Current());
        if ((osd) && (osd->Selectable()))
        {
            struct recs *entry=osd->GetEntry();
            if ((entry) && (entry->Pid))
            {
                dsyslog("sending TSTP to %i",entry->Pid);
                kill(entry->Pid,SIGTSTP);
                SetHelp(NULL,tr("Continue"));
            }
        }
        break;

    case kGreen:
        osd=(cOsdMarkAd *) Get(Current());
        if ((osd) && (osd->Selectable()))
        {
            struct recs *entry=osd->GetEntry();
            if ((entry) && (entry->Pid))
            {
                dsyslog("sending CONT to %i",entry->Pid);
                kill(entry->Pid,SIGCONT);
                SetHelp(tr("Pause"),NULL);
            }
        }
        break;

    case kUp:
        CursorUp();
        osd=(cOsdMarkAd *) Get(Current());
        if ((osd) && (osd->Selectable()))
        {
            SetHelpText(osd->GetEntry());
        }
        break;

    case kDown:
        CursorDown();
        osd=(cOsdMarkAd *) Get(Current());
        if ((osd) && (osd->Selectable()))
        {
            SetHelpText(osd->GetEntry());
        }
        break;

    case kOk:
        state=osBack;
        break;

    case kNone:
        if (time(NULL)>(last+2))
        {
            if (write())
            {
                cOsdMarkAd *osd=(cOsdMarkAd *) Get(Current());
                if ((osd) && (osd->Selectable()))
                {
                    SetHelpText(osd->GetEntry());
                }
            }
            else
            {
                SetHelpText(NULL);
            }
            last=time(NULL);
        }
        break;

    default:
        state=cOsdMenu::ProcessKey(Key);
        break;
    }
    return state;
}
