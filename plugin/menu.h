/*
 * menu.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __menu_h_
#define __menu_h_

#include <signal.h>
#include <vdr/menu.h>
#include <vdr/font.h>
#include "status.h"

class cOsdMarkAd : public cOsdItem
{
private:
    struct recs *entry;
public:
    cOsdMarkAd(struct recs *Entry);
    struct recs *GetEntry()
    {
        return entry;
    }
};

class cMenuMarkAd : public cOsdMenu
{
private:
    cStatusMarkAd *status;
    void SetHelpText(struct recs *Entry);
    bool write();
    time_t last;
    int lastpos;
public:
    cMenuMarkAd(cStatusMarkAd *Status);
    eOSState ProcessKey(eKeys Key);
};

#endif
