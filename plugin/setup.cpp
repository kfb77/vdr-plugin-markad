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
    ioprioclass=setup->IOPrioClass;
    whilerecording=setup->whileRecording;
    whilereplaying=setup->whileReplaying;
    osdmsg=setup->OSDMessage;
    backupmarks=setup->BackupMarks;
    verbose=setup->Verbose;
    genindex=setup->GenIndex;
    nomargins=setup->NoMargins;
    hidemainmenuentry=setup->HideMainMenuEntry;
    secondpass=setup->SecondPass;

    processTexts[0]=tr("after");
    processTexts[1]=tr("during");

    ioprioTexts[0]=tr("high");
    ioprioTexts[1]=tr("normal");
    ioprioTexts[2]=tr("low");
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

    Add(new cMenuEditStraItem(tr("hdd access priority"),&ioprioclass,3,ioprioTexts));

    Add(new cMenuEditBoolItem(tr("repair index, if broken"),&genindex));
    Add(new cMenuEditBoolItem(tr("ignore timer margins"),&nomargins));
    Add(new cMenuEditBoolItem(tr("detect overlaps"),&secondpass));

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
    SetupStore("SecondPass",secondpass);
    SetupStore("OSDMessage",osdmsg);
    SetupStore("Verbose",verbose);
    SetupStore("HideMainMenuEntry",hidemainmenuentry);
    SetupStore("IOPrioClass",ioprioclass);

    setup->ProcessDuring=(bool) processduring;
    setup->whileRecording=(bool) whilerecording;
    setup->whileReplaying=(bool) whilereplaying;
    setup->OSDMessage=(bool) osdmsg;
    setup->GenIndex=(bool) genindex;
    setup->SecondPass=(bool) secondpass;
    setup->BackupMarks=(bool) backupmarks;
    setup->Verbose=(bool) verbose;
    setup->NoMargins=(bool) nomargins;
    setup->HideMainMenuEntry=(bool) hidemainmenuentry;
    setup->IOPrioClass=ioprioclass;
}
