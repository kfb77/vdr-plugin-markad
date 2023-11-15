/*
 * evaluate.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "evaluate.h"
#include "logo.h"



bool cEvaluateChannel::IsInfoLogoChannel(const char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "DMAX")    != 0) &&
        (strcmp(channelName, "DMAX_HD")       != 0) &&
        (strcmp(channelName, "SIXX")          != 0) &&
        (strcmp(channelName, "SIXX_HD")       != 0) &&
        (strcmp(channelName, "SAT_1")         != 0) &&
        (strcmp(channelName, "SAT_1_HD")      != 0) &&
        (strcmp(channelName, "WELT")          != 0) &&
        (strcmp(channelName, "WELT_HD")       != 0) &&
        (strcmp(channelName, "RTL2")          != 0) &&
        (strcmp(channelName, "RTL2_HD")       != 0)) {
       return false;
    }
    return true;
}


bool cEvaluateChannel::IsLogoChangeChannel(const char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "TELE_5")    != 0) &&  // has special logo changes
        (strcmp(channelName, "TELE_5_HD") != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::ClosingCreditsChannel(const char *channelName) {
    // for performance reason only known and tested channels
    if ((strcmp(channelName, "kabel_eins")    != 0) &&
        (strcmp(channelName, "kabel_eins_HD") != 0) &&
        (strcmp(channelName, "krone_tv")      != 0) &&
        (strcmp(channelName, "SAT_1")         != 0) &&
        (strcmp(channelName, "SAT_1_HD")      != 0) &&
        (strcmp(channelName, "SIXX")          != 0) &&
        (strcmp(channelName, "SIXX_HD")       != 0) &&
        (strcmp(channelName, "DMAX")          != 0) &&
        (strcmp(channelName, "DMAX_HD")       != 0) &&
        (strcmp(channelName, "Pro7_MAXX")     != 0) &&
        (strcmp(channelName, "Pro7_MAXX_HD")  != 0) &&
        (strcmp(channelName, "RTL2")          != 0) &&
        (strcmp(channelName, "RTL2_HD")       != 0) &&
        (strcmp(channelName, "ProSieben")     != 0) &&
        (strcmp(channelName, "ProSieben_HD")  != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::AdInFrameWithLogoChannel(const char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "kabel_eins")        != 0) &&
        (strcmp(channelName, "kabel_eins_HD")     != 0) &&
        (strcmp(channelName, "Pro7_MAXX")         != 0) &&
        (strcmp(channelName, "Pro7_MAXX_HD")      != 0) &&
        (strcmp(channelName, "ProSieben")         != 0) &&
        (strcmp(channelName, "ProSieben_HD")      != 0) &&
        (strcmp(channelName, "ProSieben_MAXX")    != 0) &&
        (strcmp(channelName, "ProSieben_MAXX_HD") != 0) &&
        (strcmp(channelName, "RTL2")              != 0) &&
        (strcmp(channelName, "RTL2_HD")           != 0) &&
        (strcmp(channelName, "RTLZWEI")           != 0) &&
        (strcmp(channelName, "RTLZWEI_HD")        != 0) &&
        (strcmp(channelName, "RTL_Television")    != 0) &&
        (strcmp(channelName, "RTL_Television_HD") != 0) &&
        (strcmp(channelName, "SAT_1")             != 0) &&
        (strcmp(channelName, "SAT_1_HD")          != 0) &&
        (strcmp(channelName, "SIXX")              != 0) &&
        (strcmp(channelName, "SIXX_HD")           != 0) &&
        (strcmp(channelName, "VOX")               != 0) &&
        (strcmp(channelName, "VOX_HD")            != 0) &&
        (strcmp(channelName, "VOXup")             != 0) &&
        (strcmp(channelName, "VOXup_HD")          != 0) &&
        (strcmp(channelName, "WELT")              != 0) &&
        (strcmp(channelName, "WELT_HD")           != 0)) {
        return false;
    }
    return true;
}


bool cEvaluateChannel::IntroductionLogoChannel(const char *channelName) {
// for performance reason only for known and tested channels for now
    if ((strcmp(channelName, "kabel_eins")    != 0) &&
        (strcmp(channelName, "kabel_eins_HD") != 0) &&
        (strcmp(channelName, "SIXX")          != 0) &&
        (strcmp(channelName, "SIXX_HD")       != 0) &&
        (strcmp(channelName, "SAT_1")         != 0) &&
        (strcmp(channelName, "SAT_1_HD")      != 0) &&
        (strcmp(channelName, "RTL2")          != 0) &&
        (strcmp(channelName, "RTL2_HD")       != 0)) {
        return false;
    }
    return true;
}


cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair() {
    dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): called");
}


cEvaluateLogoStopStartPair::~cEvaluateLogoStopStartPair() {
    dsyslog("cEvaluateLogoStopStartPair::~cEvaluateLogoStopStartPair(): called");
#ifdef DEBUG_MEM
    int size =  logoPairVector.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sLogoStopStartPair), "logoPairVector");
    }
#endif
     logoPairVector.clear();
}


// Check logo stop/start pairs
// used by logo change detection
void cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(sMarkAdContext *maContext, cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART, const int iStopA) {
    if (!marks) return;

#define LOGO_CHANGE_NEXT_STOP_MIN     6840  // in ms, do not increase, 6840ms is the shortest found distance between two logo changes
                                            // next stop max (=lenght next valid broadcast) found: 1242s
#define LOGO_CHANGE_IS_ADVERTISING_MIN 300  // in s
#define LOGO_CHANGE_IS_BROADCAST_MIN   240  // in s

    dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): start with iStart %d, chkSTART %d, iStopA %d", iStart, chkSTART, iStopA);
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
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): -----------------------------------------------------------------------------------------");
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): stop (%d) start (%d) pair", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        // check for info logo section
        if (IsInfoLogoChannel(maContext->Info.ChannelName)) IsInfoLogo(marks, blackMarks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond, iStart);
        else logoPairIterator->isInfoLogo = STATUS_DISABLED;

        // check for logo change section
        if (IsLogoChangeChannel(maContext->Info.ChannelName)) IsLogoChange(marks, &(*logoPairIterator), maContext->Video.Info.framesPerSecond, iStart, chkSTART);
        else logoPairIterator->isLogoChange = STATUS_DISABLED;

        // check for closing credits section
        if (ClosingCreditsChannel(maContext->Info.ChannelName)) IsClosingCredits(marks, &(*logoPairIterator));
        else logoPairIterator->isClosingCredits = STATUS_DISABLED;

        // global informations about logo pairs
        // mark after pair
        const cMark *markStop_AfterPair = marks->GetNext(logoPairIterator->stopPosition, MT_LOGOSTOP);
        int deltaStopStart = (logoPairIterator->startPosition - logoPairIterator->stopPosition ) / maContext->Video.Info.framesPerSecond;
        if (deltaStopStart >= LOGO_CHANGE_IS_ADVERTISING_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: delta %ds (expect >=%ds) is a advertising", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_IS_ADVERTISING_MIN);
            logoPairIterator->isAdInFrame = STATUS_YES;
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
            dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: next stop mark after stop/start pair in %ds (expect >=%ds, start mark is in braoscast)", logoPairIterator->stopPosition, logoPairIterator->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_IS_BROADCAST_MIN);
            logoPairIterator->isStartMarkInBroadcast = 1;
        }
    }

    // check section of stop/start pairs
    // search for part between advertising and broadcast, keep this mark, because it contains the start mark of the broadcast
    //
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->isAdInFrame == STATUS_YES) {  // advertising pair
            std::vector<sLogoStopStartPair>::iterator next1LogoPairIterator = logoPairIterator;
            ++next1LogoPairIterator;
            if (next1LogoPairIterator != logoPairVector.end()) {
                if ((next1LogoPairIterator->isLogoChange == 0) && (next1LogoPairIterator->isStartMarkInBroadcast  == 0)){ // unknown pair
                    std::vector<sLogoStopStartPair>::iterator next2LogoPairIterator = next1LogoPairIterator;
                    ++next2LogoPairIterator;
                    if (next2LogoPairIterator != logoPairVector.end()) {
                        if (next2LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                            dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", next1LogoPairIterator->stopPosition, next1LogoPairIterator->startPosition);
                            next1LogoPairIterator->isLogoChange = STATUS_NO;
                        }
                        if ((next2LogoPairIterator->isLogoChange == 0) && (next2LogoPairIterator->isStartMarkInBroadcast  == 0)) { // unknown pair
                            std::vector<sLogoStopStartPair>::iterator next3LogoPairIterator = next2LogoPairIterator;
                            ++next3LogoPairIterator;
                            if (next3LogoPairIterator != logoPairVector.end()) {
                                if (next3LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                                    dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", next2LogoPairIterator->stopPosition, next2LogoPairIterator->startPosition);
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
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): add stop (%d) start (%d) pair:", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isLogoChange           %2d", logoPairIterator->isLogoChange);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isAdInFrame            %2d", logoPairIterator->isAdInFrame);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isStartMarkInBroadcast %2d", logoPairIterator->isStartMarkInBroadcast);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isInfoLogo             %2d", logoPairIterator->isInfoLogo);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isClosingCredits       %2d", logoPairIterator->isClosingCredits);
    }
    nextLogoPairIterator = logoPairVector.begin();
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
void cEvaluateLogoStopStartPair::IsInfoLogo(cMarks *marks, cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond, const int iStart) {
    if (framesPerSecond <= 0) return;
#define LOGO_INFO_LENGTH_MIN  3720  // min time in ms of a info logo section, bigger values than in InfoLogo becase of seek to iFrame, changed from 5000 to 4480 to 3720
#define LOGO_INFO_LENGTH_MAX 22480  // max time in ms of a info logo section, changed from 17680 to 22480
                                    // RTL2 has very long info logos
#define LOGO_INFO_SHORT_BLACKSCREEN_BEFORE_DIFF_MAX 440  // max time in ms no short blackscreen allowed before stop mark, changed from 40 to 440 to 360 to 440
                                                         // no not change, there are info logos direct after very short start logo (440ms before, length 1000ms)
#define LOGO_INFO_SHORT_BLACKSCREEN_LENGTH         1000  // length of a short blackscreen, changed from 1080 to 1000

#define LOGO_INFO_LONG_BLACKSCREEN_BEFORE_DIFF_MAX 2000  // max time in ms no long blackscreen allowed before stop mark, changed from 1920 to 1960 to 2000
#define LOGO_INFO_LONG_BLACKSCREEN_LENGTH          5000  // length of a long blackscreen
#define LOGO_INFO_BROADCAST_AFTER_MIN              1160  // min length of broadcast after info logo, changed from 4000 to 1160

#define LOGO_INFO_NEXT_STOP_MIN                    2120  // min distance of next logo stop/start pair to merge, changed from 3000 to 2120

    int maxNextStop = 0;
    if (iStart > 0) maxNextStop = 5920;                  // we are in start mark, less risk of deleting valid stop mark
    else            maxNextStop = 4560;                  // max distance of next logo stop/start pair to merge
                                                         // if info logo is very similar to logo, we false detect this as logo
                                                         // first logo start mark: nearest info logo after logo start 5920ms
                                                         // do not merge real start mark with info logo stop/start
                                                         // last logo stop mark: nearest logo stop 4560ms after info logo found, do not merge with real stop mark
    // check length
    int length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / framesPerSecond;
    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: length %dms (expect >=%dms and <=%dms)",
                                                        logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);

    // calculate next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / framesPerSecond;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: next stop mark (%d) distance %dms (expect <%dms for merge)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, markStop_AfterPair->position, delta_Stop_AfterPair, maxNextStop);
    }

    // maybe we have a wrong start/stop pair between, check if merge with next pair can help
    if ((length < LOGO_INFO_LENGTH_MIN) && (delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair < maxNextStop) && (length < 11800)) { // next pair is too near, do not merge big pairs
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): short pair and very near next start mark, try to merge with next pair");

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
                    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %dms too short, (expect >=%ds <=%ds), try next", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, maxNextStop);
                    pairNextStart = marks->GetNext(pairNextStart->position, MT_LOGOSTART);
                    pairNextStop  = marks->GetNext(pairNextStop->position, MT_LOGOSTOP);
                }
                else {
                    if (deltaStopAfterPair > maxNextStop) {
                        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %dms too big, (expect >=%ds <=%ds", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, maxNextStop);
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
        if ((lengthBlack >= 560) && (lengthBlack < 4400)) {  // too long blackscreen can be opening credits
                                                             // changed from 680 to 560
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


bool cEvaluateLogoStopStartPair::GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo, int *isStartMarkInBroadcast, const int endRange) {
    if (!stopPosition)           return false;
    if (!startPosition)          return false;
    if (!isLogoChange)           return false;
    if (!isInfoLogo)             return false;
    if (!isStartMarkInBroadcast) return false;
    if (nextLogoPairIterator == logoPairVector.end()) return false;

    // skip pair if there is nothing to detect
    while ((nextLogoPairIterator->stopPosition < endRange) &&  // if we are in end range we possible need to detect colong credits
           (nextLogoPairIterator->isLogoChange == STATUS_NO) && (nextLogoPairIterator->isInfoLogo == STATUS_NO)) {
        ++nextLogoPairIterator;
        if (nextLogoPairIterator == logoPairVector.end()) return false;
    }
    *stopPosition           = nextLogoPairIterator->stopPosition;
    *startPosition          = nextLogoPairIterator->startPosition;
    *isLogoChange           = nextLogoPairIterator->isLogoChange;
    *isInfoLogo             = nextLogoPairIterator->isInfoLogo;
    *isStartMarkInBroadcast = nextLogoPairIterator->isStartMarkInBroadcast;
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


void cEvaluateLogoStopStartPair::AddAdInFrame(const int startPosition, const int stopPosition) {
    dsyslog("cEvaluateLogoStopStartPair::AddAdInFrame(): add pair for adinframe at start (%d) stop (%d)", startPosition, stopPosition);
    sLogoStopStartPair newPair;
    newPair.startPosition = startPosition;
    newPair.stopPosition  = stopPosition;
    newPair.isAdInFrame   = STATUS_YES;
    logoPairVector.push_back(newPair);
    ALLOC(sizeof(sLogoStopStartPair), "logoPairVector");
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
    dsyslog("cEvaluateLogoStopStartPair::IncludesInfoLogo(): check if start mark (%d) and stop mark (%d) includes info logo", startPosition, stopPosition);
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition <= startPosition) && (value.stopPosition >= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::IncludesInfoLogo(): stop (%d) start (%d) pair includes info logo for stop (%d) start (%d) pair", stopPosition, startPosition, found->stopPosition, found->startPosition);
        return true;
    }

    return false;
}


cDetectLogoStopStart::cDetectLogoStopStart(sMarkAdContext *maContextParam, cMarkCriteria *markCriteriaParam, cDecoder *ptr_cDecoderParam, cIndex *recordingIndexParam, cEvaluateLogoStopStartPair *evaluateLogoStopStartPairParam) {
    maContext                 = maContextParam;
    markCriteria              = markCriteriaParam;
    ptr_cDecoder              = ptr_cDecoderParam;
    recordingIndex            = recordingIndexParam;
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


int cDetectLogoStopStart::FindFrameFirstPixel(const uchar *picture, const int corner, const int width, const int height, int startX, int startY, const int offsetX, const int offsetY) {
    int foundX        = -1;
    int foundY        = -1;
    int searchX       = startX;
    int searchY       = startY;
    int searchOffsetX = 0;
    int searchOffsetY = 0;

    switch (corner) {
        case 0: // TOP_LEFT
            searchOffsetX = 1;
            searchOffsetY = 1;
            break;

        case 1: // TOP_RIGHT
            searchOffsetX = -1;
            searchOffsetY =  1;
            break;

        case 2: // BOTTOM_LEFT
            searchOffsetX =  1;
            searchOffsetY = -1;
            break;

        case 3: // BOTTOM_RIGHT
            searchOffsetX = -1;
            searchOffsetY = -1;
            break;
        default:
            return 0;
    } // case

    for (int i = 0; i < width; i++) {
        if (picture[searchY * width + searchX] == 0) {  // pixel found
            foundX = searchX;
            foundY = searchY;
            break;
        }
        searchX += offsetX;
        if ((searchX < 0) || (searchX >= width)) break;
        searchY += offsetY;
        if ((searchY < 0) || (searchY >= height)) break;
    }
#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("-----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("cDetectLogoStopStart::FindFrameFirstPixel(): search for first pixel: start (%d,%d), direction (%d,%d): found (%d,%d)", startX, startY, offsetX, offsetY, foundX, foundY);
#endif
    if ((foundX >= 0) && (foundY >= 0)) return FindFrameStartPixel(picture, width, height, foundX, foundY, -searchOffsetX, -searchOffsetY);
    return 0;
}


int cDetectLogoStopStart::FindFrameStartPixel(const uchar *picture, const int width, const int height,  int startX, int startY, const int offsetX, const int offsetY) {
    int firstPixelX = startX;
    int firstPixelY = startY;
    while ((startX > 0) && (startX < width - 1)) {
        if (picture[startY * width + startX + offsetX] == 0) startX += offsetX;  // next position has pixel
        else break;
    }
    while ((startY > 0) && (startY < height - 1)) {
        if (picture[(startY + offsetY) * width + startX] == 0) startY += offsetY;  // next position has pixel
        else break;
    }
    if (abs(firstPixelX - startX) > abs(firstPixelY - startY)) startY = firstPixelY;  // prevent to get out of line from a single pixel, only one coordinate can change
    else startX = firstPixelX;
#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("cDetectLogoStopStart::FindFrameStartPixel(): search for start pixel: first pixel (%d,%d), direction (%d,%d): found (%d,%d)", firstPixelX, firstPixelY, offsetX, offsetY, startX, startY);
#endif
    int endX = -1;
    int endY = -1;
    int portion = FindFrameEndPixel(picture, width, height, startX, startY, -offsetX, -offsetY, &endX, &endY);
    if ((abs(startX - endX) >= 50) && (abs(startY - endY) <= 2)) { // we found a long horizontal line but no frame, try to find a reverse frame in X direction, from end pixel
                                                                   // changed from 90 to 50
        startX = endX;
        startY = endY;
        int portionTMP = FindFrameEndPixel(picture, width, height, startX, startY, offsetX, offsetY, &endX, &endY);  // try reverse direction
        if (portionTMP > portion) portion = portionTMP;
        // maybe we missed the corner pixel, try to find a reverse frame one pixel before
        if ((abs(startX - endX) >= 50) && (abs(startY - endY) <= 2)) {  // still only a long horizontal line but no frame
            portionTMP = FindFrameEndPixel(picture, width, height, startX + offsetX, endY, offsetX, offsetY, &endX, &endY);  // try reverse direction, one pixel after
            if (portionTMP > portion) portion = portionTMP;
        }
    }
    return portion;
}


int cDetectLogoStopStart::FindFrameEndPixel(const uchar *picture, const int width, const int height, const int startX, const int startY, const int offsetX, const int offsetY, int *endX, int *endY) {
    int pixelError    = 0;   // accept missing pixel in the frame from detection errors
    int sumPixelError = 0;
    *endX = startX;
    while (((*endX + pixelError) >= 0) && ((*endX + pixelError) < width)) {
        if (picture[startY * width + *endX + pixelError + offsetX] == 0) {
           *endX += (offsetX + pixelError);
           pixelError = 0;
        }
        else {
            if (abs(pixelError) >= (4 * abs(offsetX))) break;
            pixelError += offsetX;
            sumPixelError++;
            if (sumPixelError >= 8) break;
        }
    }
    pixelError    = 0;   // accept missing pixel in the frame from detection errors
    sumPixelError = 0;
    *endY = startY;
    while ((*endY + pixelError >= 0) && (*endY + pixelError < height)) {
        if (picture[(*endY + pixelError + offsetY) * width + startX] == 0) {
            *endY += (offsetY + pixelError);
            pixelError = 0;
        }
        else {
            if (abs(pixelError) >= (3 * abs(offsetY))) break;
            pixelError += offsetY;
            sumPixelError++;
            if (sumPixelError >= 6) break;
        }
    }
    int lengthX = abs(*endX - startX);
    int lengthY = abs(*endY - startY);
    int portion = 0;
    if ((lengthX > 50) || (lengthY > 50) || // we have a part of the frame found, a vertical or horizontal line
       ((lengthX > 8) && (lengthY > 8))) portion = 1000 * lengthX / width + 1000 * lengthY / height;
#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("cDetectLogoStopStart::FindFrameEndPixel(): search for end pixel: direction (%2d,%2d): found frame from (%d,%d) to (%d,%d), length (%d,%d) -> portion %d", offsetX, offsetY, startX, startY, *endX, *endY, lengthX, lengthY, portion);
#endif
    return portion;
}


// detect frame in sobel transformed picture
// sobel transformes lines can be interrupted, try also pixel next to start point
// return portion of frame pixel in picture
int cDetectLogoStopStart::DetectFrame(__attribute__((unused)) const int frameNumber, const uchar *picture, const int width, const int height, const int corner) {
    if (!picture) return 0;

    int portion =  0;

#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++");
    dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: search for frame", frameNumber, corner);
#endif

    switch (corner) {
        case 0: // TOP_LEFT
            portion = FindFrameFirstPixel(picture, corner, width, height, 10, 0, 1, 1); // search from top left to bottom right
                                                                                        // do not start at corner, maybe conrner was not exacly detected
            if (portion < 230) {  // maybe we have a text under frame or the logo
                int portionTMP = FindFrameFirstPixel(picture, corner, width, height, width / 2, 0, 1, 1); // search from top mid to bottom right
                if (portionTMP > portion) portion = portionTMP;
                if (portion < 230) {  // maybe we have a text under frame or the logo
                    portionTMP = FindFrameFirstPixel(picture, corner, width, height, 0 , height / 2, 1, 0); // search horizontal mid right mid left
                    if (portionTMP > portion) portion = portionTMP;
                    if (portion < 230) {  // maybe we have a text under frame or the logo
                        portionTMP = FindFrameFirstPixel(picture, corner, width, height, 0 , height * 9 / 10, 1, 0); // search horizontal from 9/10 button left to right
                        if (portionTMP > portion) portion = portionTMP;
                    }
                }
            }
            break;

        case 1: // TOP_RIGHT
            portion = FindFrameFirstPixel(picture, corner, width, height, width - 1, 0, -1, 1); // search from top right to bottom left
            if (portion < 180) {  // maybe we have a text under frame or the logo
                portion = FindFrameFirstPixel(picture, corner, width, height, width / 2, 0, -1, 1); // search from top mid to bottom left
            }
            break;

        case 2: // BOTTOM_LEFT
            portion = FindFrameFirstPixel(picture, corner, width, height, 0, height - 1, 1, -1); // search from buttom left to top right
            if (portion < 180) {  // maybe we have a text under frame
                portion = FindFrameFirstPixel(picture, corner, width, height, width / 2, height - 1, 1, -1); // search from bottom mid to top right
                if (portion < 180) {  // maybe we have only a part of the frame
                    portion = FindFrameFirstPixel(picture, corner, width, height, width / 3, height - 1, 1, -1); // search from bottom 1/3 left to top right
                    if (portion < 180) {  // maybe we have only a part of the frame
                        portion = FindFrameFirstPixel(picture, corner, width, height, 0, height / 2, 1, -1); // search from mid right  to top right
                        if (portion < 180) {  // maybe we have only a part of the frame
                            portion = FindFrameFirstPixel(picture, corner, width, height, width, 0, -1, 1); // search from top right to buttom left (horizontal line with text below)
                        }
                    }
                }
            }
            break;

        case 3: // BOTTOM_RIGHT
            portion = FindFrameFirstPixel(picture, corner, width, height, width - 1, height - 1, -1, -1);         // search from buttom right to top left
            if (portion < 180) {  // maybe we have a text under frame
                portion = FindFrameFirstPixel(picture, corner, width, height, width - 1, height / 2, -1, -1);     // search from mid right to top left
                if (portion < 180) {  // maybe we have a text under frame
                    portion = FindFrameFirstPixel(picture, corner, width, height, width / 2, height - 1, -1, -1); // search from buttom mid right to top left
                    if (portion < 180) {  // maybe we have a logo under frame (e.g. SAT.1)
                        portion = FindFrameFirstPixel(picture, corner, width, height, width * 2 / 3, 0, -1, 1); // search from 2/3 right top to left button
                    }
                }
            }
            break;

        default:
            return 0;
    } // case

#ifdef DEBUG_MARK_OPTIMIZATION
    dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: portion %3d", frameNumber, corner, portion);
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

    cMarkAdLogo *ptr_Logo = new cMarkAdLogo(maContext, markCriteria, recordingIndex);
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
        if (!ptr_cDecoder->GetNextPacket(false, false)) {
            dsyslog("cDetectLogoStopStart::Detect(): GetNextPacket() failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            status = false;
        }
        int frameNumber =  ptr_cDecoder->GetFrameNumber();
        if (!ptr_cDecoder->IsVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(maContext, true, maContext->Config->fullDecode, false, false)) {
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
        int start                     = 0;
        int end                       = 0;
        int startFinal                = 0;
        int endFinal                  = 0;
        int matchLogoCornerCount      = 0;
        int matchRestCornerCount      = 0;
        int matchLogoCornerCountFinal = 0;
        int matchRestCornerCountFinal = 0;
    } infoLogo;

    bool found                 = true;
    int  lastSeparatorFrame    = -1;
    int  countSeparatorFrame   =  0;
    int  lowMatchCornerCount   =  0;
    int  countFrames           =  0;
    int  countDark             =  0;

#define INFO_LOGO_MACTH_MIN 209  // changed from 210 to 209

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
#endif

        int sumPixel              = 0;
        int countZero             = 0;
        int darkCorner            = 0;

        for (int corner = 0; corner < CORNERS; corner++) {
            if (((corner == maContext->Video.Logo.corner) && (*cornerResultIt).rate[corner] > INFO_LOGO_MACTH_MIN)) infoLogo.matchLogoCornerCount++;
            if (((corner != maContext->Video.Logo.corner) && (*cornerResultIt).rate[corner] > INFO_LOGO_MACTH_MIN)) infoLogo.matchRestCornerCount++;
            if ((*cornerResultIt).rate[corner] <= 0) countZero++;
            sumPixel += (*cornerResultIt).rate[corner];
            if (((*cornerResultIt).rate[corner] <=   0) && (corner != maContext->Video.Logo.corner)) darkCorner++;   // if we have no match, this can be a too dark corner
        }
        // dark scene
        countFrames++;
        if (darkCorner == 3) countDark++;  // if all corners but logo corner has no match, this is a very dark scene

        if (((countZero >= 2) && (sumPixel <=  45)) || // changed from  60 to  45, too big values results in false detection of a separation image, do not increase
            ((countZero >= 3) && (sumPixel <  122))) { // changed from 132 to 122
            countSeparatorFrame++;
            lastSeparatorFrame = (*cornerResultIt).frameNumber2;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator frame (%d)", lastSeparatorFrame);
#endif
        }

        if (((*cornerResultIt).rate[maContext->Video.Logo.corner] > INFO_LOGO_MACTH_MIN) || // do not rededuce to prevent false positiv
            ((*cornerResultIt).rate[maContext->Video.Logo.corner] >= 142) && (lowMatchCornerCount <= 1)) { // allow one lower match for the change from new logo to normal logo
            if ((*cornerResultIt).rate[maContext->Video.Logo.corner] <= INFO_LOGO_MACTH_MIN) {
                lowMatchCornerCount++;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): lowMatchCornerCount %d", lowMatchCornerCount);
#endif
            }
            if (infoLogo.start == 0) {
                infoLogo.start = (*cornerResultIt).frameNumber1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): start info log: start frame (%d)", infoLogo.start);
#endif
                }
            infoLogo.end = (*cornerResultIt).frameNumber2;
        }
        else {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): end info log: start frame (%d), end frame (%d), matchLogoCornerCount %d, matchRestCornerCount (%d)", infoLogo.start, infoLogo.end, infoLogo.matchLogoCornerCount, infoLogo.matchRestCornerCount);
#endif
            if ((infoLogo.end - infoLogo.start) > (infoLogo.endFinal - infoLogo.startFinal)) {
                infoLogo.startFinal                = infoLogo.start;
                infoLogo.endFinal                  = infoLogo.end;
                infoLogo.matchLogoCornerCountFinal = infoLogo.matchLogoCornerCount;
                infoLogo.matchRestCornerCountFinal = infoLogo.matchRestCornerCount;
            }
            infoLogo.start                = 0;  // reset state
            infoLogo.end                  = 0;
            infoLogo.matchLogoCornerCount = 0;
            infoLogo.matchRestCornerCount = 0;
            lowMatchCornerCount           = 0;
        }
    }
    if ((infoLogo.end - infoLogo.start) > (infoLogo.endFinal - infoLogo.startFinal)) {
        infoLogo.startFinal                = infoLogo.start;
        infoLogo.endFinal                  = infoLogo.end;
        infoLogo.matchLogoCornerCountFinal = infoLogo.matchLogoCornerCount;
        infoLogo.matchRestCornerCountFinal = infoLogo.matchRestCornerCount;
    }

    // check if "no logo" corner has same matches as logo corner, in this case it must be a static scene (e.g. static preview picture in frame or adult warning) and no info logo
    infoLogo.matchRestCornerCountFinal /= 3;
    dsyslog("cDetectLogoStopStart::IsInfoLogo(): count matches greater than limit of %d: %d logo corner, avg rest corners %d", INFO_LOGO_MACTH_MIN, infoLogo.matchLogoCornerCountFinal, infoLogo.matchRestCornerCountFinal);
    if (infoLogo.matchLogoCornerCountFinal <= (infoLogo.matchRestCornerCountFinal + 3)) {  // changed from 2 to 3
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): too much similar corners, this must be a static ad or preview picture");
        found = false;
    }

    // check separator image
    if (found) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): count separator frames %d", countSeparatorFrame);
        if (countSeparatorFrame > 4) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): too much separator frames, this can not be a info logo");
            found = false;
        }
    }

    // check dark quote
    if (found) {
        int darkQuote = 100 * countDark / countFrames;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): dark quote %d%%", darkQuote);
        if ((lastSeparatorFrame >= 0) && (darkQuote < 65)) {  // on dark scenes we can not detect separator image
                                                              // darkQuote changed from 100 to 69 to 65
            int diffSeparator = 1000 * (endPos - lastSeparatorFrame) / maContext->Video.Info.framesPerSecond;
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): last separator frame found (%d), %dms before end", lastSeparatorFrame, diffSeparator);
            if (diffSeparator < 1080) { // changed from 1160 to 1080
                                        // we can get a false detected separator frame in a dark scene during change from info logo to normal logo near logo start
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator frame found, this is a valid start mark");
                found = false;
            }
        }
    }

    // check info logo
    if (found) {
        // ignore short parts at start and end, this is fade in and fade out
        int diffStart = 1000 * (infoLogo.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
        int diffEnd = 1000 * (endPos - infoLogo.endFinal) / maContext->Video.Info.framesPerSecond;
        int newStartPos = startPos;
        int newEndPos = endPos;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo start diff %dms, end diff %dms", diffStart, diffEnd);
        if (diffStart < 1920) newStartPos = infoLogo.startFinal;  // do not increase
        if (diffEnd <= 1800) newEndPos = infoLogo.endFinal;  // changed from 250 to 960 to 1440 to 1800
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): final range start (%d) end (%d)", newStartPos, newEndPos);
#define INFO_LOGO_MIN_LENGTH  3761  // changed from 2880 to 3761
                                    // prevent to get info box after preview as info logo, length 3760
#define INFO_LOGO_MAX_LENGTH 17040  // chnaged from 15640 to 15880 to 17040
                                    // RTL2 has very long info logos
#define INFO_LOGO_MIN_QUOTE     61  // no info logo: separator image with part time logo 60
        int quote  = 100  * (infoLogo.endFinal - infoLogo.startFinal) / (newEndPos - newStartPos);
        int length = 1000 * (infoLogo.endFinal - infoLogo.startFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo: start (%d), end (%d), length %dms (expect >=%dms and <=%dms), quote %d%% (expect >= %d%%)", infoLogo.startFinal, infoLogo.endFinal, length, INFO_LOGO_MIN_LENGTH, INFO_LOGO_MAX_LENGTH, quote, INFO_LOGO_MIN_QUOTE);
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
        if (ClosingCredit(true) >= 0) {
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

    int  highMatchCount          =  0;  // check if we have a lot of very similar pictures in the logo corner
    int  lowMatchCount           =  0;   // we need at least a hight quote of low similar pictures in the logo corner, if not there is no logo
    int  count                   =  0;
    int  countNoLogoInLogoCorner =  0;
    int  match[CORNERS]          = {0};
    int  matchNoLogoCorner       =  0;
    int  darkSceneStart          = -1;
    bool status                  =  true;

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
#ifdef DEBUG_MARK_OPTIMIZATION
        dsyslog("cDetectLogoStopStart::isLogoChange(): frame (%5d) and frame (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
#endif
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
    }
    // log found results
    for (int corner = 0; corner < CORNERS; corner++) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): corner %-12s rate summery %5d of %2d frames", aCorner[corner], match[corner], count);
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
int cDetectLogoStopStart::ClosingCredit(const bool noLogoCorner) {
    if (!maContext) return -1;

    if (!ClosingCreditsChannel(maContext->Info.ChannelName)) return -1;
    if (evaluateLogoStopStartPair && evaluateLogoStopStartPair->GetIsClosingCredits(startPos, endPos) == STATUS_NO) {
        dsyslog("cDetectLogoStopStart::ClosingCredit(): already known no closing credits from (%d) to (%d)", startPos, endPos);
        return -1;
    }

    dsyslog("cDetectLogoStopStart::ClosingCredit(): detect from (%d) to (%d)", startPos, endPos);

#define CLOSING_CREDITS_LENGTH_MIN 6120  // changed from 9000 to 6120
                                         // because of silence detection before closing credits detection
    int minLength = (1000 * (endPos - startPos) / maContext->Video.Info.framesPerSecond) - 2000;  // 2s buffer for change from closing credit to logo start
    if (minLength <= 3760) { // too short will result in false positive, changed from 1840 to 3760
        dsyslog("cDetectLogoStopStart::ClosingCredit(): length %dms too short for detection", minLength);
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
    int countFrames           = 0;
    int countDark             = 0;

#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_CLOSINGCREDITS)
    dsyslog("cDetectLogoStopStart::ClosingCredit(():      1. frame    2. frame:   matches (frame portion of 2. frame)");
    dsyslog("cDetectLogoStopStart::ClosingCredit(():                              TOP_LEFT     TOP_RIGHT    BOTTOM_LEFT  BOTTOM_RIGHT");
#endif

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_CLOSINGCREDITS)
        dsyslog("cDetectLogoStopStart::ClosingCredit(): frame (%5d) and (%5d): %5d (%4d) %5d (%4d) %5d (%4d) %5d (%4d)", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).framePortion[0], (*cornerResultIt).rate[1], (*cornerResultIt).framePortion[1], (*cornerResultIt).rate[2], (*cornerResultIt).framePortion[2], (*cornerResultIt).rate[3], (*cornerResultIt).framePortion[3]);
#endif

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

    // check if it is a closing credit
    int startOffset = 1000 * (ClosingCredits.start - startPos) / maContext->Video.Info.framesPerSecond;
    int endOffset   = 1000 * (endPos - ClosingCredits.end) / maContext->Video.Info.framesPerSecond;
    int length      = 1000 * (ClosingCredits.end - ClosingCredits.start) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): closing credits: start (%d) end (%d), offset start %dms end %dms, length %dms",
                                                                                                          ClosingCredits.start, ClosingCredits.end, startOffset, endOffset, length);

    if ((ClosingCredits.start > 0) && (ClosingCredits.end > 0) && // we found something
        (startOffset <= 4320) && (length <= 28720) && // do not reduce start offset, if logo fade out, we got start a little too late
                                                      // changed length from 19000 to 28720, long ad in frame between broadcast, detect as closing credit to get correct start mark
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
    int allSumFramePortion =  0;
    int frameCorner        = -1;
    for (int corner = 0; corner < CORNERS; corner++) {
        allSumFramePortion += ClosingCredits.sumFramePortion[corner];
        if (noLogoCorner && (corner ==  maContext->Video.Logo.corner)) continue;  // if we are called from Info logo, we can false detect the info logo as frame
        if (ClosingCredits.sumFramePortion[corner] > maxSumFramePortion) {
            maxSumFramePortion = ClosingCredits.sumFramePortion[corner];
            frameCorner = corner;
        }
    }
    if (ClosingCredits.frameCount > 0) {
        int framePortionQuote = maxSumFramePortion / ClosingCredits.frameCount;
        int allPortionQuote   = allSumFramePortion / ClosingCredits.frameCount / 4;
        if (frameCorner >= 0) {
            dsyslog("cDetectLogoStopStart::ClosingCredit(): sum of frame portion from best corner %s: %d from %d frames, quote %d", aCorner[frameCorner], maxSumFramePortion, ClosingCredits.frameCount, framePortionQuote);
            dsyslog("cDetectLogoStopStart::ClosingCredit(): sum of frame portion from all corner: quote %d", allPortionQuote);
        }
        // example of closing credits
        // best quote 402, all quote 269
        // example of no closing credits
        // best quote 419, all quote ?    -> kitchen cabinet in background
        if ((framePortionQuote <= 419) && (allPortionQuote < 269)) {
            dsyslog("cDetectLogoStopStart::ClosingCredit(): not enought frame pixel found, closing credits not valid");
            closingCreditsFrame = -1;  // no valid closing credits
        }
    }

    if (evaluateLogoStopStartPair && (closingCreditsFrame >= 0)) evaluateLogoStopStartPair->SetIsClosingCredits(startPos, endPos);
    return closingCreditsFrame;
}


// search advertising in frame with logo
// check if we have matches in 3 of 4 corners and a detected frame
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

    struct sAdInFrame {
        int start                         = -1;
        int startFinal                    = -1;
        int end                           = -1;
        int endFinal                      = -1;
        int frameCount                    =  0;
        int frameCountFinal               =  0;
        int sumFramePortion[CORNERS]      = {0};
        int sumFramePortionFinal[CORNERS] = {0};
    } AdInFrame;

    struct sStillImage {
        int start      = -1;
        int startFinal = -1;
        int end        = -1;
        int endFinal   = -1;
    } StillImage;

    int retFrame              = -1;

#define AD_IN_FRAME_STOP_OFFSET_MAX  15479  // changed from 15800 to 15479 because of false detection, do not increase
                                            // accept missing detection if preview direct after ad in frame
#define AD_IN_FRAME_START_OFFSET_MAX  4319  // changed from 4799 to 4319
                                            // prevent to get additional info logo as frame
    int isCornerLogo[CORNERS] = {0};
    int countFrames           =  0;
    int darkFrames            =  0;

#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo():      1. frame    2. frame:   matches (frame portion of 2. frame)");
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo():                              TOP_LEFT     TOP_RIGHT    BOTTOM_LEFT  BOTTOM_RIGHT");
#endif

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): frame (%5d) and (%5d): %5d (%4d) %5d (%4d) %5d (%4d) %5d (%4d)", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).framePortion[0], (*cornerResultIt).rate[1], (*cornerResultIt).framePortion[1], (*cornerResultIt).rate[2], (*cornerResultIt).framePortion[2], (*cornerResultIt).rate[3], (*cornerResultIt).framePortion[3]);
#endif

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
            if (((*cornerResultIt).rate[corner] >= 109) || ((*cornerResultIt).rate[corner] == -1)) similarCornersLow++;  // changed from 138 to 109
            if ((*cornerResultIt).rate[corner]  >= 253) similarCornersHigh++;  // changed from 310 to 253

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

        // check advertising in frame
        // at least 3 corners has low match and 2 corner with high match (text in the frame)
        if ((similarCornersLow >= 3) && (similarCornersHigh >= 2)) {
            if (AdInFrame.start == -1) {
                AdInFrame.start = (*cornerResultIt).frameNumber1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
                dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): ad in frame start (%d)", AdInFrame.start);
                for (int corner = 0; corner < CORNERS; corner++) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): corner %d: AdInFrame.sumFramePortion %7d", corner, AdInFrame.sumFramePortion[corner]);
#endif
            }
            AdInFrame.end = (*cornerResultIt).frameNumber2;
            AdInFrame.frameCount++;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortion[corner] += (*cornerResultIt).framePortion[corner];
        }
        else {
            if ((AdInFrame.start != -1) && (AdInFrame.end != -1)) {  // we have a new pair
                int startOffset = 1000 * (AdInFrame.start - startPos) / maContext->Video.Info.framesPerSecond;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
                dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): ad in frame start (%d) end (%d), isStartMark %d, startOffset %dms",  AdInFrame.start, AdInFrame.end, isStartMark, startOffset);
                for (int corner = 0; corner < CORNERS; corner++) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): corner %d: AdInFrame.sumFramePortion %7d", corner, AdInFrame.sumFramePortion[corner]);
#endif
                if (((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal)) && // new range if longer
                    ((isStartMark && (startOffset < AD_IN_FRAME_START_OFFSET_MAX)) ||  // adinframe after logo start must be near logo start
                    (!isStartMark && (startOffset > 1000)))) { // a valid ad in frame before stop mark has a start offset, drop invalid pair
                        AdInFrame.startFinal      = AdInFrame.start;
                        AdInFrame.endFinal        = AdInFrame.end;
                        AdInFrame.frameCountFinal = AdInFrame.frameCount;
                        for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortionFinal[corner] = AdInFrame.sumFramePortion[corner];
                }
                AdInFrame.start             = -1;  // reset state
                AdInFrame.end               = -1;
                AdInFrame.frameCount        =  0;
                for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortion[corner] = 0;
            }
        }
    }

    // check if we have a very dark scene, in this case we can not detect ad in frame
    int darkQuote = 100 * darkFrames / countFrames;
    if (darkQuote >= 86) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): scene too dark, quote %d%%, can not detect ad in frame", darkQuote);
        return -1;
    }
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): dark quote %d%% is valid for detection", darkQuote);

    // select best found possible ad in frame sequence, in case of ad in frame before stop mark go to end position
    if (!isStartMark) {
        int stopOffset = 1000 * (endPos - AdInFrame.endFinal) / maContext->Video.Info.framesPerSecond;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): select best found possible ad in frame sequence before logo stop mark");
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): before possible ad in frame from (%d) to (%d), length %d frames", AdInFrame.startFinal, AdInFrame.endFinal, AdInFrame.endFinal - AdInFrame.startFinal);
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): last   possible ad in frame from (%d) to (%d), length %d frames", AdInFrame.start, AdInFrame.end, AdInFrame.end - AdInFrame.start);
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): stopOffset %dms, AD_IN_FRAME_START_OFFSET_MAX %dms", stopOffset, AD_IN_FRAME_START_OFFSET_MAX);
        if (((AdInFrame.end - AdInFrame.start) > (AdInFrame.endFinal - AdInFrame.startFinal)) ||
            (stopOffset > AD_IN_FRAME_STOP_OFFSET_MAX)) {  // adinframe before logo stop must be near logo stop
            AdInFrame.startFinal      = AdInFrame.start;
            AdInFrame.endFinal        = AdInFrame.end;
            AdInFrame.frameCountFinal = AdInFrame.frameCount;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortionFinal[corner] = AdInFrame.sumFramePortion[corner];
        }
    }

    // final possible ad in frame
    if (AdInFrame.startFinal != -1) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start (%d) end (%d)", AdInFrame.startFinal, AdInFrame.endFinal);
    }
    else {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no advertising in frame found");
        return -1;
    }

    // check if we have a frame
    int firstSumFramePortion  =  0;
    int secondSumFramePortion =  0;
    int allSumFramePortion    =  0;
    int firstFrameCorner      = -1;
    int secondFrameCorner     = -1;
    for (int corner = 0; corner < CORNERS; corner++) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): sum of frame portion from corner %-12s: %7d", aCorner[corner], AdInFrame.sumFramePortionFinal[corner]);
        allSumFramePortion += AdInFrame.sumFramePortionFinal[corner];
        if (corner == maContext->Video.Logo.corner) continue;  // ignore logo corner, we have a static picture in that corner
        if (AdInFrame.sumFramePortionFinal[corner] > firstSumFramePortion) {
            firstSumFramePortion  = AdInFrame.sumFramePortionFinal[corner];
            firstFrameCorner      = corner;
        }
    }
    for (int corner = 0; corner < CORNERS; corner++) {
        if ((corner == maContext->Video.Logo.corner) || (corner == firstFrameCorner)) continue;
        if (AdInFrame.sumFramePortionFinal[corner] > secondSumFramePortion) {
            secondSumFramePortion = AdInFrame.sumFramePortionFinal[corner];
            secondFrameCorner     = corner;
        }
    }

    int allFramePortionQuote = 0;
    if (AdInFrame.frameCountFinal > 0) {
        allFramePortionQuote = allSumFramePortion / AdInFrame.frameCountFinal / 4;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): average of all corners portion quote in possible ad in frame range: %3d", allFramePortionQuote);
        int firstFramePortionQuote  = firstSumFramePortion  / AdInFrame.frameCountFinal;
        int secondFramePortionQuote = secondSumFramePortion / AdInFrame.frameCountFinal;
        if (firstFrameCorner >= 0) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): sum of        best frame portion from best corner %-12s: %7d from %4d frames, quote %3d", aCorner[firstFrameCorner], firstSumFramePortion, AdInFrame.frameCountFinal, firstFramePortionQuote);
        if (secondFrameCorner >= 0) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): sum of second best frame portion from best corner %-12s: %7d from %4d frames, quote %3d", aCorner[secondFrameCorner], secondSumFramePortion, AdInFrame.frameCountFinal, secondFramePortionQuote);
        // example of ad in frame
        // best corner 568, second best corner 377, average of all corners 449
        //
        // example for no ad in frame (static scene with vertial or horizontal lines, blinds, windows frames or stairs):
        // best corner 663, second best corner 377, average of all corners 461   door frame in background (conflict)
        //
        // best corner 787, second best corner 191, average of all corners 245   car in foreground
        // best corner 782, second best corner 269, average of all corners 328   news ticker on buttom
        // best corner 768, second best corner 442, average of all corners 420   static scene with blinds in background
        // best corner 618, second best corner 563, average of all corners 385   door fram
        // best corner 607, second best corner   0, average of all corners 163
        // best corner 504, second best corner  85, average of all corners 196
        // best corner 500, second best corner  87, average of all corners 173
        // best corner 328, second best corner 300, average of all corners 180
        // best corner 326, second best corner 102, average of all corners 199
        // best corner 318, second best corner 318, average of all corners 258
        // best corner 309, second best corner 281, average of all corners 189
        // best corner 277, second best corner 190, average of all corners 186
        // best corner 265, second best corner 139, average of all corners 136
        // best corner 258, second best corner 200, average of all corners 231
        // best corner 249, second best corner  81, average of all corners 208
        //
        if ((firstFramePortionQuote <= 787) && (secondFramePortionQuote <= 563) && (allFramePortionQuote <= 420)) {
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): not enought frame pixel found on best corner found, advertising in frame not valid");
            return -1;
        }
    }
    else return -1;

    // check if we have more than one logo
    // no ad in frame, second logo:
    // high matches quote 998, corner portion quote: 557, all corners portion quote: 296
    // high matches quote 997, corner portion quote: 642, all corners portion quote: 262
    // high matches quote 997, corner portion quote: 274, all corners portion quote: 376
    // high matches quote 998, corner portion quote: 417, all corners portion quote: 274
    // high matches quote 994, corner portion quote: 644, all corners portion quote: 285
    // high matches quote 992, corner portion quote: 525, all corners portion quote: 274
    // high matches quote 991, corner portion quote: 507, all corners portion quote: 245
    // high matches quote 982, corner portion quote: 712, all corners portion quote: 252  NEW
    // high matches quote 975, corner portion quote: 447, all corners portion quote: 199
    // high matches quote 947, corner portion quote: 134, all corners portion quote: 111
    // high matches quote 855, corner portion quote: 265, all corners portion quote: 154
    //
    // ad in frame, no second logo:
    // high matches quote 975, corner portion quote: 242, all corners portion quote: 770
    //
    //
    int countLogo = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        int logoQuote         = 1000 * isCornerLogo[corner] / countFrames;
        int framePortionQuote = 0;
        if (AdInFrame.frameCountFinal > 0) framePortionQuote = AdInFrame.sumFramePortionFinal[corner] / AdInFrame.frameCountFinal;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): corner %-12s: %4d high matches, high matches quote %3d: (of %2d frames)", aCorner[corner], isCornerLogo[corner], logoQuote, countFrames);
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo():                    : corner portion quote: %3d (of %3d frames)", framePortionQuote, AdInFrame.frameCountFinal);
        if (corner == maContext->Video.Logo.corner) continue;  // in logo corner we expect logo
        if ((logoQuote >= 855) && (framePortionQuote <= 712) && (allFramePortionQuote <= 376)) {
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
        if ((StillImage.endFinal == AdInFrame.startFinal) && (stillImageLength < 3000)) { // too long detected still images are invalid, changed from 30360 to 5280 to 4320 to 3000
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): move advertising in frame start (%d) to still image start (%d)", AdInFrame.startFinal, StillImage.startFinal);
            AdInFrame.startFinal = StillImage.startFinal;
        }
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no still image before advertising in frame found");

    // check advertising in frame
#define AD_IN_FRAME_LENGTH_MAX 34680  // changed from 30720 to 34680
#define AD_IN_FRAME_LENGTH_MIN  6960  // shortest ad in frame found 6960ms, changed from 8880 to 6960
    int startOffset = 1000 * (AdInFrame.startFinal - startPos) / maContext->Video.Info.framesPerSecond;
    int stopOffset  = 1000 * (endPos - AdInFrame.endFinal) / maContext->Video.Info.framesPerSecond;
    int length      = 1000 * (AdInFrame.endFinal - AdInFrame.startFinal) / maContext->Video.Info.framesPerSecond;
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start (%d) end (%d)", AdInFrame.startFinal, AdInFrame.endFinal);
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start offset %dms (expect <=%dms for start marks), stop offset %dms (expect <=%dms for stop mark), length %dms (expect >=%dms and <=%dms)", startOffset, AD_IN_FRAME_START_OFFSET_MAX, stopOffset, AD_IN_FRAME_STOP_OFFSET_MAX, length, AD_IN_FRAME_LENGTH_MIN, AD_IN_FRAME_LENGTH_MAX);

    if ((length >= AD_IN_FRAME_LENGTH_MIN) && (length <= AD_IN_FRAME_LENGTH_MAX)) { // do not reduce min to prevent false positive, do not increase to detect 10s ad in frame
        if ((isStartMark && (startOffset <= AD_IN_FRAME_START_OFFSET_MAX)) ||      // an ad in frame with logo after start mark must be near start mark, changed from 5 to 4
           (!isStartMark && (stopOffset  <= AD_IN_FRAME_STOP_OFFSET_MAX))) {       // an ad in frame with logo before stop mark must be near stop mark
                                                                                   // maybe we have a preview direct after ad in frame and missed stop mark
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): this is a advertising in frame with logo");
            if (isStartMark) retFrame = AdInFrame.endFinal;
            else retFrame = AdInFrame.startFinal;
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
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): skip this channel");
        return -1;
    }

    struct introductionLogo {
        int start            = -1;
        int end              = -1;
        int startFinal       = -1;
        int endFinal         = -1;
        int countDark        =  0;
        int countDarkFinal   =  0;
        int countFrames      =  0;
        int countFramesFinal =  0;
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

#define INTRODUCTION_MIN_LENGTH          4961  // changed from 4320 to 4961, prevent to get separator scene with length 4960ms before broadcast start
#define INTRODUCTION_MAX_LENGTH         13040  // changed from 10559  to 13040
                                               // found introduction logo with length 13040, try to fix preview problem later TODO
                                               // do not increase to prevent to detect preview as introduction logo
#define INTRODUCTION_MAX_DIFF_SEPARATOR 10119  // max distance from sepatator frame to introduction logo start, changed from 12480 to 10119
                                               // prevent to get ad in frame 10120 before broadcast start as introduction logo
                                               // somtime broacast start without logo before intruduction logo
                                               // sometime we have a undetected info logo and separtion frame is far before
#define INTRODUCTION_MAX_DIFF_END       4319   // max distance of introduction logo end to start mark (endPos)

   for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): frame (%5d) and (%5d) matches %5d %5d %5d %5d", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).rate[1], (*cornerResultIt).rate[2], (*cornerResultIt).rate[3]);
#endif

        // separator frame before introduction logo
        int sumPixel        = 0;
        int countNoMatch    = 0;
        int darkCorner      = 0;
        int countNoPixel    = 0;
        int countLow        = 0;
        int countStillImage = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner]  <=  0) {
                countNoMatch++;
                if (corner != maContext->Video.Logo.corner) darkCorner++;   // if we have no match, this can be a too dark corner
            }
            if ((*cornerResultIt).rate[corner]  <   0) countNoPixel++;
            if ((*cornerResultIt).rate[corner]  <= 65) countLow++;
            if (((*cornerResultIt).rate[corner] <=  0) || ((*cornerResultIt).rate[corner] > 507)) countStillImage++; // changed from 142 to 507
            sumPixel += (*cornerResultIt).rate[corner];
        }
        if (darkCorner == 3) introductionLogo.countDark++;  // if all corners but logo corner has no match, this is a very dark scene
        introductionLogo.countFrames++;
        // examples of separator frames before introduction logo
        //  59     0     0    -1 =  58
        //  48    14     0     0 =  62  NEW
        //  -1   325(l)  0     0 = 324  // fading out logo on black sceen between broadcast before and introduction logo, logo start mark is after introduction logo (conflict)
        //  -1  1000(l) -1    -1 = 997  // black screen with logo, last frame from previous broadcast
        //
        // example of no separator frames (l = logo corner)
        //  34   206(l)   0     0 = 240
        //   0   201(l)  30     0 = 231
        //   0    35(l) 109     0 = 144
        //   0   147(l)   0     0 = 147
        //   0    91(l)   0     0 =  91
        //   0    74(l)   0     0 =  74
        //   0    65(l)   0    -1 =  64
        // new separator image before introduction logo, restart detection
        if (((countNoMatch == 2) && (sumPixel <   63)) ||
            ((countNoMatch >= 3) && (sumPixel <   64)) ||  // changed from 74 to 64
            ((countNoPixel == 3) && (sumPixel == 997) && (introductionLogo.start == -1))) {  // special case blackscreen with logo, end of previous broadcast
                                                                                             // but not if we have a detected seperator, prevent false detection of black screen in boradcast
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
            dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator before introduction found at frame (%5d)", (*cornerResultIt).frameNumber1);
#endif
            separatorFrameBefore = (*cornerResultIt).frameNumber1;
            introductionLogo.start            = -1;
            introductionLogo.end              = -1;
            introductionLogo.startFinal       = -1;
            introductionLogo.endFinal         = -1;
            introductionLogo.countDark        =  0;
            introductionLogo.countDarkFinal   =  0;
            introductionLogo.countFrames      =  0;
            introductionLogo.countFramesFinal =  0;
            separatorFrameAfter               = -1;
            stillImage.start                  = -1;
            stillImage.startFinal             = -1;
            stillImage.end                    = -1;
            stillImage.endFinal               = -1;
            continue;
        }

        // separator after introduction logo, in this case it can not be a introduction logo
        // examples of separator frames after introduction logo
        // no seperator frame
        //  0    76     11     6 =  93
        //
        //  0    24    102     9 = 135
        //  0   109(l)   0    15 = 124
        //  2    79(l)   0     0 =  81  NEW
        //
        //  0   540      0     0 = 540  dark scene with introduction logo (conflict)
        //  0   147      0     0 = 147
        //  0    91      0     0 =  91
        //  0    74(l)   0     0 =  74
        //  0    65      0    -1 =  64
        //
        // separator frame
        //  0     0     0     0 =   0
        //  0     1    33     0 =  34
        // 52     0   114     0 = 166  (conflict)
        //  3     7    66    29 = 105
        if ((separatorFrameBefore >= 0) &&
           (((countNoMatch == 0) && (sumPixel <= 105)) ||
            ((countNoMatch == 1) && (sumPixel <   93)) ||
            ((countNoMatch == 2) && (sumPixel <   81)) ||   // changed from 124 to 81
            ((countNoMatch >= 3) && (sumPixel <   64)))) {  // changed from 74 to 64
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
            dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator after introduction found at frame (%5d)", (*cornerResultIt).frameNumber1);
#endif
            separatorFrameAfter = (*cornerResultIt).frameNumber1;
        }

        // detect still image
        if ((separatorFrameBefore >= 0) && (introductionLogo.start >= 0) && (countStillImage >= 4)) { // still image or closing credists after introduction logo
                                                                                                      // countStillImage: changed from 3 to 4
            if (stillImage.start == -1) {
                stillImage.start = (*cornerResultIt).frameNumber1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
//                dsyslog("cDetectLogoStopStart::IntroductionLogo(): still image start at frame (%5d)", stillImage.start);
#endif
            }
            stillImage.end = (*cornerResultIt).frameNumber2;
        }
        else {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
//            if (stillImage.end >= 0) dsyslog("cDetectLogoStopStart::IntroductionLogo(): still image end at frame (%5d)", stillImage.end);
#endif
            if ((stillImage.end - stillImage.start) >= (stillImage.endFinal - stillImage.startFinal)) {
                stillImage.startFinal = stillImage.start;
                stillImage.endFinal   = stillImage.end;
            }
            stillImage.start = -1;
            stillImage.end   = -1;
        }

        // detect introduction logo
        if (separatorFrameBefore >= 0) { // introduction logo start is after seperator frame
            if ((*cornerResultIt).rate[maContext->Video.Logo.corner] > 99) { // changed from 135 to 99 (change from one intro logo to another intro logo, kabel eins)
                if (introductionLogo.start == -1) introductionLogo.start = (*cornerResultIt).frameNumber1;
                introductionLogo.end = (*cornerResultIt).frameNumber2;
            }
            else {
                if ((introductionLogo.end - introductionLogo.start) >= (introductionLogo.endFinal - introductionLogo.startFinal)) {
                    introductionLogo.startFinal       = introductionLogo.start;
                    introductionLogo.endFinal         = introductionLogo.end;
                    introductionLogo.countDarkFinal   = introductionLogo.countDark;
                    introductionLogo.countFramesFinal = introductionLogo.countFrames;
                }
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): end of introduction logo without seperator because too low logo corner match");
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): current      range: from (%d) to (%d), frames %d, dark %d", introductionLogo.start, introductionLogo.end, introductionLogo.countFrames, introductionLogo.countDark);
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): current best range: from (%d) to (%d), frames %d, dark %d", introductionLogo.startFinal, introductionLogo.endFinal, introductionLogo.countFramesFinal, introductionLogo.countDarkFinal);
#endif
                introductionLogo.start       = -1;
                introductionLogo.end         = -1;
                introductionLogo.countDark   =  0;
                introductionLogo.countFrames =  0;
            }
        }
    }

    // set final detected introduction logo
    if ((introductionLogo.end - introductionLogo.start) >= (introductionLogo.endFinal - introductionLogo.startFinal)) {
        introductionLogo.startFinal       = introductionLogo.start;
        introductionLogo.endFinal         = introductionLogo.end;
        introductionLogo.countDarkFinal   = introductionLogo.countDark;
        introductionLogo.countFramesFinal = introductionLogo.countFrames;
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
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator frame after introduction logo found (%d)", separatorFrameAfter);
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
    // check dark frames, detection with a long dark scene before logo start mark is not possible, maybe closing credits from broadcast before
    int darkQuote = 0;
    if (introductionLogo.countFramesFinal > 0) darkQuote = 1000 * introductionLogo.countDarkFinal / introductionLogo.countFramesFinal;
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), count frames %d, dark frames %d, quote %d", introductionLogo.startFinal, introductionLogo.endFinal,introductionLogo.countFramesFinal, introductionLogo.countDarkFinal, darkQuote);
    if (darkQuote >= 210) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): scene before logo start mark too dark");
        return -1;
    }
    // check length and distances
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
