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
        (strcmp(channelName, "WELT") != 0) &&
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


bool cEvaluateChannel::ClosingCreditsChannel(char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "kabel_eins") != 0) &&
        (strcmp(channelName, "SAT_1") != 0) &&
        (strcmp(channelName, "SIXX") != 0) &&
        (strcmp(channelName, "DMAX") != 0) &&
        (strcmp(channelName, "Pro7_MAXX") != 0) &&
        (strcmp(channelName, "RTL2") != 0) &&
        (strcmp(channelName, "ProSieben") != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::AdInFrameWithLogoChannel(char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "kabel_eins")     != 0) &&
        (strcmp(channelName, "Pro7_MAXX")      != 0) &&
        (strcmp(channelName, "RTL2")           != 0) &&
        (strcmp(channelName, "RTLZWEI")        != 0) &&
        (strcmp(channelName, "RTL_Television") != 0) &&
        (strcmp(channelName, "SAT_1")          != 0) &&
        (strcmp(channelName, "SIXX")           != 0) &&
        (strcmp(channelName, "VOX")            != 0) &&
        (strcmp(channelName, "VOXup")          != 0) &&
        (strcmp(channelName, "WELT")           != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::IntroductionLogoChannel(char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "kabel_eins") != 0) &&
        (strcmp(channelName, "SIXX")       != 0) &&
        (strcmp(channelName, "SAT_1")      != 0) &&
        (strcmp(channelName, "RTL2")       != 0)) {
        return false;
    }
    return true;
}


cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair() {
}


// Check logo stop/start pairs
// used by logo change detection
void cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(sMarkAdContext *maContext, cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART, const int iStopA) {
    if (!marks) return;

#define LOGO_CHANGE_NEXT_STOP_MIN     7000  // in ms, do not increase, 7s is the shortest found distance between two logo changes
                                            // next stop max (=lenght next valid broadcast) found: 1242s
#define LOGO_CHANGE_IS_ADVERTISING_MIN 300  // in s
#define LOGO_CHANGE_IS_BROADCAST_MIN   240  // in s

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
            newPair.stopPosition  = -1;
//            newPair.startPosition = -1;    // no need to reset
        }
        mark = mark->Next();
    }

// evaluate stop/start pairs
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        //  evaluate logo section
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): -----------------------------------------------------------------------------------------");
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): stop (%d) start (%d) pair", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        // check for info logo section
        if (IsInfoLogoChannel(maContext->Info.ChannelName)) IsInfoLogo(marks, blackMarks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond);
        else logoPairIterator->isInfoLogo = STATUS_NO;

        // check for logo change section
        if (IsLogoChangeChannel(maContext->Info.ChannelName)) IsLogoChange(marks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond, iStart, chkSTART);
        else logoPairIterator->isLogoChange = STATUS_NO;

        // check for closing credits section
        if (ClosingCreditsChannel(maContext->Info.ChannelName)) IsClosingCredits(marks, &(*logoPairIterator));
        else logoPairIterator->isClosingCredits = STATUS_NO;

        // global informations about logo pairs
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
            std::vector<sLogoStopStartPair>::iterator next1LogoPairIterator = logoPairIterator;
            ++next1LogoPairIterator;
            if (next1LogoPairIterator != logoPairVector.end()) {
                if ((next1LogoPairIterator->isLogoChange == 0) && (next1LogoPairIterator->isStartMarkInBroadcast  == 0)){ // unknown pair
                    std::vector<sLogoStopStartPair>::iterator next2LogoPairIterator = next1LogoPairIterator;
                    ++next2LogoPairIterator;
                    if (next2LogoPairIterator != logoPairVector.end()) {
                        if (next2LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", next1LogoPairIterator->stopPosition, next1LogoPairIterator->startPosition);
                            next1LogoPairIterator->isLogoChange = STATUS_NO;
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


// check if stop/start pair could be closing credits
//
void cEvaluateLogoStopStartPair::IsClosingCredits(cMarks *marks, sLogoStopStartPair *logoStopStartPair) {
    if (!marks) return;
    if (!logoStopStartPair) return;
    cMark *bStart = marks->GetPrev(logoStopStartPair->stopPosition, MT_HBORDERCHANGE, 0xF0);
    if (bStart && (bStart->type == MT_HBORDERSTART)) {
        dsyslog("cEvaluateLogoStopStartPair::IsClosingCredits():     ----- stop (%d) start (%d) pair: hborder start (%d) before, no closing credits",
                                                                                                 logoStopStartPair->stopPosition, logoStopStartPair->startPosition, bStart->position);

        logoStopStartPair->isClosingCredits = STATUS_NO;
    }
}


// check if stop/start pair could be a logo change
//
void cEvaluateLogoStopStartPair::IsLogoChange(cMarks *marks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond, const int iStart, const int chkSTART) {
    if (framesPerSecond == 0) return;
#define LOGO_CHANGE_LENGTH_MIN  3880  // min time in ms of a logo change section, chaned from 10000 to 9400 to 6760 to 5280 to 4401 to 3880
                                      // do not reduce, we can not detect too short parts
#define LOGO_CHANGE_LENGTH_MAX 19319  // max time in ms of a logo change section, chaned from 21000 to 19319
    // check min length of stop/start logo pair
    int deltaStopStart = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition ) / framesPerSecond;
    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: length %dms (expect >=%dms <=%dms)",
                                          logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_LENGTH_MIN, LOGO_CHANGE_LENGTH_MAX);

    // calculate next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: next logo stop mark (%d) distance %dms (expect >=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, markStop_AfterPair-> position, delta_Stop_AfterPair, LOGO_CHANGE_NEXT_STOP_MIN);
    }
    else {  // this is the last logo stop we have
        if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
            int diff = 1000 * (chkSTART - logoStopStartPair->stopPosition) / framesPerSecond; // difference to current processed frame
            if (diff > LOGO_CHANGE_IS_BROADCAST_MIN) delta_Stop_AfterPair = diff;     // still no stop mark but we are in broadcast
            else delta_Stop_AfterPair = LOGO_CHANGE_NEXT_STOP_MIN; // we can not ignore early stop start pairs because they can be logo changed short after start
        }
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: no next logo stop mark found", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
    }

    // maybe we have a wrong start/stop pair between, try to merge with next pair
    if ((delta_Stop_AfterPair > 440) &&  // can not merge if we have no next logo stop mark or a very near next logo stop mark
        (delta_Stop_AfterPair < 14000) &&  // far away, result would not be valid
       ((deltaStopStart < LOGO_CHANGE_LENGTH_MIN) || (delta_Stop_AfterPair < LOGO_CHANGE_NEXT_STOP_MIN))) {  // short pair or near next pair
        // try next start mark
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         short pair or very near next start mark, try to merge with next pair");
        cMark *markNextStart = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTART);
        if (markNextStart) {
            dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         next start mark (%d) found",  markNextStart->position);
            int deltaStopStartNew = 1000 * (markNextStart->position - logoStopStartPair->stopPosition ) / framesPerSecond;
            if (deltaStopStartNew > LOGO_CHANGE_LENGTH_MAX) {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         next start mark (%d) too far away",  markNextStart->position);
            }
            else {
                markStop_AfterPair = marks->GetNext(markNextStart->position, MT_LOGOSTOP);
                if (markStop_AfterPair) {  // we have a next logo stop
                    delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / framesPerSecond;
                }
                else {  // no logo stop mark after new logo start mark
                    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange(): no stop mark found after start mark (%d) ",  markNextStart->position);
                    deltaStopStartNew = LOGO_CHANGE_LENGTH_MIN; // take it as valid
                }
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange(): replace start mark with next start mark (%d) new length %dms, new distance %dms",  markNextStart->position, deltaStopStartNew, delta_Stop_AfterPair);
                logoStopStartPair->startPosition = markNextStart->position;
                deltaStopStart = deltaStopStartNew;
            }
        }
        else {
            logoStopStartPair->isLogoChange = STATUS_NO;
            return;
        }
    }
    else dsyslog("cEvaluateLogoStopStartPair::IsLogoChange(): no valid next pair found");


    // check distance to logo start mark before
    // if length of logo change is valid, check distance to logo start mark before
    // if length of logo change is too short, may be a only short logo interuption during logo change, we can not check this, assume it as logo change
#define LOGO_CHANGE_VALID_PREV_START_MAX  6920  // chnaged from 3000 to  6920
#define LOGO_CHANGE_SHORT_PREV_START_MIN 19681  // change from 16600 to 19681
#define LOGO_CHANGE_SHORT_PREV_START_MAX 30400
    cMark *prevStart = marks->GetPrev(logoStopStartPair->stopPosition, MT_LOGOSTART);
    if (prevStart) {
        int deltaStartBefore = 1000 * (logoStopStartPair->stopPosition - prevStart->position) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: previous logo start mark (%d), distance %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, prevStart->position, deltaStartBefore);
        if (deltaStopStart >= LOGO_CHANGE_LENGTH_MIN) {  // valid length for logo change
            if (deltaStartBefore <= LOGO_CHANGE_VALID_PREV_START_MAX) {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ----- stop (%d) start (%d) pair: previous logo start mark (%d), distance %dms (expect >%dms) too short", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, prevStart->position, deltaStartBefore, LOGO_CHANGE_VALID_PREV_START_MAX);
                logoStopStartPair->isLogoChange = STATUS_NO;
                return;
            }
        }
        else { // too short logo change
            if ((deltaStartBefore >= LOGO_CHANGE_SHORT_PREV_START_MIN) && (deltaStartBefore <= LOGO_CHANGE_SHORT_PREV_START_MAX)) {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         +++++ stop (%d) start (%d) pair: short logo interuption at expected distance to logo start mark, this is a logo change", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
                deltaStopStart = LOGO_CHANGE_LENGTH_MIN; // take length as valid
            }
        }
    }

    // check min length of stop/start logo pair
    if (deltaStopStart < LOGO_CHANGE_LENGTH_MIN) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():          ----- stop (%d) start (%d) pair: length too small %dms (expect <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_LENGTH_MAX);
        logoStopStartPair->isLogoChange = STATUS_NO;
        return;
    }
    // check max length of stop/start logo pair
    if (deltaStopStart > LOGO_CHANGE_LENGTH_MAX) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():          ----- stop (%d) start (%d) pair: length too big %dms (expect <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_LENGTH_MAX);
        logoStopStartPair->isLogoChange = STATUS_NO;
        return;
    }

    // check next stop distance after stop/start pair
    if ((delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair < LOGO_CHANGE_NEXT_STOP_MIN)) {
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ----- stop (%d) start (%d) pair: stop mark after stop/start pair too fast %ds (expect >=%ds)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_NEXT_STOP_MIN);
        logoStopStartPair->isLogoChange = STATUS_NO;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         +++++ stop (%d) start (%d) pair: can be a logo change", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
}


