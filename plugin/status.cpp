/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <signal.h>
#include "status.h"
#include "setup.h"
#include "debug.h"


cEpgEventLog::cEpgEventLog(const char *recDir) {
    if (!recDir) return;
    char *eventLogName = NULL;

    if (asprintf(&eventLogName, "%s/%s", recDir, "vps.log") == -1) {
        esyslog("markad: cEpgEventLog::cEpgEventLog(): VPS event logfile asprintf failed");
        return;
    }
    ALLOC(strlen(eventLogName)+1, "eventLogName");

    eventLogFile = fopen(eventLogName, "a");
    if (!eventLogFile) {
        esyslog("markad: eventLogFile(): VPS event logfile <%s> open file failed", eventLogName);
    }
    FREE(strlen(eventLogName)+1, "eventLogName");
    free(eventLogName);
}


cEpgEventLog::~cEpgEventLog() {
    fclose(eventLogFile);
}


void cEpgEventLog::Log(const time_t recStart,  const int event, const int state, const int newState, const char* action) {
    if (!eventLogFile) return;
    if (!action) return;
    char *message = NULL;

    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeNow[20] = {0};
    strftime(timeNow, 20, "%d.%m.%Y %H:%M:%S", &now);

    char timeVPS[20] = {0};
    strftime(timeVPS, 20, "%d.%m.%Y %H:%M:%S", &now);
    int offset = difftime(curr_time, recStart);
    int h = offset/60/60;
    int m = (offset - h*60*60) / 60;
    int s = offset - h*60*60 - m*60;

    dsyslog("markad: %02d:%02d:%02d state: %d, event: %d, new state: %d -> %s", h, m, s, state, event, newState, action);
    if (asprintf(&message, "%s VPS status: time offset: %02d:%02d:%02d, eventID: %d, old state %d, new state: %d -> %s", timeNow, h, m, s, event, state, newState, action) == -1) {
        esyslog("markad: cEpgEventLog::Log(): asprintf failed");
        return;
    }
    ALLOC(strlen(message)+1, "message");

    fprintf(eventLogFile,"%s\n", message);
    FREE(strlen(message)+1, "message");
    free(message);
    fflush(eventLogFile);
}


void cEpgEventLog::Log(const char *message) {
    if (!eventLogFile) return;
    if (!message) return;

    char *messageLog = NULL;
    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeNow[20] = {0};
    strftime(timeNow, 20, "%d.%m.%Y %H:%M:%S", &now);
    if (asprintf(&messageLog, "%s %s", timeNow, message) == -1) {
        esyslog("markad: cEpgEventLog::Log(): asprintf failed");
        return;
    }
    ALLOC(strlen(message)+1, "messageLog");

    fprintf(eventLogFile,"%s\n", messageLog);
    FREE(strlen(message)+1, "messageLog");
    free(messageLog);
    fflush(eventLogFile);
}


bool cEpgHandlerMarkad::HandleEitEvent(cSchedule *Schedule, const SI::EIT::Event *EitEvent, uchar TableID, uchar Version) {
    if (!EitEvent) return false;
    if (EitEvent->getEventId() <= 0) return false;
    if (EitEvent->getRunningStatus() <= 0) return false;
    if (!Schedule) return false;
    if (!StatusMarkAd) return false;

    const cEvent *event = Schedule->GetPresentEvent();
    if (event) {
#ifdef DEBUG_VPS_EIT
        LOCK_CHANNELS_READ;
        const cChannel *channel = Channels->GetByChannelID(Schedule->ChannelID(), true);
        if ((channel) && (strcmp( channel->Name(), DEBUG_VPS_EIT) == 0)) {
            time_t startTimeEIT = EitEvent->getStartTime();
            time_t stopTimeEIT = startTimeEIT + EitEvent->getDuration();
            struct tm start = *localtime(&startTimeEIT);
            char timerStart[20] = {0};
            strftime(timerStart, 20, "%d.%m.%Y %H:%M:%S", &start);
            struct tm stop = *localtime(&stopTimeEIT);
            char timerStop[20] = {0};
            strftime(timerStop, 20, "%d.%m.%Y %H:%M:%S", &stop);
            dsyslog("markad: cEpgHandlerMarkad::HandleEitEvent(): EventID %6d, EIT EventID %5d, TableID %u, Version %u, start: %s, stop: %s, RunningStatus %d, channel: %s %s, title: %s", event->EventID(), EitEvent->getEventId(), TableID, Version, timerStart, timerStop, EitEvent->getRunningStatus(), *Schedule->ChannelID().ToString(), channel->Name(), event->Title());
        }
#endif
        StatusMarkAd->FindRecording(event, EitEvent, Schedule);
    }
    return false; // let vdr call other handler too
}


bool cEpgHandlerMarkad::HandleEvent(cEvent *Event) {
    if (!Event) return false;
    if (Event->RunningStatus() <= 0) return false;

#ifdef DEBUG_VPS_VDR
    LOCK_CHANNELS_READ;
    const cChannel *channel = Channels->GetByChannelID(Event->ChannelID(), true);
    if ((channel) && (strcmp( channel->Name(), DEBUG_VPS_VDR) == 0)) {
           dsyslog("markad: cEpgHandlerMarkad::HandleEvent():    EventID %5d,          RunningStatus %d, channel: %s %s, title: %s", Event->EventID(), Event->RunningStatus(), *Event->ChannelID().ToString(), channel->Name(), Event->Title());
    }
#endif

    StatusMarkAd->FindRecording(Event, NULL, NULL);

    return false;  // let vdr call other handler too
}


