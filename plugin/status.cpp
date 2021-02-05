/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

// VPS event test (23.05.2020)
//          channel                     VPS running states events          comment
//
// Das Erste HD;ARD:                   0 -> 1 -> 2 -> 4 (-> 2 -> 4) -> 1   OK
// ONE HD;ARD:                         0 -> 1 -> 2 -> 4 -> 1               OK
// SWR BW HD;ARD:                      0 -> 1 -> 2 -> 4 (-> 2 -> 4)        OK
// BR Fernsehen Süd HD;ARD:            0 -> 1 -> 2 -> 4 -> 1               OK
// WDR HD Köln;ARD:                    0 -> 1 -> 2 -> 4 (-> 2 -> 4) -> 1   OK
// hr-fernsehen HD;ARD:                0 -> 1 -> 2 -> 4 -> 1 (-> 4 -> 1)   OK
// NDR FS HH HD;ARD:                   0 -> 1 -> 2 (-> 1) -> 4 -> 1        OK
// rbb Berlin HD;ARD:                  0 -> 1 -> 4 -> 1                    OK
// ARD-alpha HD;ARD:                   0 -> 1 -> 4 -> 1                    OK
// arte HD;ARD:                        0 -> 2 (-> 1) -> 4 -> 1             OK
// SR Fernsehen HD;ARD:                0 -> 1 -> 4                         no real VPS, got start at planed time, no stop event
// tagesschau24 HD;ARD:                0 -> 1 -> 4                         no real VPS, got start at planed time, no stop event
// MDR Sachsen HD;ARD:                 no VPS events
// phoenix HD;ARD:                     no VPS events
//
// ZDF HD;ZDFvision:                   0 -> 1 -> 2 -> 4 -> 1               OK
// 3sat HD;ZDFvision:                  0 -> 1 -> 2 -> 4 (-> 2 ->4) -> 1    OK
// zdf_neo HD;ZDFvision:               0 -> 1 -> 2 -> 4 -> 1               OK
// ZDFinfo HD;ZDFvision:               0 -> 1 -> 4 (-> 1 ->4) -> 1         OK
// KiKA HD;ZDFvision:                  0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
//
// RTL Television,RTL;CBC:             0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
// RTL2;CBC:                           0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
// SUPER RTL;CBC:                      0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
// VOX;CBC:                            0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
// VOXup;CBC:                          0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
// RTLplus;CBC:                        0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// NITRO;CBC:                          no VPS events
// n-tv;CBC:                           no VPS events
//
// SAT.1;ProSiebenSat.1:               no VPS events
// SAT.1 Gold;ProSiebenSat.1:          no VPS events
// ProSieben;ProSiebenSat.1:           no VPS events
// kabel eins;ProSiebenSat.1:          no VPS event
// kabel eins Doku;ProSiebenSat.1:     0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// Pro7 MAXX;ProSiebenSat.1:           0 -> 4                              invalid
// SIXX;ProSiebenSat.1:                0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// WELT;ProSiebenSat.1:                0 -> 4                              invalid
//
// DMAX;BetaDigital:                   0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// Disney Channel;BetaDigital:         0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// TELE 5;BetaDigital:                 0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// ANIXE HD;BetaDigital:               0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// ANIXE+;BetaDigital:                 0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// TLC;BetaDigital:                    0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// Zee One HD;BetaDigital:             0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// SPORT1;BetaDigital:                 0 -> 2 -> 4                         no real VPS, got start at planed time, no stop event
// N24 DOKU;BetaDigital:               0 -> 4                              invalid
//
// NICK/MTV+;MTV Networks Europe:      0 -> 1 -> 4                         no real VPS, got start at planed time, no stop event
// Comedy Central;MTV Networks Europe: no VPS events
//
// Bibel TV HD;ORS:                    0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
//
// ServusTV HD Deutschland;ServusTV:   0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event
//
// münchen.tv HD;BMT:                  no VPS events
//
// Welt der Wunder;-:                  0 -> 1 -> 2                         invalid
// RiC;-:                              0 -> 1 -> 2 -> 4                    no real VPS, got start at planed time, no stop event


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
    fprintf(eventLogFile, "         time        EventID      now     next    EIT  state event new  offset  action\n");
}


