/*
 * evaluate.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "evaluate.h"


extern bool abortNow;


cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(cDecoder *decoderParam, cCriteria *criteriaParam) {
    dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): called");
    decoder  = decoderParam;
    criteria = criteriaParam;
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


void cEvaluateLogoStopStartPair::SetDecoder(cDecoder *decoderParam) {
    if (!decoder) {
        esyslog("cEvaluateLogoStopStartPair::SetDecoder(): invalid decoder");
        return;
    }
    decoder  = decoderParam;
}


// Check logo stop/start pairs
// used by logo change detection
void cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART,  const int packetEndPart, const int iStopA) {
    if (!marks) {
        esyslog(" cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): marks missing");
        return;
    }
    if (!decoder) {
        esyslog(" cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): decoder missing");
        return;
    }

    int frameRate = decoder->GetVideoFrameRate();

#define LOGO_CHANGE_NEXT_STOP_MIN     6840  // in ms, do not increase, 6840ms is the shortest found distance between two logo changes
    // next stop max (=lenght next valid broadcast) found: 1242s
#define LOGO_CHANGE_IS_ADVERTISING_MIN 300  // in s
#define LOGO_CHANGE_IS_BROADCAST_MIN   240  // in s

    dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): start with iStart (%d), chkSTART (%d), packetEndPart (%d), iStopA (%d)", iStart, chkSTART, packetEndPart, iStopA);
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
        if (criteria->IsInfoLogoChannel()) IsInfoLogo(marks, blackMarks, &(*logoPairIterator), iStart, iStopA);
        else logoPairIterator->isInfoLogo = STATUS_DISABLED;

        // check for logo change section
        if (criteria->IsLogoChangeChannel()) IsLogoChange(marks, &(*logoPairIterator), iStart, chkSTART);
        else logoPairIterator->isLogoChange = STATUS_DISABLED;

        // check for closing credits section
        if (criteria->IsClosingCreditsChannel() &&  // only check closing credits in start part for end of broadcast before or in end part
                ((logoPairIterator->stopPosition <= chkSTART) || logoPairIterator->startPosition >= packetEndPart)) IsClosingCredits(marks, &(*logoPairIterator));
        else logoPairIterator->isClosingCredits = STATUS_DISABLED;

        // check for ad in frame
        if (criteria->IsAdInFrameWithLogoChannel()) IsAdInFrame(marks, &(*logoPairIterator));
        else logoPairIterator->isAdInFrameBeforeStop = STATUS_DISABLED;

        // global information about logo pairs
        // mark after pair
        const cMark *markStop_AfterPair = marks->GetNext(logoPairIterator->stopPosition, MT_LOGOSTOP);
        int deltaStopStart = (logoPairIterator->startPosition - logoPairIterator->stopPosition ) / frameRate;
        if (deltaStopStart >= LOGO_CHANGE_IS_ADVERTISING_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: delta %ds (expect >=%ds) is a advertising", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_IS_ADVERTISING_MIN);
            logoPairIterator->isAdInFrameBeforeStop = STATUS_YES;
        }

        // check next stop distance after stop/start pair
        int delta_Stop_AfterPair = 0;
        if (markStop_AfterPair) {  // we have a next logo stop
            delta_Stop_AfterPair = (markStop_AfterPair->position - logoPairIterator->startPosition) / frameRate;
        }
        else {  // this is the last logo stop we have
            if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
                int diff = (chkSTART - logoPairIterator->stopPosition) / frameRate; // difference to current processed frame
                if (diff > LOGO_CHANGE_IS_BROADCAST_MIN) delta_Stop_AfterPair = diff;     // still no stop mark but we are in broadcast
                else delta_Stop_AfterPair = LOGO_CHANGE_NEXT_STOP_MIN; // we can not ignore early stop start pairs because they can be logo changed short after start
            }
        }
        if (delta_Stop_AfterPair >= LOGO_CHANGE_IS_BROADCAST_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs(): ----- stop (%d) start (%d) pair: next stop mark after stop/start pair in %ds (expect >=%ds, start mark is in broadcast)", logoPairIterator->stopPosition, logoPairIterator->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_IS_BROADCAST_MIN);
            logoPairIterator->isStartMarkInBroadcast = 1;
        }
    }

    // check section of stop/start pairs
    // search for part between advertising and broadcast, keep this mark, because it contains the start mark of the broadcast
    //
    for (std::vector<sLogoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->isAdInFrameBeforeStop == STATUS_YES) {  // advertising pair
            std::vector<sLogoStopStartPair>::iterator next1LogoPairIterator = logoPairIterator;
            ++next1LogoPairIterator;
            if (next1LogoPairIterator != logoPairVector.end()) {
                if ((next1LogoPairIterator->isLogoChange == 0) && (next1LogoPairIterator->isStartMarkInBroadcast  == 0)) { // unknown pair
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
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  hasBorderAroundStart   %2d", logoPairIterator->hasBorderAroundStart);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isLogoChange           %2d", logoPairIterator->isLogoChange);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isAdInFrameAfterStart  %2d", logoPairIterator->isAdInFrameAfterStart);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isAdInFrameBeforeStop  %2d", logoPairIterator->isAdInFrameBeforeStop);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isStartMarkInBroadcast %2d", logoPairIterator->isStartMarkInBroadcast);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isInfoLogo             %2d", logoPairIterator->isInfoLogo);
        dsyslog("cEvaluateLogoStopStartPair::CheckLogoStopStartPairs():                  isClosingCredits       %2d", logoPairIterator->isClosingCredits);
    }
    nextLogoPairIterator = logoPairVector.begin();
}


// check if stop/start pair can have ad in frame before or after
//
void cEvaluateLogoStopStartPair::IsAdInFrame(cMarks *marks, sLogoStopStartPair *logoStopStartPair) {
    if (!marks)             return;
    if (!logoStopStartPair) return;
    if (!decoder)           return;

    int frameRate = decoder->GetVideoFrameRate();
    if (frameRate == 0) return;
    int diff = (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / frameRate;
    if ((diff >= 19) && (diff <= 21)) {  // 20s is short ad in broadcast, there is no additional ad in frame before/after
        dsyslog("cEvaluateLogoStopStartPair::IsAdInFrame():            ----- stop (%d) start (%d) pair: %ds is short ad, no ad in frame after stop mark", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, diff);

        logoStopStartPair->isAdInFrameBeforeStop = STATUS_NO;
    }
    // check if there is a hborder aktiv after logo start mark (can be from opening credits or documentations)
    // in this case we can not (and need not) check for frame after start mark
    cMark *hBorderStartBefore = marks->GetPrev(logoStopStartPair->startPosition, MT_HBORDERSTART);
    if (hBorderStartBefore) {
        cMark *hBorderStopAfter = marks->GetNext(hBorderStartBefore->position, MT_HBORDERSTOP);
        if (hBorderStopAfter && (hBorderStopAfter->position > logoStopStartPair->startPosition )) {
            dsyslog("cEvaluateLogoStopStartPair::IsAdInFrame():          ----- stop (%d) start (%d) pair: active hborder from (%d) to (%d) after start found", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, hBorderStartBefore->position, hBorderStopAfter->position);
            logoStopStartPair->isAdInFrameAfterStart = STATUS_NO;
        }
    }
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
void cEvaluateLogoStopStartPair::IsLogoChange(cMarks *marks, sLogoStopStartPair *logoStopStartPair, const int iStart, const int chkSTART) {
    int frameRate = decoder->GetVideoFrameRate();
    if (frameRate == 0) return;
#define LOGO_CHANGE_LENGTH_MIN  3880  // min time in ms of a logo change section, chaned from 10000 to 9400 to 6760 to 5280 to 4401 to 3880
    // do not reduce, we can not detect too short parts
#define LOGO_CHANGE_LENGTH_MAX 19319  // max time in ms of a logo change section, chaned from 21000 to 19319
    // check min length of stop/start logo pair
    int deltaStopStart = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition ) / frameRate;
    dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: length %dms (expect >=%dms <=%dms)",
            logoStopStartPair->stopPosition, logoStopStartPair->startPosition, deltaStopStart, LOGO_CHANGE_LENGTH_MIN, LOGO_CHANGE_LENGTH_MAX);

    // calculate next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / frameRate;
        dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         ????? stop (%d) start (%d) pair: next logo stop mark (%d) distance %dms (expect >=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, markStop_AfterPair-> position, delta_Stop_AfterPair, LOGO_CHANGE_NEXT_STOP_MIN);
    }
    else {  // this is the last logo stop we have
        if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
            int diff = 1000 * (chkSTART - logoStopStartPair->stopPosition) / frameRate; // difference to current processed frame
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
            int deltaStopStartNew = 1000 * (markNextStart->position - logoStopStartPair->stopPosition ) / frameRate;
            if (deltaStopStartNew > LOGO_CHANGE_LENGTH_MAX) {
                dsyslog("cEvaluateLogoStopStartPair::IsLogoChange():         next start mark (%d) too far away",  markNextStart->position);
            }
            else {
                markStop_AfterPair = marks->GetNext(markNextStart->position, MT_LOGOSTOP);
                if (markStop_AfterPair) {  // we have a next logo stop
                    delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / frameRate;
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
        int deltaStartBefore = 1000 * (logoStopStartPair->stopPosition - prevStart->position) / frameRate;
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
void cEvaluateLogoStopStartPair::IsInfoLogo(cMarks *marks, cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int startA, const int iStopA) {
    int frameRate = decoder->GetVideoFrameRate();
    if (frameRate <= 0) {
        esyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): invalid video frame rate %d", frameRate);
        return;
    }
#define LOGO_INFO_LENGTH_MIN  3720  // min time in ms of a info logo section, bigger values than in InfoLogo because of seek to iFrame, changed from 5000 to 4480 to 3720
#define LOGO_INFO_LENGTH_MAX 22480  // max time in ms of a info logo section, changed from 17680 to 22480
    // RTL2 has very long info logos
#define LOGO_INFO_SHORT_BLACKSCREEN_BEFORE_DIFF_MAX 440  // max time in ms no short blackscreen allowed before stop mark, changed from 40 to 440 to 360 to 440
    // no not change, there are info logos direct after very short start logo (440ms before, length 1000ms)
#define LOGO_INFO_SHORT_BLACKSCREEN_LENGTH         1000  // length of a short blackscreen, changed from 1080 to 1000

#define LOGO_INFO_LONG_BLACKSCREEN_BEFORE_DIFF_MAX 2000  // max time in ms no long blackscreen allowed before stop mark, changed from 1920 to 1960 to 2000
#define LOGO_INFO_LONG_BLACKSCREEN_LENGTH          5000  // length of a long blackscreen
#define LOGO_INFO_BROADCAST_AFTER_MIN              1160  // min length of broadcast after info logo, changed from 4000 to 1160

// min distance of next logo stop/start pair to merge
// info logo detected as logo, but not fade in/out (kabel eins)
#define LOGO_INFO_NEXT_STOP_MIN                    1760  // changed from 2120 to 1760

    int maxNextStop = 6360;  // changed from 5920 to 6360
    if ((logoStopStartPair->stopPosition > iStopA) || (logoStopStartPair->stopPosition < startA)) maxNextStop = 4560; // avoid merge info logo with logo end mark
    // check length
    int length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / frameRate;
    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: length %dms (expect >=%dms and <=%dms)",
            logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);

    // calculate next stop distance after stop/start pair
    int delta_Stop_AfterPair = 0;
    cMark *markStop_AfterPair = marks->GetNext(logoStopStartPair->stopPosition, MT_LOGOSTOP);
    if (markStop_AfterPair) {  // we have a next logo stop
        delta_Stop_AfterPair = 1000 * (markStop_AfterPair->position - logoStopStartPair->startPosition) / frameRate;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: next stop mark (%d) distance %dms (expect <%dms for merge)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, markStop_AfterPair->position, delta_Stop_AfterPair, maxNextStop);
    }

    // maybe we have a wrong start/stop pair between, check if merge with next pair can help
    if ((length < LOGO_INFO_LENGTH_MIN) ||                                          // too short for info logo
            ((delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair < maxNextStop) &&  // next pair is too near
             (length < 11800))) {                                                   // do not merge big pairs
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): short pair and very near next start mark, try to merge with next pair");

        // try next logo stop/start pair
        cMark *pairNextStart = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTART);
        cMark *pairNextStop  = marks->GetNext(logoStopStartPair->startPosition, MT_LOGOSTOP);
        bool tryNext= true;
        while (tryNext) {
            if (pairNextStart && pairNextStop) {
                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair: stop (%d) start (%d) found", pairNextStop->position, pairNextStart->position);
                // check distance to next logo stop mark after stop/start pair
                int deltaStopAfterPair = 1000 * (pairNextStop->position - logoStopStartPair->startPosition) / frameRate;
                if (deltaStopAfterPair < LOGO_INFO_NEXT_STOP_MIN) {
                    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %dms too short, (expect >=%ds <=%ds), try next", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, maxNextStop);
                    pairNextStart = marks->GetNext(pairNextStart->position, MT_LOGOSTART);
                    pairNextStop  = marks->GetNext(pairNextStop->position, MT_LOGOSTOP);
                }
                else {
                    if (deltaStopAfterPair > maxNextStop) {
                        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): distance of next logo stop %dms too big, (expect >=%ds <=%ds)", delta_Stop_AfterPair, LOGO_INFO_NEXT_STOP_MIN, maxNextStop);
                        tryNext = false;
                    }
                    else {
                        // check length of merged pair
                        int lengthNew = 1000 * (pairNextStart->position - logoStopStartPair->stopPosition ) / frameRate;
                        if ((lengthNew < LOGO_INFO_LENGTH_MIN) || (lengthNew > LOGO_INFO_LENGTH_MAX)) {
                            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): merged pair would have no valid length %dms", lengthNew);
                            tryNext = false;
                        }
                        else {
                            int lengthNext = 1000 * (pairNextStart->position - pairNextStop->position) / frameRate;
                            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): length of next pair: %dms", lengthNext);
                            if ((length <= 560) && (lengthNext <= 840)) { // short invisible parts between/after previews
                                // length     changed from 120 to 160 to 200 to 560
                                // lengthNext changed from 160 to 840
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): this pair and next pair are very short, they are logo invisible parts after previews, do not merge");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            if (lengthNext <= 440) {  // changed from 320 to 440
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair is very short, this is the part between broadcast start and info logo, it contains a valid start mark");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            if (lengthNext > LOGO_INFO_LENGTH_MIN) {
                                dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo(): next pair is long enough to be info logo, do not merge");
                                logoStopStartPair->isLogoChange = STATUS_NO;
                                return;
                            }
                            logoStopStartPair->startPosition = pairNextStart->position;
                            length = 1000 * (logoStopStartPair->startPosition - logoStopStartPair->stopPosition) / frameRate;  // length of merged pair
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
        int diff = 1000 * (nextStop->position - logoStopStartPair->startPosition) / frameRate;
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
    cMark *blackStart = nullptr;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART);     // start mark, bleckscreen end
    if (blackStop && blackStart && (logoStopStartPair->stopPosition >= blackStart->position)) {
        int diff = 1000 * (logoStopStartPair->stopPosition - blackStart->position) / frameRate;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / frameRate;
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
    blackStart = nullptr;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART);
    if (blackStop && blackStart && (blackStop->position <= logoStopStartPair->startPosition) && (blackStart->position <= logoStopStartPair->startPosition)) {
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / frameRate;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: in between blacksceen (%d) and (%d) length %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack);
        if (lengthBlack > 1240) {  // changed from 400 to 1240
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():      ----- stop (%d) start (%d) pair: in between blacksceen pair long, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
    }
    // check blackscreen around stop (blackscreen starts before logo stop and ends between logo stop and start)
    blackStop = blackMarks->GetPrev(logoStopStartPair->stopPosition + 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
    blackStart = nullptr;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
    if (blackStop && blackStart && (blackStart->position >= logoStopStartPair->stopPosition) && (blackStart->position <= logoStopStartPair->startPosition)) {
        int diff = 1000 * (blackStart->position - logoStopStartPair->stopPosition) / frameRate;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / frameRate;
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
    blackStart = nullptr;
    if (blackStop) blackStart = blackMarks->GetNext(blackStop->position, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
    if (blackStop && blackStart && (blackStart->position >= logoStopStartPair->startPosition) && (blackStop->position >= logoStopStartPair->stopPosition)) {
        int diff = 1000 * (blackStart->position - logoStopStartPair->startPosition) / frameRate;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / frameRate;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen around start (%d) and (%d) length %dms, diff %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
        if ((lengthBlack > 1200) && (diff < 1200)) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ----- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
    }
    // check blackscreen after stop/start
    // if direct after logo start is a blackscreen mark stop/start pair, this logo stop is a valid stop mark with closing credits after
    blackStop  = blackMarks->GetNext(logoStopStartPair->startPosition - 1, MT_NOBLACKSTOP);  // blackscreen can stop at the same position as logo stop
    blackStart = blackMarks->GetNext(logoStopStartPair->startPosition - 1, MT_NOBLACKSTART); // blackscreen can start at the same position as logo stop
    if (blackStop && blackStart) {
        int diff = 1000 * (blackStart->position - logoStopStartPair->startPosition) / frameRate;
        int lengthBlack = 1000 * (blackStart->position - blackStop->position) / frameRate;
        dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: blacksceen after (%d) and (%d) length %dms, diff %dms", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, blackStop->position, blackStart->position, lengthBlack, diff);
        if ((lengthBlack > 2000) && (diff < 10)) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ---- stop (%d) start (%d) pair: blacksceen pair long and near, no info logo part", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
            logoStopStartPair->isInfoLogo = STATUS_NO;
            return;
        }
    }
    dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           +++++ stop (%d) start (%d) pair: possible info logo section found, length  %dms (expect >=%dms and <=%dms)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, length, LOGO_INFO_LENGTH_MIN, LOGO_INFO_LENGTH_MAX);
    logoStopStartPair->isInfoLogo = STATUS_UNKNOWN;
    // this is a possible info logo stop/start pair, check if there is a hborder aktiv (can be from documentations)
    // in this case we can not (and need not) check for frame or still picture
    cMark *hBorderStartBefore = marks->GetPrev(logoStopStartPair->stopPosition, MT_HBORDERSTART);
    if (hBorderStartBefore) {
        cMark *hBorderStopAfter = marks->GetNext(hBorderStartBefore->position, MT_HBORDERSTOP);
        if (hBorderStopAfter && (hBorderStopAfter->position >= logoStopStartPair->startPosition)) {
            dsyslog("cEvaluateLogoStopStartPair::IsInfoLogo():           ????? stop (%d) start (%d) pair: active hborder from (%d) to (%d) found", logoStopStartPair->stopPosition, logoStopStartPair->startPosition, hBorderStartBefore->position, hBorderStopAfter->position);
            logoStopStartPair->hasBorderAroundStart = true;
        }
    }
    return;
}


bool cEvaluateLogoStopStartPair::GetNextPair(sLogoStopStartPair *logoStopStartPair) {
    if (!logoStopStartPair) return false;
    if (nextLogoPairIterator == logoPairVector.end()) return false;

    // skip pair if there is nothing to detect
    while ((nextLogoPairIterator->isLogoChange <= STATUS_NO) && (nextLogoPairIterator->isInfoLogo <= STATUS_NO) && (nextLogoPairIterator->isAdInFrameBeforeStop <= STATUS_NO) && (nextLogoPairIterator->isClosingCredits <= STATUS_NO)) {
        ++nextLogoPairIterator;
        if (nextLogoPairIterator == logoPairVector.end()) return false;
    }
    logoStopStartPair->stopPosition           = nextLogoPairIterator->stopPosition;
    logoStopStartPair->startPosition          = nextLogoPairIterator->startPosition;
    logoStopStartPair->hasBorderAroundStart   = nextLogoPairIterator->hasBorderAroundStart;
    logoStopStartPair->isLogoChange           = nextLogoPairIterator->isLogoChange;
    logoStopStartPair->isInfoLogo             = nextLogoPairIterator->isInfoLogo;
    logoStopStartPair->isAdInFrameAfterStart  = nextLogoPairIterator->isAdInFrameAfterStart;
    logoStopStartPair->isAdInFrameBeforeStop  = nextLogoPairIterator->isAdInFrameBeforeStop;
    logoStopStartPair->isClosingCredits       = nextLogoPairIterator->isClosingCredits;
    logoStopStartPair->isStartMarkInBroadcast = nextLogoPairIterator->isStartMarkInBroadcast;
    ++nextLogoPairIterator;
    dsyslog("cEvaluateLogoStopStartPair::GetNextPair(): stopPosition (%d), startPosition (%d)", logoStopStartPair->stopPosition, logoStopStartPair->startPosition);
    return true;
}


int cEvaluateLogoStopStartPair::GetIsAdInFrame(const int stopPosition, const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [stopPosition, startPosition](sLogoStopStartPair const &value) ->bool {
        if ((value.stopPosition == stopPosition) || (value.startPosition == startPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        if (found->stopPosition == stopPosition) {
            dsyslog("cEvaluateLogoStopStartPair::GetIsAdInFrame(): isAdInFrameBeforeStop for stop (%d) start (%d) mark: %d", found->stopPosition, found->startPosition, found->isAdInFrameBeforeStop);
            return found->isAdInFrameBeforeStop;
        }
        else {
            dsyslog("cEvaluateLogoStopStartPair::GetIsAdInFrame(): isAdInFrameAfterStart for stop (%d) start (%d) mark: %d", found->stopPosition, found->startPosition, found->isAdInFrameAfterStart);
            return found->isAdInFrameAfterStart;;
        }
    }
    dsyslog("cEvaluateLogoStopStartPair::GetIsAdInFrame(): stop (%d) and start (%d) mark not found", stopPosition, startPosition);
    return STATUS_UNKNOWN;
}


void cEvaluateLogoStopStartPair::SetIsAdInFrameAroundStop(const int stopPosition, const int state) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [stopPosition](sLogoStopStartPair const &value) ->bool { if (value.stopPosition == stopPosition) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::SetIsAdInFrameAroundStop(): set isAdInFrameBeforeStop for stop (%d) to %d", found->stopPosition, state);
        found->isAdInFrameBeforeStop = state;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsAdInFrameAroundStop(): stop (%d) not found, add in list with state %d", stopPosition, state);
    sLogoStopStartPair newPair;
    newPair.stopPosition = stopPosition;
    newPair.isAdInFrameBeforeStop  = state;
    logoPairVector.push_back(newPair);
    ALLOC(sizeof(sLogoStopStartPair), "logoPairVector");
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
void cEvaluateLogoStopStartPair::SetIsClosingCredits(const int stopPosition, const int startPosition, const int endClosingCreditsPosition, const int64_t endClosingCreditsPTS, const eEvaluateStatus state) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition >= startPosition) && (value.stopPosition <= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): set isClosingCredits for stop (%d) start (%d) pair to %d", found->stopPosition, found->startPosition, state);
        found->endClosingCredits.position = endClosingCreditsPosition;
        found->endClosingCredits.pts      = endClosingCreditsPTS;
        found->isClosingCredits           = state;
        return;
    }
    dsyslog("cEvaluateLogoStopStartPair::SetIsClosingCredits(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
}


int cEvaluateLogoStopStartPair::GetIsClosingCreditsBefore(const int startPosition) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition](sLogoStopStartPair const &value) ->bool { if (value.startPosition == startPosition) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCreditsBefore(): isClosingCredits before start (%d) mark: %d", found->startPosition, found->isClosingCredits);
        return found->isClosingCredits;
    }

    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCreditsBefore(): start (%d) mark not found", startPosition);
    return STATUS_ERROR;
}


int cEvaluateLogoStopStartPair::GetIsClosingCreditsAfter(const int stopPosition) {
    if (logoPairVector.empty()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCreditsAfter(): logoPairVector is empty");
        return STATUS_UNKNOWN;
    }
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [stopPosition](sLogoStopStartPair const &value) ->bool { if (value.stopPosition == stopPosition) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCreditsAfter(): isClosingCredits after stop (%d) mark: %d", found->stopPosition, found->isClosingCredits);
        return found->isClosingCredits;
    }

    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCreditsAfter(): stop (%d) mark not found", stopPosition);
    return STATUS_ERROR;
}


void cEvaluateLogoStopStartPair::AddAdInFrame(const int startPosition, const int stopPosition) {
    dsyslog("cEvaluateLogoStopStartPair::AddAdInFrame(): add pair for adinframe at start (%d) stop (%d)", startPosition, stopPosition);
    sLogoStopStartPair newPair;
    newPair.startPosition = startPosition;
    newPair.stopPosition  = stopPosition;
    newPair.isAdInFrameBeforeStop   = STATUS_YES;
    logoPairVector.push_back(newPair);
    ALLOC(sizeof(sLogoStopStartPair), "logoPairVector");
}


enum eEvaluateStatus cEvaluateLogoStopStartPair::GetIsClosingCredits(const int stopPosition, const int startPosition, sMarkPos *endClosingCredits) {
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition >= startPosition) && (value.stopPosition <= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end()) {
        dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): isClosingCredits for stop (%d) start (%d) pair: %d", found->stopPosition, found->startPosition, found->isClosingCredits);
        if (endClosingCredits) {
            endClosingCredits->position = found->endClosingCredits.position;
            endClosingCredits->pts      = found->endClosingCredits.pts;
        }
        return found->isClosingCredits;
    }

    dsyslog("cEvaluateLogoStopStartPair::GetIsClosingCredits(): stop (%d) start (%d) pair not found", stopPosition, startPosition);
    return STATUS_ERROR;
}


bool cEvaluateLogoStopStartPair::IncludesInfoLogo(const int stopPosition, const int startPosition) {
    dsyslog("cEvaluateLogoStopStartPair::IncludesInfoLogo(): check if start mark (%d) and stop mark (%d) includes info logo", startPosition, stopPosition);
    std::vector<sLogoStopStartPair>::iterator found = std::find_if(logoPairVector.begin(), logoPairVector.end(), [startPosition, stopPosition](sLogoStopStartPair const &value) ->bool { if ((value.startPosition <= startPosition) && (value.stopPosition >= stopPosition)) return true; else return false; });

    if (found != logoPairVector.end() && (found->startPosition >= 0) && (found->stopPosition >= 0)) {  // there can be incomplete dummy entries
        dsyslog("cEvaluateLogoStopStartPair::IncludesInfoLogo(): stop (%d) start (%d) pair includes info logo for stop (%d) start (%d) pair", stopPosition, startPosition, found->stopPosition, found->startPosition);
        return true;
    }

    return false;
}


cDetectLogoStopStart::cDetectLogoStopStart(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam, cEvaluateLogoStopStartPair *evaluateLogoStopStartPairParam, const int logoCornerParam) {
    decoder                   = decoderParam;
    index                     = indexParam;
    criteria                  = criteriaParam;
    evaluateLogoStopStartPair = evaluateLogoStopStartPairParam;
    logoCorner                = logoCornerParam;
    // closing credits detection without logo is valid
    if ((logoCorner < -1) || (logoCorner >= CORNERS)) esyslog("cDetectLogoStopStart::cDetectLogoStopStart(): invalid logo corner %d", logoCorner);

    sobel = new cSobel(decoder->GetVideoWidth(), decoder->GetVideoHeight(), 0);  // boundary = 0
    ALLOC(sizeof(*sobel), "sobel");
    // area.logoSize not set, alloc memory max size for this resolution
    if (!sobel->AllocAreaBuffer(&area)) esyslog("cDetectLogoStopStart::cDetectLogoStopStart(): allocate buffer for area failed");
}


cDetectLogoStopStart::~cDetectLogoStopStart() {
#ifdef DEBUG_MEM
    int size = compareResult.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sCompareInfo), "compareResult");
    }
#endif
    compareResult.clear();

    sobel->FreeAreaBuffer(&area);
    FREE(sizeof(*sobel), "sobel");
    delete sobel;
}


int cDetectLogoStopStart::FindFrameFirstPixel(const uchar *picture, const int corner, const int width, const int height, int startX, int startY, const int offsetX, const int offsetY) {

    // set start coordinates to detect boundary
#define X_BOUNDARY 5   // ignore pixel at left and right  edge, can be a very small vertical    border
#define Y_BOUNDARY 5   // ignore pixel at top  and bottom edge, can be a very small horizonatal border
    startX = std::max(startX, X_BOUNDARY);
    startX = std::min(startX, width - X_BOUNDARY);
    startY = std::max(startY, Y_BOUNDARY);
    startY = std::min(startY, height - Y_BOUNDARY);

    int foundX        = -1;
    int foundY        = -1;

#ifdef DEBUG_FRAME_DETECTION
    int searchX       = startX;   // keep startX for debug function
    int searchY       = startY;   // keep startY for debug function
#endif

    while ((startX >= X_BOUNDARY) && (startX <= (width - X_BOUNDARY) && (startY >= Y_BOUNDARY) && (startY <= (height - Y_BOUNDARY)))) {
        if (picture[startY * width + startX] == 0) {  // pixel found
            foundX = startX;
            foundY = startY;
            break;
        }
        startX += offsetX;
        startY += offsetY;
    }

#ifdef DEBUG_FRAME_DETECTION_PICTURE
    // save plane 0 of sobel transformation
    if ((decoder->GetPacketNumber() >= (DEBUG_FRAME_DETECTION_PICTURE - 10)) && (decoder->GetPacketNumber() <= (DEBUG_FRAME_DETECTION_PICTURE + 10))) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_FindFrameFirstPixel.pgm", decoder->GetRecordingDir(), decoder->GetPacketNumber(), corner) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            sobel->SaveSobelPlane(fileName, picture, width, height);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
    }
#endif

#ifdef DEBUG_FRAME_DETECTION
    dsyslog("-----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("cDetectLogoStopStart::FindFrameFirstPixel(): frame (%d) corner %d: start (%d,%d), direction (%d,%d): found (%d,%d)", decoder->GetPacketNumber(), corner, searchX, searchY, offsetX, offsetY, foundX, foundY);
#endif

    // future search direction depends on corner
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

    if ((foundX >= 0) && (foundY >= 0)) return FindFrameStartPixel(picture, width, height, foundX, foundY, -searchOffsetX, -searchOffsetY);
    return 0;
}


// search for corner of frame
int cDetectLogoStopStart::FindFrameStartPixel(const uchar *picture, const int width, const int height,  int startX, int startY, const int offsetX, const int offsetY) {
    int pixelError  = 0;   // accept missing pixel in the frame from detection errors
    int firstPixelX = startX;
    int firstPixelY = startY;
    while ((startX > 0) && (startX < width - 1)) {
        if (picture[startY * width + startX + offsetX] == 0) startX += offsetX;  // next position has pixel
        else {
            pixelError++;
            if (pixelError > 1) {
                startX -= offsetX;  // back to last valid position
                break;
            }
            startX += offsetX;
        }
    }
    pixelError  = 0;   // accept missing pixel in the frame from detection errors
    while ((startY > 0) && (startY < height - 1)) {
        if (picture[(startY + offsetY) * width + startX] == 0) startY += offsetY;  // next position has pixel
        else {
            pixelError++;
            if (pixelError > 1) {
                startY -= offsetY;  // back to last valid position
                break;
            }
            startY += offsetY;
        }
    }
    if (abs(firstPixelX - startX) > abs(firstPixelY - startY)) {
        startY = firstPixelY;  // prevent to get out of line from a single pixel, only one coordinate can change
    }
    else startX = firstPixelX;
#ifdef DEBUG_FRAME_DETECTION
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
    while (((*endX + pixelError + offsetX) >= 0) && ((*endX + pixelError + offsetX) < width)) {
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
    while (((*endY + pixelError + offsetY) >= 0) && ((*endY + pixelError + offsetY) < height)) {
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
#ifdef DEBUG_FRAME_DETECTION
    dsyslog("cDetectLogoStopStart::FindFrameEndPixel(): search for end pixel: direction (%2d,%2d): found frame from (%d,%d) to (%d,%d) and (%d,%d), length (%d,%d) -> portion %d", offsetX, offsetY, startX, startY, startX, *endX, startY, *endY, lengthX, lengthY, portion);
#endif
    return portion;
}


// detect frame in sobel transformed picture
// sobel transformes lines can be interrupted, try also pixel next to start point
// return portion of frame pixel in picture
int cDetectLogoStopStart::DetectFrame(const uchar *picture, const int width, const int height, const int corner) {
    if (!picture) return 0;

    int portion =  0;

    switch (corner) {
    case TOP_LEFT:
        portion = FindFrameFirstPixel(picture, corner, width, height, 0, 0, 1, 1);                                   // search from top left to bottom right
        // maybe we have a text under frame or the logo
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width / 2, 0, 1, 1));        // search from top mid to bottom right
        // maybe we have a text under frame or the logo
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height / 2, 1, 0));       // search horizontal mid right mid left
        // maybe we have a text under frame or the logo
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height * 9 / 10, 1, 0));  // search horizontal from 9/10 button left to right
        break;

    case TOP_RIGHT:
        portion = FindFrameFirstPixel(picture, corner, width, height, width - 1, 0, -1, 1);                    // search from top right to bottom left
        // try to get top line of frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, 0, 1, 1));          // search from top left to bottom right
        // maybe we have a text right of frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width / 2, 0, -1, 1)); // search from top mid to bottom left
        // maybe we have a text right of frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height, 1, -1));    // search from bottom left to top right
        break;

    case BOTTOM_LEFT:
        portion = FindFrameFirstPixel(picture, corner, width, height, 0, height - 1, 1, -1);                            // search from buttom left to top right
        // maybe we have a text under frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width / 2, height - 1, 1, -1)); // search from bottom mid to top right
        // maybe we have only a part of the frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width / 3, height - 1, 1, -1)); // search from bottom 1/3 left to top right
        // maybe we have only a part of the frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height / 2, 1, -1));         // search from mid right  to top right
        // maybe we have only a part of the frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width, 0, -1, 1));              // search from top right to buttom left
        break;

    case BOTTOM_RIGHT:
        portion = FindFrameFirstPixel(picture, corner, width, height, width - 1, height - 1, -1, -1);                    // search from buttom right to top left
        // maybe we have a text right from frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height - 1, 1, -1));          // search from buttom left to top right
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, 0, 1, 1));                    // search from top left to buttom right
        // sixx, text and timer right from frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, 0, height / 2, 1, -1));          // search from mid left to topright
        // maybe we have a text under frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width - 1, height / 2, -1, -1)); // search from mid right to top left
        // maybe we have a text under frame
        portion = std::max(portion, FindFrameFirstPixel(picture, corner, width, height, width / 2, height - 1, -1, -1)); // search from buttom mid to top left
        break;
    default:
        return 0;
    }

#ifdef DEBUG_FRAME_DETECTION
    dsyslog("cDetectLogoStopStart::DetectFrame(): frame (%5d) corner %d: portion %3d", decoder->GetPacketNumber(), corner, portion);
#endif

    return portion;
}


bool cDetectLogoStopStart::CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int *rate0) {
    if (!logo1) return false;
    if (!logo2) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;

#define RATE_0_MIN     250
#define RATE_12_MIN    950

    int similar_0 = 0;
    int similar_1_2 = 0;
    int oneBlack_0 = 0;
    int rate_0 = 0;
    int rate_1_2 = 0;
    for (int i = 0; i < logoHeight*logoWidth; i++) {    // compare all black pixel in plane 0
        if ((logo1->sobel[0][i] == 255) && (logo2->sobel[0][i] == 255)) continue;   // ignore white pixel
        else oneBlack_0 ++;
        if (logo1->sobel[0][i] == logo2->sobel[0][i]) {
            similar_0++;
        }
    }
    for (int i = 0; i < logoHeight / 2 * logoWidth / 2; i++) {    // compare all pixel in plane 1 and 2
        for (int plane = 1; plane < PLANES; plane ++) {
            if (logo1->sobel[plane][i] == logo2->sobel[plane][i]) similar_1_2++;
        }
    }
#define MIN_BLACK_PLANE_0 100
    if (oneBlack_0 > MIN_BLACK_PLANE_0) rate_0 = 1000 * similar_0 / oneBlack_0;   // accept only if we found some pixels
    else rate_0 = 0;
    if (oneBlack_0 == 0) rate_0 = -1;  // tell calling function, we found no pixel
    rate_1_2 = 1000 * similar_1_2 / (logoHeight * logoWidth) * 2;

    if (rate0) *rate0 = rate_0;

    if ((rate_0 > RATE_0_MIN) && (rate_1_2 > RATE_12_MIN)) {
#ifdef DEBUG_LOGO_CORNER
        if (corner == DEBUG_LOGO_CORNER) dsyslog("cDetectLogoStopStart::CompareLogoPair(): logo ++++ frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->frameNumber, logo2->frameNumber, rate_0, RATE_0_MIN, rate_1_2, RATE_12_MIN, oneBlack_0, MIN_BLACK_PLANE_0);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_LOGO_CORNER
    if (corner == DEBUG_LOGO_CORNER) dsyslog("cDetectLogoStopStart::CompareLogoPair(): logo ---- frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->frameNumber, logo2->frameNumber, rate_0, RATE_0_MIN, rate_1_2, RATE_12_MIN, oneBlack_0, MIN_BLACK_PLANE_0);
#endif
    return false;
}



bool cDetectLogoStopStart::Detect(int startFrame, int endFrame) {
    if (!decoder) return false;
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
    int maxLogoPixel = area.logoSize.width * area.logoSize.height;

    // check if we have anything todo with this channel
    if (!criteria->IsInfoLogoChannel() && !criteria->IsLogoChangeChannel() && !criteria->IsClosingCreditsChannel()
            && !criteria->IsAdInFrameWithLogoChannel() && !criteria->IsIntroductionLogoChannel()) {
        dsyslog("cDetectLogoStopStart::Detect(): channel not in list for special logo detection");
        return false;
    }
    dsyslog("cDetectLogoStopStart::Detect(): detect from (%d) to (%d)", startFrame, endFrame);
    if (!decoder->SeekToPacket(startFrame)) {
        esyslog("cDetectLogoStopStart::Detect(): SeekToPacket (%d) failed", startFrame);
        return false;
    }

    sLogoInfo *logo1[CORNERS];
    sLogoInfo *logo2[CORNERS];
    for (int corner = 0; corner < CORNERS; corner++) {
        logo1[corner] = new sLogoInfo;
        ALLOC(sizeof(*logo1[corner]), "logo");
    }
    dsyslog("cDetectLogoStopStart::Detect(): use logo size %dWx%dH", area.logoSize.width, area.logoSize.height);

    while (decoder->DecodeNextFrame(false)) {
        if (abortNow) return false;

        if (decoder->GetPacketNumber() >= endFrame) break;  // use packet number to prevent overlapping seek (before mark, after mark)

        const sVideoPicture *picture = decoder->GetVideoPicture();
        if (!picture) {
            dsyslog("cDetectLogoStopStart::Detect(): frame (%d): picture not valid", decoder->GetPacketNumber());
            continue;
        }

        sCompareInfo compareInfo;
        for (int corner = 0; corner < CORNERS; corner++) {
            area.logoCorner = corner;
            if (!sobel->SobelPlane(picture, &area, 0)) continue;   // plane 0
#ifdef DEBUG_COMPARE_FRAME_RANGE
            if (corner == DEBUG_COMPARE_FRAME_RANGE) iFrameNumberNext = -2;
#endif

#ifdef DEBUG_MARK_OPTIMIZATION
            // save plane 0 of sobel transformation
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_Detect.pgm", decoder->GetRecordingDir(), frameNumber, corner) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                sobel->SaveSobelPlane(fileName, area.sobel[0], area.logoSize.width, area.logoSize.height);
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
#endif

            compareInfo.framePortion[corner] = DetectFrame(area.sobel[0], area.logoSize.width, area.logoSize.height, corner);

            logo2[corner] = new sLogoInfo;
            ALLOC(sizeof(*logo2[corner]), "logo");
            logo2[corner]->frameNumber = picture->packetNumber;
            logo2[corner]->pts         = picture->pts;

            // alloc memory and copy sobel transformed corner picture
            logo2[corner]->sobel = new uchar*[PLANES];
            for (int plane = 0; plane < PLANES; plane++) {
                logo2[corner]->sobel[plane] = new uchar[maxLogoPixel];
                memcpy(logo2[corner]->sobel[plane], area.sobel[plane], sizeof(uchar) * maxLogoPixel);
            }
            ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "logo[corner]->sobel");

            if (logo1[corner]->frameNumber >= 0) {  // we have a logo pair
                if (CompareLogoPair(logo1[corner], logo2[corner], area.logoSize.height, area.logoSize.width, corner, &compareInfo.rate[corner])) {
                }
            }
            if (corner == 0) {  // set current frame numbers, needed only once
                compareInfo.frameNumber1 = logo1[corner]->frameNumber;
                compareInfo.pts1         = logo1[corner]->pts;
                compareInfo.frameNumber2 = logo2[corner]->frameNumber;
                compareInfo.pts2         = logo2[corner]->pts;
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
    return true;
}


bool cDetectLogoStopStart::IsInfoLogo(int startPos, int endPos, const bool hasBorder) {
    if (!decoder) return false;
    if (compareResult.empty()) return false;
    if ((logoCorner < 0) || (logoCorner >= CORNERS)) {
        esyslog("cDetectLogoStopStart::IsInfoLogo(): invalid logo corner %d", logoCorner);
        return false;
    }

    if (!criteria->IsInfoLogoChannel()) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): skip this channel");
        return false;
    }

    dsyslog("cDetectLogoStopStart::IsInfoLogo(): detect from (%d) to (%d), hasBorder %d", startPos, endPos, hasBorder);
    int frameRate = decoder->GetVideoFrameRate();

    // start and stop frame of assumed info logo section
    struct sInfoLogo {
        int start                     = 0;
        int end                       = 0;
        int startFinal                = 0;
        int endFinal                  = 0;
        int staticCount               = 0;
        int staticCountFinal          = 0;
        int frameCount                = 0;
        int frameCountFinal           = 0;
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
        bool isStatic             = true;

        for (int corner = 0; corner < CORNERS; corner++) {
            if (((corner ==logoCorner) && (*cornerResultIt).rate[corner] > INFO_LOGO_MACTH_MIN)) infoLogo.matchLogoCornerCount++;
            if (((corner !=logoCorner) && (*cornerResultIt).rate[corner] > INFO_LOGO_MACTH_MIN)) infoLogo.matchRestCornerCount++;
            if ((*cornerResultIt).rate[corner] <= 0) countZero++;
            // if all corner has high match, this is a separator picture, changed from 308 to 600, mybe use scene change instead
            if (((*cornerResultIt).rate[corner] > 0) && ((*cornerResultIt).rate[corner] < 600)) isStatic = false;
            sumPixel += (*cornerResultIt).rate[corner];
            if (((*cornerResultIt).rate[corner] <=   0) && (corner !=logoCorner)) darkCorner++;   // if we have no match, this can be a too dark corner
        }
        // dark scene
        countFrames++;
        if (darkCorner == 3) countDark++;  // if all corners but logo corner has no match, this is a very dark scene

        if (((countZero >= 2) && (sumPixel <= 45)) ||     // changed from 60 to 45, too big values results in false detection of a separation image, do not increase
                ((countZero >= 3) && (sumPixel < 122))) { // changed from 132 to 122
            countSeparatorFrame++;
            lastSeparatorFrame = (*cornerResultIt).frameNumber2;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator frame (%d)", lastSeparatorFrame);
#endif
        }

        if (((*cornerResultIt).rate[logoCorner] > INFO_LOGO_MACTH_MIN) || // do not rededuce to prevent false positiv
                // allow 3 lower matches for the change from one info logo to another, changed from 1 to 3
                // limit changed from 142 to 7
                ((*cornerResultIt).rate[logoCorner] >= 7) && (lowMatchCornerCount <= 3)) { // allow 3 lower match for the change from one info logo to another, changed from 1 to 3
            if ((*cornerResultIt).rate[logoCorner] <= INFO_LOGO_MACTH_MIN) {
                lowMatchCornerCount++;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): lowMatchCornerCount %d", lowMatchCornerCount);
#endif
            }
            if (infoLogo.start == 0) {
                infoLogo.start = (*cornerResultIt).frameNumber1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): start info logo at frame (%d)", infoLogo.start);
#endif
            }
            infoLogo.end = (*cornerResultIt).frameNumber2;
            infoLogo.frameCount++;
            if (isStatic) infoLogo.staticCount++;
        }
        else {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INFOLOGO)
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): end info log: start frame (%d), end frame (%d), matchLogoCornerCount %d, matchRestCornerCount (%d), frameCount %d, staticCount %d", infoLogo.start, infoLogo.end, infoLogo.matchLogoCornerCount, infoLogo.matchRestCornerCount, infoLogo.frameCount, infoLogo.staticCount);
#endif
            if ((infoLogo.end - infoLogo.start) > (infoLogo.endFinal - infoLogo.startFinal)) {
                infoLogo.startFinal                = infoLogo.start;
                infoLogo.endFinal                  = infoLogo.end;
                infoLogo.frameCountFinal           = infoLogo.frameCount;
                infoLogo.staticCountFinal          = infoLogo.staticCount;
                infoLogo.matchLogoCornerCountFinal = infoLogo.matchLogoCornerCount;
                infoLogo.matchRestCornerCountFinal = infoLogo.matchRestCornerCount;
            }
            infoLogo.start                = 0;  // reset state
            infoLogo.end                  = 0;
            infoLogo.frameCount           = 0;
            infoLogo.staticCount          = 0;
            infoLogo.matchLogoCornerCount = 0;
            infoLogo.matchRestCornerCount = 0;
            lowMatchCornerCount           = 0;
        }
    }
    if ((infoLogo.end - infoLogo.start) > (infoLogo.endFinal - infoLogo.startFinal)) {
        infoLogo.startFinal                = infoLogo.start;
        infoLogo.endFinal                  = infoLogo.end;
        infoLogo.frameCountFinal           = infoLogo.frameCount;
        infoLogo.staticCountFinal          = infoLogo.staticCount;
        infoLogo.matchLogoCornerCountFinal = infoLogo.matchLogoCornerCount;
        infoLogo.matchRestCornerCountFinal = infoLogo.matchRestCornerCount;
    }

    // check if we have a static preview picture
    int staticQuote = 0;
    if (infoLogo.frameCountFinal > 0) staticQuote = 1000 * infoLogo.staticCountFinal / infoLogo.frameCountFinal;
    dsyslog("cDetectLogoStopStart::IsInfoLogo(): static picture quote %d", staticQuote);
#define MAX_STATIC_QUOTE 991   // changed from 990 to 991
    if (staticQuote >= MAX_STATIC_QUOTE) found = false;

    // check if "no logo" corner has same matches as logo corner, in this case it must be a static scene (e.g. static preview picture in frame or adult warning) and no info logo
    if (found && !hasBorder) {
        infoLogo.matchRestCornerCountFinal /= 3;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): count matches greater than limit of %d: %d logo corner, avg rest corners %d", INFO_LOGO_MACTH_MIN, infoLogo.matchLogoCornerCountFinal, infoLogo.matchRestCornerCountFinal);
        // need 9 for part time static separator picture with blends (kabel eins), but too many false postiv
        // changed from 9 to 3 because of scene in frame (kabel eins)
        if (infoLogo.matchLogoCornerCountFinal <= (infoLogo.matchRestCornerCountFinal + 3)) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): too much similar corners, this must be a static ad or preview picture");
            found = false;
        }
    }

    // check separator image
    if (found) {
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): count separator frames %d", countSeparatorFrame);
        if (countSeparatorFrame > 5) {  // changed from 4 to 5, got more from change from info logo to normal logo
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): too much separator frames, this can not be a info logo");
            found = false;
        }
    }

    // check dark quote
    if (found) {
        int darkQuote = 100 * countDark / countFrames;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): dark quote %d%%", darkQuote);
        if ((lastSeparatorFrame >= 0) && (darkQuote < 44)) {  // on dark scenes we can not detect separator image
            // darkQuote changed from 65 to 44
            int diffSeparator = 1000 * (endPos - lastSeparatorFrame) / frameRate;
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): last separator frame found (%d), %dms before end", lastSeparatorFrame, diffSeparator);
            if (diffSeparator < 1080) { // changed from 1160 to 1080
                // we can get a false detected separator frame in a dark scene during change from info logo to normal logo near logo start
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): separator frame found, this is a valid start mark");
                found = false;
            }
        }
    }

    // check info logo
#define INFO_LOGO_MAX_AFTER_START  6239  // max distance for info logo start to range start (logo stop)
#define INFO_LOGO_MIN_LENGTH       3801  // changed from 3761 to 3801
    // prevent to get info box after preview as info logo, length 3760
#define INFO_LOGO_MAX_LENGTH      17160  // chnaged from 17040 to 17160 (RTL2)
    // RTL2 has very long info logos
#define INFO_LOGO_MIN_QUOTE          71  // changed from 67 to 71, no info logo: separator image with part time logo 70
    if (found) {
        // ignore short parts at start and end, this is fade in and fade out
        int diffStart = 1000 * (infoLogo.startFinal - startPos) / frameRate;
        int diffEnd = 1000 * (endPos - infoLogo.endFinal) / frameRate;
        int newStartPos = startPos;
        int newEndPos = endPos;
        dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo start diff %dms, end diff %dms", diffStart, diffEnd);
        if (diffStart > INFO_LOGO_MAX_AFTER_START) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): info logo start (%d) too far from logo stop (%d)", infoLogo.startFinal, startPos);
            found = false;
        }
        else {
            if (diffStart <  1920) newStartPos = infoLogo.startFinal;  // do not increase
            if (diffEnd   <= 1800) newEndPos   = infoLogo.endFinal;    // changed from 1440 to 1800
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): final info logo range start (%d) end (%d)", newStartPos, newEndPos);
            int quote  =  100 * (infoLogo.endFinal - infoLogo.startFinal) / (newEndPos - newStartPos);
            int length = 1000 * (infoLogo.endFinal - infoLogo.startFinal) / frameRate;
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): detected info logo: start (%d), end (%d), length %dms (expect >=%dms and <=%dms), quote %d%% (expect >= %d%%)", infoLogo.startFinal, infoLogo.endFinal, length, INFO_LOGO_MIN_LENGTH, INFO_LOGO_MAX_LENGTH, quote, INFO_LOGO_MIN_QUOTE);
            if ((length >= INFO_LOGO_MIN_LENGTH) && (length <= INFO_LOGO_MAX_LENGTH) && (quote >= INFO_LOGO_MIN_QUOTE)) {
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): found info logo");
            }
            else {
                dsyslog("cDetectLogoStopStart::IsInfoLogo(): no info logo found");
                found = false;
            }
        }
    }

    // check if it is a closing credit, we may not delete this because it contains end mark
    sMarkPos endClosingCredits = {-1};
    if (found) {
        ClosingCredit(startPos, endPos, &endClosingCredits, true);
        if (endClosingCredits.position >= 0) {
            dsyslog("cDetectLogoStopStart::IsInfoLogo(): stop/start part is closing credit, no info logo");
            found = false;
        }
    }
    else { // we have to check for closing credits anyway, because we use this info to select start or end mark
        if (evaluateLogoStopStartPair && (staticQuote < MAX_STATIC_QUOTE)) ClosingCredit(startPos, endPos, &endClosingCredits);  // do not check if detected static pictures
    }
    return found;
}



// check if part of recording between frame <endPos> and start <startPos> is a logo change part of the recording
// some channels play with changes of the logo in the recording
// a logo change part has same similar frames in the logo corner and different frames in all other corner (test only with one corner)
//
// return: true  if the given part is detected as logo change part
//         false if not
//
bool cDetectLogoStopStart::IsLogoChange(int startPos, int endPos) {
    if (!decoder) return false;
    if (!index) return false;
    if (compareResult.empty()) return false;

    if (!criteria->IsLogoChangeChannel()) {
        dsyslog("cDetectLogoStopStart::isLogoChange(): skip this channel");
        return false;
    }
    dsyslog("cDetectLogoStopStart::isLogoChange(): check logo change between logo stop (%d) and logo start (%d)", startPos, endPos);
    int frameRate = decoder->GetVideoFrameRate();

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
        if ((*cornerResultIt).rate[logoCorner] > 250) {  // match on logo corner
            highMatchCount++;
        }
        if ((*cornerResultIt).rate[logoCorner] > 50) {   // match on logo corner
            lowMatchCount++;
        }
        if ((*cornerResultIt).rate[logoCorner] == 0) {   // no logo in the logo corner
            countNoLogoInLogoCorner++;
        }

        int matchPicture          = 0;
        int matchNoLogoCornerPair = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            matchPicture += (*cornerResultIt).rate[corner];
            match[corner] += (*cornerResultIt).rate[corner];
            if (corner !=logoCorner) {  // all but logo corner
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
                int diffDarkScene = 1000 * ((*cornerResultIt).frameNumber1 - darkSceneStart) / frameRate;
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
    dsyslog("cDetectLogoStopStart::isLogoChange(): rate summery logo corner %5d (expect >=%d), summery other corner %5d, avg other corners %d", match[logoCorner], LOGO_CHANGE_LIMIT, matchNoLogoCorner, static_cast<int>(matchNoLogoCorner / 3));
    if ((lowMatchQuote >= LOGO_LOW_QUOTE_MIN) && (noLogoQuote <= LOGO_QUOTE_NO_LOGO) &&
            ((match[logoCorner] > LOGO_CHANGE_LIMIT) || (highMatchQuote >= LOGO_HIGH_QUOTE_MIN))) {
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
void cDetectLogoStopStart::ClosingCredit(int startPos, int endPos, sMarkPos *endClosingCredits, const bool noLogoCornerCheck) {
    if (!criteria->IsClosingCreditsChannel()) return;

    if (evaluateLogoStopStartPair && evaluateLogoStopStartPair->GetIsClosingCredits(startPos, endPos, nullptr) == STATUS_NO) {
        dsyslog("cDetectLogoStopStart::ClosingCredit(): already known no closing credits from (%d) to (%d)", startPos, endPos);
        return;
    }

    dsyslog("cDetectLogoStopStart::ClosingCredit(): detect from (%d) to (%d)", startPos, endPos);
    int frameRate = decoder->GetVideoFrameRate();

#define CLOSING_CREDITS_LENGTH_MIN 6161  // changed from 6120 to 6161, prevent to detect preview picture as closing credits
    int minLength = (1000 * (endPos - startPos) / frameRate) - 2000;  // 2s buffer for change from closing credit to logo start
    if (minLength <= 3760) { // too short will result in false positive, changed from 1840 to 3760
        dsyslog("cDetectLogoStopStart::ClosingCredit(): length %dms too short for detection", minLength);
        return;
    }
    if (minLength > CLOSING_CREDITS_LENGTH_MIN) minLength = CLOSING_CREDITS_LENGTH_MIN;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): min length %dms", minLength);

    struct sClosingCredits {
        int startPosition            = -1;
        int64_t startPTS             = -1;
        int endPosition              = -1;
        int64_t endPTS               = -1;
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
            if (((*cornerResultIt).rate[corner] >= 205) || ((*cornerResultIt).rate[corner] == -1)) similarCorners++;      // changed from 230 to 205
            if (((*cornerResultIt).rate[corner] >= 260) || ((*cornerResultIt).rate[corner] == -1)) moreSimilarCorners++;  // changed from 715 to 260
            if (((*cornerResultIt).rate[corner] >= 545) || ((*cornerResultIt).rate[corner] == -1)) equalCorners++;        // changed from 605 to 545
            if ( (*cornerResultIt).rate[corner] ==  -1) noPixelCount++;
            if (((*cornerResultIt).rate[corner] <=   0) && (corner != logoCorner)) darkCorner++;   // if we have no match, this can be a too dark corner
        }
        countFrames++;
        if (darkCorner >= 2) countDark++;  // if at least two corners but logo corner has no match, this is a very dark scene

        if ((similarCorners >= 3) && (noPixelCount < CORNERS)) {  // at least 3 corners has a match, at least one corner has pixel
            if (ClosingCredits.startPosition == -1) {
                ClosingCredits.startPosition = (*cornerResultIt).frameNumber1;
                ClosingCredits.startPTS      = (*cornerResultIt).pts1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_CLOSINGCREDITS)
                dsyslog("cDetectLogoStopStart::ClosingCredit(): start");
#endif
            }
            ClosingCredits.endPosition = (*cornerResultIt).frameNumber2;
            ClosingCredits.endPTS      = (*cornerResultIt).pts2;
            ClosingCredits.frameCount++;
            for (int corner = 0; corner < CORNERS; corner++) ClosingCredits.sumFramePortion[corner] += framePortion[corner];
        }
        else {
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_CLOSINGCREDITS)
            if (ClosingCredits.startPosition != -1) dsyslog("cDetectLogoStopStart::ClosingCredit(): end");
#endif
            if ((ClosingCredits.endPosition - ClosingCredits.startPosition) >= (minLength *frameRate / 1000)) {  // first long enough part is the closing credit
                break;
            }
            // restet state
            ClosingCredits.startPosition = -1;
            ClosingCredits.startPTS      = -1;
            ClosingCredits.endPosition   = -1;
            ClosingCredits.endPTS        = -1;
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
        return;
    }

    // check if it is a closing credit
    int startOffset = 1000 * (ClosingCredits.startPosition - startPos)                   / frameRate;
    int endOffset   = 1000 * (endPos - ClosingCredits.endPosition)                       / frameRate;
    int length      = 1000 * (ClosingCredits.endPosition - ClosingCredits.startPosition) / frameRate;
    dsyslog("cDetectLogoStopStart::ClosingCredit(): closing credits: start (%d) end (%d), offset start %dms end %dms, length %dms", ClosingCredits.startPosition, ClosingCredits.endPosition, startOffset, endOffset, length);

    if ((ClosingCredits.startPosition > 0) && (ClosingCredits.endPosition > 0) && // we found something
            (startOffset <= 4320) && (endOffset < 87200) && (length <= 28720) && // do not reduce start offset, if logo fade out, we got start a little too late
            // changed length from 19000 to 28720, long ad in frame between broadcast, detect as closing credit to get correct start mark
            // startOffset increases from 1440 to 4320 because of silence detection before closing credits detection
            ((length >= CLOSING_CREDITS_LENGTH_MIN) || ((endOffset < 480) && length > 1440))) {  // if we check from info logo:
        // - we would not have the complete part, so it should go nearly to end
        //   and need a smaller min length
        // - we also should detect ad in frame
        // changed from <= 1440 to 1920 to < 1200 to 480
        dsyslog("cDetectLogoStopStart::ClosingCredit(): this is a closing credits, pair contains a valid mark");
        endClosingCredits->position = ClosingCredits.endPosition;
        endClosingCredits->pts      = ClosingCredits.endPTS;
    }
    else dsyslog("cDetectLogoStopStart::ClosingCredit(): no closing credits found");

    // check if we have a frame
    int maxSumFramePortion =  0;
    int allSumFramePortion =  0;
    int frameCorner        = -1;
    for (int corner = 0; corner < CORNERS; corner++) {
        allSumFramePortion += ClosingCredits.sumFramePortion[corner];
        if (noLogoCornerCheck && (corner == logoCorner)) continue;  // if we are called from Info logo, we can false detect the info logo as frame
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
        // best quote 493, all quote 326
        //
        // example of no closing credits
        // best quote 643, all quote 321  -> long static separator picture
        // best quote 403, all quote 465  -> long static ad picture
        // best quote 601, all quote 244  -> static preview
        if ((framePortionQuote < 493) || (allPortionQuote < 326)) {
            dsyslog("cDetectLogoStopStart::ClosingCredit(): not enough frame pixel found, closing credits not valid");
            // no valid closing credits
            endClosingCredits->position = -1;
            endClosingCredits->pts      = -1;
        }
    }

    if (evaluateLogoStopStartPair && ((endPos - startPos) / decoder->GetVideoFrameRate() >= MAX_CLOSING_CREDITS_SEARCH)) {
        dsyslog("cDetectLogoStopStart::ClosingCredit(): full range check was done, store result");
        if (endClosingCredits->position >= 0) evaluateLogoStopStartPair->SetIsClosingCredits(startPos, endPos, endClosingCredits->position, endClosingCredits->pts, STATUS_YES);
        else evaluateLogoStopStartPair->SetIsClosingCredits(startPos, endPos, endClosingCredits->position, endClosingCredits->pts, STATUS_NO);
    }
    return;
}


// search advertising in frame with logo
// check if we have matches in 3 of 4 corners and a detected frame
// start search at current position, end at stopPosition
// return first/last of advertising in frame with logo
//
void cDetectLogoStopStart::AdInFrameWithLogo(int startPos, int endPos, sMarkPos *adInFrame, const bool isStartMark, const bool isEndMark) {
    if (!decoder)              return;
    if (compareResult.empty()) return;

// for performance reason only for known and tested channels for now
    if (!criteria->IsAdInFrameWithLogoChannel()) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): skip this channel");
        return;
    }

    if (isStartMark) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between i-frame after logo start mark (%d) and (%d)", startPos, endPos);
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): start search advertising in frame between (%d) and logo stop mark at (%d)", startPos, endPos);
    int frameRate = decoder->GetVideoFrameRate();

    struct sAdInFrame {
        int startPosition                 = -1;
        int64_t startPTS                  = -1;
        int startFinalPosition            = -1;
        int64_t startFinalPTS             = -1;
        int endPosition                   = -1;
        int64_t endPTS                    = -1;
        int endFinalPosition              = -1;
        int64_t endFinalPTS               = -1;
        int frameCount                    =  0;
        int frameCountFinal               =  0;
        int sumFramePortion[CORNERS]      = {0};
        int sumFramePortionFinal[CORNERS] = {0};
        int framePortionQuote[CORNERS]    = {0};
    } AdInFrame;

    struct sStillImage {
        int start      = -1;
        int startFinal = -1;
        int end        = -1;
        int endFinal   = -1;
    } StillImage;

#define AD_IN_FRAME_STOP_OFFSET_MAX   4319  // changed from 8599 to 4319 because of false detection of scene with window frame
#define AD_IN_FRAME_START_OFFSET_MAX  4319  // changed from 4799 to 4319
#define AD_IN_FRAME_LENGTH_MAX       40000  // changed from 34680 to 40000 (40s long ad in frame found)
#define AD_IN_FRAME_LENGTH_MIN        6920  // shortest ad in frame found 6920ms, changed from 6960 to 6920
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
            if (corner ==logoCorner) {
                if ((*cornerResultIt).rate[corner] < 476) isStillImage = false; // changed from 992 to 476
            }
            else {
                if ((*cornerResultIt).rate[corner]  > 0) isStillImage = false;
                if ((*cornerResultIt).rate[corner] == 0) noPixelCountWoLogoCorner++;
                if ((*cornerResultIt).rate[corner] <= 0) darkCorner++;   // if we have no match, this can be a too dark corner
            }
            // check ad in frame
            if (((*cornerResultIt).rate[corner] >= 94) || ((*cornerResultIt).rate[corner] == -1)) similarCornersLow++;  // changed from 109 to 94
            if ((*cornerResultIt).rate[corner]  >= 253) similarCornersHigh++;  // changed from 310 to 253

            // check logo in corner
            if ((*cornerResultIt).rate[corner] >= 400) {  // changed from 424 to 400
                isCornerLogo[corner]++; // check if we have more than one logo, in this case there can not be a ad in frame
            }

            if ((*cornerResultIt).rate[corner] == 0) noPixelCountAllCorner++;
        }

        // check if it is a drank frame
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
            if (AdInFrame.startPosition == -1) {
                AdInFrame.startPosition = (*cornerResultIt).frameNumber1;
                AdInFrame.startPTS      = (*cornerResultIt).pts1;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
                dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): ad in frame start (%d)", AdInFrame.start);
                for (int corner = 0; corner < CORNERS; corner++) dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): corner %d: AdInFrame.sumFramePortion %7d", corner, AdInFrame.sumFramePortion[corner]);
#endif
            }
            AdInFrame.endPosition = (*cornerResultIt).frameNumber2;
            AdInFrame.endPTS      = (*cornerResultIt).pts2;
            AdInFrame.frameCount++;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortion[corner] += (*cornerResultIt).framePortion[corner];
        }
        else {
            if ((AdInFrame.startPosition != -1) && (AdInFrame.endPosition != -1)) {  // we have a new pair
                int startOffset = 1000 * (AdInFrame.startPosition - startPos) / frameRate;
#if defined(DEBUG_MARK_OPTIMIZATION) || defined (DEBUG_ADINFRAME)
                dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): ad in frame start (%d) end (%d), isStartMark %d, length %dms, startOffset %dms",  AdInFrame.start, AdInFrame.end, isStartMark, 1000 * (AdInFrame.end - AdInFrame.start) / frameRate, startOffset);
#endif
                // if we search for ad in frame after start mark, we end search after first valid match
                if (isStartMark && ((1000 * (AdInFrame.endPosition - AdInFrame.startPosition) / frameRate) >= AD_IN_FRAME_LENGTH_MIN)) {
                    AdInFrame.startFinalPosition = AdInFrame.startPosition;
                    AdInFrame.startFinalPTS      = AdInFrame.startPTS;
                    AdInFrame.endFinalPosition   = AdInFrame.endPosition;
                    AdInFrame.endFinalPTS        = AdInFrame.endPTS;
                    AdInFrame.frameCountFinal    = AdInFrame.frameCount;
                    for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortionFinal[corner] = AdInFrame.sumFramePortion[corner];
                    break;
                }

                // if we search for ad in frame before stop mark, continue try until stop mark reached
                if (((AdInFrame.endPosition - AdInFrame.startPosition) > (AdInFrame.endFinalPosition - AdInFrame.startFinalPosition)) && // new range if longer
                        ((isStartMark && (startOffset < AD_IN_FRAME_START_OFFSET_MAX)) ||  // adinframe after logo start must be near logo start
                         (!isStartMark && (startOffset > 1000)))) { // a valid ad in frame before stop mark has a start offset, drop invalid pair
                    AdInFrame.startFinalPosition = AdInFrame.startPosition;
                    AdInFrame.startFinalPTS      = AdInFrame.startPTS;
                    AdInFrame.endFinalPosition   = AdInFrame.endPosition;
                    AdInFrame.endFinalPTS        = AdInFrame.endPTS;
                    AdInFrame.frameCountFinal    = AdInFrame.frameCount;
                    for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortionFinal[corner] = AdInFrame.sumFramePortion[corner];
                }
                // reset state
                AdInFrame.startPosition     = -1;
                AdInFrame.startPTS          = -1;
                AdInFrame.endPosition       = -1;
                AdInFrame.endPTS            = -1;
                AdInFrame.frameCount        =  0;
                for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortion[corner] = 0;
            }
        }
    }

    // check if we have a very dark scene, in this case we can not detect ad in frame
    int darkQuote = 100 * darkFrames / countFrames;
    if (darkQuote >= 86) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): scene too dark, quote %d%%, can not detect ad in frame", darkQuote);
        return;
    }
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): dark quote %d%% is valid for detection", darkQuote);

    // select best found possible ad in frame sequence, in case of ad in frame before stop mark go to end position
    if (!isStartMark) {
        int stopOffsetBefore = 1000 * (endPos                     - AdInFrame.endFinalPosition)   / frameRate;
        int stopOffsetLast   = 1000 * (endPos                     - AdInFrame.endPosition)        / frameRate;
        int lengthBefore     = 1000 * (AdInFrame.endFinalPosition - AdInFrame.startFinalPosition) / frameRate;
        int lengthLast       = 1000 * (AdInFrame.endPosition      - AdInFrame.startPosition)      / frameRate;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): select best found possible ad in frame before logo stop mark, min length %dms, max stop offset %dms", AD_IN_FRAME_LENGTH_MIN, AD_IN_FRAME_STOP_OFFSET_MAX);
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): before possible ad in frame from (%d) to (%d), length %5dms, stop offset %5dms", AdInFrame.startFinalPosition, AdInFrame.endFinalPosition, lengthBefore, stopOffsetBefore);
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): last   possible ad in frame from (%d) to (%d), length %5dms, stop offset %5dms", AdInFrame.startPosition, AdInFrame.endPosition, lengthLast, stopOffsetLast);
        // example:
        // before possible ad in frame from (39133) to (39732), length 23960ms, stop offset 12480ms   -> static scene at end of part
        // last   possible ad in frame from (39802) to (40044), length  9680ms, stop offset     0ms   -> ad in frame
        if ((lengthLast > lengthBefore) ||   // last part is longer
                (stopOffsetBefore > AD_IN_FRAME_STOP_OFFSET_MAX) ||  // before part is too far from stop mark
                ((stopOffsetLast == 0) && (lengthLast >= AD_IN_FRAME_LENGTH_MIN))) {  // last part up to stop mark and long enough
            AdInFrame.startFinalPosition = AdInFrame.startPosition;
            AdInFrame.startFinalPTS      = AdInFrame.startPTS;
            AdInFrame.endFinalPosition   = AdInFrame.endPosition;
            AdInFrame.endFinalPTS        = AdInFrame.endPTS;
            AdInFrame.frameCountFinal    = AdInFrame.frameCount;
            for (int corner = 0; corner < CORNERS; corner++) AdInFrame.sumFramePortionFinal[corner] = AdInFrame.sumFramePortion[corner];
        }
    }

    // final possible ad in frame
    if (AdInFrame.startFinalPosition != -1) {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start (%d) PTS %" PRId64", end (%d) %" PRId64, AdInFrame.startFinalPosition, AdInFrame.startFinalPTS, AdInFrame.endFinalPosition, AdInFrame.endFinalPTS);
    }
    else {
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no advertising in frame found");
        return;
    }

    // check if we have a valid frame
    int allSumFramePortion    =  0;
    for (int corner = 0; corner < CORNERS; corner++) {
        allSumFramePortion += AdInFrame.sumFramePortionFinal[corner];
        AdInFrame.framePortionQuote[corner] = AdInFrame.sumFramePortionFinal[corner] / AdInFrame.frameCountFinal;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): sum of frame portion from corner %-12s: %7d, avg %d", aCorner[corner], AdInFrame.sumFramePortionFinal[corner], AdInFrame.framePortionQuote[corner]);
    }

    if (AdInFrame.frameCountFinal > 0) {
        int allFramePortionQuote = allSumFramePortion / AdInFrame.frameCountFinal / 4;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): average of all corners portion quote in possible ad in frame range: %3d", allFramePortionQuote);

        // example of ad in frame
        // average of all corners 445 (conflict)
        //
        // example for no ad in frame (static scene with vertial or horizontal lines, blinds, windows frames or stairs):
        // average of all corners 442   door frame
        // average of all corners 447   window frame
        //
        if (allFramePortionQuote <= 447) {
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): not enough frame pixel found on best corner found, advertising in frame not valid");
            return;
        }
        // check if we have a dark header area (eg. ProSieben), hight matches in top corner, low matches in bottom corner)
        // example in dark head area (frame portion quote of corner):
        // 921 / 917 / 300 / 125
        if ((AdInFrame.framePortionQuote[TOP_LEFT] >= 921) && (AdInFrame.framePortionQuote[TOP_RIGHT] >= 917) &&
                (AdInFrame.framePortionQuote[BOTTOM_LEFT] <= 300) && (AdInFrame.framePortionQuote[BOTTOM_RIGHT] <= 125)) {
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): dark header area detected, this is no frame");
            return;
        }
    }
    else return;

    // check still image
    if ((StillImage.end - StillImage.start) > (StillImage.endFinal - StillImage.startFinal)) {
        StillImage.startFinal = StillImage.start;
        StillImage.endFinal   = StillImage.end;
    }
    if (StillImage.start != -1) {
        int stillImageLength = 1000 * (StillImage.endFinal -  StillImage.startFinal) / frameRate;
        dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): still image before advertising in frame from (%d) to (%d), length %dms", StillImage.startFinal, StillImage.endFinal, stillImageLength);
        if ((StillImage.endFinal == AdInFrame.startFinalPosition) && (stillImageLength < 3000)) { // too long detected still images are invalid, changed from 30360 to 5280 to 4320 to 3000
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): move advertising in frame start (%d) to still image start (%d)", AdInFrame.startFinalPosition, StillImage.startFinal);
            AdInFrame.startFinalPosition = StillImage.startFinal;
        }
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): no still image before advertising in frame found");

    // check advertising in frame
    int startOffset = 1000 * (AdInFrame.startFinalPosition - startPos) / frameRate;
    int stopOffset  = 1000 * (endPos - AdInFrame.endFinalPosition) / frameRate;
    int length      = 1000 * (AdInFrame.endFinalPosition - AdInFrame.startFinalPosition) / frameRate;
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start (%d) PTS %" PRId64 ", end (%d) PTS %" PRId64, AdInFrame.startFinalPosition, AdInFrame.startFinalPTS, AdInFrame.endFinalPosition, AdInFrame.endFinalPTS);
    dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): advertising in frame: start offset %dms (expect <=%dms for start marks), stop offset %dms (expect <=%dms for stop mark), length %dms (expect >=%dms and <=%dms)", startOffset, AD_IN_FRAME_START_OFFSET_MAX, stopOffset, AD_IN_FRAME_STOP_OFFSET_MAX, length, AD_IN_FRAME_LENGTH_MIN, AD_IN_FRAME_LENGTH_MAX);

    if ((length >= AD_IN_FRAME_LENGTH_MIN) && (length <= AD_IN_FRAME_LENGTH_MAX)) { // do not reduce min to prevent false positive, do not increase to detect 10s ad in frame
        if ((isStartMark && (startOffset <= AD_IN_FRAME_START_OFFSET_MAX)) ||      // an ad in frame with logo after start mark must be near start mark, changed from 5 to 4
                (!isStartMark && (stopOffset  <= AD_IN_FRAME_STOP_OFFSET_MAX))) {       // an ad in frame with logo before stop mark must be near stop mark
            // maybe we have a preview direct after ad in frame and missed stop mark
            dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): this is a advertising in frame with logo");
            if (isStartMark) {
                adInFrame->position = AdInFrame.endFinalPosition;
                adInFrame->pts      = AdInFrame.endFinalPTS;
            }
            else {
                adInFrame->position = AdInFrame.startFinalPosition;
                adInFrame->pts      = AdInFrame.startFinalPTS;
            }
        }
        else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): offset not valid, this is not a advertising in frame with logo");
    }
    else dsyslog("cDetectLogoStopStart::AdInFrameWithLogo(): length not valid, this is not a advertising in frame with logo");
    return;
}


// check for inroduction logo before start mark
// we should find:
// - a separator frame
// - a range of similar frames in the logo corner, but no still image
// - no separator frame after similar logo corner frames
void cDetectLogoStopStart::IntroductionLogo(int startPos, int endPos, sMarkPos *introductionStart) {
    if (!introductionStart) return;
    introductionStart->position = -1;
    introductionStart->pts      = -1;

    if (!decoder) return;
    if (compareResult.empty()) return;
    if ((logoCorner < 0) || (logoCorner >= CORNERS)) {
        esyslog(" cDetectLogoStopStart::IntroductionLogo():  invalid logo corner %d", logoCorner);
        return;
    }

// for performance reason only for known and tested channels for now
    if (!criteria->IsIntroductionLogoChannel()) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): skip this channel");
        return;
    }
    int frameRate = decoder->GetVideoFrameRate();

    struct introductionLogo {
        int startPosition      = -1;
        int64_t startPTS       = -1;
        int endPosition        = -1;
        int framePortion       =  0;
        int frames             =  0;
        int startFinalPosition = -1;
        int64_t startFinalPTS  = -1;
        int endFinalPosition   = -1;
        int framePortionFinal  =  0;
    } introductionLogo;

    struct sStillImage {
        int start      = -1;
        int end        = -1;
        int startFinal = -1;
        int endFinal   = -1;
    } stillImage;

    int sumPixelBefore        =  0;
    int sumFramePortionBefore =  0;
    int separatorFrameBefore  = -1;
    int separatorFrameAfter   = -1;

#define INTRODUCTION_MIN_LENGTH          4480  // short introduction logo (sixx)
#define INTRODUCTION_MAX_LENGTH         29440  // changed from 29080 to 29440, RTL2 have very long introduction logo
#define INTRODUCTION_MAX_DIFF_SEPARATOR 20160  // max distance from sepatator frame to introduction logo start, changed from 10119 to 20160
    // somtime broacast start without logo before intruduction logo
    // sometime we have a undetected info logo or ad in frame without log before introduction logo and separtion frame is far before
#define INTRODUCTION_MAX_DIFF_END       4319   // max distance of introduction logo end to start mark (endPos)

    for(std::vector<sCompareInfo>::iterator cornerResultIt = compareResult.begin(); cornerResultIt != compareResult.end(); ++cornerResultIt) {
        // separator frame before introduction logo
        int sumPixel        = 0;
        int sumFramePortion = 0;
        int countNoMatch    = 0;
        int darkCorner      = 0;
        int countNoPixel    = 0;
        int countLow        = 0;
        int countStillImage = 0;
        for (int corner = 0; corner < CORNERS; corner++) {
            if ((*cornerResultIt).rate[corner]  <=  0) {
                countNoMatch++;
                if (corner != logoCorner) darkCorner++;   // if we have no match, this can be a too dark corner
            }
            if ((*cornerResultIt).rate[corner]  <   0) countNoPixel++;
            if ((*cornerResultIt).rate[corner]  <= 65) countLow++;
            if (((*cornerResultIt).rate[corner] <=  0) || ((*cornerResultIt).rate[corner] > 507)) countStillImage++; // changed from 142 to 507
            sumPixel        += (*cornerResultIt).rate[corner];
            sumFramePortion += (*cornerResultIt).framePortion[corner];
        }

#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): frame (%6d) and (%6d): %5d (%4d) %5d (%4d) %5d (%4d) %5d (%4d) -> %5d (%5d)", (*cornerResultIt).frameNumber1, (*cornerResultIt).frameNumber2, (*cornerResultIt).rate[0], (*cornerResultIt).framePortion[0], (*cornerResultIt).rate[1], (*cornerResultIt).framePortion[1], (*cornerResultIt).rate[2], (*cornerResultIt).framePortion[2], (*cornerResultIt).rate[3], (*cornerResultIt).framePortion[3], sumPixel, sumFramePortion);
#endif

        // examples of separator frames before introduction logo
        //  59     0     0    -1 =  58
        //  48    14     0     0 =  62  NEW
        //  -1   325(l)  0     0 = 324  // fading out logo on black sceen between broadcast before and introduction logo, logo start mark is after introduction logo (conflict)
        //  -1  1000(l) -1    -1 = 997  // black screen with logo, last frame from previous broadcast
        //
        // change from ad in frame without logo to start of broadcast with info logo, but no separator
        // frame (111307) and (111308):   991 ( 497)   937 ( 659)   999 (   0)   991 ( 805) ->  3918 ( 1961)
        // frame (111308) and (111309):    78 (   0)    23 ( 129)    33 (   0)    13 (   0) ->   147 (  129)
        //
        // frame ( 69378) and ( 69379):   991 ( 992)   946 (1001)   940 ( 842)   989 ( 993) ->  3866 ( 3828)
        // frame ( 69379) and ( 69380):    12 (   0)    58 ( 275)    57 (   0)   188 (   0) ->   315 (  275)
        //
        // frame ( 69378) and ( 69379):   991 ( 992)   948 (1002)   944 ( 817)   988 ( 994) ->  3871 ( 3805)
        // frame ( 69379) and ( 69380):    29 (   0)    62 ( 265)    65 (   0)   176 (   0) ->   332 (  265)
        if (((sumPixelBefore >= 3866) && (sumFramePortionBefore >= 1961) && (sumPixel <= 332) && (sumFramePortion <= 275)) ||
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
                ((countNoMatch == 2) && (sumPixel <   63)) ||
                ((countNoMatch >= 3) && (sumPixel <   64)) ||  // changed from 74 to 64
                ((countNoPixel == 3) && (sumPixel == 997) && (introductionLogo.startPosition == -1))) {  // special case blackscreen with logo, end of previous broadcast
            // but not if we have a detected separator, prevent false detection of black screen in boradcast
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
            dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator before introduction found at frame (%5d), sumPixel %d, sumPixelBefore %d, sumFramePortion %d, sumFramePortionBefore %d", (*cornerResultIt).frameNumber1, sumPixel, sumPixelBefore, sumFramePortion, sumFramePortionBefore);
#endif
            separatorFrameBefore = (*cornerResultIt).frameNumber1;
            introductionLogo.startPosition      = -1;
            introductionLogo.startPTS           = -1;
            introductionLogo.endPosition        = -1;
            introductionLogo.framePortion       =  0;
            introductionLogo.frames             =  0;
            introductionLogo.startFinalPosition = -1;
            introductionLogo.startFinalPTS      = -1;
            introductionLogo.endFinalPosition   = -1;
            introductionLogo.framePortionFinal  =  0;
            separatorFrameAfter                 = -1;
            stillImage.start                    = -1;
            stillImage.startFinal               = -1;
            stillImage.end                      = -1;
            stillImage.endFinal                 = -1;
            continue;
        }

        // separator after introduction logo, in this case it can not be a introduction logo
        // examples of separator frames after introduction logo
        // no separator frame
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
        if ((separatorFrameBefore >= 0) && (introductionLogo.startPosition >= 0) && (countStillImage >= 4)) { // still image or closing credists after introduction logo
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
        if (separatorFrameBefore >= 0) { // introduction logo start is after separator frame
            if ((*cornerResultIt).rate[logoCorner] >= 82) { // changed from 99 to 82 (change from one intro logo to another intro logo, kabel eins)
                if (introductionLogo.startPosition == -1) {
                    introductionLogo.startPosition = (*cornerResultIt).frameNumber1;
                    introductionLogo.startPTS      = (*cornerResultIt).pts1;
                }
                introductionLogo.endPosition = (*cornerResultIt).frameNumber2;
                introductionLogo.framePortion += sumFramePortion;
                introductionLogo.frames++;
            }
            else {
                if ((introductionLogo.endPosition - introductionLogo.startPosition) >= (introductionLogo.endFinalPosition - introductionLogo.startFinalPosition)) {
                    introductionLogo.startFinalPosition = introductionLogo.startPosition;
                    introductionLogo.startFinalPTS      = introductionLogo.startPTS;
                    introductionLogo.endFinalPosition   = introductionLogo.endPosition;
                    if (introductionLogo.frames > 0) introductionLogo.framePortionFinal = introductionLogo.framePortion / introductionLogo.frames / 4;
                }
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_INTRODUCTION)
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): packet (%d): end of introduction logo without separator because too low logo corner match", (*cornerResultIt).frameNumber2);
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): current      range: from (%d) to (%d), sum frame portion %4d, frames %3d", introductionLogo.startPosition, introductionLogo.endPosition, introductionLogo.framePortion, introductionLogo.frames);
                dsyslog("cDetectLogoStopStart::IntroductionLogo(): current best range: from (%d) to (%d), avg frame portion %4d", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, introductionLogo.framePortionFinal);
#endif
                introductionLogo.startPosition = -1;
                introductionLogo.startPTS      = -1;
                introductionLogo.endPosition   = -1;
                introductionLogo.framePortion  =  0;
                introductionLogo.frames        =  0;
            }
        }
        sumPixelBefore        = sumPixel;
        sumFramePortionBefore = sumFramePortion;
    }

    // select final introduction logo range
    int lengthLast    = 1000 * (introductionLogo.endPosition - introductionLogo.startPosition) / frameRate;
    int lastdiffStart = 1000 * (endPos                       - introductionLogo.endPosition)   / frameRate;
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): INTRODUCTION_MIN_LENGTH %dms", INTRODUCTION_MIN_LENGTH);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): last         range: from (%d) to (%d), sum frame portion %4d, frames %3d, length %dms, diff start mark %dms", introductionLogo.startPosition, introductionLogo.endPosition, introductionLogo.framePortion, introductionLogo.frames, lengthLast, lastdiffStart);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): current best range: from (%d) to (%d), avg frame portion %4d", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, introductionLogo.framePortionFinal);

    if (((introductionLogo.endPosition - introductionLogo.startPosition) >= (introductionLogo.endFinalPosition - introductionLogo.startFinalPosition)) ||   // use longer
            ((lengthLast >= INTRODUCTION_MIN_LENGTH) && (lastdiffStart == 0))) {  // use last if long enough, maybe longer range before was ad in frame without logo
        introductionLogo.startFinalPosition = introductionLogo.startPosition;
        introductionLogo.startFinalPTS      = introductionLogo.startPTS;
        introductionLogo.endFinalPosition   = introductionLogo.endPosition;
        if (introductionLogo.frames > 0) introductionLogo.framePortionFinal = introductionLogo.framePortion / introductionLogo.frames / 4;
    }
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): final introduction logo range: start (%d), end (%d), avg frame portion %d", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, introductionLogo.framePortionFinal);

    // check frame portion quote to prevent to false detect ad in frame without logo as introdition logo
    if (introductionLogo.framePortionFinal >= 336) {  // changed from 356 to 336
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): frame portion quote to high, ad in frame without logo before detected");
        return;
    }

    // check separator frame before introduction logo
    if (separatorFrameBefore >= 0) {
        int diffSeparator = 1000 * (endPos - separatorFrameBefore) / frameRate;
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator frame before introduction logo found (%d), %dms before start mark", separatorFrameBefore, diffSeparator);
    }
    else {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): no separator frame found");
        return;
    }

    // check separator frame after introduction logo
    if ((separatorFrameAfter >= 0) && (introductionLogo.endFinalPosition >= 0) && (separatorFrameAfter >= introductionLogo.endFinalPosition)) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): separator frame after introduction logo found (%d)", separatorFrameAfter);
        return;
    }

#define INTRODUCTION_STILL_MAX_DIFF_END 1440   // max distance of still image to start mark (endPos), changed from 1919 to 1440
    // check still image after introduction logo
    if ((stillImage.end - stillImage.start) >= (stillImage.endFinal - stillImage.startFinal)) {
        stillImage.startFinal = stillImage.start;
        stillImage.endFinal   = stillImage.end;
    }
    int lengthIntroductionLogo = 1000 * (introductionLogo.endFinalPosition - introductionLogo.startFinalPosition) / frameRate;
    if ((stillImage.startFinal >= 0) && (stillImage.endFinal > 0)) {
        int lengthStillImage = 1000 * (stillImage.endFinal - stillImage.startFinal) / frameRate;
        int diffStartMark    = 1000 * (endPos              - stillImage.endFinal)   / frameRate;
        int maxQuote = lengthIntroductionLogo * 0.7; // changed from 0.8 to 0.7
        // example invalid introduction logo
        // still image after introduction from (39497) to (39621), length 4960ms (expect >=18088ms), distance to start mark 1440ms (expect <=1919ms)
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): still image in introduction from (%d) to (%d), length %dms (expect >=%dms), distance to start mark %dms (expect <=%dms)", stillImage.startFinal, stillImage.endFinal, lengthStillImage, maxQuote, diffStartMark, INTRODUCTION_STILL_MAX_DIFF_END);
        if ((lengthStillImage >= maxQuote) ||
                ((diffStartMark <= INTRODUCTION_STILL_MAX_DIFF_END) && (lengthStillImage >= 4960))) {
            dsyslog("cDetectLogoStopStart::IntroductionLogo(): still image short before logo start detected, this in no introdution part");
            return;
        }
    }

// check introduction logo
    // check length and distances
    int diffEnd       = 1000 * (endPos                              - introductionLogo.endFinalPosition) / frameRate;
    int diffSeparator = 1000 * (introductionLogo.startFinalPosition - separatorFrameBefore)              / frameRate;
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), length %dms (expect >=%dms <=%dms)", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, lengthIntroductionLogo, INTRODUCTION_MIN_LENGTH, INTRODUCTION_MAX_LENGTH);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), diff to logo start mark (%d) %dms (expect <=%dms)", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, endPos, diffEnd, INTRODUCTION_MAX_DIFF_END);
    dsyslog("cDetectLogoStopStart::IntroductionLogo(): introduction logo: start (%d), end (%d), diff to separator frame (%d) %dms (expect <=%dms)", introductionLogo.startFinalPosition, introductionLogo.endFinalPosition, separatorFrameBefore, diffSeparator, INTRODUCTION_MAX_DIFF_SEPARATOR);
    if ((lengthIntroductionLogo >= INTRODUCTION_MIN_LENGTH) && (lengthIntroductionLogo <= INTRODUCTION_MAX_LENGTH) && (diffEnd <= INTRODUCTION_MAX_DIFF_END) && (diffSeparator <= INTRODUCTION_MAX_DIFF_SEPARATOR)) {
        dsyslog("cDetectLogoStopStart::IntroductionLogo(): found introduction logo start at (%d)", introductionLogo.startFinalPosition);
        introductionStart->position = introductionLogo.startFinalPosition;
        introductionStart->pts      = introductionLogo.startFinalPTS;
    }
    else dsyslog("cDetectLogoStopStart::IntroductionLogo(): no introduction logo found");
    return;
}
