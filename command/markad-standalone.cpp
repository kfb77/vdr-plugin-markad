/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "global.h"
#ifdef POSIX
   #include <syslog.h>
   #include <stdlib.h>
   #include <unistd.h>
   #include <fcntl.h>
   #include <getopt.h>
   #include <signal.h>
   #include <ctype.h>
   #include <netdb.h>
   #include <sys/stat.h>
   #include <sys/time.h>
   #include <sys/resource.h>
   #include <sys/wait.h>
   #include <locale.h>
   #include <libintl.h>
   #include <execinfo.h>
   #include <mntent.h>
   #include <utime.h>
   #include <math.h>
   #include <limits.h>
   #include <errno.h>
   #include <dirent.h>
#else
   #include "win32/mingw64.h"
#endif

#include "markad-standalone.h"
#include "version.h"
#include "logo.h"
#include "index.h"

bool SYSLOG = false;
bool LOG2REC = false;
cDecoder *ptr_cDecoder = NULL;
cExtractLogo *ptr_cExtractLogo = NULL;
cMarkAdStandalone *cmasta = NULL;
bool restartLogoDetectionDone = false;
int SysLogLevel = 2;
bool abortNow = false;
struct timeval startAll, endAll = {};
struct timeval startPass1, startOverlap, startLogoMarkOptimization, startPass4, endPass1, endOverlap, endLogoMarkOptimization, endPass4 = {};
int logoSearchTime_ms = 0;
int logoChangeTime_ms = 0;
int decodeTime_us = 0;


#ifdef POSIX

static inline int ioprio_set(int which, int who, int ioprio) {
#if defined(__i386__)
#define __NR_ioprio_set         289
#elif defined(__ppc__)
#define __NR_ioprio_set         273
#elif defined(__x86_64__)
#define __NR_ioprio_set         251
#elif defined(__arm__)
#define __NR_ioprio_set         314
#elif defined(__ia64__)
#define __NR_ioprio_set        1274
#else
#define __NR_ioprio_set           0
#endif
    if (__NR_ioprio_set) {
        return syscall(__NR_ioprio_set, which, who, ioprio);
    }
    else {
        fprintf(stderr,"set io prio not supported on this system\n");
        return 0; // just do nothing
    }
}


static inline int ioprio_get(int which, int who) {
#if defined(__i386__)
#define __NR_ioprio_get         290
#elif defined(__ppc__)
#define __NR_ioprio_get         274
#elif defined(__x86_64__)
#define __NR_ioprio_get         252
#elif defined(__arm__)
#define __NR_ioprio_get         315
#elif defined(__ia64__)
#define __NR_ioprio_get        1275
#else
#define __NR_ioprio_get           0
#endif
    if (__NR_ioprio_get) {
        return syscall(__NR_ioprio_get, which, who);
    }
    else {
        fprintf(stderr,"get io prio not supported on this system\n");
        return 0; // just do nothing
    }

}


void syslog_with_tid(int priority, const char *format, ...) {
    va_list ap;
    if ((SYSLOG) && (!LOG2REC)) {
        priority = LOG_ERR;
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
        va_start(ap, format);
        vsyslog(priority, fmt, ap);
        va_end(ap);
    }
    else {
        char buf[27] = {0};
        const time_t now = time(NULL);
        if (ctime_r(&now, buf)) {
            buf[strlen(buf) - 6] = 0;
        }
        else dsyslog("ctime_r failed");
        char fmt[255];
        char prioText[10];
        switch (priority) {
            case LOG_ERR:   strcpy(prioText,"ERROR:"); break;
            case LOG_INFO : strcpy(prioText,"INFO: "); break;
            case LOG_DEBUG: strcpy(prioText,"DEBUG:"); break;
            case LOG_TRACE: strcpy(prioText,"TRACE:"); break;
            default:        strcpy(prioText,"?????:"); break;
        }
        snprintf(fmt, sizeof(fmt), "%s%s [%d] %s %s", LOG2REC ? "":"markad: ", buf, getpid(), prioText, format);
        va_start(ap, format);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
        fflush(stdout);
    }
}

#endif /* #ifdef POSIX */

void cMarkAdStandalone::CalculateCheckPositions(int startframe) {
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): startframe %i (%dmin %2ds)", startframe, static_cast<int>(startframe / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(startframe / macontext.Video.Info.framesPerSecond) % 60);

    if (!length) {
        dsyslog("CalculateCheckPositions(): length of recording not found, set to 100h");
        length = 100 * 60 * 60; // try anyway, set to 100h
        startframe = macontext.Video.Info.framesPerSecond * 2 * 60;  // assume default pretimer of 2min
    }
    if (!macontext.Video.Info.framesPerSecond) {
        esyslog("video frame rate of recording not found");
        return;
    }

    if (startframe < 0) {   // recodring start is too late
        isyslog("recording started too late, set start mark to start of recording");
        sMarkAdMark mark = {};
        mark.position = 1;  // do not use position 0 because this will later be deleted
        mark.type = MT_RECORDINGSTART;
        AddMark(&mark);
        startframe = macontext.Video.Info.framesPerSecond * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): use frame rate %i", static_cast<int>(macontext.Video.Info.framesPerSecond));

    iStart = -startframe;
    iStop = -(startframe + macontext.Video.Info.framesPerSecond * length) ;   // iStop change from - to + when frames reached iStop

    iStartA = abs(iStart);
    iStopA = startframe + macontext.Video.Info.framesPerSecond * (length + macontext.Config->astopoffs);
    chkSTART = iStartA + macontext.Video.Info.framesPerSecond * 4 * MAXRANGE; //  fit for later broadcast start
    chkSTOP = startframe + macontext.Video.Info.framesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:   %4ds (%3dmin %2ds)", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame:  %5d  (%3dmin %2ds)", iStartA, static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:  %6d  (%3dmin %2ds)", iStopA, static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTART set to:     %6d  (%3dmin %2ds)", chkSTART, static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTOP set to:      %6d  (%3dmin %2ds)", chkSTOP, static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond) % 60);
}


int cMarkAdStandalone::CheckStop() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): start check stop (%i)", frameCurrent);

    char *indexToHMSF = marks.IndexToHMSF(iStopA);
        if (indexToHMSF) {
            dsyslog("assumed stop position (%i) at %s", iStopA, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
    DebugMarks();     //  only for debugging

    // remove logo change marks
    RemoveLogoChangeMarks();
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): start end end mark selection");

// try MT_CHANNELSTOP
    int delta = macontext.Video.Info.framesPerSecond * MAXRANGE;
    cMark *end = marks.GetAround(3 * delta, iStopA, MT_CHANNELSTOP);   // do not increase, we will get mark from last ad stop
    if (end) {
        dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found at frame %i", end->position);
        cMark *cStart = marks.GetPrev(end->position, MT_CHANNELSTART);      // if there is short befor a channel start, this stop mark belongs to next recording
        if (cStart) {
            if ((end->position - cStart->position) < delta) {
                dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTART found short before at frame %i with delta %ds, MT_CHANNELSTOP is not valid, try to find stop mark short before", cStart->position, static_cast<int> ((end->position - cStart->position) / macontext.Video.Info.framesPerSecond));
                end = marks.GetAround(delta, iStopA - delta, MT_CHANNELSTOP);
                if (end) dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found short before at frame (%d)", end->position);
            }
            else {
                cMark *cStartFirst = marks.GetNext(0, MT_CHANNELSTART);  // get first channel start mark
                cMark *movedFirst  = marks.First();                      // maybe first mark is a moved channel mark
                if (movedFirst && (movedFirst->type == MT_MOVEDSTART) && (movedFirst->oldType == MT_CHANNELSTART)) cStartFirst = movedFirst;
                if (cStartFirst) {
                    int deltaC = (end->position - cStartFirst->position) / macontext.Video.Info.framesPerSecond;
                    if (deltaC < 305) {  // changed from 300 to 305
                    dsyslog("cMarkAdStandalone::CheckStop(): first channel start mark (%d) and possible channel end mark (%d) to near %ds, this belongs to the next recording", cStartFirst->position, end->position, deltaC);
                    dsyslog("cMarkAdStandalone::CheckStop(): delete channel marks at (%d) and (%d)", cStartFirst->position, end->position);
                    marks.Del(cStartFirst->position);
                    marks.Del(end->position);
                    end = NULL;
                    }
                }
            }
        }
    }
    else dsyslog("cMarkAdStandalone::CheckStop(): no MT_CHANNELSTOP mark found");
    if (end) marks.DelWeakFromTo(marks.GetFirst()->position + 1, end->position, MT_CHANNELCHANGE); // delete all weak marks, except start mark

// try MT_ASPECTSTOP
    if (!end) {
        end = marks.GetAround(3 * delta, iStopA, MT_ASPECTSTOP);      // try MT_ASPECTSTOP
        if (end) {
            dsyslog("cMarkAdStandalone::CheckStop(): MT_ASPECTSTOP found at frame (%d)", end->position);
            if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) {
                dsyslog("cMarkAdStandalone::CheckStop(): delete all weak marks");
                marks.DelWeakFromTo(marks.GetFirst()->position + 1, end->position, MT_ASPECTCHANGE); // delete all weak marks, except start mark
            }
            else { // 16:9 broadcast with 4:3 broadcast after, maybe ad between and we have a better hborder or logo stop mark
                cMark *stopBefore  = marks.GetPrev(end->position, MT_HBORDERSTOP);         // try hborder
                if (!stopBefore) {
                    stopBefore  = marks.GetPrev(end->position, MT_LOGOSTOP);  // try logo stop
                    // check position of logo start mark before aspect ratio mark, if it is after logo stop mark, logo stop mark end can be end of preview in advertising
                    if (stopBefore) {
                        int diffAspectStop = (end->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d), %ds before aspect ratio stop mark", stopBefore->position, diffAspectStop);
                        if (diffAspectStop > 234) {  // check only for early logo stop marks, do not increase, there can be a late advertising and aspect stop on same frame as logo stop
                                                     // changed from 153 to 234
                            cMark *startLogoBefore = marks.GetPrev(end->position, MT_LOGOSTART);
                            if (startLogoBefore && (startLogoBefore->position > stopBefore->position)) {
                                dsyslog("cMarkAdStandalone::CheckStop(): logo start mark (%d) between logo stop mark (%d) and aspect ratio mark (%d), this logo stop mark is end of advertising", startLogoBefore->position, stopBefore->position, end->position);
                                stopBefore = NULL;
                            }
                        }
                    }
                }
                if (stopBefore) { // maybe real stop mark was deleted because on same frame as logo/hborder stop mark
                    int diff = (iStopA - stopBefore->position) /  macontext.Video.Info.framesPerSecond;
                    char *markType = marks.TypeToText(stopBefore->type);
                    dsyslog("cMarkAdStandalone::CheckStop(): found %s stop mark (%d) before aspect ratio end mark (%d), %ds before assumed stop", markType, stopBefore->position, end->position, diff);
                    FREE(strlen(markType)+1, "text");
                    free(markType);
                    if (diff <= 312) { // changed from 87 to 221 to 312
                        dsyslog("cMarkAdStandalone::CheckStop(): advertising before aspect ratio change, use stop mark before as end mark");
                        end = stopBefore;
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_ASPECTSTOP mark found");
    }

// try MT_HBORDERSTOP
    if (!end) {
        end = marks.GetAround(5 * delta, iStopA, MT_HBORDERSTOP);         // increased from 3 to 5
        if (end) {
            dsyslog("cMarkAdStandalone::CheckStop(): MT_HBORDERSTOP found at frame %i", end->position);
            cMark *prevHStart = marks.GetPrev(end->position, MT_HBORDERSTART);
            if (prevHStart && (prevHStart->position > iStopA)) {
                dsyslog("cMarkAdStandalone::CheckStop(): previous hborder start mark (%d) is after assumed stop (%d), hborder stop mark (%d) is invalid", prevHStart->position, iStopA, end->position);
                // check if we got first hborder stop of next broadcast
                cMark *hBorderStopPrev = marks.GetPrev(end->position, MT_HBORDERSTOP);
                if (hBorderStopPrev) {
                    int diff = (iStopA - hBorderStopPrev->position) / macontext.Video.Info.framesPerSecond;
                    if (diff <= 476) { // maybe recording length is wrong
                        dsyslog("cMarkAdStandalone::CheckStop(): previous hborder stop mark (%d) is %ds before assumed stop, take this as stop mark", hBorderStopPrev->position, diff);
                        end = hBorderStopPrev;
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStop(): previous hborder stop mark (%d) is %ds before assumed stop, not valid", hBorderStopPrev->position, diff);
                        end = NULL;
                    }
                }
                else {
                    end = NULL;
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_HBORDERSTOP mark found");
    }

// try MT_VBORDERSTOP
    if (!end  || (marks.First() && marks.First()->type == MT_VBORDERSTART)) {  // if start mark is VBORDER try anyway if we have a VBODERSTOP
        end = marks.GetAround(3*delta, iStopA, MT_VBORDERSTOP);
        if (end) {
            int deltaStopA = (end->position - iStopA) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckStop(): MT_VBORDERSTOP found at frame (%d), %ds after assumed stop", end->position, deltaStopA);
            if ((deltaStopA >= 203) && macontext.Video.Logo.isInBorder) {  // changed from 239 to 236 to 203
                cMark *logoStop = marks.GetPrev(end->position, MT_LOGOSTOP);
                if (logoStop) {
                    int deltaLogoStop = (iStopA - logoStop->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): MT_LOGOSTOP at (%d) %d before assumed stop found", logoStop->position, deltaLogoStop);
                    if (deltaLogoStop <= 381) {
                        dsyslog("cMarkAdStandalone::CheckStop(): MT_VBORDERSTOP too far after assumed stop, found bettet logo stop mark at (%d)", logoStop->position);
                        end = logoStop;
                    }
                }
            }
            if (end->type == MT_VBORDERSTOP) { // we habe not replaced vborder top with logo stop
                cMark *prevVStart = marks.GetPrev(end->position, MT_VBORDERSTART);
                if (prevVStart) {
                    dsyslog("cMarkAdStandalone::CheckStop(): vertial border start and stop found, delete weak marks except start mark");
                    marks.DelWeakFromTo(marks.GetFirst()->position + 1, INT_MAX, MT_VBORDERCHANGE);
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_VBORDERSTOP mark found");
    }

// try MT_LOGOSTOP
#define MAX_LOGO_END_MARK_FACTOR 2.7 // changed from 3 to 2.7 to prevent too early logo stop marks
    if (!end) {  // try logo stop mark

        // cleanup very short start/stop pairs around possible end marks, these are logo detection failures
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::CheckStop(): check logo end mark (cleanup very short logo start/stop pairs around possible logo end marks)");
        while (true) {
            end = marks.GetAround(MAX_LOGO_END_MARK_FACTOR * delta, iStopA, MT_LOGOSTOP);
            if (end) {
                int iStopDelta = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStop(): MT_LOGOSTOP found at frame (%d), %ds before assumed stop (%d)", end->position, iStopDelta, iStopA);
                if (iStopDelta > 282) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark too far before assumed stop");
                    end = NULL;
                    break;
                }
                else {
                    cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                    if (prevLogoStart) {
                        int deltaLogoStart = 1000 * (end->position - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
                        int maxDeltaLogoStart = 83000;  // changed from 18200 to 48700 to 83000
                        // if this is a channel with logo changes there could be one short before end mark, use lower value
                        // do not increase because of SIXX and SAT.1 has very short logo change at the end of recording, which are sometimes not detected
                        // sometimes we can not detect it at the end of the broadcast the info logo because text changes (noch eine Folge -> <Name der Folge>)
                        if (IsInfoLogoChannel(macontext.Info.ChannelName)) maxDeltaLogoStart = 8000;
                        if (deltaLogoStart <= maxDeltaLogoStart) {
                            cMark *prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
                            if (prevLogoStop && evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(prevLogoStop->position, prevLogoStart->position) == STATUS_YES)) {
                                dsyslog("cMarkAdStandalone::CheckStop(): previous logo stop (%d) start (%d) pair are closing credits, use this stop mark as end mark", prevLogoStop->position, prevLogoStart->position);
                                end = prevLogoStop;
                                break;
                            }
                            else {
                                dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) stop (%d) pair is invalid, logo start mark only %dms before, delete marks", prevLogoStart->position, end->position, deltaLogoStart);
                                marks.Del(end);
                                marks.Del(prevLogoStart);
                            }
                        }
                        else {
                            dsyslog("cMarkAdStandalone::CheckStop(): logo start mark (%d) is %dms (expect >%d) before logo stop mark (%d), logo stop mark is valid ", prevLogoStart->position, deltaLogoStart, maxDeltaLogoStart, end->position);
                            break;
                        }
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStop(): no previous logo start mark found");
                        break;
                    }
                }
            }
            else {
                dsyslog("cMarkAdStandalone::CheckStop(): no more logo stop mark found");
                break;
            }
        }

        // detect very short channel start before, in this case logo stop is invalid
        if (end) {
            cMark *prevChannelStart = marks.GetPrev(end->position, MT_CHANNELSTART);
            if (prevChannelStart) {
               int deltaChannelStart = (end->position - prevChannelStart->position) / macontext.Video.Info.framesPerSecond;
               if (deltaChannelStart <= 20) {
                   dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) is invalid, channel start mark (%d) only %ds before", end->position, prevChannelStart->position, deltaChannelStart);
                   end = NULL;
                }
            }
        }

        // check border start mark before, in this case logo stop is from next recording, use border start mark as end mark
        bool typeChange = false;
        if (end) {
            cMark *hBorderStart = marks.GetPrev(end->position, MT_HBORDERSTART);
            if (hBorderStart) {
                cMark *hBorderStartPrev = marks.GetPrev(hBorderStart->position, MT_HBORDERSTART);
                if (!hBorderStartPrev) {
                    int deltahBorder = (hBorderStart->position - iStopA) / macontext.Video.Info.framesPerSecond;
                    int deltaLogo   = (end->position          - iStopA) / macontext.Video.Info.framesPerSecond;
                    if ((deltaLogo > 0) && (deltahBorder > -1)) { // log top is after assumed end, hborder is after 1s before assumed stop
                        dsyslog("cMarkAdStandalone::CheckStop(): found MT_HBORDERSTART at (%d) %ds after assumed end (and no other MT_HBORDERSTART before), logo stop mark at (%d) %ds after assumed end is invalid, use MT_HBORDERSTART as end mark", hBorderStart->position, deltahBorder, end->position, deltaLogo);
                        marks.ChangeType(hBorderStart, MT_STOP);
                        end = hBorderStart;
                        typeChange = true;
                    }
                }
            }
        }

        // check if logo end mark is valid
        if (end && !typeChange) {
            LogSeparator(false);
            dsyslog("cMarkAdStandalone::CheckStop(): check logo end mark (%d), cleanup undetected info logos", end->position);
            // check if end mark and next start mark are closing credits
            cMark *logoStart  = marks.GetNext(end->position, MT_LOGOSTART);
            if (logoStart && evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(end->position, logoStart->position) == STATUS_YES)) {
                dsyslog("cMarkAdStandalone::CheckStop(): closing credits after this logo stop (%d), this is the end mark", end->position);
            }
            else {
                // check if next stop/start pair are closing credits, in this case, next stop mark is end mark
                cMark *nextLogoStop  = marks.GetNext(end->position, MT_LOGOSTOP);
                cMark *nextLogoStart = NULL;
                if (nextLogoStop) nextLogoStart = marks.GetNext(nextLogoStop->position, MT_LOGOSTART);
                if (nextLogoStart && nextLogoStop) {
                    int diffAssumed = (nextLogoStop->position - iStopA) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): next logo stop (%d) start (%d) pair found, stop mark is %ds after assumed end", nextLogoStop->position, nextLogoStart->position, diffAssumed);
                    if (diffAssumed < 184) {
                        if (evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(nextLogoStart->position, nextLogoStart->position) == STATUS_YES)) {
                            dsyslog("cMarkAdStandalone::CheckStop(): next logo stop (%d) start (%d) pair are closing credits, this stop mark is end mark", nextLogoStop->position, nextLogoStart->position);
                            end = nextLogoStop;
                        }
                        else dsyslog("cMarkAdStandalone::CheckStop(): next logo stop (%d) start (%d) pair are no closing credits", nextLogoStop->position, nextLogoStart->position);
                    }
                    else dsyslog("cMarkAdStandalone::CheckStop(): next logo stop mark too far after assumed end");
                }
                // check if previous logo stop/start pair are closing credits, in this case use previous logo stop mark
                cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                cMark *prevLogoStop  = NULL;
                if (prevLogoStart) prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
                if (prevLogoStart && prevLogoStop) {
#define MAX_BEFORE_ASUMED_STOP 203 // changed from 281 to 203
                    int diffAssumedPrevLogoStop = (iStopA - prevLogoStop->position) / macontext.Video.Info.framesPerSecond;
                    int diffAssumedEnd          = (iStopA - end->position)          / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): closing credits check of previous logo pair: stop (%d) start (%d): %ds (expect < %ds) before assumed stop (%d)", prevLogoStop->position, prevLogoStart->position, diffAssumedPrevLogoStop, MAX_BEFORE_ASUMED_STOP, iStopA);
                    dsyslog("cMarkAdStandalone::CheckStop(): current end mark (%d): %ds before assumed stop (%d)", end->position, diffAssumedEnd, iStopA);
                    if ((prevLogoStart->position > marks.GetFirst()->position) && (diffAssumedPrevLogoStop < MAX_BEFORE_ASUMED_STOP) && abs(diffAssumedPrevLogoStop) < (diffAssumedEnd) && evaluateLogoStopStartPair) {
                        // check closing credits, but not if we got first start mark as start mark of the pair, this could be closing credit of recording before
                        if (evaluateLogoStopStartPair->GetIsClosingCredits(prevLogoStop->position, prevLogoStart->position == STATUS_YES)) {
                            dsyslog("cMarkAdStandalone::CheckStop(): previous stop (%d) start (%d): are closing credits, use this logo stop mark", prevLogoStop->position, prevLogoStart->position);
                            end = prevLogoStop;
                        }
                    }
                    else dsyslog("cMarkAdStandalone::CheckStop(): closing credits check of previous logo pair: stop (%d) start (%d): %ds (expect < %ds) before assumed stop (%d) is not valid as end mark", prevLogoStop->position, prevLogoStart->position, diffAssumedPrevLogoStop, MAX_BEFORE_ASUMED_STOP, iStopA);
                    // detect short logo stop/start before assumed stop mark, they can be undetected info logos or text previews over the logo (e.g. SAT.1)
                    while (true) {
                        prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART); // end mark can be changed above
                        prevLogoStop  = NULL;
                        if (prevLogoStart) prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
                        else break;
                        if (prevLogoStop) {
#define MAX_BEFORE_ASUMED_STOP 203 // changed from 281 to 203
                            int deltaLogoStart = (end->position - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
                            int deltaLogoPrevStartStop = (prevLogoStart->position - prevLogoStop->position) / macontext.Video.Info.framesPerSecond;
                            dsyslog("cMarkAdStandalone::CheckStop(): check for undetected info logo: stop (%d) start (%d) pair (lenght %ds), %ds before assumed end mark (%d)", prevLogoStop->position, prevLogoStart->position, deltaLogoPrevStartStop, deltaLogoPrevStartStop, end->position);
#define CHECK_START_DISTANCE_MAX 188    // changed from 13 to 71 to 141 to 188
#define CHECK_START_STOP_LENGTH_MAX 17  // changed from  4 to 12 to  17
                            if ((deltaLogoStart <= CHECK_START_DISTANCE_MAX) && (deltaLogoPrevStartStop <= CHECK_START_STOP_LENGTH_MAX)) {
                                dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) stop (%d) pair before assumed end mark is near %ds (<=%ds) and short %ds (<=%ds), this is undetected info logo or text preview over the logo, delete marks", prevLogoStart->position, prevLogoStop->position, deltaLogoStart, CHECK_START_DISTANCE_MAX, deltaLogoPrevStartStop, CHECK_START_STOP_LENGTH_MAX);
                                marks.Del(prevLogoStart);
                                marks.Del(prevLogoStop);
                            }
                            else {
                                dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) stop (%d) pair before assumed end mark is far %ds (>%ds) or long %ds (>%ds), stop/start pair is valid", prevLogoStart->position, prevLogoStop->position, deltaLogoStart, CHECK_START_DISTANCE_MAX, deltaLogoPrevStartStop, CHECK_START_STOP_LENGTH_MAX);
                                break;
                            }
                        }
                        else break;
                    }
                    prevLogoStop = marks.GetPrev(end->position, MT_LOGOSTOP); // maybe different if deleted above
                    if (prevLogoStop) {
                        int deltaLogo = (end->position - prevLogoStop->position) / macontext.Video.Info.framesPerSecond;
#define CHECK_STOP_BEFORE_MIN 14 // if stop before is too near, maybe recording length is too big
                        if (deltaLogo < CHECK_STOP_BEFORE_MIN) {
                            dsyslog("cMarkAdStandalone::CheckStop(): logo stop before too near %ds (expect >=%ds), use (%d) as stop mark", deltaLogo, CHECK_STOP_BEFORE_MIN, prevLogoStop->position);
                            end = prevLogoStop;
                        }
                        else dsyslog("cMarkAdStandalone::CheckStop(): logo stop before at (%d) too far away %ds (expect <%ds), no alternative", prevLogoStop->position, deltaLogo, CHECK_STOP_BEFORE_MIN);
                    }
                }
            }
        }

        // check if very eary logo end mark is end of preview
        if (end && !typeChange) {
            int beforeAssumed = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckStop(): end mark (%d) %ds before assumed stop (%d)", end->position, beforeAssumed, iStopA);
            if (beforeAssumed >= 218) {
                cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                // ad before
                cMark *prevLogoStop = NULL;
                if (prevLogoStart) prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
                // broadcast after
                cMark *nextLogoStart = marks.GetNext(end->position, MT_LOGOSTART);
                cMark *nextLogoStop = NULL;
                if (nextLogoStart) nextLogoStop = marks.GetNext(end->position, MT_LOGOSTOP);

                if (prevLogoStart && prevLogoStop && nextLogoStart && !nextLogoStop) {  // debug log for future use
                    int adBefore = (prevLogoStart->position - prevLogoStop->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): advertising before from (%d) to (%d) %3ds", prevLogoStart->position, prevLogoStop->position, adBefore);
                    int adAfter = (nextLogoStart->position - end->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): advertising after  from (%d) to (%d) %3ds", end->position, nextLogoStart->position, adAfter);
                    int broadcastBefore = (end->position - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): broadcast   before from (%d) to (%d) %3ds", prevLogoStart->position, end->position, broadcastBefore);

                    if (broadcastBefore <= 115) {  // end mark invalid there is only a very short broadcast after end mark, changed from 34 to 115
                        dsyslog("cMarkAdStandalone::CheckStop(): broadcast before only %ds, end mark (%d) is invalid", broadcastBefore, end->position);
                        end = NULL;
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_LOGOSTOP mark found");
    }

    if (!end) {
        end = marks.GetAround(1.1 * delta, iStopA, MT_STOP, 0x0F);    // try any type of stop mark, accept only near assumed stop
        if (end) dsyslog("cMarkAdStandalone::CheckStop(): weak end mark found at frame %d near assumed stop (%d)", end->position, iStopA);
        else dsyslog("cMarkAdStandalone::CheckStop(): no end mark found near assumed stop (%d)", iStopA);
    }
    if (!end) {
        end = marks.GetNext(iStopA, MT_STOP, 0x0F);    // try any type of stop mark, accept only after assumed end, better safe than sorry
        if (end) dsyslog("cMarkAdStandalone::CheckStop(): weak end mark found at frame %d after assumed stop (%d)", end->position, iStopA);
        else dsyslog("cMarkAdStandalone::CheckStop(): no end mark found after assumed stop (%d)", iStopA);
    }

    cMark *lastStart = marks.GetAround(INT_MAX, frameCurrent, MT_START, 0x0F);
    if (end) {
        dsyslog("cMarkAdStandalone::CheckStop(): found end mark at (%i)", end->position);
        cMark *mark = marks.GetFirst();
        while (mark) {
            if ((mark->position >= iStopA-macontext.Video.Info.framesPerSecond * MAXRANGE) && (mark->position < end->position) && ((mark->type & 0xF0) < (end->type & 0xF0))) { // delete all weak marks
                dsyslog("cMarkAdStandalone::CheckStop(): found stronger end mark (%d) delete mark (%d)", end->position, mark->position);
                cMark *tmp = mark;
                mark = mark->Next();
                marks.Del(tmp);
                continue;
            }
            mark = mark->Next();
        }

        // if stop mark is MT_NOBLACKSTOP and it is not after iStopA try next, better save than sorry
        if (end->type == MT_NOBLACKSTOP) {
            dsyslog("cMarkAdStandalone::CheckStop(): only accept blackscreen marks after assumes stop");
            while(end->position < iStopA) {
                cMark *end2 = marks.GetNext(end->position, MT_STOP, 0x0F);
                if (end2) {
                    dsyslog("cMarkAdStandalone::CheckStop(): week stop mark is before assumed stop, use next stop mark at (%d)", end2->position);
                    end = end2;
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStop(): week stop mark is before assumed stop, no next stop mark found");
                    end = NULL;
                    break;
                }
            }
        }

        // ignore very short black screen, take next as end mark
        if (end && end->type == MT_NOBLACKSTOP) {
            dsyslog("cMarkAdStandalone::CheckStop(): do not accept very short blackscreen marks");
            cMark *blackStop  = end;
            cMark *blackStart = marks.GetNext(end->position, MT_NOBLACKSTART);
            while (true) {
                if (!blackStop || !blackStart) break;
                if ((blackStart->position - end->position) > 1) break;
                blackStop                 = marks.GetNext(blackStop->position, MT_NOBLACKSTOP);
                if (blackStop) blackStart = marks.GetNext(blackStop->position, MT_NOBLACKSTART);
            }
            if (blackStop) end = blackStop;
        }

        if (end) {
            indexToHMSF    = marks.IndexToHMSF(end->position);
            char *markType = marks.TypeToText(end->type);
            if (indexToHMSF && markType) {
                isyslog("using %s stop mark on position (%i) at %s as end mark", markType, end->position, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }

            if (end->position < iStopA - 5 * delta ) {    // last found stop mark too early, adding STOP mark at the end, increased from 3 to 5
                                                          // this can happen by audio channel change too if the next broadcast has also 6 channels
                if ((lastStart) && (lastStart->position > end->position)) {
                    isyslog("last STOP mark results in to short recording, set STOP at the end of the recording (%i)", iFrameCurrent);
                    sMarkAdMark markNew = {};
                    markNew.position = iFrameCurrent;
                    markNew.type = MT_ASSUMEDSTOP;
                    AddMark(&markNew);
                    end = marks.Get(iFrameCurrent);
                }
            }
        }
    }

    if (!end) {  // no valid stop mark found
                 // try if there is any late MT_ASPECTSTOP
        dsyslog("cMarkAdStandalone::CheckStop(): no valid end mark found, try very late MT_ASPECTSTOP");
        cMark *aFirstStart = marks.GetNext(0, MT_ASPECTSTART);
        if (aFirstStart) {
            cMark *aLastStop = marks.GetPrev(INT_MAX, MT_ASPECTSTOP);
            if (aLastStop && (aLastStop->position > iStopA)) {
                dsyslog("cMarkAdStandalone::CheckStop(): start mark is MT_ASPECTSTART (%d) found very late MT_ASPECTSTOP at (%d)", aFirstStart->position, aLastStop->position);
                end = aLastStop;
            }
        }
    }

    if (!end) {  // no end mark found at all, set end mark at the end of the recording
        dsyslog("cMarkAdStandalone::CheckStop(): no stop mark found, add stop mark at the last frame (%d)", iFrameCurrent);
        sMarkAdMark mark = {};
        mark.position = iFrameCurrent;  // we are lost, add a end mark at the last iframe
        mark.type     = MT_ASSUMEDSTOP;
        AddMark(&mark);
        end = marks.GetPrev(INT_MAX, MT_STOP, 0x0F);  // make sure we got a stop mark
    }

    // now we have a end mark
    if (end) { // be save, if something went wrong end = NULL
        dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after final stop mark at (%d)", end->position);
        marks.DelTill(end->position, false);
    }
    else esyslog("could not find a end mark");

    // delete all black sceen marks expect start or end mark
    dsyslog("cMarkAdStandalone::CheckStop(): move all black screen marks except start and end mark to black screen list");
    cMark *mark = marks.GetFirst();
    while (mark) {
        if (mark != marks.GetFirst()) {
            if (mark == marks.GetLast()) break;
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) {
                cMark *tmp = mark;
                mark = mark->Next();
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

    iStop = iStopA = 0;
    gotendmark = true;

    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): end check stop");
    LogSeparator();
    if (end) return end->position;
    else return 0;

}


// check if last stop mark is start of closing credits without logo or hborder
// move stop mark to end of closing credit
// <stopMark> last logo or hborder stop mark
// return: true if closing credits was found and last logo stop mark position was changed
//
bool cMarkAdStandalone::MoveLastStopAfterClosingCredits(cMark *stopMark) {
    if (!stopMark) return false;
    dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): check closing credits without logo after position (%d)", stopMark->position);

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, ptr_cDecoder, recordingIndexMark, NULL);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    int endPos = stopMark->position + (25 * macontext.Video.Info.framesPerSecond);  // try till 25s after stopMarkPosition
    int newPosition = -1;
    if (ptr_cDetectLogoStopStart->Detect(stopMark->position, endPos)) {
        newPosition = ptr_cDetectLogoStopStart->ClosingCredit();
    }

    FREE(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");
    delete ptr_cDetectLogoStopStart;

    if (newPosition > stopMark->position) {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): closing credits found, move logo stop mark to position (%d)", newPosition);
        marks.Move(stopMark, newPosition, "closing credits");
        return true;
    }
    else {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): no closing credits found");
        return false;
    }
}


// remove stop/start logo mark pair if it detecs a part in the broadcast with logo changes
// some channel e.g. TELE5 plays with the logo in the broadcast
//
void cMarkAdStandalone::RemoveLogoChangeMarks() {  // for performance reason only known and tested channels
    struct timeval startTimeChangeMarks, stopTimeChangeMarks;
    gettimeofday(&startTimeChangeMarks, NULL);

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): start detect and remove logo stop/start mark pairs with special logo");

    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair();
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }
    evaluateLogoStopStartPair->CheckLogoStopStartPairs(&macontext, &marks, &blackMarks, iStart, chkSTART, iStopA);

    char *indexToHMSFStop = NULL;
    char *indexToHMSFStart = NULL;
    int stopPosition = 0;
    int startPosition = 0;
    int isLogoChange = 0;
    int isInfoLogo = 0;

    // alloc new objects
    ptr_cDecoderLogoChange = new cDecoder(macontext.Config->threads, recordingIndexMark);
    ALLOC(sizeof(*ptr_cDecoderLogoChange), "ptr_cDecoderLogoChange");
    ptr_cDecoderLogoChange->DecodeDir(directory);

    cExtractLogo *ptr_cExtractLogoChange = new cExtractLogo(&macontext, macontext.Video.Info.AspectRatio, recordingIndexMark);
    ALLOC(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, ptr_cDecoderLogoChange, recordingIndexMark, evaluateLogoStopStartPair);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    // loop through all logo stop/start pairs
    int endRange = 0;  // if we are called by CheckStart, get all pairs to detect at least closing credits
    if (iStart == 0) endRange = iStopA - (26 * macontext.Video.Info.framesPerSecond); // if we are called by CheckStop, get all pairs after this frame to detect at least closing credits
    while (evaluateLogoStopStartPair->GetNextPair(&stopPosition, &startPosition, &isLogoChange, &isInfoLogo, endRange)) {
        LogSeparator();
        // free from loop before
        if (indexToHMSFStop) {
            FREE(strlen(indexToHMSFStop)+1, "indexToHMSF");
            free(indexToHMSFStop);
        }
        if (indexToHMSFStart) {
            FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
            free(indexToHMSFStart);
        }
        // get time of marks and log marks
        indexToHMSFStop = marks.IndexToHMSF(stopPosition);
        indexToHMSFStart = marks.IndexToHMSF(startPosition);
        if (indexToHMSFStop && indexToHMSFStart) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%d) at %s and logo start (%d) at %s, isInfoLogo %d", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart, isInfoLogo);
        }
        if (ptr_cDetectLogoStopStart->Detect(stopPosition, startPosition)) {
            // check for closing credits if no other checks will be done, only part of the loop elements in recording end range
            if ((isInfoLogo <= STATUS_NO) && (isLogoChange <= STATUS_NO)) ptr_cDetectLogoStopStart->ClosingCredit();

            // check info logo
            if ((isInfoLogo >= STATUS_UNKNOWN) && ptr_cDetectLogoStopStart->IsInfoLogo()) {
                // found info logo part
                if (indexToHMSFStop && indexToHMSFStart) {
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): info logo found between frame (%i) at %s and (%i) at %s, deleting marks between this positions", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
                }
                evaluateLogoStopStartPair->SetIsInfoLogo(stopPosition, startPosition);
                marks.DelFromTo(stopPosition, startPosition, MT_LOGOCHANGE);  // maybe there a false start/stop inbetween
            }

            // check logo change
            if ((isLogoChange >= STATUS_UNKNOWN) && ptr_cDetectLogoStopStart->IsLogoChange()) {
                if (indexToHMSFStop && indexToHMSFStart) {
                    isyslog("logo change between frame (%6d) at %s and (%6d) at %s, deleting marks between this positions", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
                }
                marks.DelFromTo(stopPosition, startPosition, MT_LOGOCHANGE);  // maybe there a false start/stop inbetween
            }
        }
    }

    // delete last timer string
    if (indexToHMSFStop) {
        FREE(strlen(indexToHMSFStop)+1, "indexToHMSF");
        free(indexToHMSFStop);
    }
    if (indexToHMSFStart) {
        FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
        free(indexToHMSFStart);
    }

    // free objects
    FREE(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");
    delete ptr_cExtractLogoChange;
    FREE(sizeof(*ptr_cDecoderLogoChange), "ptr_cDecoderLogoChange");
    delete ptr_cDecoderLogoChange;
    FREE(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");
    delete ptr_cDetectLogoStopStart;

    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): marks after detect and remove logo stop/start mark pairs with special logo");
    DebugMarks();     //  only for debugging

    gettimeofday(&stopTimeChangeMarks, NULL);
    time_t      sec  = stopTimeChangeMarks.tv_sec  - startTimeChangeMarks.tv_sec;
    suseconds_t usec = stopTimeChangeMarks.tv_usec - startTimeChangeMarks.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        sec--;
    }
    logoChangeTime_ms += sec * 1000 + usec / 1000;

    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): end detect and remove logo stop/start mark pairs with special logo");
    LogSeparator();
}


