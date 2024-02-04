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


void cEpgEventLog::LogState(const int severity, const sRecording *recording, const int newState, const char* action) {
    if (!eventLogFile) return;
    if (!recording) return;
    if (!action) return;
    char *message = NULL;

    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeNow[20] = {0};
    strftime(timeNow, 20, "%d.%m.%Y %H:%M:%S", &now);

    char timeVPS[20] = {0};
    strftime(timeVPS, 20, "%d.%m.%Y %H:%M:%S", &now);
    int offset = difftime(curr_time, recording->recStart);
    int h = offset/60/60;
    int m = (offset - h*60*60) / 60;
    int s = offset - h*60*60 - m*60;

    switch (severity) {
    case VPS_ERROR:
        esyslog("markad: VPS -> %s: offset: %02d:%02d:%02d, eventID: %d, state: %d, new state: %d -> %s", recording->title, h, m, s, recording->eventID, recording->runningStatus, newState, action);
        if (asprintf(&message, "%s ERROR: time offset: %02d:%02d:%02d, eventID: %d, old state %d, new state: %d -> %s", timeNow, h, m, s, recording->eventID, recording->runningStatus, newState, action) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    case VPS_INFO:
        isyslog("markad: VPS -> %s: offset: %02d:%02d:%02d, eventID: %d, state: %d, new state: %d -> %s", recording->title, h, m, s, recording->eventID, recording->runningStatus, newState, action);
        if (asprintf(&message, "%s INFO:  time offset: %02d:%02d:%02d, eventID: %d, old state %d, new state: %d -> %s", timeNow, h, m, s, recording->eventID, recording->runningStatus, newState, action) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    case VPS_DEBUG:
        dsyslog("markad: VPS -> %s: offset: %02d:%02d:%02d, eventID: %d, state: %d, new state: %d -> %s", recording->title, h, m, s, recording->eventID, recording->runningStatus, newState, action);
        if (asprintf(&message, "%s DEBUG: time offset: %02d:%02d:%02d, eventID: %d, old state %d, new state: %d -> %s", timeNow, h, m, s, recording->eventID, recording->runningStatus, newState, action) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    default:
        esyslog("markad: VPS -> invalid severity %d", severity);
        return;
    }
    ALLOC(strlen(message)+1, "message");

    fprintf(eventLogFile,"%s\n", message);
    fflush(eventLogFile);
    FREE(strlen(message)+1, "message");
    free(message);
}


void cEpgEventLog::LogEvent(const int severity, const char *title, char *eventLog) {
    if (!title) return;
    if (!eventLogFile) return;
    if (!eventLog) return;

    char *messageLog = NULL;
    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeNow[20] = {0};
    strftime(timeNow, 20, "%d.%m.%Y %H:%M:%S", &now);

    // syslog output
    switch (severity) {
    case VPS_ERROR:
        esyslog("markad: VPS -> %s: %s", title, eventLog);
        if (asprintf(&messageLog, "%s ERROR: %s", timeNow, eventLog) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    case VPS_INFO:
        isyslog("markad: VPS -> %s: %s", title, eventLog);
        if (asprintf(&messageLog, "%s INFO:  %s", timeNow, eventLog) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    case VPS_DEBUG:
        dsyslog("markad: VPS -> %s: %s", title, eventLog);
        if (asprintf(&messageLog, "%s DEBUG: %s", timeNow, eventLog) == -1) {
            esyslog("markad: cEpgEventLog::Log(): asprintf failed");
            return;
        }
        break;
    default:
        esyslog("markad: VPS -> invalid severity %d for message %s", severity, eventLog);
        return;
    }
    // logfile in recording directory output
    ALLOC(strlen(messageLog) + 1, "messageLog");
    fprintf(eventLogFile,"%s\n", messageLog);
    fflush(eventLogFile);

    FREE(strlen(messageLog) + 1, "messageLog");
    free(messageLog);
    FREE(strlen(eventLog) + 1, "eventLog");
    free(eventLog);
}


bool cEpgHandlerMarkad::HandleEitEvent(cSchedule *Schedule, const SI::EIT::Event *EitEvent, uchar TableID, uchar Version) {
    if (!EitEvent) return false;
    if (EitEvent->getEventId() <= 0) return false;
    if (EitEvent->getRunningStatus() <= 0) return false;
    if (!Schedule) return false;
    if (!StatusMarkAd) return false;

    pthread_mutex_lock(&mutex);
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
            dsyslog("markad: cEpgHandlerMarkad::HandleEitEvent(): eventID %6d, EIT eventID %5d, TableID %u, Version %u, start: %s, stop: %s, RunningStatus %d, channel: %s %s, title: %s", event->EventID(), EitEvent->getEventId(), TableID, Version, timerStart, timerStop, EitEvent->getRunningStatus(), *Schedule->ChannelID().ToString(), channel->Name(), event->Title());
        }
#endif
        StatusMarkAd->FindRecording(event, EitEvent, Schedule);
    }
    pthread_mutex_unlock(&mutex);
    return false; // let vdr call other handler too
}


bool cEpgHandlerMarkad::HandleEvent(cEvent *Event) {
    if (!Event) return false;
    if (Event->RunningStatus() <= 0) return false;

    pthread_mutex_lock(&mutex);
#ifdef DEBUG_VPS_VDR
    LOCK_CHANNELS_READ;
    const cChannel *channel = Channels->GetByChannelID(Event->ChannelID(), true);
    if ((channel) && (strcmp( channel->Name(), DEBUG_VPS_VDR) == 0)) {
        dsyslog("markad: cEpgHandlerMarkad::HandleEvent():    eventID %5d,          RunningStatus %d, channel: %s %s, title: %s", Event->EventID(), Event->RunningStatus(), *Event->ChannelID().ToString(), channel->Name(), Event->Title());
    }
#endif
    StatusMarkAd->FindRecording(Event, NULL, NULL);

    pthread_mutex_unlock(&mutex);
    return false;  // let vdr call other handler too
}


int cStatusMarkAd::Get_EIT_EventID(const sRecording *recording, const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *schedule, const bool nextEvent) {
#if APIVERSNUM>=20301  // feature not supported with old VDRs
    tEventID eitEventID  = eitEvent->getEventId();
    if (nextEvent) event = schedule->GetFollowingEvent();

    //  this is no real VPS control, we can only handle VPS events in the timer start/stop range, keep pre/post timer big enough, try to find in each EIT event
    time_t startTimeEIT   = eitEvent->getStartTime();
    time_t stopTimeEIT    = startTimeEIT + eitEvent->getDuration();

    if ((!nextEvent && (startTimeEIT > recording->timerStartTime) && (stopTimeEIT < recording->timerStopTime)) ||       // current event, VPS range is in timer range
            (nextEvent  && (startTimeEIT >  recording->timerStartTime) && (stopTimeEIT  > recording->timerStopTime))) { // next event, VPS range is after timer range

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


        char *eventLog = NULL;
        if (nextEvent) {
            if (recording->epgEventLog && (asprintf(&eventLog, "received EIT event for VDR next    event -> start: %s, stop: %s, eitEventID: %7u, channelID: %s", timerStartEIT, timerStopEIT, eitEventID, *schedule->ChannelID().ToString()) != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recording->epgEventLog->LogEvent(VPS_DEBUG, recording->title, eventLog);
            }
            if (recording->epgEventLog && (asprintf(&eventLog, "found EIT eventID %u for next VDR eventID %u", eitEventID, recording->eventNextID) != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recording->epgEventLog->LogEvent(VPS_DEBUG, recording->title, eventLog);
            }
        }
        else {
            if (recording->epgEventLog && (asprintf(&eventLog, "received EIT event for VDR current event -> start: %s, stop: %s, eitEventID: %7u, channelID: %s", timerStartEIT, timerStopEIT, eitEventID, *schedule->ChannelID().ToString()) != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recording->epgEventLog->LogEvent(VPS_DEBUG, recording->title, eventLog);
            }
            if (recording->epgEventLog && (asprintf(&eventLog, "found EIT eventID %u for current VDR eventID %u", eitEventID, recording->eventID) != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recording->epgEventLog->LogEvent(VPS_DEBUG, recording->title, eventLog);
            }
        }
        return eitEventID;
    }
#endif
    return 0;
}

void cStatusMarkAd::FindRecording(const cEvent *event, const SI::EIT::Event *eitEvent, const cSchedule *Schedule) {
    if (!setup->useVPS) return; // no VPS configured
    if (max_recs == -1) return; // no recording running, nothing to do

#if APIVERSNUM>=20301  // feature not supported with old VDRs
    tEventID eventID     = event->EventID();
    tChannelID channelID = event->ChannelID();
    int runningStatus    = event->RunningStatus();
    tEventID eitEventID  = 0;

    if (eitEvent) {
        eitEventID       = eitEvent->getEventId();
        runningStatus = eitEvent->getRunningStatus();
    }

    for (int i = 0; i <= max_recs; i++) {
        if (recs[i].eventID        ==  0) continue;   // this slot is not activ
        if (recs[i].runningStatus  == -1) continue;   // we have a final invalid state, no more state updates
        if (recs[i].eventChannelID == tChannelID::InvalidID) {
            dsyslog("markad: StatusMarkAd::FindRecording(): eventID %d: channelID invalid", eventID);
            continue;
        }

        // recording is now running but and we have no next event, try to get next eventID now
        // for all types of events
        if ((eventID == recs[i].eventID) && (channelID == recs[i].eventChannelID) && (runningStatus == 4) && (recs[i].eventNextID == 0)) {
            const cSchedule *schedule = NULL;
            if (eitEvent) schedule = Schedule;
            else schedule = event->Schedule();
            if (schedule) {
                const cEvent *eventNext = schedule->GetFollowingEvent();
                if (eventNext) {
                    tEventID eventNextID = eventNext->EventID();
                    if (eventNextID != eventID) {
                        char *eventLog = NULL;
                        if ((recs[i].epgEventLog) && (asprintf(&eventLog, "complete VDR event: eventID: %7d, channelID %s, set next VDR eventID %d", eventID, *channelID.ToString(), eventNextID) != -1)) {
                            ALLOC(strlen(eventLog) + 1, "eventLog");
                            recs[i].epgEventLog->LogEvent(VPS_DEBUG, recs[i].title, eventLog);
                        }
                    }
                    recs[i].eventNextID = eventNextID;  // before start of recording we are the next recording self
                }
            }
        }

        // process EIT event
        if (eitEvent && !recs[i].ignoreEIT) {
            if (recs[i].eventChannelID == Schedule->ChannelID()) {  // we do not know the EIT Event ID, with epg2vdr it is different from timer eventID
                if (recs[i].eitEventID     == 0) recs[i].eitEventID     = Get_EIT_EventID(&recs[i], event, eitEvent, Schedule, false);
                if (recs[i].eitEventNextID == 0) recs[i].eitEventNextID = Get_EIT_EventID(&recs[i], event, eitEvent, Schedule, true);
            }
            if ((recs[i].eitEventID == eitEventID)) {
                if (recs[i].runningStatus != runningStatus) {
                    SetVPSStatus(i, runningStatus, (eitEvent)); // store recording running status from EIT Event, with epg2vdr it is different from VDR event
                }
            }
            if ((recs[i].runningStatus == 4) && (runningStatus == 4) && (eitEventID == recs[i].eitEventNextID)) {  // next event got EIT start, for private channels this is the only stop event
                char *eventLog = NULL;
                if ((recs[i].epgEventLog) && (asprintf(&eventLog, "received EIT Event: eventID: %7d, eitEventID: %7d, runningStatus: %u -> next event started", eventID, eitEventID, runningStatus) != -1)) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[i].epgEventLog->LogEvent(VPS_INFO, recs[i].title, eventLog);
                }
                SetVPSStatus(i, 1, (eitEvent)); // store recording stop
            }
        }

        // process VDR event
        if (!eitEvent) {
            if ((eventID == recs[i].eventID) && (channelID == recs[i].eventChannelID)) {
                if (!recs[i].ignoreEIT) {
                    char *eventLog = NULL;
                    if ((recs[i].epgEventLog) && (asprintf(&eventLog, "received VDR Event: eventID: %7d, channelID %s, runningStatus: %u -> ignore future EIT events", eventID, *channelID.ToString(), runningStatus) != -1)) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[i].epgEventLog->LogEvent(VPS_DEBUG, recs[i].title, eventLog);
                    }
                    recs[i].ignoreEIT = true;
                }
                if (recs[i].runningStatus != runningStatus) {
                    SetVPSStatus(i, runningStatus, (eitEvent)); // store recording running status
                }
            }
            if ((recs[i].runningStatus == 4) && (runningStatus == 4) && (eventID == recs[i].eventNextID) && (channelID == recs[i].eventChannelID)) {  // next event got VPS start, for private channels this is the only stop event
                char *eventLog = NULL;
                if ((recs[i].epgEventLog) && (asprintf(&eventLog, "received VDR Event: channelID %s, eventID: %7d, runningStatus: %u -> next event started", *channelID.ToString(), eventID, runningStatus) != -1)) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[i].epgEventLog->LogEvent(VPS_INFO, recs[i].title, eventLog);
                }
                SetVPSStatus(i, 1, (eitEvent)); // store recording stop
            }
        }
    }
#endif
    return;
}


void cStatusMarkAd::SetVPSStatus(const int index, int runningStatus, const bool eventEIT) {
    // process changed running status
    char *eventLogAll = NULL;
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

        if (asprintf(&eventLogAll, "received %s event: eventID: %7d, eitEventID: %7d, runningStatus: %u -> %s", (eventEIT) ? "EIT" : "VDR", recs[index].eventID, recs[index].eitEventID, runningStatus, statusText) != -1) {
            ALLOC(strlen(eventLogAll) + 1, "eventLog");
            recs[index].epgEventLog->LogEvent(VPS_DEBUG, recs[index].title, eventLogAll);
        }
        FREE(strlen(statusText)+1, "statusText");
        free(statusText);
    }
    if ((recs[index].runningStatus == 0) && (runningStatus == 1)) { // VPS event not running
        if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_INFO, &recs[index], runningStatus, "before broadcast start");
        recs[index].runningStatus = 1;
        return;
    }

    if ((recs[index].runningStatus == 0) && (runningStatus == 4)) {  // to late recording start
        if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_ERROR, &recs[index], -1, "VPS event 4 (running) without event 1 (not yet running) or event 2 (starts shortly) before is invalid");
        recs[index].runningStatus = -1;
        return;
    }

    if ((recs[index].runningStatus <= 1) && (runningStatus == 2)) { // VPS event start expected in a few seconds
        if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], runningStatus, "broadcat starts in a few seconds");
        recs[index].runningStatus = 2;
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 2)) {
        if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], recs[index].runningStatus, "ignore event");
        return;
    }

    if ((recs[index].runningStatus == 2) && (runningStatus == 1)) {
        if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], recs[index].runningStatus, "ignore event");
        return;
    }

    if (((recs[index].runningStatus == 1) || (recs[index].runningStatus == 2)) && (runningStatus == 4)) { // VPS start
        if (recs[index].vpsStartTime && recs[index].vpsStopTime) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], runningStatus, "ignore event");
            char *eventLog = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&eventLog, "VPS event 'running' at status %d after we had start and stop events, ignoring event", recs[index].runningStatus) != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recs[index].epgEventLog->LogEvent(VPS_DEBUG, recs[index].title, eventLog);
            }
        }
        else {
            if (StoreVPSStatus("START", index)) {
                if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_INFO, &recs[index], runningStatus, "broadcast start");
                recs[index].runningStatus = 4;
            }
            else {
                if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], recs[index].runningStatus, "ignore event");
            }
        }
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 1)) {  // VPS stop
        if (StoreVPSStatus("STOP", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_INFO, &recs[index], runningStatus, "broadcast end");
            recs[index].runningStatus = 1;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_ERROR, &recs[index], runningStatus, "invalid VPS sequence, abort VPS detection");
            recs[index].runningStatus = -1;
        }
        return;
    }

    if ((recs[index].runningStatus == 4) && (runningStatus == 3)) { // VPS pause start
        if (StoreVPSStatus("PAUSE_START", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_INFO, &recs[index], runningStatus, "broadcast pause start");
            recs[index].runningStatus = 3;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], runningStatus, "ignore event");
        }
        return;
    }

    if ((recs[index].runningStatus == 3) && (runningStatus == 4)) { // VPS pause stop
        if (StoreVPSStatus("PAUSE_STOP", index)) {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_INFO, &recs[index], runningStatus, "broadcast pause stop");
            recs[index].runningStatus = 4;
        }
        else {
            if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_DEBUG, &recs[index], recs[index].runningStatus, "ignore event");
        }
        return;
    }

    eventLogAll = NULL;
    if ((recs[index].epgEventLog) && (asprintf(&eventLogAll, "ununexpected VPS event %d at status %d, ignoring this and future events", runningStatus, recs[index].runningStatus) != -1)) {
        ALLOC(strlen(eventLogAll) + 1, "eventLog");
        recs[index].epgEventLog->LogEvent(VPS_ERROR, recs[index].title, eventLogAll);
    }
    if (recs[index].epgEventLog) recs[index].epgEventLog->LogState(VPS_ERROR, &recs[index], -1, "invalid event");
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
        dsyslog("markad: cStatusMarkAd::SaveVPSEvents(): index %d out of range", index);
    }
    if (recs[index].runningStatus == -1 ) {
        esyslog("markad: VPS -> %s: event sequence not valid", recs[index].title);
        return;
    }
    if (!recs[index].vpsStartTime)  {
        esyslog("markad: VPS -> %s: no start event", recs[index].title);
        return;
    }
    if (!recs[index].vpsStopTime)  {
        esyslog("markad: VPS -> %s: no stop event", recs[index].title);
        return;
    }

    int offset;
    char timeVPSchar[20] = {0};
    struct tm timeVPStm;
    char *fileVPS = NULL;

    if (!asprintf(&fileVPS, "%s/%s", recs[index].fileName, "markad.vps")) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> asprintf failed", recs[index].title);
        return;
    }
    ALLOC(strlen(fileVPS)+1, "fileVPS");

    FILE *pFile = fopen(fileVPS,"a+");
    if (!pFile) {
        esyslog("markad: cStatusMarkAd::SaveVPSEvents(): recording <%s> open file %s failed", recs[index].title, fileVPS);
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
    dsyslog("markad: VPS %s: got VPS %s event at %s", recs[index].title, status, timeVPS);
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
        if (curr_time > recs[index].vpsPauseStartTime + 50) { // PAUSE STOP must be at least 1 min after PAUSE START, changed from 60 to 50
            if (recs[index].vpsPauseStopTime == 0) {
                recs[index].vpsPauseStopTime=curr_time;
                return true;
            }
            else {
                char *eventLog = NULL;
                if ((recs[index].epgEventLog) && (asprintf(&eventLog, "VPS pause stop already received, pause stop now set to last event") != -1)) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[index].epgEventLog->LogEvent(VPS_DEBUG, recs[index].title, eventLog);
                }
                recs[index].vpsPauseStopTime=curr_time;
                return true;
            }
        }
        else {
            char *eventLog = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&eventLog, "VPS pause stop to fast after pause start, ignoring") != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recs[index].epgEventLog->LogEvent(VPS_ERROR, recs[index].title, eventLog);
            }
            return false;
        }
    }
    if (strcmp(status,"STOP") == 0) {
        if ( curr_time >  recs[index].vpsStartTime + 60) {  // a valid STOP must be at least 1 min after START
            recs[index].vpsStopTime = curr_time;
            return true;
        }
        else {
            char *eventLog = NULL;
            if ((recs[index].epgEventLog) && (asprintf(&eventLog, "VPS stop to fast after start, invalid VPS sequence, abort VPS detection") != -1)) {
                ALLOC(strlen(eventLog) + 1, "eventLog");
                recs[index].epgEventLog->LogEvent(VPS_ERROR, recs[index].title, eventLog);
            }
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


bool cStatusMarkAd::Start(const char *Name, const char *FileName, const bool direct, sRecording *recording) {
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
        if (setup->autoLogoMenu >= 0) {
            if(! asprintf(&autoLogoOption, " --autologo=%i ", setup->autoLogoMenu)) {
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

    // prepare cmd option
    char *cmdOption = NULL;
    if (setup->ProcessDuring == PROCESS_AFTER) {
        if(!asprintf(&cmdOption, " after")) {
            esyslog("markad: asprintf svdrPortOption ouf of memory");
            return false;
        }
        else ALLOC(strlen(cmdOption) + 1, "cmdOption");
    }
    if (setup->ProcessDuring == PROCESS_DURING) {
        if(!asprintf(&cmdOption, " --online=%d before ", direct ? 1 : 2)) {
            esyslog("markad: asprintf cmdOption ouf of memory");
            return false;
        }
        else ALLOC(strlen(cmdOption) + 1, "cmdOption");
    }

    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
                                   bindir,
                                   setup->Verbose ? " -v " : "",
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
                                   cmdOption,
                                   FileName);
    FREE(strlen(autoLogoOption)+1, "autoLogoOption");
    free(autoLogoOption);
    FREE(strlen(svdrPortOption)+1, "svdrPortOption");
    free(svdrPortOption);
    FREE(strlen(svdrPortOption)+1, "cmdOption");
    free(cmdOption);

    usleep(1000000); // wait 1 second
    if (SystemExec(cmd) != -1) {
        dsyslog("markad: cStatusMarkAd::Start(): executing %s", *cmd);
        usleep(200000);
        int pos = Add(Name, FileName, recording);
        bool gotPID = getPid(pos); // will set recs[pos].pid
        dsyslog("markad: cStatusMarkAd::Start(): index %d, pid %d, filename %s: running markad stored in list", pos, recs[pos].pid, FileName ? FileName : "<NULL>");
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


void cStatusMarkAd::GetEventID(const cDevice *Device, const char *Name, sRecording *recording) {
    if (!Name)           return;
    if (!Device)         return;
    if (!recording)      return;

#if APIVERSNUM>=20301  // feature not supported with old VDRs
    recording->timerStartTime = 0;
    recording->timerStopTime  = 0;
    recording->eventID        = 0;
    recording->eventNextID    = 0;
    int timeDiff              = INT_MAX;


// search for timer to recording
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, device: %s, search for timer", Name, *Device->DeviceName());
#if APIVERSNUM>=20301
    const cTimer *timer = NULL;
    cStateKey StateKey;
#ifdef DEBUG_LOCKS
    dsyslog("markad: cStatusMarkAd:GetEventID(): WANT   timers READ");
#endif
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey, LOCK_TIMEOUT)) {
#ifdef DEBUG_LOCKS
        dsyslog("markad: cStatusMarkAd:GetEventID(): LOCKED timers READ");
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
                        timer = Timer;
                        timeDiff = abs(Timer->StartTime() - time(NULL));
                    }
                }
            }
        }
#if APIVERSNUM>=20301
    }
    else {
        esyslog("markad: cStatusMarkAd::GetEventID(): lock timers failed");
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
    recording->timerStartTime   = timer->StartTime();
    recording->timerStopTime    = timer->StopTime();
    recording->timerChannelID   = timer->Channel()->GetChannelID();
    recording->timerChannelName = strdup(timer->Channel()->Name());
    ALLOC(strlen(recording->timerChannelName) + 1, "timerChannelName");
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, timer title: %s, channelID: %s", Name, timer->File(), *recording->timerChannelID.ToString());
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, timer start: %s, stop: %s", Name, strtok(ctime(&recording->timerStartTime), "\n"), strtok(ctime(&recording->timerStopTime), "\n"));
    if (timer->HasFlags(tfVps)) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer <%s> uses VPS", timer->File());
        recording->timerVPS = true;
    }

    // search for event to timer
    if (!timer->Event()) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer for %s has no event", Name);
    }
    else { // we found the event
        const cEvent *event = timer->Event();
        if (event) {
            recording->eventTitle = strdup(event->Title());
            ALLOC(strlen(recording->eventTitle) + 1, "eventTitle");
            recording->eventID        = event->EventID();
            recording->eventChannelID = event->ChannelID();
            recording->eventStartTime = event->StartTime();
            recording->eventStopTime  = event->EndTime();
            const cSchedule *schedule = event->Schedule();
            if (schedule) {
                const cEvent *eventNext = schedule->GetFollowingEvent();
                if (eventNext) {
                    recording->eventNextID = eventNext->EventID();
                    if (recording->eventNextID == recording->eventID) recording->eventNextID = 0;  // before start of recording we are the next recording self
                }
            }
        }
    }
