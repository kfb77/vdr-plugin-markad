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

    processduring=(int) setup->ProcessDuring;
    whilerecording=setup->whileRecording;
    whileplaying=setup->whilePlaying;

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
        Add(new cMenuEditBoolItem(tr("while recording"),&whilerecording));
        Add(new cMenuEditBoolItem(tr("while playing"),&whileplaying));
    }
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
    SetupStore("whilePlaying",whileplaying);

    setup->ProcessDuring=(bool) processduring;
    setup->whileRecording=(bool) whilerecording;
    setup->whilePlaying=(bool) whileplaying;
}