void cMarkAdStandalone::CheckStart() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", frameCurrent, chkSTART);
    dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %i", iStartA);
    DebugMarks();     //  only for debugging
#define IGNORE_AT_START 12   // ignore this number of frames at the start for start marks, they are initial marks from recording before, changed from 11 to 12

    int hBorderStopPosition = -1;
    int delta = macontext.Video.Info.framesPerSecond * MAXRANGE;

// recording start
    cMark *begin = marks.GetAround(delta, 1, MT_RECORDINGSTART);  // do we have an incomplete recording ?
    if (begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): found MT_RECORDINGSTART (%i), use this as start mark for the incomplete recording", begin->position);
        // delete short stop marks without start mark
        cMark *stopMark = marks.GetNext(0, MT_CHANNELSTOP);
        if (stopMark) {
            int diff = stopMark->position / macontext.Video.Info.framesPerSecond;
            if ((diff < 30) && (marks.Count(MT_CHANNELSTART) == 0)) {
                dsyslog("cMarkAdStandalone::CheckStart(): delete stop mark (%d) without start mark", stopMark->position);
                marks.Del(stopMark->position);
            }
        }
    }

// audio channel start
    if (!begin) {
        for (short int stream = 0; stream < MAXSTREAMS; stream++) {
            if ((macontext.Info.Channels[stream]) && (macontext.Audio.Info.Channels[stream]) && (macontext.Info.Channels[stream] != macontext.Audio.Info.Channels[stream])) {
                char as[20];
                switch (macontext.Info.Channels[stream]) {
                    case 1:
                        strcpy(as, "mono");
                        break;
                    case 2:
                        strcpy(as, "stereo");
                        break;
                    case 6:
                        strcpy(as, "dd5.1");
                        break;
                    default:
                        strcpy(as, "??");
                        break;
                }
                char ad[20];
                switch (macontext.Audio.Info.Channels[stream]) {
                    case 1:
                        strcpy(ad, "mono");
                        break;
                    case 2:
                        strcpy(ad, "stereo");
                        break;
                    case 6:
                        strcpy(ad, "dd5.1");
                        break;
                    default:
                        strcpy(ad, "??");
                    break;
                }
                isyslog("audio description in info (%s) wrong, we have %s", as, ad);
            }
            macontext.Info.Channels[stream] = macontext.Audio.Info.Channels[stream];

            if (macontext.Config->decodeAudio && macontext.Info.Channels[stream]) {
                if ((macontext.Info.Channels[stream] == 6) && (macontext.Audio.Options.ignoreDolbyDetection == false)) {
                    isyslog("DolbyDigital5.1 audio whith 6 Channels in stream %i detected", stream);
                    if (macontext.Audio.Info.channelChange) {
                        dsyslog("cMarkAdStandalone::CheckStart(): channel change detected, disable logo/border/aspect detection");
                        video->ClearBorder();
                        macontext.Video.Options.ignoreAspectRatio   = true;
                        macontext.Video.Options.ignoreLogoDetection = true;
                        macontext.Video.Options.ignoreHborder       = true;
                        macontext.Video.Options.ignoreVborder       = true;
                        marks.DelType(MT_ASPECTCHANGE, 0xF0); // delete aspect marks if any

                        // start mark must be around iStartA
                        begin = marks.GetAround(delta * 3, iStartA, MT_CHANNELSTART);  // decrease from 4
                        if (!begin) {          // previous recording had also 6 channels, try other marks
                            dsyslog("cMarkAdStandalone::CheckStart(): no audio channel start mark found");
                        }
                        else {
                            dsyslog("cMarkAdStandalone::CheckStart(): audio channel start mark found at (%d)", begin->position);
                            if (begin->position > iStopA) {  // this could be a very short recording, 6 channel is in post recording
                                dsyslog("cMarkAdStandalone::CheckStart(): audio channel start mark after assumed stop mark not valid");
                                begin = NULL;
                            }
                            else {
                                if (marks.GetNext(begin->position, MT_HBORDERSTART) || marks.GetNext(begin->position, MT_VBORDERSTART)) macontext.Video.Info.hasBorder = true;
                                // we do not need the weaker marks, we found a strong MT_CHANNELSTART
                                marks.DelType(MT_LOGOCHANGE,    0xF0);
                                marks.DelType(MT_HBORDERCHANGE, 0xF0);
                                marks.DelType(MT_VBORDERCHANGE, 0xF0);
                            }
                        }
                    }
                    else dsyslog("cMarkAdStandalone::CheckStart(): no audio channel change found till now, do not disable logo/border/aspect detection");
                }
                else {
                    if (macontext.Audio.Options.ignoreDolbyDetection) isyslog("disabling AC3 decoding (from logo)");
                    else isyslog("AC3 audio with %d channels on stream %d",macontext.Info.Channels[stream], stream);  // macontext.Info.Channels[stream] is always true
                    if (inBroadCast) {  // if we have channel marks but we are now with 2 channels inBroascast, delete these
                        macontext.Video.Options.ignoreAspectRatio          = false;   // then we have to find other marks
                        macontext.Video.Options.ignoreLogoDetection        = false;
                        macontext.Video.Options.ignoreBlackScreenDetection = false;
                    }
                }
            }
        }
        if (begin && inBroadCast) { // set recording aspect ratio for logo search at the end of the recording
            macontext.Info.AspectRatio.num = macontext.Video.Info.AspectRatio.num;
            macontext.Info.AspectRatio.den = macontext.Video.Info.AspectRatio.den;
            macontext.Info.checkedAspectRatio = true;
            isyslog("Video with aspect ratio of %i:%i detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
        }
        if (!begin && inBroadCast) {
            cMark *chStart = marks.GetNext(0, MT_CHANNELSTART);
            cMark *chStop = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);
            if (chStart && chStop && (chStart->position > chStop->position)) {
                dsyslog("cMarkAdStandalone::CheckStart(): channel start after channel stop found, delete all weak marks between");
                marks.DelWeakFromTo(chStop->position, chStart->position, MT_CHANNELCHANGE);
            }
        }
        if (!begin && !inBroadCast) {
            dsyslog("cMarkAdStandalone::CheckStart(): we are not in broadcast at frame (%d), trying to find channel start mark anyway", frameCurrent);
            begin = marks.GetAround(delta*4, iStartA, MT_CHANNELSTART);
            // check if the start mark is from previous recording
            if (begin) {
                cMark *lastChannelStop = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);
                if (lastChannelStop && (lastChannelStop->position <= chkSTART)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): last channel stop mark at frame (%d) is too early, ignore channel marks are from previous recording", lastChannelStop->position);
                    begin = NULL;
                }
            }
        }

        if (begin) { // now we have a final channel start mark
            begin = marks.Optimize(2 * macontext.Video.Info.framesPerSecond, begin);  // optimize channel start mark with near blacksceen, channel marks are strong but not exact
            marks.DelWeakFromTo(0, INT_MAX, MT_CHANNELCHANGE); // we have a channel start mark, delete all weak marks
        }
        else {     // no channel start mark found, cleanup invalid channel stop marks
            cMark *cStart = marks.GetNext(0, MT_CHANNELSTART);
            cMark *cStop  = marks.GetNext(0, MT_CHANNELSTOP);
            if (!cStart && cStop) {  // channel stop mark and no channel start mark
                int pos = cStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): channel stop without start mark found (%i), assume as start mark of the following recording, convert it to assumed start mark", pos);  // for later use, if we found nothing else
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from channel stop (%d)", pos) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, pos, comment);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
            }
        }
    }