#if APIVERSNUM>=20301
#ifdef DEBUG_LOCKS
    dsyslog("markad: GetEventID(): UNLOCK timers READ");
#endif
    StateKey.Remove();
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, event title: %s, channelID: %s", Name, recording->eventTitle, *recording->eventChannelID.ToString());
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, event eventID: %u, eventNextID: %u", Name, recording->eventID, recording->eventNextID);
    dsyslog("markad: cStatusMarkAd::GetEventID(): recording: %s, event start: %s, stop: %s", Name, strtok(ctime(&recording->eventStartTime), "\n"), strtok(ctime(&recording->eventStopTime), "\n"));
    if (timer->HasFlags(tfVps)) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer <%s> uses VPS", timer->File());
        recording->timerVPS = true;
    }
#endif
#endif
    return;
}


void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) {
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir)   return; // we cannot operate without bindir
    if (!logodir)  return; // we don't want to operate without logodir

// recording started
    if (On) {
        runningRecordings++;
        dsyslog("markad: cStatusMarkAd::Recording():  recording: %s, file name: %s, started, recording count now %d", Name, FileName, runningRecordings);
        // check if markad is running for the same recording, this can happen if we have a short recording interuption
        int runningPos = Get(FileName, NULL);
        if (runningPos >= 0) {
            isyslog("markad: is running on the same recording %s", FileName);
            Remove(runningPos, true);
        }

        sRecording recording;
        GetEventID(Device, Name, &recording);
        SaveVPSTimer(FileName, recording.timerVPS);

        if ((setup->ProcessDuring == PROCESS_NEVER) && setup->useVPS) {  // markad start disabled per config menu, add recording for VPS detection
            int pos = Add(Name, FileName, &recording);
            if (pos >= 0) dsyslog("markad: cStatusMarkAd::Recording(): added recording <%s> channelID %s, event ID %u, eventNextID %u at index %i only for VPS detection", Name, *recording.eventChannelID.ToString(), recording.eventID, recording.eventNextID, pos);
            return;
        }
        if (setup->ProcessDuring == PROCESS_NEVER) {
            isyslog("markad: deactivated by user");
            return; // markad deactivated
        }

        bool autoLogo = false;
        if (setup->autoLogoConf >= 0) autoLogo = (setup->autoLogoConf > 0);
        else autoLogo = (setup->autoLogoMenu > 0);

        if (!autoLogo && setup->LogoOnly && !LogoExists(Device,FileName)) {   // we can find the logo in the recording
            isyslog("markad: no logo found for %s", Name);
            return;
        }

        // Start markad with recording
        if (!Start(Name, FileName, false, &recording)) {
            esyslog("markad: failed starting on <%s>", FileName);
        }
    }