// check if stop/start pair could be a info logo
//
void cEvaluateLogoStopStartPair::IsInfoLogo(cMarks *marks, cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond) {
    if (framesPerSecond <= 0) return;
#define LOGO_INFO_LENGTH_MIN  3720  // min time in ms of a info logo section, bigger values than in InfoLogo becase of seek to iFrame, changed from 5000 to 4480 to 3720
#define LOGO_INFO_LENGTH_MAX 17680  // max time in ms of a info logo section, changed from 17000 to 17640 to 17680
#define LOGO_INFO_SHORT_BLACKSCREEN_BEFORE_DIFF_MAX 440  // max time in ms no short blackscreen allowed before stop mark, changed from 40 to 440 to 360 to 440
                                                         // no not change, there are info logos direct after very short start logo (440ms before, length 1000ms)
#define LOGO_INFO_SHORT_BLACKSCREEN_LENGTH         1000  // length of a short blackscreen, changed from 1080 to 1000

#define LOGO_INFO_LONG_BLACKSCREEN_BEFORE_DIFF_MAX 2000  // max time in ms no long blackscreen allowed before stop mark, changed from 1920 to 1960 to 2000
#define LOGO_INFO_LONG_BLACKSCREEN_LENGTH          5000  // length of a long blackscreen
#define LOGO_INFO_BROADCAST_AFTER_MIN              1160  // min length of broadcast after info logo, changed from 4000 to 1160

#define LOGO_INFO_NEXT_STOP_MIN                    2120  // min distance of next logo stop/start pair to merge, changed from 3000 to 2120
#define LOGO_INFO_NEXT_STOP_MAX                    8080  // max distance of next logo stop/start pair to merge
                                                         // if info logo is very similar to logo, we false detect this as logo
                                                         // in this case we will have only a short logo interuption when info logo fade in/out, merge this range
                                                         // changed from 8000 to 8080
    // check length
    int length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / framesPerSecond;
    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: length %dms (expect >=%dms and <=%dms)",
                                                        logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);

    // calculate next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: next stop mark (%d) distance %dms (expect <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, markStop_AfterPair->position, delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MAX);
    }

    // maybe we have a wrong start/stop pair between, check if merge with next pair can help
    if ((length < LOGO_INFO_LENGTH_MIN) ||  // this pair is too short
       ((delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair <= LOGO_INFO_NEXT_STOP_MAX) && (length < 11800))) { // next pair is too near, do not merge big pairs
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): short pair or very near next start mark, try to merge with next pair");

        // try next logo stop/start pair
        cMark *pairNextStart = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTART);
        cMark *pairNextStop  = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTOP);
        bool tryNext= true;
        while (tryNext) {
            if (pairNextStart && pairNextStop) {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair: stop (%d) start (%d) found", pairNextStop->position, pairNextStart->position);
                // check distance to next logo stop mark after stop/start pair
                int deltaStopAfterPair = 1000 * (pairNextStop->position - logoStopStartPair->startPosition) / framesPerSecond;
                if (deltaStopAfterPair < LOGO_INFO_NEXT_STOP_MIN) {
                    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %ds too short, (expect <=%ds >=%ds), try next", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, LOGO_INFO_NEXT_STOP_MAX);
                    pairNextStart = marks->GetNext(pairNextStart->position, MT_LOGOSTART);
                    pairNextStop  = marks->GetNext(pairNextStop->position, MT_LOGOSTOP);
                }
                else {
                    if (deltaStopAfterPair > LOGO_INFO_NEXT_STOP_MAX) {
                        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %ds too big, (expect <=%ds >=%ds", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, LOGO_INFO_NEXT_STOP_MAX);
                        tryNext = false;
                    }
                    else {
                        // check length of merged pair
                        int lengthNew = 1000 * (pairNextStart->position - logoStopStartPair->stopPosition ) / framesPerSecond;
                        if ((lengthNew < LOGO_INFO_LENGTH_MIN) || (lengthNew > LOGO_INFO_LENGTH_MAX)) {
                            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): merged pair would have no valid length %dms", lengthNew);
                            tryNext = false;
                        }
                        else {
                            int lengthNext = 1000 * (pairNextStart->position - pairNextStop->position) / framesPerSecond;
                            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): length of next pair: %dms", lengthNext);
                            if ((length <= 560) && (lengthNext <= 840)) { // short invisible parts between/after previews
                                                                          // length     changed from 120 to 160 to 200 to 560
                                                                          // lengthNext changed from 160 to 840
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): this pair and next pair are very short, they are logo invisible parts after previews, do not merge");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            if (lengthNext <= 440) {  // changed from 320 to 440
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair is very short, this is the part between broadcast start and info logo, it containes a valid start mark");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            if (lengthNext > LOGO_INFO_LENGTH_MIN) {
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair is long enough to be info logo, do not merge");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            logoStopStartPair->startPosition = pairNextStart->position;
                            length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / framesPerSecond;  // length of merged pair
                            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): replace start mark with next start mark (%d), new length %dms",  pairNextStart->position, length);
                            tryNext = false;
                        }
                    }
                }
            }
            else {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): no next pair found");
                tryNext = false;
           }
        }
    }

    // check length of logo stop/start pair
    if ((length > LOGO_INFO_LENGTH_MAX) || (length < LOGO_INFO_LENGTH_MIN)) {
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: no info logo section, length %dms (expect >=%dms and <=%dms)",
                                                        logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);
        logoStopStartPair->isInfoLogo = STATUS_NO;
        return;
    }

    // check next stop mark (length of next broadcast)
    cMark *nextStop = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTOP);
    if (nextStop) {
        int diff = 1000 * (nextStop->position - logoStopStartPair->startPosition) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: next broadcast ends at (%d) length %dms",
                                                                                          logoStopStartPair->stopPosition, logoStopStartPair->startPosition, nextStop->position, diff);
        if (diff < LOGO_INFO_BROADCAST_AFTER_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: next broadcast length %dms is too short (expect >=%dms)",
                                                                                          logoStopStartPair->stopPosition, logoStopStartPair->startPosition, diff, LOGO_INFO_BROADCAST_AFTER_MIN);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
    }

    // check blackscreen before stop/start
    // if direct before logo stop is a blackscreen mark stop/start pair, this logo stop is a valid stop mark
    cMark *blackStop  = blackMarks->GetPrev(logoStopStartPair->stopPosition, MT_NOBLACKSTOP);  // stop mark, blackscreen start
    cMark *blackStart = NULL;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART);     // start mark, bleckscreen end
    if (blackStop && blackStart && (logoStopStartPair->stopPosition >= blackStart->position)) {
        int diff = 1000 * (logoStopStartPair->stopPosition - blackStart->position) / framesPerSecond;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen before (%d) and (%d) length %dms, diff %dms (expect <%dms)",
                                                                         logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position,
                                                                         lengthBlack, diff, LOGO_INFO_SHORT_BLACKSCREEN_BEFORE_DIFF_MAX);
        if ((lengthBlack >= LOGO_INFO_SHORT_BLACKSCREEN_LENGTH) && (diff <= LOGO_INFO_SHORT_BLACKSCREEN_BEFORE_DIFF_MAX)) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: short blacksceen pair found, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
        if ((lengthBlack >= LOGO_INFO_LONG_BLACKSCREEN_LENGTH) && (diff <= LOGO_INFO_LONG_BLACKSCREEN_BEFORE_DIFF_MAX)) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: long blacksceen pair found, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
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
    // check blackscreen around stop (blackscreen starts before logo stop and ends between logo stop and start)
    blackStop = blackMarks->GetPrev(logoStopStartPair->stopPosition + 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
    blackStart = NULL;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
    if (blackStop && blackStart && (blackStart->position >= logoStopStartPair->stopPosition) && (blackStart->position <= logoStopStartPair->startPosition)) {
        int diff = 1000 * (blackStart->position - logoStopStartPair->stopPosition) / framesPerSecond;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen around logo stop mark from (%d) to (%d), length %dms, end of blackscreen %dms after logo stop mark", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
        if ((lengthBlack > 680) && (lengthBlack < 4400)) {  // too long blackscreen can be opening credits
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: blacksceen around logo stop mark, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
    }
    // check blackscreen around start blackscreen start between logo stop and logo start and ends after logo start)
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
    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           +++++ stop (%d) start (%d) pair: possible info logo section found, length  %dms (expect >=%dms and <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);
    logoStopStartPair->isInfoLogo = STATUS_UNKNOWN;
    return;
}


bool cEvaluateLogoStopStartPair::GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo, const int endRange) {
    if (!stopPosition) return false;
    if (!startPosition) return false;
    if (!isLogoChange) return false;
    if (!isInfoLogo) return false;
    if (nextLogoPairIterator == logoPairVector.end()) return false;

    // skip pair if there is nothing to detect
    while ((nextLogoPairIterator->stopPosition < endRange) &&  // if we are in end range we possible need to detect colong credits
           (nextLogoPairIterator->isLogoChange == STATUS_NO) && (nextLogoPairIterator->isInfoLogo == STATUS_NO)) {
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
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition == startPosition) && (value.stopPosition == stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): set isInfoLogo for stop (%d) start (%d) pair", found->stopPosition, found->startPosition);
        found->isLogoChange = STATUS_YES;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
}


// set isClosingCredits to STAUS_YES
// stopPosition / startPosition do not need exact match, they must be inbetween stop/start pair
void cEvaluateLogoStopStartPair::SetIsClosingCredits(const int stopPosition, const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition >= startPosition) && (value.stopPosition <= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): set isClosingCredits for stop (%d) start (%d) pair", found->stopPosition, found->startPosition);
        found->isClosingCredits = STATUS_YES;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
}