// aspect ratio start
    if (!begin) {
        if ((macontext.Info.AspectRatio.num == 0) || (macontext.Video.Info.AspectRatio.den == 0)) {
            isyslog("no video aspect ratio found in vdr info file");
            macontext.Info.AspectRatio.num = macontext.Video.Info.AspectRatio.num;
            macontext.Info.AspectRatio.den = macontext.Video.Info.AspectRatio.den;
        }
        // check marks and correct if necessary
        cMark *aStart = marks.GetAround(4 * delta, iStartA, MT_ASPECTSTART);   // check if we have ascpect ratio START/STOP in start part
        cMark *aStopAfter = NULL;
        cMark *aStopBefore = NULL;
        if (aStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio start mark at (%d), video info is %d:%d", aStart->position, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            aStopAfter = marks.GetNext(aStart->position, MT_ASPECTSTOP);
            aStopBefore = marks.GetPrev(aStart->position, MT_ASPECTSTOP);
        }
        bool earlyAspectChange = false;
        if (aStart && aStopAfter) {  // we are in the first ad, do not correct aspect ratio from info file
            dsyslog("cMarkAdStandalone::CheckStart(): found very early aspect ratio change at (%i) and (%i)", aStart->position, aStopAfter->position);
            earlyAspectChange = true;
        }

        // check aspect ratio info from vdr info file
        cMark *firstStop = marks.GetNext(0, MT_ASPECTSTOP);
        if (!aStart && firstStop && (firstStop->position > (iStopA * 0.8))) {   // we have no start mark and a stop mark at the end, this is next recording
            dsyslog("cMarkAdStandalone::CheckStart(): first aspectio ratio stop (%d) near assumed end mark, we are in next broadcast", firstStop->position);
        }
        else { // we have marks to work with
            bool wrongAspectInfo = false;
            if ((macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {  // vdr info file tells 16:9 video
                if (aStart && aStopBefore && (aStopBefore->position > 0)) { // found 16:9 -> 4:3 -> 16:9, this can not be a 16:9 video
                    dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio change 16:9 to 4:3 at (%d) to 16:9 at (%d), video info is 16:9, this must be wrong", aStopBefore->position, aStart->position);
                    wrongAspectInfo = true;
                }
                if (!wrongAspectInfo && (macontext.Video.Info.AspectRatio.num == 4) && (macontext.Video.Info.AspectRatio.den == 3) && inBroadCast) {
                    dsyslog("cMarkAdStandalone::CheckStart(): vdr info tells 16:9 but we are in broadcast and found 4:3, vdr info file must be wrong");
                    wrongAspectInfo = true;
                }
                if (aStart && !wrongAspectInfo) {
                    cMark *logoStopBefore = marks.GetPrev(aStart->position, MT_LOGOSTOP);
                    if (logoStopBefore) {
                        int diff = 1000 * (aStart->position - logoStopBefore->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckStart(): vdr info tells 16:9, logo stop mark (%d) %dms before aspect ratio start mark (%d)", logoStopBefore->position, diff, aStart->position);
                        if (diff <= 4440) {  // do not reduce, need this for logo fade in/out, changed from 4400 to 4440
                            dsyslog("cMarkAdStandalone::CheckStart(): logo stop mark short before aspect ratio start mark, aspect ratio info must be wrong");
                            if (aStopBefore && aStopBefore->position == 0) { // this is 4:3 from previous recording, no valid mark
                                dsyslog("cMarkAdStandalone::CheckStart(): delete invalid aspect stop mark at (%d)", aStopBefore->position);
                                marks.Del(aStopBefore->position);
                            }
                            wrongAspectInfo = true;
                        }
                    }
                }
            }
            else { // vdr info file tells 4:3 video
                if (aStart && aStopAfter && (aStart->position <= IGNORE_AT_START)) {  // we have a aspect ratio start mark at the recording start and a sspect ratio top mark
                    cMark *aStartAfter = marks.GetNext(aStart->position, MT_ASPECTSTART);
                    if (!aStartAfter) wrongAspectInfo = true;  // no aspect ratio start mark follows, the vdr info file must be wrong
                }
            }

            // fix wrong aspect ratio from vdr info file
            if (wrongAspectInfo || ((!earlyAspectChange) && ((macontext.Info.AspectRatio.num != macontext.Video.Info.AspectRatio.num) ||
                                                             (macontext.Info.AspectRatio.den != macontext.Video.Info.AspectRatio.den)))) {
                sAspectRatio newMarkAdAspectRatio;
                newMarkAdAspectRatio.num = 16;
                newMarkAdAspectRatio.den = 9;
                if ((macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
                    newMarkAdAspectRatio.num = 4;
                    newMarkAdAspectRatio.den = 3;
                }
                isyslog("video aspect description in info %d:%d wrong, correct to %d:%d", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, newMarkAdAspectRatio.num, newMarkAdAspectRatio.den);
                macontext.Info.AspectRatio.num = newMarkAdAspectRatio.num;
                macontext.Info.AspectRatio.den = newMarkAdAspectRatio.den;
                // we have to invert MT_ASPECTSTART and MT_ASPECTSTOP and fix position
                cMark *aMark = marks.GetFirst();
                while (aMark) {
                    if (aMark->type == MT_ASPECTSTART) {
                        aMark->type = MT_ASPECTSTOP;
                        aMark->position = recordingIndexMark->GetIFrameBefore(aMark->position - 1);
                    }
                    else {
                        if (aMark->type == MT_ASPECTSTOP) {
                            aMark->type = MT_ASPECTSTART;
                            aMark->position = recordingIndexMark->GetIFrameAfter(aMark->position + 1);
                        }
                    }
                    aMark = aMark->Next();
                }
                dsyslog("cMarkAdStandalone::CheckStart(): fixed marks are:");
                DebugMarks();     //  only for debugging
            }
        }
        if (macontext.Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264) {
            isyslog("HD video with aspect ratio of %i:%i detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
        }
        else {
            isyslog("SD video with aspect ratio of %i:%i detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            if (((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3))) {
                isyslog("logo/border detection disabled");
                bDecodeVideo = false;
                video->ClearBorder();
                macontext.Video.Options.ignoreAspectRatio = false;
                macontext.Video.Options.ignoreLogoDetection = true;
                macontext.Video.Options.ignoreBlackScreenDetection = true;
                marks.Del(MT_CHANNELSTART);
                marks.Del(MT_CHANNELSTOP);
                // start mark must be around iStartA
                begin = marks.GetAround(delta * 4, iStartA, MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark found at (%i)", begin->position);
                    if (begin->position > abs(iStartA) / 4) {    // this is a valid start
                        dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark at (%i) is valid, delete all logo and border marks", begin->position);
                        marks.Del(MT_LOGOSTART);  // we found MT_ASPECTSTART, we do not need weeker marks
                        marks.Del(MT_LOGOSTOP);
                        marks.Del(MT_HBORDERSTART);
                        marks.Del(MT_HBORDERSTOP);
                        marks.Del(MT_VBORDERSTART);
                        marks.Del(MT_VBORDERSTOP);
                        macontext.Video.Options.ignoreHborder = true;
                        macontext.Video.Options.ignoreVborder = true;
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark at (%i) very early, maybe from broascast before", begin->position);
                        cMark *aStopNext = marks.GetNext(begin->position, MT_ASPECTSTOP);
                        if (aStopNext) dsyslog("cMarkAdStandalone::CheckStart(): found MT_ASPECTSTOP (%i)", aStopNext->position);
                        else {
                            dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART is not valid (%i), ignoring", begin->position);
                            marks.Del(begin->position);  // delete invalid start mark to prevent to be selected again later
                            begin = NULL;
                        }
                        if (begin && (begin->position <= IGNORE_AT_START)) {
                            cMark *nextAspectStart = marks.GetNext(begin->position, MT_ASPECTSTART);
                            begin = NULL;
                            if (nextAspectStart) {
                                int diffAssumed = (nextAspectStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                                dsyslog("cMarkAdStandalone::CheckStart(): found next MT_ASPECTSTART at (%d), %ds after assumed start", nextAspectStart->position, diffAssumed);
                                if (diffAssumed < 580) {  // do not use too late next start, this can be 4:3 broadcast before 4:3 broadcast, changed from 851 to 580
                                    begin = nextAspectStart;
                                    dsyslog("cMarkAdStandalone::CheckStart(): found next MT_ASPECTSTART at (%i), use this", begin->position);
                                }
                            }
                            if (!begin) dsyslog("cMarkAdStandalone::CheckStart(): only got very early start aspect ratio and no later alternative, ignoring");
                        }
                    }
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): no MT_ASPECTSTART found");   // previous is 4:3 too, search another start mark
            }
            else { // recording is 16:9 but maybe we can get a MT_ASPECTSTART mark if previous recording was 4:3
                begin = marks.GetAround(delta * 3, iStartA, MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART found at (%i) because previous recording was 4:3", begin->position);
                    cMark *vBorderStart = marks.GetAround(delta, begin->position, MT_VBORDERSTART);  // do not use this mark if there is a later vborder start mark
                    if (vBorderStart && (vBorderStart->position >  begin->position)) {
                        dsyslog("cMarkAdStandalone::CheckStart(): found later MT_VBORDERSTAT, do not use MT_ASPECTSTART");
                        begin = NULL;
                    }
                    else {
                        cMark *logoStart = marks.GetAround(delta * 4, begin->position, MT_LOGOSTART);  // do not use this mark if there is a later logo start mark
                        if (logoStart && (logoStart->position > begin->position)) {
                            cMark *stopVBorder = marks.GetNext(begin->position, MT_VBORDERSTOP); // if we have vborder stop between aspect start and logo start mark
                                                                                                 // logo start mark is invalid
                            if (stopVBorder && (stopVBorder->position < logoStart->position)) {
                                dsyslog("cMarkAdStandalone::CheckStart(): vborder stop found at (%d), between aspect stop (%d) and logo stop (%d), aspect start is valid", stopVBorder->position, begin->position, logoStart->position);
                            }
                            else {
                                dsyslog("cMarkAdStandalone::CheckStart(): found later MT_LOGOSTART, do not use MT_ASPECTSTART");
                                begin = NULL;
                            }
                        }
                    }
                }
            }
        }
        macontext.Info.checkedAspectRatio = true;
        if (begin && (macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) marks.DelWeakFromTo(0, INT_MAX, MT_ASPECTCHANGE); // delete all weak marks
    }

    if (macontext.Video.Info.frameDarkOpeningCredits >= 0) { // check for very long dark opening credits
        dsyslog("cMarkAdStandalone::CheckStart(): found very long dark opening credits start at frame (%d), check which type of border mark is valid", macontext.Video.Info.frameDarkOpeningCredits);
        cMark *hStop = marks.GetNext(iStartA, MT_HBORDERSTOP);
        cMark *vStop = marks.GetNext(iStartA, MT_VBORDERSTOP);
        if (hStop && !vStop) {
            dsyslog("cMarkAdStandalone::CheckStart(): hborder stop found but no vborder stop, recording has vborder, change hborder start to vborder start at (%d), delete all hborder marks", macontext.Video.Info.frameDarkOpeningCredits);
            marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
            marks.Add(MT_VBORDERSTART, MT_UNDEFINED, macontext.Video.Info.frameDarkOpeningCredits, "start of opening credits", true);
        }
    }

// horizontal border start
    if (!begin) {
        cMark *hStart = marks.GetAround(iStartA + delta, iStartA + delta, MT_HBORDERSTART);
        if (hStart) { // we found a hborder start mark
            dsyslog("cMarkAdStandalone::CheckStart(): horizontal border start found at (%i)", hStart->position);

            // check if next broadcast is long enough
            cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
            if (hStop) {
                int lengthBroadcast = (hStop->position - hStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): next horizontal border stop mark (%d), length of broadcast %ds", hStop->position, lengthBroadcast);
                cMark *hNextStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
                if (((lengthBroadcast <= 235) && !hNextStart) || // hborder preview before broadcast start, changed from 165 to 231 to 235
                     (lengthBroadcast <=  74)) {                 // very short broadcast length is never valid
                    int diffAssumed = (hStop->position - iStartA) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStart(): horizontal border stop (%i) short after horizontal border start (%i) found, %ds after assumed start", hStop->position, hStart->position, diffAssumed); // do not delete weak marks here because it can only be from preview
                    if (diffAssumed < 477) hBorderStopPosition = hStop->position;  // maybe we can use this position as start mark if we found nothing else
                                                                                   // do not use too late marks, they can be hborder from preview or in a doku
                    dsyslog("cMarkAdStandalone::CheckStart(): delete horizontal border start (%d) and stop (%d) mark", hStart->position, hStop->position);
                    // delete hborder start/stop marks because we ignore hborder start mark
                    marks.Del(hStart->position);
                    marks.Del(hStop->position);
                    hStart = NULL;
                }
            }

            // check hborder start position
            if (hStart) {
                if (hStart->position >= IGNORE_AT_START) {  // position < IGNORE_AT_START is a hborder start from previous recording
                    dsyslog("cMarkAdStandalone::CheckStart(): delete logo and vborder marks if any");
                    marks.DelType(MT_VBORDERCHANGE, 0xF0);
                    marks.DelType(MT_LOGOCHANGE, 0xF0);
                    begin = hStart;   // found valid horizontal border start mark
                    macontext.Video.Options.ignoreVborder = true;
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): delete too early horizontal border mark (%d)", hStart->position);
                    marks.Del(hStart->position);
                    if ((marks.Count(MT_HBORDERCHANGE, 0xF0) == 0) && !macontext.Video.Logo.isInBorder) {
                        dsyslog("cMarkAdStandalone::CheckStart(): horizontal border since start, logo marks can not be valid");
                        marks.DelType(MT_LOGOCHANGE, 0xF0);
                    }
                }
            }
        }
        else { // we found no hborder start mark
            dsyslog("cMarkAdStandalone::CheckStart(): no horizontal border at start found, disable horizontal border detection");
            macontext.Video.Options.ignoreHborder = true;
            cMark *hStop = marks.GetAround(iStartA + delta, iStartA + delta, MT_HBORDERSTOP);
            if (hStop) {
                int pos = hStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): horizontal border stop without start mark found (%i), assume as start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from horizontal border stop (%d)", pos) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                begin=marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, pos, comment);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
            }
            dsyslog("cMarkAdStandalone::CheckStart(): delete horizontal border marks, if any");
            marks.DelType(MT_HBORDERCHANGE, 0xF0);  // mybe the is a late invalid hborder start marks, exists sometimes with old vborder recordings
        }
    }

// vertical border start
    if (!begin) {
        cMark *vStart = marks.GetAround(iStartA + delta, iStartA + delta, MT_VBORDERSTART);  // do not find initial vborder start from previous recording
        if (!vStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no vertical border at start found, ignore vertical border detection");
            macontext.Video.Options.ignoreVborder = true;
            marks.DelType(MT_VBORDERSTART, 0xFF);  // maybe we have a vborder start from a preview or in a doku, delete it
            cMark *vStop = marks.GetAround(iStartA + delta, iStartA + delta, MT_VBORDERSTOP);
            if (vStop) {
                int pos = vStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, pos, comment);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
            }
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): vertical border start found at (%i)", vStart->position);
            cMark *vStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
            if (vStop) {
                cMark *vNextStart = marks.GetNext(vStop->position, MT_VBORDERSTART);
                int markDiff = static_cast<int> (vStop->position - vStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop found at (%d), %ds after vertical border start", vStop->position, markDiff);
                if (vNextStart) {
                    dsyslog("cMarkAdStandalone::CheckStart(): vertical border start (%d) after vertical border stop (%d) found, start mark at (%d) is valid", vNextStart->position, vStop->position, vStart->position);
                }
                else { // we have only start/stop vborder in start part, this can be the closing credits of recording before
                    dsyslog("cMarkAdStandalone::CheckStart(): no vertical border start found after start (%d) and stop (%d)", vStart->position, vStop->position);
                    // 228s opening credits with vborder -> invalid TODO
                    //  96s opening credits with vborder -> invalid
                    // 151s advertising in start area    -> valid
                    if ((markDiff < 114) ||        // too short for a broadcast part, changed from 122 to 144 because of early ad found
                        (frameCurrent > iStopA)) { // we got not in broadcast at chkSTART with a vborder mark
                        dsyslog("cMarkAdStandalone::CheckStart():vertical border stop at (%d) %ds after vertical border start (%i) in start part found, this is not valid, delete marks", vStop->position, markDiff, vStart->position);
                        marks.Del(vStop);
                        marks.Del(vStart);
                        vStart = NULL;
                    }
                }
            }
            if (vStart) {
                if (vStart->position >= IGNORE_AT_START) { // early position is a vborder previous recording
                    dsyslog("cMarkAdStandalone::CheckStart(): delete logo and HBORDER marks if any");
                    marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
                    marks.DelType(MT_LOGOCHANGE,    0xF0); // delete logo marks, vborder is stronger
                    begin = vStart;                        // found valid vertical border start mark
                    macontext.Video.Info.hasBorder        = true;
                    macontext.Video.Options.ignoreHborder = true;
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): ignore vertical border start found at (%d)", vStart->position);
            }
        }
    }

// logo start
    if (!begin) {
        RemoveLogoChangeMarks();
        cMark *lStart = marks.GetAround(iStartA + (2 * delta), iStartA, MT_LOGOSTART);   // increase from 1
        if (!lStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no logo start mark found");
        }
        else {  // we got a logo start mark
            char *indexToHMSF = marks.IndexToHMSF(lStart->position);
            if (indexToHMSF) {
                dsyslog("cMarkAdStandalone::CheckStart(): logo start mark found on position (%i) at %s", lStart->position, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }

            // check if logo start mark is too early
            if (lStart->position  < (iStart / 8)) {  // start mark is too early, try to find a later mark
                cMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                if (lNextStart && (lNextStart->position  > (iStart / 8))) {  // found later logo start mark
                    int diffAssumed = (lNextStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                    char *indexToHMSFStart = marks.IndexToHMSF(lNextStart->position);
                    if (indexToHMSFStart) {
                        dsyslog("cMarkAdStandalone::CheckStart(): later logo start mark found on position (%i) at %s, %ds after assumed start", lNextStart->position, indexToHMSFStart, diffAssumed);
                        FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
                        free(indexToHMSFStart);
                    }
#define MAX_LOGO_AFTER_ASSUMED 398  // changed from 518 to 398
                                    // do not increase, sometimes there is a early first advertising
                    if (diffAssumed < MAX_LOGO_AFTER_ASSUMED) lStart = lNextStart;   // found better logo start mark
                    else dsyslog("cMarkAdStandalone::CheckStart(): next logo start mark too far after assumed start");
                }
            }

            // check if logo start mark is before hborder stop mark from previous recording
            if (lStart->position  < hBorderStopPosition) {  // start mark is before hborder stop of previous recording (hBorderStopPosition = -1 of no hborder stop)
                dsyslog("cMarkAdStandalone::CheckStart(): logo start mark (%d) is before hborder stop mark (%d) from previous recording", lStart->position, hBorderStopPosition);
                cMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                if (lNextStart && (lNextStart->position  > hBorderStopPosition)) {  // found later logo start mark
                    int diffAssumed = (lNextStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                    char *indexToHMSFStart = marks.IndexToHMSF(lNextStart->position);
                    if (indexToHMSFStart) {
                        dsyslog("cMarkAdStandalone::CheckStart(): later logo start mark found on position (%i) at %s", lNextStart->position, indexToHMSFStart);
                        FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
                        free(indexToHMSFStart);
                    }
                    if (diffAssumed < MAX_LOGO_AFTER_ASSUMED) lStart = lNextStart;   // found better logo start mark
                    else dsyslog("cMarkAdStandalone::CheckStart(): next logo start mark too far after assumed start");
                }
            }


            while (true) {
                // if the logo start mark belongs to closing credits logo stop/start pair, treat it as valid
                if (evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(lStart->position) == STATUS_YES)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): logo start mark (%d) is end of closing credits, this logo start mark is valid", lStart->position);
                    // check next stop/start pair, if near and short this is a failed logo detection or an undetected info/introduction logo
                    cMark *lNextStop  = marks.GetNext(lStart->position, MT_LOGOSTOP);
                    cMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                    if (lNextStart && lNextStop) {
                        int distance = 1000 * (lNextStop->position  - lStart->position)    / macontext.Video.Info.framesPerSecond;
                        int lengthAd = 1000 * (lNextStart->position - lNextStop->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckStart(): next logo stop (%d) start (%d), distance %dms, length %dms", lNextStop->position, lNextStart->position, distance, lengthAd);
                        if ((distance <= 6800) && (lengthAd <= 6840)) { // lengthAd changed from 4400 to 6840
                                                                        // distance changed from 1280 to 6800
                            dsyslog("cMarkAdStandalone::CheckStart(): logo stop/start pair after closing credits is invalid, deleting");
                            marks.Del(lNextStop->position);
                            marks.Del(lNextStart->position);
                        }
                    }
                    break;
                }

                // trust sequence blackscreen start / logo stop / blacksceen end / logo start as start of broadcast
                cMark *prev1 = marks.GetPrev(lStart->position);
                if (prev1 && (prev1->type == MT_NOBLACKSTART)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): blackscreen end (%d) before logo start (%d) found", prev1->position, lStart->position);
                    cMark *prev2 = marks.GetPrev(prev1->position);
                    if (prev2 && (prev2->type == MT_LOGOSTOP)) {
                        dsyslog("cMarkAdStandalone::CheckStart(): logo stop (%d) before blackscreen end (%d) before logo start (%d) found", prev2->position, prev1->position, lStart->position);
                        cMark *prev3 = marks.GetPrev(prev2->position);
                        if (prev3 && (prev3->type == MT_NOBLACKSTOP)) {
                            dsyslog("cMarkAdStandalone::CheckStart(): blackscreen start (%d) before logo stop (%d) before blackscreen end (%d) before logo start (%d) found",  prev3->position, prev2->position, prev1->position, lStart->position);
                            int blacklength = 1000 * (prev1->position  - prev3->position) / macontext.Video.Info.framesPerSecond;
                            dsyslog("cMarkAdStandalone::CheckStart(): blackscreen length %dms", blacklength);
                            if (blacklength >= 2160) {  // trust only a long blackscreen
                                dsyslog("cMarkAdStandalone::CheckStart(): sequence blackscreen start / logo stop / blacksceen end / logo start found, this is the broadcast start");
                                dsyslog("cMarkAdStandalone::CheckStart(): delete all other logo stop/start marks");
                                marks.DelFromTo(lStart->position + 1, INT_MAX, MT_LOGOCHANGE);
                                break;
                            }
                        }
                    }
                }

                // check next logo stop/start pair
                cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);  // get next logo stop mark
                if (lStop) {  // there is a next stop mark in the start range
                    int distanceStartStop = (lStop->position - lStart->position) / macontext.Video.Info.framesPerSecond;
                    if (distanceStartStop < 20) {  // very short logo part, lStart is possible wrong, do not increase, first ad can be early
                                                   // change from 55 to 20 because of too short logo change detected about 20s after start mark
                        indexToHMSF = marks.IndexToHMSF(lStop->position);
                        if (indexToHMSF) {
                            dsyslog("cMarkAdStandalone::CheckStart(): next logo stop mark found very short after start mark on position (%i) at %s, distance %ds", lStop->position, indexToHMSF, distanceStartStop);
                            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                            free(indexToHMSF);
                        }
                        cMark *lNextStart = marks.GetNext(lStop->position, MT_LOGOSTART); // get next logo start mark
                        if (lNextStart) {  // now we have logo start/stop/start, this can be a preview before broadcast start
                            indexToHMSF = marks.IndexToHMSF(lNextStart->position);
                            int distanceStopNextStart = (lNextStart->position - lStop->position) / macontext.Video.Info.framesPerSecond;
                            if (distanceStopNextStart <= 136) { // found start mark short after start/stop, use this as start mark, changed from 68 to 76 to 136
                                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) at %s %ds after logo start/stop marks, use this start mark", lNextStart->position, indexToHMSF, distanceStopNextStart);
                                lStart = lNextStart;
                            }
                            else {
                                if (indexToHMSF) {
                                    dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) at %s %ds after logo start/stop marks, distance not valid", lNextStart->position, indexToHMSF, distanceStopNextStart);
                                        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                                        free(indexToHMSF);
                                    break;
                                }
                            }
                            if (indexToHMSF) {
                                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                                free(indexToHMSF);
                            }
                        }
                        else break;
                    }
                    else {  // there is a next stop mark but too far away
                        dsyslog("cMarkAdStandalone::CheckStart(): next logo stop mark (%d) but too far away %ds", lStop->position, distanceStartStop);
                        break;
                    }
                }
                else break; // the is no next stop mark
            }
            if (lStart->position  >= (iStart / 8)) {
                begin = lStart;   // found valid logo start mark
            }
            else dsyslog("cMarkAdStandalone::CheckStart(): logo start mark (%d) too early, ignoring", lStart->position);
        }
        if (begin && (!macontext.Video.Logo.isInBorder)) {
            dsyslog("cMarkAdStandalone::CheckStart(): disable border detection and delete border marks");  // avoid false detection of border
            marks.DelType(MT_HBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
            marks.DelType(MT_VBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
            macontext.Video.Options.ignoreHborder = true;
            macontext.Video.Options.ignoreVborder = true;
        }
        if (begin) { // we found a logo start, cleanup invalid marks
            cMark *vBorderStop = marks.GetNext(begin->position, MT_VBORDERSTOP);
            if (vBorderStop) {
                dsyslog("cMarkAdStandalone::CheckStart(): found invalid MT_VBORDERSTOP at (%d) after logo start mark, delete MT_VBORDERSTOP", vBorderStop->position);
                marks.Del(vBorderStop->position);
            }
        }
    }

    if (begin && (begin->type != MT_RECORDINGSTART) && (begin->position <= IGNORE_AT_START)) {  // first frames are from previous recording
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

// no strong mark found, try anything
    if (!begin) {
        marks.DelTill(IGNORE_AT_START);    // we do not want to have a initial mark from previous recording as a start mark
        begin = marks.GetAround(2 * delta, iStartA, MT_START, 0x0F);  // not too big search range, blackscreen marks can be wrong
        if (begin) {
            dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) type 0x%X after search for any type", begin->position, begin->type);
            if (begin->type == MT_NOBLACKSTART) {
                int diff = 0;
                cMark *blackStop = marks.GetPrev(begin->position, MT_NOBLACKSTOP);
                if (blackStop) {
                    diff = 1000 * (begin->position - blackStop->position) / macontext.Video.Info.framesPerSecond; // trust long blackscreen
                    dsyslog("cMarkAdStandalone::CheckStart(): found black screen from (%d) to (%d), length %dms", blackStop->position, begin->position, diff);
                }
                if ((diff < 800) && (begin->position >= (iStartA + (120 * macontext.Video.Info.framesPerSecond)))) {  // use short blackscreen only short after assumed start
                    dsyslog("cMarkAdStandalone::CheckStart(): found only very late and short black screen start mark (%i), ignoring", begin->position);
                    begin = NULL;
                }
                if (begin && (begin->position < hBorderStopPosition)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): black screen start mark (%d) is before hborder stop mark (%d), ignoring", begin->position, hBorderStopPosition);
                    begin = NULL;
                }
            }
            else {
                if ((begin->inBroadCast) || macontext.Video.Options.ignoreLogoDetection){  // test on inBroadCast because we have to take care of black screen marks in an ad
                    char *indexToHMSF = marks.IndexToHMSF(begin->position);
                    if (indexToHMSF) {
                        dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%i) type 0x%X at %s inBroadCast %i", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                        free(indexToHMSF);
                    }
                }
                else { // mark in ad
                    char *indexToHMSF = marks.IndexToHMSF(begin->position);
                    if (indexToHMSF) {
                        dsyslog("cMarkAdStandalone::CheckStart(): start mark found but not inBroadCast (%i) type 0x%X at %s inBroadCast %i, ignoring", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                        free(indexToHMSF);
                    }
                    begin = NULL;
                }
            }
        }
    }
    if (begin && ((begin->position  / macontext.Video.Info.framesPerSecond) < 1) && (begin->type != MT_RECORDINGSTART)) { // do not accept marks in the first second, the are from previous recording, expect manual set MT_RECORDINGSTART fpr missed recording start
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

    // if we found a blackscreen start mark, check if we are in very long closing credit from previous recording
    if (begin && (begin->type == MT_NOBLACKSTART)) {
        bool isInvalid = true;
        while (isInvalid) {
            cMark *nextStop  = blackMarks.GetNext(begin->position, MT_NOBLACKSTOP);
            cMark *nextStart = blackMarks.GetNext(begin->position, MT_NOBLACKSTART);
            if (nextStart && nextStop) {
                int diff = (nextStop->position - iStartA) / macontext.Video.Info.framesPerSecond;
                int adLength = (nextStart->position - nextStop->position) / macontext.Video.Info.framesPerSecond;;
                dsyslog("cMarkAdStandalone::CheckStart(): next black screen from (%d) to (%d) in %ds after assued start (%d), length %ds", nextStop->position, nextStart->position, diff,iStartA, adLength);
                if ((diff <= 51) && (adLength >= 5)) {  // changed from 3 to 5, avoid long dark scenes
                    dsyslog("cMarkAdStandalone::CheckStart(): very long black screen short after, we are in the closing credits of previous recording");
                    begin = nextStart;
                }
                else isInvalid = false;
            }
            else isInvalid = false;
        }
    }

    if (begin) {
        marks.DelTill(begin->position);    // delete all marks till start mark
        char *indexToHMSF = marks.IndexToHMSF(begin->position);
        char *typeName    = marks.TypeToText(begin->type);
        if (indexToHMSF && typeName) isyslog("using %s start mark on position (%d) at %s as broadcast start", typeName, begin->position, indexToHMSF);
        if (indexToHMSF) {
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        if (typeName) {
            FREE(strlen(typeName)+1, "text");
            free(typeName);
        }

        if ((begin->type == MT_VBORDERSTART) || (begin->type == MT_HBORDERSTART)) {
            isyslog("found %s borders, logo detection disabled",(begin->type == MT_HBORDERSTART) ? "horizontal" : "vertical");
            macontext.Video.Options.ignoreLogoDetection = true;
            macontext.Video.Options.ignoreBlackScreenDetection = true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

        dsyslog("cMarkAdStandalone::CheckStart(): delete all black screen marks except start mark");
        cMark *mark = marks.GetFirst();   // delete all black screen marks because they are weak, execpt the start mark
        while (mark) {
            if (((mark->type & 0xF0) == MT_BLACKCHANGE) && (mark->position > begin->position) ) {
                cMark *tmp = mark;
                mark = mark->Next();
                marks.Del(tmp);  // delete mark from normal list
                continue;
            }
            mark = mark->Next();
        }
    }
    else { //fallback
        // try hborder stop mark as start mark
        if (hBorderStopPosition >= 0) {
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP from previous recoring as start mark");
            marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, hBorderStopPosition, "start mark from border stop of previous recording*", true);
            begin = marks.Get(hBorderStopPosition);
            marks.DelTill(hBorderStopPosition);
        }
        else {  // set start after pre timer
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, assume start time at pre recording time");
            marks.DelTill(iStart);
            marks.Del(MT_NOBLACKSTART);  // delete all black screen marks
            marks.Del(MT_NOBLACKSTOP);
            sMarkAdMark mark = {};
            mark.position = iStart;
            mark.type = MT_ASSUMEDSTART;
            AddMark(&mark);
            begin = marks.GetFirst();
        }
    }

    // now we have the final start mark, do fine tuning
    if (begin->type == MT_HBORDERSTART) { // we found a valid hborder start mark, check black screen because of closing credits from broadcast before
        cMark *blackMark = blackMarks.GetNext(begin->position, MT_NOBLACKSTART);
        if (blackMark) {
            int diff =(blackMark->position - begin->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckStart(): black screen (%d) after, distance %ds", blackMark->position, diff);
            if (diff <= 6) {
                dsyslog("cMarkAdStandalone::CheckStart(): move horizontal border (%d) to end of black screen (%d)", begin->position, blackMark->position);
                marks.Move(begin, blackMark->position, "black screen");
            }
        }
   }

// count logo STOP/START pairs
    int countStopStart = 0;
    cMark *mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && (mark->Next()->type == MT_LOGOSTART)) {
            countStopStart++;
        }
        mark = mark->Next();
    }
    if (countStopStart >= 4) {  // changed from 3 to 4, sometimes there are a lot of previews in start area
        isyslog("%d logo STOP/START pairs found after start mark, something is wrong with your logo", countStopStart);
        if (video->ReducePlanes()) {
            dsyslog("cMarkAdStandalone::CheckStart(): reduce logo processing to first plane and delete all marks after start mark (%d)", begin->position);
            marks.DelFrom(begin->position);
        }
    }

    CheckStartMark();
    LogSeparator();
    CalculateCheckPositions(marks.GetFirst()->position);
    iStart = 0;
    marks.Save(directory, &macontext, false);
    DebugMarks();     //  only for debugging
    LogSeparator();
    return;
}


// check if start mark is valid
// check for short start/stop pairs at the start
void cMarkAdStandalone::CheckStartMark() {
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckStartMark(): check for short start/stop pairs at start");
    DebugMarks();                   //  only for debugging
    cMark *mark = marks.GetFirst(); // this is the start mark
    if (mark) {
        cMark *markStop = marks.GetNext(mark->position, MT_STOP, 0x0F);
        if (markStop) {
            int minFirstBroadcast = 8;                                   // trust strong marks
            if (mark->type <= MT_NOBLACKSTART)   minFirstBroadcast = 96; // do not trust weak marks
            else if (mark->type == MT_LOGOSTART) minFirstBroadcast = 68; // do not increase, there a broadcasts with early first advertising, changed from 71 to 68
									 // there can be short stop/start from a undetected info logo
            int lengthFirstBroadcast = (markStop->position - mark->position) / macontext.Video.Info.framesPerSecond; // length of the first broadcast part
            dsyslog("cMarkAdStandalone::CheckStartMark(): first broadcast length %ds from (%d) to (%d) (expect <=%ds)", lengthFirstBroadcast, mark->position, markStop->position, minFirstBroadcast);
            cMark *markStart = marks.GetNext(markStop->position, MT_START, 0x0F);
            if (markStart) {
                int lengthFirstAd = 1000 * (markStart->position - markStop->position) / macontext.Video.Info.framesPerSecond; // length of the first broadcast part
                dsyslog("cMarkAdStandalone::CheckStartMark(): first advertising length %dms from (%d) to (%d)", lengthFirstAd, markStop->position, markStart->position);
                if (lengthFirstAd <= 1000) {
                    dsyslog("cMarkAdStandalone::CheckStartMark(): very short first advertising, this can be a logo detection failure");
                }
                else {
                    if (lengthFirstBroadcast < minFirstBroadcast) {
                        dsyslog("cMarkAdStandalone::CheckStartMark(): too short STOP/START/STOP sequence at start, delete first pair");
                        marks.Del(mark->position);
                        marks.Del(markStop->position);
                    }
                }
            }
        }
    }
}


void cMarkAdStandalone::LogSeparator(const bool main) {
    if (main) dsyslog("=======================================================================================================================");
    else      dsyslog("-----------------------------------------------------------------------------------------------------------------------");
}


// write all current marks to log file
//
void cMarkAdStandalone::DebugMarks() {           // write all marks to log file
    dsyslog("*************************************************************");
    dsyslog("cMarkAdStandalone::DebugMarks(): current marks:");
    cMark *mark = marks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            dsyslog("mark at position %6i type 0x%X at %s inBroadCast %i", mark->position, mark->type, indexToHMSF, mark->inBroadCast);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
    tsyslog("cMarkAdStandalone::DebugMarks(): current black screen marks:");
    mark = blackMarks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            tsyslog("mark at position %6i type 0x%X at %s inBroadCast %i", mark->position, mark->type, indexToHMSF, mark->inBroadCast);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
    dsyslog("*************************************************************");
}


void cMarkAdStandalone::CheckMarks(const int endMarkPos) {           // cleanup marks that make no sense
    LogSeparator(true);

    // remove invalid marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): remove invalid marks");
    DebugMarks();     //  only for debugging
    marks.DelInvalidSequence();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // if stop/start distance is too big, remove pair
        if (((mark->type & 0x0F) == MT_STOP) && (mark->Next()) && ((mark->Next()->type & 0x0F) == MT_START)) {
            int diff = (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckMarks(): check advertising from stop (%6d) to start (%6d), length %4ds", mark->position, mark->Next()->position, diff);
            if (diff > 3600) {  // assumed possible longest advertising
                dsyslog("cMarkAdStandalone::CheckMarks(): delete invalid pair");
                cMark *tmp = mark->Next()->Next();
                marks.Del(mark->Next());
                marks.Del(mark);
                mark = tmp;
                continue;
            }
        }

        // if no stop mark at the end, add one
        if (!inBroadCast || gotendmark) {  // in this case we will add a stop mark at the end of the recording
            if (((mark->type & 0x0F) == MT_START) && (!mark->Next())) {      // delete start mark at the end
                if (marks.GetFirst()->position != mark->position) {        // do not delete start mark
                    dsyslog("cMarkAdStandalone::CheckMarks(): START mark at the end, deleting %i", mark->position);
                    marks.Del(mark);
                    break;
                }
            }
        }
        mark = mark->Next();
    }

// delete logo and border marks if we have channel marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete logo marks if we have channel or border marks");
    DebugMarks();     //  only for debugging
    cMark *channelStart = marks.GetNext(0, MT_CHANNELSTART);
    cMark *channelStop = marks.GetNext(0, MT_CHANNELSTOP);
    cMark *hborderStart = marks.GetNext(0, MT_HBORDERSTART);
    cMark *hborderStop = marks.GetNext(0, MT_HBORDERSTOP);
    cMark *vborderStart = marks.GetNext(0, MT_VBORDERSTART);
    cMark *vborderStop = marks.GetNext(0, MT_VBORDERSTOP);
    if (hborderStart && hborderStop) {
        int hDelta = (hborderStop->position - hborderStart->position) / macontext.Video.Info.framesPerSecond;
        if (hDelta <= 230) {  // changed from 120 to 230
            dsyslog("cMarkAdStandalone::CheckMarks(): found hborder start (%d) and stop (%d), but distance %d too short, try if there is a next pair", hborderStart->position, hborderStop->position, hDelta);
            hborderStart = marks.GetNext(hborderStart->position, MT_HBORDERSTART);
            hborderStop = marks.GetNext(hborderStop->position, MT_HBORDERSTOP);
        }
    }
    if (vborderStart && vborderStop) {
        int vDelta = (vborderStop->position - vborderStart->position) / macontext.Video.Info.framesPerSecond;
        if (vDelta < 120) {
            dsyslog("cMarkAdStandalone::CheckMarks(): found vborder stop/start, but distance %d too short, try if there is a next pair", vDelta);
            vborderStart = marks.GetNext(vborderStart->position, MT_VBORDERSTART);
            vborderStop = marks.GetNext(vborderStop->position, MT_VBORDERSTOP);
        }
    }
    if (channelStart && channelStop) {
        mark = marks.GetFirst();
        while (mark) {
            if (mark != marks.GetFirst()) {
                if (mark == marks.GetLast()) break;
                if ((mark->type == MT_LOGOSTART) || (mark->type == MT_LOGOSTOP)) {
                    cMark *tmp = mark;
                    mark = mark->Next();
                    dsyslog("cMarkAdStandalone::CheckMarks(): we have channel marks, delete logo mark (%i)", tmp->position);
                    marks.Del(tmp);
                    continue;
                }
            }
            mark = mark->Next();
        }
    }

    if ((hborderStart && hborderStop) || (vborderStart && vborderStop)) {
        mark = marks.GetFirst();
        while (mark) {
            if (mark != marks.GetFirst()) {
                if (((mark->type == MT_LOGOSTART) || (mark->type == MT_LOGOSTOP)) && (mark->position != endMarkPos)) {  // do not delete mark on end position, can be a logo mark even if we have border
                    cMark *tmp = mark;
                    mark = mark->Next();
                    dsyslog("cMarkAdStandalone::CheckMarks(): we have border marks, delete logo mark (%i)", tmp->position);
                    marks.Del(tmp);
                    continue;
                }
            }
            mark = mark->Next();
        }
    }

// delete all black sceen marks expect start or end mark
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete invalid black sceen marks");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if (mark != marks.GetFirst()) {
            if (mark == marks.GetLast()) break;
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) {
                cMark *tmp = mark;
                mark = mark->Next();
                dsyslog("cMarkAdStandalone::CheckMarks(): delete black screen mark (%i)", tmp->position);
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

// delete very short logo stop/start pairs
// contains start mark, do not delete
// diff 880 lengthAfter 203
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete very short logo stop/start pairs");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int diff = 1000 * (mark->Next()->position - mark->position) /  macontext.Video.Info.framesPerSecond;
            if (diff < 200 ) { // do not increase because of very short real logo interuption between broacast and preview, changed from 1000 to 920 to 520 to 200
                if (mark->Next()->Next() && (mark->Next()->Next()->type == MT_LOGOSTOP)) {
                    int lengthBefore = -1;
                    if (mark->Prev() && (mark->Prev()->type == MT_LOGOSTART)) lengthBefore = (mark->position - mark->Prev()->position) /  macontext.Video.Info.framesPerSecond;
                    int lengthAfter = (mark->Next()->Next()->position - mark->Next()->position) /  macontext.Video.Info.framesPerSecond;
                    if ((lengthBefore < 816) && (lengthAfter < 139)) {  // do not delete a short stop/start before or after a long broadcast part
                                                                        //  this pair contains start mark, lengthAfter changed from 203 to 139
                        cMark *tmp = mark->Next()->Next();
                        dsyslog("cMarkAdStandalone::CheckMarks(): very short logo stop (%d) and logo start (%d) pair, length %dms, distance before %ds after %ds, deleting", mark->position, mark->Next()->position, diff, lengthBefore, lengthAfter);
                        marks.Del(mark->Next());
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }
                    else dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%d) and logo start (%d) pair, diff %dms, length %ds, long broadcast after, this can be a start mark", mark->position, mark->Next()->position, diff, lengthAfter);
                }
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%d) and logo start (%d) pair, diff %dms long enough", mark->position, mark->Next()->position, diff);
        }
        mark = mark->Next();
    }

// delete short START STOP logo marks because they are previews in the advertisement
// preview detection chain:
// logo start -> logo stop:  broadcast before advertisement
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  preview  (this start mark is the current mark in the loop)
//                           first start mark could not be a preview
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  broadcast after advertisement
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): detect previews in advertisement");
    DebugMarks();     //  only for debugging
    cMark *endMark = marks.GetLast();
    mark = marks.GetFirst();
    while (mark) {
// check logo marks
        if ((mark->type == MT_LOGOSTART) && (mark->position != marks.GetFirst()->position)) {  // not start mark
            LogSeparator();
            dsyslog("cMarkAdStandalone::CheckMarks(): check logo start mark (%d) for preview start", mark->position);

            cMark *stopMark = marks.GetNext(mark->position, MT_LOGOSTOP);
            if (stopMark && (stopMark->type == MT_LOGOSTOP) && (stopMark->position != marks.GetLast()->position)) { // next logo stop mark not end mark
                // check distance to current end mark, near end mark there can not be a preview, it must be a logo detection failure
                int diffEnd = (endMark->position - stopMark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckMarks(): distance from logo stop mark (%d) to current end mark (%d) is %ds", stopMark->position, endMark->position, diffEnd);
                if (diffEnd > 38) {
                    // check marks before and after
                    cMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
                    cMark *startAfter= marks.GetNext(stopMark->position, MT_LOGOSTART);
                    if (stopBefore && startAfter) {  // if advertising before is long this is the really the next start mark
                        int lengthAdBefore = static_cast<int> (1000 * (mark->position - stopBefore->position) / macontext.Video.Info.framesPerSecond);
                        int lengthAdAfter = static_cast<int> (1000 * (startAfter->position - stopMark->position) / macontext.Video.Info.framesPerSecond);
                        int lengthPreview = static_cast<int> ((stopMark->position - mark->position) / macontext.Video.Info.framesPerSecond);
                        dsyslog("cMarkAdStandalone::CheckMarks(): start (%d) stop (%d): length %ds, length ad before %dms, length ad after %dms", mark->position, stopMark->position, lengthPreview, lengthAdBefore, lengthAdAfter);
                        if ((lengthAdBefore >= 800) || (lengthAdAfter >= 360)) {  // check if we have ad before or after preview. if not it is a logo detection failure
                                                                                  // before changed from 1400 to 1360 to 800 to 360
                                                                                  // after changed from 3200 to 2160 to 1560 to 800
                            if (((lengthAdBefore >= 120) && (lengthAdBefore <= 585000) && (lengthAdAfter >= 200)) || // if advertising before is long this is the next start mark
                                                                                                                     // previews can be at start of advertising (e.g. DMAX)
                                                                                                                     // before min changed from 920 to 520 to 200 to 120
                                                                                                                     // before max changed from 500000 to 560000 to 585000
                                                                                                                     // found very short logo invisible betweewn broascast
                                                                                                                     // and first preview
                                                                                                                     // after min changed from 840 to 560 to 520 to 200
                                ((lengthAdBefore >= 354200) && (lengthAdAfter >= 80))) {   // accept very short logo interuption after long ad
                                                                                           // this is between last preview and broadcast start
                                if (lengthPreview <= 120) {  // changed from 111 to 113 to 120
                                    // check if this logo stop and next logo start are closing credits, in this case stop mark is valid
                                    bool isNextClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(startAfter->position) == STATUS_YES);
                                    if (!isNextClosingCredits || (stopMark->position != marks.GetLast()->position)) { // check valid only for last mark
                                        // check if this logo start mark and previuos logo stop mark are closing credits with logo, in this case logo start mark is valid
                                        // this only work on the first logo stark mark because there are closing credits in previews
                                        bool isThisClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(mark->position) == STATUS_YES);
                                        if (!isThisClosingCredits || (stopMark->position != marks.GetFirst()->position)) {
                                            isyslog("found preview between logo start mark (%d) and logo stop mark (%d) in advertisement, deleting marks", mark->position, stopMark->position);
                                            cMark *tmp = startAfter;
                                            marks.Del(mark);
                                            marks.Del(stopMark);
                                            mark = tmp;
                                            continue;
                                        }
                                        else dsyslog("cMarkAdStandalone::CheckMarks(): long advertisement before and logo stop before and this logo start mark are closing credits, this pair contains a start mark");
                                    }
                                    else dsyslog("cMarkAdStandalone::CheckMarks(): next stop/start pair are closing credits, this pair contains a stop mark");
                                }
                                else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%d) and (%d), length %ds not valid", mark->position, mark->Next()->position, lengthPreview);
                            }
                            else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%d) and (%d), length advertising before %ds or after %dms is not valid", mark->position, mark->Next()->position, lengthAdBefore, lengthAdAfter);
                        }
                        else dsyslog("cMarkAdStandalone::CheckMarks(): not long enought ad before and after preview, maybe logo detection failure");
                    }
                    else dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_LOGOSTOP before or MT_LOGOSTART after found");
                }
                else dsyslog("cMarkAdStandalone::CheckMarks(): no preview because distance from logo stop mark to current end mark too short");
            }
        }
        mark = mark->Next();
    }

