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
#include <tinyxml2.h>
#include "status.h"
#include "setup.h"
#include "debug.h"


cEpgEventLog::cEpgEventLog(const char *recDir) {
    if (!recDir) return;
    char *eventLogName = NULL;

    if (!asprintf(&eventLogName, "%s/%s", recDir, "vps.log")) {
        esyslog("markad: cEpgEventLog::cEpgEventLog(): VPS event logfile asprintf failed");
        return;
    }
    ALLOC(strlen(eventLogName), "eventLogName");

    eventLogFile = fopen(eventLogName,"w");
    if (!eventLogFile) {
        esyslog("markad: eventLogFile(): VPS event logfile <%s> open file failed", eventLogName);
    }
    FREE(strlen(eventLogName), "eventLogName");
    free(eventLogName);
    fprintf(eventLogFile, "         time        EventID      now     next    EIT  state event new  offset  action\n");
}


cEpgEventLog::~cEpgEventLog() {
    fclose(eventLogFile);
}


void cEpgEventLog::Log(const time_t recStart, const tEventID recEventID, const tEventID eventID, const tEventID followingEventID, const tEventID eitEventID, const int state, const int event, const int newState, const char* action) {
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
    if (!asprintf(&message, "%s  %7u  %7u  %7u  %5u    %i     %i   % 1i  %02i:%02i:%02i %s", timeVPS, recEventID, eventID, followingEventID, eitEventID, state, event, newState, h, m, s, action)) {
        esyslog("markad: cEpgEventLog::Log(): asprintf failed");
        return;
    }
    if (message) {
        ALLOC(strlen(message), "message");
        fprintf(eventLogFile,"%s\n", message);
        FREE(strlen(message), "message");
        free(message);
        fflush (eventLogFile);
    }
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
        if (recs[i].eitEventID == 0) {  // we do not know the EIT Event ID, with epg2vdr it is different from eventID
//            dsyslog("markad:  recording <%s> with event %u schedule eventID %u start %i stop %i, EIT EventID %u start %i stop %i", recs[i].Name, recs[i].eventID, eventID, recs[i].timerStartTime, recs[i].timerStopTime, eitEventID, EitEvent->getStartTime(), EitEvent->getStartTime() + EitEvent->getDuration());
            if ((recs[i].eventID != eventID) && (recs[i].eventID != followingEventID)) continue;
//            dsyslog("markad:  recording <%s> with event %u schedule eventID %u start %i stop %i, EIT EventID %u start %i stop %i", recs[i].Name, recs[i].eventID, eventID, recs[i].timerStartTime, recs[i].timerStopTime, eitEventID, EitEvent->getStartTime(), EitEvent->getStartTime() + EitEvent->getDuration());
            if (!((EitEvent->getStartTime() >= recs[i].timerStartTime) && ((EitEvent->getStartTime() + EitEvent->getDuration()) <= recs[i].timerStopTime))) continue;
            recs[i].eitEventID = eitEventID;
            dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> found EIT Event ID %u", recs[i].Name, eitEventID);
        }
        if (recs[i].eitEventID != eitEventID) continue;

        int runningStatus = EitEvent->getRunningStatus();
        if (recs[i].runningStatus == runningStatus) return;
//        dsyslog("markad: accept recording <%s> eventID %u EitEventId() %u timerStartTime %u timerStopTime %i EitEventStartTime() %i EitEventEndTime %i", recs[i].Name, eventID, EitEvent->getEventId(), recs[i].timerStartTime, recs[i].timerStopTime, EitEvent->getStartTime(), EitEvent->getStartTime()+EitEvent->getDuration());
        if (recs[i].runningStatus < 0 ) return;
        if ((recs[i].runningStatus == 0) && (runningStatus == 1)) { // VPS event not running
            dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got VPS event 'not running', status changed from 0 to 1", recs[i].Name);
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "accept");
            recs[i].runningStatus = 1;
            return;
        }
        if ((recs[i].runningStatus <= 1) && (runningStatus == 2)) { // VPS event start expected in a few seconds
            dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got VPS event 'starts in a few seconds', status changed from %i to 2", recs[i].Name, recs[i].runningStatus);
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "accept");
            recs[i].runningStatus = 2;
            return;
        }

        if ((recs[i].runningStatus == 4) && (runningStatus == 2)) {
            dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got VPS event 'starts in a few seconds' at status 'running', ignoring event", recs[i].Name);
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            return;
        }
        if ((recs[i].runningStatus == 2) && (runningStatus == 1)) {
            dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got VPS event 'not running' at status 'starts in a few seconds', ignoring event", recs[i].Name);
            if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            return;
        }

        if (((recs[i].runningStatus == 1) || (recs[i].runningStatus == 2)) && (runningStatus == 4)) { // VPS start
            if (recs[i].vpsStartTime && recs[i].vpsStopTime) {
                dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got VPS event 'running' at status %i after we had start and stop events, ignoring event", recs[i].Name, recs[i].runningStatus);
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            }
            else {
                if (StoreVPSStatus("START", i)) {
                    if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "start");
                    recs[i].runningStatus = 4;
                }
            }
            return;
        }
        if ((recs[i].runningStatus == 4) && (runningStatus == 1)) {  // VPS stop
            if (StoreVPSStatus("STOP", i)) {
                SaveVPSStatus(i);
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "stop");
                recs[i].runningStatus = 1;
            }
            else {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, recs[i].runningStatus, "ignore");
            }
            return;
        }
        if ((recs[i].runningStatus == 4) && (runningStatus == 3)) { // VPS pause start
            if (StoreVPSStatus("PAUSE_START", i)) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "pause start");
                recs[i].runningStatus = 3;
            }
            return;
        }
        if ((recs[i].runningStatus == 3) && (runningStatus == 4)) { // VPS pause stop
            if (StoreVPSStatus("PAUSE_STOP", i)) {
                if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, runningStatus, "pause stop");
                recs[i].runningStatus = 4;
            }
            return;
        }
        dsyslog("markad: cStatusMarkAd::SetVPSStatus(): recording <%s> got ununexpected VPS event %i at status %i, ignoring this and future events", recs[i].Name, runningStatus, recs[i].runningStatus);
        if (recs[i].epgEventLog) recs[i].epgEventLog->Log(recs[i].recStart, recs[i].eventID, eventID, followingEventID, eitEventID, recs[i].runningStatus, runningStatus, -1, "invalid");
        recs[i].runningStatus = -1;
        recs[i].vpsValid = false;
        return;
    }
    return;
}