// recording ended
    else {
        runningRecordings--;
        if (runningRecordings < 0) runningRecordings = 0;
        dsyslog("markad: cStatusMarkAd::Recording(): recording stopped, recording count now %d", runningRecordings);
#ifdef DEBUG_PAUSE_CONTINUE
        dsyslog("markad: cStatusMarkAd::Recording(): setup->ProcessDuring %d, setup->whileRecording %d, setup->whileReplaying %d", setup->ProcessDuring, setup->whileRecording, setup->whileReplaying);
#endif
        int pos = Get(FileName, Name);
        if (pos >= 0) {
            dsyslog("markad: cStatusMarkAd::Recording(): recording: %s, index %d, pid %d, recording stopped", recs[pos].title, pos, recs[pos].pid);
            if (setup->useVPS) SaveVPSEvents(pos);  // store to get error messages for incomplete sequence
            if ((setup->ProcessDuring == PROCESS_DURING) || (setup->ProcessDuring == PROCESS_NEVER)) { // PROCESS_NEVER: recording maybe in list from vps detection
                dsyslog("markad: cStatusMarkAd::Recording(): recording: %s, remove from list", recs[pos].title);
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
        else esyslog("markad: cStatusMarkAd::Recording(): unknown recording %s stopped", FileName);
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
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey, LOCK_TIMEOUT)) {
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
    else {
        esyslog("markad: cStatusMarkAd::LogoExists(): lock timers failed");
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
    if (!recs[Position].pid) return false;
    int ret = 0;
    char procname[256] = "";
    snprintf(procname, sizeof(procname), "/proc/%i/stat", recs[Position].pid);
    FILE *fstat = fopen(procname, "r");
    if (fstat) {
        // found a running markad
        ret = fscanf(fstat, "%*10d %*255s %c", &recs[Position].status);
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
    if (ret != 1) dsyslog("markad: cStatusMarkAd::getStatus(): markad terminated for index %i, recording %s", Position, recs[Position].fileName ? recs[Position].fileName : "<NULL>");
#endif
    return (ret == 1);
}


bool cStatusMarkAd::getPid(int Position) {
    if (Position < 0) return false;
    if (!recs[Position].fileName) return false;
    if (recs[Position].pid) return true;
    int ret = 0;
    char *buf;
    if (asprintf(&buf, "%s/markad.pid", recs[Position].fileName) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    usleep(500*1000);   // wait 500ms to give markad time to create pid file
    FILE *fpid = fopen(buf,"r");
    if (fpid) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        int pid;
        ret = fscanf(fpid, "%10i\n", &pid);
        if (ret == 1) recs[Position].pid = pid;
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


bool cStatusMarkAd::GetNextActive(struct sRecording **RecEntry) {
    if (!RecEntry) return false;
    *RecEntry = NULL;

    if (actpos >= (MAXDEVICES*MAXRECEIVERS)) return false;

    do {
        if ((recs[actpos].fileName) && (recs[actpos].pid)) {
            if (getStatus(actpos)) {
                /* check if recording directory still exists */
                if (access(recs[actpos].fileName, R_OK) == -1) {
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
    struct sRecording *tmpRecs = NULL;
    ResetActPos();
    while (GetNextActive(&tmpRecs)) ;
}


bool cStatusMarkAd::MarkAdRunning() {
    struct sRecording *tmpRecs = NULL;
    ResetActPos();
    bool running = false;
    while (GetNextActive(&tmpRecs)) {
        if (tmpRecs->title) dsyslog("markad: markad is running for recording %s, defere shutdown", tmpRecs->title);
        else dsyslog("markad: markad is running for unknown recording, defere shutdown");
        running = true;
    }
    return (running);
}


int cStatusMarkAd::Get(const char *FileName, const char *Name) {
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        if (Name && recs[i].title && !strcmp(recs[i].title, Name)) return i;
        if (FileName && recs[i].fileName && !strcmp(recs[i].fileName, FileName)) return i;
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
    if (recs[pos].fileName) {
        dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d, filename %s: remove from list", pos, recs[pos].pid, (recs[pos].fileName) ? recs[pos].fileName : "<NULL>");
        if (recs[pos].runningStatus == 4) isyslog("markad: got no VPS stop event for recording %s", recs[pos].fileName);
        FREE(strlen(recs[pos].fileName) + 1, "recs[pos].fileName");
        free(recs[pos].fileName);
        recs[pos].fileName = NULL;
    }
    // recoring title / timer title
    if (recs[pos].title) {
        FREE(strlen(recs[pos].title) + 1, "recs[pos].title");
        free(recs[pos].title);
        recs[pos].title = NULL;
    }
    // event title
    if (recs[pos].eventTitle) {
        FREE(strlen(recs[pos].eventTitle) + 1, "eventTitle");
        free(recs[pos].eventTitle);
        recs[pos].eventTitle = NULL;
    }
    // timer channel name
    if (recs[pos].timerChannelName) {
        FREE(strlen(recs[pos].timerChannelName) + 1, "timerChannelName");
        free(recs[pos].timerChannelName);
        recs[pos].timerChannelName = NULL;
    }
    if ((Kill) && (recs[pos].pid)) {
        if (getStatus(pos)) {
            if ((recs[pos].status == 'R') || (recs[pos].status == 'S')) {
                dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d: terminating markad process", pos, recs[pos].pid);
                kill(recs[pos].pid, SIGTERM);
            }
            else {
                dsyslog("markad: cStatusMarkAd::Remove(): index %d, pid %d: killing markad process", pos, recs[pos].pid);
                kill(recs[pos].pid, SIGKILL);
            }
        }
    }
    recs[pos].status            = 0;
    recs[pos].pid               = 0;
    recs[pos].changedByUser     = false;
    recs[pos].ignoreEIT         = false;
    recs[pos].eventID           = 0;
    recs[pos].eventNextID       = 0;
    recs[pos].eitEventID        = 0;
    recs[pos].eitEventNextID    = 0;
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
        if (recs[i].fileName) {
            max_recs = i;
        }
    }
}


char *cStatusMarkAd::GetStatus() {
    char *status = NULL;  // vdr will free this memory
    for (int pos = 0; pos < (MAXDEVICES*MAXRECEIVERS); pos++) {
        if (!recs[pos].fileName) continue;
        dsyslog("markad: cStatusMarkAd::GetStatus(): active recording with markad running: %s",recs[pos].fileName);
        char *line = NULL;
        char *tmp = NULL;
        if (asprintf(&line, "markad: running for %s\n", recs[pos].fileName) != -1) {
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


int cStatusMarkAd::Add(const char *Name, const char *FileName, sRecording *recording) {
    for (int pos = 0; pos < (MAXDEVICES * MAXRECEIVERS); pos++) {
        if (!recs[pos].fileName) {
            // file name
            recs[pos].fileName = strdup(FileName);
            ALLOC(strlen(recs[pos].fileName)+1, "recs[pos].fileName");
            // recording title / timer title
            if (Name) {
                recs[pos].title = strdup(Name);
                ALLOC(strlen(recs[pos].title)+1, "recs[pos].title");
            }
            else {
                recs[pos].title = NULL;
            }
            // event title
            recs[pos].eventTitle = recording->eventTitle;  // allocated by GetEventID

            recs[pos].status            = 0;
            recs[pos].pid               = 0;
            recs[pos].changedByUser     = false;
            recs[pos].eventID           = recording->eventID;
            recs[pos].eventNextID       = recording->eventNextID;
            recs[pos].eitEventID        = 0;
            recs[pos].eitEventNextID    = 0;
            recs[pos].timerStartTime    = recording->timerStartTime;
            recs[pos].timerStopTime     = recording->timerStopTime;
            recs[pos].eventStartTime    = recording->eventStartTime;
            recs[pos].eventStopTime     = recording->eventStopTime;
            recs[pos].runningStatus     = 0;
            recs[pos].recStart          = time(NULL);
            recs[pos].vpsStartTime      = 0;
            recs[pos].vpsStopTime       = 0;
            recs[pos].vpsPauseStartTime = 0;
            recs[pos].vpsPauseStopTime  = 0;
            recs[pos].timerVPS          = recording->timerVPS;
            recs[pos].eventChannelID    = recording->eventChannelID;
            recs[pos].timerChannelID    = recording->timerChannelID;
            recs[pos].timerChannelName  = recording->timerChannelName;
            recs[pos].ignoreEIT         = false;

            if (setup->useVPS && setup->logVPS) {
                recs[pos].epgEventLog = new cEpgEventLog(FileName);
                ALLOC(sizeof(*(recs[pos].epgEventLog)), "recs[pos].epgEventLog");
            }
            if (!epgHandlerMarkad && setup->useVPS) {
                epgHandlerMarkad = new cEpgHandlerMarkad(this);     // VDR will free at stop
                dsyslog("markad: cStatusMarkAd::Add():: create epg event handler");
            }
            if (pos > max_recs) max_recs = pos;

            if (recs[pos].epgEventLog && recs[pos].title) {
                char *eventLog = NULL;

                // timer infos
                struct tm start = *localtime(&recs[pos].timerStartTime);
                char timerStart[20] = {0};
                strftime(timerStart, 20, "%d.%m.%Y %H:%M:%S", &start);
                struct tm stop = *localtime(&recs[pos].timerStopTime);
                char timerStop[20] = {0};
                strftime(timerStop, 20, "%d.%m.%Y %H:%M:%S", &stop);

                if (asprintf(&eventLog, "timer recording index: %d", pos) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }
                if (asprintf(&eventLog, "VDR timer title: %s", recs[pos].title) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }
                if (recording->timerChannelID == tChannelID::InvalidID) {
                    if (asprintf(&eventLog, "VDR timer channelID missing") != -1) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[pos].epgEventLog->LogEvent(VPS_ERROR, recs[pos].title, eventLog);
                    }
                }
                else {
                    if (asprintf(&eventLog, "VDR timer channelID: %s", *recs[pos].timerChannelID.ToString()) != -1) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                    }
                }
                if (recs[pos].timerChannelName && asprintf(&eventLog, "VDR timer channel name: %s", recs[pos].timerChannelName) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }
                if (asprintf(&eventLog, "VDR timer start: %s, stop: %s", timerStart, timerStop) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }

                // event infos
                start = *localtime(&recs[pos].eventStartTime);
                char eventStart[20] = {0};
                strftime(eventStart, 20, "%d.%m.%Y %H:%M:%S", &start);
                stop = *localtime(&recs[pos].eventStopTime);
                char eventStop[20] = {0};
                strftime(eventStop, 20, "%d.%m.%Y %H:%M:%S", &stop);

                if (asprintf(&eventLog, "VDR event title: %s", recs[pos].eventTitle) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }
                if (recording->eventChannelID == tChannelID::InvalidID) {
                    if (asprintf(&eventLog, "VDR event channelID missing") != -1) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[pos].epgEventLog->LogEvent(VPS_ERROR, recs[pos].title, eventLog);
                    }
                }
                else {
                    if (asprintf(&eventLog, "VDR event channelID: %s", *recs[pos].eventChannelID.ToString()) != -1) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                    }
                }
                if (asprintf(&eventLog, "VDR event eventID: %u, eventNextID: %u", recs[pos].eventID, recs[pos].eventNextID) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }
                if (asprintf(&eventLog, "VDR event start: %s, stop: %s", eventStart, eventStop) != -1) {
                    ALLOC(strlen(eventLog) + 1, "eventLog");
                    recs[pos].epgEventLog->LogEvent(VPS_DEBUG, recs[pos].title, eventLog);
                }

                // check start/stop time
                if ((recs[pos].eventStartTime < recs[pos].timerStartTime) || (recs[pos].eventStopTime > recs[pos].timerStopTime)) {
                    if (asprintf(&eventLog, "VDR event start/stop time invalid") != -1) {
                        ALLOC(strlen(eventLog) + 1, "eventLog");
                        recs[pos].epgEventLog->LogEvent(VPS_ERROR, recs[pos].title, eventLog);
                    }
                }
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
            if ((recs[i].fileName) && (!strcmp(recs[i].fileName,FileName)) && (recs[i].pid) && (!recs[i].changedByUser)) {
                dsyslog("markad: cStatusMarkAd::Pause(): index %d, pid %d, filename %s: pause markad process", i, recs[i].pid, FileName);
                kill(recs[i].pid, SIGTSTP);
            }
        }
        else {
            if ((recs[i].pid) && (!recs[i].changedByUser)) {
                dsyslog("markad: cStatusMarkAd::Pause(): index %d, pid %d, filename %s: pause markad process", i, recs[i].pid, recs[i].fileName ? recs[i].fileName : "<NULL>");
                kill(recs[i].pid, SIGTSTP);
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
            if ((recs[i].fileName) && (!strcmp(recs[i].fileName,FileName)) && (recs[i].pid) && (!recs[i].changedByUser) ) {
                dsyslog("markad: cStatusMarkAd::Continue(): index %d, pid %i, filename %s: resume markad process", i, recs[i].pid, recs[i].fileName);
                kill(recs[i].pid, SIGCONT);
            }
        }
        else {
            if ((recs[i].pid) && (!recs[i].changedByUser)) {
                dsyslog("markad: cStatusMarkAd::Continue(): index %d, pid %d, filename %s: resume markad process", i, recs[i].pid, recs[i].fileName ? recs[i].fileName : "<NULL>");
                kill(recs[i].pid, SIGCONT);
            }
        }
    }
}