int cStatusMarkAd::Get_EIT_EventID(const sRecordings *recording, const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *schedule, const bool nextEvent) {
#if APIVERSNUM>=20301  // feature not supported with old VDRs
    LOCK_SCHEDULES_READ
    tEventID eitEventID  = eitEvent->getEventId();
    if (nextEvent) event = schedule->GetFollowingEvent();

    //  this is no real VPS control, we can only handle VPS events in the timer start/stop range, keep pre/post timer big enought, try to find in each EIT event
    time_t startTimeEIT   = eitEvent->getStartTime();
    time_t stopTimeEIT    = startTimeEIT + eitEvent->getDuration();

    if ((!nextEvent && (startTimeEIT >= recording->timerStartTime) && (stopTimeEIT <= recording->timerStopTime)) || // VPS range is in timer range
        (nextEvent  && (startTimeEIT >  recording->timerStartTime) && (stopTimeEIT  > recording->timerStopTime))) { // VPS range is after timer range

        struct tm startTimer = *localtime(&recording->timerStartTime);
        char timerStartTimer[20] = {0};
        strftime(timerStartTimer, 20, "%d.%m.%Y %H:%M:%S", &startTimer);
        struct tm stopTimer = *localtime(&recording->timerStopTime);
        char timerStopTimer[20] = {0};
        strftime(timerStopTimer, 20, "%d.%m.%Y %H:%M:%S", &stopTimer);

        struct tm startEIT = *localtime(&startTimeEIT);
        char timerStartEIT[20] = {0};
        strftime(timerStartEIT, 20, "%d.%m.%Y %H:%M:%S", &startEIT);
        struct tm stopEIT = *localtime(&stopTimeEIT);
        char timerStopEIT[20] = {0};
        strftime(timerStopEIT, 20, "%d.%m.%Y %H:%M:%S", &stopEIT);

        time_t startTimeEvent = event->StartTime();
        struct tm startEvent = *localtime(&startTimeEvent);
        char timerStartEvent[20] = {0};
        strftime(timerStartEvent, 20, "%d.%m.%Y %H:%M:%S", &startEvent);
        time_t stopTimeEvent  = event->EndTime();
        struct tm stopEvent = *localtime(&stopTimeEvent);
        char timerStopEvent[20] = {0};
        strftime(timerStopEvent, 20, "%d.%m.%Y %H:%M:%S", &stopEvent);


        char *log = NULL;
        if (recording->epgEventLog) {
            if (nextEvent) recording->epgEventLog->Log("NNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNNN");
            else           recording->epgEventLog->Log("CCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCCC");
        }
        if ((recording->epgEventLog) && (asprintf(&log, "timer             -> start: %s, stop: %s, eventID:    %7d, channelID: %s, title: %s", timerStartTimer, timerStopTimer, recording->eventID, *recording->channelID.ToString(), recording->Name) != -1)) recording->epgEventLog->Log(log);
        if (log) free(log);
        if ((recording->epgEventLog) && (asprintf(&log, "EIT event %s -> start: %s, stop: %s, eitEventID: %7u, channelID: %s", (nextEvent) ? "next    " : "current", timerStartEIT, timerStopEIT, eitEventID, *schedule->ChannelID().ToString()) != -1)) recording->epgEventLog->Log(log);
        if (log) free(log);
        if ((recording->epgEventLog) && (asprintf(&log, "VDR event %s -> start: %s, stop: %s, eventID     %7u,                                 title: %s", (nextEvent) ? "next    " : "current", timerStartEvent, timerStopEvent, event->EventID(), event->Title()) != -1)) recording->epgEventLog->Log(log);
        if (log) free(log);

        if ((recording->epgEventLog) && (asprintf(&log, "found eitEventID %u for %s event", eitEventID, (nextEvent) ? "next    " : "current") != -1)) recording->epgEventLog->Log(log);
        if (log) free(log);
        if (recording->epgEventLog) recording->epgEventLog->Log("===============================================================================================================================");
        return eitEventID;
    }
#endif
    return 0;
}

void cStatusMarkAd::FindRecording(const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *Schedule) {
    if (!setup->useVPS) return; // no VPS confugured
    if (max_recs == -1) return; // no recording running, nothing to do

#if APIVERSNUM>=20301  // feature not supported with old VDRs
    tEventID eventID  = event->EventID();
    int runningStatus = event->RunningStatus();

    tEventID eitEventID  = 0;
    if (eitEvent) {
        eitEventID       = eitEvent->getEventId();
        runningStatus = eitEvent->getRunningStatus();
    }


    for (int i = 0; i <= max_recs; i++) {
        if (recs[i].eventID       ==  0) continue;   // this slot is not activ
        if (recs[i].runningStatus == -1) continue;   // we have a final invalid state, no more state updates

        // for all types of events
        if ((eventID == recs[i].eventID) && (runningStatus == 4) && (recs[i].eventNextID == 0)) { // recording is now running but and we have no next event, try now
            dsyslog("markad: StatusMarkAd::FindRecording(): index %d, eventID %4d, next event missing", i, recs[i].eventID);
            LOCK_SCHEDULES_READ
            const cSchedule *schedule = NULL;
            if (eitEvent) schedule = Schedule;
            else schedule = event->Schedule();
            if (schedule) {
                const cEvent *eventNext = schedule->GetFollowingEvent();
                if (eventNext) {
                    tEventID eventNextID = eventNext->EventID();
                    if (eventNextID != eventID) {
                        char *log = NULL;
                        if ((recs[i].epgEventLog) && (asprintf(&log, "------------------------------------> new VPS %s Event: EventID: %7d, eitEventID: %7d, runningStatus: %u -> got next eventID %d", (eitEvent) ? "EIT" : "VPS", eventID, eitEventID, runningStatus, eventNextID) != -1)) recs[i].epgEventLog->Log(log);
                        if (log) free(log);
                        recs[i].eventNextID = eventNextID;  // before start of recording we are the next recording self
                    }
                }
            }
        }

        // process EIT event
        if (eitEvent && !recs[i].ignoreEIT) {
            if (recs[i].channelID == Schedule->ChannelID()) {  // we do not know the EIT Event ID, with epg2vdr it is different from timer eventID
                if (recs[i].eitEventID     == 0) recs[i].eitEventID     = Get_EIT_EventID(&recs[i], event, eitEvent, Schedule, false);
                if (recs[i].eitEventNextID == 0) recs[i].eitEventNextID = Get_EIT_EventID(&recs[i], event, eitEvent, Schedule, true);
            }
            if ((recs[i].eitEventID == eitEventID)) {
                if (recs[i].runningStatus != runningStatus) {
                    pthread_mutex_lock(&mutex);
                    SetVPSStatus(i, runningStatus, (eitEvent)); // store recording running status from EIT Event, with epg2vdr it is differnt from VDR event
                    pthread_mutex_unlock(&mutex);
                }
            }
            if ((recs[i].runningStatus == 4) && (runningStatus == 4) && (eitEventID == recs[i].eitEventNextID)) {  // next event got EIT start, for private channels this is the only stop event
                char *log = NULL;
                if ((recs[i].epgEventLog) && (asprintf(&log, "------------------------------------> new VPS EIT Event: eventID: %7d, eitEventID: %7d, runningStatus: %u -> next event started", eventID, eitEventID, runningStatus) != -1)) recs[i].epgEventLog->Log(log);
                if (log) free(log);
                pthread_mutex_lock(&mutex);
                SetVPSStatus(i, 1, (eitEvent)); // store recording stop
                pthread_mutex_unlock(&mutex);
            }
        }

        // process VDR event
        if (!eitEvent && recs[i].ignoreEIT) {
            if (eventID == recs[i].eventID) {
                if (recs[i].runningStatus != runningStatus) {
                    pthread_mutex_lock(&mutex);
                    SetVPSStatus(i, runningStatus, (eitEvent)); // store recording running status
                    pthread_mutex_unlock(&mutex);
                }
            }
            if ((recs[i].runningStatus == 4) && (runningStatus == 4) && (eventID == recs[i].eventNextID)) {  // next event got VPS start, for private channels this is the only stop event
                char *log = NULL;
                if ((recs[i].epgEventLog) && (asprintf(&log, "------------------------------------> new VPS VDR Event: eventID: %7d, eitEventID: %7d, runningStatus: %u -> next event started", eventID, eitEventID, runningStatus) != -1)) recs[i].epgEventLog->Log(log);
                if (log) free(log);
                pthread_mutex_lock(&mutex);
                SetVPSStatus(i, 1, (eitEvent)); // store recording stop
                pthread_mutex_unlock(&mutex);
            }
        }
    }
#endif
    return;
}


