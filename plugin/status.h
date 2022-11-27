/*
 * status.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#ifndef __status_h_
#define __status_h_

#include <vdr/status.h>
// #include <chrono>
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
       cEpgEventLog(const cEpgEventLog &origin) {   //  copy constructor, not used, only for formal reason
           eventLogFile = NULL;
       };
       cEpgEventLog &operator =(const cEpgEventLog *origin) {   // operator=, not used, only for formal reason
           eventLogFile = NULL;
           return *this;
       };
       void Log(const time_t recStart, const int state, const int event, const int newState, const char* action);
       void Log(const char *message);
    private:
       FILE *eventLogFile = NULL;
};


struct recs {
    char     *Name             = NULL;
    char     *FileName         = NULL;
    pid_t    Pid               = 0;
    char     Status            = 0;
    bool     ChangedbyUser     = false;
    tEventID eventID           = 0;
    tEventID eitEventID        = 0;
    time_t   timerStartTime    = 0;
    time_t   timerStopTime     = 0;
    bool     timerVPS          = false;
    int      runningStatus     = 0;
    time_t   recStart          = 0;
    time_t   vpsStartTime      = 0;
    time_t   vpsStopTime       = 0;
    time_t   vpsPauseStartTime = 0;
    time_t   vpsPauseStopTime  = 0;
    cEpgEventLog *epgEventLog;
};


class cEpgHandlerMarkad;


// --- cStatusMarkAd
class cStatusMarkAd : public cStatus {
    private:
        struct recs recs[MAXDEVICES*MAXRECEIVERS];
        int        max_recs = -1;
        struct     setup *setup;
        const char *bindir;
        const char *logodir;
        int        actpos;
        int        runningRecordings = 0;

        bool getPid(int Position);
        bool getStatus(int Position);
        bool Replaying();
        int Get(const char *FileName, const char *Name = NULL);
        int Add(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime, bool timerVPS);
        void Remove(int Position, bool Kill = false);
        void Remove(const char *Name, bool Kill = false);
        void Pause(const char *FileName);
        void Continue(const char *FileName);
        bool LogoExists(const cDevice *Device, const char *FileName);
        void GetEventID(const cDevice *Device,const char *FileName, tEventID *eventID, time_t *timerStartTime, time_t *timerStopTime, bool *timerVPS);
        void SaveVPSTimer(const char *FileName, const bool timerVPS);
        void SaveVPSEvents(const int index);
        bool StoreVPSStatus(const char *status, const int index);
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
        char *GetStatus();
        void Check(void);
        bool GetNextActive(struct recs **RecEntry);
        bool Start(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime, const bool timerVPS, const bool Direct);
        void SetVPSStatus(const cSchedule *Schedule, const SI::EIT::Event *EitEvent);
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