int cEvaluateLogoStopStartPair::GetIsClosingCredits(const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition](sLogoStopStartPair const &value) ->bool { if (value.startPosition == startPosition) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): isClosingCredits for start (%d) mark: %d", found->startPosition, found->isClosingCredits);
        return found->isClosingCredits;
    }

    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): start (%d) mark not found", startPosition);
    return STATUS_ERROR;
}


int cEvaluateLogoStopStartPair::GetIsClosingCredits(const int stopPosition, const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition >= startPosition) && (value.stopPosition <= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): isClosingCredits for stop (%d) start (%d) pair: %d", found->stopPosition, found->startPosition, found->isClosingCredits);
        return found->isClosingCredits;
    }

    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
    return STATUS_ERROR;
}


bool cEvaluateLogoStopStartPair::IncludesInfoLogo(const int stopPosition, const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition <= startPosition) && (value.stopPosition >= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::SetIsInfoLogo(): stop %d start %d pair includes info logo for stop (%d) start (%d) pair", stopPosition, startPosition, found->stopPosition, found->startPosition);
        return true;
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


#define DETECTFRAME_MAXSEARCH 120
void cDetectLogoStopStart::FindFrameFirstPixel( const uchar *picture, int *startX, int *startY, const int width, const int height, int searchX, int searchY, const int offsetX, const int offsetY) {
    *startX = -1;
    *startY = -1;
    for (int i = 0; i < DETECTFRAME_MAXSEARCH; i++) {
        if (picture[searchY * width + searchX] == 0) {  // pixel found
            *startX = searchX;
            *startY = searchY;
            break;
        }
        searchX += offsetX;
        if ((searchX < 0) || (searchX >= width)) break;
        searchY += offsetY;
        if ((searchY < 0) || (searchY >= height)) break;
    }
}


void cDetectLogoStopStart::FindFrameStartPixel(const uchar *picture, int *startX, int *startY, const int width, const int height, const int offsetX, const int offsetY) {
    while ((*startX > 0) && (*startX < width - 1)) {
        if (picture[*startY * width + *startX + offsetX] == 0) *startX += offsetX;  // next position has pixel
        else break;
    }
    while ((*startY > 0) && (*startY < height - 1)) {
        if (picture[(*startY + offsetY) * width + *startX] == 0) *startY += offsetY;  // next position has pixel
        else break;
    }
}


void cDetectLogoStopStart::FindFrameEndPixel(const uchar *picture, int *endX, int *endY, const int startX, const int startY, const int width, const int height, const int offsetX, const int offsetY) {
    *endX = startX;
    while ((*endX >= 0) && (*endX < width)) {
        if (picture[ startY * width + *endX + offsetX] == 0) *endX += offsetX;
        else break;
    }
    *endY = startY;
    while ((*endY >= 0) && (*endY < height)) {
        if (picture[(*endY + offsetY) * width + startX] == 0) *endY += offsetY;
        else break;
    }
}


// detect frame in sobel transformed picture
// sobel transformes lines can be interrupted, try also pixel next to start point
// return portion of frame pixel in picture
int cDetectLogoStopStart::DetectFrame(__attribute__((unused)) const int frameNumber, const uchar *picture, const int width, const int height, const int corner) {
    if (!picture) return 0;
    if ((width <= DETECTFRAME_MAXSEARCH) || (height <= DETECTFRAME_MAXSEARCH)) return 0;    // we can not detect in very small pictures

    int startX  = -1;
    int startY  = -1;
    int endX    = -1;
    int endY    = -1;
    int portion =  0;

#define FRAME_MIN_PIXEL 8


    switch (corner) {
        case 0: // TOP_LEFT
            FindFrameFirstPixel(picture, &startX, &startY, width, height, 0, 0, 1, 1); // search from top left to bottom right
            if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                FindFrameStartPixel(picture, &startX, &startY, width, height, -1, -1);  // start search from first pixel, search to left and top
                FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, 1, 1);  // start search from start pixel, search to right and to buttom
                if (((endX - startX) >= FRAME_MIN_PIXEL) && ((endY - startY) >= FRAME_MIN_PIXEL)) portion = 1000 * (endX - startX) / (width - startX) + 1000 * (endY - startY) / (height - startY);
            }
#ifdef DEBUG_MARK_OPTIMIZATION
            else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from top left to buttom right", frameNumber, corner);
#endif
            if (portion < 180) {  // maybe we have a text under frame or the logo
                FindFrameFirstPixel(picture, &startX, &startY, width, height, width / 2, 0, 1, 1); // search from top mid to bottom right
                if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                    FindFrameStartPixel(picture, &startX, &startY, width, height, -1, -1);  // start search from first pixel, search to left and top
                    FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, 1, 1);  // start search from start pixel, search to right and to buttom
                    if (((endX - startX) >= FRAME_MIN_PIXEL) && ((endY - startY) >= FRAME_MIN_PIXEL)) portion = 1000 * (endX - startX) / (width - startX) + 1000 * (endY - startY) / (height - startY);
                }
#ifdef DEBUG_MARK_OPTIMIZATION
                else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from buttom right to top left", frameNumber, corner);
#endif
            }
            break;

        case 1: // TOP_RIGHT
            FindFrameFirstPixel(picture, &startX, &startY, width, height, width - 1, 0, -1, 1); // search from top right to bottom left
            if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                FindFrameStartPixel(picture, &startX, &startY, width, height, 1, -1);  // start search from first pixel, search to right and top
                FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, - 1, 1);  // start search from start pixel, search to left and to buttom
                if (((startX - endX) >= FRAME_MIN_PIXEL) && ((endY - startY) >= FRAME_MIN_PIXEL)) portion = 1000 * (startX - endX) / startX + 1000 * (endY - startY) / (height - startY);
            }
#ifdef DEBUG_MARK_OPTIMIZATION
            else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from top right to buttom left", frameNumber, corner);
#endif
            if (portion < 180) {  // maybe we have a text under frame or the logo
                FindFrameFirstPixel(picture, &startX, &startY, width, height, width / 2, 0, -1, 1); // search from top mid to bottom left
                if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                    FindFrameStartPixel(picture, &startX, &startY, width, height, 1, -1);  // start search from first pixel, search to right and top
                    FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, - 1, 1);  // start search from start pixel, search to left and to buttom
                    if (((startX - endX) >= FRAME_MIN_PIXEL) && ((endY - startY) >= FRAME_MIN_PIXEL)) portion = 1000 * (startX - endX) / startX + 1000 * (endY - startY) / (height - startY);
                }
#ifdef DEBUG_MARK_OPTIMIZATION
                else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from top mid to buttom left", frameNumber, corner);
#endif
            }
            break;

        case 2: // BOTTOM_LEFT
            FindFrameFirstPixel(picture, &startX, &startY, width, height, 0, height - 1, 1, -1); // search from buttom left to top right
            if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                FindFrameStartPixel(picture, &startX, &startY, width, height, -1, 1);  // start search from first pixel, search to left and to bottom
                FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, 1, -1);  // start search from start pixel, search to right and to top
                if (((endX - startX) >= FRAME_MIN_PIXEL) && ((startY - endY) >= FRAME_MIN_PIXEL)) portion = 1000 * (endX - startX) / (width - startX) + 1000 * (startY - endY) / startY;
            }
#ifdef DEBUG_MARK_OPTIMIZATION
            else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from buttom left to top right", frameNumber, corner);
#endif
            if (portion < 180) {  // maybe we have a text under frame
                FindFrameFirstPixel(picture, &startX, &startY, width, height, width / 2, height - 1, 1, -1); // search from bottom mid to top right
                if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                    FindFrameStartPixel(picture, &startX, &startY, width, height, -1, 1);  // start search from first pixel, search to left and to bottom
                    FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, 1, -1);  // start search from start pixel, search to right and to top
                    if (((endX - startX) >= FRAME_MIN_PIXEL) && ((startY - endY) >= FRAME_MIN_PIXEL)) portion = 1000 * (endX - startX) / (width - startX) + 1000 * (startY - endY) / startY;
                }
#ifdef DEBUG_MARK_OPTIMIZATION
                else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from buttom mid to top right", frameNumber, corner);
#endif
            }
            break;

        case 3: // BOTTOM_RIGHT
            FindFrameFirstPixel(picture, &startX, &startY, width, height, width - 1, height - 1, -1, -1); // search from buttom right to top left
            if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                FindFrameStartPixel(picture, &startX, &startY, width, height, 1, 1);  // start search from first pixel, search to right and to bottom
                FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, -1, -1);  // start search from start pixel, search to left and to top
                if (((startX - endX) >= FRAME_MIN_PIXEL) && ((startY - endY) >= FRAME_MIN_PIXEL)) portion = 1000 * (startX - endX) / startX + 1000 * (startY - endY) / startY;
            }
#ifdef DEBUG_MARK_OPTIMIZATION
            else dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from buttom right to top left", frameNumber, corner);
#endif
            // maybe we have a text or a logo under frame, try from top left
            FindFrameFirstPixel(picture, &startX, &startY, width, height, 0, 0, 1, 1); // search from top left to buttom right
            if ((startX >= 0) && (startY >= 0)) { // found a start pixel
                int portionTMP =  0;
                FindFrameStartPixel(picture, &startX, &startY, width, height, 1, 1);  // start search from first pixel, search to right and botton
                FindFrameEndPixel(picture, &endX, &endY, startX, startY, width, height, -1, -1);  // start search from start pixel, search to left and to top
                if (((startX - endX) >= FRAME_MIN_PIXEL) && ((startY - endY) >= FRAME_MIN_PIXEL)) portionTMP = 1000 * (startX - endX) / startX + 1000 * (startY - endY) / startY;
                if (portionTMP > portion) portion = portionTMP;
            }
#ifdef DEBUG_MARK_OPTIMIZATION
            dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: no start found with search from top left to buttom right", frameNumber, corner);
#endif
            break;

        default:
            return 0;
    } // case


#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: startX %3d, endX %3d, startY %3d, endY %3d, portion %3d", frameNumber, corner, startX, endX, startY, endY, portion);
#endif

    return portion;
}