void cStatusMarkAd::SetVPSStatus(const int index, int runningStatus, const bool eventEIT) {
    // process changed running status
    char *logAll = NULL;
    if (recs[index].epgEventLog) {
        // VPS RunningStatus: 0=undefined, 1=not running, 2=starts in a few seconds, 3=pausing, 4=running
        char *statusText = NULL;
        switch (runningStatus) {
            case 1:
                if (asprintf(&statusText, "not running") == -1) return;
                break;
            case 2:
                if (asprintf(&statusText, "starts in a few seconds") == -1) return;
                break;
            case 3:
                if (asprintf(&statusText, "pausing") == -1) return;
                break;
            case 4:
                if (asprintf(&statusText, "running") == -1) return;
                break;
        }
        ALLOC(strlen(statusText)+1, "statusText");

        if (asprintf(&logAll, "------------------------------------> new VPS %s Event: EventID: %7d, eitEventID: %7d, runningStatus: %u -> %s", (eventEIT) ? "EIT" : "VDR", recs[index].eventID, recs[index].eitEventID, runningStatus, statusText) != -1) recs[index].epgEventLog->Log(logAll);
        if (logAll) free(logAll);
        FREE(strlen(statusText)+1, "statusText");
        free(statusText);
    }
    if ((recs[index].runningStatus == 0) && (runningStatus == 1)) { // VPS event not running
        if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "before recording start");
        recs[index].runningStatus = 1;
        return;
    }

    if ((recs[index].runningStatus == 0) && (runningStatus == 4)) {  // to late recording start
        if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, -1, "recording start after VPS timer start is invalid");
        recs[index].runningStatus = -1;
        return;
    }

    if ((recs[index].runningStatus <= 1) && (runningStatus == 2)) { // VPS event start expected in a few seconds
        if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "recording starts in a few seconds");
        recs[index].runningStatus = 2;
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 2)) {
        if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, recs[index].runningStatus, "ignore");
        return;
    }

    if ((recs[index].runningStatus == 2) && (runningStatus == 1)) {
        if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, recs[index].runningStatus, "ignore");
        return;
    }

    if (((recs[index].runningStatus == 1) || (recs[index].runningStatus == 2)) && (runningStatus == 4)) { // VPS start
        if (recs[index].vpsStartTime && recs[index].vpsStopTime) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "ignore");
            char *log = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&log, "VPS event 'running' at status %i after we had start and stop events, ignoring event", recs[index].runningStatus) != -1)) recs[index].epgEventLog->Log(log);
            if (log) free(log);
        }
        else {
            if (StoreVPSStatus("START", index)) {
                if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "recording start");
                recs[index].runningStatus = 4;
            }
            else {
                if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, recs[index].runningStatus, "ignore");
            }
        }
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 1)) {  // VPS stop
        if (StoreVPSStatus("STOP", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "recording end");
            recs[index].runningStatus = 1;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "invalid VPS sequence, abort VPS detection");
            recs[index].runningStatus = -1;
        }
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 3)) { // VPS pause start
        if (StoreVPSStatus("PAUSE_START", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "pause start");
            recs[index].runningStatus = 3;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "ignore");
        }
        return;
    }

    if ((recs[index].runningStatus == 3) && (runningStatus == 4)) { // VPS pause stop
        if (StoreVPSStatus("PAUSE_STOP", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, runningStatus, "pause stop");
            recs[index].runningStatus = 4;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, recs[index].runningStatus, "ignore");
        }
        return;
    }

    logAll = NULL;
    if ((recs[index].epgEventLog) && (asprintf(&logAll, "ununexpected VPS event %d at status %d, ignoring this and future events", runningStatus, recs[index].runningStatus) != -1)) recs[index].epgEventLog->Log(logAll);
    if (logAll) free(logAll);
    if (recs[index].epgEventLog) recs[index].epgEventLog->Log(recs[index].recStart, recs[index].eventID, recs[index].runningStatus, -1, "invalid");
    recs[index].runningStatus = -1;
    return;
}


void cStatusMarkAd::SaveVPSTimer(const char *FileName, const bool timerVPS) {
    if (!FileName) return;

    char *fileVPS = NULL;
    if (!asprintf(&fileVPS, "%s/%s", FileName, "markad.vps")) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> asprintf failed", FileName);
        return;
    }
    ALLOC(strlen(fileVPS)+1, "fileVPS");
    FILE *pFile = fopen(fileVPS,"w");
    if (!pFile) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> open file %s failed", FileName, fileVPS);
        FREE(strlen(fileVPS)+1, "fileVPS");
        free(fileVPS);
        return;
    }
    if (timerVPS) fprintf(pFile, "VPSTIMER=YES\n");
    else fprintf(pFile, "VPSTIMER=NO\n");

    fclose(pFile);
    FREE(strlen(fileVPS)+1, "fileVPS");
    free(fileVPS);
    return;
}