// delete invalid short START STOP hborder marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): check border marks");
    DebugMarks();     //  only for debugging
    // delete start stop hborder pairs at before chkSTART if there are no other hborder marks, they are a preview with hborder before recording start
    mark = marks.GetFirst();
    if (mark && (mark->type == MT_HBORDERSTART) && mark->Next() && (mark->Next()->type == MT_HBORDERSTOP) && (mark->Next()->position < chkSTART) && (marks.Count(MT_HBORDERSTART) == 1) && (marks.Count(MT_HBORDERSTOP) == 1)) {
        dsyslog("cMarkAdStandalone::CheckMarks(): preview with hborder before recording start found, delete start (%d) stop (%d)", mark->position, mark->Next()->position);
        marks.Del(mark->Next()->position);
        marks.Del(mark->position);
    }
    // delete short START STOP hborder marks with logo start mark between, because they are advertisement with border in the advertisement
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_HBORDERSTART) && (mark->position != marks.GetFirst()->position) && mark->Next()) {  // not start or end mark
            cMark *bStop = marks.GetNext(mark->position, MT_HBORDERSTOP);
            if (bStop && (bStop->position != marks.GetLast()->position)) { // next mark not end mark
                int lengthAd = static_cast<int> ((bStop->position - mark->position) / macontext.Video.Info.framesPerSecond);
                if (lengthAd < 130) { // increased from 70 to 130
                    isyslog("found advertisement of length %is between hborder mark (%i) and hborder mark (%i), deleting marks", lengthAd, mark->position, bStop->position);
                    cMark *logoStart = marks.GetNext(mark->position, MT_LOGOSTART);
                    if (logoStart && (logoStart->position <= bStop->position)) { // if there is a logo start between hborder start and bhorder end, it is the logo start of the preview, this is invalid
                        dsyslog("cMarkAdStandalone::CheckMarks(): invalid logo start mark between hborder start/stop, delete (%d)", logoStart->position);
                        marks.Del(logoStart);
                    }
                    cMark *tmp = mark;
                    mark = mark->Next();  // this can be the border stop mark or any other mark in between
                    if (mark->position == bStop->position) mark = mark->Next();  // there can be other marks in between
                    marks.Del(tmp);
                    marks.Del(bStop);
                    continue;
                }
            }
        }
        mark = mark->Next();
    }

// check start marks
// check for short start/stop pairs at the start
    CheckStartMark();