bool cDetectLogoStopStart::Detect(int startFrame, int endFrame) {
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
    if (!IsInfoLogoChannel(maContext->Info.ChannelName) && !IsLogoChangeChannel(maContext->Info.ChannelName) && !ClosingCreditsChannel(maContext->Info.ChannelName)
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
    dsyslog("cDetectLogoStopStart::Detect(): detect from i-frame (%d) to i-frame (%d)", startPos, endPos);

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
    ptr_cExtractLogo->GetLogoSize(maContext, &logoHeight, &logoWidth);  // default logo size of this resolution, not real logo size, info logos are greater than real logo
    dsyslog("cDetectLogoStopStart::Detect(): use logo size %dWx%dH", logoWidth, logoHeight);

    if (!ptr_cDecoder->SeekToFrame(maContext, startPos - 1)) {  // one frame before startPos because we start loop with GetNextPacket
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
        if (!maContext->Video.Data.valid) {
            dsyslog("cDetectLogoStopStart::Detect(): faild to get video data of i-frame (%d)", frameNumber);
            continue;
        }

        sCompareInfo compareInfo;
        for (int corner = 0; corner < CORNERS; corner++) {
            area->corner = corner;
            int iFrameNumberNext = -1;  // flag for detect logo: -1: called by cExtractLogo, dont analyse, only fill area
                                        //                       -2: called by cExtractLogo, dont analyse, only fill area, store logos in /tmp for debug
#ifdef DEBUG_COMPARE_FRAME_RANGE
            if (corner == DEBUG_COMPARE_FRAME_RANGE) iFrameNumberNext = -2;
#endif
            ptr_Logo->Detect(0, frameNumber, &iFrameNumberNext);  // we do not take care if we detect the logo, we only fill the area

#ifdef DEBUG_MARK_OPTIMIZATION
            // save plane 0 of sobel transformation
            char *fileName = NULL;
            if (asprintf(&fileName,"%s/F__%07d-P0-C%1d.pgm", maContext->Config->recDir, frameNumber, corner) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                SaveSobel(fileName, area->sobel[0], logoWidth, logoHeight);
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
#endif

            compareInfo.framePortion[corner] = DetectFrame(frameNumber, area->sobel[0], logoWidth, logoHeight, corner);

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
                    delete[] logo1[corner]->sobel[plane];
                }
                delete[] logo1[corner]->sobel;
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
                delete[] logo1[corner]->sobel[plane];
            }
            delete[] logo1[corner]->sobel;
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
    return status;
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

    // start and stop frame of assumed info logo section
    struct sInfoLogo {
        int start      = 0;
        int end        = 0;
        int startFinal = 0;
        int endFinal   = 0;
    } InfoLogo;

    // start and stop frame of detected zoomed still image
    // this happens before start mark in adult warning info (e.g. SIXX)
    struct sZoomedPicture {
        int start    = -1;
        int end      = -1;
        bool ongoing = true;
    } zoomedPicture;

    bool found              = true;
    int lastSeparatorFrame  = -1;
    int countSeparatorFrame =  0;
    int lowMatchCount       =  0;
    int countFrames         =  0;
    int countDark           =  0;

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);

        int sumPixel              = 0;
        int countZero             = 0;
        int zoomedPictureCount    = 0;
        int darkCorner            = 0;

        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner] <= 0) countZero++;
            if (((*cornerResultIt).rate[corner] >= 319) || ((*cornerResultIt).rate[corner] <= 0))zoomedPictureCount++;
            sumPixel += (*cornerResultIt).rate[corner];
            if (((*cornerResultIt).rate[corner] <=   0) && (corner != maContext->Video.Logo.corner)) darkCorner++;   // if we have no match, this can be a too dark corner
        }
        // dark scene
        countFrames++;
        if (darkCorner == 3) countDark++;  // if all corners but logo corner has no match, this is a very dark scene

        if (((countZero >= 2) && (sumPixel <=  45)) || // changed from 0 to 15 to 100 to 60 to 45, too big values results in false detection of a separation image, do not increase
            ((countZero >= 3) && (sumPixel <= 132))) {
            countSeparatorFrame++;
            lastSeparatorFrame = (*cornerResultIt).frameNumber2;
        }

        // check zoomed picture
        if (zoomedPicture.ongoing && (zoomedPictureCount == 4) && (countZero < 3)) {
            if (zoomedPicture.start == -1) zoomedPicture.start = (*cornerResultIt).frameNumber1;
            zoomedPicture.end = (*cornerResultIt).frameNumber2;
        }
        else if (zoomedPicture.start >= 0) zoomedPicture.ongoing = false;

#define INFO_LOGO_MACTH_MIN 210
        if (((*cornerResultIt).rate[maContext->Video.Logo.corner] > INFO_LOGO_MACTH_MIN) || // do not rededuce to prevent false positiv
            ((*cornerResultIt).rate[maContext->Video.Logo.corner] >= 142) && (lowMatchCount == 0)) { // allow one lower match for the change from new logo to normal logo
            if ((*cornerResultIt).rate[maContext->Video.Logo.corner] <= INFO_LOGO_MACTH_MIN) lowMatchCount++;
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

    // check zoomed picture
    if ((zoomedPicture.start >= 0) && (zoomedPicture.end >= 0)) {
        int startDiff =  1000 * (zoomedPicture.start - startPos) / maContext->Video.Info.framesPerSecond;
        int endDiff   =  1000 * (endPos - zoomedPicture.end)     / maContext->Video.Info.framesPerSecond;
        if ((startDiff <= 960) && (endDiff <= 480)) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): zoomed picture found from (%d) to (%d), start offset %dms, end offset %dms",
                                                                                                                zoomedPicture.start, zoomedPicture.end, startDiff, endDiff);
            found = false;
        }
        else dsyslog("cDetectLogoStopStart::IsInfoLogo(): no zoomed picture  from (%d) to (%d), start offset %dms, end offset %dms",
                                                                                                                zoomedPicture.start, zoomedPicture.end, startDiff, endDiff);
    }

    // check separator image
    dsyslog("cDetectLogoStopStart::IsInfoLogo(): count separator frames %d", countSeparatorFrame);
    if (countSeparatorFrame >= 3) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): too much separator frames, this can not be a info logo");
        found = false;
    }
    if (found) {
        int darkQuote = 100 * countDark / countFrames;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): dark quote %d%%", darkQuote);
        if ((lastSeparatorFrame >= 0) && (darkQuote < 65)) {  // on dark scenes we can not detect separator image
                                                              // darkQuote changed from 100 to 69 to 65
            int diffSeparator = 1000 * (endPos - lastSeparatorFrame) / maContext->Video.Info.framesPerSecond;
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): last separator frame found (%d), %dms before end", lastSeparatorFrame, diffSeparator);
            if (diffSeparator <= 1440) { // changed from 480 to 1440
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator frame found, this is a valid start mark");
                found = false;
            }
        }
    }

    // check info logo
    if (found) {
        // ignore short parts at start and end, this is fade in and fade out
        int diffStart = 1000 * (InfoLogo.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
        int diffEnd = 1000 * (endPos - InfoLogo.endFinal) / maContext->Video.Info.framesPerSecond;
        int newStartPos = startPos;
        int newEndPos = endPos;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo start diff %dms, end diff %dms", diffStart, diffEnd);
        if (diffStart < 1920) newStartPos = InfoLogo.startFinal;  // do not increase
        if (diffEnd <= 1800) newEndPos = InfoLogo.endFinal;  // changed from 250 to 960 to 1440 to 1800
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): final range start (%d) end (%d)", newStartPos, newEndPos);
#define INFO_LOGO_MIN_LENGTH  2880  // changed from 4000 to 3360 to 2880
#define INFO_LOGO_MAX_LENGTH 14520  // chnaged from 14000 to 14160 to 14400 to 14520
#define INFO_LOGO_MIN_QUOTE     69  // changed from 80 to 72 to 70 to 69
        int quote = 100 * (InfoLogo.endFinal - InfoLogo.startFinal) / (newEndPos - newStartPos);
        int length = 1000 * (InfoLogo.endFinal - InfoLogo.startFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo: start (%d), end (%d), length %dms (expect >=%dms and <=%dms), quote %d%% (expect >= %d%%)", InfoLogo.startFinal, InfoLogo.endFinal, length, INFO_LOGO_MIN_LENGTH, INFO_LOGO_MAX_LENGTH, quote, INFO_LOGO_MIN_QUOTE);
        if ((length >= INFO_LOGO_MIN_LENGTH) && (length <= INFO_LOGO_MAX_LENGTH) && (quote >= INFO_LOGO_MIN_QUOTE)) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): found info logo");
        }
        else {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): no info logo found");
            found = false;
        }
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

    struct previewImage {  // image at the end of a preview
        int start  = 0;
        int end    = 0;
        int length = 0;
    } previewImage;

    struct sAdInFrame {
        int start      = -1;
        int startFinal = -1;
        int end        = -1;
        int endFinal   = -1;
    } AdInFrame;


    int highMatchCount          = 0;  // check if we have a lot of very similar pictures in the logo corner
    int lowMatchCount           = 0;   // we need at least a hight quote of low similar pictures in the logo corner, if not there is no logo
    int count                   = 0;
    int countNoLogoInLogoCorner = 0;
    int match[CORNERS]          = {0};
    int matchNoLogoCorner       = 0;
    int darkSceneStart          = -1;

    bool isSeparationImageLowPixel = false;
    bool status                    = true;

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): frame (%5d) and frame (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);

        // check for advertising in frame without logo
        int adInFrameCorner = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner] >= 651) adInFrameCorner++;
        }
        if (adInFrameCorner >= 3) { // at least 3 corner have a high match
            if (AdInFrame.start == -1) AdInFrame.start = (*cornerResultIt).frameNumber1;
            AdInFrame.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal)) {
                AdInFrame.startFinal = AdInFrame.start;
                AdInFrame.endFinal   = AdInFrame.end;
                AdInFrame.start      = -1;
                AdInFrame.end        = -1;
            }
        }

        // calculate possible preview fixed images
