/*
 * menu.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __menu_h_
#define __menu_h_

#include "status.h"


class cOsdMarkAd : public cOsdItem {
private:
    struct sRecording *entry;
public:
    explicit cOsdMarkAd(struct sRecording *Entry);
    struct sRecording *GetEntry() {
        return entry;
    }
};


class cMenuMarkAd : public cOsdMenu {
private:
    cStatusMarkAd *status = nullptr;
    void SetHelpText(struct sRecording *Entry);
    bool write();
    time_t last = time(nullptr);;
    int lastpos = 0;
public:
    explicit cMenuMarkAd(cStatusMarkAd *Status);
    eOSState ProcessKey(eKeys Key);
};
#endif