// check blackscreen and assumed end mark
// check for better end mark not very far away from assuemd end
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): check for near better end mark in case of recording length is too big");
    DebugMarks();     //  only for debugging
    mark = marks.GetLast();
    if (mark && ((mark->type & 0xF0) < MT_CHANNELCHANGE)) {  // trust only channel marks and better
        int maxBeforeAssumed;           // max 5 min before assumed stop
        switch(mark->type) {
            case MT_ASSUMEDSTOP:
                maxBeforeAssumed = 449; // try hard to get a better end mark, changed from 389 to 532 to 449
                                        // not not increase to prevent to get preview stop from last advertising
                break;
            case MT_NOBLACKSTOP:
                maxBeforeAssumed = 351; // try a litte more to get end mark, changed from 389 to 351
                break;
            case MT_LOGOSTOP:
                maxBeforeAssumed = 198; // changed from 299 to 198
                break;
            case MT_VBORDERSTOP:
                maxBeforeAssumed = 288;
                break;
            default:
                maxBeforeAssumed = 300;                               // max 5 min before assumed stop
                break;
        }

        cMark *markPrev = marks.GetPrev(mark->position);
        if (markPrev) {
            int diffStart = (mark->position - markPrev->position) / macontext.Video.Info.framesPerSecond; // length of the last broadcast part, do not check it only depends on after timer
            dsyslog("cMarkAdStandalone::CheckMarks(): last broadcast length %ds from (%d) to (%d)", diffStart, markPrev->position, mark->position);
            if (diffStart >= 15) { // changed from 17 to 15
                cMark *markStop = marks.GetPrev(markPrev->position);
                if (markStop) {
                    int diffStop = (markPrev->position - markStop->position) / macontext.Video.Info.framesPerSecond; // distance of the logo stop/start pair before last pair
                    int maxAd = 390;  // changed from 163 to 196 to 390
                    // if start mark is hborder and end mark is not hborder, try last hborder stop mark with greater distance
                    if ((markStop->type == MT_HBORDERSTOP) && (markPrev->type != MT_HBORDERSTART) && (mark->type != MT_HBORDERSTOP)) { // we have hborder marks, but end pair is not
                        maxAd            = 505;
                        maxBeforeAssumed = 755;
                    }
                    int iStopA2 = marks.GetFirst()->position + macontext.Video.Info.framesPerSecond * (length + macontext.Config->astopoffs);  // we have to recalculate iStopA
                    if (markPrev->type == MT_LOGOSTART) {
                        int diffAssumedStopStart = (iStopA2 - markPrev->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckMarks(): last logo start mark (%d) is %ds before assumed stop (%d)", markPrev->position, diffAssumedStopStart, iStopA2);
                        if (diffAssumedStopStart <= 16) {
                            maxAd            = 529;
                            maxBeforeAssumed = 498;
                        }
                    }
                    dsyslog("cMarkAdStandalone::CheckMarks(): last advertising length %ds (expect <=%ds) from (%d) to (%d)", diffStop, maxAd, markStop->position, markPrev->position);

                    if ((diffStop > 2) && (diffStop <= maxAd)) { // changed from 0 to 2 to avoid move to logo detection failure
                        if ((mark->type != MT_LOGOSTOP) || (diffStop < 11) || (diffStop > 12)) { // ad from 11s to 12s can be undetected info logo at the end (SAT.1 or RTL2)
                            int diffAssumed = (iStopA2 - markStop->position) / macontext.Video.Info.framesPerSecond; // distance from assumed stop
                            dsyslog("cMarkAdStandalone::CheckMarks(): last stop mark (%d) %ds (expect <=%ds) before assumed stop (%d)", markStop->position, diffAssumed, maxBeforeAssumed, iStopA2);
                            if (diffAssumed <= maxBeforeAssumed) {
                                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark before as end mark, assume too big recording length");
                                marks.Del(mark->position);
                                marks.Del(markPrev->position);
                            }
                        }
                    }
                }
            }
        }
    }

// delete short START STOP logo marks because they are previes not detected above or due to next broadcast
// delete short STOP START logo marks because they are logo detection failure
// delete short STOP START hborder marks because some channels display information in the border
// delete short STOP START vborder marks because they are from malfunction recording
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): remove logo and hborder detection failure marks");
    DebugMarks();     //  only for debugging

    // first pass, delete all very short STOP START logo marks because they are logo detection failure
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_LOGOSTOP) && (mark->Next()) && (mark->Next()->type == MT_LOGOSTART)) {
            int diff = 1000 * (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
            if (diff <= 360) {
                dsyslog("cMarkAdStandalone::CheckMarks(): very short logo stop (%d) start (%d) length %dms, deleting", mark->position, mark->Next()->position, diff);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

    // second pass, delete rest
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->position > marks.GetFirst()->position) && (mark->type == MT_LOGOSTART) && mark->Next() && mark->Next()->type == MT_LOGOSTOP) {  // do not delete selected start mark
                                                                                                                                                   // next logo stop/start pair could be "Teletext ..." info
            int MARKDIFF = static_cast<int> (macontext.Video.Info.framesPerSecond * 38); // changed from 8 to 18 to 35 to 38
            double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                isyslog("mark distance between logo START and STOP too short %.1fs, deleting (%i,%i)", distance, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): mark distance between logo START and STOP %.1fs, keep (%i,%i)", distance, mark->position, mark->Next()->position);
        }
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.framesPerSecond * 23);   // assume thre is shortest advertising, changed from 20s to 23s
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
                isyslog("mark distance between logo STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        if ((mark->type == MT_HBORDERSTOP) && mark->Next() && mark->Next()->type == MT_HBORDERSTART) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.framesPerSecond * 20);  // increased from 15 to 20
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
                isyslog("mark distance between horizontal STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        if ((mark->type == MT_VBORDERSTOP) && mark->Next() && mark->Next()->type == MT_VBORDERSTART) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.framesPerSecond * 2);
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
                isyslog("mark distance between vertical STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

// if we have a VPS events, move start and stop mark to VPS event
    LogSeparator();
    if (macontext.Config->useVPS) {
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): apply VPS events");
        DebugMarks();     //  only for debugging
        int vpsOffset = marks.LoadVPS(macontext.Config->recDir, "START:"); // VPS start mark
        if (vpsOffset >= 0) {
            isyslog("VPS start event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_START, false);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS start event found");

        vpsOffset = marks.LoadVPS(macontext.Config->recDir, "PAUSE_START:");     // VPS pause start mark = stop mark
        if (vpsOffset >= 0) {
            isyslog("VPS pause start event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_STOP, true);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause start event found");

        vpsOffset = marks.LoadVPS(macontext.Config->recDir, "PAUSE_STOP:");     // VPS pause stop mark = start mark
        if (vpsOffset >= 0) {
            isyslog("VPS pause stop  event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_START, true);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause stop event found");

        vpsOffset = marks.LoadVPS(macontext.Config->recDir, "STOP:");     // VPS stop mark
        if (vpsOffset >= 0) {
            isyslog("VPS stop  event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_STOP, false);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS stop event found");

// once again check marks
        mark = marks.GetFirst();
        while (mark) {
            if (((mark->type & 0x0F)==MT_START) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_START)) {  // two start marks, delete second
                dsyslog("cMarkAdStandalone::CheckMarks(): start mark (%i) followed by start mark (%i) delete non VPS mark", mark->position, mark->Next()->position);
                if (mark->type == MT_VPSSTART) {
                    marks.Del(mark->Next());
                    continue;
                }
                if (mark->Next()->type == MT_VPSSTART) {
                    cMark *tmp=mark;
                    mark = mark->Next();
                    marks.Del(tmp);
                    continue;
                }
            }
            if (((mark->type & 0x0F)==MT_STOP) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_STOP)) {  // two stop marks, delete second
                dsyslog("cMarkAdStandalone::CheckMarks(): stop mark (%i) followed by stop mark (%i) delete non VPS mark", mark->position, mark->Next()->position);
                if (mark->type == MT_VPSSTOP) {
                    marks.Del(mark->Next());
                    continue;
                }
                if (mark->Next()->type == MT_VPSSTOP) {
                    cMark *tmp=mark;
                    mark = mark->Next();
                    marks.Del(tmp);
                    continue;
                }
            }
            if (((mark->type & 0x0F) == MT_START) && (!mark->Next())) {      // delete start mark at the end
                if (marks.GetFirst()->position != mark->position) {        // do not delete start mark
                    dsyslog("cMarkAdStandalone::CheckMarks(): START mark at the end, deleting %i", mark->position);
                    marks.Del(mark);
                    break;
                }
            }
            mark = mark->Next();
        }
    }

    marks.DelInvalidSequence();

    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): final marks:");
    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type, const bool isPause) {
    if (!ptr_cDecoder) return;
    int delta = macontext.Video.Info.framesPerSecond * MAXRANGE;
    int vpsFrame = recordingIndexMark->GetFrameFromOffset(offset * 1000);
    if (vpsFrame < 0) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): failed to get frame from offset %d", offset);
    }
    cMark *mark = NULL;
    char *comment = NULL;
    char *timeText = NULL;
    if (!isPause) {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetAround(delta, vpsFrame, MT_START, 0x0F) :  marks.GetAround(delta, vpsFrame, MT_STOP, 0x0F);
    }
    if (!mark) {
        if (isPause) {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): no mark found to replace with pause mark, add new mark");
            if (asprintf(&comment,"VPS %s (%d)%s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, (type == MT_START) ? "*" : "") == -1) comment=NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, MT_UNDEFINED, vpsFrame, comment);
            FREE(strlen(comment)+1,"comment");
            free(comment);
            return;
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found no mark found to replace");
        return;
    }
    if ( (type & 0x0F) != (mark->type & 0x0F)) return;

    timeText = marks.IndexToHMSF(mark->position);
    if (timeText) {
        if ((mark->type > MT_LOGOCHANGE) && (mark->type != MT_RECORDINGSTART)) {  // keep strong marks, they are better than VPS marks
                                                                                  // for VPS recording we replace recording start mark
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep mark at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
        }
        else {
            int diff = 1000 * abs(mark->position - vpsFrame) / macontext.Video.Info.framesPerSecond;
            if (diff > 5500) { // near blackscreen my be better than VPS event, chaned from 2640 to 5500
                dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s, %d ms away", mark->position, mark->type, timeText, diff);
                char *markTypeText =  marks.TypeToText(mark->type);
                if (asprintf(&comment,"VPS %s (%d), moved from %s mark (%d) at %s %s", (type == MT_START) ? "start" : "stop", vpsFrame, markTypeText, mark->position, timeText, (type == MT_START) ? "*" : "") == -1) comment=NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                FREE(strlen(markTypeText)+1, "text");  // alloc in TypeToText
                free(markTypeText);

                dsyslog("cMarkAdStandalone::AddMarkVPS(): delete mark on position (%d)", mark->position);
                marks.Del(mark->position);
                marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, MT_UNDEFINED, vpsFrame, comment);
                FREE(strlen(comment)+1,"comment");
                free(comment);
                if ((type == MT_START) && !isPause) {   // delete all marks before vps start
                    marks.DelWeakFromTo(0, vpsFrame, 0xFF);
                }
                else if ((type == MT_STOP) && isPause) {  // delete all marks between vps start and vps pause start
                    cMark *startVPS = marks.GetFirst();
                    if (startVPS && (startVPS->type == MT_VPSSTART)) {
                        marks.DelWeakFromTo(startVPS->position, vpsFrame, MT_VPSCHANGE);
                    }
                }
            }
            else dsyslog("cMarkAdStandalone::AddMarkVPS(): keep near blackscreen mark at frame (%d)", mark->position);
        }
        FREE(strlen(timeText)+1, "indexToHMSF");
        free(timeText);
    }
}


void cMarkAdStandalone::AddMark(sMarkAdMark *mark) {
    if (!mark) return;
    if (!mark->type) return;
    if ((macontext.Config) && (macontext.Config->logoExtraction != -1)) return;
    if (gotendmark) return;

    char *comment = NULL;
    switch (mark->type) {
        case MT_ASSUMEDSTART:
            if (asprintf(&comment, "assuming start (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_ASSUMEDSTOP:
            if (asprintf(&comment, "assuming stop  (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_NOBLACKSTART:
            if (asprintf(&comment, "detected end of black screen   (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_NOBLACKSTOP:
            if (asprintf(&comment, "detected start of black screen (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_LOGOSTART:
            if (asprintf(&comment, "detected logo start (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_LOGOSTOP:
            if (asprintf(&comment, "detected logo stop  (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_HBORDERSTART:
            if (asprintf(&comment, "detected start of horiz. borders (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_HBORDERSTOP:
            if (asprintf(&comment, "detected stop  of horiz. borders (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_VBORDERSTART:
            if (asprintf(&comment, "detected start of vert. borders (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_VBORDERSTOP:
            if (asprintf(&comment, "detected stop  of vert. borders (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_ASPECTSTART:
            if (!mark->AspectRatioBefore.num) {
                if (asprintf(&comment, "aspect ratio start with %2d:%d (%6d)*", mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
            }
            else {
                if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%6d)*", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den,
                         mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                if ((macontext.Config->autoLogo > 0) &&( mark->position > 0) && bDecodeVideo) {
                    isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
                    macontext.Video.Options.ignoreLogoDetection = false;
                    macontext.Video.Options.ignoreBlackScreenDetection = false;
                }
            }
            break;
        case MT_ASPECTSTOP:
            if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%6d) ", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && bDecodeVideo) {
                isyslog("logo detection reenabled at frame (%d)", mark->position);
                macontext.Video.Options.ignoreLogoDetection = false;
                macontext.Video.Options.ignoreBlackScreenDetection = false;
            }
            break;
        case MT_CHANNELSTART:
            if (asprintf(&comment, "audio channel change from %i to %i (%6d)*", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            if (!macontext.Audio.Info.channelChange && (mark->position > iStopA / 2)) {
                dsyslog("AddMark(): first channel start at frame (%d) after half of assumed recording length at frame (%d), this is start mark of next braoscast", mark->position, iStopA / 2);
            }
            else macontext.Audio.Info.channelChange = true;
            break;
        case MT_CHANNELSTOP:
            if ((mark->position > chkSTART) && (mark->position < iStopA / 2) && !macontext.Audio.Info.channelChange) {
                dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after chkSTART, disable logo/border/aspect detection now");
                if (iStart == 0) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we heve selected a start mark
                bDecodeVideo = false;
                video->ClearBorder();
                macontext.Video.Options.ignoreAspectRatio = true;
                macontext.Video.Options.ignoreLogoDetection = true;
                macontext.Video.Options.ignoreBlackScreenDetection = true;
            }
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment, "audio channel change from %i to %i (%6d)", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_RECORDINGSTART:
            if (asprintf(&comment, "start of recording (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        case MT_RECORDINGSTOP:
            if (asprintf(&comment, "stop of recording (%6d) ",mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            break;
        default:
            dsyslog("cMarkAdStandalone::AddMark(): unknown mark type 0x%X", mark->type);
    }

    // add blackscreen mark in andy case
    if ((mark->type & 0xF0) == MT_BLACKCHANGE) blackMarks.Add(mark->type, MT_UNDEFINED, mark->position, NULL, inBroadCast);

    // check duplicate too near marks with different type
    cMark *prev = marks.GetLast();
    while (prev) { // we do not want blackscreen marks
        if ((prev->type & 0xF0) == MT_BLACKCHANGE) prev = prev->Prev();
        else break;
    }
    if (prev) {
        if (((prev->type & 0x0F) == (mark->type & 0x0F)) && ((prev->type & 0xF0) != (mark->type & 0xF0))) { // do not delete same mark type
            int markDiff = 30000;
            if (iStart != 0) markDiff = 200;  // we are in the start part, let more marks untouched, we need them for start detection, changed from 720 to 680 to 200
                                              // there are some broadcasts who start with a hborder preview but is not hborder
            if (restartLogoDetectionDone) markDiff = 5839; // we are in the end part, keep more marks to detect best end mark, changed from 15000 to 5839
            int diff = 1000 * (abs(mark->position - prev->position)) / macontext.Video.Info.framesPerSecond;
            if (diff < markDiff) {
                char *markType = marks.TypeToText(mark->type);
                char *prevType = marks.TypeToText(prev->type);
                if (markType && prevType) {
                    if (prev->type > mark->type) {
                        if ((mark->type & 0xF0) == MT_BLACKCHANGE) {
                            dsyslog("cMarkAdStandalone::AddMark(): previous %s mark (%d) is stronger than actual %s mark (%d), distance %dms, deleting (%d)", prevType, prev->position, markType, mark->position, diff, mark->position);
                        }
                        else {
                            isyslog("previous %s mark (%i) is stronger than actual %s mark (%d), distance %dms, ignoring (%i)", prevType, prev->position, markType, mark->position, diff, mark->position);
                        }
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(prevType)+1, "text");
                        free(prevType);
                        if (comment) {
                            FREE(strlen(comment)+1, "comment");
                            free(comment);
                        }
                        return;
                    }
                    else {
                        // do not delete logo stop mark on same position as only one aspect ratio mark in end part, prevent false end mark selection
                        if (restartLogoDetectionDone && (mark->type == MT_ASPECTSTOP) && (prev->type == MT_LOGOSTOP) && (mark->position == prev->position) &&
                            marks.Count(MT_ASPECTCHANGE, 0xF0) == 0) {
                            isyslog("only one aspect ratio mark on same position (%d) as logo stop mark in end part, ignore aspect ratio mark, it is from next recording", mark->position);
                            FREE(strlen(markType)+1, "text");
                            free(markType);
                            FREE(strlen(prevType)+1, "text");
                            free(prevType);
                            if (comment) {
                                FREE(strlen(comment)+1, "comment");
                                free(comment);
                            }
                            return;
                        }

                        // delete weaker mark
                        isyslog("actual %s mark (%d) stronger then previous %s mark (%d), distance %dms, deleting (%d)", markType, mark->position, prevType, prev->position, diff, prev->position);
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(prevType)+1, "text");
                        free(prevType);
                        marks.Del(prev);
                    }
                }
                else esyslog("failed to convert mark type in text");
            }
        }
    }

// set inBroadCast status
    if ((mark->type & 0xF0) != MT_BLACKCHANGE){ //  dont use BLACKSCEEN to detect if we are in broadcast
        if (!((mark->type <= MT_ASPECTSTART) && (marks.GetPrev(mark->position, MT_CHANNELSTOP) && marks.GetPrev(mark->position, MT_CHANNELSTART)))) { // if there are MT_CHANNELSTOP and MT_CHANNELSTART marks, wait for MT_CHANNELSTART
            if ((mark->type & 0x0F) == MT_START) {
                inBroadCast = true;
            }
            else {
                inBroadCast = false;
            }
        }
    }

// add mark
    char *indexToHMSF = marks.IndexToHMSF(mark->position);
    if (indexToHMSF) {
        if (comment) {
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) dsyslog("%s at %s inBroadCast: %i",comment, indexToHMSF, inBroadCast);
            else isyslog("%s at %s", comment, indexToHMSF);
        }
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    dsyslog("cMarkAdStandalone::AddMark(): inBroadCast now: %i", inBroadCast);
    marks.Add(mark->type, MT_UNDEFINED, mark->position, comment, inBroadCast);
    if (comment) {
        FREE(strlen(comment)+1, "comment");
        free(comment);
    }

// save marks
    if (iStart == 0) marks.Save(directory, &macontext, false);  // save after start mark is valid
}


// save currect content of the frame buffer to /tmp
// if path and suffix is set, this will set as target path and file name suffix
//
#if defined(DEBUG_OVERLAP_FRAME_RANGE)
void cMarkAdStandalone::SaveFrame(const int frame, const char *path, const char *suffix) {
    if (!macontext.Video.Info.height) {
        dsyslog("cMarkAdStandalone::SaveFrame(): macontext.Video.Info.height not set");
        return;
    }
    if (!macontext.Video.Info.width) {
        dsyslog("cMarkAdStandalone::SaveFrame(): macontext.Video.Info.width not set");
        return;
    }
    if (!macontext.Video.Data.valid) {
        dsyslog("cMarkAdStandalone::SaveFrame():  macontext.Video.Data.valid not set");
        return;
    }
    char szFilename[1024];

    for (int plane = 0; plane < PLANES; plane++) {
        int height;
        int width;
        if (plane == 0) {
            height = macontext.Video.Info.height;
            width  = macontext.Video.Info.width;
        }
        else {
            height = macontext.Video.Info.height / 2;
            width  = macontext.Video.Info.width  / 2;
        }
        // set path and file name
        if (path && suffix) sprintf(szFilename, "%s/frame%06d_P%d_%s.pgm", path, frame, plane, suffix);
        else sprintf(szFilename, "/tmp/frame%06dfull_P%d.pgm", frame, plane);
        // Open file
        FILE *pFile = fopen(szFilename, "wb");
        if (pFile == NULL) {
            dsyslog("cMarkAdStandalone::SaveFrame(): open file %s failed", szFilename);
            return;
        }
        // Write header
        fprintf(pFile, "P5\n%d %d\n255\n", width, height);
        // Write pixel data
        for (int line = 0; line < height; line++) {
            if (fwrite(&macontext.Video.Data.Plane[plane][line * macontext.Video.Data.PlaneLinesize[plane]], 1, width, pFile)) {};
        }
        // Close file
        fclose(pFile);
    }
}
#endif


void cMarkAdStandalone::CheckIndexGrowing()
{
    // Here we check if the index is more
    // advanced than our framecounter.
    // If not we wait. If we wait too much,
    // we discard this check...

#define WAITTIME 15

    if (!indexFile) {
        dsyslog("cMarkAdStandalone::CheckIndexGrowing(): no index file found");
        return;
    }
    if (macontext.Config->logoExtraction != -1) {
        return;
    }
    if (sleepcnt >= 2) {
        dsyslog("slept too much");
        return; // we already slept too much
    }
    if (ptr_cDecoder) framecnt1 = ptr_cDecoder->GetFrameNumber();
    bool notenough = true;
    do {
        struct stat statbuf;
        if (stat(indexFile,&statbuf) == -1) {
            return;
        }

        int maxframes = statbuf.st_size / 8;
        if (maxframes < (framecnt1 + 200)) {
            if ((difftime(time(NULL), statbuf.st_mtime)) >= WAITTIME) {
                if (length && startTime) {
                    time_t endRecording = startTime + (time_t) length;
                    if (time(NULL) > endRecording) {
                        // no markad during recording
//                        dsyslog("cMarkAdStandalone::CheckIndexGrowing(): assuming old recording, now > startTime + length");
                        return;
                    }
                    else {
                        sleepcnt = 0;
                        if (!iwaittime) {
                            dsyslog("cMarkAdStandalone::CheckIndexGrowing(): startTime %s length %d", strtok(ctime(&startTime), "\n"), length);
                            dsyslog("cMarkAdStandalone::CheckIndexGrowing(): expected end: %s", strtok(ctime(&endRecording), "\n"));
                            esyslog("recording interrupted, waiting for continuation...");
                        }
                        iwaittime += WAITTIME;
                    }
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckIndexGrowing(): no length and startTime");
                    return;
                }
            }
            unsigned int sleeptime = WAITTIME;
            time_t sleepstart = time(NULL);
            double slepttime = 0;
            while ((unsigned int)slepttime < sleeptime) {
                while (sleeptime > 0) {
                    macontext.Info.isRunningRecording = true;
                    unsigned int ret = sleep(sleeptime); // now we sleep and hopefully the index will grow
                    if ((errno) && (ret)) {
                        if (abortNow) return;
                        esyslog("got errno %i while waiting for new data", errno);
                        if (errno != EINTR) return;
                    }
                    sleeptime = ret;
                }
                slepttime = difftime(time(NULL), sleepstart);
                if (slepttime < WAITTIME) {
                    esyslog("what's wrong with your system? we just slept %.0fs", slepttime);
                }
            }
            waittime += static_cast<int> (slepttime);
            sleepcnt++;
            if (sleepcnt >= 2) {
                esyslog("no new data after %is, skipping wait!", waittime);
                notenough = false; // something went wrong?
            }
        }
        else {
            if (iwaittime) {
                esyslog("resuming after %is of interrupted recording, marks can be wrong now!", iwaittime);
            }
            iwaittime = 0;
            sleepcnt = 0;
            notenough = false;
        }
    }
    while (notenough);
    return;
}


bool cMarkAdStandalone::ProcessMarkOverlap(cMarkAdOverlap *overlap, cMark **mark1, cMark **mark2) {
    if (!ptr_cDecoder) return false;
    if (!mark1) return false;
    if (!*mark1) return false;
    if (!mark2) return false;
    if (!*mark2) return false;

    sOverlapPos overlapPos;
    overlapPos.similarBeforeStart = -1;
    overlapPos.similarBeforeEnd   = -1;
    overlapPos.similarAfterStart  = -1;
    overlapPos.similarAfterEnd    = -1;

    Reset();

// calculate overlap check positions
#define OVERLAP_CHECK_BEFORE 90  // start before stop mark, max found 58s, changed from 120 to 90
#define OVERLAP_CHECK_AFTER  90  // end after start mark,                  changed from 300 to 90
    int fRangeBegin = (*mark1)->position - (macontext.Video.Info.framesPerSecond * OVERLAP_CHECK_BEFORE);
    if (fRangeBegin < 0) fRangeBegin = 0;                    // not before beginning of broadcast
    fRangeBegin = recordingIndexMark->GetIFrameBefore(fRangeBegin);
    if (fRangeBegin < 0) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    int fRangeEnd = (*mark2)->position + (macontext.Video.Info.framesPerSecond * OVERLAP_CHECK_AFTER);

    cMark *prevStart = marks.GetPrev((*mark1)->position, MT_START, 0x0F);
    if (prevStart) {
        if (fRangeBegin <= (prevStart->position + ((OVERLAP_CHECK_AFTER + 1) * macontext.Video.Info.framesPerSecond))) { // previous start mark less than OVERLAP_CHECK_AFTER away, prevent overlapping check
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): previous stop mark at (%d) very near, unable to check overlap", prevStart->position);
            return false;
        }
    }

    cMark *nextStop = marks.GetNext((*mark2)->position, MT_STOP, 0x0F);
    if (nextStop) {
        if (nextStop->position != marks.GetLast()->position) {
            if (fRangeEnd >= (nextStop->position - ((OVERLAP_CHECK_BEFORE + OVERLAP_CHECK_AFTER + 1) * macontext.Video.Info.framesPerSecond))) { // next start mark less than OVERLAP_CHECK_AFTER + OVERLAP_CHECK_BEFORE away, prevent overlapping check
                fRangeEnd = nextStop->position - ((OVERLAP_CHECK_BEFORE + 1) * macontext.Video.Info.framesPerSecond);
                if (fRangeEnd <= (*mark2)->position) {
                    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): next stop mark at (%d) very near, unable to check overlap", nextStop->position);
                    return false;
                }
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): next stop mark at (%d) to near, reduce check end position", nextStop->position);
            }
        }
        else if (fRangeEnd >= nextStop->position) fRangeEnd = nextStop->position - 2; // do read after last stop mark position because we want to start one frame before end mark with closing credits check
    }

// seek to start frame of overlap check
    char *indexToHMSF = marks.IndexToHMSF(fRangeBegin);
    if (indexToHMSF) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): start check %ds before start mark (%d) from frame (%d) at %s", OVERLAP_CHECK_BEFORE, (*mark1)->position, fRangeBegin, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): preload from frame       (%5d) to (%5d)", fRangeBegin, (*mark1)->position);
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): compare with frames from (%5d) to (%5d)", (*mark2)->position, fRangeEnd);
    if (ptr_cDecoder->GetFrameNumber() > fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): current framenumber (%d) greater then start frame (%d), set start to current frame" , ptr_cDecoder->GetFrameNumber(), fRangeBegin);
        fRangeBegin =  ptr_cDecoder->GetFrameNumber();
    }

    // seek to start frame
    if (!ptr_cDecoder->SeekToFrame(&macontext, fRangeBegin)) {
        esyslog("could not seek to frame (%i)", fRangeBegin);
        return false;
    }

// get frame count of range before stop mark to check for overlap
    int frameCount;
    if (macontext.Config->fullDecode) frameCount = (*mark1)->position - fRangeBegin + 1;
    else frameCount = recordingIndexMark->GetIFrameRangeCount(fRangeBegin, (*mark1)->position);
    if (frameCount < 0) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
        return false;
    }
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): %d frames to preload between start of check (%d) and stop mark (%d)", frameCount, fRangeBegin, (*mark1)->position);

// preload frames before stop mark
    while (ptr_cDecoder->GetFrameNumber() <= (*mark1)->position ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextPacket()) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetNextPacket failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->IsVideoPacket()) continue;

#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif

        if (!ptr_cDecoder->GetFrameInfo(&macontext, macontext.Config->fullDecode) && macontext.Config->fullDecode) dsyslog("cMarkAdStandalone::ProcessMarkOverlap() before stop mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
        if (!macontext.Config->fullDecode && !ptr_cDecoder->IsVideoIFrame()) continue;

#ifdef DEBUG_OVERLAP_FRAME_RANGE
        if ((ptr_cDecoder->GetFrameNumber() > (DEBUG_OVERLAP_FRAME_BEFORE - DEBUG_OVERLAP_FRAME_RANGE)) &&
            (ptr_cDecoder->GetFrameNumber() < (DEBUG_OVERLAP_FRAME_BEFORE + DEBUG_OVERLAP_FRAME_RANGE))) SaveFrame(ptr_cDecoder->GetFrameNumber(), NULL, NULL);
#endif

        overlap->Process(&overlapPos, ptr_cDecoder->GetFrameNumber(), frameCount, true, (macontext.Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264));
    }

// seek to iFrame before start mark
    fRangeBegin = recordingIndexMark->GetIFrameBefore((*mark2)->position);
    if (fRangeBegin <= 0) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    if (fRangeBegin <  ptr_cDecoder->GetFrameNumber()) fRangeBegin = ptr_cDecoder->GetFrameNumber(); // on very short stop/start pairs we have no room to go before start mark
    indexToHMSF = marks.IndexToHMSF(fRangeBegin);
    if (indexToHMSF) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): seek forward to frame (%d) at %s before start mark (%d) and start overlap check", fRangeBegin, indexToHMSF, (*mark2)->position);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    if (!ptr_cDecoder->SeekToFrame(&macontext, fRangeBegin)) {
        esyslog("could not seek to frame (%d)", fRangeBegin);
        return false;
    }

    if (macontext.Config->fullDecode) frameCount = fRangeEnd - fRangeBegin + 1;
    else frameCount = recordingIndexMark->GetIFrameRangeCount(fRangeBegin, fRangeEnd) - 2;
    if (frameCount < 0) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
            return false;
    }
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): %d frames to preload between start mark (%d) and  end of check (%d)", frameCount, (*mark2)->position, fRangeEnd);

// process frames after start mark and detect overlap
    while (ptr_cDecoder->GetFrameNumber() <= fRangeEnd ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextPacket()) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetNextPacket failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->IsVideoPacket()) continue;
#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif

        if (!ptr_cDecoder->GetFrameInfo(&macontext, macontext.Config->fullDecode) && macontext.Config->fullDecode) dsyslog("cMarkAdStandalone::ProcessMarkOverlap() after start mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
        if (!macontext.Config->fullDecode && !ptr_cDecoder->IsVideoIFrame()) continue;

#ifdef DEBUG_OVERLAP_FRAME_RANGE
        if ((ptr_cDecoder->GetFrameNumber() > (DEBUG_OVERLAP_FRAME_AFTER - DEBUG_OVERLAP_FRAME_RANGE)) &&
            (ptr_cDecoder->GetFrameNumber() < (DEBUG_OVERLAP_FRAME_AFTER + DEBUG_OVERLAP_FRAME_RANGE))) SaveFrame(ptr_cDecoder->GetFrameNumber(), NULL, NULL);
#endif

        overlap->Process(&overlapPos, ptr_cDecoder->GetFrameNumber(), frameCount, false, (macontext.Info.vPidType==MARKAD_PIDTYPE_VIDEO_H264));

        if (overlapPos.similarAfterEnd >= 0) {
            // found overlap
            int lengthBefore = 1000 * (overlapPos.similarBeforeEnd - overlapPos.similarBeforeStart + 1) / macontext.Video.Info.framesPerSecond; // include first and last
            int lengthAfter  = 1000 * (overlapPos.similarAfterEnd  - overlapPos.similarAfterStart + 1)  / macontext.Video.Info.framesPerSecond;
            char *indexToHMSFbeforeStart = marks.IndexToHMSF(overlapPos.similarBeforeStart);
            char *indexToHMSFbeforeEnd   = marks.IndexToHMSF(overlapPos.similarBeforeEnd);
            char *indexToHMSFafterStart  = marks.IndexToHMSF(overlapPos.similarAfterStart);
            char *indexToHMSFafterEnd    = marks.IndexToHMSF(overlapPos.similarAfterEnd);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap(): similar from (%5d) at %s to (%5d) at %s, length %5dms", overlapPos.similarBeforeStart, indexToHMSFbeforeStart, overlapPos.similarBeforeEnd, indexToHMSFbeforeEnd, lengthBefore);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              (%5d) at %s to (%5d) at %s, length %5dms",     overlapPos.similarAfterStart,  indexToHMSFafterStart,  overlapPos.similarAfterEnd,  indexToHMSFafterEnd, lengthAfter);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              maximum deviation in overlap %6d", overlapPos.similarMax);
            if (overlapPos.similarEnd > 0) dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              next deviation after overlap %6d", overlapPos.similarEnd); // can be 0 if overlap ends at the mark

            char *indexToHMSFmark1  = marks.IndexToHMSF((*mark1)->position);
            char *indexToHMSFmark2  = marks.IndexToHMSF((*mark2)->position);

            int gapStop         = ((*mark1)->position - overlapPos.similarBeforeEnd)   / macontext.Video.Info.framesPerSecond;
            int lengthBeforeStop = ((*mark1)->position - overlapPos.similarBeforeStart) / macontext.Video.Info.framesPerSecond;
            int gapStart        = (overlapPos.similarAfterStart - (*mark2)->position)  / macontext.Video.Info.framesPerSecond;
            int lengthAfterStart = (overlapPos.similarAfterEnd - (*mark2)->position)    / macontext.Video.Info.framesPerSecond;

            if (indexToHMSFbeforeStart && indexToHMSFbeforeEnd && indexToHMSFafterStart && indexToHMSFafterEnd && indexToHMSFmark1 && indexToHMSFmark2) {
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): overlap from (%6d) at %s to (%6d) at %s, before stop mark gap %3ds length %3ds, are identical with",overlapPos.similarBeforeEnd, indexToHMSFbeforeEnd, (*mark1)->position, indexToHMSFmark1, gapStop, lengthBeforeStop);
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap():              (%6d) at %s to (%6d) at %s, after start mark gap %3ds length %3ds", (*mark2)->position, indexToHMSFmark2, overlapPos.similarAfterEnd, indexToHMSFafterEnd, gapStart, lengthAfterStart);
            }
            if (indexToHMSFbeforeStart) {
                FREE(strlen(indexToHMSFbeforeStart)+1, "indexToHMSF");
                free(indexToHMSFbeforeStart);
            }
            if (indexToHMSFbeforeEnd) {
                FREE(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSF");
                free(indexToHMSFbeforeEnd);
            }
            if (indexToHMSFafterStart) {
                FREE(strlen(indexToHMSFafterStart)+1, "indexToHMSF");
                free(indexToHMSFafterStart);
            }
            if (indexToHMSFafterEnd) {
                FREE(strlen(indexToHMSFafterEnd)+1, "indexToHMSF");
                free(indexToHMSFafterEnd);
            }

            if (indexToHMSFmark1) {
                FREE(strlen(indexToHMSFmark1)+1, "indexToHMSF");
                free(indexToHMSFmark1);
            }
            if (indexToHMSFmark2) {
                FREE(strlen(indexToHMSFmark2)+1, "indexToHMSF");
                free(indexToHMSFmark2);
            }

            // check overlap gap
            int gapStartMax = 16;                                   // changed gapStart from 21 to 18 to 16
            if (gapStop > 0) {                                      // smaller valid diff if we do not hit stop mark, if both are not 0, this can be a invalid overlap
                if (length <= 4920) gapStartMax = 9;                // short overlaps are weak, can be a false positive
                else gapStartMax = 14;
            }
            if ((*mark2)->type == MT_ASPECTSTART)  gapStartMax = 7; // for strong marks we can check with a lower value
            if ((*mark2)->type == MT_VBORDERSTART) gapStartMax = 7; // for strong marks we can check with a lower value
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): maximum valid gap after start mark: %ds", gapStartMax);
            if ((lengthBefore >= 46640) ||                                // very long overlaps should be valid
                ((gapStop < 23) && (gapStart == 0)) ||              // if we hit start mark, trust greater stop gap, maybe we have no correct stop mark, changed from 34 to 23
                ((gapStop < 15) && (gapStart < gapStartMax))) {     // we can not detect all similars during a scene changes, changed from 27 to 15
                                                                    // but if it is too far away it is a false positiv
                                                                    // changed gapStop from 36 to 27
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): overlap gap to marks are valid, before stop mark %ds, after start mark %ds, length %dms", gapStop, gapStart, lengthBefore);
                *mark1 = marks.Move(*mark1, overlapPos.similarBeforeEnd, "overlap");
                *mark2 = marks.Move(*mark2, overlapPos.similarAfterEnd,  "overlap");
                marks.Save(directory, &macontext, false);
            }
            else dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): overlap gap to marks are not valid, before stop mark %ds, after start mark %ds, length %dms", gapStop, gapStart, lengthBefore);
            return true;
        }
    }
    return false;
}


#ifdef DEBUG_MARK_FRAMES
void cMarkAdStandalone::DebugMarkFrames() {
    if (!ptr_cDecoder) return;

    ptr_cDecoder->Reset();
    cMark *mark = marks.GetFirst();
    if (!macontext.Config->fullDecode) {
        while (mark) {
            if (mark->position != recordingIndexMark->GetIFrameBefore(mark->position)) dsyslog("cMarkAdStandalone::DebugMarkFrames(): mark at (%d) type 0x%X is not a iFrame position", mark->position, mark->type);
            mark=mark->Next();
        }
    }

    mark = marks.GetFirst();
    if (!mark) return;

    int writePosition = mark->position;
    for (int i = 0; i < DEBUG_MARK_FRAMES; i++) {
        if (!macontext.Config->fullDecode) writePosition = recordingIndexMark->GetIFrameBefore(writePosition - 1);
        else writePosition--;
    }
    int writeOffset = -DEBUG_MARK_FRAMES;

    // read and decode all video frames, we want to be sure we have a valid decoder state, this is a debug function, we dont care about performance
    while(mark && (ptr_cDecoder->DecodeDir(directory))) {
        while(mark && (ptr_cDecoder->GetNextPacket())) {
            if (ptr_cDecoder->IsVideoPacket()) {
                if (ptr_cDecoder->GetFrameInfo(&macontext, macontext.Config->fullDecode)) {
                    if (ptr_cDecoder->GetFrameNumber() >= writePosition) {
                        dsyslog("cMarkAdStandalone::DebugMarkFrames(): mark at frame (%5d) type 0x%X, write frame (%5d)", mark->position, mark->type, writePosition);
                        char suffix1[10] = "";
                        char suffix2[10] = "";
                        if ((mark->type & 0x0F) == MT_START) strcpy(suffix1, "START");
                        if ((mark->type & 0x0F) == MT_STOP)  strcpy(suffix1, "STOP");
                        if (writePosition < mark->position)  strcpy(suffix2, "BEFORE");
                        if (writePosition > mark->position)  strcpy(suffix2, "AFTER");

                        char *fileName = NULL;
                        if (asprintf(&fileName,"%s/F__%07d_%s_%s.pgm", macontext.Config->recDir, ptr_cDecoder->GetFrameNumber(), suffix1, suffix2) >= 1) {
                            ALLOC(strlen(fileName)+1, "fileName");
                            SaveFrameBuffer(&macontext, fileName);
                            FREE(strlen(fileName)+1, "fileName");
                            free(fileName);
                        }

                        if (!macontext.Config->fullDecode) writePosition = recordingIndexMark->GetIFrameAfter(writePosition + 1);
                        else writePosition++;
                        if (writeOffset >= DEBUG_MARK_FRAMES) {
                            mark = mark->Next();
                            if (!mark) break;
                            writePosition = mark->position;
                            for (int i = 0; i < DEBUG_MARK_FRAMES; i++) {
                                if (!macontext.Config->fullDecode) writePosition = recordingIndexMark->GetIFrameBefore(writePosition - 1);
                                else writePosition--;
                            }
                            writeOffset = -DEBUG_MARK_FRAMES;
                        }
                        else writeOffset++;
                    }
                }
            }
        }
    }
}
#endif


void cMarkAdStandalone::MarkadCut() {
    if (abortNow) return;
    if (!ptr_cDecoder) {
        dsyslog("cMarkAdStandalone::MarkadCut(): ptr_cDecoder not set");
        return;
    }
    LogSeparator(true);
    isyslog("start cut video based on marks");
    if (marks.Count() < 2) {
        isyslog("need at least 2 marks to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): final marks are:");
    DebugMarks();     //  only for debugging

    // init encoder
    cEncoder *ptr_cEncoder = new cEncoder(&macontext);
    ALLOC(sizeof(*ptr_cEncoder), "ptr_cEncoder");

    int passMin = 0;
    int passMax = 0;
    if (macontext.Config->fullEncode) {  // to full endcode we need 2 pass full encoding
        passMin = 1;
        passMax = 2;
    }

    for (int pass = passMin; pass <= passMax; pass ++) {
        dsyslog("cMarkAdStandalone::MarkadCut(): start pass %d", pass);
        ptr_cDecoder->Reset();
        ptr_cDecoder->DecodeDir(directory);
        ptr_cEncoder->Reset(pass);

        // set start and end mark of first part
        cMark *startMark = marks.GetFirst();
        if ((startMark->type & 0x0F) != MT_START) {
            esyslog("got invalid start mark at (%i) type 0x%X", startMark->position, startMark->type);
            return;
        }
        int startPosition;
        if (macontext.Config->fullEncode) startPosition = recordingIndexMark->GetIFrameBefore(startMark->position - 1);  // go before mark position to preload decoder pipe
        else startPosition = recordingIndexMark->GetIFrameAfter(startMark->position);  // go after mark position to prevent last picture of ad
        if (startPosition < 0) startPosition = startMark->position;

        cMark *stopMark = startMark->Next();
        if ((stopMark->type & 0x0F) != MT_STOP) {
            esyslog("got invalid stop mark at (%i) type 0x%X", stopMark->position, stopMark->type);
            return;
        }

        // open output file
        ptr_cDecoder->SeekToFrame(&macontext, startPosition);  // seek to start posiition to get correct input video parameter
        if (!ptr_cEncoder->OpenFile(directory, ptr_cDecoder)) {
            esyslog("failed to open output file");
            FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
            delete ptr_cEncoder;
            ptr_cEncoder = NULL;
            return;
        }

        bool nextFile = true;
        // process input file
        while(nextFile && ptr_cDecoder->DecodeDir(directory)) {
            while(ptr_cDecoder->GetNextPacket()) {
                int frameNumber = ptr_cDecoder->GetFrameNumber();
                if  (frameNumber < startPosition) {  // go to start frame
                    LogSeparator();
                    dsyslog("cMarkAdStandalone::MarkadCut(): decoding from frame (%d) for start mark (%d) to frame (%d) in pass %d", startPosition, startMark->position, stopMark->position, pass);
                    ptr_cDecoder->SeekToFrame(&macontext, startPosition);
                    frameNumber = ptr_cDecoder->GetFrameNumber();
                }
                if  (frameNumber > stopMark->position) {  // stop mark reached
                    if (stopMark->Next() && stopMark->Next()->Next()) {  // next mark pair
                        startMark = stopMark->Next();
                        if ((startMark->type & 0x0F) != MT_START) {
                            esyslog("got invalid start mark at (%i) type 0x%X", startMark->position, startMark->type);
                            return;
                        }

                        if (macontext.Config->fullEncode) startPosition = recordingIndexMark->GetIFrameBefore(startMark->position - 1);  // go before mark position to preload decoder pipe
                        else startPosition = recordingIndexMark->GetIFrameAfter(startMark->position);  // go after mark position to prevent last picture of ad
                        if (startPosition < 0) startPosition = startMark->position;

                        stopMark = startMark->Next();
                        if ((stopMark->type & 0x0F) != MT_STOP) {
                            esyslog("got invalid stop mark at (%i) type 0x%X", stopMark->position, stopMark->type);
                            return;
                        }
                    }
                    else {
                        nextFile = false;
                        break;
                    }
                    continue;
                }
                // read packet
                AVPacket *pkt = ptr_cDecoder->GetPacket();
                if (!pkt) {
                    esyslog("failed to get packet from input stream");
                    return;
                }
                // preload decoder pipe
                if ((macontext.Config->fullEncode) && (frameNumber < startMark->position)) {
                    ptr_cDecoder->DecodePacket(pkt);
                    continue;
                }
                // decode/encode/write packet
                if (!ptr_cEncoder->WritePacket(pkt, ptr_cDecoder)) {
                    dsyslog("cMarkAdStandalone::MarkadCut(): failed to write frame %d to output stream", frameNumber);  // no not abort, maybe next frame works
                }
                if (abortNow) {
                    ptr_cEncoder->CloseFile(ptr_cDecoder);  // ptr_cEncoder must be valid here because it is used above
                    if (ptr_cDecoder) {
                        FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                        delete ptr_cDecoder;
                        ptr_cDecoder = NULL;
                    }
                    FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
                    delete ptr_cEncoder;
                    ptr_cEncoder = NULL;
                    return;
                }
            }
        }
        if (!ptr_cEncoder->CloseFile(ptr_cDecoder)) {
            dsyslog("failed to close output file");
            return;
        }
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): end at frame %d", ptr_cDecoder->GetFrameNumber());
    FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
    delete ptr_cEncoder;  // ptr_cEncoder must be valid here because it is used above
    ptr_cEncoder = NULL;
    framecnt4 = ptr_cDecoder->GetFrameNumber();
}


// logo mark optimization
// move logo marks:
//     - if closing credits are detected after last logo stop mark
//     - if silence was detected before start mark or after/before end mark
//     - if black screen marks are direct before stop mark or direct after start mark
//
void cMarkAdStandalone::LogoMarkOptimization() {
    if (!ptr_cDecoder) return;

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): start logo mark optimization");
    bool save = false;

// check for advertising in frame with logo after logo start mark and before logo stop mark and check for introduction logo
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): check for advertising in frame with logo after logo start and before logo stop mark and check for introduction logo");

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, ptr_cDecoder, recordingIndexMark, NULL);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    cMark *markLogo = marks.GetFirst();
    while (markLogo) {
        if (markLogo->type == MT_LOGOSTART) {
            char *indexToHMSFStartMark = marks.IndexToHMSF(markLogo->position);

            // check for introduction logo before logo mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (30 * macontext.Video.Info.framesPerSecond); // introduction logos are usually 10s, somettimes longer, changed from 12 to 30
            if (searchStartPosition < 0) searchStartPosition = 0;
            char *indexToHMSFSearchStart = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFStartMark && indexToHMSFSearchStart) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search introduction logo from position (%d) at %s to logo start mark (%d) at %s", searchStartPosition, indexToHMSFSearchStart, markLogo->position, indexToHMSFStartMark);
            if (indexToHMSFSearchStart) {
                FREE(strlen(indexToHMSFSearchStart)+1, "indexToHMSF");
                free(indexToHMSFSearchStart);
            }
            int introductionStartPosition = -1;
            if (ptr_cDetectLogoStopStart->Detect(searchStartPosition, markLogo->position)) {
                introductionStartPosition = ptr_cDetectLogoStopStart->IntroductionLogo();
            }

            // check for advertising in frame with logo after logo start mark position
            int adInFrameEndPosition = -1;
            if (markLogo->position > marks.GetFirst()->position) { // never saw a advertising in frame after first start mark
                LogSeparator(false);
                int searchEndPosition = markLogo->position + (35 * macontext.Video.Info.framesPerSecond); // advertising in frame are usually 30s
                char *indexToHMSFSearchEnd = marks.IndexToHMSF(searchEndPosition);
                if (indexToHMSFStartMark && indexToHMSFSearchEnd) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo after logo start mark (%d) at %s to position (%d) at %s", markLogo->position, indexToHMSFStartMark, searchEndPosition, indexToHMSFSearchEnd);
                if (indexToHMSFSearchEnd) {
                    FREE(strlen(indexToHMSFSearchEnd)+1, "indexToHMSF");
                    free(indexToHMSFSearchEnd);
                }
                if (ptr_cDetectLogoStopStart->Detect(markLogo->position, searchEndPosition)) {
                    adInFrameEndPosition = ptr_cDetectLogoStopStart->AdInFrameWithLogo(true);
                }
                if (adInFrameEndPosition >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) found", markLogo->position, adInFrameEndPosition);
                    if (evaluateLogoStopStartPair && (evaluateLogoStopStartPair->IncludesInfoLogo(markLogo->position, adInFrameEndPosition))) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        adInFrameEndPosition = -1;
                    }
                }
            }
            if (adInFrameEndPosition != -1) {  // if we found advertising in frame, use this
                adInFrameEndPosition = recordingIndexMark->GetIFrameAfter(adInFrameEndPosition + 1);  // we got last frame of ad, go to next iFrame for start mark
                markLogo = marks.Move(markLogo, adInFrameEndPosition, "advertising in frame");
                save = true;
            }
            else {
                if (introductionStartPosition != -1) {
                    bool move = true;
                    // check blackscreen between introduction logo start and logo start, there should be no long blackscreen, short blackscreen are from retrospect
                    cMark *blackMarkStart = blackMarks.GetNext(introductionStartPosition, MT_NOBLACKSTART);
                    cMark *blackMarkStop = blackMarks.GetNext(introductionStartPosition, MT_NOBLACKSTOP);
                    if (blackMarkStart && blackMarkStop && (blackMarkStart->position <= markLogo->position) && (blackMarkStop->position <= markLogo->position)) {
                        int innerLength = 1000 * (blackMarkStart->position - blackMarkStop->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen start (%d) and stop (%d) between introduction logo (%d) and start mark (%d), length %dms", blackMarkStop->position, blackMarkStart->position, introductionStartPosition, markLogo->position, innerLength);
                        if (innerLength > 1000) move = false;  // only move if we found no long blackscreen between introduction logo and logo start
                    }
                    if (move) {
                        // check blackscreen before introduction logo
                        blackMarkStart = blackMarks.GetPrev(introductionStartPosition, MT_NOBLACKSTART);
                        blackMarkStop = blackMarks.GetPrev(introductionStartPosition, MT_NOBLACKSTOP);
                        if (blackMarkStart && blackMarkStop) {
                            int beforeLength = 1000 * (blackMarkStart->position - blackMarkStop->position)  / macontext.Video.Info.framesPerSecond;
                            int diff = 1000 * (introductionStartPosition - blackMarkStart->position) / macontext.Video.Info.framesPerSecond;
                            dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen start (%d) and stop (%d) before introduction logo (%d), distance %dms, length %dms", blackMarkStop->position, blackMarkStart->position, introductionStartPosition, diff, beforeLength);
                            if (diff <= 3520) { // blackscreen beforeshould be near
                                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found valid black screen at (%d), %dms before introduction logo", blackMarkStart->position, diff);
                                if (macontext.Config->fullDecode && (markLogo->position == marks.First()->position)) {
                                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): this is the start mark of the broadcast, keep last black screen at (%d)", blackMarkStart->position - 1);
                                    markLogo = marks.Move(markLogo, blackMarkStart->position - 1, "black screen before introduction logo");
                                }
                                else markLogo = marks.Move(markLogo, blackMarkStart->position, "black screen before introduction logo");
                                move = false;  // move is done based on blackscreen position
                            }
                        }
                    }
                    if (move) markLogo = marks.Move(markLogo, introductionStartPosition, "introduction logo");
                    save = true;
                }
            }
            if (indexToHMSFStartMark) {
                FREE(strlen(indexToHMSFStartMark)+1, "indexToHMSF");
                free(indexToHMSFStartMark);
            }
        }
        if ((markLogo->type == MT_LOGOSTOP) && (marks.GetNext(markLogo->position, MT_STOP, 0x0F))) { // do not test logo end mark, ad in frame with logo and closing credits without logo looks the same
            // check for advertising in frame with logo before logo stop mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (45 * macontext.Video.Info.framesPerSecond); // advertising in frame are usually 30s, changed from 35 to 45
                                                                                                        // somtimes there is a closing credit in frame with logo before
            char *indexToHMSFStopMark = marks.IndexToHMSF(markLogo->position);
            char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo from frame (%d) at %s to logo stop mark (%d) at %s", searchStartPosition, indexToHMSFSearchPosition, markLogo->position, indexToHMSFStopMark);
            if (indexToHMSFStopMark) {
                FREE(strlen(indexToHMSFStopMark)+1, "indexToHMSF");
                free(indexToHMSFStopMark);
            }
            if (indexToHMSFSearchPosition) {
                FREE(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
                free(indexToHMSFSearchPosition);
            }
            // short start/stop pair can result in overlapping checks
            if (ptr_cDecoder->GetFrameNumber() > searchStartPosition) {
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): current framenumber (%d) greater than framenumber to seek (%d), restart decoder", ptr_cDecoder->GetFrameNumber(), searchStartPosition);
                ptr_cDecoder->Reset();
                ptr_cDecoder->DecodeDir(directory);
            }
            // detect frames
            if (ptr_cDetectLogoStopStart->Detect(searchStartPosition, markLogo->position)) {
                int newStopPosition = ptr_cDetectLogoStopStart->AdInFrameWithLogo(false);
                if (newStopPosition != -1) {
                    newStopPosition = recordingIndexMark->GetIFrameBefore(newStopPosition - 1);  // we got first frame of ad, go one iFrame back for stop mark
                    markLogo = marks.Move(markLogo, newStopPosition, "advertising in frame");
                    save = true;
               }
            }
        }
        markLogo = markLogo->Next();
    }
    FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    delete evaluateLogoStopStartPair;
    FREE(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");
    delete ptr_cDetectLogoStopStart;

// search for audio silence near logo marks
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search for audio silence around logo marks");
    int silenceRange = 3500;           // in ms, do not increase, otherwise we got stop/start marks behind separation images (e.g. NITRO)
    if (macontext.Info.ChannelName) {  // macontext.Info.ChannelName == NULL can happen if the VDR info file is missing
        if (strcmp(macontext.Info.ChannelName, "NITRO")       == 0) silenceRange = 2700; // short separation scene with no audio before
        if (strcmp(macontext.Info.ChannelName, "TELE_5")      == 0) silenceRange = 4000; // logo fade in/out, do not increase to prevent to get ad before start
        if (strcmp(macontext.Info.ChannelName, "SIXX")        == 0) silenceRange = 5000; // short preview with logo direct after broadcast, get real stop with black screen between
        if (strcmp(macontext.Info.ChannelName, "Nickelodeon") == 0) silenceRange = 7000; // logo fade in/out
        if (strcmp(macontext.Info.ChannelName, "DMAX")        == 0) silenceRange = 8000; // logo color change at the begin
                                                                                         // changed from 12000 to 8000 to prevent to get a silence part away from stop mark
    }

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    char *indexToHMSF = NULL;
    cMark *mark = marks.GetFirst();
    while (mark) {
        if (indexToHMSF) {
           FREE(strlen(indexToHMSF)+1, "indexToHMSF");
           free(indexToHMSF);
        }
        indexToHMSF = marks.IndexToHMSF(mark->position);

        if (mark->type == MT_LOGOSTART) {
            LogSeparator(false);
            if (indexToHMSF) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): detect audio silence before logo start mark at frame (%6d) type 0x%X at %s max range %dms", mark->position, mark->type, indexToHMSF, silenceRange);
            int seekPos =  mark->position - (silenceRange * macontext.Video.Info.framesPerSecond / 1000);
            if (seekPos < 0) seekPos = 0;
            if (!ptr_cDecoder->SeekToFrame(&macontext, seekPos)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            framecnt3 += silenceRange * macontext.Video.Info.framesPerSecond / 1000;
            int beforeSilence = ptr_cDecoder->GetNextSilence(&macontext, mark->position, true, true);
            if ((beforeSilence >= 0) && (beforeSilence != mark->position)) {
                int diff = 1000 * (mark->position - beforeSilence) /  macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found audio silence at frame (%d) %dms before logo start mark (%d)", beforeSilence, diff, mark->position);
                // search for blackscreen near silence to optimize mark positon
                cMark *blackMark = blackMarks.GetAround(3 * macontext.Video.Info.framesPerSecond, beforeSilence, MT_NOBLACKSTART);  // changed from 1 to 3
                if (blackMark) {
                    int diffBlack = 1000 * (beforeSilence - blackMark->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen (%d) %dms %s silence position (%d)", blackMark->position, abs(diffBlack), (diffBlack > 0) ? "before" : "after", beforeSilence);
                    if (macontext.Config->fullDecode && (mark->position == marks.First()->position)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): this is the start mark of the broadcast, keep last black screen at (%d)", blackMark->position - 1);
                        mark = marks.Move(mark, blackMark->position - 1, "black screen near silence");
                    }
                    else mark = marks.Move(mark, blackMark->position, "black screen near silence");
                }
                else mark = marks.Move(mark, beforeSilence, "silence");
                save = true;
                continue;
            }
            if (indexToHMSF) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no audio silence before logo mark at frame (%6i) type 0x%X at %s found", mark->position, mark->type, indexToHMSF);

        }
        if (mark->type == MT_LOGOSTOP) {
            // search before stop mark
            LogSeparator(false);
            if (indexToHMSF) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): detect audio silence before logo stop mark at frame (%6d) type 0x%X at %s range %dms", mark->position, mark->type, indexToHMSF, silenceRange);
            int seekPos =  mark->position - (silenceRange * macontext.Video.Info.framesPerSecond / 1000);
            if (seekPos < ptr_cDecoder->GetFrameNumber()) seekPos = ptr_cDecoder->GetFrameNumber();  // will retun -1 before first frame read
            if (seekPos < 0) seekPos = 0;
            if (!ptr_cDecoder->SeekToFrame(&macontext, seekPos)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            int beforeSilence = ptr_cDecoder->GetNextSilence(&macontext, mark->position, true, false);
            if (beforeSilence >= 0) {
                int diff = 1000 * (mark->position - beforeSilence) /  macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found audio silence at frame (%d) %dms before logo stop mark (%d)", beforeSilence, diff, mark->position);
            }
            // search after stop mark
            if (indexToHMSF) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): detect audio silence after logo stop mark at frame (%6i) type 0x%X at %s range %ims", mark->position, mark->type, indexToHMSF, silenceRange);
            if (!ptr_cDecoder->SeekToFrame(&macontext, mark->position)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            int stopFrame =  mark->position + (silenceRange * macontext.Video.Info.framesPerSecond / 1000);
            int afterSilence = ptr_cDecoder->GetNextSilence(&macontext, stopFrame, false, false);
            if (afterSilence >= 0) {
                int diff = 1000 * (afterSilence - mark->position) /  macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found audio silence at frame (%d) %dms after logo stop mark (%d)", afterSilence, diff, mark->position);
            }
            framecnt3 += 2 * silenceRange - 1 * macontext.Video.Info.framesPerSecond / 1000;
            bool before = false;

            // use before silence only if we found no after silence
            if (afterSilence < 0) {
                afterSilence = beforeSilence;
                before = true;
            }

            if ((afterSilence >= 0) && (afterSilence != mark->position)) {
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): use audio silence %s logo stop at frame (%d)", (before) ? "before" : "after", afterSilence);
                // search for blackscreen near silence to optimize mark positon
                cMark *blackMark = blackMarks.GetAround(1 * macontext.Video.Info.framesPerSecond, afterSilence, MT_NOBLACKSTART);
                if (blackMark) {
                    int diff = 1000 * (afterSilence - blackMark->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen (%d) %dms %s silence position (%d)", blackMark->position, abs(diff), (diff > 0) ? "before" : "after", afterSilence);
                    mark = marks.Move(mark, blackMark->position - 1, "black screen near silence"); // MT_NOBLACKSTART is first frame after black screen
                }
                else mark = marks.Move(mark, afterSilence, "silence");
                save = true;
                continue;
            }
        }
        mark=mark->Next();
    }
    if (indexToHMSF) {  // cleanup after loop
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }

// try blacksceen mark
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): start search for black screen near logo marks");
    int blackscreenRange = 4270;
    // logo fade in/out
    if (macontext.Info.ChannelName) {  // macontext.Info.ChannelName == NULL can happen if the VDR info file is missing
        if ((strcmp(macontext.Info.ChannelName, "TELE_5")                == 0) ||  // these channels have fading in/out logo, so we need more range
            (strcmp(macontext.Info.ChannelName, "Disney_Channel")        == 0) ||
            (strcmp(macontext.Info.ChannelName, "Nick_Comedy_Central+1") == 0) ||
            (strcmp(macontext.Info.ChannelName, "Nickelodeon")           == 0)) blackscreenRange = 5500;
    }
    mark = marks.GetFirst();
    while (mark) {
        // logo start mark, use blackscreen before and after mark
        if (mark->type == MT_LOGOSTART) {
            for (int i = 0; i <= 1; i++) {
                cMark *blackMark = NULL;
                if (i == 0) blackMark = blackMarks.GetAround(blackscreenRange * macontext.Video.Info.framesPerSecond / 1000, mark->position, MT_NOBLACKSTART); // first try near   logo start mark
                else blackMark = blackMarks.GetPrev(mark->position, MT_NOBLACKSTART);                                                                          //  next try before logo start mark

                if (blackMark) {
                    int distance_ms = 1000 * (mark->position - blackMark->position) / macontext.Video.Info.framesPerSecond;
                    if ((distance_ms > 0) && (distance_ms <= blackscreenRange)) { // blackscreen is before logo start mark
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): black screen (%d) distance %dms (expect >0 and <=%dms) before logo start mark (%d), move mark", blackMark->position, distance_ms, blackscreenRange, mark->position);
                        if (macontext.Config->fullDecode && (mark->position == marks.First()->position)) {
                            dsyslog("cMarkAdStandalone::LogoMarkOptimization(): this is the start mark of the broadcast, keep last black screen at (%d)", blackMark->position - 1);
                            mark = marks.Move(mark, blackMark->position - 1, "black screen");
                        }
                        else mark = marks.Move(mark, blackMark->position, "black screen");
                        save = true;
                        break;
                    }
                    else {
                        if (i == 0) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): black screen near   logo start mark (%d) distance %dms (expect >0 and <=%ds) before (-after) logo start mark (%d), keep mark", blackMark->position, distance_ms, blackscreenRange, mark->position);
                        else        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): black screen before logo start mark (%d) distance %dms (expect >0 and <=%ds) before (-after) logo start mark (%d), keep mark", blackMark->position, distance_ms, blackscreenRange, mark->position);
                    }
                }
                else {
                    if (i == 0) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no black screen mark found near   logo start mark (%d)", mark->position);
                    else        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no black screen mark found before logo start mark (%d)", mark->position);
                }
            }
        }
        // logo stop mark or blackscreen start (=stop mark, this can only be a end mark, move mark to end of black screen range)
        // use black screen mark only after mark
        if ((mark->type == MT_LOGOSTOP) || (mark->type == MT_NOBLACKSTOP)) {
            // try blackscreen after stop mark
            bool foundAfter = false;
            cMark *blackMark = blackMarks.GetNext(mark->position, MT_NOBLACKSTART);
            if (blackMark) {
                int diff_ms = 1000 * (blackMark->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): black screen (%d) %dms (expect <=%ds) after logo stop mark (%d) found", blackMark->position, diff_ms, blackscreenRange, mark->position);
                if (diff_ms <= blackscreenRange) {
                    foundAfter = true;
                    int newPos;
                    if (!macontext.Config->fullDecode) {
                        newPos =  recordingIndexMark->GetIFrameBefore(blackMark->position); // MT_NOBLACKSSTART with "only iFrame decoding" is the first frame afer blackscreen, get last frame of blackscreen, blacksceen at stop mark belongs to broasdact
                    }
                    else newPos = blackMark->position - 1; // MT_NOBLACKSTART is first frame after blackscreen, go one back

                    if (newPos == mark->position) { // found blackscreen at same position
                        mark = mark->Next();
                        continue;
                    }
                    mark = marks.Move(mark, newPos, "black screen");
                    save = true;
                    continue;
                }
            }
            else dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no black screen mark found after logo stop mark (%d)", mark->position);

            // try blackscreen before stop mark, only with half distance, better save than sorry
            if (!foundAfter) {
                cMark *blackMarkAfter = blackMarks.GetPrev(mark->position, MT_NOBLACKSTART);
                if (blackMarkAfter) {
                    int diff_ms = 1000 * (mark->position - blackMarkAfter->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): black screen (%d) %dms (expect <=%ds) before logo stop mark (%d) found", blackMarkAfter->position, diff_ms, blackscreenRange / 2, mark->position);
                    if (diff_ms <= (blackscreenRange / 2)) {
                        int newPos;
                        if (!macontext.Config->fullDecode) {
                            newPos =  recordingIndexMark->GetIFrameBefore(blackMarkAfter->position); // MT_NOBLACKSSTART with "only iFrame decoding" is the first frame afer blackscreen, get last frame of blackscreen, blacksceen at stop mark belongs to broasdact
                        }
                        else newPos = blackMarkAfter->position - 1; // MT_NOBLACKSTART is first frame after blackscreen, go one back

                        if (newPos == mark->position) { // found blackscreen at same position
                            mark = mark->Next();
                            continue;
                        }
                        mark = marks.Move(mark, newPos, "black screen");
                        save = true;
                        continue;
                    }
                }
                else dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no black screen mark found before logo stop mark (%d)", mark->position);
            }
        }
        mark = mark->Next();
    }

    if (save) marks.Save(directory, &macontext, false);
    return;
}