void cStatusMarkAd::SaveVPSStatus(const int index) {
    if ((index < 0) || (index >= MAXDEVICES*MAXRECEIVERS)) {
        dsyslog("markad: cStatusMarkAd::SaveVPSStatus(): index %i out of range",index);
    }
    if (!recs[index].vpsValid) {
        dsyslog("markad: cStatusMarkAd::SaveVPSStatus(): VPS Infos not valid for recording <%s>",recs[index].Name);
        return;
    }
    if (!recs[index].vpsStartTime)  {
        dsyslog("markad: cStatusMarkAd::SaveVPSStatus(): no VPS start time to save for recording <%s>",recs[index].Name);
        return;
    }
    if (!recs[index].vpsStopTime)  {
        dsyslog("markad: cStatusMarkAd::SaveVPSStatus(): no VPS stop time to save for recording <%s>",recs[index].Name);
        return;
    }

    int offset;
    char timeVPSchar[20] = {0};
    struct tm timeVPStm;
    char *fileVPS = NULL;

    if (!asprintf(&fileVPS, "%s/%s", recs[index].FileName, "markad.vps")) {
        esyslog("markad: cStatusMarkAd::SaveVPSStatus(): recording <%s> asprintf failed", recs[index].Name);
        return;
    }
    ALLOC(strlen(fileVPS), "fileVPS");

    FILE *pFile = fopen(fileVPS,"w");
    if (!pFile) {
        esyslog("markad: cStatusMarkAd::SaveVPSStatus(): recording <%s> open file %s failed", recs[index].Name, fileVPS);
        FREE(strlen(fileVPS), "fileVPS");
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
    FREE(strlen(fileVPS), "fileVPS");
    free(fileVPS);
    return;
}


bool cStatusMarkAd::StoreVPSStatus(const char *status, int index) {
    if (!status) return false;
    if ((index < 0) || (index >= MAXDEVICES*MAXRECEIVERS)) {
        dsyslog("markad: cStatusMarkAd::StoreVPSStatus(): index %i out of range",index);
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
        recs[index].vpsPauseStartTime=curr_time;
        return true;
    }
    if (strcmp(status,"PAUSE_STOP") == 0) {
        recs[index].vpsPauseStopTime=curr_time;
        return true;
    }
    if (strcmp(status,"STOP") == 0) {
        if ( curr_time >  recs[index].vpsStartTime + 60) {
            recs[index].vpsStopTime=curr_time;
            return true;
        }
        else {
           dsyslog("markad: cStatusMarkAd::StoreVPSStatus(): VPS stop to fast after start for recording %s, ignoring",  recs[index].Name);
           return false;
        }
    }
    dsyslog("cStatusMarkAd::StoreVPSStatus(): unknown state %s", status);
    return false;
}


cStatusMarkAd::cStatusMarkAd(const char *BinDir, const char *LogoDir, struct setup *Setup) {
    setup=Setup;
    bindir=BinDir;
    logodir=LogoDir;
    actpos=0;
    memset(&recs,0,sizeof(recs));
}


cStatusMarkAd::~cStatusMarkAd() {
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++) {
        Remove(i,true);
    }
}