void cStatusMarkAd::SaveVPSEvents(const int index) {
    if ((index < 0) || (index >= MAXDEVICES * MAXRECEIVERS)) {
        dsyslog("markad: cStatusMarkAd::SaveVPSEvents(): index %i out of range", index);
    }
    if (recs[index].runningStatus == -1 ) {
        esyslog("markad: VPS event sequence not valid for recording <%s>", recs[index].Name);
        return;
    }
    if (!recs[index].vpsStartTime)  {
        esyslog("markad: no VPS start event for recording <%s>", recs[index].Name);
        return;
    }
    if (!recs[index].vpsStopTime)  {
        esyslog("markad: no VPS stop event for recording <%s>", recs[index].Name);
        return;
    }

    int offset;
    char timeVPSchar[20] = {0};
    struct tm timeVPStm;
    char *fileVPS = NULL;

    if (!asprintf(&fileVPS, "%s/%s", recs[index].FileName, "markad.vps")) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> asprintf failed", recs[index].Name);
        return;
    }
    ALLOC(strlen(fileVPS)+1, "fileVPS");

    FILE *pFile = fopen(fileVPS,"a+");
    if (!pFile) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> open file %s failed", recs[index].Name, fileVPS);
        FREE(strlen(fileVPS)+1, "fileVPS");
        free(fileVPS);
        return;
    }

    if (recs[index].vpsStartTime) {
        timeVPStm = *localtime(&recs[index].vpsStartTime);
        offset = difftime(recs[index].vpsStartTime,recs[index].recStart);
        strftime(timeVPSchar, 20, "%d.%m.%Y-%H:%M:%S", &timeVPStm);
        fprintf(pFile, "%s: %s %i\n", "START", timeVPSchar, offset);
    }
    if (recs[index].vpsPauseStartTime) {
        timeVPStm = *localtime(&recs[index].vpsPauseStartTime);
        offset = difftime(recs[index].vpsPauseStartTime,recs[index].recStart);
        strftime(timeVPSchar, 20, "%d.%m.%Y-%H:%M:%S", &timeVPStm);
        fprintf(pFile, "%s: %s %i\n", "PAUSE_START", timeVPSchar, offset);
    }
    if (recs[index].vpsPauseStopTime) {
        timeVPStm = *localtime(&recs[index].vpsPauseStopTime);
        offset = difftime(recs[index].vpsPauseStopTime,recs[index].recStart);
        strftime(timeVPSchar, 20, "%d.%m.%Y-%H:%M:%S", &timeVPStm);
        fprintf(pFile, "%s: %s %i\n", "PAUSE_STOP", timeVPSchar, offset);
    }
    if (recs[index].vpsStopTime) {
        timeVPStm = *localtime(&recs[index].vpsStopTime);
        offset = difftime(recs[index].vpsStopTime,recs[index].recStart);
        strftime(timeVPSchar, 20, "%d.%m.%Y-%H:%M:%S", &timeVPStm);
        fprintf(pFile, "%s: %s %i\n", "STOP", timeVPSchar, offset);
    }

    fclose(pFile);
    FREE(strlen(fileVPS)+1, "fileVPS");
    free(fileVPS);
    return;
}


bool cStatusMarkAd::StoreVPSStatus(const char *status, const int index) {
    if (!status) return false;
    if ((index < 0) || (index >= MAXDEVICES * MAXRECEIVERS)) {
        dsyslog("markad: cStatusMarkAd::StoreVPSStatus(): index %i out of range", index);
        return false;
    }

    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeVPS[20] = {0};
    strftime(timeVPS, 20, "%d.%m.%Y %H:%M:%S", &now);
    dsyslog("markad: StatusMarkAd::StoreVPSStatus(): recording <%s> got VPS %s event at %s", recs[index].Name, status, timeVPS);
    if (strcmp(status,"START") == 0) {
        recs[index].vpsStartTime=curr_time;
        return true;
    }
    if (strcmp(status,"PAUSE_START") == 0) {
        if (recs[index].vpsPauseStartTime == 0) {
            recs[index].vpsPauseStartTime=curr_time;
            return true;
        }
        else {
            return false;
        }
    }
    if (strcmp(status,"PAUSE_STOP") == 0) {
        if (curr_time > recs[index].vpsPauseStartTime + 60) { // PAUSE STOP must be at least 1 min after PAUSE START
            if (recs[index].vpsPauseStopTime == 0) {
                recs[index].vpsPauseStopTime=curr_time;
                return true;
            }
            else {
                char *log = NULL;
                if ((recs[index].epgEventLog) && (asprintf(&log, "VPS pause stop already received, pause stop now set to last event") != -1)) recs[index].epgEventLog->Log(log);
                if (log) free(log);
                recs[index].vpsPauseStopTime=curr_time;
                return true;
            }
        }
        else {
            char *log = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&log, "VPS pause stop to fast after pause start, ignoring") != -1)) recs[index].epgEventLog->Log(log);
            if (log) free(log);
            return false;
        }
    }
    if (strcmp(status,"STOP") == 0) {
        if ( curr_time >  recs[index].vpsStartTime + 60) {  // a valid STOP must be at least 1 min after START
            recs[index].vpsStopTime=curr_time;
            return true;
        }
        else {
            char *log = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&log, "VPS stop to fast after start, invalid VPS sequence, abort VPS detection") != -1)) recs[index].epgEventLog->Log(log);
            if (log) free(log);
            return false;
        }
    }
    dsyslog("markad: cStatusMarkAd::StoreVPSStatus(): unknown state %s", status);
    return false;
}


cStatusMarkAd::cStatusMarkAd(const char *BinDir, const char *LogoDir, struct setup *Setup) {
    setup = Setup;
    bindir = BinDir;
    logodir = LogoDir;
    actpos = 0;
    memset(&recs, 0, sizeof(recs));

    dsyslog("markad: cStatusMarkAd::cStatusMarkAd(): create epg event handler");
    epgHandlerMarkad = new cEpgHandlerMarkad(this);     // VDR will free at stop
}


cStatusMarkAd::~cStatusMarkAd() {
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        Remove(i, true);
    }
}


bool cStatusMarkAd::Replaying() {
    for (int i = 0; i < cDevice::NumDevices(); i++) {
        cDevice *dev = cDevice::GetDevice(i);
        if (dev) {
            if (dev->Replaying()) {
#ifdef DEBUG_PAUSE_CONTINUE
                dsyslog("markad: cStatusMarkAd::Replaying(): device %d is playing",i);
#endif
                return true;
            }
        }
    }
    return false;
}