#define LOGO_CHANGE_STILL_MATCH_MIN 655  // chnaged from 500 to 655
        if (((*cornerResultIt).rate[0] >= LOGO_CHANGE_STILL_MATCH_MIN) && ((*cornerResultIt).rate[1] >= LOGO_CHANGE_STILL_MATCH_MIN) &&
            ((*cornerResultIt).rate[2] >= LOGO_CHANGE_STILL_MATCH_MIN) && ((*cornerResultIt).rate[3] >= LOGO_CHANGE_STILL_MATCH_MIN)) {
            if (previewImage.start == 0) previewImage.start = (*cornerResultIt).frameNumber1;
            previewImage.end = (*cornerResultIt).frameNumber2;
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

        int matchPicture          = 0;
        int matchNoLogoCornerPair = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            matchPicture += (*cornerResultIt).rate[corner];
            match[corner] += (*cornerResultIt).rate[corner];
            if (corner != maContext->Video.Logo.corner) {  // all but logo corner
                matchNoLogoCornerPair += (*cornerResultIt).rate[corner];
            }
        }
        matchNoLogoCorner += matchNoLogoCornerPair;

        // detect dark scene
        if (matchNoLogoCornerPair <= 0) {
            if (darkSceneStart == -1) darkSceneStart = (*cornerResultIt).frameNumber1;
        }
        else darkSceneStart = -1;

        // detect separator frame
        if (matchPicture <= 0) { // all 4 corners has no pixel
            dsyslog("cDetectLogoStopStart::isLogoChange(): separation image without pixel at all corners found");
            if (darkSceneStart >= 0) {
                int diffDarkScene = 1000 * ((*cornerResultIt).frameNumber1 - darkSceneStart) / maContext->Video.Info.framesPerSecond;
                dsyslog("cDetectLogoStopStart::isLogoChange(): dark scene start at (%d), distance to separator frame %dms", darkSceneStart, diffDarkScene);
                if (diffDarkScene < 2400) return false;
            }
            else return false;
        }
        if ((matchPicture <= 197) && ((*cornerResultIt).frameNumber1 >= previewImage.end) && (previewImage.end != 0)) { // all 4 corner has only a few pixel, changed from 98 to 197
            if (darkSceneStart == -1) {
                isSeparationImageLowPixel = true; // we found a separation image after preview image
                dsyslog("cDetectLogoStopStart::isLogoChange(): separation image found with low pixel count found");
            }
            else dsyslog("cDetectLogoStopStart::isLogoChange(): separation image with low pixel count after dark scene found, ignoring");
        }
    }
    // log found results
    for (int corner = 0; corner < CORNERS; corner++) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): corner %-12s rate summery %5d of %2d frames", aCorner[corner], match[corner], count);
    }

    // check if we found an advertising in frame without logo
    if ((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal)) {
        AdInFrame.startFinal = AdInFrame.start;
        AdInFrame.endFinal   = AdInFrame.end;
    }
    if (AdInFrame.startFinal >= 0) {
        int adInFrameLength = 1000 * (AdInFrame.endFinal - AdInFrame.startFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::isLogoChange(): found advertising in frame without logo from (%d) to (%d), length %dms", AdInFrame.startFinal, AdInFrame.endFinal, adInFrameLength);
        if (adInFrameLength >= 4800) {
            dsyslog("cDetectLogoStopStart::isLogoChange(): there is an advertising in frame without logo, pair contains a valid start mark");
            return false;
        }
    }

    // check if there is a separation image
#define LOGO_CHANGE_STILL_QUOTE_MIN           71  // changed from 77 to 71
#define LOGO_CHANGE_STILL_LENGTH_SHORT_MIN   960
#define LOGO_CHANGE_STILL_LENGTH_LONG_MIN  11000
    previewImage.length = 1000 * (previewImage.end - previewImage.start) / maContext->Video.Info.framesPerSecond;
    int quote = 100 * (previewImage.end - previewImage.start) / (endPos - startPos);
    dsyslog("cDetectLogoStopStart::isLogoChange(): preview image: start (%d) end (%d), length %dms (expect short >= %ds long >%dms), quote %d%% (expect >=%d%%)", previewImage.start, previewImage.end, previewImage.length, LOGO_CHANGE_STILL_LENGTH_SHORT_MIN, LOGO_CHANGE_STILL_LENGTH_LONG_MIN, quote, LOGO_CHANGE_STILL_QUOTE_MIN);
    if ((quote >= LOGO_CHANGE_STILL_QUOTE_MIN) ||                    // a big part is still image
       ((previewImage.length >= LOGO_CHANGE_STILL_LENGTH_SHORT_MIN) && isSeparationImageLowPixel) ||   // short still image, changed from 3 to 2 to 1 and a separator frame
        (previewImage.length > LOGO_CHANGE_STILL_LENGTH_LONG_MIN)) { // a long still preview image direct before broadcast start, changed from 9 to 11
                                                                     // prevent detection a still scene as separation image
        dsyslog("cDetectLogoStopStart::isLogoChange(): there is a separation images, pair can contain a valid start mark");
        return false;
    }

    // check match quotes
    int highMatchQuote = 0;
    int lowMatchQuote  = 0;
    int noLogoQuote    = 0;
    if (count > 0) {
        highMatchQuote = 100 * highMatchCount / count;
        lowMatchQuote  = 100 * lowMatchCount  / count;
        noLogoQuote    = 100 * countNoLogoInLogoCorner / count;
    }
#define LOGO_CHANGE_LIMIT static_cast<int>((matchNoLogoCorner / 3) * 1.15)  // chnaged from 1.3 to 1.15
#define LOGO_LOW_QUOTE_MIN  83 // changed from 78 to 80 to 82 to 83
#define LOGO_HIGH_QUOTE_MIN 86 // changed from 88 to 86
#define LOGO_QUOTE_NO_LOGO 19
    dsyslog("cDetectLogoStopStart::isLogoChange(): logo corner high matches %d quote %d%% (expect >=%d%%), low matches %d quote %d%% (expect >=%d%%), noLogoQuote %d (expect <=%d))", highMatchCount, highMatchQuote, LOGO_HIGH_QUOTE_MIN, lowMatchCount, lowMatchQuote, LOGO_LOW_QUOTE_MIN, noLogoQuote, LOGO_QUOTE_NO_LOGO);
    dsyslog("cDetectLogoStopStart::isLogoChange(): rate summery logo corner %5d (expect >=%d), summery other corner %5d, avg other corners %d", match[maContext->Video.Logo.corner], LOGO_CHANGE_LIMIT, matchNoLogoCorner, static_cast<int>(matchNoLogoCorner / 3));
    if ((lowMatchQuote >= LOGO_LOW_QUOTE_MIN) && (noLogoQuote <= LOGO_QUOTE_NO_LOGO) &&
              ((match[maContext->Video.Logo.corner] > LOGO_CHANGE_LIMIT) || (highMatchQuote >= LOGO_HIGH_QUOTE_MIN))) {
       dsyslog("cDetectLogoStopStart::isLogoChange(): matches over limit, logo change found");
        status = true;
    }
    else {
        dsyslog("cDetectLogoStopStart::isLogoChange(): matches under limits, no logo change");
        status = false;
    }
    return status;
}