int cStatusMarkAd::Recording() {
    int cnt=0;
    for (int i=0; i<cDevice::NumDevices(); i++) {
        cDevice *dev=cDevice::GetDevice(i);
        if (dev) {
            if (dev->Receiving()) cnt++;
        }
    }
    return cnt;
}


bool cStatusMarkAd::Replaying() {
    for (int i=0; i<cDevice::NumDevices(); i++) {
        cDevice *dev=cDevice::GetDevice(i);
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


bool cStatusMarkAd::Start(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime, const bool Direct) {
    if ((Direct) && (Get(FileName)!=-1)) return false;

    char *autoLogoOption = NULL;
    if (setup->autoLogoConf >= 0) {
        if(! asprintf(&autoLogoOption," --autologo=%i ",setup->autoLogoConf)) {
            esyslog("markad: asprintf ouf of memory");
            return false;
        }
        ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
    }
    else {
        if (setup->autoLogoMenue > 0) {
            if(! asprintf(&autoLogoOption," --autologo=%i ",setup->autoLogoMenue)) {
                esyslog("markad: asprintf ouf of memory");
                return false;
            }
            ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
        }
    }
    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s%s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
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
                                   setup->cDecoder ? " --cDecoder " : "",
                                   setup->useVPS ? " --vps " : "",
                                   setup->MarkadCut ? " --cut " : "",
                                   setup->ac3ReEncode ? " --ac3reencode " : "",
                                   autoLogoOption ? autoLogoOption : "",
                                   logodir,
                                   Direct ? "-O after" : "--online=2 before",
                                   FileName);
    FREE(strlen(autoLogoOption)+1, "autoLogoOption");
    free(autoLogoOption);

    usleep(1000000); // wait 1 second
    if (SystemExec(cmd)!=-1) {
        dsyslog("markad: executing %s",*cmd);
        usleep(200000);
        int pos=Add(FileName, Name, eventID, timerStartTime, timerStopTime);
        if (getPid(pos) && getStatus(pos)) {
            if (setup->ProcessDuring == PROCESS_AFTER) {
                if (!Direct) {
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
    if (Change!=tcDel) return;
    if (setup->ProcessDuring == PROCESS_NEVER) return;
    if (time(NULL)>=Timer->StopTime()) return; // don't react on normal VDR timer deletion after recording
    Remove(Timer->File(),true);
}


void cStatusMarkAd::GetEventID(const cDevice *Device, const char *Name, tEventID *eventID, time_t *timerStartTime, time_t *timerStopTime) {
    if (!Name) return;
    const cTimer *timer=NULL;
    *timerStartTime = 0;
    *timerStopTime = 0;
    *eventID = 0;
    cStateKey StateKey;
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
        for (const cTimer *Timer=Timers->First(); Timer; Timer=Timers->Next(Timer)) {
            if (Timer->Recording() && const_cast<cDevice *>(Device)->IsTunedToTransponder(Timer->Channel())) {
                timer=Timer;
                break;
            }
        }
    }
    if (!timer) {
        esyslog("markad: cannot find timer for <%s>",Name);
        StateKey.Remove();
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
    StateKey.Remove();
    dsyslog("markad: cStatusMarkAd::GetEventID(): eventID %u from event for timer <%s>  timer start %ld stop %ld", *eventID, Name, *timerStartTime, *timerStopTime);
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
        GetEventID(Device, Name, &eventID, &timerStartTime, &timerStopTime);

       if ((setup->ProcessDuring == PROCESS_NEVER) && setup->useVPS) {  // markad start disabled per config menue, add recording for VPS detection
            int pos=Add(FileName, Name, eventID, timerStartTime, timerStopTime);
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

        if (!autoLogo && setup->LogoOnly && !LogoExists(Device,FileName)) {   // from v2.0.0 we will find the logo in the recording
            isyslog("markad: no logo found for %s",Name);
            return;
        }

        // Start markad with recording
        if (!Start(FileName, Name, eventID, timerStartTime, timerStopTime, false)) {
            esyslog("markad: failed starting on <%s>",FileName);
        }
    }
    else {
        dsyslog("markad: cStatusMarkAd::Recording(): recording <%s> [%s] stopped", Name, FileName);
        int pos = Get(FileName, Name);
        if ((setup->ProcessDuring == PROCESS_NEVER) && ( pos >= 0)) {
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


bool cStatusMarkAd::LogoExists(const cDevice *Device,const char *FileName) {
    if (!FileName) return false;
    char *cname=NULL;
#if APIVERSNUM>=20301
    const cTimer *timer=NULL;
    cStateKey StateKey;
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
        for (const cTimer *Timer=Timers->First(); Timer; Timer=Timers->Next(Timer))
#else
    cTimer *timer=NULL;
    for (cTimer *Timer = Timers.First(); Timer; Timer=Timers.Next(Timer))
#endif
        {
            if (Timer->Recording() && const_cast<cDevice *>(Device)->IsTunedToTransponder(Timer->Channel()))
                if (difftime(time(NULL),Timer->StartTime())<60) {
                    timer=Timer;
                    break;
                }
                else esyslog("markad: recording start is later than timer start, ignoring");
        }

        if (!timer) {
            esyslog("markad: cannot find timer for '%s'",FileName);
        }
        else {
            const cChannel *chan=timer->Channel();
            if (chan) {
                cname=strdup(chan->Name());
                ALLOC(strlen(cname)+1, "cname");
            }
        }

#if APIVERSNUM>=20301
        StateKey.Remove();
    }
#endif

    if (!timer) return false;
    if (!cname) return false;

    for (int i=0; i<(int) strlen(cname); i++) {
        if (cname[i]==' ') cname[i]='_';
        if (cname[i]=='.') cname[i]='_';
        if (cname[i]=='/') cname[i]='_';
    }

    char *fname=NULL;
    if (asprintf(&fname,"%s/%s-A16_9-P0.pgm",logodir,cname)==-1) {
        FREE(strlen(cname)+1, "cname");
        free(cname);
        return false;
    }
    ALLOC(strlen(fname)+1, "fname");

    struct stat statbuf;
    if (stat(fname,&statbuf)==-1) {
        FREE(strlen(fname)+1, "fname");
        free(fname);
        fname=NULL;
        if (asprintf(&fname,"%s/%s-A4_3-P0.pgm",logodir,cname)==-1) {
            FREE(strlen(cname)+1, "cname");
            free(cname);
            return false;
        }
        ALLOC(strlen(fname)+1, "fname");

        if (stat(fname,&statbuf)==-1) {
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
    if (Position<0) return false;
    if (!recs[Position].Pid) return false;
    int ret=0;
    char procname[256]="";
    snprintf(procname,sizeof(procname),"/proc/%i/stat",recs[Position].Pid);
    FILE *fstat=fopen(procname,"r");
    if (fstat) {
        // found a running markad
        ret=fscanf(fstat,"%*10d %*255s %c",&recs[Position].Status);
        fclose(fstat);
    }
    else {
        if (errno==ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove filename from list
            Remove(Position);
        }
    }
    return (ret==1);
}


bool cStatusMarkAd::getPid(int Position) {
    if (Position<0) return false;
    if (!recs[Position].FileName) return false;
    if (recs[Position].Pid) return true;
    int ret=0;
    char *buf;
    if (asprintf(&buf,"%s/markad.pid",recs[Position].FileName)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    usleep(500*1000);   // wait 500ms to give markad time to create pid file
    FILE *fpid=fopen(buf,"r");
    if (fpid) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        int pid;
        ret=fscanf(fpid,"%10i\n",&pid);
        if (ret==1) recs[Position].Pid=pid;
        fclose(fpid);
    }
    else {
        esyslog("markad: failed to open pid file %s with errno %i", buf, errno);
        if (errno==ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove entry from list
            Remove(Position);
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    return (ret==1);
}


bool cStatusMarkAd::GetNextActive(struct recs **RecEntry) {
    if (!RecEntry) return false;
    *RecEntry=NULL;

    if (actpos>=(MAXDEVICES*MAXRECEIVERS)) return false;

    do {
        if ((recs[actpos].FileName) && (recs[actpos].Pid)) {
            if (getStatus(actpos)) {
                /* check if recording directory still exists */
                if (access(recs[actpos].FileName,R_OK)==-1) {
                    Remove(actpos,true);
                } else {
                    *RecEntry=&recs[actpos++];
                    return true;
                }
            }
        }
        actpos++;
    }
    while (actpos<(MAXDEVICES*MAXRECEIVERS));

    return false;
}


void cStatusMarkAd::Check() {
    struct recs *tmpRecs=NULL;
    ResetActPos();
    while (GetNextActive(&tmpRecs)) ;
}


bool cStatusMarkAd::MarkAdRunning() {
    struct recs *tmpRecs=NULL;
    ResetActPos();
    GetNextActive(&tmpRecs);
    return (tmpRecs!=NULL);
}


int cStatusMarkAd::Get(const char *FileName, const char *Name) {
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++) {
        if (Name && recs[i].Name && !strcmp(recs[i].Name,Name)) return i;
        if (FileName && recs[i].FileName && !strcmp(recs[i].FileName,FileName)) return i;
    }
    return -1;
}


void cStatusMarkAd::Remove(const char *Name, bool Kill) {
    if (!Name) return;
    int pos=Get(NULL,Name);
    if (pos==-1) return;
    Remove(pos,Kill);
}


void cStatusMarkAd::Remove(int pos, bool Kill) {
    if (recs[pos].FileName) {
        dsyslog("markad: cStatusMarkAd::Remove(): remove %s from position %i", recs[pos].FileName, pos);
        if (recs[pos].runningStatus == 4) isyslog("markad: got no VPS stop event for %s", recs[pos].FileName);
        FREE(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
        free(recs[pos].FileName);
    }

    recs[pos].FileName=NULL;
    if (recs[pos].Name) {
        FREE(strlen(recs[pos].Name)+1, "recs[pos].Name");
        free(recs[pos].Name);
        recs[pos].Name=NULL;
    }
    if ((Kill) && (recs[pos].Pid)) {
        if (getStatus(pos)) {
            if ((recs[pos].Status=='R') || (recs[pos].Status=='S')) {
                dsyslog("markad: cStatusMarkAd::Remove(): terminating pid %i",recs[pos].Pid);
                kill(recs[pos].Pid,SIGTERM);
            }
            else {
                dsyslog("markad: cStatusMarkAd::Remove(): killing pid %i",recs[pos].Pid);
                kill(recs[pos].Pid,SIGKILL);
            }
        }
    }
    recs[pos].Status=0;
    recs[pos].Pid=0;
    recs[pos].ChangedbyUser=false;
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
    recs[pos].vpsValid = true;
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


int cStatusMarkAd::Add(const char *FileName, const char *Name, const tEventID eventID, const time_t timerStartTime, const time_t timerStopTime) {
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
            recs[pos].vpsValid = true;
            if (setup->useVPS && setup->logVPS) {
                recs[pos].epgEventLog = new cEpgEventLog(FileName);
                ALLOC(sizeof(*(recs[pos].epgEventLog)), "recs[pos].epgEventLog");
            }
            if (!epgHandlerMarkad && setup->useVPS) {
                epgHandlerMarkad = new cEpgHandlerMarkad(this);     // VDR will free at stop
                dsyslog("markad: cStatusMarkAd::Add():: create epg event handler");
            }
            if (pos > max_recs) max_recs = pos;
            dsyslog("markad: cStatusMarkAd::Add(): add recording <%s> to running list at position %i, eventID %u, start %ld, stop %ld", Name, pos, eventID, timerStartTime, timerStopTime);
            return pos;
        }
    }
    return -1;
}


void cStatusMarkAd::Pause(const char *FileName) {
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: pausing pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGTSTP);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: pausing pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGTSTP);
            }
        }
    }
}


void cStatusMarkAd::Continue(const char *FileName) {
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++) {
        if (FileName) {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid) && (!recs[i].ChangedbyUser) ) {
                dsyslog("markad: resume pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGCONT);
            }
        }
        else {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser)) {
                dsyslog("markad: resume pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGCONT);
            }
        }
    }
}
