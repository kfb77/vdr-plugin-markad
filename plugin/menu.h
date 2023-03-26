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
        struct sRecordings *entry;
    public:
        explicit cOsdMarkAd(struct sRecordings *Entry);
        struct sRecordings *GetEntry() {
            return entry;
        }
};


class cMenuMarkAd : public cOsdMenu {
    private:
        cStatusMarkAd *status = NULL;
        void SetHelpText(struct sRecordings *Entry);
        bool write();
        time_t last = time(NULL);;
        int lastpos = 0;
    public:
        explicit cMenuMarkAd(cStatusMarkAd *Status);
        eOSState ProcessKey(eKeys Key);
};
#endif