#ifdef DEBUG_PAUSE_CONTINUE
void cStatusMarkAd::Replaying(const cControl *UNUSED(Control), const char *UNUSED(Name), const char *FileName, bool On) {
    dsyslog("markad: cStatusMarkAd::Replaying called, %s recording %s", On ? "start" : "stop", FileName ? FileName : "<NULL>");
#else
void cStatusMarkAd::Replaying(const cControl *UNUSED(Control), const char *UNUSED(Name), const char *UNUSED(FileName), bool On) {
#endif
    if (setup->ProcessDuring != PROCESS_AFTER) return;
    if (setup->whileReplaying) return;
    if (On) {
        Pause(NULL);
    }
    else {
        if (runningRecordings == 0) Continue(NULL);
    }
}


bool cStatusMarkAd::Start(const char *FileName, const char *Name, const tEventID eventID, const tEventID eventNextID, const tChannelID channelID, const time_t timerStartTime, const time_t timerStopTime, const bool timerVPS, const bool direct) {
    if ((direct) && (Get(FileName) != -1)) return false;

    char *autoLogoOption = NULL;
    if (setup->autoLogoConf >= 0) {
        if(!asprintf(&autoLogoOption," --autologo=%i ", setup->autoLogoConf)) {
            esyslog("markad: asprintf autoLogoOption ouf of memory");
            return false;
        }
        ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
    }
    else {
        if (setup->autoLogoMenue >= 0) {
            if(! asprintf(&autoLogoOption, " --autologo=%i ", setup->autoLogoMenue)) {
                esyslog("markad: asprintf ouf of memory");
                return false;
            }
            ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
        }
    }
    // prepare --svdrpport option
    char *svdrPortOption = NULL;
    if(!asprintf(&svdrPortOption, "-O --svdrpport=%d ", setup->svdrPort)) {
        esyslog("markad: asprintf svdrPortOption ouf of memory");
        return false;
    }
    ALLOC(strlen(svdrPortOption)+1, "svdrPortOption");

    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
                                   bindir,
                                   setup->Verbose ? " -v " : "",
                                   setup->GenIndex ? " -G " : "",
                                   setup->OSDMessage ? svdrPortOption : "",
                                   setup->NoMargins ? " -i 4 " : "",
                                   setup->SecondPass ? "" : " --pass1only ",
                                   setup->Log2Rec ? " -R " : "",
                                   setup->LogLevel ? setup->LogLevel : "",
                                   setup->aStopOffs ? setup->aStopOffs : "",
                                   setup->useVPS ? " --vps " : "",
                                   setup->MarkadCut ? " --cut " : "",
                                   setup->ac3ReEncode ? " --ac3reencode " : "",
                                   autoLogoOption ? autoLogoOption : "",
                                   setup->fulldecode ? " --fulldecode " : "",
                                   logodir,
                                   direct ? "-O after" : "--online=2 before",
                                   FileName);
    FREE(strlen(autoLogoOption)+1, "autoLogoOption");
    free(autoLogoOption);
    FREE(strlen(svdrPortOption)+1, "svdrPortOption");
    free(svdrPortOption);

    usleep(1000000); // wait 1 second
    if (SystemExec(cmd) != -1) {
        dsyslog("markad: cStatusMarkAd::Start(): executing %s", *cmd);
        usleep(200000);
        int pos = Add(FileName, Name, eventID, eventNextID, channelID, timerStartTime, timerStopTime, timerVPS);
        bool gotPID = getPid(pos); // will set recs[pos].Pid
        dsyslog("markad: cStatusMarkAd::Start(): index %d, pid %d, filename %s: running markad stored in list", pos, recs[pos].Pid, FileName ? FileName : "<NULL>");
        if (gotPID && getStatus(pos)) {
            if (setup->ProcessDuring == PROCESS_AFTER) {
                if (!direct) {
                    if (!setup->whileRecording) Pause(NULL);
                    else Pause(FileName);
                }
                else {
                    if (!setup->whileRecording && (runningRecordings > 0)) Pause(FileName);
                    if (!setup->whileReplaying && Replaying()) Pause(FileName);
                }
            }
        }
        else isyslog("markad: cannot find running process");
        return true;
    }
    return false;
}


void cStatusMarkAd::TimerChange(const cTimer *Timer, eTimerChange Change) {
    if (!Timer) return;
    if (Change != tcDel) return;
    if (setup->ProcessDuring == PROCESS_NEVER) return;
    if (time(NULL) >= Timer->StopTime()) return; // don't react on normal VDR timer deletion after recording
    Remove(Timer->File(), true);
}


void cStatusMarkAd::GetEventID(const cDevice *Device, const char *Name, tEventID *eventID, tEventID *eventNextID,  tChannelID *channelID, time_t *timerStartTime, time_t *timerStopTime, bool *timerVPS) {
    if (!Name)           return;
    if (!Device)         return;
    if (!eventID)        return;
    if (!eventNextID)    return;
    if (!timerStartTime) return;
    if (!timerStopTime)  return;

#if APIVERSNUM>=20301  // feature not supported with old VDRs
    *timerStartTime = 0;
    *timerStopTime  = 0;
    *eventID        = 0;
    *eventNextID    = 0;
    int timeDiff    = INT_MAX;

#if APIVERSNUM>=20301
    const cTimer *timer = NULL;
    cStateKey StateKey;
#ifdef DEBUG_LOCKS
        dsyslog("markad: GetEventID(): WANT   timers READ");
#endif
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
#ifdef DEBUG_LOCKS
        dsyslog("markad: GetEventID(): LOCKED timers READ");
#endif
        for (const cTimer *Timer = Timers->First(); Timer; Timer = Timers->Next(Timer))
#else
    cTimer *timer = NULL;
    for (cTimer *Timer = Timers.First(); Timer; Timer = Timers.Next(Timer))
#endif
        {
            if (Timer->Recording() && const_cast<cDevice *>(Device)->IsTunedToTransponder(Timer->Channel())) {
                if (Timer->File() && (strcmp(Name, Timer->File()) == 0)) {
                    if (abs(Timer->StartTime() - time(NULL)) < timeDiff) {  // maybe we have two timer on same channel with same name, take the nearest start time
                        timer=Timer;
                        timeDiff = abs(Timer->StartTime() - time(NULL));
                    }
                }
            }
        }
#if APIVERSNUM>=20301
    }
#endif
    if (!timer) {
        esyslog("markad: cannot find timer for <%s>", Name);
#if APIVERSNUM>=20301
#ifdef DEBUG_LOCKS
        dsyslog("markad: GetEventID(): UNLOCK timers READ");
#endif
        StateKey.Remove();
#endif
        return;
    }
    *timerStartTime = timer->StartTime();
    *timerStopTime = timer->StopTime();

    if (!timer->Event()) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer for %s has no event", Name);
    }
    else { // we found the event
        const cEvent *event    = timer->Event();
        if (event) {
            *eventID                  = event->EventID();
            *channelID                = event->ChannelID();
            LOCK_SCHEDULES_READ
            const cSchedule *schedule = event->Schedule();
            if (schedule) {
                const cEvent *eventNext = schedule->GetFollowingEvent();
                if (eventNext) {
                    *eventNextID = eventNext->EventID();
                    if (*eventNextID == *eventID) *eventNextID = 0;  // before start of recording we are the next recording self
                }
            }
        }
    }