cEpgEventLog::~cEpgEventLog() {
    fclose(eventLogFile);
}


void cEpgEventLog::Log(const time_t recStart, const tEventID recEventID, const tEventID eventID, const tEventID followingEventID, const tEventID eitEventID, const int state, const int event, const int newState, const char* action) {
    if (!eventLogFile) return;
    if (!action) return;
    char *message = NULL;
    time_t curr_time = time(NULL);
    struct tm now = *localtime(&curr_time);
    char timeVPS[20] = {0};
    strftime(timeVPS, 20, "%d.%m.%Y %H:%M:%S", &now);
    int offset = difftime(curr_time, recStart);
    int h = offset/60/60;
    int m = (offset - h*60*60) / 60;
    int s = offset - h*60*60 - m*60;
    if (asprintf(&message, "%s  %7u  %7u  %7u  %5u    %i     %i   % 1i  %02i:%02i:%02i %s", timeVPS, recEventID, eventID, followingEventID, eitEventID, state, event, newState, h, m, s, action) == -1) {
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
    if (!StatusMarkAd) return false;
    if (EitEvent->getRunningStatus()) StatusMarkAd->SetVPSStatus(Schedule, EitEvent);
    return false;
}


/*
bool cEpgHandlerMarkad::HandleEvent(cEvent *Event) {
    if (Event->RunningStatus()) StatusMarkAd->SetVPSStatus(Event->EventID(), Event->RunningStatus());
    return false;
}
*/


// vdr RunningStatus: 0=undefined, 1=not running, 2=starts in a few seconds, 3=pausing, 4=running

void cStatusMarkAd::SetVPSStatus(const cSchedule *Schedule, const SI::EIT::Event *EitEvent) {
    if (!setup->useVPS) return;
    if (max_recs == -1) return;
    if (!EitEvent) return;
    if (!Schedule) return;
    tEventID eventID = 0;
    tEventID followingEventID = 0;
    if (Schedule->GetPresentEvent()) eventID = Schedule->GetPresentEvent()->EventID();
    if (Schedule->GetFollowingEvent()) followingEventID = Schedule->GetFollowingEvent()->EventID();
    tEventID eitEventID = (unsigned int) EitEvent->getEventId();
    for (int i = 0; i <= max_recs; i++) {
        if (recs[i].eitEventID == 0) {  // we do not know the EIT Event ID, with epg2vdr it is different from timer eventID
            time_t startTimeEIT = EitEvent->getStartTime();
            time_t stopTimeEIT = startTimeEIT + EitEvent->getDuration();
            if ((recs[i].eventID != eventID) && (recs[i].eventID != followingEventID)) continue;
            if (!((startTimeEIT >= recs[i].timerStartTime) && (stopTimeEIT <= recs[i].timerStopTime))) continue;
            if ((startTimeEIT - recs[i].timerStartTime) > (recs[i].timerStopTime - stopTimeEIT)) continue;  // pre timer must be smaller or equal than post timer
                                                                                                            // to prevent getting short next broadcast
            recs[i].eitEventID = eitEventID;

            struct tm start = *localtime(&startTimeEIT);
            char timerStart[20] = {0};
            strftime(timerStart, 20, "%d.%m.%Y %H:%M:%S", &start);
            struct tm stop = *localtime(&stopTimeEIT);
            char timerStop[20] = {0};
            strftime(timerStop, 20, "%d.%m.%Y %H:%M:%S", &stop);
            char *log = NULL;
            if ((recs[i].epgEventLog) && (asprintf(&log, "        EIT eventID %7u, start %s, stop %s", eitEventID, timerStart, timerStop) != -1)) recs[i].epgEventLog->Log(log);
            if (log) free(log);
        }
        if (recs[i].eitEventID != eitEventID) continue;

        int runningStatus = EitEvent->getRunningStatus();
        if (recs[i].runningStatus == runningStatus) return;
        if (recs[i].runningStatus < 0 ) return;

        char *logAll = NULL;
        if ((recs[i].epgEventLog) && (asprintf(&logAll, "got event %u", runningStatus) != -1)) recs[i].epgEventLog->Log(logAll);
        if (logAll) free(logAll);

        if ((recs[i].runningStatus == 0) && (runningStatus == 1)) { // VPS event not running
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "accept");
            recs[i].runningStatus = 1;
            return;
        }

        if ((recs[i].runningStatus == 0) && (runningStatus == 4)) {  // to late VPS timer start or invalid vps start sequence
            if (recs[i].timerVPS) {
                if (StoreVPSStatus("START", i)) {
                    if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "late VPS timer start");
                    recs[i].runningStatus = 4;
                }
                else {
                    if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
                }
            }
            else {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "invalid start sequence");
                recs[i].runningStatus = -1;
            }
            return;
        }

        if ((recs[i].runningStatus <= 1) && (runningStatus == 2)) { // VPS event start expected in a few seconds
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "accept");
            recs[i].runningStatus = 2;
            return;
        }

        if ((recs[i].runningStatus == 4) && (runningStatus == 2)) {
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            return;
        }

        if ((recs[i].runningStatus == 2) && (runningStatus == 1)) {
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            return;
        }

        if (((recs[i].runningStatus == 1) || (recs[i].runningStatus == 2)) && (runningStatus == 4)) { // VPS start
            if (recs[i].vpsStartTime && recs[i].vpsStopTime) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
                char *log = NULL;
                if ((recs[i].epgEventLog) && (asprintf(&log, "VPS event 'running' at status %i after we had start and stop events, ignoring event", recs[i].runningStatus) != -1)) recs[i].epgEventLog->Log(log);
                free(log);
            }
            else {
                if (StoreVPSStatus("START", i)) {
                    if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "start");
                    recs[i].runningStatus = 4;
                }
                else {
                    if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
                }
            }
            return;
        }

        if ((recs[i].runningStatus == 4) && (runningStatus == 1)) {  // VPS stop
            if (StoreVPSStatus("STOP", i)) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "stop");
                if ((recs[i].vpsStopTime - recs[i].vpsStartTime) >  EitEvent->getDuration() / 2) recs[i].runningStatus = 1; // recording length at least half VPS duration
                else {
                    recs[i].runningStatus = -1;
                    char *log = NULL;
                    if ((recs[i].epgEventLog) && (asprintf(&log, "stop event results in too short recording") != -1)) recs[i].epgEventLog->Log(log);
                    if (log) free(log);
                }
                SaveVPSEvents(i);
            }
            else {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "reset");
                recs[i].runningStatus = 1;
            }
            return;
        }

        if ((recs[i].runningStatus == 4) && (runningStatus == 3)) { // VPS pause start
            if (StoreVPSStatus("PAUSE_START", i)) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "pause start");
                recs[i].runningStatus = 3;
            }
            else {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            }
            return;
        }

        if ((recs[i].runningStatus == 3) && (runningStatus == 4)) { // VPS pause stop
            if (StoreVPSStatus("PAUSE_STOP", i)) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "pause stop");
                recs[i].runningStatus = 4;
            }
            else {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            }
            return;
        }

        logAll = NULL;
        if ((recs[i].epgEventLog) && (asprintf(&logAll, "ununexpected VPS event %i at status %i, ignoring this and future events", runningStatus, recs[i].runningStatus) != -1)) recs[i].epgEventLog->Log(logAll);
        free(logAll);
        if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, -1, "invalid");
        recs[i].runningStatus = -1;
        return;
    }
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
        dsyslog("markad: cStatusMarkAd::SaveVPSEvents(): VPS Infos not valid for recording <%s>", recs[index].Name);
        return;
    }
    if (!recs[index].vpsStartTime)  {
        dsyslog("markad: cStatusMarkAd::SaveVPSEvents(): no VPS start time to save for recording <%s>", recs[index].Name);
        return;
    }
    if (!recs[index].vpsStopTime)  {
        dsyslog("markad: cStatusMarkAd::SaveVPSEvents(): no VPS stop time to save for recording <%s>", recs[index].Name);
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
            if ((recs[index].epgEventLog) && (asprintf(&log, "VPS stop to fast after start, reset to not running") != -1)) recs[index].epgEventLog->Log(log);
            if (log) free(log);
            recs[index].vpsStartTime=0;
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
}


