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

enum eVpsLog {
    VPS_ERROR = 0,
    VPS_INFO  = 1,
    VPS_DEBUG = 2,
};


class cEpgEventLog;

struct sRecording {
    char        *title             = nullptr;
    char        *fileName          = nullptr;
    pid_t        pid               = 0;
    char         status            = 0;
    bool         changedByUser     = false;
    bool         ignoreEIT         = false;
    // event
    char        *eventTitle        = nullptr;
    tEventID     eventID           = 0;
    tEventID     eventNextID       = 0;
    tEventID     eitEventID        = 0;
    tEventID     eitEventNextID    = 0;
    time_t       eventStartTime    = 0;
    time_t       eventStopTime     = 0;
    tChannelID   eventChannelID    = tChannelID::InvalidID;
    // timer
    time_t       timerStartTime    = 0;
    time_t       timerStopTime     = 0;
    bool         timerVPS          = false;
    tChannelID   timerChannelID    = tChannelID::InvalidID;
    char        *timerChannelName  = nullptr;
    // VPS
    int          runningStatus     = 0;
    time_t       recStart          = 0;
    time_t       vpsStartTime      = 0;
    time_t       vpsStopTime       = 0;
    time_t       vpsPauseStartTime = 0;
    time_t       vpsPauseStopTime  = 0;
    cEpgEventLog *epgEventLog;
};


// epg event log
class cEpgEventLog {
public:
    explicit cEpgEventLog(const char *recDir);
    ~cEpgEventLog();
    cEpgEventLog(const cEpgEventLog &origin) {   //  copy constructor, not used, only for formal reason
        eventLogFile = nullptr;
    };
    cEpgEventLog &operator =(const cEpgEventLog *origin) {   // operator=, not used, only for formal reason
        eventLogFile = nullptr;
        return *this;
    };
    void LogState(const int severity, const sRecording *recording, const int newState, const char* action);
    void LogEvent(const int severity, const char *title, char *eventLog);
private:
    FILE *eventLogFile = nullptr;
};


class cEpgHandlerMarkad;


// --- cStatusMarkAd
class cStatusMarkAd : public cStatus {
private:
    struct sRecording recs[MAXDEVICES*MAXRECEIVERS];
    int             max_recs          = -1;
    const char      *bindir           = nullptr;
    const char      *logodir          = nullptr;
    int             actpos            = 0;
    struct          setup *setup      = nullptr;
    int             runningRecordings = 0;

    bool getPid(int Position);
    bool getStatus(int Position);
    bool Replaying();
    int Get(const char *FileName, const char *Name = nullptr);
    int Add(const char *Name, const char *FileName, sRecording *recording);
    void Remove(int pos, bool Kill = false);
    void Remove(const char *Name, bool Kill = false);
    void Pause(const char *FileName);
    void Continue(const char *FileName);
    bool LogoExists(const cDevice *Device, const char *FileName);
    void GetEventID(const cDevice *Device,const char *Name, sRecording *recording);
    void SaveVPSTimer(const char *FileName, const bool timerVPS);
    void SaveVPSEvents(const int index);
    bool StoreVPSStatus(const char *status, const int index);
    cEpgHandlerMarkad *epgHandlerMarkad = nullptr;
protected:
    virtual void Recording(const cDevice *Device, const char *Name, const char *FileName, bool On);
    virtual void Replaying(const cControl *Control, const char *Name, const char *FileName, bool On);
    virtual void TimerChange(const cTimer *Timer, eTimerChange Change);
public:
    cStatusMarkAd(const char *BinDir, const char *LogoDir, struct setup *Setup);
    ~cStatusMarkAd();
    bool MarkAdRunning(void);
    void ResetActPos(void) {
        actpos=0;
    }
    char *GetStatus();
    void Check(void);
    bool GetNextActive(struct sRecording **RecEntry);
    bool Start(const char *Name, const char *FileName, const bool direct, sRecording *recording);
    int Get_EIT_EventID(const sRecording *recording, const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *schedule, const bool nextEvent);
    void FindRecording(const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *Schedule);
    void SetVPSStatus(const int index, int runningStatus, const bool eventEIT);
};


// epg handler
class cEpgHandlerMarkad : public cEpgHandler {
public:
    explicit cEpgHandlerMarkad(cStatusMarkAd *statusMonitor) {
        StatusMarkAd = statusMonitor;
    };
    ~cEpgHandlerMarkad(void) {};
    virtual bool HandleEitEvent(cSchedule *Schedule, const SI::EIT::Event *EitEvent, uchar TableID, uchar Version);
    virtual bool HandleEvent(cEvent *Event);
private:
    cStatusMarkAd *StatusMarkAd = nullptr;
    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
};
#endif