#if APIVERSNUM>=20301
#ifdef DEBUG_LOCKS
        dsyslog("markad: GetEventID(): UNLOCK timers READ");
#endif
    StateKey.Remove();
#endif
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording <%s>, timer <%s>, eventID %u, eventNextID %u, timer start %ld stop %ld", Name, timer->File(), *eventID, *eventNextID, *timerStartTime, *timerStopTime);
    if (timer->HasFlags(tfVps)) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer <%s> uses VPS", timer->File());
        *timerVPS = true;
    }
#endif
    return;
}


void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) {
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir)   return; // we cannot operate without bindir
    if (!logodir)  return; // we dont want to operate without logodir

    if (On) {
        runningRecordings++;
        dsyslog("markad: cStatusMarkAd::Recording(): recording <%s> [%s] started, recording count now %d", Name, FileName, runningRecordings);
        // check if markad is running for the same recording, this can happen if we have a short recording interuption
        int runningPos = Get(FileName, NULL);
        if (runningPos >= 0) {
        isyslog("markad: is running on the same recording %s", FileName);
            Remove(runningPos, true);
        }

        tEventID eventID        = 0;
        tEventID eventNextID    = 0;
        time_t   timerStartTime = 0;
        time_t   timerStopTime  = 0;
        bool     timerVPS       = false;
        tChannelID channelID;
        GetEventID(Device, Name, &eventID, &eventNextID, &channelID, &timerStartTime, &timerStopTime, &timerVPS);
        SaveVPSTimer(FileName, timerVPS);

        if ((setup->ProcessDuring == PROCESS_NEVER) && setup->useVPS) {  // markad start disabled per config menue, add recording for VPS detection
            int pos = Add(FileName, Name, eventID, eventNextID, channelID, timerStartTime, timerStopTime, timerVPS);
            if (pos >= 0) dsyslog("markad: cStatusMarkAd::Recording(): added recording <%s> event ID %u, eventNextID %u at index %i only for VPS detection", Name, eventID, eventNextID, pos);
            return;
        }
        if (setup->ProcessDuring == PROCESS_NEVER) {
            isyslog("markad: deactivated by user");
            return; // markad deactivated
        }

        bool autoLogo = false;
        if (setup->autoLogoConf >= 0) autoLogo = (setup->autoLogoConf > 0);
        else autoLogo = (setup->autoLogoMenue > 0);

        if (!autoLogo && setup->LogoOnly && !LogoExists(Device,FileName)) {   // we can find the logo in the recording
            isyslog("markad: no logo found for %s", Name);
            return;
        }

        // Start markad with recording
        if (!Start(FileName, Name, eventID, eventNextID, channelID, timerStartTime, timerStopTime, timerVPS, false)) {
            esyslog("markad: failed starting on <%s>", FileName);
        }
    }
    else {
        runningRecordings--;
        if (runningRecordings < 0) runningRecordings = 0;
        dsyslog("markad: cStatusMarkAd::Recording(): recording stopped, recording count now %d", runningRecordings);
#ifdef DEBUG_PAUSE_CONTINUE
        dsyslog("markad: cStatusMarkAd::Recording(): setup->ProcessDuring %d, setup->whileRecording %d, setup->whileReplaying %d", setup->ProcessDuring, setup->whileRecording, setup->whileReplaying);
#endif
        int pos = Get(FileName, Name);
        if (pos >= 0) {
            dsyslog("markad: cStatusMarkAd::Recording(): index %d, pid %d, filename %s: recording stopped", pos, recs[pos].Pid, FileName);
            if (setup->useVPS) SaveVPSEvents(pos);  // store to get error messages for incomplete sequence
            if ((setup->ProcessDuring == PROCESS_DURING) || (setup->ProcessDuring == PROCESS_NEVER)) { // PROCESS_NEVER: recording maybe in list from vps detection
                dsyslog("markad: cStatusMarkAd::Recording(): remove recording <%s> [%s] from list", Name, FileName);
                Remove(pos, false);
            }

            if (setup->ProcessDuring == PROCESS_AFTER) {
                if (!setup->whileRecording) {
#ifdef DEBUG_PAUSE_CONTINUE
                    dsyslog("markad: cStatusMarkAd::Recording(): PROCESS_AFTER");
#endif
                    if (!setup->whileReplaying) {
#ifdef DEBUG_PAUSE_CONTINUE
                        dsyslog("markad: cStatusMarkAd::Recording(): replaying status %d", Replaying());
#endif
                        if ((runningRecordings == 0) && !Replaying()) Continue(NULL);
                    }
                    else {
                        if (runningRecordings == 0) Continue(NULL);
                        else dsyslog("markad: cStatusMarkAd::Recording(): resume not possible, still %d running recording(s)", runningRecordings);
                    }
                }
                else {
                    Continue(FileName);
               }
           }
        }
        else dsyslog("markad: cStatusMarkAd::Recording(): unknown recording %s stopped", FileName);
    }
}


bool cStatusMarkAd::LogoExists(const cDevice *Device, const char *FileName) {
    if (!FileName) return false;
    if (!Device) return false;
    char *cname = NULL;
#if APIVERSNUM>=20301
    const cTimer *timer = NULL;
    cStateKey StateKey;
#ifdef DEBUG_LOCKS
        dsyslog("markad: LogoExists(): WANT timers READ");
#endif
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
#ifdef DEBUG_LOCKS
        dsyslog("markad: LogoExists(): LOCKED timers READ");
#endif
        for (const cTimer *Timer = Timers->First(); Timer; Timer = Timers->Next(Timer))
#else
    cTimer *timer = NULL;
    for (cTimer *Timer = Timers.First(); Timer; Timer = Timers.Next(Timer))
#endif
        {
            if (Timer->Recording() && const_cast<cDevice *>(Device)->IsTunedToTransponder(Timer->Channel()))
                if (difftime(time(NULL), Timer->StartTime()) < 60) {
                    timer = Timer;
                    break;
                }
                else esyslog("markad: recording start is later than timer start, ignoring");
        }

        if (!timer) {
            esyslog("markad: cannot find timer for '%s'", FileName);
        }
        else {
            const cChannel *chan = timer->Channel();
            if (chan) {
                cname = strdup(chan->Name());
                ALLOC(strlen(cname)+1, "cname");
            }
        }

#if APIVERSNUM>=20301
#ifdef DEBUG_LOCKS
        dsyslog("markad: LogoExists(): UNLOCK timers READ");
#endif
        StateKey.Remove();
    }