// search for closing credits in frame without logo after broadcast end
int cDetectLogoStopStart::ClosingCredit() {
    if (!maContext) return -1;

    if (!ClosingCreditsChannel(maContext->Info.ChannelName)) return -1;
    if (evaluateLogoStopStartPair && evaluateLogoStopStartPair->GetIsClosingCredits(startPos, endPos) == STATUS_NO) {
        dsyslog("cDetectLogoStopStart::ClosingCredit(): already known no closing credits from (%d) to (%d)", startPos, endPos);
        return -1;
    }

    dsyslog("cDetectLogoStopStart::ClosingCredit(): detect from (%d) to (%d)", startPos, endPos);

#define CLOSING_CREDITS_LENGTH_MIN 6120  // changed from 9000 to 6120
                                         // because of silence deetection before closing credits detection
    int minLength = (1000 * (endPos - startPos) / maContext->Video.Info.framesPerSecond) - 2000;  // 2s buffer for change from closing credit to logo start
    if (minLength <= 1840) { // too short will result in false positive, changed from 1000 to 1840
        dsyslog("cDetectLogoStopStart::ClosingCredit(): length too short for detection");
        return -1;
    }
    if (minLength > CLOSING_CREDITS_LENGTH_MIN) minLength = CLOSING_CREDITS_LENGTH_MIN;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): min length %dms", minLength);

    int closingCreditsFrame = -1;
    struct sClosingCredits {
        int start                    = -1;
        int end                      = -1;
        int frameCount               =  0;
        int sumFramePortion[CORNERS] = {0};

    } ClosingCredits;
    int framePortion[CORNERS] = {0};

    struct sClosingImage {
        int start      = -1;
        int end        = -1;
        int startFinal = -1;
        int endFinal   = -1;
   } ClosingImage;

    int countFrames = 0;
    int countDark   = 0;

    dsyslog("cDetectLogoStopStart::ClosingCredit(():      1. frame    2. frame:   matches (frame portion of 2. frame)");
    dsyslog("cDetectLogoStopStart::ClosingCredit(():                              TOP_LEFT     TOP_RIGHT    BOTTOM_LEFT  BOTTOM_RIGHT");

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::ClosingCredit(): frame (%5d) and (%5d): %5d (%4d) %5d (%4d) %5d (%4d) %5d (%4d)", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).framePortion[0], (*cornerResultIt).rate[1], (*cornerResultIt).framePortion[1], (*cornerResultIt).rate[2], (*cornerResultIt).framePortion[2], (*cornerResultIt).rate[3], (*cornerResultIt).framePortion[3]);

        int similarCorners     = 0;
        int moreSimilarCorners = 0;
        int equalCorners       = 0;
        int noPixelCount       = 0;
        int darkCorner         = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if (((*cornerResultIt).rate[corner] >= 230) || ((*cornerResultIt).rate[corner] == -1)) similarCorners++; // prevent false positiv from static scenes, changed from 220 to 230
            if (((*cornerResultIt).rate[corner] >= 260) || ((*cornerResultIt).rate[corner] == -1)) moreSimilarCorners++;   // changed from 899 to 807 to 715 to 260
            if (((*cornerResultIt).rate[corner] >= 545) || ((*cornerResultIt).rate[corner] == -1)) equalCorners++;  // changed from 924 to 605 to 545
            if ( (*cornerResultIt).rate[corner] ==  -1) noPixelCount++;
            if (((*cornerResultIt).rate[corner] <=   0) && (corner != maContext->Video.Logo.corner)) darkCorner++;   // if we have no match, this can be a too dark corner
        }
        countFrames++;
        if (darkCorner >= 2) countDark++;  // if at least two corners but logo corner has no match, this is a very dark scene

        if ((similarCorners >= 3) && (noPixelCount < CORNERS)) {  // at least 3 corners has a match, at least one corner has pixel
            if (ClosingCredits.start == -1) ClosingCredits.start = (*cornerResultIt).frameNumber1;
            ClosingCredits.end = (*cornerResultIt).frameNumber2;
            ClosingCredits.frameCount++;
            for (int corner = 0; corner < CORNERS; corner++) ClosingCredits.sumFramePortion[corner] += framePortion[corner];
        }
        else {
            if ((ClosingCredits.end - ClosingCredits.start) >= (minLength * maContext->Video.Info.framesPerSecond / 1000)) {  // first long enough part is the closing credit
                break;
            }
            // restet state
            ClosingCredits.start      = -1;
            ClosingCredits.end        = -1;
            ClosingCredits.frameCount =  0;
            for (int corner = 0; corner < CORNERS; corner++) ClosingCredits.sumFramePortion[corner] = 0;
        }

        if ((moreSimilarCorners == 4) || (equalCorners >= 3)) {  //  all 4 corners are more similar, this must be a still image
                                                                 //  at least 3 corner are equal, these are closing credits in a different form than a frame
            if (ClosingImage.start == -1) ClosingImage.start = (*cornerResultIt).frameNumber1;
            ClosingImage.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((ClosingImage.end - ClosingImage.start) >= (ClosingImage.endFinal - ClosingImage.startFinal)) {  // store longest part
                ClosingImage.startFinal        = ClosingImage.start;
                ClosingImage.endFinal          = ClosingImage.end;
            }
            // restet state
            ClosingImage.start = -1;
            ClosingImage.end   = -1;
        }
        // store frame portion of frame 2
        for (int corner = 0; corner < CORNERS; corner++) {
            framePortion[corner] = (*cornerResultIt).framePortion[corner];
        }
    }

    // check if we have a too much dark scene to detect
    int darkQuote = 100 * countDark / countFrames;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): dark scene quote %d%%", darkQuote);
    if (darkQuote >= 95) {  // changed from 100 to 95
        dsyslog("cDetectLogoStopStart::ClosingCredit(): too much dark scene, closing credits are not dark");
        return -1;
    }

    // store longest part maybe it was last part
    if ((ClosingImage.end - ClosingImage.start) >= (ClosingImage.endFinal - ClosingImage.startFinal)) {
        ClosingImage.startFinal = ClosingImage.start;
        ClosingImage.endFinal   = ClosingImage.end;
    }

    // check if it is a closing credit
    int startOffset = 1000 * (ClosingCredits.start - startPos) / maContext->Video.Info.framesPerSecond;
    int endOffset   = 1000 * (endPos - ClosingCredits.end) / maContext->Video.Info.framesPerSecond;
    int length      = 1000 * (ClosingCredits.end - ClosingCredits.start) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): closing credits: start (%d) end (%d), offset start %dms end %dms, length %dms",
                                                                                                          ClosingCredits.start, ClosingCredits.end, startOffset, endOffset, length);

    dsyslog("cDetectLogoStopStart::ClosingCredit(): closing imge:      start (%d) end (%d)", ClosingImage.startFinal, ClosingImage.endFinal);
    if ((ClosingCredits.start > 0) && (ClosingCredits.end > 0) && // we found something
        (startOffset <= 4320) && (length < 19000) && // do not reduce start offset, if logo fade out, we got start a little too late
                                                     // startOffset increases from 1440 to 4320 because of silence detection before closing credits detection
           ((length >= CLOSING_CREDITS_LENGTH_MIN) || ((endOffset < 480) && length > 1440))) {  // if we check from info logo:
                                                                                                // - we would not have the complete part, so it should go nearly to end
                                                                                                //   and need a smaller min length
                                                                                                // - we also should detect ad in frame
                                                                                                // changed from <= 1440 to 1920 to < 1200 to 480
        dsyslog("cDetectLogoStopStart::ClosingCredit(): this is a closing credits, pair contains a valid mark");
        closingCreditsFrame = ClosingCredits.end;
    }
    else dsyslog("cDetectLogoStopStart::ClosingCredit(): no closing credits found");

    // check if we have a frame
    int maxSumFramePortion =  0;
    int frameCorner        = -1;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (ClosingCredits.sumFramePortion[corner] > maxSumFramePortion) {
            maxSumFramePortion = ClosingCredits.sumFramePortion[corner];
            frameCorner = corner;
        }
    }
    if (ClosingCredits.frameCount > 0) {
        int framePortionQuote = maxSumFramePortion / ClosingCredits.frameCount;
        if (frameCorner >= 0) dsyslog("cDetectLogoStopStart::ClosingCredit(): sum of frame portion from best corner %s: %d from %d frames, quote %d", aCorner[frameCorner], maxSumFramePortion, ClosingCredits.frameCount, framePortionQuote);
        if (framePortionQuote <= 514) {
            dsyslog("cDetectLogoStopStart::ClosingCredit(): not enought frame pixel found, closing credits not valid");
            closingCreditsFrame = -1;  // no valid ad in a frame
        }
    }

    // check if it is a still image or an ad in a different form than a frame
#define CLOSING_CREDITS_STILL_OFFSET_MAX 10559  // max offset in ms from startPos (stop mark) to begin of still image
    if (darkQuote < 66) {  // too dark to detect a still image
        if (closingCreditsFrame == -1) {
            int lengthStillImage      = 1000 *  (ClosingImage.endFinal - ClosingImage.startFinal) / maContext->Video.Info.framesPerSecond;
            int offsetStartStillImage = 1000 * (ClosingImage.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
            if (lengthStillImage > 4200) {  // changed from 0 to 4200
                dsyslog("cDetectLogoStopStart::ClosingCredit(): still image from (%d) to (%d), start offset %dms (expect <= %dms), length %dms)", ClosingImage.startFinal, ClosingImage.endFinal, offsetStartStillImage, CLOSING_CREDITS_STILL_OFFSET_MAX, lengthStillImage);
                if (offsetStartStillImage <= CLOSING_CREDITS_STILL_OFFSET_MAX) {
                    dsyslog("cDetectLogoStopStart::ClosingCredit(): still image found");
                    closingCreditsFrame = ClosingImage.endFinal;
                }
                else dsyslog("cDetectLogoStopStart::ClosingCredit(): still image too far away from stop mark");
            }
            else dsyslog("cDetectLogoStopStart::ClosingCredit(): no still image found or too short");
        }
    }
    else dsyslog("cDetectLogoStopStart::ClosingCredit(): too dark %d%% to detect a still image", darkQuote);

    if (evaluateLogoStopStartPair && (closingCreditsFrame >= 0)) evaluateLogoStopStartPair->SetIsClosingCredits(startPos, endPos);
    return closingCreditsFrame;
}


// search advertising in frame with logo
// check if we have matches in 3 of 4 corners
// start search at current position, end at stopPosition
// return first/last of advertising in frame with logo
//
int cDetectLogoStopStart::AdInFrameWithLogo(const bool isStartMark) {
    if (!maContext)            return -1;
    if (!ptr_cDecoder)         return -1;
    if (compareResult.empty()) return -1;

// for performance reason only for known and tested channels for now
    if (!AdInFrameWithLogoChannel(maContext->Info.ChannelName)) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): skip this channel");
        return -1;
    }

    if (isStartMark) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between logo start mark (%d) and (%d)", startPos, endPos);
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between (%d) and logo stop mark at (%d)", startPos, endPos);

    struct sAdInFrameType1 {
        int start                         = -1;
        int startFinal                    = -1;
        int end                           = -1;
        int endFinal                      = -1;
        int frameCount                    =  0;
        int frameCountFinal               =  0;
        int sumFramePortion[CORNERS]      = {0};
        int sumFramePortionFinal[CORNERS] = {0};
    } AdInFrameType1;

    struct sAdInFrameType2 {
        int start      = -1;
        int startFinal = -1;
        int end        = -1;
        int endFinal   = -1;
    } AdInFrameType2;

    struct sAdInFrameFinal {
        int start      = -1;
        int end        = -1;
    } AdInFrameFinal;

    struct sStillImage {
        int start      = -1;
        int startFinal = -1;
        int end        = -1;
        int endFinal   = -1;
    } StillImage;

    int framePortion[CORNERS] = {0};
    int retFrame              = -1;

#define AD_IN_FRAME_STOP_OFFSET_MAX  15360  // for false positiv info logo, changed from 5280 to 9120 to 9600
                                            // for preview direct after ad in frame changed from 9600 to 13440 to 15360