void cMarkAdStandalone::ProcessOverlap() {
    if (abortNow) return;
    if (duplicate) return;
    if (!ptr_cDecoder) return;
    if (!length) return;
    if (!startTime) return;
    if (time(NULL) < (startTime+(time_t) length)) return;

    LogSeparator(true);
    dsyslog("ProcessOverlap(): start overlap detection");
    DebugMarks();     //  only for debugging

    if (!macontext.Video.Info.framesPerSecond) {
        isyslog("WARNING: assuming fps of 25");
        macontext.Video.Info.framesPerSecond = 25;
    }

    bool save = false;
    cMark *p1 = NULL,*p2 = NULL;

    if (ptr_cDecoder) {
        ptr_cDecoder->Reset();
        ptr_cDecoder->DecodeDir(directory);
    }

    if (marks.Count() >= 4) {
        p1 = marks.GetFirst();
        if (!p1) return;

        p1 = p1->Next();
        if (p1) p2 = p1->Next();

        while ((p1) && (p2)) {
            if (ptr_cDecoder) {
                LogSeparator(false);
                dsyslog("cMarkAdStandalone::ProcessOverlap(): check overlap before stop mark (%d) and after start mark (%d)", p1->position, p2->position);
                // init overlap detection object
                cMarkAdOverlap *overlap = new cMarkAdOverlap(&macontext);
                ALLOC(sizeof(*overlap), "overlap");
                // detect overlap before stop and after start
                if (!ProcessMarkOverlap(overlap, &p1, &p2)) {
                    dsyslog("cMarkAdStandalone::ProcessOverlap(): no overlap found before stop mark (%d) and after start (%d)", p1->position, p2->position);
                }
                else save = true;
                // free overlap detection object
                FREE(sizeof(*overlap), "overlap");
                delete overlap;
            }
            p1 = p2->Next();
            if (p1) {
                p2 = p1->Next();
            }
            else {
                p2 = NULL;
            }
        }
    }

    // check last logo stop mark if closing credits follows
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): check last logo stop mark if closing credits follows");
    if (ptr_cDecoder) {  // we use file position
        cMark *lastStop = marks.GetLast();
        if (lastStop) {
            if ((lastStop->type == MT_NOBLACKSTOP) || (lastStop->oldType == MT_NOBLACKSTOP)) {
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): end mark is a weak blackscreen mark, no closing credits without logo can follow");
            }
            else {
                if ((lastStop->type == MT_LOGOSTOP) || (lastStop->type == MT_HBORDERSTOP) || (lastStop->type == MT_MOVEDSTOP)) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search for closing credits");
                    if (MoveLastStopAfterClosingCredits(lastStop)) {
                        save = true;
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): moved last logo stop mark after closing credit");
                    }
                }
            }
        }
    }

    framecntOverlap = ptr_cDecoder->GetFrameNumber();
    if (save) marks.Save(directory, &macontext, false);
    dsyslog("end Overlap");
    return;
}


void cMarkAdStandalone::Reset() {
    iFrameBefore = -1;
    iFrameCurrent = -1;
    frameCurrent = -1;
    gotendmark = false;
    chkSTART = chkSTOP = INT_MAX;
    macontext.Video.Info.AspectRatio.den = 0;
    macontext.Video.Info.AspectRatio.num = 0;
    memset(macontext.Audio.Info.Channels, 0, sizeof(macontext.Audio.Info.Channels));

    if (video) video->Clear(false);
    if (audio) audio->Clear();
    return;
}