#endif

    if (!timer) return false;
    if (!cname) return false;

    for (int i = 0; i < static_cast<int>(strlen(cname)); i++) {
        if (cname[i] == ' ') cname[i] = '_';
        if (cname[i] == '.') cname[i] = '_';
        if (cname[i] == '/') cname[i] = '_';
    }

    char *fname = NULL;
    if (asprintf(&fname, "%s/%s-A16_9-P0.pgm", logodir,cname) == -1) {
        FREE(strlen(cname)+1, "cname");
        free(cname);
        return false;
    }
    ALLOC(strlen(fname)+1, "fname");

    struct stat statbuf;
    if (stat(fname,&statbuf) == -1) {
        FREE(strlen(fname)+1, "fname");
        free(fname);
        fname = NULL;
        if (asprintf(&fname, "%s/%s-A4_3-P0.pgm", logodir,cname) == -1) {
            FREE(strlen(cname)+1, "cname");
            free(cname);
            return false;
        }
        ALLOC(strlen(fname)+1, "fname");

        if (stat(fname,&statbuf) == -1) {
            FREE(strlen(cname)+1, "cname");
            free(cname);

            FREE(strlen(fname)+1, "fname");
            free(fname);
            return false;
        }
    }
    FREE(strlen(cname)+1, "cname");
    free(cname);

    FREE(strlen(fname)+1, "fname");
    free(fname);
    return true;
}


bool cStatusMarkAd::getStatus(int Position) {
    if (Position < 0) return false;
    if (!recs[Position].Pid) return false;
    int ret = 0;
    char procname[256] = "";
    snprintf(procname, sizeof(procname), "/proc/%i/stat", recs[Position].Pid);
    FILE *fstat = fopen(procname, "r");
    if (fstat) {
        // found a running markad
        ret = fscanf(fstat, "%*10d %*255s %c", &recs[Position].Status);
        fclose(fstat);
    }
    else {
        if (errno == ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove filename from list
            Remove(Position);
        }
    }
#ifdef DEBUG_PAUSE_CONTINUE
    if (ret != 1) dsyslog("markad: cStatusMarkAd::getStatus(): markad terminated for index %i, recording %s", Position, recs[Position].FileName ? recs[Position].FileName : "<NULL>");
#endif
    return (ret == 1);
}