#define AD_IN_FRAME_START_OFFSET_MAX  4319  // changed from 4799 to 4319
                                            // prevent to get additional info logo as frame
    int isCornerLogo[CORNERS] = {0};
    int countFrames           =  0;
    int darkFrames            =  0;

    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo():      1. frame    2. frame:   matches (frame portion of 2. frame)");
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo():                              TOP_LEFT     TOP_RIGHT    BOTTOM_LEFT  BOTTOM_RIGHT");
    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): frame (%5d) and (%5d): %5d (%4d) %5d (%4d) %5d (%4d) %5d (%4d)", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).framePortion[0], (*cornerResultIt).rate[1], (*cornerResultIt).framePortion[1], (*cornerResultIt).rate[2], (*cornerResultIt).framePortion[2], (*cornerResultIt).rate[3], (*cornerResultIt).framePortion[3]);

        // calculate possible advertising in frame
        int similarCornersLow        = 0;
        int similarCornersHigh       = 0;
        bool isStillImage            = true;
        int noPixelCountWoLogoCorner = 0;
        int noPixelCountAllCorner    = 0;
        int darkCorner               = 0;
        countFrames++;

        for (int corner = 0; corner < CORNERS; corner++) {
            // check if we have a still image before ad in frame
            if (corner == maContext->Video.Logo.corner) {
                if ((*cornerResultIt).rate[corner] < 476) isStillImage = false; // changed from 992 to 476
            }
            else {
                if ((*cornerResultIt).rate[corner]  > 0) isStillImage = false;
                if ((*cornerResultIt).rate[corner] == 0) noPixelCountWoLogoCorner++;
                if ((*cornerResultIt).rate[corner] <= 0) darkCorner++;   // if we have no match, this can be a too dark corner
            }
            // check ad in frame
            if (((*cornerResultIt).rate[corner] >= 138) || ((*cornerResultIt).rate[corner] == -1)) similarCornersLow++;  // changed from 140 to 138
            if ((*cornerResultIt).rate[corner]  >= 310) {  // changed from 315 to 310
                similarCornersHigh++;
            }
            // check logo in corner
            if ((*cornerResultIt).rate[corner] >= 400) {  // changed from 424 to 400
                isCornerLogo[corner]++; // check if we have more than one logo, in this case there can not be a ad in frame
            }

            if ((*cornerResultIt).rate[corner] == 0) noPixelCountAllCorner++;
        }

        // check it it is a drank frame
        if (darkCorner == 3) darkFrames++;

        // check still image before ad in frame
        if (!isStartMark) {
            if (isStillImage && (noPixelCountWoLogoCorner < 3)) {  // if we have no pixel in all corner but logo corner, we have a separotor frame between very dark scene and ad in frame
                if (StillImage.start == -1) StillImage.start = (*cornerResultIt).frameNumber1;
                StillImage.end =  (*cornerResultIt).frameNumber2;
            }
            else {
                if ((StillImage.end - StillImage.start) > (StillImage.endFinal - StillImage.startFinal)) {
                    StillImage.startFinal = StillImage.start;
                    StillImage.endFinal   = StillImage.end;
                }
                StillImage.start = -1;
                StillImage.end   = -1;
            }
        }

        // check advertising in frame, we can have 2 types of frames
        // type 1: at least 3 corners has low match and 2 corner with high match (text in the frame)
        if ((similarCornersLow >= 3) && (similarCornersHigh >= 2)) {
            if (AdInFrameType1.start == -1) AdInFrameType1.start = (*cornerResultIt).frameNumber1;
            AdInFrameType1.end = (*cornerResultIt).frameNumber2;
            AdInFrameType1.frameCount++;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrameType1.sumFramePortion[corner] += framePortion[corner];
        }
        else {
            if ((AdInFrameType1.start != -1) && (AdInFrameType1.end != -1)) {  // we have a new pair
                int startOffset = 1000 * (AdInFrameType1.start - startPos) / maContext->Video.Info.framesPerSecond;
                if (((AdInFrameType1.end - AdInFrameType1.start) > (AdInFrameType1.endFinal - AdInFrameType1.startFinal)) && // new range if longer
                    ((isStartMark && (startOffset < AD_IN_FRAME_START_OFFSET_MAX)) ||  // ignore pair with invalid offset
                    (!isStartMark && (startOffset > 1000)))) { // a valid ad in frame before stop mark has a start offset, drop invalid pair
                        AdInFrameType1.startFinal      = AdInFrameType1.start;
                        AdInFrameType1.endFinal        = AdInFrameType1.end;
                        AdInFrameType1.frameCountFinal =  AdInFrameType1.frameCount;
                        for (int corner = 0; corner < CORNERS; corner++) AdInFrameType1.sumFramePortionFinal[corner] = AdInFrameType1.sumFramePortion[corner];
                }
                AdInFrameType1.start             = -1;  // reset state
                AdInFrameType1.end               = -1;
                AdInFrameType1.frameCount        =  0;
                for (int corner = 0; corner < CORNERS; corner++) AdInFrameType1.sumFramePortion[corner] = 0;
            }
        }

        // type 2: 2 corner without pixel (the frame without text) and one corner with a static picture (logo or text in frame)
        if ((similarCornersHigh >= 1) && (noPixelCountAllCorner >= 2)) {
            if (AdInFrameType2.start == -1) AdInFrameType2.start = (*cornerResultIt).frameNumber1;
            AdInFrameType2.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((AdInFrameType2.start != -1) && (AdInFrameType2.end != -1)) {  // we have a new pair
                int startOffset = 1000 * (AdInFrameType2.start - startPos) / maContext->Video.Info.framesPerSecond;
                if (((AdInFrameType2.end - AdInFrameType2.start) > (AdInFrameType2.endFinal - AdInFrameType2.startFinal)) && // new range if longer
                    ((isStartMark && (startOffset < AD_IN_FRAME_START_OFFSET_MAX)) ||  // ignore pair with invalid offset
                    (!isStartMark && (startOffset > 1000)))) { // a valid ad in frame before stop mark has a start offset, drop invalid pair
                        AdInFrameType2.startFinal = AdInFrameType2.start;
                        AdInFrameType2.endFinal   = AdInFrameType2.end;
                }
                AdInFrameType2.start = -1;  // reset state
                AdInFrameType2.end   = -1;
            }
        }
        // store frame portion of frame 2
        for (int corner = 0; corner < CORNERS; corner++) {
            framePortion[corner] = (*cornerResultIt).framePortion[corner];
        }
    }

    // check if we have a very dark scene, in this case we can not detect ad in frame
    int darkQuote = 100 * darkFrames / countFrames;
    if (darkQuote >= 86) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): scene too dark, quote %d%%, can not detect ad in frame", darkQuote);
        return -1;
    }
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): dark quote %d%% is valid for detection", darkQuote);

    // select longest found ad in frame
    // type 1
    if ((AdInFrameType1.end - AdInFrameType1.start) > (AdInFrameType1.endFinal - AdInFrameType1.startFinal)) {  // in case of ad in frame go to end position
            AdInFrameType1.startFinal = AdInFrameType1.start;
            AdInFrameType1.endFinal   = AdInFrameType1.end;
            AdInFrameType1.frameCountFinal =  AdInFrameType1.frameCount;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrameType1.sumFramePortionFinal[corner] = AdInFrameType1.sumFramePortion[corner];
    }
    if (AdInFrameType1.startFinal != -1) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame type 1: start (%d) end (%d)", AdInFrameType1.startFinal, AdInFrameType1.endFinal);
    }
    // check if we have a frame
    int maxSumFramePortion =  0;
    int frameCorner        = -1;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (corner == maContext->Video.Logo.corner) continue;  // ignore logo corner, we have a static picture in that corner
        if (AdInFrameType1.sumFramePortionFinal[corner] > maxSumFramePortion) {
            maxSumFramePortion = AdInFrameType1.sumFramePortionFinal[corner];
            frameCorner = corner;
        }
    }

    if (AdInFrameType1.frameCountFinal > 0) {
        int framePortionQuote = maxSumFramePortion / AdInFrameType1.frameCountFinal;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): sum of frame portion from best corner %s: %d from %d frames, quote %d", aCorner[frameCorner], maxSumFramePortion, AdInFrameType1.frameCountFinal, framePortionQuote);
        if (framePortionQuote < 362) {  // changed from 500 to 362
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): not enought frame pixel found, advertising in frame type 1 not valid");
            AdInFrameType1.startFinal = -1;
        }
    }

    // type 2
    if ((AdInFrameType2.end - AdInFrameType2.start) > (AdInFrameType2.endFinal - AdInFrameType2.startFinal)) {  // in case of ad in frame go to end position
            AdInFrameType2.startFinal = AdInFrameType2.start;
            AdInFrameType2.endFinal   = AdInFrameType2.end;
    }
    if (AdInFrameType2.start != -1) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame type 2: start (%d) end (%d)", AdInFrameType2.start, AdInFrameType2.end);
    }
    // select longest ad in frame of both types
    int lengthMin = 8880;  // changed from 8280 to 8880
                           // prevent to detect short second info logo (e.g. läuft THE WALKING DEAD, length 8280ms) as advertising in frame
    if ((AdInFrameType1.endFinal - AdInFrameType1.startFinal) > (AdInFrameType2.endFinal - AdInFrameType2.startFinal)) {
        AdInFrameFinal.start = AdInFrameType1.startFinal;
        AdInFrameFinal.end   = AdInFrameType1.endFinal;
    }
    else {
        AdInFrameFinal.start = AdInFrameType2.startFinal;
        AdInFrameFinal.end   = AdInFrameType2.endFinal;
        lengthMin = 9841;  // prevet to get dark scene as advertising in frame
    }

    // check if we found any matching part
    if ((AdInFrameFinal.start == -1) || (AdInFrameFinal.end == -1)) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no match found");
        return -1;
    }

    // check if we have more than one logo
    int countLogo = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        int logoQuote         = 1000 * isCornerLogo[corner] / countFrames;
        int framePortionQuote = 0;
        if (AdInFrameType1.frameCountFinal > 0) framePortionQuote = AdInFrameType1.sumFramePortionFinal[corner] / AdInFrameType1.frameCountFinal;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): corner %-12s: %2d high matches, high matches qoute %3d (of %2d frames), frame portion quote %3d (of %2d frames)", aCorner[corner], isCornerLogo[corner], logoQuote, countFrames, framePortionQuote, AdInFrameType1.frameCountFinal);
        if (corner == maContext->Video.Logo.corner) continue;  // in logo corner we expect logo
        if ((logoQuote >= 894) && (framePortionQuote <= 500)) {  // logoQuote changed from 900 to 894
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): additional logo found in corner %s", aCorner[corner]);
            countLogo++;
        }
    }
    if (countLogo > 0) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): found more than one logo, this is not a advertising in frame");
        return -1;
    }

    // check still image
    if ((StillImage.end - StillImage.start) > (StillImage.endFinal - StillImage.startFinal)) {
        StillImage.startFinal = StillImage.start;
        StillImage.endFinal   = StillImage.end;
    }
    if (StillImage.start != -1) {
        int stillImageLength = 1000 * (StillImage.endFinal -  StillImage.startFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): still image before advertising in frame from (%d) to (%d), length %dms", StillImage.startFinal, StillImage.endFinal, stillImageLength);
        if ((StillImage.endFinal == AdInFrameFinal.start) && (stillImageLength < 3000)) { // too long detected still images are invalid, changed from 30360 to 5280 to 4320 to 3000
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): move advertising in frame start (%d) to still image start (%d)", AdInFrameFinal.start, StillImage.startFinal);
            AdInFrameFinal.start = StillImage.startFinal;
        }
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no still image before advertising in frame found");

    // check advertising in frame
