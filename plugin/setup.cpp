/*
 * setup.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "setup.h"

cSetupMarkAd::cSetupMarkAd(struct setup *Setup)
{
    setup=Setup;

    processduring=setup->ProcessDuring;
    whilerecording=setup->whileRecording;
    whilereplaying=setup->whileReplaying;
    osdmsg=setup->OSDMessage;
    backupmarks=setup->BackupMarks;
    verbose=setup->Verbose;
    genindex=setup->GenIndex;
    nomargins=setup->NoMargins;
hidemainmenuentry=setup->HideMainMenuEntry;

    processTexts[0]=tr("after");
    processTexts[1]=tr("during");

    write();
}

void cSetupMarkAd::write(void)
{

    Clear();
    Add(new cMenuEditStraItem(tr("execution"),&processduring,2,processTexts));
    if (!processduring)
    {
        Add(new cMenuEditBoolItem(tr("  during another recording"),&whilerecording));
        Add(new cMenuEditBoolItem(tr("  while replaying"),&whilereplaying));
    }

    Add(new cMenuEditBoolItem(tr("repair index, if broken"),&genindex));
    Add(new cMenuEditBoolItem(tr("ignore timer margins"),&nomargins));

    Add(new cMenuEditBoolItem(tr("OSD message"),&osdmsg));
    Add(new cMenuEditBoolItem(tr("backup marks"),&backupmarks));
    Add(new cMenuEditBoolItem(tr("verbose logging"),&verbose));
    Add(new cMenuEditBoolItem(tr("Hide Mainmenu entry"),&hidemainmenuentry)); 

    Display();
}

eOSState cSetupMarkAd::ProcessKey(eKeys Key)
{

    eOSState state=osUnknown;
    switch (Key)
    {
    case kLeft:
        state=cMenuSetupPage::ProcessKey(Key);
        if (Current()==0) write();
        break;

    case kRight:
        state=cMenuSetupPage::ProcessKey(Key);
        if (Current()==0) write();
        break;

    default:
        state=cMenuSetupPage::ProcessKey(Key);
        break;
    }
    return state;
}

void cSetupMarkAd::Store(void)
{
    SetupStore("Execution",processduring);
    SetupStore("whileRecording",whilerecording);
    SetupStore("whileReplaying",whilereplaying);
    SetupStore("IgnoreMargins",nomargins);
    SetupStore("BackupMarks",backupmarks);
    SetupStore("GenIndex",genindex);
    SetupStore("OSDMessage",osdmsg);
    SetupStore("Verbose",verbose);
    SetupStore("HideMainMenuEntry",hidemainmenuentry);

    setup->ProcessDuring=(bool) processduring;
    setup->whileRecording=(bool) whilerecording;
    setup->whileReplaying=(bool) whilereplaying;
    setup->OSDMessage=(bool) osdmsg;
    setup->GenIndex=(bool) genindex;
    setup->BackupMarks=(bool) backupmarks;
    setup->Verbose=(bool) verbose;
    setup->NoMargins=(bool) nomargins;
    setup->HideMainMenuEntry=(bool) hidemainmenuentry;
}