bool cStatusMarkAd::getPid(int Position) {
    if (Position < 0) return false;
    if (!recs[Position].FileName) return false;
    if (recs[Position].Pid) return true;
    int ret = 0;
    char *buf;
    if (asprintf(&buf, "%s/markad.pid", recs[Position].FileName) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    usleep(500*1000);   // wait 500ms to give markad time to create pid file
    FILE *fpid = fopen(buf,"r");
    if (fpid) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        int pid;
        ret = fscanf(fpid, "%10i\n", &pid);
        if (ret == 1) recs[Position].Pid = pid;
        fclose(fpid);
    }
    else {
        esyslog("markad: failed to open pid file %s with errno %i", buf, errno);
        if (errno == ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove entry from list
            Remove(Position);
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    return (ret == 1);
}


bool cStatusMarkAd::GetNextActive(struct sRecordings **RecEntry) {
    if (!RecEntry) return false;
    *RecEntry = NULL;

    if (actpos >= (MAXDEVICES*MAXRECEIVERS)) return false;

    do {
        if ((recs[actpos].FileName) && (recs[actpos].Pid)) {
            if (getStatus(actpos)) {
                /* check if recording directory still exists */
                if (access(recs[actpos].FileName, R_OK) == -1) {
                    Remove(actpos,true);
                } else {
                    *RecEntry = &recs[actpos++];
                    return true;
                }
            }
        }
        actpos++;
    }
    while (actpos < (MAXDEVICES*MAXRECEIVERS));

    return false;
}


void cStatusMarkAd::Check() {
    struct sRecordings *tmpRecs = NULL;
    ResetActPos();
    while (GetNextActive(&tmpRecs)) ;
}


bool cStatusMarkAd::MarkAdRunning() {
    struct sRecordings *tmpRecs = NULL;
    ResetActPos();
    bool running = false;
    while (GetNextActive(&tmpRecs)) {
        if (tmpRecs->Name) dsyslog("markad: markad is running for recording %s, defere shutdown", tmpRecs->Name);
        else dsyslog("markad: markad is running for unknown recording, defere shutdown");
        running = true;
    }
    return (running);
}


int cStatusMarkAd::Get(const char *FileName, const char *Name) {
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        if (Name && recs[i].Name && !strcmp(recs[i].Name, Name)) return i;
        if (FileName && recs[i].FileName && !strcmp(recs[i].FileName, FileName)) return i;
    }
    return -1;
}


void cStatusMarkAd::Remove(const char *Name, bool Kill) {
    if (!Name) return;
    int pos = Get(NULL, Name);
    if (pos == -1) return;
    Remove(pos, Kill);
}


void cStatusMarkAd::Remove(int pos, bool Kill) {
    if (pos < 0) return;
//    dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d, filename %s: remove from list", pos, recs[pos].Pid, (recs[pos].FileName) ? recs[pos].FileName : "<NULL>");
    if (recs[pos].FileName) {
        if (recs[pos].runningStatus == 4) isyslog("markad: got no VPS stop event for recording %s", recs[pos].FileName);
        FREE(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
        free(recs[pos].FileName);
        recs[pos].FileName = NULL;
    }

    if (recs[pos].Name) {
        FREE(strlen(recs[pos].Name)+1, "recs[pos].Name");
        free(recs[pos].Name);
        recs[pos].Name = NULL;
    }
    if ((Kill) && (recs[pos].Pid)) {
        if (getStatus(pos)) {
            if ((recs[pos].Status == 'R') || (recs[pos].Status == 'S')) {
                dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d: terminating markad process", pos, recs[pos].Pid);
                kill(recs[pos].Pid, SIGTERM);
            }
            else {
                dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d: killing markad process", pos, recs[pos].Pid);
                kill(recs[pos].Pid, SIGKILL);
            }
        }
    }
    recs[pos].Status            = 0;
    recs[pos].Pid               = 0;
    recs[pos].ChangedbyUser     = false;
    recs[pos].ignoreEIT         = false;
    recs[pos].eventID           = 0;
    recs[pos].eventNextID       = 0;
    recs[pos].eitEventID        = 0;
    recs[pos].eitEventNextID        = 0;
    recs[pos].timerStartTime    = 0;
    recs[pos].timerStopTime     = 0;
    recs[pos].runningStatus     = 0;
    recs[pos].recStart          = {0};
    recs[pos].vpsStartTime      = 0;
    recs[pos].vpsStopTime       = 0;
    recs[pos].vpsPauseStartTime = 0;
    recs[pos].vpsPauseStopTime  = 0;
    recs[pos].timerVPS          = false;
    if (recs[pos].epgEventLog) {
        FREE(sizeof(*(recs[pos].epgEventLog)), "recs[pos].epgEventLog");
        delete recs[pos].epgEventLog;
        recs[pos].epgEventLog = NULL;
    }

    max_recs = -1;
    for (int i = 0; i < (MAXDEVICES*MAXRECEIVERS); i++) {
        if (recs[i].FileName) {
            max_recs = i;
        }
    }
}


char *cStatusMarkAd::GetStatus() {
    char *status = NULL;  // vdr will free this memory
    for (int pos = 0; pos < (MAXDEVICES*MAXRECEIVERS); pos++) {
        if (!recs[pos].FileName) continue;
        dsyslog("markad: cStatusMarkAd::GetStatus(): active recording with markad running: %s",recs[pos].FileName);
        char *line = NULL;
        char *tmp = NULL;
        if (asprintf(&line, "markad: running for %s\n", recs[pos].FileName) != -1) {
            if (asprintf(&tmp, "%s%s", (status) ? status : "", line) != -1) {
                free(status);
                free(line);
                status = tmp;
            }
        }
     }
     if (!status) {
         if (asprintf(&status, "markad: no active recording with running markad found\n") != -1) return status;
     }
     return status;
}


int cStatusMarkAd::Add(const char *FileName, const char *Name, const tEventID eventID, const tEventID eventNextID, const tChannelID channelID, const time_t timerStartTime, const time_t timerStopTime, bool timerVPS) {
    for (int pos = 0; pos < (MAXDEVICES*MAXRECEIVERS); pos++) {
        if (!recs[pos].FileName) {
            recs[pos].FileName = strdup(FileName);
            ALLOC(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
            if (Name) {
                recs[pos].Name = strdup(Name);
                ALLOC(strlen(recs[pos].Name)+1, "recs[pos].Name");
            }
            else {
                recs[pos].Name = NULL;
            }
            recs[pos].Status            = 0;
            recs[pos].Pid               = 0;
            recs[pos].ChangedbyUser     = false;
            recs[pos].eventID           = eventID;
            recs[pos].eventNextID       = eventNextID;
            recs[pos].eitEventID        = 0;
            recs[pos].eitEventNextID    = 0;
            recs[pos].timerStartTime    = timerStartTime;
            recs[pos].timerStopTime     = timerStopTime;
            recs[pos].runningStatus     = 0;
            recs[pos].recStart          = time(NULL);
            recs[pos].vpsStartTime      = 0;
            recs[pos].vpsStopTime       = 0;
            recs[pos].vpsPauseStartTime = 0;
            recs[pos].vpsPauseStopTime  = 0;
            recs[pos].timerVPS          = timerVPS;
            recs[pos].channelID         = channelID;

            if (eventID <= 0xFFFF) recs[pos].ignoreEIT = true;  // no epg2vdr plugin, we do not need EIT events
            else recs[pos].ignoreEIT = false;                   // epg2vdr plugin used, we need to use EIT events, VDR events will not work

            if (setup->useVPS && setup->logVPS) {
                recs[pos].epgEventLog = new cEpgEventLog(FileName);
                ALLOC(sizeof(*(recs[pos].epgEventLog)), "recs[pos].epgEventLog");
            }
            if (!epgHandlerMarkad && setup->useVPS) {
                epgHandlerMarkad = new cEpgHandlerMarkad(this);     // VDR will free at stop
                dsyslog("markad: cStatusMarkAd::Add():: create epg event handler");
            }
            if (pos > max_recs) max_recs = pos;

            struct tm start = *localtime(&timerStartTime);
            char timerStart[20] = {0};
            strftime(timerStart, 20, "%d.%m.%Y %H:%M:%S", &start);
            struct tm stop = *localtime(&timerStopTime);
            char timerStop[20] = {0};
            strftime(timerStop, 20, "%d.%m.%Y %H:%M:%S", &stop);
            char *log = NULL;
            if (recs[pos].epgEventLog) {
                if (asprintf(&log, "timer info: recording index %d, eventID %u, eventNextID %u", pos, eventID, eventNextID) != -1) recs[pos].epgEventLog->Log(log);
                if (log) free(log);
                if (asprintf(&log, "timer info: start %s, stop %s", timerStart, timerStop) != -1) recs[pos].epgEventLog->Log(log);
                if (log) free(log);
                if (!recs[pos].ignoreEIT) {
                    if (asprintf(&log, "timer info: eventID %d > 0xFFFF: channel under control of epg2vdr, use HandleEitEvent()", eventID) != -1) recs[pos].epgEventLog->Log(log);
                }
                else {
                    if (asprintf(&log, "timer info: eventID %d <= 0xFFFF: channel not under control of epg2vdr, use HandleEvent() and ignore HandleEitEvent()", eventID) != -1) recs[pos].epgEventLog->Log(log);
                }
                if (log) free(log);
            }
            return pos;
        }
    }
    return -1;
}


void cStatusMarkAd::Pause(const char *FileName) {
#ifdef DEBUG_PAUSE_CONTINUE
    dsyslog("markad: cStatusMarkAd::Pause(): called with filename %s", FileName ? FileName : "<NULL>");
#endif
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: cStatusMarkAd::Pause(): index %d, pid %d, filename %s: pause markad process", i, recs[i].Pid, FileName);
                kill(recs[i].Pid, SIGTSTP);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: cStatusMarkAd::Pause(): index %d, pid %d, filename %s: pause markad process", i, recs[i].Pid, recs[i].FileName ? recs[i].FileName : "<NULL>");
                kill(recs[i].Pid, SIGTSTP);
            }
        }
    }
}


void cStatusMarkAd::Continue(const char *FileName) {
#ifdef DEBUG_PAUSE_CONTINUE
    dsyslog("markad: cStatusMarkAd::Continue(): called with filename %s", FileName ? FileName : "<NULL>");
#endif
    for (int i = 0; i < (MAXDEVICES*MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser) ) {
                dsyslog("markad: cStatusMarkAd::Continue(): index %d, pid %i, filename %s: resume markad process", i, recs[i].Pid, recs[i].FileName);
                kill(recs[i].Pid, SIGCONT);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: cStatusMarkAd::Continue(): index %d, pid %d, filename %s: resume markad process", i, recs[i].Pid, recs[i].FileName ? recs[i].FileName : "<NULL>");
                kill(recs[i].Pid, SIGCONT);
            }
        }
    }
}
