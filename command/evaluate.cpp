/*
 * evaluate.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "evaluate.h"
#include "logo.h"



bool cEvaluateChannel::IsInfoLogoChannel(char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "kabel_eins") != 0) &&
        (strcmp(channelName, "DMAX") != 0) &&
        (strcmp(channelName, "SIXX") != 0) &&
        (strcmp(channelName, "SAT_1") != 0) &&
        (strcmp(channelName, "RTL2") != 0)) {
       return false;
    }
    return true;
}


bool cEvaluateChannel::IsLogoChangeChannel(char *channelName) {
    // for performance reason only known and tested channels
    if (strcmp(channelName, "TELE_5") != 0) {  // has logo changes
        return false;
    }
    return true;
}


bool cEvaluateChannel::ClosingCreditChannel(char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "kabel_eins") != 0) &&
        (strcmp(channelName, "SAT_1") != 0) &&
        (strcmp(channelName, "SIXX") != 0) &&
        (strcmp(channelName, "Pro7_MAXX") != 0) &&
        (strcmp(channelName, "ProSieben") != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::AdInFrameWithLogoChannel(char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "SIXX") != 0) &&
        (strcmp(channelName, "RTL2") != 0) &&
        (strcmp(channelName, "VOX") != 0) &&
        (strcmp(channelName, "RTL_Television") != 0) &&
        (strcmp(channelName, "kabel_eins") != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::IntroductionLogoChannel(char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "kabel_eins") != 0) &&
        (strcmp(channelName, "RTL2") != 0)) {
        return false;
    }
    return true;
}


// evaluate logo stop/start pairs
// used by logo change detection
cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(sMarkAdContext *maContext, cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART, const int iStopA) {
    if (!marks) return;

#define LOGO_CHANGE_NEXT_STOP_MIN   7  // in s, do not increase, 7s is the shortest found distance between two logo changes
                                       // next stop max (=lenght next valid broadcast) found: 1242


#define LOGO_CHANGE_IS_ADVERTISING_MIN 300  // in s
#define LOGO_CHANGE_IS_BROADCAST_MIN 240  // in s

    dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): start with iStart %d, chkSTART %d, iStopA %d", iStart, chkSTART, iStopA);
    sLogoStopStartPair newPair;

    cMark *mark = marks->GetFirst();
    while (mark) {
        if (mark->type == MT_LOGOSTOP) newPair.stopPosition = mark->position;
        if ((mark->type == MT_LOGOSTART) && (newPair.stopPosition >= 0)) {
            newPair.startPosition = mark->position;
            logoPairVector.push_back(newPair);
            ALLOC(sizeof(sLogoStopStartPair), "logoPairVector");
            // reset for next pair
            newPair.stopPosition = -1;
            newPair.startPosition = -1;
        }
        mark = mark->Next();
    }

// evaluate stop/start pairs
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        //  evaluate logo section
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): -----------------------------------------------------------------------------------------");
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): stop (%d) start (%d) pair", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        // check for info logo section
        if (IsInfoLogoChannel(maContext->Info.ChannelName)) IsInfoLogo(blackMarks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond);
        else logoPairIterator->isInfoLogo = STATUS_NO;

        // check for logo change section
        if (IsLogoChangeChannel(maContext->Info.ChannelName)) IsLogoChange(marks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond, iStart, chkSTART);
        else logoPairIterator->isLogoChange = STATUS_NO;

        // mark after pair
        cMark *markStop_AfterPair = marks->GetNext(logoPairIterator->stopPosition, MT_LOGOSTOP);
        int deltaStopStart = (logoPairIterator->startPosition - logoPairIterator->stopPosition ) / maContext->Video.Info.framesPerSecond;
        if (deltaStopStart >= LOGO_CHANGE_IS_ADVERTISING_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: delta %ds (expect >=%ds) is a advertising", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_IS_ADVERTISING_MIN);
            logoPairIterator->isAdvertising = 1;
        }

        // check next stop distance after stop/start pair
        int delta_Stop_AfterPair = 0;
        if (markStop_AfterPair) {  // we have a next logo stop
            delta_Stop_AfterPair = (markStop_AfterPair->position - logoPairIterator->startPosition) / maContext->Video.Info.framesPerSecond;
        }
        else {  // this is the last logo stop we have
            if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
                int diff = (chkSTART - logoPairIterator->stopPosition) / maContext->Video.Info.framesPerSecond; // difference to current processed frame
                if (diff > LOGO_CHANGE_IS_BROADCAST_MIN) delta_Stop_AfterPair = diff;     // still no stop mark but we are in broadcast
                else delta_Stop_AfterPair = LOGO_CHANGE_NEXT_STOP_MIN; // we can not ignore early stop start pairs because they can be logo changed short after start
            }
        }
        if (delta_Stop_AfterPair >= LOGO_CHANGE_IS_BROADCAST_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: next stop mark after stop/start pair in %ds (expect >=%ds, start mark is in braoscast)", logoPairIterator->stopPosition, logoPairIterator->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_IS_BROADCAST_MIN);
            logoPairIterator->isStartMarkInBroadcast = 1;
        }
    }

    // check section of stop/start pairs
    // search for part between advertising and broadcast, keep this mark, because it contains the start mark of the broadcast
    //
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->isAdvertising == 1) {  // advertising pair
            std::vector<sLogoStopStartPair>::iterator nextLogoPairIterator = logoPairIterator;
            ++nextLogoPairIterator;
            if (nextLogoPairIterator != logoPairVector.end()) {
                if ((nextLogoPairIterator->isLogoChange == 0) && (nextLogoPairIterator->isStartMarkInBroadcast  == 0)){ // unknown pair
                    std::vector<sLogoStopStartPair>::iterator next2LogoPairIterator = nextLogoPairIterator;
                    ++next2LogoPairIterator;
                    if (next2LogoPairIterator != logoPairVector.end()) {
                        if (next2LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", nextLogoPairIterator->stopPosition, nextLogoPairIterator->startPosition);
                            nextLogoPairIterator->isLogoChange = STATUS_NO;
                        }
                        if ((next2LogoPairIterator->isLogoChange == 0) && (next2LogoPairIterator->isStartMarkInBroadcast  == 0)) { // unknown pair
                            std::vector<sLogoStopStartPair>::iterator next3LogoPairIterator = next2LogoPairIterator;
                            ++next3LogoPairIterator;
                            if (next3LogoPairIterator != logoPairVector.end()) {
                                if (next3LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                                    dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", next2LogoPairIterator->stopPosition, next2LogoPairIterator->startPosition);
                                    next2LogoPairIterator->isLogoChange = STATUS_NO;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): add stop (%d) start (%d) pair:", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isLogoChange           %2d", logoPairIterator->isLogoChange);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isAdvertising          %2d", logoPairIterator->isAdvertising);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isStartMarkInBroadcast %2d", logoPairIterator->isStartMarkInBroadcast);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isInfoLogo             %2d", logoPairIterator->isInfoLogo);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isClosingCredits       %2d", logoPairIterator->isClosingCredits);
    }
    nextLogoPairIterator = logoPairVector.begin();
}


cEvaluateLogoStopStartPair::~cEvaluateLogoStopStartPair() {
#ifdef DEBUG_MEM
    int size =  logoPairVector.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sLogoStopStartPair), "logoPairVector");
    }
#endif
     logoPairVector.clear();
}


// check if stop/start pair could be a logo change
//
void cEvaluateLogoStopStartPair::IsLogoChange(cMarks *marks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond, const int iStart, const int chkSTART) {
#define LOGO_CHANGE_STOP_START_MIN  6760  // min time in ms of a logo change section, chaned from 10000 to 9400 to 6760
#define LOGO_CHANGE_STOP_START_MAX 21000  // max time in ms of a logo change section
    // check min length of stop/start logo pair
    int deltaStopStart = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition ) / framesPerSecond;
    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: min length %dms (expect >=%dms <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_STOP_START_MIN, LOGO_CHANGE_STOP_START_MAX);
    if (deltaStopStart < LOGO_CHANGE_STOP_START_MIN) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ----- stop (%d) start (%d) pair: delta too small %dms (expect >=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_STOP_START_MIN);
        // maybe we have a wrong start/stop pait inbetween, try next start mark
        cMark *markNextStart = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTART);
        if (markNextStart) {
            int deltaStopStartNew = 1000 * (markNextStart->position - logoStopStartPair->stopPosition ) / framesPerSecond;
            if (deltaStopStartNew > LOGO_CHANGE_STOP_START_MAX) {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange(): next start mark (%d) too far away",  markNextStart->position);
            }
            else {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange(): replace start mark with next start mark (%d) new delta is now %dms",  markNextStart->position, deltaStopStartNew);
                logoStopStartPair->startPosition = markNextStart->position;
                deltaStopStart = deltaStopStartNew;
            }
        }
        else {
            logoStopStartPair->isLogoChange = STATUS_NO;
            return;
        }
    }

    // check max length of stop/start logo pair
    if (deltaStopStart > LOGO_CHANGE_STOP_START_MAX) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():          ----- stop (%d) start (%d) pair: delta too big %dms (expect <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_STOP_START_MAX);
        logoStopStartPair->isLogoChange = STATUS_NO;
        return;
    }

    // check next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = (markStop_AfterPair->position - logoStopStartPair->startPosition) / framesPerSecond;
    }
    else {  // this is the last logo stop we have
        if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
            int diff = (chkSTART - logoStopStartPair->stopPosition) / framesPerSecond; // difference to current processed frame
            if (diff > LOGO_CHANGE_IS_BROADCAST_MIN) delta_Stop_AfterPair = diff;     // still no stop mark but we are in broadcast
            else delta_Stop_AfterPair = LOGO_CHANGE_NEXT_STOP_MIN; // we can not ignore early stop start pairs because they can be logo changed short after start
        }
    }
    if ((delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair < LOGO_CHANGE_NEXT_STOP_MIN)) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ----- stop (%d) start (%d) pair: stop mark after stop/start pair too fast %ds (expect >=%ds)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_NEXT_STOP_MIN);
        logoStopStartPair->isLogoChange = STATUS_NO;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         +++++ stop (%d) start (%d) pair: can be a logo change", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
}


// check if stop/start pair could be a info logo
//
void cEvaluateLogoStopStartPair::IsInfoLogo(cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond) {
    if (framesPerSecond <= 0) return;
#define LOGO_INFO_STOP_START_MIN 4480  // min time in ms of a info logo section, bigger values than in InfoLogo becase of seek to iFrame, changed from 5000 to 4480
#define LOGO_INFO_STOP_START_MAX 17000  // max time in ms of a info logo section
#define LOGO_INFO_BLACKSCREEN_BEFORE_DIFF_MAX 40  // max time in ms no blackscreen allowed before stop mark
    int length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / framesPerSecond;
    if ((length <= LOGO_INFO_STOP_START_MAX) && (length >= LOGO_INFO_STOP_START_MIN)) {

        // check blackscreen before stop/start
        // if direct before logo stop is a blackscreen mark stop/start pair, this logo stop is a valid stop mark
        cMark *blackStop = blackMarks->GetPrev(logoStopStartPair->stopPosition + 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
        cMark *blackStart = blackMarks->GetPrev(logoStopStartPair->stopPosition + 1, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
        if ( blackStop && blackStart) {
            int diff = 1000 * (logoStopStartPair->stopPosition - blackStart->position) / framesPerSecond;
            int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen before (%d) and (%d) length %dms, diff %dms (expect <=%dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff, LOGO_INFO_BLACKSCREEN_BEFORE_DIFF_MAX);
            if ((lengthBlack >= 1080) && (diff <= LOGO_INFO_BLACKSCREEN_BEFORE_DIFF_MAX)) { // changed from 1700 to 1080
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                logoStopStartPair->isInfoLogo = STATUS_NO;
                return;
            }
        }
        // check blackscreen in between stop/start
        blackStop = blackMarks->GetNext(logoStopStartPair->stopPosition, MT_NOBLACKSTOP);
        blackStart = NULL;
        if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART);
        if (blackStop && blackStart && (blackStop->position <= logoStopStartPair->startPosition) && (blackStart->position <= logoStopStartPair->startPosition)) {
            int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: in between blacksceen (%d) and (%d) length %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack);
            if (lengthBlack > 1240) {  // changed from 400 to 1240
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():      ----- stop (%d) start (%d) pair: in between blacksceen pair long, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                logoStopStartPair->isInfoLogo = STATUS_NO;
                return;
            }
        }
        // check blackscreen around stop
        blackStop = blackMarks->GetPrev(logoStopStartPair->stopPosition + 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
        blackStart = NULL;
        if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
        if (blackStop && blackStart && (blackStart->position >= logoStopStartPair->stopPosition) && (blackStart->position <= logoStopStartPair->startPosition)) {
            int diff = 1000 * (blackStart->position - logoStopStartPair->startPosition) / framesPerSecond;
            int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen around stop (%d) and (%d) length %dms, diff %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
            if ((lengthBlack > 1200) && (diff < 1200)) {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                logoStopStartPair->isInfoLogo = STATUS_NO;
                return;
            }
        }
        // check blackscreen around start
        blackStop = blackMarks->GetPrev(logoStopStartPair->startPosition + 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
        blackStart = NULL;
        if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
        if (blackStop && blackStart && (blackStart->position >= logoStopStartPair->startPosition) && (blackStop->position >= logoStopStartPair->stopPosition)) {
            int diff = 1000 * (blackStart->position - logoStopStartPair->startPosition) / framesPerSecond;
            int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen around start (%d) and (%d) length %dms, diff %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
            if ((lengthBlack > 1200) && (diff < 1200)) {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                logoStopStartPair->isInfoLogo = STATUS_NO;
                return;
            }
        }
        // check blackscreen after stop/start
        // if direct after logo start is a blackscreen mark stop/start pair, this logo stop is a valid stop mark with closing credits after
        blackStop = blackMarks->GetNext(logoStopStartPair->startPosition - 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
        blackStart = blackMarks->GetNext(logoStopStartPair->startPosition - 1, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
        if (blackStop && blackStart) {
            int diff = 1000 * (blackStart->position - logoStopStartPair->startPosition) / framesPerSecond;
            int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen after (%d) and (%d) length %dms, diff %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
            if ((lengthBlack > 2000) && (diff < 10)) {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ---- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                logoStopStartPair->isInfoLogo = STATUS_NO;
                return;
            }
        }

        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           +++++ stop (%d) start (%d) pair: possible info logo section found, length  %ds (expect >=%ds and <=%ds)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_STOP_START_MIN, LOGO_INFO_STOP_START_MAX);
        logoStopStartPair->isInfoLogo = 0;
        return;
    }
    else {
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: no info logo section, length  %dms (expect >=%dms and <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_STOP_START_MIN, LOGO_INFO_STOP_START_MAX);
    }
    logoStopStartPair->isInfoLogo = STATUS_NO;
    return;
}


bool cEvaluateLogoStopStartPair::GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo) {
    if (!stopPosition) return false;
    if (!startPosition) return false;
    if (!isLogoChange) return false;
    if (!isInfoLogo) return false;
    if (nextLogoPairIterator == logoPairVector.end()) return false;

    while ((nextLogoPairIterator->isLogoChange == STATUS_NO) && (nextLogoPairIterator->isInfoLogo == STATUS_NO)) {
        ++nextLogoPairIterator;
        if (nextLogoPairIterator == logoPairVector.end()) return false;
    }
    *stopPosition = nextLogoPairIterator->stopPosition;
    *startPosition = nextLogoPairIterator->startPosition;
    *isLogoChange = nextLogoPairIterator->isLogoChange;
    *isInfoLogo = nextLogoPairIterator->isInfoLogo;
    ++nextLogoPairIterator;
    return true;
}


void cEvaluateLogoStopStartPair::SetIsInfoLogo(const int stopPosition, const int startPosition) {
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if ((logoPairIterator->stopPosition == stopPosition) && (logoPairIterator->startPosition == startPosition)) {
            dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): set isInfoLogo for stop (%d) start (%d) pair", logoPairIterator->stopPosition, logoPairIterator->startPosition);
            logoPairIterator->isLogoChange = STATUS_YES;
            return;
        }
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
}


// set isClosingCredits to STAUS_YES
// stopPosition / startPosition do not need exact match, they must be inbetween stop/start pair
void cEvaluateLogoStopStartPair::SetIsClosingCredits(const int stopPosition, const int startPosition) {
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if ((logoPairIterator->stopPosition <= stopPosition) && (logoPairIterator->startPosition >= startPosition)) {
            dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): set isClosingCredits for stop (%d) start (%d) pair", logoPairIterator->stopPosition, logoPairIterator->startPosition);
            logoPairIterator->isClosingCredits = STATUS_YES;
            return;
        }
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
}


bool cEvaluateLogoStopStartPair::GetIsClosingCredits(const int startPosition) {
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->startPosition == startPosition) {
            dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): isClosingCredits for start (%d) mark: %d", logoPairIterator->startPosition, logoPairIterator->isClosingCredits);
            if (logoPairIterator->isClosingCredits == STATUS_YES) return true;
            else return false;
        }
    }
    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): start (%d) mark not found", startPosition);
    return false;
}


bool cEvaluateLogoStopStartPair::IncludesInfoLogo(const int stopPosition, const int startPosition) {
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if ((logoPairIterator->stopPosition >= stopPosition) && (logoPairIterator->startPosition <= startPosition)) {
            dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): stop %d start %d pair includes info logo for stop (%d) start (%d) pair", stopPosition, startPosition, logoPairIterator->stopPosition, logoPairIterator->startPosition);
            return true;
        }
    }
    return false;
}



cDetectLogoStopStart::cDetectLogoStopStart(sMarkAdContext *maContextParam, cDecoder *ptr_cDecoderParam, cIndex *recordingIndexParam, cEvaluateLogoStopStartPair *evaluateLogoStopStartPairParam) {
    maContext = maContextParam;
    ptr_cDecoder = ptr_cDecoderParam;
    recordingIndex = recordingIndexParam;
    evaluateLogoStopStartPair = evaluateLogoStopStartPairParam;
}


cDetectLogoStopStart::~cDetectLogoStopStart() {
#ifdef DEBUG_MEM
    int size = compareResult.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sCompareInfo), "compareResult");
    }
#endif
    compareResult.clear();
}


bool cDetectLogoStopStart::Detect(int startFrame, int endFrame, const bool adInFrame) {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;
    if (!recordingIndex) return false;
    if (startFrame >= endFrame) return false;

    if (!compareResult.empty()) {  // reset compare result
#ifdef DEBUG_MEM
        int size = compareResult.size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(sCompareInfo), "compareResult");
        }
#endif
        compareResult.clear();
    }
    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);

    // check if we have anything todo with this channel
    if (!IsInfoLogoChannel(maContext->Info.ChannelName) && !IsLogoChangeChannel(maContext->Info.ChannelName) && !ClosingCreditChannel(maContext->Info.ChannelName)
                                                        && !AdInFrameWithLogoChannel(maContext->Info.ChannelName) && !IntroductionLogoChannel(maContext->Info.ChannelName)) {
        dsyslog("cDetectLogoStopStart::Detect(): channel not in list for special logo detection");
        return false;
    }

    sMarkAdContext maContextSaveState = {};
    maContextSaveState.Video = maContext->Video;     // save state of calling video context
    maContextSaveState.Audio = maContext->Audio;     // save state of calling audio context


    bool status = true;
    startPos = recordingIndex->GetIFrameAfter(startFrame);
    endPos = recordingIndex->GetIFrameBefore(endFrame);
    dsyslog("cDetectLogoStopStart::Detect(): detect from iFrame (%d) to iFrame (%d)", startPos, endPos);

    cMarkAdLogo *ptr_Logo = new cMarkAdLogo(maContext, recordingIndex);
    ALLOC(sizeof(*ptr_Logo), "ptr_Logo");
    sAreaT *area = ptr_Logo->GetArea();

    cExtractLogo *ptr_cExtractLogo = new cExtractLogo(maContext, maContext->Video.Info.AspectRatio, recordingIndex);
    ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");

    sLogoInfo *logo1[CORNERS];
    sLogoInfo *logo2[CORNERS];
    for (int corner = 0; corner < CORNERS; corner++) {
        logo1[corner] = new sLogoInfo;
        ALLOC(sizeof(*logo1[corner]), "logo");
    }

    int logoHeight = 0;
    int logoWidth  = 0;
    ptr_cExtractLogo->GetLogoSize(maContext, &logoHeight, &logoWidth);
    if (adInFrame) { // do check for frame
        logoWidth *= 0.32;   // less width to ignore content in frame
        dsyslog("cDetectLogoStopStart::Detect(): use logo size %dWx%dH", logoWidth, logoHeight);
        ptr_Logo->SetLogoSize(logoWidth, logoHeight);
    }
    if (!ptr_cDecoder->SeekToFrame(maContext, startPos)) {
        dsyslog("cDetectLogoStopStart::Detect(): SeekToFrame (%d) failed", startPos);
        status = false;
    }
    while (status && (ptr_cDecoder->GetFrameNumber() < endPos)) {
        if (!ptr_cDecoder->GetNextPacket()) {
            dsyslog("cDetectLogoStopStart::Detect(): GetNextPacket() failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            status = false;
        }
        int frameNumber =  ptr_cDecoder->GetFrameNumber();
        if (!ptr_cDecoder->IsVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(maContext, false)) {
            if (ptr_cDecoder->IsVideoIFrame()) // if we have interlaced video this is expected, we have to read the next half picture
                tsyslog("cDetectLogoStopStart::Detect(): GetFrameInfo() failed at frame (%d)", frameNumber);
                continue;
        }
        sCompareInfo compareInfo;
        if (!maContext->Video.Data.valid) {
            dsyslog("cDetectLogoStopStart::Detect(): faild to get video data of i-frame (%d)", frameNumber);
            continue;
        }
        for (int corner = 0; corner < CORNERS; corner++) {
            area->corner = corner;
            int iFrameNumberNext = -1;  // flag for detect logo: -1: called by cExtractLogo, dont analyse, only fill area
                                            //                       -2: called by cExtractLogo, dont analyse, only fill area, store logos in /tmp for debug
#ifdef DEBUG_COMPARE_FRAME_RANGE
            if (corner == DEBUG_COMPARE_FRAME_RANGE) iFrameNumberNext = -2;
#endif
            ptr_Logo->Detect(0, frameNumber, &iFrameNumberNext);  // we do not take care if we detect the logo, we only fill the area
            logo2[corner] = new sLogoInfo;
            ALLOC(sizeof(*logo2[corner]), "logo");
            logo2[corner]->iFrameNumber = frameNumber;

            // alloc memory and copy sobel transformed corner picture
            logo2[corner]->sobel = new uchar*[PLANES];
            for (int plane = 0; plane < PLANES; plane++) {
                logo2[corner]->sobel[plane] = new uchar[maxLogoPixel];
                memcpy(logo2[corner]->sobel[plane], area->sobel[plane], sizeof(uchar) * maxLogoPixel);
            }
            ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "logo[corner]->sobel");

#define RATE_0_MIN     250
#define RATE_12_MIN    950
            if (logo1[corner]->iFrameNumber >= 0) {  // we have a logo pair
                if (ptr_cExtractLogo->CompareLogoPair(logo1[corner], logo2[corner], logoHeight, logoWidth, corner, RATE_0_MIN, RATE_12_MIN, &compareInfo.rate[corner])) {
                }
            }
            if (corner == 0) {  // set current frame numbers, needed only once
                compareInfo.frameNumber1 = logo1[corner]->iFrameNumber;
                compareInfo.frameNumber2 = logo2[corner]->iFrameNumber;
            }

            // free memory
            if (logo1[corner]->sobel) {  // at first iteration logo1[corner]->sobel is not allocated
                for (int plane = 0; plane < PLANES; plane++) {
                    delete logo1[corner]->sobel[plane];
                }
                delete logo1[corner]->sobel;
                FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "logo[corner]->sobel");
            }
            FREE(sizeof(*logo1[corner]), "logo");
            delete logo1[corner];

            logo1[corner] = logo2[corner];
        }
        if (compareInfo.frameNumber1 >= 0) {  // got valid pair
            compareResult.push_back(compareInfo);
            ALLOC((sizeof(sCompareInfo)), "compareResult");
        }
    }
    FREE(sizeof(*ptr_Logo), "ptr_Logo");
    delete ptr_Logo;

    // free memory of last logo
    for (int corner = 0; corner < CORNERS; corner++) {
        if (logo1[corner]->sobel) {
            for (int plane = 0; plane < PLANES; plane++) {
                delete logo1[corner]->sobel[plane];
            }
            delete logo1[corner]->sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "logo[corner]->sobel");
        }
        FREE(sizeof(*logo1[corner]), "logo");
        delete logo1[corner];
    }
    FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
    delete ptr_cExtractLogo;

    // restore maContext state
    maContext->Video = maContextSaveState.Video;     // restore state of calling video context
    maContext->Audio = maContextSaveState.Audio;     // restore state of calling audio context
    return true;
}



bool cDetectLogoStopStart::IsInfoLogo() {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;
    if (compareResult.empty()) return false;

    if (!IsInfoLogoChannel(maContext->Info.ChannelName)) {
       dsyslog("cDetectLogoStopStart::IsInfoLogo(): skip this channel");
       return false;
    }

    dsyslog("cDetectLogoStopStart::IsInfoLogo(): detect from (%d) to (%d)", startPos, endPos);
    struct sInfoLogo {
        int start = 0;
        int end = 0;
        int startFinal = 0;
        int endFinal = 0;
    } InfoLogo;
    bool found = false;
    int separatorFrame = -1;

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);

        int sumPixel = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            sumPixel += (*cornerResultIt).rate[corner];
        }
        if (sumPixel == 0) separatorFrame = (*cornerResultIt).frameNumber2;

        if ((*cornerResultIt).rate[maContext->Video.Logo.corner] > 210) {  // do not rededuce to prevent false positiv
            if (InfoLogo.start == 0) InfoLogo.start = (*cornerResultIt).frameNumber1;
            InfoLogo.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((InfoLogo.end - InfoLogo.start) > (InfoLogo.endFinal - InfoLogo.startFinal)) {
                InfoLogo.startFinal = InfoLogo.start;
                InfoLogo.endFinal = InfoLogo.end;
            }
            InfoLogo.start = 0;  // reset state
            InfoLogo.end = 0;
        }
    }
    if ((InfoLogo.end - InfoLogo.start) > (InfoLogo.endFinal - InfoLogo.startFinal)) {
        InfoLogo.startFinal = InfoLogo.start;
        InfoLogo.endFinal = InfoLogo.end;
    }
    // check separator image
    if (separatorFrame == endPos) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator image at end frame found, this is a valid start mark");
        found = false;
    }
    else {
        // ignore short parts at start and end, this is fade in and fade out
        int diffStart = 1000 * (InfoLogo.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
        int diffEnd = 1000 * (endPos - InfoLogo.endFinal) / maContext->Video.Info.framesPerSecond;
        int newStartPos = startPos;
        int newEndPos = endPos;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): start diff %dms, end diff %dms", diffStart, diffEnd);
        if (diffStart < 1920) newStartPos = InfoLogo.startFinal;  // do not increase
        if (diffEnd <= 1800) newEndPos = InfoLogo.endFinal;  // changed from 250 to 960 to 1440 to 1800
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): final range start (%d) end (%d)", newStartPos, newEndPos);
#define INFO_LOGO_MIN_LENGTH 3360  // changed from 4000 to 3360
#define INFO_LOGO_MAX_LENGTH 14000
#define INFO_LOGO_MIN_QUOTE 70 // changed from 80 to 72 to 70
        int quote = 100 * (InfoLogo.endFinal - InfoLogo.startFinal) / (newEndPos - newStartPos);
        int length = 1000 * (InfoLogo.endFinal - InfoLogo.startFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo: start (%d), end (%d), length %dms (expect >=%dms and <=%dms), quote %d%% (expect >= %d%%)", InfoLogo.startFinal, InfoLogo.endFinal, length, INFO_LOGO_MIN_LENGTH, INFO_LOGO_MAX_LENGTH, quote, INFO_LOGO_MIN_QUOTE);
        if ((length >= INFO_LOGO_MIN_LENGTH) && (length <= INFO_LOGO_MAX_LENGTH) && (quote >= INFO_LOGO_MIN_QUOTE)) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): found info logo");
            found = true;
        }
        else dsyslog("cDetectLogoStopStart::IsInfoLogo(): no info logo found");
    }

    // check if it is a closing credit, we may not delete this because it contains end mark
    if (found) {
        if (ClosingCredit() >= 0) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): stop/start part is closing credit, no info logo");
            found = false;
        }
    }
    else if (evaluateLogoStopStartPair) ClosingCredit(); // we have to check for closing credits anyway, because we are called in start part and need this info to select start mark
    return found;
}



// check if part of recording between frame <endPos> and start <startPos> is a logo change part of the recording
// some channels play with changes of the logo in the recording
// a logo change part has same similar frames in the logo corner and different frames in all other corner (test only with one corner)
//
// return: true  if the given part is detected as logo change part
//         false if not
//
bool cDetectLogoStopStart::IsLogoChange() {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;
    if (!recordingIndex) return false;
    if (compareResult.empty()) return false;

    if (!IsLogoChangeChannel(maContext->Info.ChannelName)) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): skip this channel");
        return false;
    }
    dsyslog("cDetectLogoStopStart::isLogoChange(): check logo change between logo stop (%d) and logo start (%d)", startPos, endPos);

    bool status = true;
    int highMatchCount = 0;  // check if we have a lot of very similar pictures in the logo corner
    int lowMatchCount = 0;   // we need at least a hight quote of low similar pictures in the logo corner, if not there is no logo
    struct previewImage {  // image at the end of a preview
        int start = 0;
        int end = 0;
        int length = 0;
    } previewImage;

    int count = 0;
    int countNoLogoInLogoCorner = 0;
    int match[CORNERS] = {0};
    int matchNoLogoCorner = 0;
    bool isSeparationImageNoPixel = false;
    bool isSeparationImageLowPixel = false;
    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): frame (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
        // calculate possible preview fixed images
        if (((*cornerResultIt).rate[0] >= 500) && ((*cornerResultIt).rate[1] >= 500) && ((*cornerResultIt).rate[2] >= 500) && ((*cornerResultIt).rate[3] >= 500)) {
            if (previewImage.start == 0) previewImage.start = (*cornerResultIt).frameNumber1;
        }
        else {
            if ((previewImage.start != 0) && (previewImage.end == 0)) previewImage.end = (*cornerResultIt).frameNumber1;
        }
        // calculate matches
        count ++;
        if ((*cornerResultIt).rate[maContext->Video.Logo.corner] > 250) {  // match on logo corner
            highMatchCount++;
        }
        if ((*cornerResultIt).rate[maContext->Video.Logo.corner] > 50) {   // match on logo corner
            lowMatchCount++;
        }
        if ((*cornerResultIt).rate[maContext->Video.Logo.corner] == 0) {   // no logo in the logo corner
            countNoLogoInLogoCorner++;
        }
        int matchPicture = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            matchPicture += (*cornerResultIt).rate[corner];
            match[corner] += (*cornerResultIt).rate[corner];
            if (corner != maContext->Video.Logo.corner) {  // all but logo corner
                matchNoLogoCorner+= (*cornerResultIt).rate[corner];
            }
        }
        if (matchPicture == -4) { // all 4 corners has no pixel
            isSeparationImageNoPixel = true; // we found a separation image at start
            dsyslog("cDetectLogoStopStart::isLogoChange(): separation image without pixel at all corners found");
        }
        if ((matchPicture <= 75) && ((*cornerResultIt).frameNumber1 >= previewImage.end) && (previewImage.end != 0)) { // all 4 corner has only a few pixel, changed from 60 to 75
            isSeparationImageLowPixel = true; // we found a separation image
            dsyslog("cDetectLogoStopStart::isLogoChange(): separation image found with low pixel count found");
        }
    }
    // log found results
    for (int corner = 0; corner < CORNERS; corner++) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): corner %-12s rate summery %5d of %2d frames", aCorner[corner], match[corner], count);
    }
    // check if there is a separation image at start or a separation image and a later preview image
    dsyslog("cDetectLogoStopStart::isLogoChange(): preview image: start (%d) end (%d)", previewImage.start, previewImage.end);
    previewImage.length = (previewImage.end - previewImage.start) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::isLogoChange(): preview image: length %ds", previewImage.length);
    if ((isSeparationImageNoPixel || ((previewImage.length >= 1) && isSeparationImageLowPixel))) {  // changed from 3 to 2 to 1
        dsyslog("cDetectLogoStopStart::isLogoChange(): there is a separation images, pair can contain a valid start mark");
        status = false;
    }
    // check match quotes
    if (status) {
        int highMatchQuote = 0;
        int lowMatchQuote  = 0;
        int noLogoQuote    = 0;
        if (count > 0) {
            highMatchQuote = 100 * highMatchCount / count;
            lowMatchQuote  = 100 * lowMatchCount  / count;
            noLogoQuote    = 100 * countNoLogoInLogoCorner / count;
        }
#define LOGO_CHANGE_LIMIT static_cast<int>((matchNoLogoCorner / 3) * 1.3)
#define LOGO_LOW_QUOTE_MIN  78 // changed from 75 to 76 to 78
#define LOGO_HIGH_QUOTE_MIN 86 // changed from 88 to 86
#define LOGO_QUOTE_NO_LOGO 19
        dsyslog("cDetectLogoStopStart::isLogoChange(): logo corner high matches %d quote %d%% (expect >=%d%%), low matches %d quote %d%% (expect >=%d%%), noLogoQuote %d (expect <=%d))", highMatchCount, highMatchQuote, LOGO_HIGH_QUOTE_MIN, lowMatchCount, lowMatchQuote, LOGO_LOW_QUOTE_MIN, noLogoQuote, LOGO_QUOTE_NO_LOGO);
        dsyslog("cDetectLogoStopStart::isLogoChange(): rate summery log corner %5d (expect >=%d), summery other corner %5d, avg other corners %d", match[maContext->Video.Logo.corner], LOGO_CHANGE_LIMIT, matchNoLogoCorner, static_cast<int>(matchNoLogoCorner / 3));
        if ((lowMatchQuote >= LOGO_LOW_QUOTE_MIN) && (noLogoQuote <= LOGO_QUOTE_NO_LOGO) &&
                  ((match[maContext->Video.Logo.corner] > LOGO_CHANGE_LIMIT) || (highMatchQuote >= LOGO_HIGH_QUOTE_MIN))) {
            dsyslog("cDetectLogoStopStart::isLogoChange(): matches over limit, logo change found");
            status = true;
        }
        else {
            dsyslog("cDetectLogoStopStart::isLogoChange(): matches under limits, no logo change");
            status = false;
        }
    }
    return status;
}



int cDetectLogoStopStart::ClosingCredit() {
    if (!maContext) return -1;

    if (!ClosingCreditChannel(maContext->Info.ChannelName)) return -1;

    dsyslog("cDetectLogoStopStart::ClosingCredit: detect from (%d) to (%d)", startPos,endPos);

#define CLOSING_CREDITS_LENGTH_MIN 9
    int minLength = ((endPos - startPos) / maContext->Video.Info.framesPerSecond) - 2;  // 2s buffer for change from closing credit to logo start
    if (minLength > CLOSING_CREDITS_LENGTH_MIN) minLength = CLOSING_CREDITS_LENGTH_MIN;
    dsyslog("cDetectLogoStopStart::ClosingCredit: min length %d", minLength);

    int closingCreditsFrame = -1;
    struct closingCredits {
        int start = 0;
        int end = 0;
    } closingCredits;

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::ClosingCredit: frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
        int similarCorners = 0;
        int noPixelCount = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner] >= 220) similarCorners++;
            if ((*cornerResultIt).rate[corner] == -1) {
                similarCorners++;
                noPixelCount++;
            }
        }
        if ((similarCorners >= 3) && (noPixelCount < CORNERS)) {  // at least 3 corners has a match, at least one corner has pixel
            if (closingCredits.start == 0) closingCredits.start = (*cornerResultIt).frameNumber1;
            closingCredits.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((closingCredits.end - closingCredits.start) >= (maContext->Video.Info.framesPerSecond * minLength)) {  // first long enough part is the closing credit
                break;
            }
            closingCredits.start = 0;
            closingCredits.end = 0;
        }
    }
    // check if it is a closing credit
    int startOffset = 1000 * (closingCredits.start - startPos) / maContext->Video.Info.framesPerSecond;
    int endOffset  = 1000 * (endPos - closingCredits.end) / maContext->Video.Info.framesPerSecond;
    int length = (closingCredits.end - closingCredits.start) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): closing credits: start (%d) end (%d), offset start %dms end %dms, length %ds",
                                                                                                          closingCredits.start, closingCredits.end, startOffset, endOffset, length);

    if ((startOffset <= 1440) && (length < 19) && // do not reduce offset
           ((length >= CLOSING_CREDITS_LENGTH_MIN) || (endOffset <= 1920))) { // if we check from info logo:
                                                                              // - we would not have the complete part, so it should go nearly to end
                                                                              // - we also should detect ad in frame
                                                                              // changed from 1440 to 1920
        dsyslog("cDetectLogoStopStart::ClosingCredit(): this is a closing credits, pair contains a valid mark");
        closingCreditsFrame = closingCredits.end;
    }
    else dsyslog("cDetectLogoStopStart::ClosingCredit(): no closing credits found");

    if (evaluateLogoStopStartPair && (closingCreditsFrame >= 0)) evaluateLogoStopStartPair->SetIsClosingCredits(startPos, endPos);
    return closingCreditsFrame;
}


// search advertising in frame with logo
// check if we have matches in 3 of 4 corners
// start search at current position, end at stopPosition
// return first/last of advertising in frame with logo
//
int cDetectLogoStopStart::AdInFrameWithLogo(const bool isStartMark) {
    if (!maContext) return -1;
    if (!ptr_cDecoder) return -1;
    if (compareResult.empty()) return -1;

// for performance reason only for known and tested channels for now
    if (!AdInFrameWithLogoChannel(maContext->Info.ChannelName)) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): skip this channel");
        return -1;
    }

    if (isStartMark) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between logo start mark (%d) and (%d)", startPos, endPos);
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between (%d) and logo stop mark at (%d)", startPos, endPos);

    struct sAdInFrame {
        int start = -1;
        int end = -1;
        int startFinal = -1;
        int endFinal = -1;
    } AdInFrame;
    int retFrame = -1;

#define START_OFFSET_MAX 4
    int isCornerLogo[CORNERS] = {0};
    int countFrames = 0;
    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
        // calculate possible advertising in frame
        int similarCornersLow  = 0;
        int similarCornersHigh = 0;
        countFrames++;
        for (int corner = 0; corner < CORNERS; corner++) {
            if (((*cornerResultIt).rate[corner] >= 140) || ((*cornerResultIt).rate[corner] == -1)) similarCornersLow++;
            if ((*cornerResultIt).rate[corner] >= 300) {
                similarCornersHigh++;
                isCornerLogo[corner]++; // check if we have more than one logo
            }
        }
        if ((similarCornersLow >= 3) && (similarCornersHigh >= 2)) {  // at least 3 corners has low match and 2 corner with high match
            if (AdInFrame.start == -1) AdInFrame.start = (*cornerResultIt).frameNumber1;
            AdInFrame.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((AdInFrame.start != -1) && (AdInFrame.end != -1)) {  // we have a new pair
                int startOffset = (AdInFrame.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
                if ((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal) ||
                    (!isStartMark && (startOffset < 1))) { // a valid ad in frame before stop mark has a start offset, drop invalid pair
                    if (!isStartMark || (((AdInFrame.start - startPos) / maContext->Video.Info.framesPerSecond) < START_OFFSET_MAX)) { // ignore pair with invalid offset
                        AdInFrame.startFinal = AdInFrame.start;
                        AdInFrame.endFinal = AdInFrame.end;
                    }
                }
                AdInFrame.start = -1;  // reset state
                AdInFrame.end = -1;
            }
        }
    }
    if ((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal)) {  // in case of ad in frame go to end position
        if (!isStartMark || (((AdInFrame.start - startPos) / maContext->Video.Info.framesPerSecond) < START_OFFSET_MAX)) {  // ignore pair with invalid start offset
            AdInFrame.startFinal = AdInFrame.start;
            AdInFrame.endFinal = AdInFrame.end;
        }
    }
    // check if we found any matching part
    if ((AdInFrame.startFinal == -1) || (AdInFrame.endFinal == -1)) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no match found");
        return -1;
    }
    // check if we have more than one logo
    int countLogo = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (corner == maContext->Video.Logo.corner) continue;
        if ((100 * isCornerLogo[corner]) > (95 * countFrames)) { // we should not have a lot of high mathes in the complete part expect logo corner
                                                                 // 57 / 59 = 0.96 is second logo
                                                                 // 64 / 67 = 0.94 is ad in frame
                                                                 // 54 / 58 = 0.93 is ad in frame
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): %d high matches of %d frames, found additional logo in corner %s", isCornerLogo[corner], countFrames, aCorner[corner]);
            countLogo++;
        }
        else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): %d high matches of %d frames, no additional logo in corner %s", isCornerLogo[corner], countFrames, aCorner[corner]);
    }
    if (countLogo > 0) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): found more than one logo, this is not a advertising in frame");
        return -1;
    }
    int startOffset = (AdInFrame.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
    int stopOffset = (endPos - AdInFrame.endFinal) / maContext->Video.Info.framesPerSecond;
    int length = 1000 * (AdInFrame.endFinal - AdInFrame.startFinal) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start offset %ds start (%d), end (%d) stop offset %ds, length %dms (expect >8280ms and <=30s)",
                                                                                                        startOffset, AdInFrame.startFinal, AdInFrame.endFinal, stopOffset, length);
    if ((length > 8280) && (length <= 30000)) { // do not reduce min to prevent false positive, do not increase to detect 10s ad in frame
                                                // minor change from 8000 to 8280
        if ((isStartMark && startOffset < START_OFFSET_MAX) ||  // an ad in frame with logo after start mark must be near start mark, changed from 5 to 4
           (!isStartMark && stopOffset  < 5)) {  // an ad in frame with logo before stop mark must be near stop mark
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): this is a advertising in frame with logo");
            if (isStartMark) retFrame = AdInFrame.endFinal;
            else retFrame = AdInFrame.startFinal;
        }
        else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): offset not valid, this is not a advertising in frame with logo");
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): length not valid, this is not a advertising in frame with logo");

    return retFrame;
}


int cDetectLogoStopStart::IntroductionLogo() {
    if (!maContext) return -1;
    if (!ptr_cDecoder) return -1;
    if (compareResult.empty()) return -1;

// for performance reason only for known and tested channels for now
    if (!IntroductionLogoChannel(maContext->Info.ChannelName)) {
        dsyslog(" cDetectLogoStopStart::IntroductionLogo(): skip this channel");
        return -1;
    }

    struct introductionLogo {
        int start = 0;
        int end = 0;
        int startFinal = 0;
        int endFinal = 0;
    } introductionLogo;
    int firstLowFrame = -1;
    int retFrame = -1;

#define INTRODUCTION_MIN_LENGTH     5     // changed from 6 to 5
#define INTRODUCTION_MAX_DIFF       4
#define INTRODUCTION_MAX_LOW_OFFSET 0.9   // factor of end position, first low match from start of the introduction logo must be before

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);

        if ((*cornerResultIt).rate[maContext->Video.Logo.corner] >= 155) {
            if (introductionLogo.start == 0) introductionLogo.start = (*cornerResultIt).frameNumber1;
            introductionLogo.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((((introductionLogo.end - introductionLogo.start) / maContext->Video.Info.framesPerSecond) >= INTRODUCTION_MIN_LENGTH) &&  // if min length reached
                                                                                    (introductionLogo.end > introductionLogo.endFinal)) {  // and later part, use this
                introductionLogo.startFinal = introductionLogo.start;
                introductionLogo.endFinal = introductionLogo.end;
            }
            introductionLogo.start = 0;  // reset state
            introductionLogo.end = 0;
        }
        if ((firstLowFrame == -1) &&  ((*cornerResultIt).rate[maContext->Video.Logo.corner] < 315)) { // we expect a low match a the start of the introduction logo part, changed from 940 to 938 to 774 to 607 to 315
            firstLowFrame = (*cornerResultIt).frameNumber2;
        }
   }
   if ((((introductionLogo.end - introductionLogo.start) / maContext->Video.Info.framesPerSecond) >= INTRODUCTION_MIN_LENGTH) &&  // if min length reached
                                                                           (introductionLogo.end > introductionLogo.endFinal)) {  // and later part, use this
        introductionLogo.startFinal = introductionLogo.start;
        introductionLogo.endFinal = introductionLogo.end;
    }
    int diff = (endPos - introductionLogo.endFinal) / maContext->Video.Info.framesPerSecond;
    int length = (introductionLogo.endFinal - introductionLogo.startFinal) / maContext->Video.Info.framesPerSecond;
    int maxLowFrame = introductionLogo.endFinal - ((introductionLogo.endFinal - introductionLogo.startFinal) * INTRODUCTION_MAX_LOW_OFFSET);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), length %ds (expect >=%ds, diff to start mark %d (expect <=%d)", introductionLogo.startFinal, introductionLogo.endFinal, length, INTRODUCTION_MIN_LENGTH, diff, INTRODUCTION_MAX_DIFF);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: first low match (%d) (expect <= %d)", firstLowFrame, maxLowFrame);
    if ((length >= INTRODUCTION_MIN_LENGTH) && (diff <= INTRODUCTION_MAX_DIFF) && (firstLowFrame <= maxLowFrame)) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): found introduction logo start at (%d)", introductionLogo.startFinal);
        retFrame = introductionLogo.startFinal;
    }
    else dsyslog("cDetectLogoStopStart::IntroductionLogo(): no introduction logo found");
    return retFrame;
}