bool cMarkAdStandalone::ProcessFrame(cDecoder *ptr_cDecoder) {
    if (!ptr_cDecoder) return false;
    if (!video) {
        esyslog("cMarkAdStandalone::ProcessFrame() video not initialized");
        return false;
    }

    if ((macontext.Config->logoExtraction != -1) && (ptr_cDecoder->GetIFrameCount() >= 512)) {    // extract logo
        isyslog("finished logo extraction, please check /tmp for pgm files");
        abortNow=true;
    }
    frameCurrent = ptr_cDecoder->GetFrameNumber();
    if (ptr_cDecoder->IsVideoIFrame()) {
        iFrameBefore = iFrameCurrent;
        iFrameCurrent = frameCurrent;
        checkAudio = true;
    }
    if (ptr_cDecoder->GetFrameInfo(&macontext, macontext.Config->fullDecode)) {
        if (ptr_cDecoder->IsVideoPacket()) {
            if ((ptr_cDecoder->GetFileNumber() == 1) &&  // found some Finnish H.264 interlaced recordings who changed real bite rate in second TS file header
                                                         // frame rate can not change, ignore this and keep frame rate from first TS file
                 ptr_cDecoder->IsInterlacedVideo() && !macontext.Video.Info.interlaced && (macontext.Info.vPidType==MARKAD_PIDTYPE_VIDEO_H264) &&
                (ptr_cDecoder->GetVideoAvgFrameRate() == 25) && (ptr_cDecoder->GetVideoRealFrameRate() == 50)) {
                dsyslog("cMarkAdStandalone::ProcessFrame(): change internal frame rate to handle H.264 interlaced video");
                macontext.Video.Info.framesPerSecond *= 2;
                macontext.Video.Info.interlaced = true;
                CalculateCheckPositions(macontext.Info.tStart * macontext.Video.Info.framesPerSecond);
            }
            if ((iStart < 0) && (frameCurrent > -iStart)) iStart = frameCurrent;
            if ((iStop < 0) && (frameCurrent > -iStop)) {
                iStop = frameCurrent;
                iStopinBroadCast = inBroadCast;
            }
            if ((iStopA < 0) && (frameCurrent > -iStopA)) {
                iStopA = frameCurrent;
            }

            if (!macontext.Video.Data.valid) {
                isyslog("failed to get video data of frame (%d)", ptr_cDecoder->GetFrameNumber());
                return false;
            }

            // turn on logo and blackscreen detection for end part even if we use stronger marks, just in case we will get no strong end mark
            if (!restartLogoDetectionDone && (frameCurrent > (iStopA - (macontext.Video.Info.framesPerSecond * 2 * MAXRANGE))) && (iStart == 0)) { // not before start part done
                dsyslog("cMarkAdStandalone::ProcessFrame(): enter end part at frame (%d)", frameCurrent);
                restartLogoDetectionDone = true;
                if ((macontext.Video.Options.ignoreBlackScreenDetection) || (macontext.Video.Options.ignoreLogoDetection)) {
                    isyslog("restart logo, border and black screen detection at frame (%d)", ptr_cDecoder->GetFrameNumber());
                    bDecodeVideo = true;
                    macontext.Video.Options.ignoreBlackScreenDetection = false;   // use this to find end mark
                    macontext.Video.Options.ignoreVborder              = false;
                    macontext.Video.Options.ignoreHborder              = false;
                    if (macontext.Video.Options.ignoreLogoDetection == true) {
                        macontext.Video.Options.ignoreLogoDetection = false;
                        if (video) {
                            dsyslog("cMarkAdStandalone::ProcessFrame():  reset logo detector status");
                            video->Clear(true, inBroadCast);
                        }
                    }
                }
            }

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
            if ((frameCurrent > (DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) && (frameCurrent < (DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE))) {
                char *fileName = NULL;
                if (asprintf(&fileName,"%s/F__%07d.pgm", macontext.Config->recDir, frameCurrent) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    SaveFrameBuffer(&macontext, fileName);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                }
            }
#endif

            if (!bDecodeVideo) macontext.Video.Data.valid = false; // make video picture invalid, we do not need them
            sMarkAdMarks *vmarks = video->Process(iFrameBefore, iFrameCurrent, frameCurrent);
            if (vmarks) {
                for (int i = 0; i < vmarks->Count; i++) {
                    AddMark(&vmarks->Number[i]);
                }
            }

            if (iStart > 0) {
                if ((inBroadCast) && (frameCurrent > chkSTART)) CheckStart();
            }
            if ((iStop > 0) && (iStopA > 0)) {
                if (frameCurrent > chkSTOP) {
                    if (iStart != 0) {
                        dsyslog("still no chkStart called, doing it now");
                        CheckStart();
                    }
                    endMarkPos = CheckStop();
                    return false;
                }
            }
        }
        // check audio channel changes
        if (checkAudio && ptr_cDecoder->IsAudioPacket()) {  // check only after i-frame to be able to get next i-frame
            checkAudio = false;
            sMarkAdMark *amark = audio->Process();  // class audio will take frame number of channel change from macontext->Audio.Info.frameChannelChange
            if (amark) AddMark(amark);
        }
    }
    return true;
}


void cMarkAdStandalone::ProcessFiles() {
    if (abortNow) return;

    if (macontext.Config->backupMarks) marks.Backup(directory);

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::ProcessFiles(): start processing files");
    ptr_cDecoder = new cDecoder(macontext.Config->threads, recordingIndexMark);
    ALLOC(sizeof(*ptr_cDecoder), "ptr_cDecoder");
    CheckIndexGrowing();
    while(ptr_cDecoder && ptr_cDecoder->DecodeDir(directory)) {
        if (abortNow) {
            if (ptr_cDecoder) {
                FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                delete ptr_cDecoder;
                ptr_cDecoder = NULL;
            }
            break;
        }
        if(ptr_cDecoder->GetFrameNumber() < 0) {
            macontext.Info.vPidType = ptr_cDecoder->GetVideoType();
            if (macontext.Info.vPidType == 0) {
                dsyslog("cMarkAdStandalone::ProcessFiles(): video type not set");
                return;
            }
            macontext.Video.Info.height = ptr_cDecoder->GetVideoHeight();
            isyslog("video hight: %4d", macontext.Video.Info.height);

            macontext.Video.Info.width = ptr_cDecoder->GetVideoWidth();
            isyslog("video width: %4d", macontext.Video.Info.width);

            macontext.Video.Info.framesPerSecond = ptr_cDecoder->GetVideoAvgFrameRate();
            isyslog("average frame rate: %d frames per second", static_cast<int> (macontext.Video.Info.framesPerSecond));
            if (macontext.Video.Info.framesPerSecond < 0) {
                esyslog("average frame rate of %d frames per second is invalid, recording is damaged", static_cast<int> (macontext.Video.Info.framesPerSecond));
                abortNow = true;
            }
            isyslog("real frame rate:    %i frames per second", ptr_cDecoder->GetVideoRealFrameRate());

            CalculateCheckPositions(macontext.Info.tStart * macontext.Video.Info.framesPerSecond);
        }
        while(ptr_cDecoder && ptr_cDecoder->GetNextPacket()) {
            if (abortNow) {
                if (ptr_cDecoder) {
                    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                    delete ptr_cDecoder;
                    ptr_cDecoder = NULL;
                }
                break;
            }
            // write an early start mark for running recordings
            if (macontext.Info.isRunningRecording && !macontext.Info.isStartMarkSaved && (ptr_cDecoder->GetFrameNumber() >= (macontext.Info.tStart * macontext.Video.Info.framesPerSecond))) {
                dsyslog("cMarkAdStandalone::ProcessFiles(): recording is aktive, read frame (%d), now save dummy start mark at pre timer position %ds", ptr_cDecoder->GetFrameNumber(), macontext.Info.tStart);
                cMarks marksTMP;
                marksTMP.RegisterIndex(recordingIndexMark);
                marksTMP.Add(MT_ASSUMEDSTART, MT_UNDEFINED, ptr_cDecoder->GetFrameNumber(), "timer start", true);
                marksTMP.Save(macontext.Config->recDir, &macontext, true);
                macontext.Info.isStartMarkSaved = true;
            }

            if (!cMarkAdStandalone::ProcessFrame(ptr_cDecoder)) break;
            CheckIndexGrowing();
        }
    }

    if (!abortNow) {
        if (iStart !=0 ) {  // iStart will be 0 if iStart was called
            dsyslog("cMarkAdStandalone::ProcessFiles(): recording ends unexpected before chkSTART (%d) at frame %d", chkSTART, frameCurrent);
            isyslog("got end of recording before recording length from info file reached");
            CheckStart();
        }
        if (iStopA > 0) {
            if (iStop <= 0) {  // unexpected end of recording reached
                iStop = frameCurrent;
                iStopinBroadCast = true;
                dsyslog("cMarkAdStandalone::ProcessFiles(): recording ends unexpected before chkSTOP (%d) at frame %d", chkSTOP, frameCurrent);
                isyslog("got end of recording before recording length from info file reached");
            }
            endMarkPos = CheckStop();
        }
        CheckMarks(endMarkPos);
        if ((inBroadCast) && (!gotendmark) && (frameCurrent)) {
            sMarkAdMark tempmark;
            tempmark.type = MT_RECORDINGSTOP;
            tempmark.position = iFrameCurrent;
            AddMark(&tempmark);
        }
        marks.Save(directory, &macontext, false);
    }
    dsyslog("cMarkAdStandalone::ProcessFiles(): end processing files");
}


bool cMarkAdStandalone::SetFileUID(char *file) {
    if (!file) return false;
    struct stat statbuf;
    if (!stat(directory, &statbuf)) {
        if (chown(file, statbuf.st_uid, statbuf.st_gid) == -1) return false;
    }
    return true;
}


bool cMarkAdStandalone::IsVPSTimer() {
    if (!directory) return false;

    bool timerVPS = false;
    char *fpath   = NULL;

    if (asprintf(&fpath, "%s/%s", directory, "markad.vps") == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    FILE *mf;
    mf = fopen(fpath, "r");
    if (!mf) {
        dsyslog("cMarkAdStandalone::isVPSTimer(): markad.vps not found");
        FREE(strlen(fpath)+1, "fpath");
        free(fpath);
        return false;
    }
    FREE(strlen(fpath)+1, "fpath");
    free(fpath);

    size_t size = 0;
    char   *line        = NULL;
    char   vpsTimer[13] = "";

    while (getline(&line, &size, mf) != -1) {
        sscanf(line, "%12s", reinterpret_cast<char *>(&vpsTimer));
        if (strcmp(vpsTimer, "VPSTIMER=YES") == 0) {
            timerVPS = true;
            break;
        }
    }

    if (line) free(line);
    fclose(mf);
    return timerVPS;
}


time_t cMarkAdStandalone::GetRecordingStart(time_t start, int fd) {
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat   statbuf;
    FILE *mounts = setmntent(_PATH_MOUNTED, "r");
    int  mlen;
    int  oldmlen  = 0;
    bool useatime = false;
    while ((ent = getmntent(mounts)) != NULL) {
        if (strstr(directory, ent->mnt_dir)) {
            mlen = strlen(ent->mnt_dir);
            if (mlen > oldmlen) {
                if (strstr(ent->mnt_opts, "noatime")) {
                    useatime = true;
                }
                else {
                    useatime = false;
                }
            }
            oldmlen = mlen;
        }
    }
    endmntent(mounts);

    if (useatime) dsyslog("cMarkAdStandalone::GetRecordingStart(): mount option noatime is set, use atime from directory %s to get creation time", directory);
    else dsyslog("cMarkAdStandalone::GetRecordingStart(): mount option noatime is not set");

    if ((useatime) && (stat(directory, &statbuf) != -1)) {
        if (fabs(difftime(start,statbuf.st_atime)) < 60 * 60 * 12) {  // do not believe recordings > 12h
            dsyslog("cMarkAdStandalone::GetRecordingStart(): got recording start from directory creation time");
            return statbuf.st_atime;
        }
        dsyslog("cMarkAdStandalone::GetRecordingStart(): got no valid directory creation time, maybe recording was copied %s", strtok(ctime(&statbuf.st_atime), "\n"));
        dsyslog("cMarkAdStandalone::GetRecordingStart(): broadcast start time from vdr info file                          %s", strtok(ctime(&start), "\n"));
    }

    // (try to get from mtime)
    // (and hope info.vdr has not changed after the start of the recording)
    // since vdr 2.6 with the recording error count in the info file, this is not longer valid
    // use it only if mtime fits a common pre timer value
    if (fstat(fd,&statbuf) != -1) {
        dsyslog("cMarkAdStandalone::GetRecordingStart(): recording start from VDR info file modification time             %s", strtok(ctime(&statbuf.st_mtime), "\n"));
        if (fabs(difftime(start, statbuf.st_mtime)) < 600) {  // max valid pre time 10 min
            dsyslog("cMarkAdStandalone::GetRecordingStart(): use recording start from VDR info file modification time         %s", strtok(ctime(&statbuf.st_mtime), "\n"));
            return (time_t) statbuf.st_mtime;
        }
        else dsyslog("cMarkAdStandalone::GetRecordingStart(): vdr info file modification time %ds after recording start, file was modified because of vdr error counter", int(difftime(statbuf.st_mtime, start)));
    }

    // fallback to the directory name (time part)
    const char *timestr = strrchr(directory, '/');
    if (timestr) {
        timestr++;
        if (isdigit(*timestr)) {
            time_t now = time(NULL);
            struct tm tm_r;
            struct tm t = *localtime_r(&now, &tm_r); // init timezone
            if (sscanf(timestr, "%4d-%02d-%02d.%02d%*c%02d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, & t.tm_min)==5) {
                t.tm_year -= 1900;
                t.tm_mon--;
                t.tm_sec = 0;
                t.tm_isdst = -1;
                isyslog("getting recording start from directory (can be wrong!)");
                return mktime(&t);
            }
        }
    }
    return (time_t) 0;
}


bool cMarkAdStandalone::CheckLogo() {
    if (!macontext.Config) return false;
    if (!*macontext.Config->logoDirectory) return false;
    if (!macontext.Info.ChannelName) return false;
    int len=strlen(macontext.Info.ChannelName);
    if (!len) return false;

    dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s", macontext.Config->logoDirectory);
    dsyslog("cMarkAdStandalone::CheckLogo(): searching logo for %s", macontext.Info.ChannelName);
    DIR *dir = opendir(macontext.Config->logoDirectory);
    if (!dir) return false;

    struct dirent *dirent = NULL;
    while ((dirent = readdir(dir))) {
        if (!strncmp(dirent->d_name, macontext.Info.ChannelName, len)) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);

    if (macontext.Config->autoLogo > 0) {
        isyslog("no logo for %s %d:%d found in logo cache directory %s, trying to find logo in recording directory", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->logoDirectory);
        DIR *recDIR = opendir(macontext.Config->recDir);
        if (recDIR) {
            struct dirent *direntRec = NULL;
            while ((direntRec = readdir(recDIR))) {
                if (!strncmp(direntRec->d_name, macontext.Info.ChannelName, len)) {
                    closedir(recDIR);
                    isyslog("logo found in recording directory");
                    return true;
                }
            }
            closedir(recDIR);
        }
        isyslog("no logo for %s %d:%d found in recording directory %s, trying to extract logo from recording", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->recDir);
        ptr_cExtractLogo = new cExtractLogo(&macontext, macontext.Info.AspectRatio, recordingIndexMark);
        ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
        int startPos =  macontext.Info.tStart * 25;  // search logo from assumed start, we do not know the frame rate at this point, so we use 25
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = ptr_cExtractLogo->SearchLogo(&macontext, startPos);
        for (int retry = 2; retry <= 7; retry++) {  // do not reduce, we will not get some logos
            startPos += 5 * 60 * macontext.Video.Info.framesPerSecond; // next try 5 min later, now we know the frame rate
            if (endpos > 0) {
                dsyslog("cMarkAdStandalone::CheckLogo(): no logo found in recording, retry in %ind part of the recording at frame (%d)", retry, startPos);
                endpos = ptr_cExtractLogo->SearchLogo(&macontext, startPos);
            }
            else break;
        }
        if (ptr_cExtractLogo) {
            FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
            delete ptr_cExtractLogo;
            ptr_cExtractLogo = NULL;
        }
        if (endpos == 0) {
            dsyslog("cMarkAdStandalone::CheckLogo(): found logo in recording");
            return true;
        }
        else {
            dsyslog("cMarkAdStandalone::CheckLogo(): logo search failed");
            return false;
        }
    }
    return false;
}


bool cMarkAdStandalone::LoadInfo() {
    char *buf;
    if (asprintf(&buf, "%s/info", directory) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    if (macontext.Config->before) {
        macontext.Info.isRunningRecording = true;
        dsyslog("parameter before is set, markad is called with a running recording");
    }

    FILE *f;
    f = fopen(buf, "r");
    FREE(strlen(buf)+1, "buf");
    free(buf);
    buf = NULL;
    if (!f) {
        // second try for reel vdr
        if (asprintf(&buf, "%s/info.txt", directory) == -1) return false;
        ALLOC(strlen(buf)+1, "buf");
        f = fopen(buf,"r");
        FREE(strlen(buf)+1, "buf");
        free(buf);
        if (!f) return false;
        isREEL = true;
    }

    char *line = NULL;
    size_t linelen = 0;
    while (getline(&line, &linelen, f) != -1) {
        if (line[0] == 'C') {
            char channelname[256];
            memset(channelname, 0, sizeof(channelname));
            int result = sscanf(line, "%*c %*80s %250c", reinterpret_cast<char *>(&channelname));
            if (result == 1) {
                macontext.Info.ChannelName = strdup(channelname);
                ALLOC(strlen(macontext.Info.ChannelName)+1, "macontext.Info.ChannelName");
                char *lf = strchr(macontext.Info.ChannelName, 10);
                if (lf) {
                   *lf = 0;
                    char *tmpName = strdup(macontext.Info.ChannelName);
                    ALLOC(strlen(tmpName)+1, "macontext.Info.ChannelName");
                    *lf = 10;
                    FREE(strlen(macontext.Info.ChannelName)+1, "macontext.Info.ChannelName");
                    free(macontext.Info.ChannelName);
                    macontext.Info.ChannelName = tmpName;
                }
                char *cr = strchr(macontext.Info.ChannelName, 13);
                if (cr) {
                    *cr = 0;
                    char *tmpName = strdup(macontext.Info.ChannelName);
                    ALLOC(strlen(tmpName)+1, "macontext.Info.ChannelName");
                    *lf = 13;
                    FREE(strlen(macontext.Info.ChannelName)+1, "macontext.Info.ChannelName");
                    free(macontext.Info.ChannelName);
                    macontext.Info.ChannelName = tmpName;
                }
                for (int i = 0; i < static_cast<int> (strlen(macontext.Info.ChannelName)); i++) {
                    if (macontext.Info.ChannelName[i] == ' ') macontext.Info.ChannelName[i] = '_';
                    if (macontext.Info.ChannelName[i] == '.') macontext.Info.ChannelName[i] = '_';
                    if (macontext.Info.ChannelName[i] == '/') macontext.Info.ChannelName[i] = '_';
                }
                if ((strcmp(macontext.Info.ChannelName, "SAT_1") == 0) || (strcmp(macontext.Info.ChannelName, "SAT_1_HD")) == 0) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): channel %s has a rotating logo", macontext.Info.ChannelName);
                    macontext.Video.Logo.isRotating = true;
                }
                if ((strcmp(macontext.Info.ChannelName, "TELE_5") == 0) || (strcmp(macontext.Info.ChannelName, "TELE_5_HD")) == 0) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): channel %s has logo in the border", macontext.Info.ChannelName);
                    macontext.Video.Logo.isInBorder = true;
                }
            }
        }
        if ((line[0] == 'E') && (!bLiveRecording)) {
            long st;
            int result = sscanf(line,"%*c %*10i %20li %6i %*2x %*2x", &st, &length);
            startTime=(time_t)st;
            if (result != 2) {
                dsyslog("cMarkAdStandalone::LoadInfo(): vdr info file not valid, could not read start time and length");
                startTime = 0;
                length = 0;
            }
        }
        if (line[0] == 'T') {
            memset(title, 0, sizeof(title));
            int result = sscanf(line, "%*c %79c", title);
            if ((result == 0) || (result == EOF)) {
                title[0] = 0;
            }
            else {
                char *lf = strchr(title, 10);
                if (lf) *lf = 0;
                char *cr = strchr(title, 13);
                if (cr) *cr = 0;
            }
        }
        if (line[0] == 'F') {
            int fps;
            int result = sscanf(line, "%*c %3i", &fps);
            if ((result == 0) || (result == EOF)) {
                macontext.Video.Info.framesPerSecond = 0;
            }
            else {
                macontext.Video.Info.framesPerSecond = fps;
            }
        }
        if ((line[0] == 'X') && (!bLiveRecording)) {
            int stream = 0, type = 0;
            char descr[256];
            memset(descr, 0, sizeof(descr));
            int result=sscanf(line, "%*c %3i %3i %250c", &stream, &type, reinterpret_cast<char *>(&descr));
            if ((result != 0) && (result != EOF)) {
                if ((stream == 1) || (stream == 5)) {
                    if ((type != 1) && (type != 5) && (type != 9) && (type != 13)) {
                        isyslog("aspect ratio 16:9 (from vdr info)");
                        macontext.Info.AspectRatio.num = 16;
                        macontext.Info.AspectRatio.den = 9;
                    }
                    else {
                        isyslog("aspect ratio 4:3 (from vdr info)");
                        macontext.Info.AspectRatio.num = 4;
                        macontext.Info.AspectRatio.den = 3;
                    }
                }

                if (stream == 2) {
                    if (type == 5) {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr, '2')) {
                            isyslog("broadcast with DolbyDigital2.0 (from vdr info)");
                            macontext.Info.Channels[stream] = 2;
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr, '5')) {
                            isyslog("broadcast with DolbyDigital5.1 (from vdr info)");
                            macontext.Info.Channels[stream] = 6;
                        }
                    }
                }
            }
        }
    }
    if ((macontext.Info.AspectRatio.num == 0) && (macontext.Info.AspectRatio.den == 0)) isyslog("no aspect ratio found in vdr info");
    if (line) free(line);

    macontext.Info.timerVPS = IsVPSTimer();
    if ((length) && (startTime)) {
        if (!bIgnoreTimerInfo) {
            time_t rStart = GetRecordingStart(startTime, fileno(f));
            if (rStart) {
                dsyslog("cMarkAdStandalone::LoadInfo(): recording start at %s", strtok(ctime(&rStart), "\n"));
                dsyslog("cMarkAdStandalone::LoadInfo():     timer start at %s", strtok(ctime(&startTime), "\n"));
                if (macontext.Info.timerVPS) { //  VPS controlled recording start, we use assume broascast start 45s after recording start
                    isyslog("VPS controlled recording start");
                    macontext.Info.tStart = marks.LoadVPS(macontext.Config->recDir, "START:"); // VPS start mark
                    if (macontext.Info.tStart >= 0) {
                        dsyslog("cMarkAdStandalone::LoadInfo(): found VPS start event at offset %ds", macontext.Info.tStart);
                    }
                    else {
                        dsyslog("cMarkAdStandalone::LoadInfo(): no VPS start event found");
                        macontext.Info.tStart = 45;
                    }
                }
                else {
                    macontext.Info.tStart = static_cast<int> (startTime - rStart);
                    if (macontext.Info.tStart > 60 * 60) {   // more than 1h pre-timer make no sense, there must be a wrong directory time
                        isyslog("pre-time %is not valid, possible wrong directory time, set pre-timer to vdr default (2min)", macontext.Info.tStart);
                        macontext.Info.tStart = 120;
                    }
                    if (macontext.Info.tStart < 0) {
                        if (length + macontext.Info.tStart > 0) {
                            startTime = rStart;
                            isyslog("missed broadcast start by %02d:%02d min, event length %5ds", -macontext.Info.tStart / 60, -macontext.Info.tStart % 60, length);
                            length += macontext.Info.tStart;
                            isyslog("                                 corrected length %5ds", length);
                        }
                        else {
                            isyslog("cannot determine broadcast start, assume VDR default pre timer of 120s");
                            macontext.Info.tStart = 120;
                        }
                    }
                }
            }
            else {
                macontext.Info.tStart = 0;
            }
        }
        else {
            macontext.Info.tStart = 0;
        }
    }
    else {
        dsyslog("cMarkAdStandalone::LoadInfo(): start time and length from vdr info file not valid");
        macontext.Info.tStart = 0;
    }
    fclose(f);
    dsyslog("cMarkAdStandalone::LoadInfo(): broadcast start %is after recording start", macontext.Info.tStart);

    if ((!length) && (!bLiveRecording)) {
        esyslog("cannot read broadcast length from info, marks can be wrong!");
        macontext.Info.AspectRatio.num = 0;
        macontext.Info.AspectRatio.den = 0;
        bDecodeVideo = macontext.Config->decodeVideo;
        macontext.Video.Options.ignoreAspectRatio = false;
    }

    if (!macontext.Info.ChannelName) {
        return false;
    }
    else {
        return true;
    }
}


bool cMarkAdStandalone::CheckTS() {
    if (!directory) return false;
    char *buf;
    if (asprintf(&buf, "%s/00001.ts", directory) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");
    struct stat statbuf;
    if (stat(buf, &statbuf) == -1) {
        if (errno != ENOENT) {
            FREE(strlen(buf)+1, "buf");
            free(buf);
            return false;
        }
        buf = NULL;
    }
    if (buf) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    return true;
}


bool cMarkAdStandalone::CreatePidfile() {
    char *buf = NULL;
    if (asprintf(&buf, "%s/markad.pid", directory) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    // check for other running markad process
    FILE *oldpid=fopen(buf, "r");
    if (oldpid) {
        // found old pidfile, check if it's still running
        int pid;
        if (fscanf(oldpid, "%10i\n", &pid) == 1) {
            char procname[256] = "";
            snprintf(procname, sizeof(procname), "/proc/%i",pid);
            struct stat statbuf;
            if (stat(procname,&statbuf) != -1) {
                // found another, running markad
                fprintf(stderr, "another instance is running on %s", directory);
                abortNow = duplicate = true;
            }
        }
        fclose(oldpid);
    }
    else { // fopen above sets the error to 2, reset it here!
        errno = 0;
    }
    if (duplicate) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        return false;
    }

    FILE *pidfile = fopen(buf, "w+");

    SetFileUID(buf);

    FREE(strlen(buf)+1, "buf");
    free(buf);
    if (!pidfile) return false;
    fprintf(pidfile, "%i\n", static_cast<int> (getpid()));
    fflush(pidfile);
    fclose(pidfile);
    return true;
}


void cMarkAdStandalone::RemovePidfile() {
    if (!directory) return;
    if (duplicate) return;

    char *buf;
    if (asprintf(&buf, "%s/markad.pid", directory) != -1) {
        ALLOC(strlen(buf)+1, "buf");
        unlink(buf);
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
}


// const char cMarkAdStandalone::frametypes[8]={'?','I','P','B','D','S','s','b'};


cMarkAdStandalone::cMarkAdStandalone(const char *directoryParam, sMarkAdConfig *config, cIndex *recordingIndex) {
    setlocale(LC_MESSAGES, "");
    directory = directoryParam;
    gotendmark = false;
    inBroadCast = false;
    iStopinBroadCast = false;
    isREEL = false;
    recordingIndexMark = recordingIndex;
    marks.RegisterIndex(recordingIndexMark);
    indexFile = NULL;
    video = NULL;
    audio = NULL;
    osd = NULL;

    length = 0;
    sleepcnt = 0;
    waittime = iwaittime = 0;
    duplicate = false;
    title[0] = 0;

    macontext = {};
    macontext.Config = config;

    bDecodeVideo = config->decodeVideo;
    bDecodeAudio = config->decodeAudio;

    macontext.Info.tStart = iStart = iStop = iStopA = 0;

    if ((config->ignoreInfo & IGNORE_TIMERINFO) == IGNORE_TIMERINFO) {
        bIgnoreTimerInfo = true;
    }
    else {
        bIgnoreTimerInfo = false;
    }

    if (!config->noPid) {
        CreatePidfile();
        if (abortNow) return;
    }

    if (LOG2REC) {
        char *fbuf;
        if (asprintf(&fbuf, "%s/%s", directory, config->logFile) != -1) {
            ALLOC(strlen(fbuf)+1, "fbuf");
            if (freopen(fbuf, "w+", stdout)) {};
            SetFileUID(fbuf);
            FREE(strlen(fbuf)+1, "fbuf");
            free(fbuf);
        }
    }

    long lb;
    errno = 0;
    lb=sysconf(_SC_LONG_BIT);
    if (errno == 0) isyslog("starting markad v%s (%libit)", VERSION, lb);
    else isyslog("starting markad v%s", VERSION);

    // check avcodec library version
#if LIBAVCODEC_VERSION_INT < LIBAVCODEC_VERSION_DEPRECATED
    #error "libavcodec not installed or version not supported, please install or update libavcodec"
#endif
#if LIBAVCODEC_VERSION_INT < LIBAVCODEC_VERSION_VALID
    #warning "libavcodec version is deprecated, please update"
#endif

    int ver = avcodec_version();
    char *libver = NULL;
    if (asprintf(&libver, "%i.%i.%i", ver >> 16 & 0xFF, ver >> 8 & 0xFF, ver & 0xFF) != -1) {
        ALLOC(strlen(libver)+1, "libver");
        isyslog("using libavcodec.so.%s with %i threads", libver, config->threads);
        if (ver != LIBAVCODEC_VERSION_INT) {
            esyslog("libavcodec header version %s", AV_STRINGIFY(LIBAVCODEC_VERSION));
            esyslog("header and library mismatch, do not report decoder bugs");
        }
        if (ver < LIBAVCODEC_VERSION_VALID) esyslog("your libavcodec is deprecated, please update");
        FREE(strlen(libver)+1, "libver");
        free(libver);
    }
    tsyslog("libavcodec config: %s",avcodec_configuration());
    isyslog("on %s", directory);

    if (!bDecodeAudio) {
        isyslog("audio decoding disabled by user");
    }
    if (!bDecodeVideo) {
        isyslog("video decoding disabled by user");
    }
    if (bIgnoreTimerInfo) {
        isyslog("timer info usage disabled by user");
    }
    if (config->logoExtraction != -1) {
        // just to be sure extraction works
        bDecodeVideo = true;
    }
    if (config->before) sleep(10);

    char *tmpDir = strdup(directory);
#ifdef DEBUG_MEM
    ALLOC(strlen(tmpDir)+1, "tmpDir");
    int memsize_tmpDir = strlen(directory) + 1;
#endif
    char *datePart = strrchr(tmpDir, '/');
    if (!datePart) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): faild to find last '/'");
        FREE(strlen(tmpDir+1), "tmpDir");
        free(tmpDir);
        return;
    }
    *datePart = 0;    // cut off date part
    char *recName = strrchr(tmpDir, '/');
    if (!recName) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): faild to find last '/'");
        FREE(strlen(tmpDir+1), "tmpDir");
        free(tmpDir);
        return;
    }
    if (strstr(recName, "/@")) {
        isyslog("live-recording, disabling pre-/post timer");
        bIgnoreTimerInfo = true;
        bLiveRecording = true;
    }
    else {
        bLiveRecording = false;
    }
#ifdef DEBUG_MEM
    FREE(memsize_tmpDir, "tmpDir");