#define AD_IN_FRAME_LENGTH_MAX  30720  // changed from 30000 to 30720
    int startOffset = 1000 * (AdInFrameFinal.start - startPos) / maContext->Video.Info.framesPerSecond;
    int stopOffset  = 1000 * (endPos - AdInFrameFinal.end) / maContext->Video.Info.framesPerSecond;
    int length      = 1000 * (AdInFrameFinal.end - AdInFrameFinal.start) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start (%d) end (%d)", AdInFrameFinal.start, AdInFrameFinal.end);
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start offset %dms (expect <=%dms for start marks), stop offset %dms (expect <=%dms for stop mark), length %dms (expect >%dms and <=%dms)", startOffset, AD_IN_FRAME_START_OFFSET_MAX, stopOffset, AD_IN_FRAME_STOP_OFFSET_MAX, length, lengthMin, AD_IN_FRAME_LENGTH_MAX);

    if ((length > lengthMin) && (length <= AD_IN_FRAME_LENGTH_MAX)) {         // do not reduce min to prevent false positive, do not increase to detect 10s ad in frame
        if ((isStartMark && (startOffset <= AD_IN_FRAME_START_OFFSET_MAX)) ||  // an ad in frame with logo after start mark must be near start mark, changed from 5 to 4
           (!isStartMark && (stopOffset  <= AD_IN_FRAME_STOP_OFFSET_MAX))) {   // an ad in frame with logo before stop mark must be near stop mark
                                                                               // maybe we have a preview direct after ad in frame and missed stop mark
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): this is a advertising in frame with logo");
            if (isStartMark) retFrame = AdInFrameFinal.end;
            else retFrame = AdInFrameFinal.start;
        }
        else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): offset not valid, this is not a advertising in frame with logo");
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): length not valid, this is not a advertising in frame with logo");

    // write report
    if (retFrame >= 0) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): report -> %s mark, length %dms, start offset %dms, stop offset %dms", (isStartMark) ? "after start" : "before stop", length, startOffset, stopOffset);

    return retFrame;
}

// check for inroduction logo before start mark
// we should find:
// - a separator frame
// - a range of similar frames in the logo corner, but no still image
// - no separator frame after similar logo corner frames
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
        int start      = -1;
        int end        = -1;
        int startFinal = -1;
        int endFinal   = -1;
    } introductionLogo;

    struct sStillImage {
        int start      = -1;
        int end        = -1;
        int startFinal = -1;
        int endFinal   = -1;
    } stillImage;


    int retFrame             = -1;
    int separatorFrameBefore = -1;
    int separatorFrameAfter  = -1;

#define INTRODUCTION_MIN_LENGTH          4320
#define INTRODUCTION_MAX_LENGTH         10559  // changed from 26000 to 16799 to 13919 to 10559
                                               // do not increase to prevent to detect preview as introduction logo
#define INTRODUCTION_MAX_DIFF_SEPARATOR 12480  // max distance from sepatator frame to introduction logo start, changed from 480 to 2400 to 3000 to 12480
                                               // somtime broacast start without logo before intruduction logo
                                               // sometime we have a undetected info logo and separtion frame is far before
#define INTRODUCTION_MAX_DIFF_END       4319   // max distance of introduction logo end to start mark (endPos)

   for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);

        // separator frame before introduction logo
        int sumPixel        = 0;
        int countZero       = 0;
        int countLow        = 0;
        int countStillImage = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner]  <=  0) countZero++;
            if ((*cornerResultIt).rate[corner]  <= 65) countLow++;
            if (((*cornerResultIt).rate[corner] <=  0) || ((*cornerResultIt).rate[corner] >= 142)) countStillImage++; // changed from 964 to 441 to 142
            sumPixel += (*cornerResultIt).rate[corner];
        }
        // examples of separator frames before introduction logo
        // 59     0     0    -1 =  58
        // 42    20    47    65 = 174
        //  3    99    62    65 = 229
        int diffSeparator = 1000 * (endPos - (*cornerResultIt).frameNumber1) / maContext->Video.Info.framesPerSecond;
        if ((countLow >= 3) && (sumPixel <= 229) && (diffSeparator > 960)) { // new separator image before introduction logo, restart detection, changed from 14 to 58
                                                                             // ignore first separator frame near endPos (logo start mark), this can not be start of introduction logo
            separatorFrameBefore = (*cornerResultIt).frameNumber1;
            introductionLogo.start      = -1;
            introductionLogo.end        = -1;
            introductionLogo.startFinal = -1;
            introductionLogo.endFinal   = -1;
            separatorFrameAfter         = -1;
            stillImage.start            = -1;
            stillImage.startFinal       = -1;
            stillImage.end              = -1;
            stillImage.endFinal         = -1;
            continue;
        }

        // separator after introduction logo, in this case it can not be a introduction logo
        // examples of separator frames after introduction logo
        // no seperator frame
        //  0    76    11     6 =  93
        //  0    24   102     9 = 135
        //  0   540     0     0 = 540  dark scene with introduction logo
        //
        // separator frame
        //  0     0     0     0 =   0
        //  0     1    33     0 =  34
        // 52     0   114     0 = 166
        if ((separatorFrameBefore >= 0) &&
           (((countZero == 1) && (sumPixel <   93)) ||
            ((countZero == 2) && (sumPixel <= 166)) ||
            ((countZero >= 3) && (sumPixel <  540)))) {
            separatorFrameAfter = (*cornerResultIt).frameNumber1;
        }

        // detect still image
        if ((separatorFrameBefore >= 0) && (introductionLogo.start >= 0) && (countStillImage >= 3)) { // still image or closing credists after introduction logo
                                                                                                      // countStillImage: changed from 4 to 3
            if (stillImage.start == -1) stillImage.start = (*cornerResultIt).frameNumber1;
            stillImage.end = (*cornerResultIt).frameNumber2;
        }
        else {
            if ((stillImage.end - stillImage.start) >= (stillImage.endFinal - stillImage.startFinal)) {
                stillImage.startFinal = stillImage.start;
                stillImage.endFinal   = stillImage.end;
            }
            stillImage.start = -1;
            stillImage.end   = -1;
        }

        // detect introduction logo
        if (separatorFrameBefore >= 0) { // introduction logo start is after seperator frame
            if ((*cornerResultIt).rate[maContext->Video.Logo.corner] >= 162) { // changed from 155 to 162
                if (introductionLogo.start == -1) introductionLogo.start = (*cornerResultIt).frameNumber1;
                introductionLogo.end = (*cornerResultIt).frameNumber2;
            }
            else {
                if ((introductionLogo.end - introductionLogo.start) >= (introductionLogo.endFinal - introductionLogo.startFinal)) {
                    introductionLogo.startFinal = introductionLogo.start;
                    introductionLogo.endFinal   = introductionLogo.end;
                }
                introductionLogo.start = -1;
                introductionLogo.end   = -1;
            }
        }
    }

    // set final detected introduction logo
    if ((introductionLogo.end - introductionLogo.start) >= (introductionLogo.endFinal - introductionLogo.startFinal)) {
        introductionLogo.startFinal = introductionLogo.start;
        introductionLogo.endFinal   = introductionLogo.end;
    }
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d)", introductionLogo.startFinal, introductionLogo.endFinal);

    // check separator frame before introduction logo
    if (separatorFrameBefore >= 0) {
        int diffSeparator = 1000 * (endPos - separatorFrameBefore) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator frame before introduction logo found (%d), %dms before start mark", separatorFrameBefore, diffSeparator);
    }
    else {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): no separator frame found");
        return -1;
    }

    // check separator frame after introduction logo
    if ((separatorFrameAfter >= 0) && (introductionLogo.endFinal >= 0) && (separatorFrameAfter >= introductionLogo.endFinal)) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator image after introduction logo found (%d)", separatorFrameAfter);
        return -1;
    }

#define INTRODUCTION_STILL_MAX_DIFF_END       1919   // max distance of still image to start mark (endPos)
    // check still image after introduction logo
    if ((stillImage.end - stillImage.start) >= (stillImage.endFinal - stillImage.startFinal)) {
        stillImage.startFinal = stillImage.start;
        stillImage.endFinal   = stillImage.end;
    }
    int length        = 1000 * (introductionLogo.endFinal   - introductionLogo.startFinal) / maContext->Video.Info.framesPerSecond;
    if ((stillImage.startFinal >= 0) && (stillImage.endFinal > 0)) {
        int lengthStillImage = 1000 * (stillImage.endFinal - stillImage.startFinal) / maContext->Video.Info.framesPerSecond;
        int diffStartMark    = 1000 * (endPos              - stillImage.endFinal)   / maContext->Video.Info.framesPerSecond;
        int maxQuote = length * 0.7; // changed from 0.8 to 0.7
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): still image after introduction from (%d) to (%d), length %dms (expect >=%dms), distance to start mark %dms (expect <=%dms)", stillImage.startFinal, stillImage.endFinal, lengthStillImage, maxQuote, diffStartMark, INTRODUCTION_STILL_MAX_DIFF_END);
        if ((lengthStillImage >= maxQuote) && (diffStartMark <= INTRODUCTION_STILL_MAX_DIFF_END)) return -1;
    }

    // check introduction logo
    int diffEnd       = 1000 * (endPos                      - introductionLogo.endFinal)   / maContext->Video.Info.framesPerSecond;
    int diffSeparator = 1000 * (introductionLogo.startFinal - separatorFrameBefore)   / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), length %dms (expect >=%dms <=%dms)", introductionLogo.startFinal, introductionLogo.endFinal, length, INTRODUCTION_MIN_LENGTH, INTRODUCTION_MAX_LENGTH);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), diff to logo start mark %dms (expect <=%dms)", introductionLogo.startFinal, introductionLogo.endFinal, diffEnd, INTRODUCTION_MAX_DIFF_END);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), diff to separator frame (%d) %dms (expect <=%dms)", introductionLogo.startFinal, introductionLogo.endFinal, separatorFrameBefore, diffSeparator, INTRODUCTION_MAX_DIFF_SEPARATOR);
    if ((length >= INTRODUCTION_MIN_LENGTH) && (length <= INTRODUCTION_MAX_LENGTH) && (diffEnd <= INTRODUCTION_MAX_DIFF_END) && (diffSeparator <= INTRODUCTION_MAX_DIFF_SEPARATOR)) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): found introduction logo start at (%d)", introductionLogo.startFinal);
        retFrame = introductionLogo.startFinal;
    }
    else dsyslog("cDetectLogoStopStart::IntroductionLogo(): no introduction logo found");
    return retFrame;
}