cStatusMarkAd::~cStatusMarkAd() {
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        Remove(i, true);
    }
}


int cStatusMarkAd::Recording() {
    int cnt = 0;
    for (int i = 0; i < cDevice::NumDevices(); i++) {
        cDevice *dev = cDevice::GetDevice(i);
        if (dev) {
            if (dev->Receiving()) cnt++;
        }
    }
    return cnt;
}


bool cStatusMarkAd::Replaying() {
    for (int i = 0; i < cDevice::NumDevices(); i++) {
        cDevice *dev = cDevice::GetDevice(i);
        if (dev) {
            if (dev->Replaying()) return true;
        }
    }
    return false;
}


void cStatusMarkAd::Replaying(const cControl *UNUSED(Control), const char *UNUSED(Name), const char *UNUSED(FileName), bool On) {
    if (setup->ProcessDuring != PROCESS_AFTER) return;
    if (setup->whileReplaying) return;
    if (On) {
        Pause(NULL);
    }
    else {
        if (!Recording()) Continue(NULL);
    }
}


bool cStatusMarkAd::Start(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime, const bool timerVPS, const bool direct) {
    if ((direct) && (Get(FileName) != -1)) return false;

    char *autoLogoOption = NULL;
    if (setup->autoLogoConf >= 0) {
        if(! asprintf(&autoLogoOption," --autologo=%i ", setup->autoLogoConf)) {
            esyslog("markad: asprintf ouf of memory");
            return false;
        }
        ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
    }
    else {
        if (setup->autoLogoMenue > 0) {
            if(! asprintf(&autoLogoOption, " --autologo=%i ", setup->autoLogoMenue)) {
                esyslog("markad: asprintf ouf of memory");
                return false;
            }
            ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
        }
    }
    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
                                   bindir,
                                   setup->Verbose ? " -v " : "",
                                   setup->SaveInfo ? " -I " : "",
                                   setup->GenIndex ? " -G " : "",
                                   setup->OSDMessage ? " -O --svdrpport=6419 " : "",
                                   setup->NoMargins ? " -i 4 " : "",
                                   setup->SecondPass ? "" : " --pass1only ",
                                   setup->Log2Rec ? " -R " : "",
                                   setup->LogLevel ? setup->LogLevel : "",
                                   setup->aStopOffs ? setup->aStopOffs : "",
                                   setup->useVPS ? " --vps " : "",
                                   setup->MarkadCut ? " --cut " : "",
                                   setup->ac3ReEncode ? " --ac3reencode " : "",
                                   autoLogoOption ? autoLogoOption : "",
                                   logodir,
                                   direct ? "-O after" : "--online=2 before",
                                   FileName);
    FREE(strlen(autoLogoOption)+1, "autoLogoOption");
    free(autoLogoOption);

    usleep(1000000); // wait 1 second
    if (SystemExec(cmd) != -1) {
        dsyslog("markad: executing %s", *cmd);
        usleep(200000);
        int pos=Add(FileName, Name, eventID, timerStartTime, timerStopTime, timerVPS);
        if (getPid(pos) && getStatus(pos)) {
            if (setup->ProcessDuring == PROCESS_AFTER) {
                if (!direct) {
                    if (!setup->whileRecording) Pause(NULL);
                    else Pause(FileName);
                }
                else {
                    if (!setup->whileRecording && Recording()) Pause(FileName);
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


void cStatusMarkAd::GetEventID(const cDevice *Device, const char *Name, tEventID *eventID, time_t *timerStartTime, time_t *timerStopTime, bool *timerVPS) {
    if (!Name) return;
    if (!Device) return;
    if (!eventID) return;
    if (!timerStartTime) return;
    if (!timerStopTime) return;
    *timerStartTime = 0;
    *timerStopTime = 0;
    *eventID = 0;
    int timeDiff = INT_MAX;

#if APIVERSNUM>=20301
    const cTimer *timer = NULL;
    cStateKey StateKey;
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
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
        StateKey.Remove();
#endif
        return;
    }
    *timerStartTime = timer->StartTime();
    *timerStopTime = timer->StopTime();

    if (!timer->Event()) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer for %s has no event", Name);
    }
    else {
        *eventID = timer->Event()->EventID();
    }
#if APIVERSNUM>=20301
    StateKey.Remove();
#endif
    dsyslog("markad: cStatusMarkAd::GetEventID(): eventID %u from event for recording <%s> timer <%s>  timer start %ld stop %ld", *eventID, Name, timer->File(), *timerStartTime, *timerStopTime);
    if (timer->HasFlags(tfVps)) {
        dsyslog("markad: cStatusMarkAd::GetEventID(): timer <%s> uses VPS", timer->File());
        *timerVPS = true;
    }
    return;
}


void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On) {
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir) return; // we cannot operate without bindir
    if (!logodir) return; // we dont want to operate without logodir

    if (On) {
        dsyslog("markad: cStatusMarkAd::Recording(): recording <%s> [%s] started", Name, FileName);
        tEventID eventID = 0;
        time_t timerStartTime = 0;
        time_t timerStopTime = 0;
        bool timerVPS = false;
        GetEventID(Device, Name, &eventID, &timerStartTime, &timerStopTime, &timerVPS);
        SaveVPSTimer(FileName, timerVPS);

        if ((setup->ProcessDuring == PROCESS_NEVER) && setup->useVPS) {  // markad start disabled per config menue, add recording for VPS detection
            int pos=Add(FileName, Name, eventID, timerStartTime, timerStopTime, timerVPS);
            dsyslog("markad: cStatusMarkAd::Recording(): added recording <%s> event ID %u at position %i only for VPS detection", Name, eventID, pos);
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
        if (!Start(FileName, Name, eventID, timerStartTime, timerStopTime, timerVPS, false)) {
            esyslog("markad: failed starting on <%s>", FileName);
        }
    }
    else {
        dsyslog("markad: cStatusMarkAd::Recording(): recording <%s> [%s] stopped", Name, FileName);
        int pos = Get(FileName, Name);
        if ((setup->ProcessDuring == PROCESS_DURING) && ( pos >= 0)) {
            Remove(pos, false);
        }
        if (setup->ProcessDuring == PROCESS_NEVER) {
        dsyslog("markad: deactivated by user");
        return; // markad deactivated
        }

        if (setup->ProcessDuring == PROCESS_AFTER) {
            if (!setup->whileRecording) {
                if (!setup->whileReplaying) {
                    if (!Recording() && !Replaying()) Continue(NULL);
                }
                else {
                    if (!Recording()) Continue(NULL);
                }
            }
            else {
                Continue(FileName);
            }
        }
    }
}


bool cStatusMarkAd::LogoExists(const cDevice *Device, const char *FileName) {
    if (!FileName) return false;
    if (!Device) return false;
    char *cname = NULL;
#if APIVERSNUM>=20301
    const cTimer *timer = NULL;
    cStateKey StateKey;
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
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
        ret=fscanf(fstat, "%*10d %*255s %c", &recs[Position].Status);
        fclose(fstat);
    }
    else {
        if (errno == ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove filename from list
            Remove(Position);
        }
    }
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


bool cStatusMarkAd::GetNextActive(struct recs **RecEntry) {
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
    struct recs *tmpRecs = NULL;
    ResetActPos();
    while (GetNextActive(&tmpRecs)) ;
}


bool cStatusMarkAd::MarkAdRunning() {
    struct recs *tmpRecs = NULL;
    ResetActPos();
    GetNextActive(&tmpRecs);
    return (tmpRecs != NULL);
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
    if (recs[pos].FileName) {
        dsyslog("markad: cStatusMarkAd::Remove(): remove %s from position %i", recs[pos].FileName, pos);
        if (recs[pos].runningStatus == 4) isyslog("markad: got no VPS stop event for %s", recs[pos].FileName);
        FREE(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
        free(recs[pos].FileName);
    }

    recs[pos].FileName = NULL;
    if (recs[pos].Name) {
        FREE(strlen(recs[pos].Name)+1, "recs[pos].Name");
        free(recs[pos].Name);
        recs[pos].Name = NULL;
    }
    if ((Kill) && (recs[pos].Pid)) {
        if (getStatus(pos)) {
            if ((recs[pos].Status == 'R') || (recs[pos].Status == 'S')) {
                dsyslog("markad: cStatusMarkAd::Remove(): terminating pid %i", recs[pos].Pid);
                kill(recs[pos].Pid, SIGTERM);
            }
            else {
                dsyslog("markad: cStatusMarkAd::Remove(): killing pid %i", recs[pos].Pid);
                kill(recs[pos].Pid, SIGKILL);
            }
        }
    }
    recs[pos].Status = 0;
    recs[pos].Pid = 0;
    recs[pos].ChangedbyUser = false;
    recs[pos].eventID = 0;
    recs[pos].eitEventID = 0;
    recs[pos].timerStartTime = 0;
    recs[pos].timerStopTime = 0;
    recs[pos].runningStatus = 0;
    recs[pos].recStart = {0};
    recs[pos].vpsStartTime = 0;
    recs[pos].vpsStopTime = 0;
    recs[pos].vpsPauseStartTime = 0;
    recs[pos].vpsPauseStopTime = 0;
    recs[pos].timerVPS = false;
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


int cStatusMarkAd::Add(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime, bool timerVPS) {
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
            recs[pos].Status = 0;
            recs[pos].Pid = 0;
            recs[pos].ChangedbyUser = false;
            recs[pos].eventID = eventID;
            recs[pos].eitEventID = 0;
            recs[pos].timerStartTime = timerStartTime;
            recs[pos].timerStopTime = timerStopTime;
            recs[pos].runningStatus = 0;
            recs[pos].recStart = time(NULL);
            recs[pos].vpsStartTime = 0;
            recs[pos].vpsStopTime = 0;
            recs[pos].vpsPauseStartTime = 0;
            recs[pos].vpsPauseStopTime = 0;
            recs[pos].timerVPS = timerVPS;
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
            if ((recs[pos].epgEventLog) && (asprintf(&log, "position %i, eventID %7u, start %s, stop %s", pos, eventID, timerStart, timerStop) != -1)) recs[pos].epgEventLog->Log(log);
            if (log) free(log);

            return pos;
        }
    }
    return -1;
}


void cStatusMarkAd::Pause(const char *FileName) {
    for (int i = 0; i < (MAXDEVICES * MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: pausing pid %i", recs[i].Pid);
                kill(recs[i].Pid, SIGTSTP);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: pausing pid %i", recs[i].Pid);
                kill(recs[i].Pid, SIGTSTP);
            }
        }
    }
}


void cStatusMarkAd::Continue(const char *FileName) {
    for (int i = 0; i < (MAXDEVICES*MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser) ) {
                dsyslog("markad: resume pid %i", recs[i].Pid);
                kill(recs[i].Pid, SIGCONT);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: resume pid %i", recs[i].Pid);
                kill(recs[i].Pid, SIGCONT);
            }
        }
    }
}
