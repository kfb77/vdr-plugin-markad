/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>
#include <chrono>
#include "setup.h"

#if __GNUC__ > 3
#define UNUSED(v) UNUSED_ ## v __attribute__((unused))
#else
#define UNUSED(x) x
#endif


// epg event log
class cEpgEventLog {
    public:
       explicit cEpgEventLog(const char *recDir);
       ~cEpgEventLog();
       void Log(const time_t recStart, const int state, const int event, const int newState, const char* action);
    private:
       FILE *eventLogFile = NULL;
};


struct recs {
    char *Name = NULL;
    char *FileName = NULL;
    pid_t Pid = 0;
    char Status = 0;
    bool ChangedbyUser = false;
    tEventID eventID = 0;
    int runningStatus = 0;
    time_t recStart = 0;
    time_t vpsStartTime = 0;
    time_t vpsStopTime = 0;
    time_t vpsPauseStartTime = 0;
    time_t vpsPauseStopTime = 0;
    bool vpsValid = true;
    cEpgEventLog *epgEventLog;
};


class cEpgHandlerMarkad;


// --- cStatusMarkAd
class cStatusMarkAd : public cStatus {
    private:
        struct recs recs[MAXDEVICES*MAXRECEIVERS] = {0};
        int max_recs = -1;
        struct setup *setup;
        const char *bindir;
        const char *logodir;
        int actpos;
        bool getPid(int Position);
        bool getStatus(int Position);
        int Recording();
        bool Replaying();
        int Get(const char *FileName, const char *Name=NULL);
        int Add(const char *FileName, const char *Name, const tEventID eventID);
        void Remove(int Position, bool Kill=false);
        void Remove(const char *Name, bool Kill=false);
        void Pause(const char *FileName);
        void Continue(const char *FileName);
        bool LogoExists(const cDevice *Device, const char *FileName);
        tEventID GetEventID(const cDevice *Device,const char *FileName);
        void SaveVPSStatus(const int index);
        bool StoreVPSStatus(const char *status, int index);
        cEpgHandlerMarkad *epgHandlerMarkad = NULL;
    protected:
        virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
        virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On);
        virtual void TimerChange(const cTimer *Timer, eTimerChange Change);
    public:
        cStatusMarkAd(const char *BinDir,const char *LogoDir, struct setup *Setup);
        ~cStatusMarkAd();
        bool MarkAdRunning(void);
        void ResetActPos(void) {
            actpos=0;
        }
        void Check(void);
        bool GetNextActive(struct recs **RecEntry);
        bool Start(const char *FileName, const char *Name, const tEventID eventID, const bool Direct=false);
        void SetVPSStatus(const tEventID eventID, const int runningStatus);
};


// epg handler
class cEpgHandlerMarkad : public cEpgHandler {
    public:
        explicit cEpgHandlerMarkad(cStatusMarkAd *statusMonitor) {
            StatusMarkAd = statusMonitor;
        };
        ~cEpgHandlerMarkad(void) {};
        virtual bool HandleEitEvent(cSchedule *Schedule, const SI::EIT::Event *EitEvent, uchar TableID, uchar Version);
//        virtual bool HandleEvent(cEvent *Event);
    private:
        cStatusMarkAd *StatusMarkAd = NULL;
};
#endif