#endif
    free(tmpDir);

    if (!CheckTS()) {
        esyslog("no files found");
        abortNow = true;
        return;
    }

    if (asprintf(&indexFile, "%s/index", directory) == -1) indexFile = NULL;
    if (indexFile) {
        ALLOC(strlen(indexFile)+1, "indexFile");
    }

    if (!LoadInfo()) {
        if (bDecodeVideo) {
            esyslog("failed loading info - logo %s%sdisabled",
                    (config->logoExtraction != -1) ? "extraction" : "detection",
                    bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
            macontext.Info.tStart = iStart = iStop = iStopA = 0;
            macontext.Video.Options.ignoreLogoDetection = true;
        }
    }
    else {
        if (!CheckLogo() && (config->logoExtraction==-1) && (config->autoLogo == 0)) {
            isyslog("no logo found, logo detection disabled");
            macontext.Video.Options.ignoreLogoDetection = true;
        }
    }

    if (macontext.Info.tStart > 1) {
        if ((macontext.Info.tStart < 60) && (!macontext.Info.timerVPS)) macontext.Info.tStart = 60;
    }
    isyslog("pre-timer:        %2d:%02d:%02dh", macontext.Info.tStart / 60 / 60, abs((macontext.Info.tStart / 60) % 60), abs(macontext.Info.tStart % 60));

    if (length) isyslog("broadcast length: %2d:%02d:%02dh", length / 60 / 60, ( length / 60) % 60,  length % 60 );

    if (title[0]) {
        ptitle = title;
    }
    else {
        ptitle = const_cast<char *>(directory);
    }

    if (config->osd) {
        osd= new cOSDMessage(config->svdrphost, config->svdrpport);
        if (osd) osd->Send("%s '%s'", tr("starting markad for"), ptitle);
    }
    else {
        osd = NULL;
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (!abortNow) {
        video = new cMarkAdVideo(&macontext, recordingIndex);
        ALLOC(sizeof(*video), "video");
        audio = new cMarkAdAudio(&macontext, recordingIndex);
        ALLOC(sizeof(*audio), "audio");
        if (macontext.Info.ChannelName)
            isyslog("channel: %s", macontext.Info.ChannelName);
        if (macontext.Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264)
            macontext.Video.Options.ignoreAspectRatio = true;
    }

    framecnt1       = 0;
    framecntOverlap = 0;
    framecnt3       = 0;
    framecnt4       = 0;
    chkSTART = chkSTOP = INT_MAX;
}


cMarkAdStandalone::~cMarkAdStandalone() {
    marks.Save(directory, &macontext, true);
    if ((!abortNow) && (!duplicate)) {
        LogSeparator();
        dsyslog("time for decoding:              %3ds %3dms", decodeTime_us / 1000000, (decodeTime_us % 1000000) / 1000);
        if (logoSearchTime_ms > 0) dsyslog("time to find logo in recording: %3ds %3dms", logoSearchTime_ms / 1000, logoSearchTime_ms % 1000);
        if (logoChangeTime_ms > 0) dsyslog("time to find logo changes:      %3ds %3dms", logoChangeTime_ms / 1000, logoChangeTime_ms % 1000);

        time_t sec = endPass1.tv_sec - startPass1.tv_sec;
        suseconds_t usec = endPass1.tv_usec - startPass1.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) dsyslog("pass 1:                 time %5lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt1, framecnt1 / (sec + usec / 1000000));


        sec = endOverlap.tv_sec - startOverlap.tv_sec;
        usec = endOverlap.tv_usec - startOverlap.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) dsyslog("overlap:                time %5lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecntOverlap, framecntOverlap / (sec + usec / 1000000));

        sec = endLogoMarkOptimization.tv_sec - startLogoMarkOptimization.tv_sec;
        usec = endLogoMarkOptimization.tv_usec - startLogoMarkOptimization.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) dsyslog("logo mark optimization: time %5lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt3, framecnt3 / (sec + usec / 1000000));

        sec = endPass4.tv_sec - startPass4.tv_sec;
        usec = endPass4.tv_usec - startPass4.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) dsyslog("pass 4:                 time %5lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt4, framecnt4 / (sec + usec / 1000000));

        gettimeofday(&endAll, NULL);
        sec = endAll.tv_sec - startAll.tv_sec;
        usec = endAll.tv_usec - startAll.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        double etime = 0;
        double ftime = 0;
        etime = sec + ((double) usec / 1000000) - waittime;
        if (etime > 0) ftime = (framecnt1 + framecntOverlap + framecnt3) / etime;
        isyslog("processed time %d:%02d min with %d fps", static_cast<int> (etime / 60), static_cast<int> (etime - (static_cast<int> (etime / 60) * 60)), static_cast<int>(round(ftime)));
    }

    if ((osd) && (!duplicate)) {
        if (abortNow) {
            osd->Send("%s '%s'", tr("markad aborted for"), ptitle);
        }
        else {
            osd->Send("%s '%s'", tr("markad finished for"), ptitle);
        }
    }

    if (macontext.Info.ChannelName) {
        FREE(strlen(macontext.Info.ChannelName)+1, "macontext.Info.ChannelName");
        free(macontext.Info.ChannelName);
    }
    if (indexFile) {
        FREE(strlen(indexFile)+1, "indexFile");
        free(indexFile);
    }
    if (video) {
        FREE(sizeof(*video), "video");
        delete video;
        video = NULL;
    }
    if (audio) {
        FREE(sizeof(*audio), "audio");
        delete audio;
        audio = NULL;
    }
    if (osd) {
        FREE(sizeof(*osd), "osd");
        delete osd;
        osd = NULL;
    }
    if (ptr_cDecoder) {
        if (ptr_cDecoder->GetErrorCount() > 0) isyslog("decoding errors: %d", ptr_cDecoder->GetErrorCount());
        FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
        delete ptr_cDecoder;
        ptr_cDecoder = NULL;
    }
    RemovePidfile();
}


bool isnumber(const char *s) {
    while (*s) {
        if (!isdigit(*s))
            return false;
        s++;
    }
    return true;
}


int usage(int svdrpport) {
    // nothing done, give the user some help
    printf("Usage: markad [options] cmd <record>\n"
           "options:\n"
           "-b              --background\n"
           "                  markad runs as a background-process\n"
           "                  this will be automatically set if called with \"after\"\n"
           "-d              --disable=<option>\n"
           "                  <option>   1 = disable video decoding, 2 = disable audio\n"
           "                             decoding, 3 = disable video and audio decoding\n"
           "-i              --ignoreinfo=<info>\n"
           "                  ignores hints from info(.vdr) file\n"
           "                  <info> 4 = ignore timer info\n"
           "-l              --logocachedir\n"
           "                  directory where logos stored, default /var/lib/markad\n"
           "-p              --priority=<priority>\n"
           "                  software priority of markad when running in background\n"
           "                  <priority> from -20...19, default 19\n"
           "-r              --ioprio=<class>[,<level>]\n"
           "                  io priority of markad when running in background\n"
           "                  <class> 1 = realtime, <level> from 0..7, default 4\n"
           "                          2 = besteffort, <level> from 0..7, default 4\n"
           "                          3 = idle (default)\n"
           "                  make sure your I/O scheduler supports scheduling priorities and classes (e.g. BFQ or CFQ)\n"
           "-v              --verbose\n"
           "                  increments loglevel by one, can be given multiple times\n"
           "-B              --backupmarks\n"
           "                  make a backup of existing marks\n"
           "-L              --extractlogo=<direction>[,width[,height]]\n"
           "                  extracts logo to /tmp as pgm files (must be renamed)\n"
           "                  <direction>  0 = top left,    1 = top right\n"
           "                               2 = bottom left, 3 = bottom right\n"
           "-O              --OSD\n"
           "                  markad sends an OSD-Message for start and end\n"
           "-R              --log2rec\n"
           "                  write logfiles into recording directory\n"
           "                --logfile=<filename>\n"
           "                  logfile name (default: markad.log)\n"
           "-T              --threads=<number>\n"
           "                  number of threads used for decoding, max. 16\n"
           "                  (default is the number of cpus)\n"
           "-V              --version\n"
           "                  print version-info and exit\n"
           "                --loglevel=<level>\n"
           "                  sets loglevel to the specified value\n"
           "                  <level> 1=error 2=info 3=debug 4=trace\n"
           "                --markfile=<markfilename>\n"
           "                  set a different markfile-name\n"
           "                --nopid\n"
           "                  disables creation of markad.pid file in recdir\n"
           "                --online[=1|2] (default is 1)\n"
           "                  start markad immediately when called with \"before\" as cmd\n"
           "                  if online is 1, markad starts online for live-recordings\n"
           "                  only, online=2 starts markad online for every recording\n"
           "                  live-recordings are identified by having a '@' in the\n"
           "                  filename so the entry 'Mark instant recording' in the menu\n"
           "                  'Setup - Recording' of the vdr should be set to 'yes'\n"
           "                --pass1only\n"
           "                  process only first pass, setting of marks\n"
           "                --pass2only\n"
           "                  process only second pass, fine adjustment of marks\n"
           "                --svdrphost=<ip/hostname> (default is 127.0.0.1)\n"
           "                  ip/hostname of a remote VDR for OSD messages\n"
           "                --svdrpport=<port> (default is %i)\n"
           "                  port of a remote VDR for OSD messages\n"
           "                --astopoffs=<value> (default is 0)\n"
           "                  assumed stop offset in seconds range from 0 to 240\n"
           "                --posttimer=<value> (default is 600)\n"
           "                  additional recording after timer end in seconds range from 0 to 1200\n"
           "                --vps\n"
           "                  use markad.vps from recording directory to optimize start, stop and break marks\n"
           "                --cut\n"
           "                  cut video based on marks and write it in the recording directory\n"
           "                --ac3reencode\n"
           "                  re-encode AC3 stream to fix low audio level of cutted video on same devices\n"
           "                  requires --cut\n"
           "                --autologo=<option>\n"
           "                  <option>   0 = disable, only use logos from logo cache directory\n"
           "                             1 = deprecated, do not use\n"
           "                             2 = enable, find logo from recording and store it in the recording directory (default)\n"
           "                                 speed optimized operation mode, use it only on systems with >= 1 GB main memory\n"
           "                --fulldecode\n"
           "                  decode all video frame types and set mark position to all frame types\n"
           "                --fullencode=<streams>\n"
           "                  full reencode video generated by --cut\n"
           "                  use it only on powerfull CPUs, it will double overall run time\n"
           "                  <streams>  all  = keep all video and audio streams of the recording\n"
           "                             best = only encode best video and best audio stream, drop rest\n"
           "\ncmd: one of\n"
           "-                            dummy-parameter if called directly\n"
           "nice                         runs markad directly and with nice(19)\n"
           "after                        markad started by vdr after the recording is complete\n"
           "before                       markad started by vdr before the recording is complete, only valid together with --online\n"
           "edited                       markad started by vdr in edit function and exits immediately\n"
           "\n<record>                     is the name of the directory where the recording\n"
           "                             is stored\n\n",
           svdrpport
          );
    return -1;
}


static void signal_handler(int sig) {
    switch (sig) {
        #ifdef POSIX
        case SIGTSTP:
            isyslog("paused by signal");
            kill(getpid(), SIGSTOP);
            break;
        case SIGCONT:
            isyslog("continued by signal");
            break;
        #endif /* ifdef POSIX */

        case SIGABRT:
            esyslog("aborted by signal");
            abortNow = true;;
            break;
        case SIGSEGV: {
            esyslog("segmentation fault");

            #ifdef POSIX
            void *trace[32];
            int i, trace_size = backtrace(trace, 32);
            char **messages = backtrace_symbols(trace, trace_size);

            esyslog("[bt] Execution path:");
            for (i=0; i < trace_size; ++i) {
                esyslog("[bt] %s", messages[i]);
            }
            #endif /* #ifdef POSIX */

            _exit(1);
            break;
            }
        case SIGTERM:
        case SIGINT:
            esyslog("aborted by user");
            abortNow = true;
            break;
        default:
            break;
    }
}


char *recDir = NULL;


void freedir(void) {
    if (recDir) free(recDir);
}


int main(int argc, char *argv[]) {
    bool bAfter = false, bEdited = false;
    bool bFork = false, bNice = false, bImmediateCall = false;
    int niceLevel = 19;
    int ioprio_class = 3;
    int ioprio = 7;
    char *tok,*str;
    int ntok;
    bool bPass2Only = false;
    bool bPass1Only = false;
    struct sMarkAdConfig config = {};

    gettimeofday(&startAll, NULL);

    // set defaults
    config.decodeVideo = true;
    config.decodeAudio = true;
    config.logoExtraction = -1;
    config.logoWidth = -1;
    config.logoHeight = -1;
    config.threads = -1;
    config.astopoffs = 0;
    config.posttimer = 600;
    strcpy(config.svdrphost, "127.0.0.1");
    strcpy(config.logoDirectory, "/var/lib/markad");

    #ifdef WINDOWS
    /* winsock2 needs initialization, if we later support it. */
    w32init RuntimeInit;
    #endif

    struct servent *serv=getservbyname("svdrp", "tcp");
    if (serv) {
        config.svdrpport = htons(serv->s_port);
    }
    else {
        config.svdrpport = 6419;
    }

    atexit(freedir);

    while (1) {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"background", 0, 0, 'b'},
            {"disable", 1, 0, 'd'},
            {"ignoreinfo", 1, 0, 'i' },
            {"logocachedir", 1, 0, 'l'},
            {"priority",1,0,'p'},
            {"ioprio",1,0,'r'},
            {"verbose", 0, 0, 'v'},

            {"backupmarks", 0, 0, 'B'},
            {"saveinfo",0, 0, 'I'},
            {"extractlogo", 1, 0, 'L'},
            {"OSD",0,0,'O' },
            {"log2rec",0,0,'R'},
            {"threads", 1, 0, 'T'},
            {"version", 0, 0, 'V'},

            {"markfile",1,0,1},
            {"loglevel",1,0,2},
            {"online",2,0,3},
            {"nopid",0,0,4},
            {"svdrphost",1,0,5},
            {"svdrpport",1,0,6},
            {"pass2only",0,0,7},
            {"pass1only",0,0,8},
            {"astopoffs",1,0,9},
            {"posttimer",1,0,10},
            {"cDecoder",0,0,11},
            {"cut",0,0,12},
            {"ac3reencode",0,0,13},
            {"vps",0,0,14},
            {"logfile",1,0,15},
            {"autologo",1,0,16},
            {"fulldecode",0,0,17},
            {"fullencode",1,0,18},
            {"pts",0,0,19},

            {0, 0, 0, 0}
        };

        int option = getopt_long  (argc, argv, "bd:i:l:p:r:vBGIL:ORT:V", long_options, &option_index);
        if (option == -1) break;

        switch (option) {
            case 'b':
                // --background
                bFork = SYSLOG = true;
                break;
            case 'd':
                // --disable
                switch (atoi(optarg)) {
                    case 1:
                        config.decodeVideo = false;
                        break;
                    case 2:
                        config.decodeAudio = false;
                        break;
                    case 3:
                        config.decodeVideo = false;
                        config.decodeAudio = false;
                        break;
                    default:
                        fprintf(stderr, "markad: invalid disable option: %s\n", optarg);
                         return 2;
                         break;
                }
                break;
            case 'i':
                // --ignoreinfo
                config.ignoreInfo = atoi(optarg);
                if ((config.ignoreInfo < 1) || (config.ignoreInfo > 255)) {
                    fprintf(stderr, "markad: invalid ignoreinfo option: %s\n", optarg);
                    return 2;
                }
                break;
            case 'l':
                if ((strlen(optarg) + 1) > sizeof(config.logoDirectory)) {
                    fprintf(stderr, "markad: logo path too long: %s\n", optarg);
                    return 2;
                }
                strncpy(config.logoDirectory, optarg, sizeof(config.logoDirectory) - 1);
                break;
            case 'p':
                // --priority
                if (isnumber(optarg) || *optarg == '-') niceLevel = atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                    return 2;
                }
                bNice = true;
                break;
            case 'r':
                // --ioprio
                str=strchr(optarg, ',');
                if (str) {
                    *str = 0;
                    ioprio = atoi(str+1);
                    *str = ',';
                }
                ioprio_class = atoi(optarg);
                if ((ioprio_class < 1) || (ioprio_class > 3)) {
                    fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                    return 2;
                }
                if ((ioprio < 0) || (ioprio > 7)) {
                    fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                    return 2;
                }
                if (ioprio_class == 3) ioprio = 7;
                bNice = true;
                break;
            case 'v':
                // --verbose
                SysLogLevel++;
                if (SysLogLevel > 10) SysLogLevel = 10;
                break;
            case 'B':
                // --backupmarks
                config.backupMarks = true;
                break;
            case 'L':
                // --extractlogo
                str=optarg;
                ntok=0;
                while (true) {
                    tok=strtok(str, ",");
                    if (!tok) break;
                    switch (ntok) {
                        case 0:
                            config.logoExtraction = atoi(tok);
                            if ((config.logoExtraction < 0) || (config.logoExtraction > 3)) {
                                fprintf(stderr, "markad: invalid extractlogo value: %s\n", tok);
                                return 2;
                            }
                            break;
                        case 1:
                            config.logoWidth = atoi(tok);
                            break;
                        case 2:
                            config.logoHeight = atoi(tok);
                            break;
                         default:
                            break;
                    }
                    str = NULL;
                    ntok++;
                }
                break;
            case 'O':
                // --OSD
                config.osd = true;
                break;
            case 'R':
                // --log2rec
                LOG2REC = true;
                break;
            case 'T':
                // --threads
                config.threads = atoi(optarg);
                if (config.threads < 1) config.threads = 1;
                if (config.threads > 16) config.threads = 16;
                break;
            case 'V':
                printf("markad %s - marks advertisements in VDR recordings\n", VERSION);
                return 0;
            case '?':
                printf("unknown option ?\n");
                break;
            case 0:
                printf ("option %s", long_options[option_index].name);
                if (optarg) printf (" with arg %s", optarg);
                printf ("\n");
                break;
            case 1: // --markfile
                if ((strlen(optarg) + 1) > sizeof(config.markFileName)) {
                    fprintf(stderr, "markad: mark file name too long: %s\n", optarg);
                    return 2;
                }
                strncpy(config.markFileName, optarg, sizeof(config.markFileName) - 1);
                break;
            case 2: // --loglevel
                SysLogLevel = atoi(optarg);
                if (SysLogLevel > 10) SysLogLevel = 10;
                if (SysLogLevel < 0) SysLogLevel = 2;
                break;
            case 3: // --online
                if (optarg) {
                    config.online = atoi(optarg);
                }
                else {
                    config.online = 1;
                }
                if ((config.online != 1) && (config.online != 2)) {
                    fprintf(stderr, "markad: invalid online value: %s\n", optarg);
                    return 2;
                }
                break;
            case 4: // --nopid
                config.noPid = true;
                break;
            case 5: // --svdrphost
                if ((strlen(optarg) + 1) > sizeof(config.svdrphost)) {
                    fprintf(stderr, "markad: svdrphost too long: %s\n", optarg);
                    return 2;
                }
                strncpy(config.svdrphost, optarg, sizeof(config.svdrphost) - 1);
                break;
            case 6: // --svdrpport
                if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536) {
                    config.svdrpport = atoi(optarg);
                }
                else {
                    fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                    return 2;
                }
                break;
            case 7: // --pass2only
                bPass2Only = true;
                if (bPass1Only) {
                    fprintf(stderr, "markad: you cannot use --pass2only with --pass1only\n");
                    return 2;
                }
                break;
            case 8: // --pass1only
                bPass1Only = true;
                if (bPass2Only) {
                    fprintf(stderr, "markad: you cannot use --pass1only with --pass2only\n");
                    return 2;
                }
                break;
            case 9: // --astopoffs
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 240) {
                    config.astopoffs = atoi(optarg);
                }
                else {
                    fprintf(stderr, "markad: invalid astopoffs value: %s\n", optarg);
                    return 2;
                }
                break;
            case 10: // --posttimer
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 1200) config.posttimer=atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid posttimer value: %s\n", optarg);
                    return 2;
                }
                break;
            case 11: // --cDecoder
                fprintf(stderr, "markad: parameter --cDecoder: is depreciated, please remove it from your configuration\n");
                break;
            case 12: // --cut
                config.MarkadCut = true;
                break;
            case 13: // --ac3reencode
                config.ac3ReEncode = true;
                break;
            case 14: // --vps
                config.useVPS = true;
                break;
            case 15: // --logfile
                strncpy(config.logFile, optarg, sizeof(config.logFile) - 1);
                config.logFile[sizeof(config.logFile) - 1] = 0;
                break;
            case 16: // --autologo
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 2) config.autoLogo = atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid autologo value: %s\n", optarg);
                    return 2;
                }
                if (config.autoLogo == 1) {
                    fprintf(stderr,"markad: --autologo=1 is removed, will use --autologo=2 instead, please update your configuration\n");
                    config.autoLogo = 2;
                }
                break;
            case 17: // --fulldecode
                config.fullDecode = true;
                break;
            case 18: // --fullencode
                config.fullEncode = true;
                str = optarg;
                ntok = 0;
                while (str) {
                    tok = strtok(str, ",");
                    if (!tok) break;
                    switch (ntok) {
                        case 0:
                            if (strcmp(tok, "all") == 0) config.bestEncode = false;
                            else if (strcmp(tok, "best") == 0) config.bestEncode = true;
                                 else {
                                     fprintf(stderr, "markad: invalid --fullencode value: %s\n", tok);
                                     return 2;
                                 }
                            break;
                         default:
                            break;
                    }
                    str = NULL;
                    ntok++;
                }
                break;
            case 19: // --pts
                config.pts = true;
                break;
            default:
                printf ("? getopt returned character code 0%o ? (option_index %d)\n", option,option_index);
        }
    }

    if (optind < argc) {
        while (optind < argc) {
            if (strcmp(argv[optind], "after" ) == 0 ) {
                bAfter = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "before" ) == 0 ) {
                if (!config.online) config.online = 1;
                config.before = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "edited" ) == 0 ) {
                bEdited = true;
            }
            else if (strcmp(argv[optind], "nice" ) == 0 ) {
                bNice = true;
            }
            else if (strcmp(argv[optind], "-" ) == 0 ) {
                bImmediateCall = true;
            }
            else {
                if ( strstr(argv[optind], ".rec") != NULL ) {
                    recDir=realpath(argv[optind], NULL);
                    config.recDir = recDir;
                }
            }
            optind++;
        }
    }

    // set defaults
    if (config.logFile[0] == 0) {
        strncpy(config.logFile, "markad.log", sizeof(config.logFile));
        config.logFile[sizeof("markad.log") - 1] = 0;
    }

    // do nothing if called from vdr before/after the video is cutted
    if (bEdited) return 0;
    if ((bAfter) && (config.online)) return 0;
    if ((config.before) && (config.online == 1) && recDir && (!strchr(recDir, '@'))) return 0;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || config.before || bAfter || bNice) && recDir ) {
        // if bFork is given go in background
        if ( bFork ) {
            #ifdef POSIX
            //close_files();
            pid_t pid = fork();
            if (pid < 0) {
                char *err = strerror(errno);
                fprintf(stderr, "%s\n", err);
                return 2;
            }
            if (pid != 0) {
                return 0; // initial program immediately returns
            }
            if (chdir("/") == -1) {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            if (setsid() == (pid_t)(-1)) {
                perror("setsid");
                exit(EXIT_FAILURE);
            }
            if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
                perror("signal(SIGHUP, SIG_IGN)");
                errno = 0;
            }
            int f;

            f = open("/dev/null", O_RDONLY);
            if (f == -1) {
                perror("/dev/null");
                errno = 0;
            }
            else {
                if (dup2(f, fileno(stdin)) == -1) {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }

            f = open("/dev/null", O_WRONLY);
            if (f == -1) {
                perror("/dev/null");
                errno = 0;
            }
            else {
                if (dup2(f, fileno(stdout)) == -1) {
                    perror("dup2");
                    errno = 0;
                }
                if (dup2(f, fileno(stderr)) == -1) {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }
            #else
            std::cerr << "fork is unsupported on WIN32." << std::endl;
            #endif /* ifdef POSIX */
        }

        (void)umask((mode_t)0022);

        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors

        // should we renice ?
        if (bNice) {
            if (setpriority(PRIO_PROCESS, 0, niceLevel) == -1) {
                fprintf(stderr, "failed to set nice to %d\n", niceLevel);
            }
            if (ioprio_set(1,getpid(), ioprio | ioprio_class << 13) == -1) {
                fprintf(stderr, "failed to set ioprio to %i,%i\n", ioprio_class, ioprio);
            }
        }
        // store the real values, maybe set by calling nice
        errno = 0;
        int PrioProcess = getpriority(PRIO_PROCESS, 0);
        if ( errno ) {  // use errno because -1 is a valid return value
            fprintf(stderr,"failed to get nice value\n");
        }
        int IOPrio = ioprio_get(1, getpid());
        if (IOPrio < 0) {
            fprintf(stderr,"failed to get ioprio, rc = %d\n", IOPrio);
        }
        else IOPrio = IOPrio >> 13;

        // now do the work...
        struct stat statbuf;
        if (stat(recDir, &statbuf) == -1) {
            fprintf(stderr,"%s not found\n", recDir);
            return -1;
        }

        if (!S_ISDIR(statbuf.st_mode)) {
            fprintf(stderr, "%s is not a directory\n", recDir);
            return -1;
        }

        if (access(recDir, W_OK|R_OK) == -1) {
            fprintf(stderr,"cannot access %s\n", recDir);
            return -1;
        }

        // ignore some signals
        signal(SIGHUP, SIG_IGN);

        // catch some signals
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGSEGV, signal_handler);
        signal(SIGABRT, signal_handler);
        #ifdef POSIX
        signal(SIGUSR1, signal_handler);
        signal(SIGTSTP, signal_handler);
        signal(SIGCONT, signal_handler);
        #endif /* ifdef POSIX */

        cIndex *recordingIndex = new cIndex();
        ALLOC(sizeof(*recordingIndex), "recordingIndex");

        cmasta = new cMarkAdStandalone(recDir,&config, recordingIndex);
        ALLOC(sizeof(*cmasta), "cmasta");
        if (!cmasta) return -1;

        dsyslog("parameter --loglevel is set to %i", SysLogLevel);

        if (niceLevel != 19) {
            isyslog("parameter --priority %i", niceLevel);
            isyslog("warning: increasing priority may affect other applications");
        }
        if (ioprio_class != 3) {
            isyslog("parameter --ioprio %i", ioprio_class);
            isyslog("warning: increasing priority may affect other applications");
        }
        dsyslog("markad process nice level %i", PrioProcess);
        dsyslog("markad IO priority class  %i" ,IOPrio);

        dsyslog("parameter --logocachedir is set to %s", config.logoDirectory);
        dsyslog("parameter --threads is set to %i", config.threads);
        dsyslog("parameter --astopoffs is set to %i", config.astopoffs);
        if (LOG2REC) dsyslog("parameter --log2rec is set");

        if (config.useVPS) {
            dsyslog("parameter --vps is set");
        }
        if (config.MarkadCut) {
            dsyslog("parameter --cut is set");
        }
        if (config.ac3ReEncode) {
            dsyslog("parameter --ac3reencode is set");
            if (!config.MarkadCut) {
                esyslog("--cut is not set, ignoring --ac3reencode");
                config.ac3ReEncode = false;
            }
        }
        dsyslog("parameter --autologo is set to %i",config.autoLogo);
        if (config.fullDecode) {
            dsyslog("parameter --fulldecode is set");
        }
        if (config.fullEncode) {
            dsyslog("parameter --fullencode is set");
            if (config.bestEncode) dsyslog("encode best streams");
            else dsyslog("encode all streams");
        }
        if (!bPass2Only) {
            gettimeofday(&startPass1, NULL);
            cmasta->ProcessFiles();
            gettimeofday(&endPass1, NULL);
        }
        if (!bPass1Only) {
            gettimeofday(&startLogoMarkOptimization, NULL);
            cmasta->LogoMarkOptimization();  // logo mark optimization
            gettimeofday(&endLogoMarkOptimization, NULL);

            gettimeofday(&startOverlap, NULL);
            cmasta->ProcessOverlap();  // overlap detection
            gettimeofday(&endOverlap, NULL);
        }
        if (config.MarkadCut) {
            gettimeofday(&startPass4, NULL);
            cmasta->MarkadCut();
            gettimeofday(&endPass4, NULL);
        }
#ifdef DEBUG_MARK_FRAMES
        cmasta->DebugMarkFrames(); // write frames picture of marks to recording directory
#endif
        if (cmasta) {
            FREE(sizeof(*cmasta), "cmasta");
            delete cmasta;
            cmasta = NULL;
        }
        if (recordingIndex) {
            FREE(sizeof(*recordingIndex), "recordingIndex");
            delete recordingIndex;
            recordingIndex = NULL;
        }

#ifdef DEBUG_MEM
        memList();
#endif
        return 0;
    }
    return usage(config.svdrpport);
}
