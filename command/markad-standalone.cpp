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
#include "debug.h"
#include "audio.h"
#include "test.h"


bool SYSLOG                    = false;
bool LOG2REC                   = false;
bool restartLogoDetectionDone  = false;
int SysLogLevel                = 2;
bool abortNow                  = false;
int logoSearchTime_ms          = 0;
long int decodeTime_us         = 0;

struct timeval startAll, endAll = {};
struct timeval startTime1, endTime1 = {}; // pass 1 (logo search) time
struct timeval startTime2, endTime2 = {}; // pass 2 (mark detection) time
struct timeval startTime3, endTime3 = {}; // pass 3 (mark optimization) time
struct timeval startTime4, endTime4 = {}; // pass 4 (overlap detection) time
struct timeval startTime5, endTime5 = {}; // pass 5 (cut recording) time
struct timeval startTime6, endTime6 = {}; // pass 6 (mark pictures) time


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
        const time_t now = time(nullptr);
        if (ctime_r(&now, buf)) {
            buf[strlen(buf) - 6] = 0;
        }
        else dsyslog("ctime_r failed");
        char fmt[255];
        char prioText[10];
        switch (priority) {
        case LOG_ERR:
            strcpy(prioText,"ERROR:");
            break;
        case LOG_INFO :
            strcpy(prioText,"INFO: ");
            break;
        case LOG_DEBUG:
            strcpy(prioText,"DEBUG:");
            break;
        case LOG_TRACE:
            strcpy(prioText,"TRACE:");
            break;
        default:
            strcpy(prioText,"?????:");
            break;
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

// startFrame:
// - before CheckStart: timer or VPS start time, negativ if recoring start after timer start
// - after  CheckStart: real start frame
void cMarkAdStandalone::CalculateCheckPositions(int startFrame) {
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): timer or VPS start:  (%6d)  %3d:%02dmin", startFrame, static_cast<int>(startFrame / decoder->GetVideoFrameRate() / 60), startFrame /  decoder->GetVideoFrameRate() % 60);

    if (!length) {
        dsyslog("CalculateCheckPositions(): length of recording not found, set to 100h");
        length = 100 * 60 * 60; // try anyway, set to 100h
        startFrame = decoder->GetVideoFrameRate() * 2 * 60;  // assume default pretimer of 2min
    }

    if (startFrame < 0) {   // recodring start is too late
        isyslog("recording started too late, set start mark to start of recording");
        sMarkAdMark mark = {};
        mark.position = 1;  // do not use position 0 because this will later be deleted
        mark.type = MT_RECORDINGSTART;
        AddMark(&mark);
        startFrame = decoder->GetVideoFrameRate() * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }

    startA = startFrame;
    stopA  = startA + decoder->GetVideoFrameRate() * length;
    frameCheckStart = startA + decoder->GetVideoFrameRate() * (1.5 * MAX_ASSUMED) ; //  fit for later broadcast start
    frameCheckStop  = startFrame + decoder->GetVideoFrameRate() * (length + (1.5 * MAX_ASSUMED));

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:    %4ds  %3d:%02dmin", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame: (%6d)  %3d:%02dmin", startA, static_cast<int>(startA / decoder->GetVideoFrameRate() / 60), static_cast<int>(startA / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:  (%6d)  %3d:%02dmin", stopA, static_cast<int>(stopA / decoder->GetVideoFrameRate() / 60), static_cast<int>(stopA / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): check start set to:  (%6d)  %3d:%02dmin", frameCheckStart, static_cast<int>(frameCheckStart / decoder->GetVideoFrameRate() / 60), static_cast<int>(frameCheckStart / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): check stop set to:   (%6d)  %3d:%02dmin", frameCheckStop, static_cast<int>(frameCheckStop / decoder->GetVideoFrameRate() / 60), static_cast<int>(frameCheckStop / decoder->GetVideoFrameRate()) % 60);
}


// try MT_CHANNELSTOP
cMark *cMarkAdStandalone::Check_CHANNELSTOP() {
    // cleanup short channel stop/start pairs, they are stream errors
    cMark *channelStop = marks.GetNext(-1, MT_CHANNELSTOP);
    while (true) {
        if (!channelStop) break;
        cMark *channelStart = marks.GetNext(channelStop->position, MT_CHANNELSTART);
        if (!channelStart) break;
        int lengthChannel = 1000 * (channelStart->position - channelStop->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): channel stop (%6d) start (%6d): length %6dms", channelStop->position, channelStart->position, lengthChannel);
        if (lengthChannel <= 280) {
            dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): channel stop (%6d) start (%6d): length too short, delete marks", channelStop->position, channelStart->position);
            int tmp = channelStop->position;
            marks.Del(channelStop->position);
            marks.Del(channelStart->position);
            channelStop = marks.GetNext(tmp, MT_CHANNELSTOP);
        }
        else channelStop = marks.GetNext(channelStop->position, MT_CHANNELSTOP);
    }
    // search for channel stop mark
#define MAX_BEFORE_CHANNEL 336   // do not increase, will miss end in double episodes with 6 channels
    cMark *end = marks.GetAround(MAX_BEFORE_CHANNEL * decoder->GetVideoFrameRate(), stopA, MT_CHANNELSTOP);
    if (end) {
        int diffAssumed = (stopA - end->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTOP (%d) found %ds before assumed stop (%d)", end->position, diffAssumed, stopA);
    }
    else {
        // try last channel stop
        end = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);
        if (end) {
            int diffAssumed = (stopA - end->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP (%d) found %ds before assumed stop (%d)", end->position, diffAssumed, stopA);
            cMark *channelStartAfter = marks.GetNext(end->position, MT_CHANNELSTART);
            if (channelStartAfter) {
                int diffChannelStartstopA = (stopA - channelStartAfter->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTART (%d) %ds before assumed stop (%d)", channelStartAfter->position, diffChannelStartstopA, stopA);
                if ((diffAssumed > MAX_BEFORE_CHANNEL) && (diffChannelStartstopA > 7)) {
                    dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP (%d) too far before assumed stop and MT_CHANNELSTART (%d) far before assumed stop", end->position, channelStartAfter->position);
                    // we have valid channel stop/start marks, but no channel end mark
                    // delete all logo marks between, they contains no valid logo stop mark
                    // prevent to select later false logo stop mark
                    marks.DelFromTo(end->position + 1, channelStartAfter->position - 1, MT_LOGOCHANGE, 0xF0);
                    end = nullptr;
                }
            }
            if (diffAssumed <= -547) { // changed from -561 to -547
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP too far after assumed stop");
                end = nullptr;
            }
        }
    }
    // check if channel stop mark is valid end mark
    if (end) {
        cMark *cStart = marks.GetPrev(end->position, MT_CHANNELSTART);      // if there is short before a channel start, this stop mark belongs to next recording
        if (cStart) {
            if ((end->position - cStart->position) < (decoder->GetVideoFrameRate() * 120)) {
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTART short before MT_CHANNELSTOP found at frame %d with delta %ds, MT_CHANNELSTOP is not valid, try to find stop mark short before", cStart->position, static_cast<int> ((end->position - cStart->position) / decoder->GetVideoFrameRate()));
                end = marks.GetAround(decoder->GetVideoFrameRate() * 120, stopA - (decoder->GetVideoFrameRate() * 120), MT_CHANNELSTOP);
                if (end) dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): new MT_CHANNELSTOP found short before at frame (%d)", end->position);
            }
            else {
                cMark *cStartFirst = marks.GetNext(0, MT_CHANNELSTART);  // get first channel start mark
                cMark *movedFirst  = marks.First();                      // maybe first mark is a moved channel mark
                if (movedFirst && (movedFirst->type == MT_MOVEDSTART) && (movedFirst->oldType == MT_CHANNELSTART)) cStartFirst = movedFirst;
                if (cStartFirst) {
                    int deltaC = (end->position - cStartFirst->position) / decoder->GetVideoFrameRate();
                    if (deltaC < 244) {  // changed from 287 to 244, found shortest last part, do not reduce
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): first channel start mark (%d) and possible channel end mark (%d) to near %ds, this belongs to the next recording", cStartFirst->position, end->position, deltaC);
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): delete channel marks at (%d) and (%d)", cStartFirst->position, end->position);
                        marks.Del(cStartFirst->position);
                        marks.Del(end->position);
                        end = nullptr;
                    }
                }
            }
        }
    }
    if (criteria->GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): cleanup logo start/stop marks near by channel start marks, they are useless info logo");
        cMark *channelStart = marks.GetNext(-1, MT_CHANNELSTART);
        while (channelStart) {
#define CHANNEL_LOGO_MARK 60
            cMark *logoMark = marks.GetAround(CHANNEL_LOGO_MARK * decoder->GetVideoFrameRate(), channelStart->position, MT_LOGOCHANGE, 0xF0);
            while (logoMark) {
                int diff = abs((channelStart->position - logoMark->position) / decoder->GetVideoFrameRate());
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): delete logo mark (%d), %ds around channel start (%d)", logoMark->position, diff, channelStart->position);
                marks.Del(logoMark->position);
                logoMark = marks.GetAround(CHANNEL_LOGO_MARK * decoder->GetVideoFrameRate(), channelStart->position, MT_LOGOCHANGE, 0xF0);
            }
            channelStart = marks.GetNext(channelStart->position, MT_CHANNELSTART);
        }
    }

    if (end) {  // we found a channel end mark
        cMark *startMark = marks.GetFirst();
        int startChannelPos = startMark->position;
        // take care of first 2 marks,
        // can be hborder start and hborder stop if we have two broadcasts with 6 channels and hborder only in second bronadcast in the recording
        // can be black screen start and hborder stop if we have two broadcasts with both 6 channels and hborder in the recording
        if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_AVAILABLE) {
            const cMark *nextMark  = marks.GetNext(startMark->position, MT_ALL);
            if (nextMark && (nextMark->type == MT_HBORDERSTOP)) startChannelPos = nextMark->position;
        }
        marks.DelWeakFromTo(startChannelPos + 1, end->position, MT_CHANNELCHANGE); // delete all weak marks, except start mark
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTOP end mark (%d) found", end->position);
        return end;
    }
    else {
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): no MT_CHANNELSTOP mark found");
        return nullptr;
    }
}


// try MT_HBORDERSTOP
cMark *cMarkAdStandalone::Check_HBORDERSTOP() {
    cMark *end = nullptr;
    // cleanup very short hborder start/stop pair after detection restart, this can be a very long dark scene
    cMark *hStart = marks.GetNext(stopA - (decoder->GetVideoFrameRate() * 240), MT_HBORDERSTART);
    if (hStart) {
        cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);
        if (hStop && (hStop->position < stopA)) {
            int broadcastLength = (hStop->position - hStart->position) / decoder->GetVideoFrameRate();
            if (broadcastLength <= 92) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): found short hborder start (%d) stop (%d) pair, length %ds after detection restart, this can be a very long dark scene, delete marks", hStart->position, hStop->position, broadcastLength);
                marks.Del(hStart->position);
                marks.Del(hStop->position);
            }
        }
    }
    // search hborder stop mark around stopA
    end = marks.GetAround(600 * decoder->GetVideoFrameRate(), stopA, MT_HBORDERSTOP);  // 10 minutes
    if (end) {
        dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_HBORDERSTOP found at frame %i", end->position);
        cMark *prevHStart = marks.GetPrev(end->position, MT_HBORDERSTART);
        if (prevHStart && (prevHStart->position > stopA)) {
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder start mark (%d) is after assumed stop (%d), hborder stop mark (%d) is invalid", prevHStart->position, stopA, end->position);
            // check if we got first hborder stop of next broadcast
            cMark *hBorderStopPrev = marks.GetPrev(end->position, MT_HBORDERSTOP);
            if (hBorderStopPrev) {
                int diff = (stopA - hBorderStopPrev->position) / decoder->GetVideoFrameRate();
                if (diff <= 476) { // maybe recording length is wrong
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder stop mark (%d) is %ds before assumed stop, take this as stop mark", hBorderStopPrev->position, diff);
                    end = hBorderStopPrev;
                }
                else {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder stop mark (%d) is %ds before assumed stop, not valid", hBorderStopPrev->position, diff);
                    end = nullptr;
                }
            }
            else {
                end = nullptr;
            }
        }
    }
    else {
        if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
            cMark *hBorderLast = marks.GetPrev(INT_MAX, MT_HBORDERCHANGE, 0xF0);
            if (hBorderLast && (hBorderLast->type == MT_HBORDERSTOP)) {
                int diffAssumed = (stopA - hBorderLast->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): last hboder mark (%d) is stop mark, %ds before assumed stop (%d)", hBorderLast->position, diffAssumed, stopA);
                if (diffAssumed <= 600) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): last hboder mark stop (%d) selected as end mark", hBorderLast->position);
                    end = hBorderLast;
                }
            }
        }
    }
    // we found a hborder end mark
    if (end) {
        dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): found MT_HBORDERSTOP end mark (%d)", end->position);
        cMark *channelStart = marks.GetPrev(end->position, MT_CHANNELSTART);
        // cleanup channel start mark short before hborder stop, this is start mark of next broadcast
        if (channelStart) {
            int diff = 1000 * (end->position - channelStart->position) / decoder->GetVideoFrameRate();
            if (diff <= 1000) {
                marks.Del(channelStart->position);
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): channel start (%d) from next braodcast %dms before hborder stop (%d) found, delete channel mark", channelStart->position, diff, end->position);
            }
        }
        // optimize hborder end mark with logo stop mark in case of next broadcast is also with hborder (black closing credits or black opening credits)
        // check sequence MT_LOGOSTOP ->  MT_HBORDERSTOP (end) -> MT_HBORDERSTART
        cMark *logoStop     = marks.GetPrev(end->position, MT_LOGOSTOP);       // end of this boradcast
        cMark *hborderStart = marks.GetNext(end->position, MT_HBORDERSTART);   // start of next hborder broadcast
        if (logoStop && hborderStart) {
            int deltaLogoStop = (end->position - logoStop->position) / decoder->GetVideoFrameRate();
            int deltaAssumed  = (stopA        - logoStop->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_LOGOSTOP at (%d) %ds before hborder stop, %ds before assumed stop found", logoStop->position, deltaLogoStop, deltaAssumed);
            if ((logoStop->position > hborderStart->position) && (deltaLogoStop <= 415) && (deltaAssumed <= 12)) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): use logo stop mark at (%d) before hborder stop (%d)", logoStop->position, end->position);
                end = logoStop;
                evaluateLogoStopStartPair->SetIsAdInFrame(end->position, STATUS_DISABLED);  // prevent to false detect hborder as adinframe
            }
        }
        // optimize hborder end mark with logo stop mark in case of next broadcast is also with hborder and too early hborder stop from closing credits overlays hborder
        // check sequence MT_HBORDERSTOP (end) -> MT_LOGOSTOP -> MT_HBORDERSTART (start of next broadcast)
        logoStop     = marks.GetNext(end->position, MT_LOGOSTOP);
        hborderStart = marks.GetNext(end->position, MT_HBORDERSTART);
        if (logoStop && hborderStart) {
            int hBorderStopLogoStop  = 1000 * (logoStop->position     - end->position)      / decoder->GetVideoFrameRate();
            int logoStophBorderStart = 1000 * (hborderStart->position - logoStop->position) /  decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_HBORDERSTOP (%d) -> %dms -> MT_LOGOSTOP (%d) -> %dms -> MT_HBORDERSTART (%d)", end->position, hBorderStopLogoStop, logoStop->position, logoStophBorderStart, hborderStart->position);
            // valid example
            // MT_HBORDERSTOP (196569) -> 28000ms -> MT_LOGOSTOP (197269) -> 6040ms -> MT_HBORDERSTART (197420) RTLZWEI
            if ((hBorderStopLogoStop <= 28000) && (logoStophBorderStart >= 0) && (logoStophBorderStart <= 6040)) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): hborder end mark (%d) from closing credits overlays hborder, use logo stop after (%d)", end->position, logoStop->position);
                end = logoStop;
                evaluateLogoStopStartPair->SetIsAdInFrame(end->position, STATUS_DISABLED);  // closing credits overlay hborder, prevent to false detect this as adinframe
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): no MT_HBORDERSTOP end mark found");
    return end;
}


// try MT_VBORDERSTOP
cMark *cMarkAdStandalone::Check_VBORDERSTOP() {
    cMark *end = marks.GetAround(360 * decoder->GetVideoFrameRate(), stopA, MT_VBORDERSTOP); // 3 minutes
    if (end) {
        int deltaStopA = (end->position - stopA) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): MT_VBORDERSTOP found at frame (%d), %ds after assumed stop", end->position, deltaStopA);
        if (deltaStopA >= 326) {  // we found start of first ad from next broadcast, changed from 353 to 326
            dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): MT_VBORDERSTOP too far after assumed stop, ignoring");
            return nullptr;
        }
        if (criteria->LogoInBorder()) { // not with random logo interruption
            cMark *logoStop = marks.GetPrev(end->position, MT_LOGOSTOP);
            if (logoStop) {
                int deltaLogoStop = 1000 * (end->position - logoStop->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): MT_LOGOSTOP at (%d) %d before assumed stop found", logoStop->position, deltaLogoStop);
                if (deltaLogoStop <= 2000) {
                    dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): use logo stop mark at (%d) short before vborder stop (%d)", logoStop->position, end->position);
                    end = logoStop;
                }

            }
        }
        if (end->type == MT_VBORDERSTOP) { // we have not replaced vborder top with logo stop
            cMark *prevVStart = marks.GetPrev(end->position, MT_VBORDERSTART);
            if (prevVStart) {
                if (prevVStart->position > stopA) {
                    dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): previous vertial border start (%d) is after assumed stop (%d), delete this marks, they are form next brodcast", prevVStart->position, stopA);
                    marks.Del(prevVStart->position);
                    marks.Del(end->position);
                    end = nullptr;
                }
            }
            // we use vborder and we found final vborder end mark
            if (end && (criteria->GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_USED)) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): vertial border end mark found, delete weak marks except start mark");
                marks.DelWeakFromTo(marks.GetFirst()->position + 1, INT_MAX, MT_VBORDERCHANGE);
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): no MT_VBORDERSTOP mark found");
    return end;
}


// try MT_LOGOSTOP
cMark *cMarkAdStandalone::Check_LOGOSTOP() {
    cMark *end         = nullptr;
    cMark *lEndAssumed = marks.GetAround(MAX_ASSUMED * decoder->GetVideoFrameRate(), stopA, MT_LOGOSTOP); // do not allow more than 5 minutes away from assumed stop
    if (!lEndAssumed) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no logo stop mark found");
        return nullptr;  // no logo stop mark around assumed stop
    }

    // if not used until now, init object
    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }

    cMark *lEnd = nullptr;
    // try to select best logo end mark based on closing credits follow
    if (criteria->IsClosingCreditsChannel()) {
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): search for best logo end mark based on closing credits after logo stop");
        // search from nearest logo stop mark to end
        lEnd = lEndAssumed;
        while (!end && lEnd) {
            int diffAssumed = (lEnd->position - stopA) / decoder->GetVideoFrameRate();
            if (diffAssumed >= 0) {
                int status = evaluateLogoStopStartPair->GetIsClosingCreditsAfter(lEnd->position);
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d): %ds after assumed stop (%d), closing credits status %d", lEnd->position, diffAssumed, stopA, status);
                if (diffAssumed > MAX_ASSUMED) break;
                if (status == STATUS_YES) {
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d) closing credits follow, valid end mark found", lEnd->position);
                    end = lEnd;
                }
            }
            lEnd = marks.GetNext(lEnd->position, MT_LOGOSTOP);
        }
        // search before nearest logo stop mark
        lEnd = lEndAssumed;
        while (!end) {
            if (!lEnd) break;
            int diffAssumed = (stopA - lEnd->position) / decoder->GetVideoFrameRate();
            if (diffAssumed > 0) {
                int status = evaluateLogoStopStartPair->GetIsClosingCreditsAfter(lEnd->position);
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d): %ds before assumed stop (%d), closing credits status %d", lEnd->position, diffAssumed, stopA, status);
                if (diffAssumed > MAX_ASSUMED) break;
                if (status == STATUS_YES) {
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d) closing credits follow, valid end mark found", lEnd->position);
                    end = lEnd;
                }
            }
            lEnd = marks.GetPrev(lEnd->position, MT_LOGOSTOP);
        }
    }

    // try to select best logo end mark based on long black screen or silence
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): search for logo end mark based black screen, silence or closing logo sequence separator");
    // search from nearest logo stop mark to end
    lEnd = lEndAssumed;
    while (!end && lEnd) {
        int diffAssumed = (lEnd->position - stopA) / decoder->GetVideoFrameRate();
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): check for separator for logo stop (%d), %ds after assumed end (%d)", lEnd->position, diffAssumed, stopA);
        if (diffAssumed > MAX_ASSUMED || diffAssumed < -MAX_ASSUMED) break;
        if (HaveBlackSeparator(lEnd) || HaveSilenceSeparator(lEnd) || HaveInfoLogoSequence(lEnd)) {
            end = lEnd;
            break;
        }
        lEnd = marks.GetNext(lEnd->position, MT_LOGOSTOP);   // try next logo stop mark as end mark
    }
    // search before nearest logo stop mark
    lEnd = lEndAssumed;
    while (!end) {
        lEnd = marks.GetPrev(lEnd->position, MT_LOGOSTOP);   // try previous logo stop mark as end mark
        if (!lEnd) break;
        int diffAssumed = (stopA - lEnd->position) / decoder->GetVideoFrameRate();
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): check for separator for logo stop (%d), %ds before assumed end (%d)", lEnd->position, diffAssumed, stopA);
        // examples:
        // valid: 330 before startA
        if (diffAssumed > MAX_ASSUMED) break;
        if (HaveBlackSeparator(lEnd) || HaveSilenceSeparator(lEnd) || HaveInfoLogoSequence(lEnd)) {
            end = lEnd;
        }
    }

    // logo end mark found based on separator or closing credits, cleanup undetected info logo stop/start marks
    if (end) {
        CleanupUndetectedInfoLogo(end);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): found logo end mark (%d)", end->position);
        evaluateLogoStopStartPair->SetIsAdInFrame(end->position, STATUS_DISABLED);  // before closing credits oder separator there is no ad in frame
        cMark *logoStart = marks.GetNext(end->position, MT_LOGOSTART);
        if (logoStart) {
            int diffStart = 1000 * (logoStart->position - end->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): next logo start mark (%d) %dms after end mark (%d)", logoStart->position, diffStart, end->position);
            if (diffStart <= 880) criteria->SetClosingCreditsState(end->position, CRITERIA_UNAVAILABLE);  // early logo start after, there are no closing credits without logo
        }
        return end;
    }

    // cleanup very short start/stop pairs around possible end marks, these are logo detection failures
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): search for nearest logo end mark to assumed stop");
    while (true) {
        end = marks.GetAround(400 * decoder->GetVideoFrameRate(), stopA, MT_LOGOSTOP);
        if (end) {
            int iStopDelta = (stopA - end->position) / decoder->GetVideoFrameRate();
#define MAX_LOGO_BEFORE_ASSUMED 240
// valid examples: 105, 117, 240
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): MT_LOGOSTOP found at frame (%d), %ds (expect <= %ds) before assumed stop (%d)", end->position, iStopDelta, MAX_LOGO_BEFORE_ASSUMED, stopA);
            if (iStopDelta > MAX_LOGO_BEFORE_ASSUMED) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo stop mark too far before assumed stop");
                end = nullptr;
                break;
            }
            else {
                cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                if (prevLogoStart) {
                    int deltaLogoStart = 1000 * (end->position - prevLogoStart->position) / decoder->GetVideoFrameRate();
#define MIN_LOGO_START_STOP 2480   // very short logo start/stop can be false positiv logo detection or preview in ad before, changed from 1480 to 2480
                    if (deltaLogoStart <= MIN_LOGO_START_STOP ) {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): very short logo start (%d) stop (%d) pair is invalid, length %dms (expect >%ds), delete marks", prevLogoStart->position, end->position, deltaLogoStart, MIN_LOGO_START_STOP);
                        marks.Del(end);
                        marks.Del(prevLogoStart);
                    }
                    else {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo start mark (%d) is %dms (expect >%dms) before logo stop mark (%d), logo stop mark is valid end mark", prevLogoStart->position, deltaLogoStart, MIN_LOGO_START_STOP, end->position);
                        break;
                    }
                }
                else {
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no previous logo start mark found");
                    break;
                }
            }
        }
        else {
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no more logo stop mark found");
            break;
        }
    }
    // for broadcast without hborder check border start mark from next bradcast before logo stop
    // in this case logo stop mark is from next recording, use border start mark as end mark
    bool typeChange = false;
    if (end && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
        cMark *hBorderStart = marks.GetPrev(end->position, MT_HBORDERSTART);
        if (hBorderStart) {
            const cMark *hBorderStartPrev = marks.GetPrev(hBorderStart->position, MT_HBORDERSTART);
            if (!hBorderStartPrev) {
                int deltahBorder = (hBorderStart->position - stopA) / decoder->GetVideoFrameRate();
                int deltaLogo    = (end->position          - stopA) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): found MT_HBORDERSTART at (%d) %ds after assumed end (and no other MT_HBORDERSTART before), logo stop mark at (%d) %ds after assumed end", hBorderStart->position, deltahBorder, end->position, deltaLogo);
                if ((deltaLogo >= 0) && (deltahBorder >= -1)) {
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo stop mark at (%d) %ds after assumed end is invalid, use MT_HBORDERSTART (%d) as end mark", end->position, deltaLogo, hBorderStart->position);
                    marks.ChangeType(hBorderStart, MT_STOP);
                    end = hBorderStart;
                    typeChange = true;
                }
            }
        }
    }

    // check if very eary logo end mark is end of preview
    if (end && !typeChange) {
        int beforeAssumed = (stopA - end->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): end mark (%d) %ds before assumed stop (%d)", end->position, beforeAssumed, stopA);
        if (beforeAssumed >= 218) {
            cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
            // ad before
            cMark *prevLogoStop = nullptr;
            if (prevLogoStart) prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
            // broadcast after
            cMark *nextLogoStart = marks.GetNext(end->position, MT_LOGOSTART);
            cMark *nextLogoStop = nullptr;
            if (nextLogoStart) nextLogoStop = marks.GetNext(end->position, MT_LOGOSTOP);

            if (prevLogoStart && prevLogoStop && nextLogoStart && !nextLogoStop) {  // debug log for future use
                int adBefore = (prevLogoStart->position - prevLogoStop->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): advertising before from (%d) to (%d) %3ds", prevLogoStart->position, prevLogoStop->position, adBefore);
                int adAfter = (nextLogoStart->position - end->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): advertising after  from (%d) to (%d) %3ds", end->position, nextLogoStart->position, adAfter);
                int broadcastBefore = (end->position - prevLogoStart->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): broadcast   before from (%d) to (%d) %3ds", prevLogoStart->position, end->position, broadcastBefore);

                if (broadcastBefore <= 115) {  // end mark invalid there is only a very short broadcast after end mark, changed from 34 to 115
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): broadcast before only %ds, end mark (%d) is invalid", broadcastBefore, end->position);
                    end = nullptr;
                }
            }
        }
    }

    // now we have a logo end mark
    if (end) {
        // check previous logo stop mark against VPS stop event, if any
        cMark *prevLogoStop = marks.GetPrev(end->position, MT_LOGOSTOP); // maybe different if deleted above
        if (prevLogoStop) {
            int vpsOffset = vps->GetStop(); // get VPS stop mark
            if (vpsOffset >= 0) {
                int vpsStopFrame = index->GetFrameFromOffset(vpsOffset * 1000);
                if (vpsStopFrame >= 0) {
                    int diffAfterVPS = (prevLogoStop->position - vpsStopFrame) / decoder->GetVideoFrameRate();
                    if (diffAfterVPS >= 0) {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): VPS stop event at (%d) is %ds after previous logo stop (%d), use this as end mark", vpsStopFrame, diffAfterVPS, prevLogoStop->position);
                        end = prevLogoStop;
                    }
                }
                else esyslog("cMarkAdStandalone::Check_LOGOSTOP(): get frame number to VPS stop offset at %ds failed", vpsOffset);
            }
        }
        // check if there could follow closing credits, prevent false detection of closing credits from opening creditis of next broadcast
        cMark *nextLogoStart = marks.GetNext(end->position);
        if (nextLogoStart) {
            int closingCreditsLength = (nextLogoStart->position - end->position) / decoder->GetVideoFrameRate();
            if (closingCreditsLength <= 2) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo start (%d) %ds after end mark (%d), no closing credits without logo can follow", nextLogoStart->position, closingCreditsLength, end->position);
                criteria->SetClosingCreditsState(end->position, CRITERIA_UNAVAILABLE);
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no MT_LOGOSTOP mark found");

    // clean undeteced info logos / logo changes
    if (end) CleanupUndetectedInfoLogo(end);
    return end;
}



// detect short logo stop/start before final end mark or after final start mark
// they can be logo detection failure, undetected info logos, introduction logos or text previews over the logo (e.g. SAT.1)
// only called if we are sure this is the correct logo start/end mark by closing credit detected or separator detected
// prevent to later move end mark/start mark to previous/after logo mark from invalid logo stop/start pairs
void cMarkAdStandalone::CleanupUndetectedInfoLogo(const cMark *mark) {
    if (!mark) return;
    if (mark->type == MT_LOGOSTART) { // cleanup logo
        while (true) {
            cMark *nextLogoStop = marks.GetNext(mark->position, MT_LOGOSTOP);
            if (!nextLogoStop) return;
            cMark *nextLogoStart = marks.GetNext(nextLogoStop->position, MT_LOGOSTART);
            if (!nextLogoStart) return;
            int deltaStop = 1000 * (nextLogoStop->position  - mark->position)         / decoder->GetVideoFrameRate();
            int adLength  = 1000 * (nextLogoStart->position - nextLogoStop->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): MT_LOGOSTART (%5d) -> %6dms -> MT_LOGOSTOP (%5d) -> %6dms -> MT_LOGOSTART (%5d)", mark->position, deltaStop, nextLogoStop->position, adLength, nextLogoStart->position);
            // example of invald logo stop/start pairs in start part
            // MT_LOGOSTART ( 5343) ->  33280ms -> MT_LOGOSTOP ( 6175) ->   1240ms -> MT_LOGOSTART ( 6206)
            // MT_LOGOSTART ( 5439) ->  33320ms -> MT_LOGOSTOP ( 6272) ->   1120ms -> MT_LOGOSTART ( 6300)
            // MT_LOGOSTART ( 5439) ->  41040ms -> MT_LOGOSTOP ( 6465) ->    400ms -> MT_LOGOSTART ( 6475)
            // MT_LOGOSTART (13421) ->  55220ms -> MT_LOGOSTOP (16182) ->    900ms -> MT_LOGOSTART (16227)  -> arte HD, logo detection failure
            if ((deltaStop <= 55220) && (adLength <= 6240)) {
                dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): logo detection failure or undetected info logo from (%d) to (%d), delete marks", nextLogoStop->position, nextLogoStart->position);
                marks.Del(nextLogoStop->position);
                marks.Del(nextLogoStart->position);
            }
            else return;
        }
    }
    if (mark->type == MT_LOGOSTOP) {  // cleanup logo stop/start marks short before end mark
        while (true) {
            cMark *prevLogoStart = marks.GetPrev(mark->position, MT_LOGOSTART);
            if (!prevLogoStart) return;
            cMark *prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
            if (!prevLogoStop) return;
            int stopStart = 1000 * (prevLogoStart->position - prevLogoStop->position)  / decoder->GetVideoFrameRate();
            int startEnd  = 1000 * (mark->position          - prevLogoStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): logo stop (%d) -> %dms -> start (%d) -> %dms -> end mark (%d)", prevLogoStop->position, stopStart, prevLogoStart->position, startEnd, mark->position);
            // valid info logo sequence
            // logo stop (77361) -> 12840ms -> start (77682) -> 71680ms -> end mark (79474)
            // logo stop (78852) -> 12720ms -> start (79170) -> 75400ms -> end mark (81055)
            if ((stopStart <= 30000) && (startEnd <= 75400)) {
                dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): logo start (%5d) stop (%5d): undetected info logo or text preview over the logo, delete marks", prevLogoStart->position, prevLogoStop->position);
                marks.Del(prevLogoStart);
                marks.Del(prevLogoStop);
            }
            else return;
        }
    }
}


bool cMarkAdStandalone::HaveLowerBorder(const cMark *mark) {
    if (!mark) return false;
    if ((mark->type & 0x0F) == MT_START) {
        // check sequence MT_NOLOWERBORDERSTART -> MT_NOLOWERBORDERSTOP -> MT_LOGOSTOP -> MT_LOGOSTART (mark) [-> MT_LOGOSTOP]
        cMark *prevLogoStop = marks.GetPrev(mark->position, MT_LOGOSTOP);
        if (prevLogoStop) {
            cMark *lowerStop = blackMarks.GetPrev(prevLogoStop->position, MT_NOLOWERBORDERSTART);
            if (lowerStop) {
                cMark *lowerStart = blackMarks.GetPrev(lowerStop->position, MT_NOLOWERBORDERSTOP);
                if (lowerStart) {
                    int diffLogoStartLogoStop = INT_MAX;
                    int nextLogoStopPosition  = INT_MAX;
                    cMark *nextLogoStop = marks.GetNext(mark->position, MT_LOGOSTOP);
                    if (nextLogoStop) {
                        nextLogoStopPosition  = nextLogoStop->position;
                        diffLogoStartLogoStop = 1000 * (nextLogoStop->position - mark->position) / decoder->GetVideoFrameRate();
                    }
                    int diffLowerStartLowerStop = 1000 * (lowerStop->position    - lowerStart->position)   / decoder->GetVideoFrameRate();
                    int diffLowerStopLogoStop   = 1000 * (prevLogoStop->position - lowerStop->position)    / decoder->GetVideoFrameRate();
                    int diffLogoStopLogoStart   = 1000 * (mark->position         - prevLogoStop->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveLowerBorder(): MT_NOLOWERBORDERSTART (%d) -> %dms -> MT_NOLOWERBORDERSTOP (%d) -> %dms -> MT_LOGOSTOP (%d) -> %dms -> MT_LOGOSTART (%d) -> %dms -> MT_LOGOSTOP (%d)", lowerStart->position, diffLowerStartLowerStop, lowerStop->position, diffLowerStopLogoStop, prevLogoStop->position, diffLogoStopLogoStart, mark->position, diffLogoStartLogoStop, nextLogoStopPosition);
// valid example
// MT_NOLOWERBORDERSTART (3723) -> 4520ms -> MT_NOLOWERBORDERSTOP (3836) -> 7360ms -> MT_LOGOSTOP (4020) -> 120ms -> MT_LOGOSTART (4023) -> 2147483647ms -> MT_LOGOSTOP (2147483647)
//
// invalid example
// MT_NOLOWERBORDERSTART (8231) -> 4440ms -> MT_NOLOWERBORDERSTOP (8342) -> 240ms -> MT_LOGOSTOP (8348) -> 160ms -> MT_LOGOSTART (8352) -> 6960ms -> MT_LOGOSTOP (8526)
                    if ((diffLowerStartLowerStop >= MIN_LOWER_BORDER) && (diffLowerStartLowerStop <= MAX_LOWER_BORDER) &&
                            (diffLowerStopLogoStop <= 7360) && (diffLogoStopLogoStart <= 120) && (diffLogoStartLogoStop > 6960)) {
                        dsyslog("cMarkAdStandalone::HaveLowerBorder(): logo start mark (%d): lower border closing credits before are valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveLowerBorder(): logo start mark (%d): lower border closing credits are invalid", mark->position);
                }
            }
        }
    }
    return false;
}


bool cMarkAdStandalone::HaveSilenceSeparator(const cMark *mark) {
    if (!mark) return false;
    // check start mark
    if (mark->type == MT_LOGOSTART) {
        // check sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTART (mark)
        // does only work for short stop/start sequence without long ad between previous and current broadcast
        // too much false positiv otherwise
        cMark *silenceStop = silenceMarks.GetPrev(mark->position, MT_SOUNDSTART);
        if (silenceStop) {
            cMark *silenceStart = silenceMarks.GetPrev(silenceStop->position, MT_SOUNDSTOP);
            if (silenceStart) {
                cMark *logoStop = marks.GetPrev(silenceStart->position, MT_LOGOSTOP);
                if (logoStop) {
                    int logoStopSilenceStart    = 1000 * (silenceStart->position - logoStop->position)     / decoder->GetVideoFrameRate();
                    int silenceStartSilenceStop = 1000 * (silenceStop->position  - silenceStart->position) / decoder->GetVideoFrameRate();
                    int silenceStopLogoStart    = 1000 * (mark->position         - silenceStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%5d) -> %6dms -> MT_SOUNDSTOP (%5d) -> %5dms -> MT_SOUNDSTART (%5d) -> %5dms -> MT_LOGOSTART (%5d)", logoStop->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, mark->position);
// valid example
// MT_LOGOSTOP ( 4496) ->    560ms -> MT_SOUNDSTOP ( 4510) ->   160ms -> MT_SOUNDSTART ( 4514) ->  1520ms -> MT_LOGOSTART ( 4548)
// MT_LOGOSTOP ( 3536) ->   1520ms -> MT_SOUNDSTOP ( 3574) ->   440ms -> MT_SOUNDSTART ( 3585) ->  1880ms -> MT_LOGOSTART ( 3632)
// MT_LOGOSTOP ( 6960) ->   6120ms -> MT_SOUNDSTOP ( 7113) ->   160ms -> MT_SOUNDSTART ( 7117) ->    40ms -> MT_LOGOSTART ( 7118) -> VOX
// MT_LOGOSTOP (11369) ->   5160ms -> MT_SOUNDSTOP (11498) ->   760ms -> MT_SOUNDSTART (11517) ->    80ms -> MT_LOGOSTART (11519) -> Comedy Central
// MT_LOGOSTOP (12724) ->  13520ms -> MT_SOUNDSTOP (13062) ->   360ms -> MT_SOUNDSTART (13071) ->   120ms -> MT_LOGOSTART (13074) -> Comedy Central
// MT_LOGOSTOP ( 5923) ->  10200ms -> MT_SOUNDSTOP ( 6178) ->   840ms -> MT_SOUNDSTART ( 6199) ->    80ms -> MT_LOGOSTART ( 6201) -> Comedy Central
// MT_LOGOSTOP (10171) ->  12280ms -> MT_SOUNDSTOP (10478) ->   880ms -> MT_SOUNDSTART (10500) ->  1240ms -> MT_LOGOSTART (10531) -> DMAX
// MT_LOGOSTOP (17369) ->  36820ms -> MT_SOUNDSTOP (19210) ->   520ms -> MT_SOUNDSTART (19236) ->  2060ms -> MT_LOGOSTART (19339) -> ZDF (conflict)
//
// invalid example
// MT_LOGOSTOP (  204) ->  28460ms -> MT_SOUNDSTOP ( 1627) ->   240ms -> MT_SOUNDSTART ( 1639) ->  1840ms -> MT_LOGOSTART ( 1731) -> ZDF HD, Wetter before broadcast (conflict)
// MT_LOGOSTOP (10728) ->     80ms -> MT_SOUNDSTOP (10730) ->   480ms -> MT_SOUNDSTART (10742) ->   600ms -> MT_LOGOSTART (10757)
                    if ((logoStopSilenceStart > 80) && (logoStopSilenceStart <= 13520) &&
                            (silenceStartSilenceStop >= 160) && (silenceStartSilenceStop <=  880) &&
                            (silenceStopLogoStart    >=  40) && (silenceStopLogoStart    <= 1880)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
        // check sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_LOGOSTART -> MT_SOUNDSTART
        // silence around logo start
        silenceStop = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTART);   // end of silence can be on same frame as logo start
        if (silenceStop) {
            cMark *silenceStart = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);
            if (silenceStart) {
                cMark *logoStop = marks.GetPrev(silenceStart->position, MT_LOGOSTOP);
                if (logoStop) {
                    int logoStopSilenceStart  = 1000 * (silenceStart->position - logoStop->position)     / decoder->GetVideoFrameRate();
                    int silenceStartLogoStart = 1000 * (mark->position         - silenceStart->position) / decoder->GetVideoFrameRate();
                    int logoStartSilenceStop  = 1000 * (silenceStop->position  - mark->position)         / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%6d) -> %5dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_LOGOSTART (%6d) -> %4dms -> MT_SOUNDSTART (%6d)", logoStop->position, logoStopSilenceStart, silenceStart->position, silenceStartLogoStart, mark->position, logoStartSilenceStop, silenceStop->position);
// valid example
// MT_LOGOSTOP (  8282) ->  3920ms -> MT_SOUNDSTOP (  8380) ->   80ms -> MT_LOGOSTART (  8382) ->   40ms -> MT_SOUNDSTART (  8383)
// MT_LOGOSTOP (  6556) -> 42040ms -> MT_SOUNDSTOP (  7607) ->  360ms -> MT_LOGOSTART (  7616) ->   40ms -> MT_SOUNDSTART (  7617) -> RTL Television
// MT_LOGOSTOP (  6774) -> 36560ms -> MT_SOUNDSTOP (  7688) ->  360ms -> MT_LOGOSTART (  7697) ->    0ms -> MT_SOUNDSTART (  7697) -> RTL Television
// MT_LOGOSTOP ( 13138) -> 13520ms -> MT_SOUNDSTOP ( 13476) ->  480ms -> MT_LOGOSTART ( 13488) ->   80ms -> MT_SOUNDSTART ( 13490) -> Comedy Central
// MT_LOGOSTOP (  9990) -> 13800ms -> MT_SOUNDSTOP ( 10335) ->  200ms -> MT_LOGOSTART ( 10340) ->   80ms -> MT_SOUNDSTART ( 10342) -> Comedy Central
//
// invalid example
// MT_LOGOSTOP (  1358) ->   920ms -> MT_SOUNDSTOP (  1381) ->  280ms -> MT_LOGOSTART (  1388) ->    0ms -> MT_SOUNDSTART (  1388)  -> RTL Television preview start
                    if ((logoStopSilenceStart > 920) && (logoStopSilenceStart <= 42040) &&
                            (silenceStartLogoStart <= 480) && (logoStartSilenceStop <= 80)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
        // check sequence MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTOP -> MT_LOGOSTART (mark)
        // in this case logo stop mark is a valid end mark from previous broadcast, far away next logo start mark is valid start mark
        cMark *logoStop = marks.GetPrev(mark->position, MT_LOGOSTOP);
        if (logoStop) {
            silenceStop = silenceMarks.GetPrev(logoStop->position, MT_SOUNDSTART);
            if (silenceStop) {
                cMark *silenceStart = silenceMarks.GetPrev(silenceStop->position, MT_SOUNDSTOP);
                if (silenceStart) {
                    int silenceStarSilenceStop = 1000 * (silenceStop->position  - silenceStart->position) / decoder->GetVideoFrameRate();
                    int silenceStopLogoStop    = 1000 * (logoStop->position     - silenceStop->position)  / decoder->GetVideoFrameRate();
                    int logoStopLogoStart      = 1000 * (mark->position         - logoStop->position)     / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_SOUNDSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %4dms -> MT_LOGOSTOP (%6d) -> %4dms -> MT_LOGOSTART (%6d)", silenceStart->position, silenceStarSilenceStop, silenceStop->position, silenceStopLogoStop, logoStop->position, logoStopLogoStart, mark->position);
                    // valid example
                    // MT_SOUNDSTOP ( 17992) ->  680ms -> MT_SOUNDSTART ( 18026) ->  400ms -> MT_LOGOSTOP ( 18046) -> 29980ms -> MT_LOGOSTART ( 19545) -> phoenix HD
                    if ((silenceStarSilenceStop >= 680) && (silenceStopLogoStop <= 400) && (logoStopLogoStart >= 29980)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }

        // check sequence MT_LOGOSTART (mark) -> MT_SOUNDSTOP -> MT_SOUNDSTART
        // broadcast start with long silence in opening credits
        cMark *silenceStart = silenceMarks.GetNext(mark->position, MT_SOUNDSTOP);
        if (silenceStart) {
            silenceStop = silenceMarks.GetNext(silenceStart->position, MT_SOUNDSTART);
            if (silenceStop) {
                int diffLogoStartSilenceStart   = 1000 * (silenceStart->position  - mark->position)         / decoder->GetVideoFrameRate();
                int diffSilenceStartSilenceStop = 1000 * (silenceStop->position   - silenceStart->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTART (%6d) -> %4dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d)", mark->position, diffLogoStartSilenceStart, silenceStart->position, diffSilenceStartSilenceStop, silenceStop->position);
                // valid example
                // MT_LOGOSTART (  8632) ->  400ms -> MT_SOUNDSTOP (  8642) -> 5720ms -> MT_SOUNDSTART (  8785)
                if ((diffLogoStartSilenceStart <= 400) && (diffSilenceStartSilenceStop >= 5720)) {
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                    return true;
                }
                dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
            }
        }
    }

    // check stop mark
    if (mark->type == MT_LOGOSTOP) {
        // sequence MT_LOGOSTART -> MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTART
        cMark *logoStartBefore = marks.GetPrev(mark->position, MT_LOGOSTART);
        if (logoStartBefore) {
            cMark *silenceStart = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP); // silence can start at the same position as logo stop
            if (silenceStart) {
                cMark *silenceStop = silenceMarks.GetNext(silenceStart->position, MT_SOUNDSTART);
                if (silenceStop) {
                    cMark *logoStartAfter = marks.GetNext(silenceStop->position, MT_LOGOSTART);
                    if (logoStartAfter) {
                        int logoStartLogoStop       = 1000 * (mark->position           - logoStartBefore->position) / decoder->GetVideoFrameRate();
                        int logoStopSilenceStart    = 1000 * (silenceStart->position   - mark->position)            / decoder->GetVideoFrameRate();
                        int silenceStartSilenceStop = 1000 * (silenceStop->position    - silenceStart->position)    / decoder->GetVideoFrameRate();
                        int silenceStopLogoStart    = 1000 * (logoStartAfter->position - silenceStop->position)     / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTART (%6d) -> %7ds -> MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %5dms -> MT_LOGOSTART (%6d)", logoStartBefore->position, logoStartLogoStop, mark->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, logoStartAfter->position);
// valid sequence
// MT_LOGOSTART ( 16056) -> 1505140s -> MT_LOGOSTOP ( 91313) ->  140ms -> MT_SOUNDSTOP ( 91320) ->  180ms -> MT_SOUNDSTART ( 91329) ->  1920ms -> MT_LOGOSTART ( 91425) -> RTL Television
// MT_LOGOSTART ( 84526) ->   10400s -> MT_LOGOSTOP ( 84786) ->  160ms -> MT_SOUNDSTOP ( 84790) ->   80ms -> MT_SOUNDSTART ( 84792) ->  1920ms -> MT_LOGOSTART ( 84840) -> Pro7 MAXX
// MT_LOGOSTART ( 81870) ->   10400s -> MT_LOGOSTOP ( 82130) ->    0ms -> MT_SOUNDSTOP ( 82130) ->  320ms -> MT_SOUNDSTART ( 82138) ->  1840ms -> MT_LOGOSTART ( 82184) -> Pro7 MAXX
// MT_LOGOSTART ( 56844) ->   31760s -> MT_LOGOSTOP ( 57638) ->  840ms -> MT_SOUNDSTOP ( 57659) ->  440ms -> MT_SOUNDSTART ( 57670) ->  2360ms -> MT_LOGOSTART ( 57729) -> Comedy Central
// MT_LOGOSTART ( 56372) ->   26000s -> MT_LOGOSTOP ( 57022) ->   80ms -> MT_SOUNDSTOP ( 57024) -> 1160ms -> MT_SOUNDSTART ( 57053) ->  2440ms -> MT_LOGOSTART ( 57114) -> Comedy Central
// MT_LOGOSTART ( 79759) ->  224920s -> MT_LOGOSTOP ( 85382) ->  200ms -> MT_SOUNDSTOP ( 85387) ->   40ms -> MT_SOUNDSTART ( 85388) -> 27560ms -> MT_LOGOSTART ( 86077) -> VOX
// MT_LOGOSTART ( 79759) ->  224880s -> MT_LOGOSTOP ( 85381) ->  240ms -> MT_SOUNDSTOP ( 85387) ->   40ms -> MT_SOUNDSTART ( 85388) ->   240ms -> MT_LOGOSTART ( 85394) -> VOX
//
// invalid sequence
// MT_LOGOSTART ( 87985) ->    1400s -> MT_LOGOSTOP ( 88020) -> 4840ms -> MT_SOUNDSTOP ( 88141) ->  160ms -> MT_SOUNDSTART ( 88145) ->  8400ms -> MT_LOGOSTART ( 88355) -> DMAX
                        if ((logoStartLogoStop >= 10400) &&            // possible end of info logo before
                                (logoStopSilenceStart <= 840) &&       // silence start short after end mark
                                (silenceStartSilenceStop >= 40) && (silenceStartSilenceStop <= 1160) &&
                                (silenceStopLogoStart >= 240) && (silenceStopLogoStart <= 27560)) {
                            dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is valid", mark->position);
                            return true;
                        }
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is invalid", mark->position);
                    }
                }
            }
        }

        // sequence MT_SOUNDSTOP -> MT_LOGOSTOP (mark) -> MT_SOUNDSTART -> MT_LOGOSTART
        // silence around end mark
        cMark *silenceStart = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);
        if (silenceStart) {
            cMark *silenceStop = silenceMarks.GetNext(mark->position, MT_SOUNDSTART);
            if (silenceStop) {
                cMark *logoStart = marks.GetNext(silenceStop->position, MT_LOGOSTART);
                if (logoStart) {
                    int silenceStartLogoStop = 1000 * (mark->position         - silenceStart->position) / decoder->GetVideoFrameRate();
                    int logoStopSilenceStop  = 1000 * (silenceStop->position  - mark->position)         / decoder->GetVideoFrameRate();
                    int silenceStopLogoStart = 1000 * (logoStart->position    - silenceStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_SOUNDSTOP (%6d) -> %4dms MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %6dms -> MT_LOGOSTART (%6d)", silenceStart->position, silenceStartLogoStop, mark->position, logoStopSilenceStop, silenceStop->position, silenceStopLogoStart, logoStart->position);
// valid sequence
// MT_SOUNDSTOP ( 72667) ->  560ms MT_LOGOSTOP ( 72681) ->  240ms -> MT_SOUNDSTART ( 72687) ->   1920ms -> MT_LOGOSTART ( 72735)
// MT_SOUNDSTOP ( 44964) -> 3400ms MT_LOGOSTOP ( 45049) -> 1200ms -> MT_SOUNDSTART ( 45079) ->   2480ms -> MT_LOGOSTART ( 45141)
// MT_SOUNDSTOP (158458) ->   40ms MT_LOGOSTOP (158460) ->  160ms -> MT_SOUNDSTART (158468) ->  27540ms -> MT_LOGOSTART (159845)
//
// invalid example
// MT_SOUNDSTOP ( 88721) ->  120ms MT_LOGOSTOP ( 88724) ->  240ms -> MT_SOUNDSTART ( 88730) ->  65560ms -> MT_LOGOSTART ( 90369)  -> stop mark before last ad
// MT_SOUNDSTOP ( 40563) ->  240ms MT_LOGOSTOP ( 40569) ->   80ms -> MT_SOUNDSTART ( 40571) ->   4040ms -> MT_LOGOSTART ( 40672)  -> stop mark between preview and last valid start mark
                    if ((silenceStartLogoStop <= 3400) &&
                            (logoStopSilenceStop  > 80) && (logoStopSilenceStop <= 1200) &&
                            (silenceStopLogoStart <= 27540)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
        // sequence MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTOP -> MT_LOGOSTART
        silenceStart = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);
        if (silenceStart) {
            cMark *silenceStop = silenceMarks.GetNext(silenceStart->position, MT_SOUNDSTART);
            if (silenceStop) {
                cMark *logoStart = marks.GetNext(mark->position, MT_LOGOSTART);
                if (logoStart) {
                    int silenceStartSilenceStop = 1000 * (silenceStop->position - silenceStart->position) / decoder->GetVideoFrameRate();
                    int silenceStopLogoStop     = 1000 * (mark->position        - silenceStop->position)  / decoder->GetVideoFrameRate();
                    int logoStopLogoStart       = 1000 * (logoStart->position   - mark->position)         / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_SOUNDSTOP (%6d) -> %5dms -> MT_SOUNDSTART (%6d) -> %5dms -> MT_LOGOSTOP (%6d) -> %5dms -> MT_LOGOSTART (%6d)", silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStop, mark->position, logoStopLogoStart, logoStart->position);
                    // valid sequence
                    // MT_SOUNDSTOP ( 96076) ->   320ms -> MT_SOUNDSTART ( 96092) ->   360ms -> MT_LOGOSTOP ( 96110) -> 13620ms -> MT_LOGOSTART ( 96791)
                    // MT_SOUNDSTOP ( 96076) ->   320ms -> MT_SOUNDSTART ( 96092) ->   380ms -> MT_LOGOSTOP ( 96111) -> 13600ms -> MT_LOGOSTART ( 96791)
                    //
                    // invalid sequence
                    // MT_SOUNDSTOP ( 40563) ->   320ms -> MT_SOUNDSTART ( 40571) ->     0ms -> MT_LOGOSTOP ( 40571) ->  4040ms -> MT_LOGOSTART ( 40672)
                    // MT_SOUNDSTOP ( 38187) ->   320ms -> MT_SOUNDSTART ( 38195) ->     0ms -> MT_LOGOSTOP ( 38195) ->  4040ms -> MT_LOGOSTART ( 38296)
                    if ((silenceStartSilenceStop >= 320) && (silenceStopLogoStop >= 360) && (silenceStopLogoStop <= 380) && (logoStopLogoStart <= 13620)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
    }
    return false;
}


bool cMarkAdStandalone::HaveBlackSeparator(const cMark *mark) {
    if (!mark) return false;
    // check log start mark
    if (mark->type == MT_LOGOSTART) {

        // check squence MT_LOGOSTOP -> MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART (mark) -> MT_LOGOSTOP
        cMark *blackStop = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART);
        if (blackStop) {  // from above
            cMark *blackStart = blackMarks.GetPrev(blackStop->position, MT_NOBLACKSTOP);
            if (blackStart) {
                cMark *stopBefore = marks.GetPrev(blackStart->position, MT_LOGOSTOP);
                if (stopBefore) {
                    cMark *stopAfter = marks.GetNext(mark->position, MT_LOGOSTOP);
                    int diffLogoStartLogoStop  = INT_MAX;
                    int stopAfterPosition      = INT_MAX;
                    if (stopAfter) {
                        stopAfterPosition           = stopAfter->position;
                        diffLogoStartLogoStop       = 1000 * (stopAfter->position  - mark->position)       / decoder->GetVideoFrameRate();
                    }
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - stopBefore->position) / decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart  = 1000 * (mark->position       - blackStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP(%5d)->%5dms->MT_NOBLACKSTOP(%5d)->%3dms->MT_NOBLACKSTART(%5d)->%3dms->MT_LOGOSTART(%5d)->%dms->MT_LOGOSTOP(%5d)", stopBefore->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, mark->position, diffLogoStartLogoStop, stopAfterPosition);
// black screen short after end mark of previous broadcast
// valid example
// tbd
//
// invalid example
// MT_LOGOSTOP(11443)->  40ms->MT_NOBLACKSTOP(11444)->120ms->MT_NOBLACKSTART(11447)-> 8440ms->MT_LOGOSTART(11658)->  9760ms->MT_LOGOSTOP(11902) Comedy Central blackscreen before preview
// MT_LOGOSTOP( 1908)-> 560ms->MT_NOBLACKSTOP( 1922)->640sm->MT_NOBLACKSTART( 1938)-> 5000ms->MT_LOGOSTART( 2063)->192200ms->MT_LOGOSTOP( 6868) RTL Television blackscreen last broadcast
// MT_LOGOSTOP(  445)-> 520ms->MT_NOBLACKSTOP(  458)->640ms->MT_NOBLACKSTART(  474)->24160ms->MT_LOGOSTART( 1078)->231720ms->MT_LOGOSTOP( 6871) RTL Television blackscreen before preview
                    if ((diffLogoStopBlackStart <= 7040) && (diffBlackStartBlackStop >= 40) && (diffBlackStopLogoStart <= 38720) && (diffLogoStartLogoStop > 231720)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence after previous stop is valid");
                        return true;
                    }
// black screen short before start mark of broadcast
// valid example
// MT_LOGOSTOP( 8177)->31200ms->MT_NOBLACKSTOP( 8957)->120ms->MT_NOBLACKSTART( 8960)-> 680ms->MT_LOGOSTART( 8977)->2147483647ms->MT_LOGOSTOP(2147483647) Disney Channel
// MT_LOGOSTOP( 6035)-> 8240ms->MT_NOBLACKSTOP( 6241)->160ms->MT_NOBLACKSTART( 6245)->1400ms->MT_LOGOSTART( 6280)->2147483647ms->MT_LOGOSTOP(2147483647) DMAX
// MT_LOGOSTOP( 9047)->47380ms->MT_NOBLACKSTOP(11416)->180ms->MT_NOBLACKSTART(11425)->1660ms->MT_LOGOSTART(11508)->2147483647ms->MT_LOGOSTOP(2147483647) KiKA
//
// invalid example
// logo start with black screen before preview
// MT_LOGOSTOP( 6578)->165640ms->MT_NOBLACKSTOP(10719)->840ms->MT_NOBLACKSTART(10740)-> 680ms->MT_LOGOSTART(10757)->     27440ms->MT_LOGOSTOP(     11443) Comedy Central
                    if ((diffLogoStopBlackStart >= 8240) && (diffBlackStartBlackStop >= 120) && (diffBlackStopLogoStart <= 1660) && (diffLogoStartLogoStop > 27440)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence before start is invalid");
                }
            }
        }

        // check sequence MT_NOBLACKSTOP -> MT_LOGOSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART (mark)
        blackStop = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART);  // black screen end can be on same position as logo start
        if (blackStop) {
            cMark *stopBefore = marks.GetPrev(blackStop->position, MT_LOGOSTOP);
            if (stopBefore) {
                cMark *blackStart = blackMarks.GetPrev(stopBefore->position, MT_NOBLACKSTOP);
                if (blackStart) {
                    int diffBlackStartLogoStop = 1000 * (stopBefore->position - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffLogoStopBlackStop  = 1000 * (blackStop->position - stopBefore->position)  / decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart = 1000 * (mark->position - blackStop->position)        / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%d) -> %4dms -> MT_LOGOSTOP (%d) -> %4dms -> MT_NOBLACKSTART (%d) -> %4dms -> MT_LOGOSTART (%d)", blackStart->position, diffBlackStartLogoStop, stopBefore->position, diffLogoStopBlackStop, blackStop->position, diffBlackStopLogoStart, mark->position);
// valid sequence
// MT_NOBLACKSTOP (8245) -> 4760ms -> MT_LOGOSTOP (8364) -> 5000ms -> MT_NOBLACKSTART (8489) -> 2360ms -> MT_LOGOSTART (8548) -> TELE 5
// MT_NOBLACKSTOP (7254) ->  280ms -> MT_LOGOSTOP (7261) ->   40ms -> MT_NOBLACKSTART (7262) -> 4000ms -> MT_LOGOSTART (7362) -> RTL_Television
// invalid sequence
// MT_NOBLACKSTOP (2818) -> 1480ms -> MT_LOGOSTOP (2855) ->   40ms -> MT_NOBLACKSTART (2856) -> 2040ms -> MT_LOGOSTART (2907) -> RTL2 (black screen before preview)
// MT_NOBLACKSTOP (2965) -> 1840ms -> MT_LOGOSTOP (3011) ->   40ms -> MT_NOBLACKSTART (3012) -> 1600ms -> MT_LOGOSTART (3052) -> RTL2 (black screen before preview)
// MT_NOBLACKSTOP (3480) -> 1240ms -> MT_LOGOSTOP (3511) ->   40ms -> MT_NOBLACKSTART (3512) -> 1800ms -> MT_LOGOSTART (3557) -> RTL2 (black screen before preview)
                    if ((diffBlackStartLogoStop <= 4760) && (diffLogoStopBlackStop <= 5000) &&
                            (diffBlackStopLogoStart >= 2360) && (diffBlackStopLogoStart <= 4000)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is invalid");
                }
            }
        }

        // check squence MT_LOGOSTOP ->  MT_LOGOSTART (mark) -> MT_NOBLACKSTOP -> MT_NOBLACKSTART
        cMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
        if (stopBefore) {
            cMark *blackStart = blackMarks.GetNext(mark->position, MT_NOBLACKSTOP);
            if (blackStart) {
                blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopLogoStart   = 1000 * (mark->position       - stopBefore->position) / decoder->GetVideoFrameRate();
                    int diffLogoStartBlackStart = 1000 * (blackStart->position - mark->position)       / decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%d)-> %3dms -> MT_LOGOSTART (%d) -> %3dms -> MT_NOBLACKSTOP (%d) -> %3dms -> MT_NOBLACKSTART (%d)", stopBefore->position, diffLogoStopLogoStart, mark->position, diffLogoStartBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position);
                    // valid example
                    // MT_LOGOSTOP (3996)-> 2040ms ->  MT_LOGOSTART (4047) -> 680ms -> MT_NOBLACKSTOP (4064) -> 200ms -> MT_NOBLACKSTART (4069)
                    if ((diffLogoStopLogoStart <= 2040) && (diffLogoStartBlackStart <= 680) && (diffBlackStartBlackStop >= 200)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is invalid");
                }
            }
        }

        // check squence MT_LOGOSTOP ->  MT_NOBLACKSTOP -> MT_LOGOSTART (mark) -> MT_NOBLACKSTART
        // black screen around logo start mark
        blackStop = blackMarks.GetNext(mark->position, MT_NOBLACKSTART);
        if (blackStop) {
            cMark *blackStart = blackMarks.GetPrev(blackStop->position, MT_NOBLACKSTOP);
            if (blackStart) {
                stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
                if (stopBefore) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - stopBefore->position) / decoder->GetVideoFrameRate();
                    int diffBlackStartLogoStart = 1000 * (mark->position       - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffLogoStartBlackStop  = 1000 * (blackStop->position  - mark->position)       / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%5d)-> %5dms -> MT_NOBLACKSTOP (%5d) -> %4dms -> MT_LOGOSTART (%5d) -> %4dms -> MT_NOBLACKSTART (%5d)", stopBefore->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartLogoStart, mark->position, diffLogoStartBlackStop, blackStop->position);
// valid example
// MT_LOGOSTOP (16022)-> 79080ms -> MT_NOBLACKSTOP (19976) ->   40ms -> MT_LOGOSTART (19978) ->  440ms -> MT_NOBLACKSTART (20000)
// MT_LOGOSTOP ( 7582)->   720ms -> MT_NOBLACKSTOP ( 7600) -> 2800ms -> MT_LOGOSTART ( 7670) -> 1880ms -> MT_NOBLACKSTART ( 7717) long black opening credits, fade in logo  (TELE 5)
// MT_LOGOSTOP ( 8017)-> 21000ms -> MT_NOBLACKSTOP ( 8542) ->    0ms -> MT_LOGOSTART ( 8542) -> 2920ms -> MT_NOBLACKSTART ( 8615) -> Comedy Central
                    if ((diffLogoStopBlackStart  >= 0) && (diffLogoStopBlackStart  <= 79080) &&
                            (diffBlackStartLogoStart >= 0) && (diffBlackStartLogoStart <= 2800) &&
                            (diffLogoStartBlackStop  >= 0) && (diffLogoStartBlackStop  <= 2920)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is invalid");
                }
            }
        }

        // check sequence MT_LOGOSTART (mark) -> MT_NOBLACKSTOP -> MT_NOBLACKSTRT
        // black screen short after logo start
        cMark *blackStart = blackMarks.GetNext(mark->position, MT_NOBLACKSTOP);
        if (blackStart) {
            blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
            if (blackStop) {
                int diffLogoStartBlackStart  = 1000 * (blackStart->position - mark->position) / decoder->GetVideoFrameRate();
                int diffBlackStartBlackStop  = 1000 * (blackStop->position - blackStart->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTART (%5d)-> %5dms -> MT_NOBLACKSTOP (%5d) -> %4dms -> MT_NOBLACKSTART (%5d)", mark->position, diffLogoStartBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position);
                // valid example
                // MT_LOGOSTART (14148)->    40ms -> MT_NOBLACKSTOP (14150) ->   40ms -> MT_NOBLACKSTART (14152)  arte HD
                if ((diffLogoStartBlackStart <= 40) && (diffBlackStartBlackStop <= 40)) {
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                    return true;
                }
                else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is invalid");
            }
        }
    }

    // check logo stop mark
    if (mark->type == MT_LOGOSTOP) {
        // check sequence MT_NOBLACKSTOP -> MT_LOGOSTOP (mark) -> MT_NOBLACKSTART -> MT_LOGOSTART
        // black screen around end mark
        cMark *startAfter = marks.GetNext(mark->position, MT_LOGOSTART);
        if (startAfter) {
            cMark *blackStart = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP); // black screen can start at the same position as logo stop
            if (blackStart) {
                cMark *blackStop = blackMarks.GetNext(mark->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffBlackStartLogoStop = 1000 * (mark->position       - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffLogoStopBlackStop  = 1000 * (blackStop->position  - mark->position)       / decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart = 1000 * (startAfter->position - blackStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%6d) -> %4dms -> MT_LOGOSTOP (%6d) -> %4dms -> MT_NOBLACKSTART (%6d) -> %5dms -> MT_LOGOSTART (%6d)", blackStart->position, diffBlackStartLogoStop, mark->position, diffLogoStopBlackStop, blackStop->position, diffBlackStopLogoStart, startAfter->position);
// valid sequence
// MT_NOBLACKSTOP ( 90520) ->  360ms -> MT_LOGOSTOP ( 90529) ->  200ms -> MT_NOBLACKSTART ( 90534) ->  3960ms -> MT_LOGOSTART ( 90633)
// MT_NOBLACKSTOP ( 47364) -> 1320ms -> MT_LOGOSTOP ( 47397) ->  280ms -> MT_NOBLACKSTART ( 47404) -> 18080ms -> MT_LOGOSTART ( 47856)
// MT_NOBLACKSTOP ( 84098) -> 1400ms -> MT_LOGOSTOP ( 84133) ->   40ms -> MT_NOBLACKSTART ( 84134) ->  1560ms -> MT_LOGOSTART ( 84173) -> RTL2
// MT_NOBLACKSTOP ( 42629) ->  760ms -> MT_LOGOSTOP ( 42648) ->   40ms -> MT_NOBLACKSTART ( 42649) ->   840ms -> MT_LOGOSTART ( 42670) -> SIXX
// MT_NOBLACKSTOP ( 44025) ->  840ms -> MT_LOGOSTOP ( 44046) ->  360ms -> MT_NOBLACKSTART ( 44055) ->   520ms -> MT_LOGOSTART ( 44068) -> SIXX
// MT_NOBLACKSTOP (260383) ->  400ms -> MT_LOGOSTOP (260403) ->   80ms -> MT_NOBLACKSTART (260407) -> 24320ms -> MT_LOGOSTART (261623) -> zdf neo HD
                    if ((diffBlackStartLogoStop >= 360) && (diffBlackStartLogoStop <= 1400) &&
                            (diffLogoStopBlackStop  >=  40) && (diffLogoStopBlackStop  <=   360) &&
                            (diffBlackStopLogoStart >= 520) && (diffBlackStopLogoStart <= 24320)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
                }
            }
        }

        // check sequence MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTOP  (blackscreen short before logo stop mark)
        cMark *blackStop = blackMarks.GetPrev(mark->position, MT_NOBLACKSTART);
        if (blackStop) {
            cMark *blackStart = blackMarks.GetPrev(blackStop->position, MT_NOBLACKSTOP);   // this can be different from above
            if (blackStart) {
                int lengthBlack  = 1000 * (blackStop->position - blackStart->position)  / decoder->GetVideoFrameRate();
                int distLogoStop = 1000 * (mark->position      - blackStop->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%5d) -> %4dms -> MT_NOBLACKSTART (%5d) -> %4dms -> MT_LOGOSTOP (%5d)", blackStart->position, lengthBlack, blackStop->position, distLogoStop, mark->position);
                // valid example
                // MT_NOBLACKSTOP (136013) ->   40ms -> MT_NOBLACKSTART (136015) -> 1340ms -> MT_LOGOSTOP (136082)   -> ANIXE HD
                // MT_NOBLACKSTOP (151167) -> 2920ms -> MT_NOBLACKSTART (151313) ->  120ms -> MT_LOGOSTOP (151319)   -> SRF zwei
                //
                // invalid example
                // MT_NOBLACKSTOP  (84646) ->  160ms -> MT_NOBLACKSTART  (84650) ->  480ms -> MT_LOGOSTOP (84662)    -> pattern in blackground from preview (RTLZWEI) (conflict)
                if ((lengthBlack >= 40) && (distLogoStop <= 1340)) {
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                    return true;
                }
                dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
            }
        }

        // check sequence: MT_LOGOSTOP -> MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART
        // black screen between logo end mark and start of next broadcast, near (depends on fade out logo) by logo end mark
        cMark *blackStart = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
        if (blackStart) {
            blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
            if (blackStop) {
                cMark *logoStart = marks.GetNext(blackStop->position - 1, MT_LOGOSTART);
                if (logoStart) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - mark->position)       /  decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) /  decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart  = 1000 * (logoStart->position  - blackStop->position)  /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%5d) -> %5dms -> MT_NOBLACKSTOP (%5d) -> %5dms ->  MT_NOBLACKSTART (%5d) -> %6dms -> MT_LOGOSTART (%5d)", mark->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, logoStart->position);
// channel with fade out logo
// valid sequence:
// MT_LOGOSTOP (72210) ->  1400ms -> MT_NOBLACKSTOP (72245) ->   200ms ->  MT_NOBLACKSTART (72250) -> 104320ms -> MT_LOGOSTART (74858) -> TLC
// MT_LOGOSTOP (86133) ->  1320ms -> MT_NOBLACKSTOP (86166) ->  2120ms ->  MT_NOBLACKSTART (86219) ->  16000ms -> MT_LOGOSTART (86619) -> Disney Channel
// MT_LOGOSTOP (72310) ->  4240ms -> MT_NOBLACKSTOP (72416) ->   440ms ->  MT_NOBLACKSTART (72427) ->    800ms -> MT_LOGOSTART (72447) -> Disney Channel
//
// invalid sequence:
// MT_LOGOSTOP (38975) ->  1360ms -> MT_NOBLACKSTOP (39009) ->   120ms ->  MT_NOBLACKSTART (39012) ->   4520ms -> MT_LOGOSTART (39125) -> Nickelodeon, black screen after preview
// MT_LOGOSTOP (39756) ->  1480ms -> MT_NOBLACKSTOP (39793) ->   120ms ->  MT_NOBLACKSTART (39796) ->   4520ms -> MT_LOGOSTART (39909) -> Nickelodeon, black screen after preview
//
                    if ((criteria->LogoFadeInOut() & FADE_OUT) &&
                            (diffLogoStopBlackStart <= 4240) && (diffBlackStartBlackStop >= 200) && (diffBlackStopLogoStart <= 104320)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
// channel without fade out logo
// valid sequence:
// MT_LOGOSTOP (81055) ->     0ms -> MT_NOBLACKSTOP (81055) ->    40ms ->  MT_NOBLACKSTART (81056) ->   1680ms -> MT_LOGOSTART (81098) -> RTL2
// MT_LOGOSTOP (84786) ->   120ms -> MT_NOBLACKSTOP (84789) ->   240ms ->  MT_NOBLACKSTART (84795) ->   1800ms -> MT_LOGOSTART (84840) -> Pro7 MAXX
// MT_LOGOSTOP (86549) ->    80ms -> MT_NOBLACKSTOP (86551) ->   280ms ->  MT_NOBLACKSTART (86558) ->  31800ms -> MT_LOGOSTART (87353) -> Pro7 MAXX (conflict)
// MT_LOGOSTOP (82130) ->    80ms -> MT_NOBLACKSTOP (82132) ->   240ms ->  MT_NOBLACKSTART (82138) ->   1840ms -> MT_LOGOSTART (82184) -> Pro7 MAXX
// MT_LOGOSTOP (78161) ->    80ms -> MT_NOBLACKSTOP (78163) ->   240ms ->  MT_NOBLACKSTART (78169) ->   2800ms -> MT_LOGOSTART (78239) -> Pro7 MAXX
// MT_LOGOSTOP (46818) ->   840ms -> MT_NOBLACKSTOP (46839) ->   160ms ->  MT_NOBLACKSTART (46843) ->   2680ms -> MT_LOGOSTART (46910) -> Comedy Central
//
// invalid sequence:
// MT_LOGOSTOP (81485) ->  4040ms -> MT_NOBLACKSTOP (81586) ->   160ms ->  MT_NOBLACKSTART (81590) ->  95920ms -> MT_LOGOSTART (83988) -> RTLZWEI, sequence in preview
// MT_LOGOSTOP (55728) ->    40ms -> MT_NOBLACKSTOP (55729) ->   120ms ->  MT_NOBLACKSTART (55732) ->   8440ms -> MT_LOGOSTART (55943) -> Comedy Central, sequence in preview
                    if (!(criteria->LogoFadeInOut() & FADE_OUT) &&
                            (diffLogoStopBlackStart <= 840) && (diffBlackStartBlackStop >= 40) && (diffBlackStopLogoStart <= 2800)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
                }
            }

        }

        // check sequence: MT_LOGOSTOP -> MT_NOBLACKSTOP -> MT_LOGOSTART -> MT_NOBLACKSTART  (black screen from short after logo end mark to and short after start of next broadcast)
        blackStart = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
        if (blackStart) {
            cMark *logoStart = marks.GetNext(blackStart->position, MT_LOGOSTART);
            if (logoStart) {
                blackStop = blackMarks.GetNext(logoStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - mark->position)       /  decoder->GetVideoFrameRate();
                    int diffBlackStartLogoStart = 1000 * (logoStart->position  - blackStart->position) /  decoder->GetVideoFrameRate();
                    int diffLogoStartBlackStop  = 1000 * (blackStop->position  - logoStart->position)  /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%5d) -> %4dms -> MT_NOBLACKSTOP (%5d) -> %4dms ->  MT_LOGOSTART (%5d) -> %4dms -> MT_NOBLACKSTART (%5d)", mark->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartLogoStart, logoStart->position, diffLogoStartBlackStop, blackStop->position);
                    // valid sequence
                    // MT_LOGOSTOP (82667) ->   80ms -> MT_NOBLACKSTOP (82669) -> 2080ms ->  MT_LOGOSTART (82721) ->  600ms -> MT_NOBLACKSTART (82736)
                    // MT_LOGOSTOP (81688) ->  120ms -> MT_NOBLACKSTOP (81691) -> 2040ms ->  MT_LOGOSTART (81742) ->  920ms -> MT_NOBLACKSTART (81765)
                    // MT_LOGOSTOP (83499) ->   80ms -> MT_NOBLACKSTOP (83501) -> 2040ms ->  MT_LOGOSTART (83552) -> 1640ms -> MT_NOBLACKSTART (83593)
                    if ((diffLogoStopBlackStart <= 120) && (diffBlackStartLogoStart <= 2080) && (diffLogoStartBlackStop <= 1640)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
                }
            }

        }
        // check sequence: MT_LOGOSTOP -> MT_LOGOSTART -> MT_NOBLACKSTOP -> MT_NOBLACKSTART  (long black screen short after near logo start, opening credids of next braadcast)
        cMark *logoStart = marks.GetNext(mark->position, MT_LOGOSTART);
        if (logoStart) {
            blackStart = blackMarks.GetNext(logoStart->position, MT_NOBLACKSTOP);
            if (blackStart) {
                blackStop = blackMarks.GetNext(logoStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopLogoStart  = 1000 * (logoStart->position - mark->position)       /  decoder->GetVideoFrameRate();
                    int diffLogoStartBlackStart  = 1000 * (blackStart->position - logoStart->position)       /  decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop  = 1000 * (blackStop->position - blackStart->position)       /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%5d) -> %4dms -> MT_LOGOSTART (%5d) -> %4dms ->  MT_NOBLACKSTOP (%5d) -> %4dms -> MT_NOBLACKSTART (%5d)", mark->position, diffLogoStopLogoStart, logoStart->position, diffLogoStartBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position);
                    // valid sequence
                    // MT_LOGOSTOP (178106) -> 7080ms -> MT_LOGOSTART (178283) ->   40ms ->  MT_NOBLACKSTOP (178284) -> 6680ms -> MT_NOBLACKSTART (178451)
                    if ((diffLogoStopLogoStart <= 7080) && (diffLogoStartBlackStart <= 40) && (diffBlackStartBlackStop >= 6680)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
                }
            }
        }
    }
    return false;
}


// special case opening logo sequence from kabel eins, unable to detect info logo change from this channel, too much and too short
bool cMarkAdStandalone::HaveInfoLogoSequence(const cMark *mark) {
    if (!mark) return false;
    // check logo start mark
    if (mark->type == MT_LOGOSTART) {
        cMark *stop1After = marks.GetNext(mark->position, MT_LOGOSTOP);
        if (!stop1After) return false;
        cMark *start2After = marks.GetNext(stop1After->position, MT_LOGOSTART);
        if (!start2After) return false;
        int diffMarkStop1After        = 1000 * (stop1After->position  - mark->position)       / decoder->GetVideoFrameRate();
        int diffStop1AfterStart2After = 1000 * (start2After->position - stop1After->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTART (%5d) -> %4dms -> MT_LOGOSTOP (%5d) -> %4dms -> MT_LOGOSTART (%5d)", mark->position, diffMarkStop1After, stop1After->position, diffStop1AfterStart2After, start2After->position);
        // valid example
        // MT_LOGOSTART ( 5439) -> 5920ms -> MT_LOGOSTOP ( 5587) -> 1120ms -> MT_LOGOSTART ( 5615) -> kabel eins
        // MT_LOGOSTART ( 7913) -> 4480ms -> MT_LOGOSTOP ( 8025) -> 2200ms -> MT_LOGOSTART ( 8080) -> kabel eins
        // MT_LOGOSTART ( 8298) -> 7760ms -> MT_LOGOSTOP ( 8492) -> 1080ms -> MT_LOGOSTART ( 8519)
        //
        // invald example
        // MT_LOGOSTART ( 3833) ->  480ms -> MT_LOGOSTOP ( 3845) ->  480ms  -> MT_LOGOSTART ( 3857) -> DMAX
        if ((diffMarkStop1After >= 4480) && (diffMarkStop1After <= 7760) &&
                (diffStop1AfterStart2After >= 1080) && (diffStop1AfterStart2After <= 2200)) {
            dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): found opening info logo sequence");
            return true;
        }
        else dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): logo start mark (%d): opening info logo sequence is invalid", mark->position);
    }
    // check logo stop mark
    if (mark->type == MT_LOGOSTOP) {
        cMark *start1Before = marks.GetPrev(mark->position, MT_LOGOSTART);
        if (!start1Before) return false;
        cMark *stop1Before = marks.GetPrev(start1Before->position, MT_LOGOSTOP);
        if (!stop1Before) return false;
        cMark *start2Before = marks.GetPrev(stop1Before->position, MT_LOGOSTART);
        if (!start2Before) return false;
        cMark *stop2Before = marks.GetPrev(start2Before->position, MT_LOGOSTOP);
        if (!stop2Before) return false;
        int diffStart1Mark  = 1000 * (mark->position         - start1Before->position) / decoder->GetVideoFrameRate();
        int diffStop1Start1 = 1000 * (start1Before->position - stop1Before->position)  / decoder->GetVideoFrameRate();
        int diffStart2Stop1 = 1000 * (stop1Before->position  - start2Before->position) / decoder->GetVideoFrameRate();
        int diffStop2Start2 = 1000 * (start2Before->position - stop2Before->position)  / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTOP (%5d) -> %dms -> MT_LOGOSTART (%5d) -> %dms -> MT_LOGOSTOP (%5d) -> %dms -> MT_LOGOSTART (%5d) -> %dms -> MT_LOGOSTOP (%5d)", stop2Before->position, diffStop2Start2, start2Before->position, diffStart2Stop1, stop1Before->position, diffStop1Start1, start1Before->position, diffStart1Mark, mark->position);
// valid examples
// MT_LOGOSTOP (185315) -> 1080ms -> MT_LOGOSTART (185342) -> 8160ms -> MT_LOGOSTOP (185546) ->  840ms -> MT_LOGOSTART (185567) -> 18880ms -> MT_LOGOSTOP (186039)
// MT_LOGOSTOP (128417) -> 1120ms -> MT_LOGOSTART (128445) -> 7840ms -> MT_LOGOSTOP (128641) -> 1160ms -> MT_LOGOSTART (128670) ->  7840ms -> MT_LOGOSTOP (128866)
//
// invalid example
// MT_LOGOSTART ( 3833) -> 480ms -> MT_LOGOSTOP ( 3845) -> 480ms -> MT_LOGOSTART ( 3857)
        if ((diffStop2Start2 >=  1080) && (diffStop2Start2 <=  1120)   &&  // change from logo to closing logo
                (diffStart2Stop1 >= 7840) && (diffStart2Stop1 <= 8160) &&  // closing logo deteted as logo
                (diffStop1Start1 >=  840) && (diffStop1Start1 <= 1160) &&  // change from closing logo to logo
                (diffStart1Mark  >= 7840) && (diffStart1Mark  <= 18880)) { // end part between closing logo and broadcast end
            dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): found closing info logo sequence");
            return true;
        }
        else dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): logo stop mark (%d): closing logo info sequence is invalid", mark->position);
    }
    return false;
}


void cMarkAdStandalone::CheckStop() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): start check stop (%d)", decoder->GetFrameNumber());

    char *indexToHMSF = marks.IndexToHMSF(stopA, false);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("assumed stop position (%d) at %s", stopA, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    DebugMarks();     //  only for debugging

    // cleanup invalid marks
    //
    // cleanup very short logo stop/start after aspect ratio start, they are from a fading in logo
    cMark *aStart = marks.GetNext(0, MT_ASPECTSTART);
    while (true) {
        if (!aStart) break;
        cMark *lStop = marks.GetNext(aStart->position, MT_LOGOSTOP);
        if (lStop) {
            cMark *lStart = marks.GetNext(lStop->position, MT_LOGOSTART);
            if (lStart) {
                int diffStop  = 1000 * (lStop->position  - aStart->position) / decoder->GetVideoFrameRate();
                int diffStart = 1000 * (lStart->position - aStart->position) / decoder->GetVideoFrameRate();
                if ((diffStop < 1000) && (diffStart < 1000)) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) and logo start mark (%d) very near after aspect ratio start mark (%d), this is a fading in logo, delete marks", aStart->position, lStop->position, lStart->position);
                    marks.Del(lStop->position);
                    marks.Del(lStart->position);
                }
            }
        }
        aStart = marks.GetNext(aStart->position, MT_ASPECTSTART);
    }
    // remove logo change marks
    if (criteria->GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_UNKNOWN) RemoveLogoChangeMarks(false);

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): marks after first cleanup:");
    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): start end mark selection");

// try MT_CHANNELSTOP
    cMark *end = nullptr;
    if (criteria->GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_UNKNOWN) end = Check_CHANNELSTOP();

// try MT_ASPECTSTOP
    if (!end) {
        // if we have 16:9 broadcast, every aspect stop without aspect start before is end mark, maybe it is very early in case of wrong recording length
        if ((macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
            cMark *aspectStop = marks.GetNext(0, MT_ASPECTSTOP);
            if (aspectStop) {
                const cMark *aspectStart = marks.GetPrev(aspectStop->position, MT_ASPECTSTOP);
                if (!aspectStart) {
                    dsyslog("cMarkAdStandalone::CheckStop(): we have 16:9 bradcast and MT_ASPECTSTOP at frame (%d) without MT_ASPECTSTART before, this is a possible end mark", aspectStop->position);
                    end = aspectStop;
                }
            }
        }
        if (!end) end = marks.GetAround(400 * (decoder->GetVideoFrameRate()), stopA, MT_ASPECTSTOP);      // changed from 360 to 400
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
                        int diffAspectStop = (end->position - stopBefore->position) / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d), %ds before aspect ratio stop mark", stopBefore->position, diffAspectStop);
                        if (diffAspectStop > 234) {  // check only for early logo stop marks, do not increase, there can be a late advertising and aspect stop on same frame as logo stop
                            // changed from 153 to 234
                            cMark *startLogoBefore = marks.GetPrev(end->position, MT_LOGOSTART);
                            if (startLogoBefore && (startLogoBefore->position > stopBefore->position)) {
                                dsyslog("cMarkAdStandalone::CheckStop(): logo start mark (%d) between logo stop mark (%d) and aspect ratio mark (%d), this logo stop mark is end of advertising", startLogoBefore->position, stopBefore->position, end->position);
                                stopBefore = nullptr;
                            }
                        }
                    }
                }
                // now we may have a hborder or a logo stop mark before aspect stop mark, check if valid
                if (stopBefore) { // maybe real stop mark was deleted because on same frame as logo/hborder stop mark
                    int diffStopA      = (stopA - stopBefore->position)        /  decoder->GetVideoFrameRate();
                    int diffAspectStop = (end->position - stopBefore->position) / decoder->GetVideoFrameRate();
                    char *markType = marks.TypeToText(stopBefore->type);
                    dsyslog("cMarkAdStandalone::CheckStop(): found %s stop mark (%d) %ds before aspect ratio end mark (%d), %ds before assumed stop", markType, stopBefore->position, diffAspectStop, end->position, diffStopA);
                    FREE(strlen(markType)+1, "text");
                    free(markType);
                    if ((diffStopA <= 867) && (diffAspectStop <= 66)) { // changed from 760 to 867, for broadcast length from info file too long
                        // changed from 39 to 40 to 66, for longer ad found between broadcasts
                        dsyslog("cMarkAdStandalone::CheckStop(): there is an advertising before aspect ratio change, use stop mark (%d) before as end mark", stopBefore->position);
                        end = stopBefore;
                        // cleanup possible info logo or logo detection failure short before end mark
                        if (end->type == MT_LOGOSTOP) marks.DelFromTo(end->position - (60 * decoder->GetVideoFrameRate()), end->position - 1, MT_LOGOCHANGE, 0xF0);
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_ASPECTSTOP mark found");
    }

// try MT_HBORDERSTOP
    // try hborder end if hborder used even if we got another end mark, maybe we found a better one, but not if we have a hborder end mark
    if ((!end && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) >= CRITERIA_UNKNOWN)) ||
            (end && (end->type != MT_HBORDERSTOP) && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED))) {
        cMark *hBorder = Check_HBORDERSTOP();
        if (hBorder) end = hBorder;  // do not override an existing end mark with nullptr
    }
    // cleanup all marks after hborder start from next broadcast
    if (!end && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
        cMark *hBorderStart = marks.GetNext(stopA - (60 * decoder->GetVideoFrameRate()), MT_HBORDERSTART);
        if (hBorderStart) {
            // use logo stop mark short after hborder as end mark
            cMark *logoStop = marks.GetNext(hBorderStart->position, MT_LOGOSTOP);
            if (logoStop) {
                int diff = (logoStop->position - hBorderStart->position) / decoder->GetVideoFrameRate();
                if (diff <= 2) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) %ds after hborder start mark (%d), use it as end mark", logoStop->position, diff, hBorderStart->position);
                    marks.Del(hBorderStart->position);
                    end = logoStop;
                }
            }
            else {
                dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after hborder start (%d) from next broadcast", hBorderStart->position);
                marks.DelTill(hBorderStart->position, false);
            }
        }
    }

// try MT_VBORDERSTOP
    if (!end && (criteria->GetMarkTypeState(MT_VBORDERCHANGE) >= CRITERIA_UNKNOWN)) end = Check_VBORDERSTOP();

// try MT_LOGOSTOP
    if (!end && (criteria->GetMarkTypeState(MT_LOGOCHANGE) >= CRITERIA_UNKNOWN)) end = Check_LOGOSTOP();
    // detect very short channel start before, this is start from next broadcast
    if (end && (criteria->GetMarkTypeState(MT_CHANNELCHANGE) < CRITERIA_USED)) {
        cMark *prevChannelStart = marks.GetPrev(end->position, MT_CHANNELSTART);
        if (prevChannelStart) {
            int deltaChannelStart = 1000 * (end->position - prevChannelStart->position) / decoder->GetVideoFrameRate();
            if (deltaChannelStart <= 1000) {
                dsyslog("cMarkAdStandalone::CheckStop(): channel start mark (%d) %dms before logo end mark (%d) is start of next broadcast, delete mark", prevChannelStart->position, deltaChannelStart, end->position);
                marks.Del(prevChannelStart->position);
            }
        }
    }

// no end mark found, try if we can use a start mark of next bradcast as end mark
    // no valid stop mark found, try if there is a MT_CHANNELSTART from next broadcast
    if (!end && (criteria->GetMarkTypeState(MT_CHANNELCHANGE) != CRITERIA_USED)) {
        // not possible is we use channel mark in this broadcast
        cMark *channelStart = marks.GetNext(stopA, MT_CHANNELSTART);
        if (channelStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): use channel start mark (%d) from next broadcast as end mark", channelStart->position);
            marks.ChangeType(channelStart, MT_STOP);
            end = channelStart;
        }
    }
    // try to get hborder start mark from next broadcast as stop mark
    if (!end && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) != CRITERIA_USED)) {
        cMark *hBorderStart = marks.GetNext((stopA - (240 *  decoder->GetVideoFrameRate())), MT_HBORDERSTART);  // accept max 4 min before stopA
        if (hBorderStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): found hborder start mark (%d) from next broadcast at end of recording", hBorderStart->position);
            cMark *prevMark = marks.GetPrev(hBorderStart->position, MT_ALL);
            if ((prevMark->type & 0x0F) == MT_START) {
                dsyslog("cMarkAdStandalone::CheckStop(): start mark (%d) before found, use hborder start mark (%d) from next broadcast as end mark", prevMark->position, hBorderStart->position);
                if (criteria->GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_USED) {  // if we use logo marks, there must be a valid logo stop mark before hborder start
                    cMark *lStop = marks.GetPrev(hBorderStart->position, MT_LOGOSTOP);  // try to find a early logo stop, maybe too long broadcast from info fileA
                    if (lStop) {
                        int diffAssumed = (stopA - lStop->position) / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop (%d) %ds before hborder start (%d)", lStop->position, diffAssumed, hBorderStart->position);
                        if (diffAssumed <= 251) end = lStop;
                    }
                }
                if (!end) {
                    end = marks.ChangeType(hBorderStart, MT_STOP);
                    if (end) end = marks.Move(end, end->position - 1, MT_TYPECHANGESTOP);  // one frame before hborder start is end mark
                }
            }
            else {
                dsyslog("cMarkAdStandalone::CheckStop(): use stop mark (%d) before hborder start mark (%d) ", prevMark->position, hBorderStart->position);
                end = prevMark;
            }
        }
    }

    if (end) {
        indexToHMSF = marks.GetTime(end);
        char *markType = marks.TypeToText(end->type);
        if (indexToHMSF && markType) {
            isyslog("using %s stop mark on position (%d) at %s as end mark", markType, end->position, indexToHMSF);
            FREE(strlen(markType)+1, "text");
            free(markType);
        }
    }
    else {  // no end mark found at all, set end mark to assumed end
        dsyslog("cMarkAdStandalone::CheckStop(): no stop mark found, add end mark at assumed end (%d)", stopA);
        cMark *markBefore = marks.GetPrev(stopA, MT_ALL);
        if (markBefore && ((markBefore->type & 0x0F) == MT_STOP)) {
            dsyslog("cMarkAdStandalone::CheckStop(): mark before (%d) assumed stop (%d) is a stop mark, use this as end mark", markBefore->position, stopA);
            end = markBefore;
        }
        else {
            int lastFrame = index->GetLastFrame();
            int stopPos   = stopA;
            if (stopPos > lastFrame) {
                dsyslog("cMarkAdStandalone::CheckStop(): assumed stop (%d) after recording end (%d), use recording end", stopA, lastFrame);
                stopPos = lastFrame;
            }
            sMarkAdMark mark = {};
            mark.position = stopPos;  // we are lost, add a end mark at assumed end
            mark.type     = MT_ASSUMEDSTOP;
            AddMark(&mark);
            end = marks.Get(stopPos);
        }
    }


    // delete all marks after end mark
    if (end) { // be save, if something went wrong end = nullptr
        dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after final stop mark at (%d)", end->position);
        cMark *startBefore = marks.GetPrev(end->position, MT_START, 0x0F);
        if (!startBefore) {
            esyslog("cMarkAdStandalone::CheckStop(): invalid marks, no start mark before end mark");
            sMarkAdMark mark = {};
            mark.position = 0;
            mark.type     = MT_RECORDINGSTART;
            AddMark(&mark);
        }
        marks.DelTill(end->position, false);
    }
    else esyslog("could not find a end mark");

    // cleanup detection failures (e.g. very long dark scenes), keep start end end mark, they can be from different type
    if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelFromTo(marks.First()->position + 1, end->position - 1, MT_HBORDERCHANGE, 0xF0);
    if (criteria->GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelFromTo(marks.First()->position + 1, end->position - 1, MT_VBORDERCHANGE, 0xF0);

    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): end check stop");
    doneCheckStop = true;
    LogSeparator();
    return;
}


// check if last stop mark is start of closing credits without logo or hborder
// move stop mark to end of closing credit
// <stopMark> last logo or hborder stop mark
// return: true if closing credits was found and last logo stop mark position was changed
//
bool cMarkAdStandalone::MoveLastStopAfterClosingCredits(cMark *stopMark) {
    if (!stopMark) return false;
    if (criteria->GetClosingCreditsState(stopMark->position) < CRITERIA_UNKNOWN) return false;

    dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): check closing credits in frame without logo after position (%d)", stopMark->position);

    // init objects for logo mark optimization
    if (!detectLogoStopStart) {  // init in RemoveLogoChangeMarks(), but maybe not used
        detectLogoStopStart = new cDetectLogoStopStart(decoder, index, criteria, evaluateLogoStopStartPair, video->GetLogoCorner());
        ALLOC(sizeof(*detectLogoStopStart), "detectLogoStopStart");
    }
    // check current read position of decoder
    if (stopMark->position < decoder->GetFrameNumber()) decoder->Restart();

    int endPos = stopMark->position + (25 * decoder->GetVideoFrameRate());  // try till 25s after stopMarkPosition
    int newPosition = -1;
    if (detectLogoStopStart->Detect(stopMark->position, endPos)) {
        newPosition = detectLogoStopStart->ClosingCredit(stopMark->position, endPos);
    }

    if (newPosition > stopMark->position) {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): closing credits found, move stop mark to position (%d)", newPosition);
        marks.Move(stopMark, newPosition, MT_CLOSINGCREDITSSTOP);
        return true;
    }
    else {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): no closing credits found");
        return false;
    }
}


// remove logo stop/start pairs from logo changes / info logo / introduction logo
// have to be done before end mark selection to prevent to select wrong end mark
//
void cMarkAdStandalone::RemoveLogoChangeMarks(const bool checkStart) {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): start detect and remove logo stop/start marks from special logo");

    // only if there are at last one corect logo stop/start and one logo/stop/start from special logo
    if (marks.Count(MT_LOGOCHANGE, 0xF0) < 4) {
        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): to less logo marks");
        return;
    }
    // check if this channel has special logos, for performance reason only known and tested channels
    if (!criteria->IsInfoLogoChannel() && !criteria->IsLogoChangeChannel() && !criteria->IsClosingCreditsChannel()) {
        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): channel not in list for special logo");
        return;
    }

    // do not messup decoder read position if called by CheckStart(), use new instance for detection
    // use local variables with same name as global
    cDecoder *decoder_local = decoder;
    if (checkStart) {
        decoder_local = new cDecoder(macontext.Config->recDir, macontext.Config->threads, macontext.Config->fullDecode, macontext.Config->hwaccel, macontext.Config->forceHW,  macontext.Config->forceInterlaced, index);
        ALLOC(sizeof(*decoder_local), "decoder_local");
        if (!decoder_local->ReadNextFile()) { // force init decoder to get infos about video (frame rate is used by cEvaluateLogoStopStartPair)
            esyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): failed to open first video file");
            return;
        }
        if (detectLogoStopStart) {
            esyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): object detectLogoStopStart should not exists");
            return;
        }
        if (evaluateLogoStopStartPair) {
            esyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): object evaluateLogoStopStartPair should not exists");
            return;
        }
    }
    else decoder_local->Restart();  // we are called from CheckStop(), decoder read position is at end of recording

    // check if objects exists, otherwise create new with global variables
    if (!detectLogoStopStart) {
        detectLogoStopStart = new cDetectLogoStopStart(decoder_local, index, criteria, evaluateLogoStopStartPair, video->GetLogoCorner());
        ALLOC(sizeof(*detectLogoStopStart), "detectLogoStopStart");
    }

    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder_local, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }

    evaluateLogoStopStartPair->CheckLogoStopStartPairs(&marks, &blackMarks, startA, frameCheckStart, stopA);

    char *indexToHMSFStop      = nullptr;
    char *indexToHMSFStart     = nullptr;
    int stopPosition           = 0;
    int startPosition          = 0;
    int isLogoChange           = STATUS_UNKNOWN;
    int isInfoLogo             = STATUS_UNKNOWN;
    int isStartMarkInBroadcast = STATUS_UNKNOWN;

    // loop through all logo stop/start pairs
    int endRange = 0;  // if we are called by CheckStart, get all pairs to detect at least closing credits
    if (startA == 0) endRange = stopA - (27 * decoder->GetVideoFrameRate()); // if we are called by CheckStop, get all pairs after this frame to detect at least closing credits
    // changed from 26 to 27
    while (evaluateLogoStopStartPair->GetNextPair(&stopPosition, &startPosition, &isLogoChange, &isInfoLogo, &isStartMarkInBroadcast, endRange)) {
        if (abortNow) return;
        if (!marks.Get(startPosition) || !marks.Get(stopPosition)) continue;  // at least one of the mark from pair was deleted, nothing to do

        if (decoder->GetPacketNumber() >= stopPosition) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): overlapping pairs from info logo merge, skip pair logo stop (%d) start (%d)", stopPosition, startPosition);
            continue;
        }

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
        indexToHMSFStop = marks.IndexToHMSF(stopPosition, false);
        if (indexToHMSFStop) {
            ALLOC(strlen(indexToHMSFStop)+1, "indexToHMSF");
        }

        indexToHMSFStart = marks.IndexToHMSF(startPosition, false);
        if (indexToHMSFStart) {
            ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF");
        }

        if (indexToHMSFStop && indexToHMSFStart) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%d) at %s and logo start (%d) at %s, isInfoLogo %d", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart, isInfoLogo);
        }
        if (detectLogoStopStart->Detect(stopPosition, startPosition)) {
            bool doInfoCheck = true;
            // check for closing credits if no other checks will be done, only part of the loop elements in recording end range
            if ((isInfoLogo <= STATUS_NO) && (isLogoChange <= STATUS_NO)) detectLogoStopStart->ClosingCredit(stopPosition, startPosition);

            // check for info logo if  we are called by CheckStart and we are in broadcast
            if ((startA > 0) && criteria->IsIntroductionLogoChannel() && (isStartMarkInBroadcast == STATUS_YES)) {
                // do not delete info logo, it can be introduction logo, it looks the same
                // expect we have another start very short before
                cMark *lStartBefore = marks.GetPrev(stopPosition, MT_LOGOSTART);
                if (lStartBefore) {
                    int diffStart = 1000 * (stopPosition - lStartBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): logo start (%d) %dms before stop mark (%d)", lStartBefore->position, diffStart, stopPosition);
                    if (diffStart > 1240) {  // do info logo check if we have a logo start mark short before, some channel send a early info log after broadcast start
                        // changed from 1160 to 1240
                        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): do not check for info logo, we are in start range, it can be introducion logo");
                        doInfoCheck = false;
                    }
                }
            }
            if (doInfoCheck && (isInfoLogo >= STATUS_UNKNOWN) && detectLogoStopStart->IsInfoLogo(stopPosition, startPosition)) {
                // found info logo part
                if (indexToHMSFStop && indexToHMSFStart) {
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): info logo found between frame (%i) at %s and (%i) at %s, deleting marks between this positions", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
                }
                evaluateLogoStopStartPair->SetIsInfoLogo(stopPosition, startPosition);
                marks.DelFromTo(stopPosition, startPosition, MT_LOGOCHANGE, 0xF0);  // maybe there a false start/stop inbetween
            }
            // check logo change
            if ((isLogoChange >= STATUS_UNKNOWN) && detectLogoStopStart->IsLogoChange(stopPosition, startPosition)) {
                if (indexToHMSFStop && indexToHMSFStart) {
                    isyslog("logo change between frame (%6d) at %s and (%6d) at %s, deleting marks between this positions", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
                }
                marks.DelFromTo(stopPosition, startPosition, MT_LOGOCHANGE, 0xF0);  // maybe there a false start/stop inbetween
            }
        }
    }

    // delete buffer and objects
    if (indexToHMSFStop) {
        FREE(strlen(indexToHMSFStop)+1, "indexToHMSF");
        free(indexToHMSFStop);
    }
    if (indexToHMSFStart) {
        FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
        free(indexToHMSFStart);
    }

    // delete only one time used object from CheckStart() call
    if (checkStart) {
        FREE(sizeof(*decoder_local), "decoder_local");
        delete decoder_local;
        decoder_local = nullptr;
        FREE(sizeof(*detectLogoStopStart), "detectLogoStopStart");
        delete detectLogoStopStart;
        detectLogoStopStart = nullptr;
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
        evaluateLogoStopStartPair = nullptr;
    }

    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): marks after detect and remove logo stop/start mark pairs with special logo");
    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): end detect and remove logo stop/start mark pairs with special logo");
    LogSeparator();
}


// fix aspect info, invert MT_ASPECTSTART and MT_ASPECTSTOP and fix position
void cMarkAdStandalone::SwapAspectRatio() {
    dsyslog("cMarkAdStandalone::SwapAspectRatio(); aspect ratio from VDR info file was wrong, swap aspect ratio marks");
    if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) {
        macontext.Info.AspectRatio.num = 16;
        macontext.Info.AspectRatio.den =  9;
        marks.DelType(MT_ASPECTCHANGE, 0xF0); // aspect marks with 16:9 videos are invalid
    }
    else {
        macontext.Info.AspectRatio.num = 4;
        macontext.Info.AspectRatio.den = 3;
    }

    cMark *aMark = marks.GetFirst();
    while (aMark) {
        if (aMark->type == MT_ASPECTSTART) {
            aMark->type = MT_ASPECTSTOP;
            if (macontext.Config->fullDecode) {
                if (decoder->IsInterlacedFrame()) aMark->position = aMark->position - 2;
                else                              aMark->position = aMark->position - 1;
            }
            else aMark->position = index->GetIFrameBefore(aMark->position - 1);
        }
        else {
            if (aMark->type == MT_ASPECTSTOP) {
                aMark->type = MT_ASPECTSTART;
                if (macontext.Config->fullDecode) {
                    if (decoder->IsInterlacedFrame()) aMark->position = aMark->position + 3;  // one full picture forward and the îget the next half picture for full decode
                    else                              aMark->position = aMark->position + 1;
                }
                else aMark->position = index->GetIFrameAfter(aMark->position + 1);
            }
        }
        aMark = aMark->Next();
    }
    video->SetAspectRatioBroadcast(macontext.Info.AspectRatio);
    dsyslog("cMarkAdStandalone::SwapAspectRatio(): new aspect ratio %d:%d, fixed marks are:", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    DebugMarks();     //  only for debugging
}


cMark *cMarkAdStandalone::Check_CHANNELSTART() {
    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): search for channel start mark");

    // delete very early first mark, if channels send ad with 6 channels, this can be wrong
    cMark *channelStart = marks.GetNext(-1, MT_CHANNELSTART);
    if (channelStart) {
        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE);  // there is a 6 channel audio in broadcast, may we can use it later
        if (channelStart->position < IGNORE_AT_START) marks.Del(channelStart->position);
    }
    // 6 channel double episode, there is no channel start mark
    if (decoder->GetAC3ChannelCount() >= 5) criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE);  // there is a 6 channel audio in broadcast, may we can use it later

    // search channel start mark
    channelStart = marks.GetAround(MAX_ASSUMED * decoder->GetVideoFrameRate(), startA, MT_CHANNELSTART);
    // check audio streams
    if (channelStart) {
        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): channels start at (%d)", channelStart->position);
        // we have a channel change, cleanup border and aspect ratio
        video->ClearBorder();
        marks.DelType(MT_ASPECTCHANGE, 0xF0);

        int diffAssumed = (channelStart->position - startA) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): audio channel start mark found at (%d) %ds after assumed start", channelStart->position, diffAssumed);
        if (channelStart->position > stopA) {  // this could be a very short recording, 6 channel is in post recording
            dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): audio channel start mark after assumed stop mark not valid");
            return nullptr;
        }

        // for early channel start mark, check if there is a logo start mark stop/start pair near assumed start
        // this can happen if previous broadcast has also 6 channel
        if (diffAssumed <= -121) {
            cMark *logoStop = marks.GetNext(channelStart->position, MT_LOGOSTOP);
            if (logoStop) {  // if channel start is from previous recording, we should have a logo stop mark near assumed start
                cMark *logoStart = marks.GetNext(logoStop->position, MT_LOGOSTART);
                if (logoStart) {
                    int diffLogoStart = (logoStart->position - startA) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): found logo start mark (%d) %ds after assumed start", logoStart->position, diffLogoStart);
                    if ((diffLogoStart >= -1) && (diffLogoStart <= 56)) {  // changed from 17 to 56
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): use logo start mark (%d) as start mark", logoStart->position);
                        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
                        return logoStart;
                    }
                }
            }
        }
        // now we have a final channel start mark
        marks.DelType(MT_LOGOCHANGE,    0xF0);
        marks.DelType(MT_HBORDERCHANGE, 0xF0);
        marks.DelType(MT_VBORDERCHANGE, 0xF0);
        marks.DelWeakFromTo(0, INT_MAX, MT_CHANNELCHANGE); // we have a channel start mark, delete all weak marks
        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
        return channelStart;
    }
    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): no audio channel start mark found");
    return nullptr;
}



cMark *cMarkAdStandalone::Check_LOGOSTART() {
    cMark *begin = nullptr;

    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): search for logo start mark");

    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }

    // cleanup invalid logo start marks
    cMark *lStart = marks.GetFirst();
    while (lStart) {
        if (lStart->type == MT_LOGOSTART) {
            bool delMark = false;
            // remove very early logo start marks, this can be delayed logo start detection
            int diff = lStart->position / decoder->GetVideoFrameRate();
            if (diff <= 10) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start (%5d) %ds after recording start too early", lStart->position, diff);
                delMark = true;
            }
            else {
                // remove very short logo start/stop pairs, this, is a false positive logo detection
                cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);
                if (lStop) {
                    diff = 1000 * (lStop->position - lStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start (%5d) logo stop (%5d), distance %dms", lStart->position, lStop->position, diff);
                    if (diff <= 60) {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): distance too short, deleting marks");
                        delMark = true;
                    }
                }
            }
            if (delMark) {
                cMark *tmpMark = lStart->Next();
                marks.Del(lStart->position);
                lStart = tmpMark;
            }
        }
        if (lStart) lStart = lStart->Next();
    }

    // search for logo start mark around assumed start
    cMark *lStartAssumed = marks.GetAround(startA + (MAX_ASSUMED * decoder->GetVideoFrameRate()), startA, MT_LOGOSTART);
    if (!lStartAssumed) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): no logo start mark found");
        return nullptr;
    }

    // try to select best logo start mark based on closing credits follow
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check for logo start mark based on closing credits from previous broadcast");
    // prevent to detect ad in frame from previous broadcast as closing credits
    if (!criteria->IsAdInFrameWithLogoChannel() &&
            criteria->IsClosingCreditsChannel()) {
        // search from nearest logo start mark to end, first mark can be before startA
        lStart = lStartAssumed;
        while (!begin && lStart) {
            LogSeparator(false);
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check logo start mark (%d) based on closing credits after assumed start", lStart->position);
            int status = evaluateLogoStopStartPair->GetIsClosingCreditsBefore(lStart->position);
            int diffAssumed = (lStart->position - startA) / decoder->GetVideoFrameRate();
            if ((diffAssumed < -MAX_ASSUMED) || (diffAssumed > MAX_ASSUMED)) break;
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d): %ds after assumed start (%d), closing credits status %d", lStart->position, diffAssumed, startA, status);
            if (status == STATUS_YES) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) has closing credits in frame before, valid start mark found", lStart->position);
                begin = lStart;
                break;
            }
            lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
        }
        // search from nearest logo start mark to recording start
        lStart = lStartAssumed;
        while (!begin) {
            lStart = marks.GetPrev(lStart->position, MT_LOGOSTART);
            if (!lStart) break;
            LogSeparator(false);
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check logo start mark (%d) based on closing credits before assumed start", lStart->position);
            int status = evaluateLogoStopStartPair->GetIsClosingCreditsBefore(lStart->position);
            int diffAssumed = (startA - lStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d): %ds before assumed start (%d), closing credits status %d", lStart->position, diffAssumed, startA, status);
            if (diffAssumed > MAX_ASSUMED) break;
            if (status == STATUS_YES) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) has closing credits in frame before, valid start mark found", lStart->position);
                begin = lStart;
            }
        }
    }

    // try to select best logo start mark based on lower border
    lStart = lStartAssumed;
    while (!begin) { // search from nearest logo start mark to end, first mark can be before startA
        if (HaveLowerBorder(lStart)) {
            begin = lStart;
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) has lower border before, valid start mark found", lStart->position);
            break;
        }
        lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
        if (!lStart) break;
        int diffAssumed = (lStart->position - startA) / decoder->GetVideoFrameRate();
        if ((diffAssumed < -MAX_ASSUMED) || (diffAssumed > MAX_ASSUMED)) break;
    }
    lStart = lStartAssumed;
    while (!begin) {  // search from nearest logo start mark to recording start
        lStart = marks.GetPrev(lStart->position, MT_LOGOSTART);
        if (!lStart) break;
        int diffAssumed = (lStart->position - startA) / decoder->GetVideoFrameRate();
        if ((diffAssumed < -MAX_ASSUMED) || (diffAssumed > MAX_ASSUMED)) break;
        if (HaveLowerBorder(lStart)) {
            begin = lStart;
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) has lower border before, valid start mark found", lStart->position);
        }
    }

    // try to select best logo start mark based on black screen, silence or info logo sequence
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check for logo start mark with black screen or silence separator");
    lStart = lStartAssumed;
    while (!begin && lStart) {
        int diffAssumed = (lStart->position - startA) / decoder->GetVideoFrameRate();
        LogSeparator();
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check logo start mark (%d), %ds after assumed start", lStart->position, diffAssumed);
        if (diffAssumed > MAX_ASSUMED) {
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) too late for valid broadcast start", lStart->position);
            break;
        }
        if (HaveBlackSeparator(lStart) || HaveSilenceSeparator(lStart) || HaveInfoLogoSequence(lStart)) {
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark has separator, start mark (%d) is valid", lStart->position);
            begin = lStart;
            break;
        }
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) has no separator", lStart->position);
        lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
    }
    // search from nearest logo start mark to recording start
    lStart = lStartAssumed;
    while (!begin) {
        lStart = marks.GetPrev(lStart->position, MT_LOGOSTART);
        if (!lStart) break;
        int diffAssumed = (startA - lStart->position) / decoder->GetVideoFrameRate();
        if (diffAssumed > MAX_ASSUMED) break;
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check logo start mark (%d) %ds before assumed start", lStart->position, diffAssumed);
        if (HaveBlackSeparator(lStart) || HaveSilenceSeparator(lStart) || HaveInfoLogoSequence(lStart)) {
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark has separator, start mark (%d) is valid", lStart->position);
            begin = lStart;
            break;
        }
    }

    if (begin) CleanupUndetectedInfoLogo(begin);  // strong broadcast start found, cleanup undetected info logos, introduction logos short after final start mark
    LogSeparator();


    lStart = lStartAssumed;
    while (!begin && lStart) {
        // check for too early, can be start of last part from previous broadcast
        int diffAssumed = (startA - lStart->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d), %ds before assumed start (%d)", lStart->position, diffAssumed, startA);
        if (diffAssumed >= MAX_ASSUMED) { // do not accept start mark if it is more than 5 min before assumed start
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds before assumed start too early", lStart->position, diffAssumed);
            cMark *lNext = marks.GetNext(lStart->position, MT_LOGOSTART);  // get next logo start mark
            marks.Del(lStart);
            lStart = lNext;
            continue;
        }
        // check for too late logo start, can be of first ad
        if (diffAssumed < -MAX_ASSUMED) {  // do not accept start mark if it is more than 5 min after assumed start
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds after assumed start too late", lStart->position, -diffAssumed);
            break;
        }
        else {
            begin = lStart;  // start with nearest start mark to assumed start
        }

        // check next logo stop/start pair
        cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);  // get next logo stop mark
        if (lStop) {  // there is a next stop mark in the start range
            int distanceStartStop = (lStop->position - lStart->position) / decoder->GetVideoFrameRate();
            if (distanceStartStop < 17) {  // change from 20 to 17 because of very short stop/start from logo change 17s after start mark
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): next logo stop mark found very short after start mark on position (%d), distance %ds", lStop->position, distanceStartStop);
                cMark *lNextStart = marks.GetNext(lStop->position, MT_LOGOSTART); // get next logo start mark
                if (lNextStart) {  // now we have logo start/stop/start, this can be a preview before broadcast start
                    int distanceStopNextStart = (lNextStart->position - lStop->position) / decoder->GetVideoFrameRate();
                    if (distanceStopNextStart <= 136) { // found start mark short after start/stop, use this as start mark, changed from 68 to 76 to 136
                        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): found start mark (%d) %ds after logo start/stop marks, use this start mark", lNextStart->position, distanceStopNextStart);
                        begin = lNextStart;
                        break;
                    }
                    else {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): found start mark (%d) %ds after logo start/stop marks, length too big", lNextStart->position, distanceStopNextStart);
                        break;
                    }
                }
                else break;
            }
            else {  // there is a next stop mark but too far away
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): next logo stop mark (%d) but too far away %ds", lStop->position, distanceStartStop);
                break;
            }
        }
        else break; // there is no next stop mark
        lStart = marks.GetNext(lStart->position, MT_LOGOSTART);  // try next start mark
    }

    if (!begin) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): no logo start mark found");
        return nullptr;
    }

    // valid logo start mark found
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): found logo start mark (%d)", begin->position);
    marks.DelWeakFromTo(0, INT_MAX, MT_LOGOCHANGE);   // maybe the is a assumed start from converted channel stop
    if ((criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) ||
            (criteria->GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_USED) ||
            (criteria->GetMarkTypeState(MT_ASPECTCHANGE)  == CRITERIA_USED) ||
            (criteria->GetMarkTypeState(MT_CHANNELCHANGE) == CRITERIA_USED)) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): stronger marks are set for detection, use logo mark only for start mark, delete logo marks after (%d)", begin->position);
        marks.DelFromTo(begin->position + 1, INT_MAX, MT_LOGOCHANGE, 0xF0);
    }
    else {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo marks set for detection, cleanup late hborder and vborder stop marks from previous broadcast");
        const cMark *delMark = marks.GetAround(10 * decoder->GetVideoFrameRate(), begin->position, MT_VBORDERSTOP, 0xFF);
        if (delMark) marks.Del(delMark->position);
        delMark = marks.GetAround(10 * decoder->GetVideoFrameRate(), begin->position, MT_HBORDERSTOP, 0xFF);
        if (delMark) marks.Del(delMark->position);
        criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_USED);
    }
    if (!criteria->LogoInBorder()) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): disable border detection and delete border marks");  // avoid false detection of border
        marks.DelType(MT_HBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        marks.DelType(MT_VBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        criteria->SetDetectionState(MT_HBORDERCHANGE, false);
        criteria->SetDetectionState(MT_VBORDERCHANGE, false);
    }
    return begin;
}


cMark *cMarkAdStandalone::Check_HBORDERSTART() {
    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): search for hborder start mark");
    cMark *hStart = marks.GetAround(MAX_ASSUMED * decoder->GetVideoFrameRate(), startA, MT_HBORDERSTART);
    if (hStart) { // we found a hborder start mark
        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start found at (%d)", hStart->position);

        // check if hborder marks are from long black closing credits
        cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
        if (hStop && criteria->LogoInBorder()) {
            cMark *logoStartBefore = marks.GetPrev(hStop->position, MT_LOGOSTART);
            if (logoStartBefore) {
                cMark *logoStopAfter  = marks.GetNext(logoStartBefore->position, MT_LOGOSTOP);
                if (logoStopAfter) {
                    int diffLogoStarthBorderStart  = (hStart->position - logoStartBefore->position) / decoder->GetVideoFrameRate();
                    int diffBorderStarthBorderStop = (hStop->position  - hStart->position)          / decoder->GetVideoFrameRate();
                    int diffhBorderStopLogoStart   = (logoStopAfter->position - hStart->position)   / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): check for false detected hborder from opening credits");
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): MT_LOGOSTART (%d) -> %ds -> MT_HBORDERSTART (%d) -> %ds -> MT_HBORDERSTOP (%d) -> %ds -> MT_LOGOSTOP (%d)", logoStartBefore->position, diffLogoStarthBorderStart, hStart->position, diffBorderStarthBorderStop, hStop->position, diffhBorderStopLogoStart, logoStopAfter->position);
                    // exampe of openeing credits false detected as hborder
                    // MT_LOGOSTART (15994) -> 3s -> MT_HBORDERSTART (16074) -> 82s -> MT_HBORDERSTOP (18131) -> 1460s -> MT_LOGOSTOP (52588)
                    if ((diffLogoStarthBorderStart <= 3) && (diffBorderStarthBorderStop <= 82) && (diffhBorderStopLogoStart >= 1460)) {
                        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): false detected hborder from opening credits found, delete hborder marks");
                        // there can also be a false vborder start from opening credits around logo start
                        const cMark *vStart = marks.GetAround(decoder->GetVideoFrameRate(), logoStartBefore->position, MT_VBORDERSTART);
                        if (vStart) marks.Del(vStart->position);
                        marks.Del(hStart->position);
                        marks.Del(hStop->position);
                        return nullptr;
                    }
                }
            }
        }

        // check if first broadcast is long enough
        if (hStop) {
            int lengthBroadcast = (hStop->position - hStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): next horizontal border stop mark (%d), length of broadcast %ds", hStop->position, lengthBroadcast);
            const cMark *hNextStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
            // invalid example
            // vertical border stop at (9645) 321s after vertical border start (1614)
            if ((((hStart->position == 0) || (lengthBroadcast <= 321)) && !hNextStart) || // hborder preview or hborder brodcast before broadcast start, changed from 291 to 321
                    (lengthBroadcast <=  74)) {                                           // very short broadcast length is never valid
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start (%d) and stop (%d) mark from previous recording, delete all marks from up to hborder stop", hStart->position, hStop->position);
                // delete hborder start/stop marks because we ignore hborder start mark
                marks.DelTill(hStop->position, true);
                return nullptr;
            }
        }

        // check hborder start position
        if (hStart->position >= IGNORE_AT_START) {  // position < IGNORE_AT_START is a hborder start from previous recording
            // found valid horizontal border start mark
            criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
            // we found a hborder, check logo stop/start after to prevent to get closing credit from previous recording as start
            // only works for channel with logo in border
            if (criteria->LogoInBorder()) {
                cMark *logoStop  = marks.GetNext(hStart->position, MT_LOGOSTOP);        // logo stop mark can be after hborder start
                if (!logoStop) logoStop = marks.GetPrev(hStart->position, MT_LOGOSTOP); //                   or before hborder start
                cMark *logoStart = marks.GetNext(hStart->position, MT_LOGOSTART);
                if (logoStop && logoStart && (logoStart->position > logoStop->position)) {
                    int diffStop  = (logoStop->position  - hStart->position) / decoder->GetVideoFrameRate();
                    int diffStart = (logoStart->position - hStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): found logo stop (%d) %ds and logo start (%d) %ds after hborder start (%d)", logoStop->position, diffStop, logoStart->position, diffStart, hStart->position);
                    if ((diffStop >= -1) && (diffStop <= 13) && (diffStart <= 17)) {
                        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): hborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", hStart->position, logoStart->position);
                        marks.Del(hStart->position);
                        hStart = logoStart;
                    }
                }
            }
            // cleanup all logo marks if we have a hborder start mark
            if (hStart->type != MT_LOGOSTART) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete logo marks if any");
                marks.DelType(MT_LOGOCHANGE, 0xF0);
            }
            // cleanup vborder marks
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete vborder marks if any");
            marks.DelType(MT_VBORDERCHANGE, 0xF0);
        }
        else {
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete too early horizontal border mark (%d)", hStart->position);
            marks.Del(hStart->position);
            hStart = nullptr;
            if (marks.Count(MT_HBORDERCHANGE, 0xF0) == 0) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border since start, use it for mark detection");
                criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
                if (!criteria->LogoInBorder()) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): logo marks can not be valid, delete it");
                    marks.DelType(MT_LOGOCHANGE, 0xF0);
                }
            }
        }
    }
    else { // we found no hborder start mark
        // check if we have a hborder double episode from recording start
        cMark *firstBorderStart = marks.GetNext(-1, MT_HBORDERSTART);
        if (firstBorderStart && (firstBorderStart->position <= IGNORE_AT_START) && (marks.Count(MT_HBORDERSTART) > marks.Count(MT_HBORDERSTOP))) {
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start mark at recording start found, we have a double episode");
            criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
        }
        else { // currect broadcast has no hborder
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): no horizontal border start mark found, disable horizontal border detection and cleanup marks");
            criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_DISABLED);
            // keep last hborder stop, maybe can use it as start mark
            const cMark *lastBorderStop = marks.GetPrev(INT_MAX, MT_HBORDERSTOP);
            if (lastBorderStop) { // delete all marks before hborder stop, they can not be a valid start mark
                marks.DelFromTo(0, lastBorderStop->position - 1, MT_ALL, 0xFF);
            }
            else marks.DelType(MT_HBORDERCHANGE, 0xF0);  // maybe the is a late invalid hborder start marks, exists sometimes with old vborder recordings
        }
        return nullptr;
    }
    return hStart;
}

cMark *cMarkAdStandalone::Check_VBORDERSTART(const int maxStart) {
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): search for vborder start mark");
    cMark *vStart = marks.GetAround(240 * decoder->GetVideoFrameRate() + startA, startA, MT_VBORDERSTART);
    if (!vStart) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no vertical border at start found, ignore vertical border detection");
        criteria->SetDetectionState(MT_VBORDERCHANGE, false);
        marks.DelType(MT_VBORDERSTART, 0xFF);  // maybe we have a vborder start from a preview or in a doku, delete it
        const cMark *vStop = marks.GetAround(240 * decoder->GetVideoFrameRate() + startA, startA, MT_VBORDERSTOP);
        if (vStop) {
            int pos = vStop->position;
            char *comment = nullptr;
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
            marks.Del(pos);
            if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment = nullptr;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, pos, comment);
            if (comment) {
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        return nullptr;
    }

    // found vborder start, check if it is valid
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border start found at (%d)", vStart->position);
    cMark *vStopAfter = marks.GetNext(vStart->position, MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is invalid
    if (vStopAfter) {
        int markDiff = (vStopAfter->position - vStart->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop found at (%d), %ds after vertical border start", vStopAfter->position, markDiff);

        // prevent to get start of last part of previous broadcast as start mark
        cMark *vNextStart = marks.GetNext(vStopAfter->position, MT_VBORDERSTART);
        cMark *vPrevStart = marks.GetPrev(vStart->position,     MT_VBORDERSTART);
        if (!vPrevStart && !vNextStart) {
            // we have only start/stop vborder sequence in start part, this can be from broadcast before or false vborder detection from dark scene in vborder
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no next vertical border start found after start (%d) and stop (%d)", vStart->position, vStopAfter->position);
            // check if it is false vborder detection from dark scene in vborder
            int startAvBorderStart     = (vStart->position     - startA)              / decoder->GetVideoFrameRate();
            int vBorderStartvBorderStop = (vStopAfter->position - vStart->position)     / decoder->GetVideoFrameRate();
            int vBorderStopframeCheckStart     = (frameCheckStart             - vStopAfter->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): startA (%5d) -> %ds -> MT_VBORDERSTART (%d) -> %ds -> MT_VBORDERSTART (%d) -> %ds -> frameCheckStart (%d)", startA, startAvBorderStart,  vStart->position, vBorderStartvBorderStop, vStopAfter->position, vBorderStopframeCheckStart, frameCheckStart);
            // example of invalid vborder from dark scene
            // startA ( 4075) -> 9s -> MT_VBORDERSTART (4310) -> 115s -> MT_VBORDERSTART (7188) -> 355s -> frameCheckStart (16075)
            if ((startAvBorderStart <= 9) && (vBorderStartvBorderStop <= 115) && (vBorderStopframeCheckStart >= 355)) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border start (%d) and stop (%d) from dark scene, delete marks", vStart->position, vStopAfter->position);
                marks.Del(vStart->position);
                marks.Del(vStopAfter->position);
                return nullptr;
            }

            // check if it is from broadcast before
            // example
            // start vertical border    at 0:05:03.23 -> stop  vertical border    at 0:09:04.91 (241s) valid first part, no next vborder start because of long ad after
            // start vertical border    at 0:01:04.80 -> stop  vertical border    at 0:06:25.92 (321s) is from broadcast before
            if ((vStart->position < IGNORE_AT_START) || ((vStart->position < startA) && (markDiff <= 321))) {  // vbordet start/stop from previous broadcast
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop at (%d) %ds after vertical border start (%d) in start part found, this is from previous broadcast, delete all marks before", vStopAfter->position, markDiff, vStart->position);
                // check if we have long dark opening credits witch results in late vborder stop from previous broadcast, only possible if we have valid logos
                // prevent to delete valid logo start mark, delete false vborder marks instead
                if (criteria->LogoInBorder()) {
                    cMark *logoStartBefore = marks.GetPrev(vStopAfter->position, MT_LOGOSTART);
                    cMark *logoStopAfter   = marks.GetNext(vStopAfter->position, MT_LOGOSTOP);
                    if (logoStartBefore && logoStopAfter) {
                        int difflogoStartBefore = (vStopAfter->position    - logoStartBefore->position) / decoder->GetVideoFrameRate();
                        int difflogoStopAfter   = (logoStopAfter->position - vStopAfter->position)      / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): MT_LOGOSTART (%d) -> %ds -> MT_VBORDERSTOP (%d) -> %ds -> MT_LOGOSTOP (%d)", logoStartBefore->position, difflogoStartBefore, vStopAfter->position, difflogoStopAfter, logoStopAfter->position);
                        // example of long opening credits after vborder broadcast
                        // MT_LOGOSTART (7643) -> 67s -> MT_VBORDERSTOP (9321) -> 1450s -> MT_LOGOSTOP (45586)
                        if ((difflogoStartBefore <= 67) && (difflogoStopAfter >= 1450)) {
                            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): valid logo start mark (%d) found, delete vborder marks", logoStartBefore->position);
                            marks.Del(vStart->position);
                            marks.Del(vStopAfter->position);
                            return nullptr;
                        }
                    }
                }
                // broadcast start can not be before vborder stop from previous broadcast, keep vborder stop as possible start mark
                // keep all stop marks, maybe we need logo stop to detect valid logo start mark
                marks.DelFromTo(0, vStopAfter->position - 1, MT_START, 0x0F);
                criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);
                return nullptr;
            }
        }
    }

    // prevent to get start of next broadcast as start of this very short broadcast
    if (vStart->position > maxStart) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vborder start mark (%d) after max start mark (%d) is invalid", vStart->position, maxStart);
        return nullptr;
    }

    if (vStart->position < IGNORE_AT_START) { // early position is a vborder from previous recording
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): delete too early vertical border start found at (%d)", vStart->position);
        const cMark *vBorderStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);
        marks.Del(vStart->position);
        if (!vBorderStop || (vBorderStop->position > startA + 420 * decoder->GetVideoFrameRate())) {
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border since start, use it for mark detection");
            criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);
            if (!criteria->LogoInBorder()) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): logo marks can not be valid, delete it");
                marks.DelType(MT_LOGOCHANGE, 0xF0);
            }
        }
        return nullptr;
    }

    // check if we have a logo start before vborder start, prevent a false vborder start/stop from dark scene as start mark
    if (!criteria->LogoInBorder()) {  // not possible for logo in border channel because vborder start and logo start can be on same position
        // valid example:
        // double episode 4:3 and vborder, too short vborder in broadcast before because closing credits overlay vborder, vborder start is valid start mark
        // start aspect ratio       at 0:00:00.00, inBroadCast 0
        // start logo               at 0:00:00.00, inBroadCast 1
        // start vertical border    at 0:01:51.00, inBroadCast 1
        cMark *logoStart  = marks.GetPrev(vStart->position, MT_ALL);
        if (logoStart && (logoStart->type == MT_LOGOSTART) && (logoStart->position >= IGNORE_AT_START)) {
            int diff = (vStart->position - logoStart->position) / decoder->GetVideoFrameRate();
            if (diff > 50) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): logo start mark (%d) direct %ds before vborder start (%d) found, delete invalid vborder marks from dark scene", logoStart->position, diff, vStart->position);
                marks.DelType(MT_VBORDERCHANGE, 0xF0);
                return nullptr;
            }

        }
    }

    // found valid vertical border start mark
    if (criteria->GetMarkTypeState(MT_ASPECTCHANGE) == CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): use vertical border only as start mark, keep using aspect ratio detection");
        criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE);
    }
    else criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);

    // check logo start after vborder start to prevent to get closing credit from previous recording as start mark
    if (criteria->LogoInBorder()) {  // prevent to get logo interruption as false start mark
        cMark *logoStart = marks.GetNext(vStart->position, MT_LOGOSTART);
        if (logoStart) {
            int diffStart = (logoStart->position - vStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): found logo start (%d) %ds after vborder start (%d)", logoStart->position, diffStart, vStart->position);
            if ((diffStart >= 10) && (diffStart <= 30)) {  // near logo start is fade in logo
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", vStart->position, logoStart->position);
                marks.Del(vStart->position);
                return logoStart;
            }
        }
    }

    // check logo stop after vborder start to prevent to get closing credit from previous recording as start mark
    cMark *logoStop  = marks.GetNext(vStart->position, MT_LOGOSTOP);
    if (logoStop) {
        int diffStop  = (logoStop->position  - vStart->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): found logo stop (%d) %ds after vborder start (%d)", logoStop->position, diffStop, vStart->position);
        if (diffStop <= 51) {  // changed from 25 to 51
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vborder start mark position (%d) includes previous closing credits, use logo stop (%d) instead", vStart->position, logoStop->position);
            marks.Del(vStart->position);
            logoStop = marks.ChangeType(logoStop, MT_START);
            return logoStop;
        }
    }
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): delete logo marks if any");
    marks.DelType(MT_LOGOCHANGE, 0xF0); // delete logo marks, vborder is stronger

    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): delete HBORDER marks if any");
    marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
    return vStart;
}


void cMarkAdStandalone::CheckStart() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", decoder->GetFrameNumber(), frameCheckStart);
    int maxStart = startA + (length * decoder->GetVideoFrameRate() / 2);  // half of recording
    char *indexToHMSFStart = marks.IndexToHMSF(startA, false);
    if (indexToHMSFStart) {
        ALLOC(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %d at %s, max allowed start frame (%d)", startA, indexToHMSFStart, maxStart);
        FREE(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        free(indexToHMSFStart);
    }
    DebugMarks();     //  only for debugging

    // set initial mark criteria
    if (marks.Count(MT_HBORDERSTART) == 0) criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no hborder start, broadcast can not have hborder
    else if ((marks.Count(MT_HBORDERSTART) == 1) && (marks.Count(MT_HBORDERSTOP) == 0)) criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

    if (marks.Count(MT_VBORDERSTART) == 0) criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no vborder start, broadcast can not have vborder
    else if ((marks.Count(MT_VBORDERSTART) == 1) && (marks.Count(MT_VBORDERSTOP) == 0)) criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

// recording start
    cMark *begin = marks.GetAround(startA, 1, MT_RECORDINGSTART);  // do we have an incomplete recording ?
    if (begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): found MT_RECORDINGSTART (%i), use this as start mark for the incomplete recording", begin->position);
        // delete short stop marks without start mark
        cMark *stopMark = marks.GetNext(0, MT_CHANNELSTOP);
        if (stopMark) {
            int diff = stopMark->position / decoder->GetVideoFrameRate();
            if ((diff < 30) && (marks.Count(MT_CHANNELSTART, 0xFF) == 0)) {
                dsyslog("cMarkAdStandalone::CheckStart(): delete stop mark (%d) without start mark", stopMark->position);
                marks.Del(stopMark->position);
            }
        }
    }

// audio channel start
    if (!begin) begin = Check_CHANNELSTART();

// check if aspect ratio from VDR info file is valid
    bool checkedAspectRatio = false;
    sAspectRatio *aspectRatioFrame = decoder->GetFrameAspectRatio();
    // end of start part can not be 4:3 if broadcast is 16:9
    if (aspectRatioFrame && (macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9) && (aspectRatioFrame->num == 4) && (aspectRatioFrame->den == 3)) {
        dsyslog("cMarkAdStandalone::CheckStart(): broadcast at end of start part is 4:3, VDR info tells 16:9, info file is wrong");
        SwapAspectRatio();
        checkedAspectRatio = true;  // now we are sure, aspect ratio is correct
    }
    // very short start/stop pairs (broadcast) are impossible, these must be stop/start (ad) pairs
    if (!checkedAspectRatio) {
        cMark *aspectStart = marks.GetNext(-1, MT_ASPECTSTART); // first start can be on position 0
        if (aspectStart) {
            cMark *aspectStop = marks.GetNext(aspectStart->position, MT_ASPECTSTOP);
            if (aspectStop) {
                int diff    = (aspectStop->position - aspectStart->position) / decoder->GetVideoFrameRate();
                int startAS = startA / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start (%d) stop (%d): length %ds", aspectStart->position, aspectStop->position, diff);
                if (diff <= 60) {
                    dsyslog("cMarkAdStandalone::CheckStart(): length %ds for first broadcast too short, pre recording time is %ds, VDR info file must be wrong", diff, startAS);
                    SwapAspectRatio();
                    checkedAspectRatio = true;  // now we are sure, aspect ratio is correct
                }
            }
        }
    }
    // if broadcast is 16:9, check aspect ratio from info file
    //
    // valid sequence for 16:9 video:
    // MT_ASPECTSTOP (start of prev broadcast) -> MT_LOGOSTART (4:3 logo) ->  MT_LOGOSTOP (4:3 logo)            -> MT_ASPECTSTART (start of 16:9 ad) -> MT_LOGOSTART (16:9 broadcast)
    // MT_ASPECTSTOP (start of prev broadcast) -> MT_LOGOSTART (4:3 logo) ->  MT_ASPECTSTART (start of 16:9 ad) -> MT_LOGOSTOP (4:3 logo)            -> MT_LOGOSTART (16:9 broadcast)
    //
    // found invalid sequence, info is 16:9 but broadcast is 4:3
    // MT_ASPECTSTOP (start of prev broadcast) ->  MT_LOGOSTART (4:3 logo) -> MT_ASPECTSTART (start of 16:9 ad)  (continuous double episode)
    if (!checkedAspectRatio && (macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
        cMark *aspectStop = marks.GetNext(-1, MT_ASPECTCHANGE, 0xF0);
        if (aspectStop && aspectStop->type == MT_ASPECTSTOP) {  // with aspect mark must be MT_ASPECTSTOP
            cMark *logoStart = marks.GetNext(aspectStop->position, MT_LOGOSTART);   // next mark must be 4:3 logo start
            if (logoStart) {
                cMark *aspectStart = marks.GetNext(logoStart->position, MT_ASPECTSTART);
                if (aspectStart) {
                    cMark *nextMark = marks.GetNext(aspectStart->position, MT_ALL);  // check if no next mark in start part
                    if (!nextMark) {
                        dsyslog("cMarkAdStandalone::CheckStart(): sequence MT_ASPECTSTOP (%d) -> MT_LOGOSTART (%d) -> MT_ASPECTSTART (%d) is invalid for 16:9 video", aspectStop->position, logoStart->position, aspectStart->position);
                        SwapAspectRatio();
                    }
                }
            }
            else {   // we have no valid logo start (detection fault or border)
                cMark *aspectStart = marks.GetNext(aspectStop->position, MT_ASPECTSTART);
                if (aspectStart) {
                    int lengthAd = (aspectStart - aspectStop) /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::CheckStart(): length of first ad %ds", length);
                    if (lengthAd > 10) {
                        dsyslog("cMarkAdStandalone::CheckStart(): length of ad is invalid, must be length of broadcast");
                        SwapAspectRatio();
                    }
                }
            }
        }
    }
    video->SetAspectRatioBroadcast(macontext.Info.AspectRatio);  // now aspect ratio is correct, tell it video based detection
    // for 4:3 broadcast cleanup all vborder marks, these are false detected from dark scene or, if realy exists, they are too small for reliable detection
    if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) marks.DelType(MT_VBORDERCHANGE, 0xF0); // delete wrong vborder marks


// aspect ratio start
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for aspect ratio start mark");
        // search for aspect ratio start mark
        cMark *aStart = marks.GetAround(480 * decoder->GetVideoFrameRate(), startA, MT_ASPECTSTART);
        if (aStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio start mark at (%d), video info is %d:%d", aStart->position, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) { // we have a aspect ratio start mark, check if valid
                criteria->SetMarkTypeState(MT_ASPECTCHANGE, CRITERIA_USED);  // use aspect ratio marks for detection, even if we have to use another start mark
                while (aStart && (aStart->position <= (16 * decoder->GetVideoFrameRate()))) {    // to near at start of recording is from broadcast before
                    dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark (%d) too early, try next", aStart->position);
                    aStart = marks.GetNext(aStart->position, MT_ASPECTSTART);
                    if (aStart && aStart->position > (startA +  (MAX_ASSUMED * decoder->GetVideoFrameRate()))) aStart = nullptr; // too late, this can be start of second part
                }
                // check if we have a 4:3 double episode
                if (aStart) {
                    int diffAssumed = (startA - aStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark (%d) %ds before assumed start (%d)", aStart->position, diffAssumed, startA);
                    if (diffAssumed < 263) {
                        begin = aStart;
                        dsyslog("cMarkAdStandalone::CheckStart(): valid aspect ratio start mark (%d) found", aStart->position);
                    }
                    else dsyslog("cMarkAdStandalone::CheckStart(): ignore too early aspect ratio start mark (%d)", aStart->position);
                }
                // we have valid a aspect ratio start mark from a 4:3 recording, advertisement are 16:9, delete all other marks and disable all other detection
                if (begin) {
                    marks.DelWeakFromTo(0, INT_MAX, MT_ASPECTCHANGE); // delete all weak marks
                    marks.Del(MT_CHANNELSTART);  // delete channel marks from previous recording
                    marks.Del(MT_CHANNELSTOP);
                    video->ClearBorder();
                }
            }
            else { // video is 16:9 but we have a aspect start mark, must be end previous 4:3 broadcast, broadcast start must be after that
                dsyslog("cMarkAdStandalone::CheckStart(): 16:9 video, aspect ratio start (%d) from end of previous 4:3 broadcast, delete marks before", aStart->position);
                marks.DelTill(aStart->position, true);  // keep aspect ratio start mark, maybe we use it if we have no logo start mark
            }
        }
    }

    // log video infos
    if (decoder->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264) isyslog("HD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    else isyslog("SD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);

    // before we can check border marks, we have to check with is valid

    // cleanup invalid logo marks, done before border check because we use logo marks to correct border marks, call without decoder, do not messup read position
    RemoveLogoChangeMarks(true);

// horizontal border start
    if (!begin) begin = Check_HBORDERSTART();

// vertical border start
    if (!begin) begin = Check_VBORDERSTART(maxStart);

// try logo start mark
    if (!begin) begin = Check_LOGOSTART();

    // drop too early marks of all types
    if (begin && (begin->type != MT_RECORDINGSTART) && (begin->position <= IGNORE_AT_START)) {  // first frames are from previous recording
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = nullptr;
    }

// no mark found, try anything
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for any start mark");
        marks.DelTill(IGNORE_AT_START);    // we do not want to have a initial mark from previous recording as a start mark
        begin = marks.GetAround(160 * decoder->GetVideoFrameRate(), startA, MT_START, 0x0F);  // not too big search range, changed from 240 to 160
        if (begin) {
            dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) type 0x%X after search for any type", begin->position, begin->type);
            if ((begin->type == MT_ASSUMEDSTART) || (begin->inBroadCast) || !criteria->GetDetectionState(MT_LOGOCHANGE)) { // test on inBroadCast because we have to take care of black screen marks in an ad, MT_ASSUMEDSTART is from converted channel stop of previous broadcast
                const char *indexToHMSF = marks.GetTime(begin);
                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%i) type 0x%X at %s inBroadCast %i", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
            }
            else { // mark in ad
                const char *indexToHMSF = marks.GetTime(begin);
                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): start mark found but not inBroadCast (%i) type 0x%X at %s inBroadCast %i, ignoring", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                begin = nullptr;
            }
        }
    }

    if (begin && ((begin->position  / decoder->GetVideoFrameRate()) < 1) && (begin->type != MT_RECORDINGSTART)) { // do not accept marks in the first second, the are from previous recording, expect manual set MT_RECORDINGSTART for missed recording start
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = nullptr;
    }

    // still no start mark found, try channel stop marks from previous broadcast
    // for channel with good VPS events better use this, there maybe an ad after end of previous broadcast
    if (!begin && !criteria->GoodVPS()) {
        cMark *channelStop  = marks.GetNext(0, MT_CHANNELSTOP);
        if (channelStop) {
            cMark *channelStart = marks.GetNext(channelStop->position, MT_CHANNELSTART);
            if (!channelStart) { // if there is a channel start mark after, channel stop is not an end mark of previous broadcast
                dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_CHANNELSTOP (%d) from previous recoring as start mark", channelStop->position);
                begin = marks.ChangeType(channelStop, MT_START);
                marks.DelTill(begin->position);
            }
        }
    }

    // still no start mark found, try hborder stop marks from previous broadcast
    if (!begin) { // try hborder stop mark as start mark
        cMark *hborderStop  = marks.GetNext(0, MT_HBORDERSTOP);
        if (hborderStop) {
            cMark *hborderStart = marks.GetNext(hborderStop->position, MT_HBORDERSTART);
            if (!hborderStart) { // if there is a hborder start mark after, hborder stop is not an end mark of previous broadcast
                dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP (%d) from previous recoring as start mark", hborderStop->position);
                begin = marks.ChangeType(hborderStop, MT_START);
                marks.DelTill(begin->position);
            }
        }
    }

    // still no start mark found, try vborder stop marks from previous broadcast
    if (!begin) { // try hborder stop mark as start mark
        cMark *vborderStop = marks.GetNext(0, MT_VBORDERSTOP);
        if (vborderStop) {
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_VBORDERSTOP (%d) from previous recoring as start mark", vborderStop->position);
            begin = marks.ChangeType(vborderStop, MT_START);
            marks.DelTill(begin->position);
        }
    }

    // no start mark found at all, set start after pre timer
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, assume start time at pre recording time");
        cMark *nextMark = marks.GetNext(startA, MT_ALL);
        if (!nextMark || ((nextMark->type & 0x0F) == MT_STOP)) {  // do not insert black screen start before other start mark
            marks.DelTill(startA);
            marks.Del(MT_NOBLACKSTART);  // delete all black screen marks
            marks.Del(MT_NOBLACKSTOP);
            sMarkAdMark mark = {};
            mark.position = startA;
            mark.type = MT_ASSUMEDSTART;
            AddMark(&mark);
            begin = marks.GetFirst();
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): use start mark (%d) after assumed start (%d)", nextMark->position, startA);
            begin = nextMark;
        }
    }


    // now we have the final start mark, do fine tuning
    marks.DelTill(begin->position, true);    // delete all marks till start mark
    const char *indexToHMSF = marks.GetTime(begin);
    char *typeName    = marks.TypeToText(begin->type);
    if (indexToHMSF && typeName) isyslog("using %s start mark on position (%d) at %s as broadcast start", typeName, begin->position, indexToHMSF);
    if (typeName) {
        FREE(strlen(typeName)+1, "text");
        free(typeName);
    }

    // if we have border marks, delete logo marks
    if ((begin->type == MT_VBORDERSTART) || (begin->type == MT_HBORDERSTART)) {
        marks.Del(MT_LOGOSTART);
        marks.Del(MT_LOGOSTOP);
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
    if (countStopStart > 6) {  // changed from 5 to 6, sometimes there are a lot of previews in start area
        video->ReducePlanes();
        isyslog("%d logo STOP/START pairs found after start mark, something is wrong with your logo", countStopStart);
        dsyslog("cMarkAdStandalone::CheckStart(): reduce logo processing to first plane and delete all marks after start mark (%d)", begin->position);
        marks.DelAfterFromToEnd(begin->position);
    }

    CheckStartMark();
    LogSeparator();
    CalculateCheckPositions(marks.GetFirst()->position);
    marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
    doneCheckStart = true;

    // debugging infos
    DebugMarks();
    LogSeparator();
    criteria->ListMarkTypeState();
    criteria->ListDetection();
    LogSeparator();
    return;
}


// check if start mark is valid
// check for short start/stop pairs at the start and select final start mark
void cMarkAdStandalone::CheckStartMark() {
    LogSeparator();
    cMark *startMark = marks.GetFirst(); // this is the start mark
    while (startMark) {
        if ((startMark->type & 0x0F) != MT_START) {
            dsyslog("cMarkAdStandalone::CheckStartMark(): invalid type, no start mark (%d)", startMark->position);
            return;
        }
        // check logo start mark
        if (startMark->type == MT_LOGOSTART) {
            dsyslog("cMarkAdStandalone::CheckStartMark(): check for final logo start mark (%d)", startMark->position);
            cMark *logoStop1  = marks.GetNext(startMark->position, MT_LOGOSTOP);
            if (logoStop1) {
                cMark *logoStart2 = marks.GetNext(logoStop1->position, MT_LOGOSTART);
                if (logoStart2) {
                    cMark *stop2 = marks.GetNext(logoStart2->position, MT_STOP, 0x0F);  // can be assumed stop
                    if (stop2) {
                        int lengthBroadcast1    = (logoStop1->position  - startMark->position)  / decoder->GetVideoFrameRate();
                        int lengthAd            = (logoStart2->position - logoStop1->position)  / decoder->GetVideoFrameRate();
                        int lengthBroadcast2    = (stop2->position  - logoStart2->position)     / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::CheckStartMark(): MT_LOGOSTART (%5d) -> %3ds -> MT_LOGOSTOP (%5d) -> %4ds ->  MT_LOGOSTART (%5d) -> %4ds -> MT_LOGOSTOP (%5d)",  startMark->position, lengthBroadcast1,  logoStop1->position, lengthAd, logoStart2->position, lengthBroadcast2, stop2->position);
// example of valid logo start mark, do not delete
// MT_LOGOSTART ( 7115) ->  82s -> MT_LOGOSTOP ( 9169) ->    0s ->  MT_LOGOSTART ( 9188) ->   10s -> MT_LOGOSTOP ( 9458) -> Comedy Central, preview direct after stop
//
// example of invalid logo start mark, delete first start pair
// MT_LOGOSTART ( 2944) ->  25s -> MT_LOGOSTOP ( 3584) ->   50s ->  MT_LOGOSTART ( 4848) -> 1213s -> MT_LOGOSTOP (35173) -> sixx, start of preview before broadcast
// MT_LOGOSTART (23712) ->   3s -> MT_LOGOSTOP (23871) ->   57s ->  MT_LOGOSTART (26732) ->  796s -> MT_LOGOSTOP (66572) -> ZDF, logo detetion fault
// MT_LOGOSTART ( 4675) ->  11s -> MT_LOGOSTOP ( 4962) ->  150s ->  MT_LOGOSTART ( 8715) -> 1312s -> MT_LOGOSTOP (41526) -> Nickelodeon, ad with false detected logo
// MT_LOGOSTART (  952) ->   3s -> MT_LOGOSTOP ( 1037) ->  196s ->  MT_LOGOSTART ( 5961) -> 1276s -> MT_LOGOSTOP (37865) -> Nickelodeon, ad with false detected logo
// MT_LOGOSTART ( 7383) ->   8s -> MT_LOGOSTOP ( 7590) ->  197s ->  MT_LOGOSTART (12518) -> 1247s -> MT_LOGOSTOP (43707) -> NICK MTV+, ad with false detected logo
// MT_LOGOSTART ( 3661) ->   1s -> MT_LOGOSTOP ( 3694) ->  199s ->  MT_LOGOSTART ( 8684) -> 1299s -> MT_LOGOSTOP (41161) -> Disney Channel, false logo detection from background pattern
// MT_LOGOSTART ( 2396) ->   2s -> MT_LOGOSTOP ( 2463) ->  287s ->  MT_LOGOSTART ( 9641) -> 1312s -> MT_LOGOSTOP (42442) -> Disney Channel, ad with false detected logo
// MT_LOGOSTART ( 2377) ->   2s -> MT_LOGOSTOP ( 2441) ->  290s ->  MT_LOGOSTART ( 9697) -> 1207s -> MT_LOGOSTOP (39877) -> Disney Channel, ad with false detected logo
// MT_LOGOSTART ( 4621) ->   1s -> MT_LOGOSTOP ( 4660) ->  325s ->  MT_LOGOSTART (12786) -> 1173s -> MT_LOGOSTOP (42121) -> Disney Channel, ad with false detected logo
// MT_LOGOSTART ( 3448) ->   3s -> MT_LOGOSTOP ( 3533) ->  370s ->  MT_LOGOSTART (12786) -> 1126s -> MT_LOGOSTOP (40948) -> Disney Channel, ad with false detected logo
// MT_LOGOSTART ( 7249) ->   7s -> MT_LOGOSTOP ( 7435) ->  460s ->  MT_LOGOSTART (18940) ->  718s -> MT_LOGOSTOP (36895) -> RTL Television, delayed broadcast start
// MT_LOGOSTART ( 3828) ->  28s -> MT_LOGOSTOP ( 4549) ->   37s ->  MT_LOGOSTART ( 5487) -> 1237s -> MT_LOGOSTOP (36427) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 3785) ->  28s -> MT_LOGOSTOP ( 4507) ->   31s ->  MT_LOGOSTART ( 5285) -> 1202s -> MT_LOGOSTOP (35355) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 2979) ->  33s -> MT_LOGOSTOP ( 3824) ->   54s ->  MT_LOGOSTART ( 5183) ->  955s -> MT_LOGOSTOP (29081) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 2870) ->  56s -> MT_LOGOSTOP ( 4276) ->   50s ->  MT_LOGOSTART ( 5532) -> 1218s -> MT_LOGOSTOP (35991) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 3867) ->  34s -> MT_LOGOSTOP ( 4718) ->   29s ->  MT_LOGOSTART ( 5458) -> 1229s -> MT_LOGOSTOP (36205) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 1015) ->  63s -> MT_LOGOSTOP ( 2611) ->   87s ->  MT_LOGOSTART ( 4804) -> 1236s -> MT_LOGOSTOP (35723) -> SIXX, start of preview before broadcast
                        if ((lengthBroadcast1 <= 63)  && (lengthAd >= 29) &&
                                (lengthBroadcast2 >= 718)) {   // if second broadcast part is very long, this maybe the valid first part
                            dsyslog("cMarkAdStandalone::CheckStartMark(): current start mark invalid, delete start (%d) and stop (%d) mark", startMark->position, logoStop1->position);
                            marks.Del(startMark->position);
                            marks.Del(logoStop1->position);
                            startMark = marks.GetFirst();
                        }
                        else break;
                    }
                    else break;
                }
                else break;
            }
            else break;
        }
        else {
            // check non logo start mark
            dsyslog("cMarkAdStandalone::CheckStartMark(): check for final start mark (%d)", startMark->position);
            cMark *markStop = marks.GetNext(startMark->position, MT_STOP, 0x0F);
            if (markStop) {
                int minFirstBroadcast = 60;                              // more trust strong marks
                if (startMark->type < MT_LOGOSTART) minFirstBroadcast = 106;  // changed from 77 to 80 to 106
                int lengthFirstBroadcast = (markStop->position - startMark->position) / decoder->GetVideoFrameRate(); // length of the first broadcast part
                dsyslog("cMarkAdStandalone::CheckStartMark(): first broadcast length %ds from (%d) to (%d) (expect <=%ds)", lengthFirstBroadcast, startMark->position, markStop->position, minFirstBroadcast);
                cMark *markStart = marks.GetNext(markStop->position, MT_START, 0x0F);
                if (markStart) {
                    int lengthFirstAd = 1000 * (markStart->position - markStop->position) / decoder->GetVideoFrameRate(); // length of the first broadcast part
                    dsyslog("cMarkAdStandalone::CheckStartMark(): first advertising length %dms from (%d) to (%d)", lengthFirstAd, markStop->position, markStart->position);
                    if (lengthFirstAd <= 1000) {
                        dsyslog("cMarkAdStandalone::CheckStartMark(): very short first advertising, this can be a logo detection failure");
                        break;
                    }
                    else {
                        if (lengthFirstBroadcast < minFirstBroadcast) {
                            dsyslog("cMarkAdStandalone::CheckStartMark(): too short STOP/START/STOP sequence at start, delete first pair");
                            marks.Del(startMark->position);
                            marks.Del(markStop->position);
                            startMark = marks.GetFirst();
                        }
                        else break;
                    }
                }
                else break;
            }
            else break;
        }
    }
    return;
}



// write all current marks to log file
//
void cMarkAdStandalone::DebugMarks() {           // write all marks to log file
    dsyslog("***********************************************************************************************************************");
    dsyslog("cMarkAdStandalone::DebugMarks(): current marks:");

    // strong marks
    cMark *mark = marks.GetFirst();
    while (mark) {
        const char *indexToHMSF = marks.GetTime(mark);
        if (indexToHMSF) {
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                if ((mark->type & 0x0F) == MT_START) LogSeparator(false);
                if ((mark->type & 0xF0) == MT_MOVED) {
                    char *markOldType = marks.TypeToText(mark->oldType);
                    char *markNewType = marks.TypeToText(mark->newType);
                    if (markOldType && markNewType) {
                        dsyslog("mark at position %6d: %-5s %-18s at %s, inBroadCast %d, old type: %s %s, new type: %s %s", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast, markOldType, ((mark->oldType & 0x0F) == MT_START)? "start" : "stop", markNewType, ((mark->newType & 0x0F) == MT_START)? "start" : "stop");
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                    }
                }
                else dsyslog("mark at position %6d: %-5s %-18s at %s, inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        else esyslog("cMarkAdStandalone::DebugMarks(): could not get time to mark (%d) type %d", mark->position, mark->type);
        mark = mark->Next();
    }
#ifdef DEBUG_WEAK_MARKS
    // weak marks
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current black marks:");
    mark = blackMarks.GetFirst();
    while (mark) {
        const char *indexToHMSF = marks.GetTime(mark);
        if (indexToHMSF) {
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else esyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        mark=mark->Next();
    }
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current silence marks:");
    mark = silenceMarks.GetFirst();
    while (mark) {
        const char *indexToHMSF = marks.GetTime(mark);
        if (indexToHMSF) {
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        mark=mark->Next();
    }
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current scene change marks:");
    mark = sceneMarks.GetFirst();
    while (mark) {
        const char *indexToHMSF = marks.GetTime(mark);
        if (indexToHMSF) {
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        mark=mark->Next();
    }
#endif
    dsyslog("***********************************************************************************************************************");
}


void cMarkAdStandalone::CheckMarks() {           // cleanup marks that make no sense
    LogSeparator(true);

    const cMark *firstMark = marks.GetFirst();
    if (!firstMark) {
        esyslog("no marks at all detected, something went very wrong");
        return;
    }
    int newStopA = firstMark->position + decoder->GetVideoFrameRate() * (length + (2 * MAX_ASSUMED));  // we have to recalculate stopA
    // remove invalid marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): remove invalid marks");
    DebugMarks();     //  only for debugging
    marks.DelInvalidSequence();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // if stop/start distance is too big, remove pair
        if (((mark->type & 0x0F) == MT_STOP) && (mark->Next()) && ((mark->Next()->type & 0x0F) == MT_START)) {
            int diff = (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
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
        if (((mark->type & 0x0F) == MT_START) && (!mark->Next())) {      // delete start mark at the end
            if (marks.GetFirst()->position != mark->position) {        // do not delete start mark
                esyslog("cMarkAdStandalone::CheckMarks(): START mark at the end, deleting %d", mark->position);
                marks.Del(mark);
                break;
            }
        }
        mark = mark->Next();
    }

// delete logo and border marks if we have channel marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete logo marks if we have channel or border marks");
    DebugMarks();     //  only for debugging
    const cMark *channelStart = marks.GetNext(0, MT_CHANNELSTART);
    const cMark *channelStop  = marks.GetNext(0, MT_CHANNELSTOP);
    cMark *hborderStart       = marks.GetNext(0, MT_HBORDERSTART);
    cMark *hborderStop        = marks.GetNext(0, MT_HBORDERSTOP);
    cMark *vborderStart       = marks.GetNext(0, MT_VBORDERSTART);
    cMark *vborderStop        = marks.GetNext(0, MT_VBORDERSTOP);
    if (hborderStart && hborderStop) {
        int hDelta = (hborderStop->position - hborderStart->position) / decoder->GetVideoFrameRate();
        if (hDelta <= 230) {  // changed from 120 to 230
            dsyslog("cMarkAdStandalone::CheckMarks(): found hborder start (%d) and stop (%d), but distance %d too short, try if there is a next pair", hborderStart->position, hborderStop->position, hDelta);
            hborderStart = marks.GetNext(hborderStart->position, MT_HBORDERSTART);
            hborderStop = marks.GetNext(hborderStop->position, MT_HBORDERSTOP);
        }
    }
    if (vborderStart && vborderStop) {
        int vDelta = (vborderStop->position - vborderStart->position) / decoder->GetVideoFrameRate();
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
                if (((mark->type == MT_LOGOSTART) || (mark->type == MT_LOGOSTOP)) && (mark->position != marks.GetLast()->position)) {  // do not delete mark on end position, can be a logo mark even if we have border
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

// delete very short logo stop/start pairs from detection error or undetected info logo
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete very short logo stop/start pairs from detection error or undetected info logo");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if (mark->type == MT_LOGOSTOP) {
            cMark *prevLogoStart = marks.GetPrev(mark->position, MT_LOGOSTART);
            cMark *nextLogoStart = marks.GetNext(mark->position, MT_LOGOSTART);
            if (prevLogoStart && nextLogoStart) {
                cMark *nextStop = marks.GetNext(mark->position, MT_STOP, 0x0F);  // last stop mark can be a different type
                if (nextStop) {
                    int prevLogoStart_Stop      = 1000 * (mark->position          - prevLogoStart->position) /  decoder->GetVideoFrameRate();
                    long int stop_nextLogoStart = 1000 * (nextLogoStart->position - mark->position)          /  decoder->GetVideoFrameRate();
                    int nextLogoStart_nextStop  = 1000 * (nextStop->position      - nextLogoStart->position) /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::CheckMarks(): MT_LOGOSTART (%6d) -> %7dms -> MT_LOGOSTOP (%6d) -> %7ldms -> MT_LOGOSTART (%6d) -> %7dms -> MT_STOP (%6d)", prevLogoStart->position, prevLogoStart_Stop, mark->position, stop_nextLogoStart, nextLogoStart->position, nextLogoStart_nextStop, nextStop->position);

// cleanup logo detection failure
// delete sequence long broadcast -> very short stop/start -> long broadcast
// MT_LOGOSTART ( 15766) -> 1103180ms -> MT_LOGOSTOP ( 70925) ->    1040ms -> MT_LOGOSTART ( 70977) ->   83260ms -> MT_STOP ( 75140)
// MT_LOGOSTART ( 70977) ->   83260ms -> MT_LOGOSTOP ( 75140) ->    1020ms -> MT_LOGOSTART ( 75191) -> 1536500ms -> MT_STOP (152016)
                    if ((prevLogoStart_Stop     >= 83260) &&
                            (stop_nextLogoStart     <= 1040) &&
                            (nextLogoStart_nextStop >= 83260)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from logo detection failure, deleting", mark->position, nextLogoStart->position);
                        cMark *tmp = nextStop;
                        marks.Del(nextLogoStart);
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }

// delete very short stop/start pair from introduction logo
// valid short stop/start, do not delete
// MT_LOGOSTART ( 48867) ->    4880ms -> MT_LOGOSTOP ( 48989) ->     760ms -> MT_LOGOSTART ( 49008) ->  795000ms -> MT_STOP (68883)
// MT_LOGOSTART ( 51224) ->   29800ms -> MT_LOGOSTOP ( 51969) ->     920ms -> MT_LOGOSTART ( 51992) ->  622840ms -> MT_STOP (67563)
// MT_LOGOSTART ( 49708) ->  593600ms -> MT_LOGOSTOP ( 64548) ->     120ms -> MT_LOGOSTART ( 64551) ->   14880ms -> MT_STOP (64923)
// MT_LOGOSTART ( 37720) ->   26280ms -> MT_LOGOSTOP ( 38377) ->     840ms -> MT_LOGOSTART ( 38398) ->  254120ms -> MT_STOP (44751)
//

// invalid stop/start pair from introduction logo change (detected as logo) to normal logo (kabel eins)
// MT_LOGOSTART ( 99981) ->    7720ms -> MT_LOGOSTOP (100174) ->    1120ms -> MT_LOGOSTART (100202) -> 1098840ms -> MT_STOP (127673) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 41139) ->    7760ms -> MT_LOGOSTOP ( 41333) ->    1080ms -> MT_LOGOSTART ( 41360) ->  872280ms -> MT_STOP ( 63167) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 74781) ->    7800ms -> MT_LOGOSTOP ( 74976) ->    1040ms -> MT_LOGOSTART ( 75002) ->  416280ms -> MT_STOP ( 85409) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 63508) ->    7920ms -> MT_LOGOSTOP ( 63706) ->     880ms -> MT_LOGOSTART ( 63728) -> 1204960ms -> MT_STOP ( 93852) -> introdution logo change (kabel eins)
                    if (criteria->IsIntroductionLogoChannel() &&
                            (prevLogoStart_Stop     <=   7920) &&
                            (stop_nextLogoStart     <=   1120) &&
                            (nextLogoStart_nextStop >= 416280)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from introduction logo, deleting", mark->position, nextLogoStart->position);
                        cMark *tmp = nextStop;
                        marks.Del(nextLogoStart);
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }

// invalid stop/start pair from undetected logo change short after logo start mark, delete pair
// MT_LOGOSTART ( 82313) ->    1120ms -> MT_LOGOSTOP ( 82341) ->     640ms -> MT_LOGOSTART ( 82357) ->    1400ms -> MT_STOP ( 82392) -> first of double logo change (TELE 5)
// MT_LOGOSTART ( 87621) ->    1560ms -> MT_LOGOSTOP ( 87660) ->     640ms -> MT_LOGOSTART ( 87676) ->  193000ms -> MT_STOP ( 92501) -> logo change (TELE 5)
// MT_LOGOSTART ( 82313) ->    3160ms -> MT_LOGOSTOP ( 82392) ->     280ms -> MT_LOGOSTART ( 82399) ->  381160ms -> MT_STOP ( 91928) -> second of double logo change (TELE 5)
// MT_LOGOSTART ( 38739) ->    3240ms -> MT_LOGOSTOP ( 38820) ->     640ms -> MT_LOGOSTART ( 38836) -> 1222360ms -> MT_STOP ( 69395) -> logo change (TELE 5)
// MT_LOGOSTART ( 83792) ->    3240ms -> MT_LOGOSTOP ( 83873) ->     640ms -> MT_LOGOSTART ( 83889) ->  349560ms -> MT_STOP ( 92628) -> logo change (TELE 5)
// MT_LOGOSTART (126382) ->    3240ms -> MT_LOGOSTOP (126463) ->     640ms -> MT_LOGOSTART (126479) -> 1303560ms -> MT_STOP (159068) -> logo change (TELE 5)
// MT_LOGOSTART ( 41268) ->    3280ms -> MT_LOGOSTOP ( 41350) ->     640ms -> MT_LOGOSTART ( 41366) -> 1185880ms -> MT_STOP ( 71013) -> logo change (TELE 5)
// MT_LOGOSTART ( 41268) ->    3280ms -> MT_LOGOSTOP ( 41350) ->     640ms -> MT_LOGOSTART ( 41366) -> 1186840ms -> MT_STOP ( 71037) -> logo change (TELE 5)
// MT_LOGOSTART ( 82897) ->    3360ms -> MT_LOGOSTOP ( 82981) ->     640ms -> MT_LOGOSTART ( 82997) ->  283880ms -> MT_STOP ( 90094) -> logo change (TELE 5)
// MT_LOGOSTART ( 40561) ->    3400ms -> MT_LOGOSTOP ( 40646) ->     440ms -> MT_LOGOSTART ( 40657) ->     560ms -> MT_STOP ( 40671) -> first of double logo change (TELE 5)
// MT_LOGOSTART ( 31246) ->    3560ms -> MT_LOGOSTOP ( 31335) ->     280ms -> MT_LOGOSTART ( 31342) ->  936960ms -> MT_STOP ( 54766) -> logo change (TELE 5)
// MT_LOGOSTART ( 43027) ->    3560ms -> MT_LOGOSTOP ( 43116) ->     360ms -> MT_LOGOSTART ( 43125) -> 1180320ms -> MT_STOP ( 72633) -> logo change (TELE 5)
// MT_LOGOSTART ( 38402) ->    4440ms -> MT_LOGOSTOP ( 38513) ->    1120ms -> MT_LOGOSTART ( 38541) -> 1242400ms -> MT_STOP ( 69601) -> logo change (TELE 5)
// MT_LOGOSTART ( 35193) ->    4840ms -> MT_LOGOSTOP ( 35314) ->     760ms -> MT_LOGOSTART ( 35333) ->  919360ms -> MT_STOP ( 58317) -> logo change (TELE 5)
// MT_LOGOSTART ( 43980) ->    6760ms -> MT_LOGOSTOP ( 44149) ->     320ms -> MT_LOGOSTART ( 44157) -> 1129600ms -> MT_STOP ( 72397) -> second of double logo change (TELE 5)
// MT_LOGOSTART ( 43745) ->    7800ms -> MT_LOGOSTOP ( 43940) ->     520ms -> MT_LOGOSTART ( 43953) -> 1165200ms -> MT_STOP ( 73083) -> second of double logo change (TELE 5)
// MT_LOGOSTART ( 40306) ->    8160ms -> MT_LOGOSTOP ( 40510) ->     760ms -> MT_LOGOSTART ( 40529) -> 1216040ms -> MT_STOP ( 70930) -> logo change (TELE 5)
// MT_LOGOSTART ( 43750) ->    8160ms -> MT_LOGOSTOP ( 43954) ->     640ms -> MT_LOGOSTART ( 43970) -> 1187040ms -> MT_STOP ( 73646) -> logo change (TELE 5)
// MT_LOGOSTART ( 49262) ->    8160ms -> MT_LOGOSTOP ( 49466) ->     640ms -> MT_LOGOSTART ( 49482) -> 1196440ms -> MT_STOP ( 79393) -> second of double logo change (TELE 5)
// MT_LOGOSTART ( 49474) ->    8200ms -> MT_LOGOSTOP ( 49679) ->     640ms -> MT_LOGOSTART ( 49695) -> 1163720ms -> MT_STOP ( 78788) -> second of double logo change (TELE 5)
// MT_LOGOSTART ( 82486) ->    8440ms -> MT_LOGOSTOP ( 82697) ->     400ms -> MT_LOGOSTART ( 82707) ->  462880ms -> MT_STOP ( 94279) -> second of double logo change (TELE 5)
                    if (criteria->IsLogoChangeChannel() &&
                            (prevLogoStart_Stop     >= 1120) && (prevLogoStart_Stop     <=    8440) &&
                            (stop_nextLogoStart     >=  280) && (stop_nextLogoStart     <=    1120) &&
                            (nextLogoStart_nextStop >=  560) && (nextLogoStart_nextStop <= 1303560)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from logo change, deleting", mark->position, nextLogoStart->position);
                        cMark *tmp = nextStop;
                        marks.Del(nextLogoStart);
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }
                }
            }
        }
        mark = mark->Next();
    }

// delete short START STOP logo marks because they are previews in the advertisement
// preview detection chain:
// logo start -> logo stop:  broadcast before advertisement or previous preview
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  preview  (this start mark is the current mark in the loop)
//                           first start mark and last stop mark could not be part of a preview
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  broadcast after advertisement or next preview
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): detect previews in advertisement");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (true) {
        mark = mark->Next();   // ignore first mark, this is start of broadcast and can not be start of preview
        if (!mark) break;
// check logo marks
        if (mark->type != MT_LOGOSTART) continue;
        cMark *stopMark = marks.GetNext(mark->position, MT_LOGOSTOP);
        if (!stopMark) continue;
        if (stopMark->position == marks.GetLast()->position) continue; // do not check end mark
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): check logo start (%d) and logo stop (%d) for preview", mark->position, stopMark->position);
        // get marks before and after
        const cMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
        if (!stopBefore) {
            dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_LOGOSTOP before found");
            continue;
        }
        cMark *startAfter = marks.GetNext(stopMark->position, MT_LOGOSTART);
        if (!startAfter) {
            dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_LOGOSTART after found");
            continue;
        }
        cMark *startBefore = marks.GetPrev(stopBefore->position, MT_START, 0x0F);   // start mark can be MT_ASSUMEDSTART
        if (!startBefore) {
            dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_START before found");
            continue;
        }
        const cMark *stopAfter   = marks.GetNext(startAfter->position, MT_STOP, 0x0F);   // end mark can be MT_ASSUMEDSTOP
        if (!stopAfter) {
            dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_STOP after found");
            continue;
        }
        int lengthAdBefore        = 1000 * (mark->position       - stopBefore->position)  / decoder->GetVideoFrameRate();
        int lengthAdAfter         = 1000 * (startAfter->position - stopMark->position)    / decoder->GetVideoFrameRate();
        int lengthPreview         = 1000 * (stopMark->position   - mark->position)        / decoder->GetVideoFrameRate();
        int lengthBroadcastBefore = 1000 * (stopBefore->position - startBefore->position) / decoder->GetVideoFrameRate();
        int lengthBroadcastAfter  = 1000 * (stopAfter->position  - startAfter->position)  / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::CheckMarks(): MT_LOGOSTART (%d) -> %ds -> MT_LOGOSTOP (%d) -> %ds -> [MT_LOGOSTART (%d) -> %dms -> MT_LOGOSTOP (%d)] -> %dms -> MT_LOGOSTART (%d) -> %dms -> MT_STOP (%d)", startBefore->position, lengthBroadcastBefore, stopBefore->position, lengthAdBefore, mark->position, lengthPreview, stopMark->position, lengthAdAfter, startAfter->position, lengthBroadcastAfter, stopAfter->position);
// preview example
// tbd
// no preview example
//                         broadcast before                  ad before                           broadcast                          short ad                           broadcast after
// MT_LOGOSTART (40485) -> 940800s -> MT_LOGOSTOP (64005) -> 612600s -> [MT_LOGOSTART (79320) -> 79800ms -> MT_LOGOSTOP (81315)] -> 12000ms -> MT_LOGOSTART (81615) -> 1365600ms -> MT_STOP (115755)
//
//                          broadcast                           short ad                            broadcast                            short logo interruption           ad in frame with logo
// MT_LOGOSTART (149051) -> 1899920s -> MT_LOGOSTOP (196549) -> 11440s -> [MT_LOGOSTART (196835) -> 132360ms -> MT_LOGOSTOP (200144)] -> 200ms -> MT_LOGOSTART (200149) -> 19960ms -> MT_LOGOSTOP (200648)

        if ((lengthAdBefore >= 800) || (lengthAdAfter >= 360)) {  // check if we have ad before or after preview. if not it is a logo detection failure
            if (((lengthAdBefore >= 120) && (lengthAdBefore <= 585000) && (lengthAdAfter >= 200))) { // if advertising before is long this is the next start mark
                if (lengthPreview < 132360) {
                    // check if this logo stop and next logo start are closing credits, in this case stop mark is valid
                    bool isNextClosingCredits = false;
                    if (criteria->IsClosingCreditsChannel()) isNextClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCreditsAfter(startAfter->position) == STATUS_YES);
                    if (!isNextClosingCredits || (stopMark->position != marks.GetLast()->position)) { // check valid only for last mark
                        // check if this logo start mark and previuos logo stop mark are closing credits with logo, in this case logo start mark is valid
                        // this only work on the first logo stark mark because there are closing credits in previews
                        bool isThisClosingCredits = false;
                        if (criteria->IsClosingCreditsChannel()) isThisClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCreditsAfter(mark->position) == STATUS_YES);
                        if (!isThisClosingCredits || (stopMark->position != marks.GetFirst()->position)) {
                            isyslog("found preview between logo start mark (%d) and logo stop mark (%d) in advertisement, deleting marks", mark->position, stopMark->position);
                            cMark *tmp = startBefore;  // continue with mark before
                            marks.Del(mark);
                            marks.Del(stopMark);
                            mark = tmp;
                        }
                        else dsyslog("cMarkAdStandalone::CheckMarks(): long advertisement before and logo stop before and this logo start mark are closing credits, this pair contains a start mark");
                    }
                    else dsyslog("cMarkAdStandalone::CheckMarks(): next stop/start pair are closing credits, this pair contains a stop mark");
                }
                else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%d) and (%d), length %dms not valid", mark->position, mark->Next()->position, lengthPreview);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%d) and (%d), length advertising before %ds or after %dms is not valid", mark->position, mark->Next()->position, lengthAdBefore, lengthAdAfter);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): not long enough ad before and after preview, maybe logo detection failure");
    }

// delete invalid short START STOP hborder marks
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): check border marks");
    DebugMarks();     //  only for debugging

// delete late first vborder start mark, they are start of next recording
    cMark *borderStart = marks.GetNext(0, MT_VBORDERSTART);
    if ((borderStart) && (marks.Count(MT_VBORDERCHANGE, 0xF0) == 1)) {
        int diffEnd = (borderStart->position - newStopA) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::CheckMarks(): late single vborder start mark (%d) %ds after assumed stop (%d), this is start of next broadcast", borderStart->position, diffEnd, newStopA);
        if (diffEnd >= -156) { // maybe reported recording length is too big
            dsyslog("cMarkAdStandalone::CheckMarks(): delete all marks from (%d) to end", borderStart->position);
            marks.DelAfterFromToEnd(borderStart->position - 1);
        }
    }


// delete start/stop hborder pairs before frameCheckStart if there are no other hborder marks, they are a preview with hborder before recording start
    mark = marks.GetFirst();
    if (mark && (mark->type == MT_HBORDERSTART) && (marks.Count(MT_HBORDERSTART) == 1)) {
        cMark *markNext = mark->Next();
        if (markNext && (markNext->type == MT_HBORDERSTOP) && (markNext->position < frameCheckStart) && (markNext->position != marks.GetLast()->position) && (marks.Count(MT_HBORDERSTOP) == 1)) {
            dsyslog("cMarkAdStandalone::CheckMarks(): preview with hborder before recording start found, delete start (%d) stop (%d)", mark->position, mark->Next()->position);
            marks.Del(markNext->position);
            marks.Del(mark->position);
        }
    }
// delete short START STOP hborder marks with logo start mark between, because they are advertisement with border in the advertisement
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_HBORDERSTART) && (mark->position != marks.GetFirst()->position) && mark->Next()) {  // not start or end mark
            cMark *bStop = marks.GetNext(mark->position, MT_HBORDERSTOP);
            if (bStop && (bStop->position != marks.GetLast()->position)) { // next mark not end mark
                int lengthAd = static_cast<int> ((bStop->position - mark->position) / decoder->GetVideoFrameRate());
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

// check for short start/stop pairs at the start part
    CheckStartMark();


// if we have a VPS events, move start and stop mark to VPS event
    LogSeparator();
    if (macontext.Config->useVPS) {
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): apply VPS events");
        DebugMarks();     //  only for debugging
        int vpsOffset = vps->GetStart(); // VPS start mark
        if (vpsOffset >= 0) {
            isyslog("VPS start event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_START, false);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS start event found");

        vpsOffset = vps->GetPauseStart();     // VPS pause start mark = stop mark
        if (vpsOffset >= 0) {
            isyslog("VPS pause start event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_STOP, true);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause start event found");

        vpsOffset = vps->GetPauseStop();     // VPS pause stop mark = start mark
        if (vpsOffset >= 0) {
            isyslog("VPS pause stop  event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_START, true);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause stop event found");

        vpsOffset = vps->GetStop();     // VPS stop mark
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

// check for better end mark not very far away from assuemd end
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): check for final end mark in case of recording length is too big");
    DebugMarks();     //  only for debugging

// get last 3 marks

    // new version
    bool moreMarks = true;
    while (moreMarks) {
        if (abortNow) return;
        cMark *lastStopMark = marks.GetLast();
        if (!lastStopMark) break;
        if ((lastStopMark->type & 0x0F) != MT_STOP) break;
        if ((lastStopMark->type & 0xF0) >= MT_CHANNELCHANGE) break;  // trust channel marks and better
        if ((lastStopMark->type & 0xF0) == MT_MOVED) break;          // trust channel moved marks
        cMark *lastStartMark = marks.GetPrev(lastStopMark->position);
        if (!lastStartMark) break;
        if ((lastStartMark->type & 0x0F) != MT_START) break;
        cMark *prevStopMark = marks.GetPrev(lastStartMark->position);
        if (!prevStopMark) break;
        if ((prevStopMark->type & 0x0F) != MT_STOP) break;
        cMark *prevStartMark = marks.GetPrev(prevStopMark->position);
        if (!prevStartMark) break;
        if ((prevStartMark->type & 0x0F) != MT_START) break;
        int lastBroadcast        = (lastStopMark->position  - lastStartMark->position) / decoder->GetVideoFrameRate();
        int prevBroadcast        = (prevStopMark->position  - prevStartMark->position) / decoder->GetVideoFrameRate();
        int diffLastStopAssumed  = (lastStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
        int diffLastStartAssumed = (lastStartMark->position - newStopA)                / decoder->GetVideoFrameRate();
        int diffPrevStopAssumed  = (prevStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
        int lastAd               = (lastStartMark->position - prevStopMark->position)  / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::CheckMarks(): MT_START (%5d) -> %3ds -> MT_STOP (%5d) -> %3ds -> MT_START (%5d) -> %3ds -> MT_STOP (%5d)", prevStartMark->position, prevBroadcast, prevStopMark->position, lastAd, lastStartMark->position, lastBroadcast, lastStopMark->position);
        dsyslog("cMarkAdStandalone::CheckMarks(): end mark          (%5d) %4ds after assumed end (%5d)", lastStopMark->position,  diffLastStopAssumed,  newStopA);
        dsyslog("cMarkAdStandalone::CheckMarks(): start mark before (%5d) %4ds after assumed end (%5d)", lastStartMark->position, diffLastStartAssumed, newStopA);
        dsyslog("cMarkAdStandalone::CheckMarks(): stop  mark before (%5d) %4ds after assumed end (%5d)", prevStopMark->position,  diffPrevStopAssumed,  newStopA);
        switch(lastStopMark->type) {
        case MT_LOGOSTOP:
            // example of invalid log stop mark sequence
            // MT_START (73336) -> 960s -> MT_STOP (97358) -> 18s -> MT_START (97817) -> 83s -> MT_STOP (99916)
            // MT_START (97756) ->   0s -> MT_STOP (97761) ->  0s -> MT_START (97766) -> 86s -> MT_STOP (99916)   -> more than one false logo stop
            // MT_START (97460) ->   0s -> MT_STOP (97465) ->  0s -> MT_START (97469) -> 86s -> MT_STOP (99619)   -> more than one false logo stop
            if ((lastAd <= 18) && (lastBroadcast <= 86)) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, assume too big recording length", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
            }
            else moreMarks = false;
            break;
        default:
            moreMarks = false;
            break;
        }
    }

// old version TODO migrate to new version
    cMark * lastStopMark = marks.GetLast();
    if (lastStopMark && ((lastStopMark->type & 0x0F) == MT_STOP)) {
        cMark *lastStartMark = marks.GetPrev(lastStopMark->position);
        if (lastStartMark && ((lastStartMark->type & 0x0F) == MT_START)) {
            cMark *prevStopMark = marks.GetPrev(lastStartMark->position);
            if (prevStopMark && ((prevStopMark->type & 0x0F) == MT_STOP)) {
                cMark *prevStartMark = marks.GetPrev(prevStopMark->position);
                if (prevStartMark && ((prevStartMark->type & 0x0F) == MT_START)) {
                    int lastBroadcast        = (lastStopMark->position  - lastStartMark->position) / decoder->GetVideoFrameRate();
                    int prevBroadcast        = (prevStopMark->position  - prevStartMark->position) / decoder->GetVideoFrameRate();
                    int diffLastStopAssumed  = (lastStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
                    int diffLastStartAssumed = (lastStartMark->position - newStopA)                / decoder->GetVideoFrameRate();
                    int diffPrevStopAssumed  = (prevStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
                    int lastAd               = (lastStartMark->position - prevStopMark->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::CheckMarks(): MT_START (%d) -> %ds -> MT_STOP (%d) -> %ds -> MT_START (%d) -> %ds -> MT_STOP (%d)", prevStartMark->position, prevBroadcast, prevStopMark->position, lastAd, lastStartMark->position, lastBroadcast, lastStopMark->position);
                    dsyslog("cMarkAdStandalone::CheckMarks(): end mark          (%5d) %4ds after assumed end (%5d)", lastStopMark->position, diffLastStopAssumed, newStopA);
                    dsyslog("cMarkAdStandalone::CheckMarks(): start mark before (%5d) %4ds after assumed end (%5d)", lastStartMark->position, diffLastStartAssumed, newStopA);
                    dsyslog("cMarkAdStandalone::CheckMarks(): stop  mark before (%5d) %4ds after assumed end (%5d)", prevStopMark->position,  diffPrevStopAssumed, newStopA);

                    // check length of last broadcast and distance to assumed end
                    if (((lastStopMark->type & 0xF0) < MT_CHANNELCHANGE) || ((lastStopMark->type & 0xF0) == MT_MOVED)) {  // trust channel marks and better
                        bool deleted = false;
                        int minLastStopAssumed  = 0;  // trusted distance to assumed stop depents on hardness of marks
                        int minLastStartAssumed = 0;
                        int minPrevStopAssumed  = 0;
                        int minLastBroadcast    = 0;
                        int minLastAd           = 0;  // very short lst ad is not in broadcast, this is between broadcast and next broadcast
                        switch(lastStopMark->type) {
                        case MT_ASSUMEDSTOP:
                            // too long broadcast length from info file, delete last stop:
                            //   0 / -172 / -536 NEW
                            //   0 / -184 / -631
                            //   0 / -231 / -355 (conflict)
                            // correct end mark, do not delete last stop
                            //   0 / -220 / -353
                            //   0 / -230 / -581
                            //   0 / -273 / -284
                            minLastStopAssumed  =    0;
                            minLastStartAssumed = -184;
                            minPrevStopAssumed  = -631;
                            minLastBroadcast    =  141;  // changed from 129 to 141
                            minLastAd           =   46;
                            break;
                        case MT_NOBLACKSTOP:
                            // too long broadcast length from info file, delete last stop:
                            //  73 / -173 / -536
                            //  93 / -278 / -541
                            // 102 /  -98 / -515
                            // 102 / -225 / -535
                            // 123 / -189 / -500
                            // 123 / -384 / -539
                            // 147 /  -60 / -535
                            // 169 / -226 / -537
                            // 187 / -225 / -535
                            // 209 /  -76 / -535
                            // 416 /  -95 / -538
                            // correct end mark, do not delete last stop
                            // 154 / -217 / -510  (conflict)
                            //  73 / -384 / -510  (conflict)
                            //  68 / -124 / -534
                            //  82 /  -78 / -504
                            minLastStopAssumed  =   73;
                            minLastStartAssumed = -384;
                            minPrevStopAssumed  = -541;
                            minLastBroadcast    =  169;  // changed from 65 to 169
                            break;
                        case MT_VBORDERSTOP:
                            minLastStopAssumed  =  288;
                            minLastStartAssumed =   56;
                            minPrevStopAssumed  = -477;
                            minLastBroadcast    =    0;
                            break;
                        case MT_MOVEDSTOP:
                            minLastStopAssumed  = 1000;  // do nothing
                            minLastStartAssumed = 1000;
                            minPrevStopAssumed  = 1000;
                            minLastBroadcast    =    5;
                            break;
                        default:
                            minLastStopAssumed  = 1000;  // do nothing
                            minLastStartAssumed = 1000;
                            minPrevStopAssumed  = 1000;
                            minLastBroadcast    =    0;
                        }
                        if (!deleted) {
                            dsyslog("cMarkAdStandalone::CheckMarks(): select previous stop if: end mark        >= %4ds after assumed end (%d)", minLastStopAssumed, newStopA);
                            dsyslog("cMarkAdStandalone::CheckMarks():                          last start mark >= %4ds after assumed end (%d)", minLastStartAssumed, newStopA);
                            dsyslog("cMarkAdStandalone::CheckMarks():                          last stop  mark >= %4ds after assumed end (%d)", minPrevStopAssumed, newStopA);
                            // check end sequence
                            if ((diffLastStopAssumed >= minLastStopAssumed) && (diffLastStartAssumed >= minLastStartAssumed) && (diffPrevStopAssumed >= minPrevStopAssumed)) {
                                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, assume too big recording length", prevStopMark->position);
                                marks.Del(lastStopMark->position);
                                marks.Del(lastStartMark->position);
                            }
                            else {
                                // very short last broadcast is preview after broadcast
                                dsyslog("cMarkAdStandalone::CheckMarks(): min length of last broadcast < %4ds", minLastBroadcast);
                                if (lastBroadcast < minLastBroadcast) {
                                    dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last broadcast too short", prevStopMark->position);
                                    marks.Del(lastStopMark->position);
                                    marks.Del(lastStartMark->position);
                                }
                                else {
                                    // very short last ad is not in broadcast, this is between broadcast and next broadcast
                                    dsyslog("cMarkAdStandalone::CheckMarks(): min length of last ad < %4ds", minLastAd);
                                    if ((lastAd < minLastAd) && (lastStopMark->position >= newStopA) && (prevBroadcast >= 60)) { // prevent to cut off after logo detection failure
                                        dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last ad too short", prevStopMark->position);
                                        marks.Del(lastStopMark->position);
                                        marks.Del(lastStartMark->position);
                                    }
                                }
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
            int diff = 1000 * (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
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
            int MARKDIFF = static_cast<int> (decoder->GetVideoFrameRate() * 38); // changed from 8 to 18 to 35 to 38
            double distance = (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
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

        // check for logo detection failure, delete if logo stop/start pair is too short for an ad
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int lengthAd = 1000 * (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::CheckMarks(): mark distance between logo stop (%d) and logo start (%d), length advertising %dms", mark->position, mark->Next()->position, lengthAd);
            if (lengthAd < 13040) {  // found shortest ad in broadcast with 13040ms, between two broadcast the can be very short
                isyslog("mark distance %ds too short, deleting logo stop mark (%d) and logo start mark (%d)", lengthAd / 1000, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        if ((mark->type == MT_HBORDERSTOP) && mark->Next() && mark->Next()->type == MT_HBORDERSTART) {
            int MARKDIFF = static_cast<int> (decoder->GetVideoFrameRate() * 20);  // increased from 15 to 20
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
                isyslog("mark distance between horizontal STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                cMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        if ((mark->type == MT_VBORDERSTOP) && mark->Next() && mark->Next()->type == MT_VBORDERSTART) {
            int MARKDIFF = static_cast<int> (decoder->GetVideoFrameRate() * 2);
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / decoder->GetVideoFrameRate();
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

    marks.DelInvalidSequence();

    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): final marks:");
    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type, const bool isPause) {
    if (!decoder) return;
    int delta = decoder->GetVideoFrameRate() * 120;
    int vpsFrame = index->GetFrameFromOffset(offset * 1000);
    if (vpsFrame < 0) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): failed to get frame from offset %d", offset);
    }
    cMark *mark = nullptr;
    char *comment = nullptr;
    char *timeText = nullptr;
    if (!isPause) {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame, false);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF) + 1, "indexToHMSF");
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, (indexToHMSF) ? indexToHMSF : "unknown");
        if (indexToHMSF) {
            FREE(strlen(indexToHMSF) + 1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame, false);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, (indexToHMSF) ? indexToHMSF : "unknown");
        if (indexToHMSF) {
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark = ((type == MT_START)) ? marks.GetAround(delta, vpsFrame, MT_START, 0x0F) :  marks.GetAround(delta, vpsFrame, MT_STOP, 0x0F);
    }
    if (!mark) {
        if (isPause) {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): no mark found to replace with pause mark, add new mark");
            if (asprintf(&comment,"VPS %s (%d)%s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, (type == MT_START) ? "*" : "") == -1) comment = nullptr;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, MT_UNDEFINED, MT_UNDEFINED, vpsFrame, comment);
            FREE(strlen(comment)+1,"comment");
            free(comment);
            return;
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found no mark found to replace");
        return;
    }
    if ( (type & 0x0F) != (mark->type & 0x0F)) return;

    timeText = marks.GetTime(mark);
    if (timeText) {
        if ((((mark->type & 0xF0) >= MT_LOGOCHANGE) || (mark->type == MT_RECORDINGSTART)) &&  // keep strong marks, they are better than VPS marks
                ((mark->type != MT_TYPECHANGESTOP) || !criteria->GoodVPS())) { // keep broadcast start from next recording if no VPS event expected
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep mark at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
        }
        else { // replace weak marks
            int diff = abs(mark->position - vpsFrame) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s, %ds after mark", mark->position, mark->type, timeText, diff);
            if (abs(diff) < 1225) {  // do not replace very far marks, there can be an invalid VPS events
                dsyslog("cMarkAdStandalone::AddMarkVPS(): move mark on position (%d) to VPS event position (%d)", mark->position, vpsFrame);
                // remove marks witch will become invalid after applying VPS event
                switch (type) {
                case MT_START:
                    marks.DelFromTo(0, mark->position - 1, MT_ALL, 0xFF);
                    break;
                case MT_STOP: {  // delete all marks between stop mark before VPS stop event (included) and current end mark (not included)
                    int delStart = vpsFrame;
                    const cMark *prevMark = marks.GetPrev(vpsFrame);
                    if (prevMark && ((prevMark->type & 0x0F) == MT_STOP)) delStart = prevMark->position;
                    marks.DelFromTo(delStart, mark->position - 1, MT_ALL, 0xFF);
                }
                break;
                default:
                    esyslog("cMarkAdStandalone::AddMarkVPS(): invalid type 0x%X", type);
                }
                dsyslog("cMarkAdStandalone::AddMarkVPS(): marks after cleanup:");
                DebugMarks();     //  only for debugging
                marks.Move(mark, vpsFrame, (type == MT_START) ? MT_VPSSTART : MT_VPSSTOP);
            }
            else dsyslog("cMarkAdStandalone::AddMarkVPS(): VPS event too far from mark, ignoring");
        }
    }
}


void cMarkAdStandalone::AddMark(sMarkAdMark *mark) {
    if (!mark) return;
    if (!mark->type) return;
    if (mark->type <= MT_UNDEFINED) {
        esyslog("cMarkAdStandalone::AddMark(): mark type 0x%X invalid", mark->type);
        return;
    }
    if (mark->position < 0) {
        esyslog("cMarkAdStandalone::AddMark(): mark position (%d) invalid, type 0x%X", mark->position, mark->type);
        return;
    }

    // set comment of the new mark
    char *comment = nullptr;
    switch (mark->type) {
    case MT_ASSUMEDSTART:
        if (asprintf(&comment, "start assumed (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_ASSUMEDSTOP:
        if (asprintf(&comment, "end   assumed (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SCENESTART:
        if (asprintf(&comment, "start scene (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SOUNDSTART:
        if (asprintf(&comment, "end   silence (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SOUNDSTOP:
        if (asprintf(&comment, "start silence (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SCENESTOP:
        if (asprintf(&comment, "end   scene (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOLOWERBORDERSTART:
        if (asprintf(&comment, "end   lower border (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOLOWERBORDERSTOP:
        if (asprintf(&comment, "start lower border (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKSTART:
        if (asprintf(&comment, "end   black screen (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKSTOP:
        if (asprintf(&comment, "start black screen (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_LOGOSTART:
        if (asprintf(&comment, "start logo (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_LOGOSTOP:
        if (asprintf(&comment, "stop  logo (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_HBORDERSTART:
        if (asprintf(&comment, "start horiz. borders (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_HBORDERSTOP:
        if (asprintf(&comment, "stop  horiz. borders (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_VBORDERSTART:
        if (asprintf(&comment, "start vert. borders (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_VBORDERSTOP:
        if (asprintf(&comment, "stop  vert. borders (%d) ", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_ASPECTSTART:
        if ((mark->AspectRatioBefore.num == 0) || (mark->AspectRatioBefore.den == 0)) {
            if (asprintf(&comment, "start recording with aspect ratio %2d:%d (%d)*", mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = nullptr;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
        }
        else {
            if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%d)*", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = nullptr;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (criteria->GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
                isyslog("AddMark(): frame (%d): aspect ratio change from %2d:%d to %2d:%d, logo detection reenabled", mark->position, mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den);
                criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
            }
        }
        break;
    case MT_ASPECTSTOP:
        if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%d) ", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (criteria->GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
            isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
            criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
        }
        break;
    case MT_CHANNELSTART:
        if (asprintf(&comment, "audio channel change from %d to %d (%d)*", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        if ((marks.Count(MT_CHANNELSTART, 0xFF) == 0) && (mark->position > stopA / 2)) {
            dsyslog("AddMark(): first channel start at frame (%d) after half of assumed recording length at frame (%d), this is start mark of next braoscast", mark->position, stopA / 2);
        }
        break;
    case MT_CHANNELSTOP:
        if ((mark->position > frameCheckStart) && (mark->position < stopA * 2 / 3) && (marks.Count(MT_CHANNELSTART, 0xFF) == 0)) {
            dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after frameCheckStart, disable video decoding detection now");
            // disable all video detection
            video->ClearBorder();
            // use now channel change for detection
            criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
            if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
                criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE);
                cMark *hborderStop = marks.GetAround(1 * decoder->GetVideoFrameRate(), mark->position, MT_HBORDERSTOP);
                if (hborderStop) {  // use hborder stop, we have no scene change or black screen to optimize channel stop mark, will result false optimization
                    dsyslog("cMarkAdStandalone::AddMark(): keep hborder stop (%d), ignore channel stop (%d)", hborderStop->position, mark->position);
                    return;
                }
            }
            if (doneCheckStart) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we have selected a start mark
        }
        if (asprintf(&comment, "audio channel change from %d to %d (%d) ", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_RECORDINGSTART:
        if (asprintf(&comment, "start of recording (%d)*", mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_RECORDINGSTOP:
        if (asprintf(&comment, "stop of recording (%d) ",mark->position) == -1) comment = nullptr;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    default:
        esyslog("cMarkAdStandalone::AddMark(): unknown mark type 0x%X", mark->type);
        return;
    }

    // add weak marks only to separate marks object
    switch (mark->type & 0xF0) {
    case MT_SCENECHANGE:
        sceneMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, nullptr, inBroadCast);
        if (comment) {
#ifdef DEBUG_WEAK_MARKS
            char *indexToHMSF = marks.IndexToHMSF(mark->position, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                if (indexToHMSF) {
                    dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
            }
#endif
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
        break;
    case MT_SOUNDCHANGE:
        silenceMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, nullptr, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
        break;
    case MT_LOWERBORDERCHANGE:
    case MT_BLACKCHANGE:
        blackMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, nullptr, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
        break;
    default:
        // set inBroadCast status
        if (!((mark->type <= MT_ASPECTSTART) && (marks.GetPrev(mark->position, MT_CHANNELSTOP) && marks.GetPrev(mark->position, MT_CHANNELSTART)))) { // if there are MT_CHANNELSTOP and MT_CHANNELSTART marks, wait for MT_CHANNELSTART
            if ((mark->type & 0x0F) == MT_START) inBroadCast = true;
            else                                 inBroadCast = false;
        }
        // add mark
        char *indexToHMSF = nullptr;
        if (mark->type == MT_RECORDINGSTART) {
            if (asprintf(&indexToHMSF, "00:00:00.00") == -1) esyslog("cMarkAdStandalone::AddMark(): asprintf failed");  // we have no index to get time for position 0
        }
        else indexToHMSF = marks.IndexToHMSF(mark->position, false);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            if (comment) isyslog("%s at %s", comment, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        marks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, comment, inBroadCast);
        if (comment) {
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
        if (doneCheckStart) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);  // save after start mark is valid
    }
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
        if (pFile == nullptr) {
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
    if (sleepcnt >= 2) {
        dsyslog("slept too much");
        return; // we already slept too much
    }
    if (decoder) framecnt = decoder->GetFrameNumber();
    bool notenough = true;
    do {
        struct stat statbuf;
        if (stat(indexFile,&statbuf) == -1) {
            return;
        }

        int maxframes = statbuf.st_size / 8;
        if (maxframes < (framecnt + 200)) {
            if ((difftime(time(nullptr), statbuf.st_mtime)) >= WAITTIME) {
                if (length && startTime) {
                    time_t endRecording = startTime + (time_t) length;
                    if (time(nullptr) > endRecording) {
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
            time_t sleepstart = time(nullptr);
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
                slepttime = difftime(time(nullptr), sleepstart);
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


#ifdef DEBUG_MARK_FRAMES
void cMarkAdStandalone::DebugMarkFrames() {
    if (!decoder) return;

    LogSeparator(true);
    decoder->Restart();   // restart decoder with first frame
    cMark *mark = marks.GetFirst();
    if (!mark) return;

    // if no fullDecode, check if all marks are on i-frame position
    if (!macontext.Config->fullDecode) {
        while (mark) {
            if (abortNow) return;
            if (mark->position != index->GetIFrameBefore(mark->position)) {
                esyslog("cMarkAdStandalone::DebugMarkFrames(): mark at (%d) type 0x%X is not an iFrame position", mark->position, mark->type);
                dsyslog("cMarkAdStandalone::DebugMarkFrames(): frame (%d): i-frame before (%d)", mark->position, index->GetIFrameBefore(mark->position));
            }
            mark=mark->Next();
        }
    }

    mark = marks.GetFirst();
    int oldFrameNumber = -1;

    // read and decode all video frames, we want to be sure we have a valid decoder state, this is a debug function, we don't care about performance
    while (decoder->DecodeNextFrame(false)) {  // no audio
        if (abortNow) return;
        int frameNumber = decoder->GetFrameNumber();
        int frameDistance = 1;
        if (!macontext.Config->fullDecode) frameDistance = frameNumber - oldFrameNumber;  // get distance between to frame numbers
        if (frameNumber >= (mark->position - (frameDistance * DEBUG_MARK_FRAMES))) {
            dsyslog("cMarkAdStandalone::DebugMarkFrames(): mark frame (%5d) type 0x%X, write frame (%5d)", mark->position, mark->type, frameNumber);
            char suffix1[10] = "";
            char suffix2[10] = "";
            if ((mark->type & 0x0F) == MT_START) strcpy(suffix1, "START");
            if ((mark->type & 0x0F) == MT_STOP)  strcpy(suffix1, "STOP");

            if (frameNumber < mark->position)    strcpy(suffix2, "BEFORE");
            if ((macontext.Config->fullDecode)  && (frameNumber > mark->position))     strcpy(suffix2, "AFTER");
            if ((!macontext.Config->fullDecode) && (frameNumber > mark->position + 1)) strcpy(suffix2, "AFTER");  // for interlaced stream we will get the picture after the iFrame
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d_%s_%s.pgm", macontext.Config->recDir, frameNumber, suffix1, suffix2) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
            if (frameNumber >= (mark->position + (frameDistance * DEBUG_MARK_FRAMES))) {
                mark = mark->Next();
                if (!mark) break;
            }
        }
        oldFrameNumber = frameNumber;
    }
}
#endif


void cMarkAdStandalone::MarkadCut() {
    if (!decoder) {
        esyslog("cMarkAdStandalone::MarkadCut(): decoder not set");
        return;
    }
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::MarkadCut():cut video based on marks: fullDecode = %d, fullEncode = %d", macontext.Config->fullDecode, macontext.Config->fullEncode);

    if (macontext.Config->fullEncode && !macontext.Config->fullDecode) {
        dsyslog("full encode needs full decode, activate it");
        decoder->SetFullDecode(true);
    }

    if (marks.Count() < 2) {
        esyslog("need at least one start mark and one stop mark to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): final marks are:");
    DebugMarks();     //  only for debugging

    // init encoder
    cEncoder *encoder = new cEncoder(decoder, index, macontext.Config->recDir, macontext.Config->fullEncode, macontext.Config->bestEncode, macontext.Config->ac3ReEncode);
    ALLOC(sizeof(*encoder), "encoder");

    int passMin = 0;
    int passMax = 0;
    if (macontext.Config->fullEncode) {  // to full endcode we need 2 pass full encoding
        passMin = 1;
        passMax = 2;
    }

    for (int pass = passMin; pass <= passMax; pass ++) {
        dsyslog("cMarkAdStandalone::MarkadCut(): reset decoder for pass: %d", pass);
        decoder->Restart();
        encoder->Reset(pass);

        // set start and end mark of first part
        cMark *startMark = marks.GetFirst();
        if ((startMark->type & 0x0F) != MT_START) {
            esyslog("got invalid start mark at (%d) type 0x%X", startMark->position, startMark->type);
            return;
        }
        cMark *stopMark = startMark->Next();
        if ((stopMark->type & 0x0F) != MT_STOP) {
            esyslog("got invalid stop mark at (%d) type 0x%X", stopMark->position, stopMark->type);
            return;
        }
        // open output file
        if (!encoder->OpenFile()) {
            esyslog("failed to open output file");
            FREE(sizeof(*encoder), "encoder");
            delete encoder;
            encoder = nullptr;
            return;
        }

        // cut out all start/stop pairs
        while (true) {
            if (abortNow) return;
            if (!encoder->CutOut(startMark->position, stopMark->position)) break;

            // next start/stop pair
            if (stopMark->Next() && stopMark->Next()->Next()) {  // next mark pair
                startMark = stopMark->Next();
                if ((startMark->type & 0x0F) != MT_START) {
                    esyslog("got invalid start mark at (%d) type 0x%X", startMark->position, startMark->type);
                    break;
                }
                stopMark = startMark->Next();
                if ((stopMark->type & 0x0F) != MT_STOP) {
                    esyslog("got invalid stop mark at (%d) type 0x%X", stopMark->position, stopMark->type);
                    break;
                }
            }
            else break;
        }
        // close file to write index in pass 1
        if (!encoder->CloseFile()) {
            esyslog("failed to close output file");
            return;
        }
    }
    FREE(sizeof(*encoder), "encoder");
    delete encoder;  // encoder must be valid here because it is used above
    encoder = nullptr;

    dsyslog("cMarkAdStandalone::MarkadCut(): end at frame %d", decoder->GetFrameNumber());
}


// logo mark optimization
// do it with all mark types, because even with channel marks from a double episode, logo marks can be the only valid end mark type
// - move logo marks before intrudiction logo
// - move logo marks before/after ad in frame
// - remove stop/start from info logo
//
void cMarkAdStandalone::LogoMarkOptimization() {
    if (!decoder)                   return;
    if (!index)                     return;
    if (!criteria)                  return;

    if (marks.Count(MT_LOGOCHANGE, 0xF0) == 0) {
        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no logo marks used");
        return;
    }

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): start logo mark optimization");
    if (!evaluateLogoStopStartPair) {  // init in RemoveLogoChangeMarks(), but maybe not used
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }

    // init objects for logo mark optimization
    if (!detectLogoStopStart) {        // init in RemoveLogoChangeMarks(), but maybe not used
        detectLogoStopStart = new cDetectLogoStopStart(decoder, index, criteria, evaluateLogoStopStartPair, video->GetLogoCorner());
        ALLOC(sizeof(*detectLogoStopStart), "detectLogoStopStart");
    }

    decoder->Restart();
    bool save = false;

// check for advertising in frame with logo after logo start mark and before logo stop mark and check for introduction logo
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): check for advertising in frame with logo after logo start and before logo stop mark and check for introduction logo");
    cMark *markLogo = marks.GetFirst();
    while (markLogo) {
        if (markLogo->type == MT_LOGOSTART) {

            const char *indexToHMSFStartMark = marks.GetTime(markLogo);
            int introductionStartPosition = -1;

            // check for introduction logo before logo mark position
            if (criteria->IsIntroductionLogoChannel()) {
                LogSeparator(false);
                int searchStartPosition = markLogo->position - (30 * decoder->GetVideoFrameRate()); // introduction logos are usually 10s, somettimes longer, changed from 12 to 30
                if (searchStartPosition < 0) searchStartPosition = 0;
                cMark *stopBefore = marks.GetPrev(markLogo->position, MT_STOP, 0x0F);
                if (stopBefore && (stopBefore->position > searchStartPosition)) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): move start of search from (%d) to stop mark before (%d)", searchStartPosition, stopBefore->position);
                    searchStartPosition = stopBefore->position;
                }

                char *indexToHMSFSearchStart = marks.IndexToHMSF(searchStartPosition, false);
                if (indexToHMSFSearchStart) {
                    ALLOC(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
                }

                if (indexToHMSFStartMark && indexToHMSFSearchStart) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search introduction logo from position (%d) at %s to logo start mark (%d) at %s", searchStartPosition, indexToHMSFSearchStart, markLogo->position, indexToHMSFStartMark);
                if (indexToHMSFSearchStart) {
                    FREE(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
                    free(indexToHMSFSearchStart);
                }
                if (detectLogoStopStart->Detect(searchStartPosition, markLogo->position)) {
                    introductionStartPosition = detectLogoStopStart->IntroductionLogo(searchStartPosition, markLogo->position);
                }
            }

            // check for advertising in frame with logo after logo start mark position
            if (criteria->IsAdInFrameWithLogoChannel()) {
                int adInFrameEndPosition = -1;
                LogSeparator(false);
                int searchEndPosition = markLogo->position + (60 * decoder->GetVideoFrameRate()); // advertising in frame are usually 30s
                // sometimes advertising in frame has text in "e.g. Werbung"
                // check longer range to prevent to detect text as second logo
                // changed from 35 to 60

                char *indexToHMSFSearchEnd = marks.IndexToHMSF(searchEndPosition, false);
                if (indexToHMSFSearchEnd) {
                    ALLOC(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
                }
                if (indexToHMSFStartMark && indexToHMSFSearchEnd) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo after logo start mark (%d) at %s to position (%d) at %s", markLogo->position, indexToHMSFStartMark, searchEndPosition, indexToHMSFSearchEnd);
                if (indexToHMSFSearchEnd) {
                    FREE(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
                    free(indexToHMSFSearchEnd);
                }
                if (detectLogoStopStart->Detect(markLogo->position, searchEndPosition)) {
                    adInFrameEndPosition = detectLogoStopStart->AdInFrameWithLogo(markLogo->position, searchEndPosition, true, false);
                }
                if (adInFrameEndPosition >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) found", markLogo->position, adInFrameEndPosition);
                    if (evaluateLogoStopStartPair->IncludesInfoLogo(markLogo->position, adInFrameEndPosition)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        adInFrameEndPosition = -1;
                    }
                }
                if (adInFrameEndPosition != -1) {  // if we found advertising in frame, use this
                    if (!macontext.Config->fullDecode) adInFrameEndPosition = index->GetIFrameAfter(adInFrameEndPosition + 1);  // we got last frame of ad, go to next iFrame for start mark
                    else adInFrameEndPosition++; // use next frame after ad in frame as start mark
                    markLogo = marks.Move(markLogo, adInFrameEndPosition, MT_NOADINFRAMESTART);
                    if (!markLogo) {
                        esyslog("cMarkAdStandalone::LogoMarkOptimization(): move mark failed");
                        break;
                    }
                    save = true;
                }
                else {
                    if (introductionStartPosition != -1) {
                        bool move = true;
                        // check blackscreen between introduction logo start and logo start, there should be no long blackscreen, short blackscreen are from retrospect
                        cMark *blackMarkStart = blackMarks.GetNext(introductionStartPosition, MT_NOBLACKSTART);
                        cMark *blackMarkStop = blackMarks.GetNext(introductionStartPosition, MT_NOBLACKSTOP);
                        if (blackMarkStart && blackMarkStop && (blackMarkStart->position <= markLogo->position) && (blackMarkStop->position <= markLogo->position)) {
                            int innerLength = 1000 * (blackMarkStart->position - blackMarkStop->position) / decoder->GetVideoFrameRate();
                            dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen start (%d) and stop (%d) between introduction logo (%d) and start mark (%d), length %dms", blackMarkStop->position, blackMarkStart->position, introductionStartPosition, markLogo->position, innerLength);
                            if (innerLength > 1000) move = false;  // only move if we found no long blackscreen between introduction logo and logo start
                        }
                        if (move) markLogo = marks.Move(markLogo, introductionStartPosition, MT_INTRODUCTIONSTART);
                        if (!markLogo) {
                            esyslog("cMarkAdStandalone::LogoMarkOptimization(): move mark failed");
                            break;
                        }
                        save = true;
                    }
                }
            }
        }
        if (markLogo->type == MT_LOGOSTOP) {
            // check for advertising in frame with logo before logo stop mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (45 * decoder->GetVideoFrameRate()); // advertising in frame are usually 30s, changed from 35 to 45
            // sometimes there is a closing credit in frame with logo before
            const char *indexToHMSFStopMark = marks.GetTime(markLogo);
            char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition, false);
            if (indexToHMSFSearchPosition) {
                ALLOC(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
            }

            if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo from frame (%d) at %s to logo stop mark (%d) at %s", searchStartPosition, indexToHMSFSearchPosition, markLogo->position, indexToHMSFStopMark);
            if (indexToHMSFSearchPosition) {
                FREE(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
                free(indexToHMSFSearchPosition);
            }
            // short start/stop pair can result in overlapping checks
            if (decoder->GetFrameNumber() > searchStartPosition) {
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): current framenumber (%d) greater than framenumber to seek (%d), restart decoder", decoder->GetFrameNumber(), searchStartPosition);
                decoder->Restart();
            }
            // detect frames
            if ((evaluateLogoStopStartPair->GetIsAdInFrame(markLogo->position) >= STATUS_UNKNOWN) && (detectLogoStopStart->Detect(searchStartPosition, markLogo->position))) {
                bool isEndMark = false;
                if (markLogo->position == marks.GetLast()->position) isEndMark = true;
                int newStopPosition = detectLogoStopStart->AdInFrameWithLogo(searchStartPosition, markLogo->position, false, isEndMark);
                if (newStopPosition >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) found", newStopPosition, markLogo->position);
                    if (evaluateLogoStopStartPair->IncludesInfoLogo(newStopPosition, markLogo->position)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        newStopPosition = -1;
                    }
                }
                if (newStopPosition != -1) {
                    if (!macontext.Config->fullDecode) newStopPosition = index->GetIFrameBefore(newStopPosition - 1);  // we got first frame of ad, go one iFrame back for stop mark
                    else newStopPosition--; // get frame before ad in frame as stop mark
                    evaluateLogoStopStartPair->AddAdInFrame(newStopPosition, markLogo->position);  // store info that we found here adinframe
                    markLogo = marks.Move(markLogo, newStopPosition, MT_NOADINFRAMESTOP);
                    if (!markLogo) {
                        esyslog("cMarkAdStandalone::LogoMarkOptimization(): move mark failed");
                        break;
                    }
                    save = true;
                }
            }
        }
        markLogo = markLogo->Next();
    }

    // delete logo mark optimization objects
    FREE(sizeof(*detectLogoStopStart), "detectLogoStopStart");
    delete detectLogoStopStart;
    detectLogoStopStart = nullptr;

    // save marks
    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
}


void cMarkAdStandalone::BlackScreenOptimization() {
    bool save = false;
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark optimization with black screen");
    DebugMarks();
    cMark *mark = marks.GetFirst();
#define START_STOP_BLACK (decoder->GetVideoFrameRate() / 2)    // black picture before start and after stop mark
    while (mark) {
        int lengthBefore   = 0;
        int lengthAfter    = 0;
        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            // log available start marks
            bool moved         = false;
            int diffBefore     = INT_MAX;
            int diffAfter      = INT_MAX;
            bool silenceBefore = false;
            bool silenceAfter  = false;
            // stop of black screen is start mark
            cMark *stopBefore  = nullptr;
            cMark *stopAfter   = nullptr;
            cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART); // new part starts after the black screen
            cMark *startAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTART); // new part starts after the black screen
            if (startBefore) {
                stopBefore = blackMarks.GetPrev(startBefore->position, MT_NOBLACKSTOP);
                if (stopBefore) {
                    diffBefore   = 1000 * (mark->position        - startBefore->position) / decoder->GetVideoFrameRate();
                    lengthBefore = 1000 * (startBefore->position - stopBefore->position)  / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopBefore->position, MT_SOUNDCHANGE, 0xF0);         // around black screen start
                    if (!silence) silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), startBefore->position, MT_SOUNDCHANGE, 0xF0); // around black screen stop
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms before -> length %5dms, silence around %d", mark->position, stopBefore->position, startBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else startBefore = nullptr; // no pair, this is invalid
            }
            if (startAfter) {
                stopAfter = blackMarks.GetPrev(startAfter->position, MT_NOBLACKSTOP);
                if (stopAfter) {
                    diffAfter   = 1000 * (startAfter->position - mark->position)      / decoder->GetVideoFrameRate();
                    lengthAfter = 1000 * (startAfter->position - stopAfter->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopAfter->position, MT_SOUNDCHANGE, 0xF0);         // around black screen start
                    if (!silence) silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), startAfter->position, MT_SOUNDCHANGE, 0xF0); // around black screen stop
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, stopAfter->position, startAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else startAfter = nullptr; // no pair, this is invalid
            }
            // try black screen before start mark
            if (startBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    // rule 1: use nearer and longer black screen
                    if ((diffAfter < diffBefore) && (lengthAfter > lengthBefore)) diffBefore = INT_MAX;

                    // rule 2: use very near black screen after
                    else if ((diffAfter <= 1440) && (diffBefore >= 26520)) diffBefore = INT_MAX;

                    maxBefore = 29960;
                    break;
                case MT_LOGOSTART:
                    // rule 1: very short blackscreen with silence after
                    if (silenceAfter && (diffAfter <= 40)) diffBefore = INT_MAX;

                    if ((criteria->LogoFadeInOut() & FADE_IN) && silenceBefore)            maxBefore = 6840;
                    else if ((criteria->LogoFadeInOut() & FADE_IN) && (lengthBefore > 40)) maxBefore = 5680;
                    else                                                                   maxBefore = 3020;
                    break;
                case MT_CHANNELSTART:
                    maxBefore = 1240;
                    break;
                case MT_TYPECHANGESTART:
                    maxBefore = 1740;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_INTRODUCTIONSTART:
                        maxBefore = 4960;
                        break;
                    case MT_VPSSTART:
                        // rule 1: for channel with good VPS events use nearer and longer black screen
                        if (criteria->GoodVPS() && (diffAfter < diffBefore) && (lengthAfter > lengthBefore)) diffBefore = INT_MAX;

                        // rule 2: blackscreen with silence before VPS start (preview) and black screen with silence after VPS start (broadcast start)
                        if (silenceBefore && silenceAfter && (diffBefore >= 26660) && (diffAfter <= 44100)) diffBefore = INT_MAX;

                        // rule 3: not so far blackscreen after is start of broadcast, no silence around
                        else if (!silenceBefore && !silenceAfter && (diffBefore > 48040) && (diffAfter <= 26560) && (lengthAfter >= 80)) diffBefore = INT_MAX;

                        if (criteria->GoodVPS()) maxBefore = 26099;
                        else if (silenceBefore)                           maxBefore = 81800;
                        else if (lengthBefore >= 600)                     maxBefore = 88400;
                        else if (lengthBefore >= 240)                     maxBefore = 50480;
                        else                                              maxBefore =     0;  // do not accept short black screen, too much false positiv
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {  // move even to same position to prevent scene change do a move
                    int newPos =  startBefore->position;
                    if (mark->position == marks.First()->position) {
                        newPos -= (START_STOP_BLACK + 1);  // start broadcast with some black picture, one before because we get first frame after black screen
                        if (newPos < stopBefore->position) newPos = stopBefore->position;
                        else {
                            int midBlack = (startBefore->position + stopBefore->position) / 2;  // for long black screen, take mid of a the black screen
                            if (newPos < midBlack) newPos = midBlack;
                        }
                        newPos = index->GetFrameAfter(newPos - 1);  // if not full decoding, this will set to next i-Frame
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try black screen after start mark
            if (!moved && startAfter) { // move even to same position to prevent scene change do a move
                int maxAfter = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxAfter = 73240;
                    break;
                case MT_LOGOSTART:
                    maxAfter = 2240;
                    break;
                case MT_HBORDERSTART:
                    maxAfter = 260;
                    break;
                case MT_VBORDERSTART:
                    if (lengthAfter >= maxAfter)                   maxAfter =    0;  // prevent to move to end of black screen with text from broadcast start
                    else if (silenceAfter && (lengthAfter >= 240)) maxAfter = 7440;
                    else                                           maxAfter = 6120;
                    break;
                case MT_CHANNELSTART:
                    maxAfter = 4319;   // black sceen after start of broadcast 4320ms (3840)
                    break;
                case MT_ASPECTSTART:
                    maxAfter = 1000;   // valid black screen 1000ms (650)
                    break;
                case MT_TYPECHANGESTART:
                    maxAfter = 1560;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if (criteria->GoodVPS())        maxAfter =   7020;
                        else if (silenceAfter)          maxAfter = 139480;
                        else if (diffBefore == INT_MAX) maxAfter = 124520;  // broadcast does not have a black screen before, trust black screen after
                        else                            maxAfter =  21680;  // use only very near short black screen
                        break;
                    case MT_INTRODUCTIONSTART:
                        if (lengthAfter >= 3800) maxAfter = 3760;
                        else                     maxAfter = 3199;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {
                    int newPos =  startAfter->position;
                    if (mark->position == marks.First()->position) {
                        newPos -= (START_STOP_BLACK + 1);  // start broadcast with some black picture, one before because we get first frame after black screen
                        if (newPos < stopAfter->position) newPos = stopAfter->position;
                        else {
                            if (lengthAfter < 2520) {  // very long blackscreen is from closing credit before, changed from 4200 to 2520
                                int midBlack = (stopAfter->position + startAfter->position) / 2;  // for long black screen, take mid of a the black screen
                                if (newPos > midBlack) newPos = midBlack;
                            }
                        }
                        newPos = index->GetFrameAfter(newPos - 1);  // if not full decoding, this will set to next i-Frame
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART);
                    if (mark) {
                        save = true;
                    }
                    else break;
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available stop marks
            bool  moved              = false;
            long int diffBefore      = INT_MAX;
            int   diffAfter          = INT_MAX;
            bool  silenceBefore      = false;
            bool  silenceAfter       = false;
            const cMark *startAfter  = nullptr;
            const cMark *stopAfter   = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);

            const cMark *blackStartBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP);
            const cMark *blackStopBefore  = nullptr;
            if (blackStartBefore) {
                diffBefore = 1000 * (mark->position - blackStartBefore->position) / decoder->GetVideoFrameRate();
                blackStopBefore = blackMarks.GetNext(blackStartBefore->position, MT_NOBLACKSTART);
                if (blackStopBefore) {
                    lengthBefore = 1000 * (blackStopBefore->position - blackStartBefore->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), blackStartBefore->position, MT_SOUNDCHANGE, 0xF0);
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7ldms before -> length %5dms, silence around %d", mark->position, blackStartBefore->position, blackStopBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else blackStartBefore = nullptr; // no pair, this is invalid
            }
            if (stopAfter) {
                diffAfter = 1000 * (stopAfter->position - mark->position) / decoder->GetVideoFrameRate();
                startAfter = blackMarks.GetNext(stopAfter->position, MT_NOBLACKSTART);
                if (startAfter) {
                    lengthAfter = 1000 * (startAfter->position - stopAfter->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopAfter->position, MT_SOUNDCHANGE, 0xF0);
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, stopAfter->position, startAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else stopAfter = nullptr; // no pair, this is invalid
            }

            // try black screen after stop marks
            if (stopAfter) {  // move even to same position to prevent scene change for move again
                int maxAfter = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    // rule 1: use short before if after is far away
                    if ((diffBefore <= 7600) && (diffAfter >= 61920)) diffAfter = INT_MAX;

                    // rule 2: use longer and nearer
                    if ((lengthBefore > lengthAfter) && (diffBefore < diffAfter)) diffAfter = INT_MAX;

                    if (lengthAfter >= 120) maxAfter = 280200;
                    else                    maxAfter =  25119;
                    break;
                case MT_LOGOSTOP:
                    // rules for fade out channels
                    if (criteria->LogoFadeInOut() & FADE_OUT) {
                        // rule 1: black screen very before logo stop and far after, old recording without fade out from fade out channel
                        if ((diffBefore <= 20) && (diffAfter >= 2000)) diffAfter = INT_MAX;
                    }
                    // rules for channel without fade out
                    else {
                        // rule 2: black screen before from before logo stop to after logo stop
                        if ((lengthBefore >= diffBefore)) diffAfter = INT_MAX;

                        // rule 3: long black screen at end of broadcast, short black screen after preview
                        else if ((diffBefore <= 4580) && (lengthBefore >= 600) && (diffAfter <= 3240) && (lengthAfter <= 520)) diffAfter = INT_MAX;
                    }

                    if ((criteria->LogoFadeInOut() & FADE_OUT) )  maxAfter = 4960;
                    else                                                                   maxAfter = 1399;
                    break;
                case MT_HBORDERSTOP:
                    // rule 1: black screen short before hborder stop is end of closing credits
                    if ((diffBefore <= 360) && (diffAfter >= 7000)) diffAfter = INT_MAX;

                    if (silenceAfter && (lengthAfter >= 200)) maxAfter = 10760;  // closing credits overlay hborder
                    else                                      maxAfter =     0;
                    break;
                case MT_VBORDERSTOP:
                    maxAfter = 480;  // include black closing credits
                    break;
                case MT_CHANNELSTOP:
                    maxAfter = 320;
                    break;
                case MT_TYPECHANGESTOP:
                    maxAfter = 80;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        // rule 1: silence before and after, first black screen is end mark, second is after preview or in next broadcast
                        if (silenceBefore && silenceAfter && (diffBefore <= 6360) && (diffAfter >= 12560)) diffAfter = INT_MAX;

                        // rule 2: no silence before or after, first near black screen is end mark, second far from stop is after preview or in next broadcast
                        else if (!silenceBefore && !silenceAfter && (diffBefore <= 1760) && (lengthBefore >= 40) && (diffAfter >= 112720) && (lengthAfter <= 400)) diffAfter = INT_MAX;
                        // rule 3: no silence before or after, first black screen is end mark, second is after preview or in next broadcast
                        else if (!silenceBefore && !silenceAfter && (diffBefore <= 116120) && (lengthBefore >= 200) && (diffAfter >= 8500) && (lengthAfter <= 280)) diffAfter = INT_MAX;

                        // rule 4: valid black screen with silence around before
                        else if (silenceBefore && !silenceAfter && (diffBefore <= 6260) && (diffAfter >= 4180)) diffAfter = INT_MAX;

                        if (criteria->GoodVPS())    maxAfter =  12779;
                        else if (lengthAfter >= 80) maxAfter = 200759;
                        else                        maxAfter =   2200;
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxAfter = 11040;
                        break;
                    case MT_NOADINFRAMESTOP:
                        maxAfter = 3439;
                        break;
                    case MT_TYPECHANGESTOP:
                        maxAfter = 560;   // length 15360ms from closing credits
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {  // move even to same position to prevent scene change for move again
                    int newPos =  stopAfter->position;
                    if (mark->position == marks.GetLast()->position) {
                        newPos += START_STOP_BLACK;  // end broadcast with some black picture
                        if (newPos > (startAfter->position - 1)) newPos = startAfter->position - 1;
                        else {
                            if (lengthAfter < 4200) { // too long black screen is opening credits from next broadcast
                                int midBlack = (stopAfter->position + startAfter->position) / 2;  // for long black screen, take mid of a the black screen
                                if (newPos < midBlack) newPos = midBlack;
                            }
                        }
                    }
                    newPos = index->GetFrameBefore(newPos); // black stop is first frame after black screen
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try black screen before stop mark
            if (!moved && blackStartBefore) {  // move even to same position to prevent scene change for move again
                int maxBefore = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxBefore = 29120;
                    break;
                case MT_LOGOSTOP:
                    if (criteria->LogoFadeInOut() & FADE_OUT) {
                        if (lengthBefore > diffBefore) maxBefore =    20;   // fade out in black screen
                        else                           maxBefore =     0;   // never use black screen before fade out logo
                    }
                    else                               maxBefore =  5139;
                    break;
                case MT_HBORDERSTOP:
                    maxBefore = 360;
                    break;
                case MT_VBORDERSTOP:
                    maxBefore = 6840;
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 2040;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (criteria->GoodVPS())      maxBefore = 60000;
                        else if (lengthBefore >= 360) maxBefore = 116120;
                        else if (silenceBefore)       maxBefore =   6360;
                        else                          maxBefore =   5800;
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxBefore = 15200;
                        break;
                    case MT_NOADINFRAMESTOP:
                        if ((mark->position == marks.GetLast()->position) && (lengthBefore > diffBefore)) maxBefore =    -1;  // long black closing credits before ad in frame, keep this
                        else                                                                              maxBefore = 17720;
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    int newPos =  blackStartBefore->position;
                    if (mark->position == marks.GetLast()->position) {
                        // keep full closing black screen around last stop mark
                        // ignore if too long, in this case it contains opening credits from next broadcast
                        if ((lengthBefore >= diffBefore) && (lengthBefore < 5480)) newPos = blackStopBefore->position;  // changed from 9080 to 5480
                        else {
                            newPos =  blackStartBefore->position + START_STOP_BLACK;  // set end of broadcast with some black picture, not all, this can be opening credits of next broadcast
                            if (newPos > (blackStopBefore->position - 1)) newPos = (blackStopBefore->position - 1);
                            else {
                                if (lengthBefore < 5040) {  // next broadcast starts with a long dark scene
                                    int midBlack = (blackStopBefore->position + blackStartBefore->position) / 2;  // for long black screen, take mid of a the black screen
                                    if (newPos < midBlack) newPos = midBlack;
                                }
                            }
                        }
                    }
                    newPos = index->GetFrameBefore(newPos); // black stop is first frame after black screen
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTOP);
                    if (mark) save = true;
                    else break;
                }
            }
        }
        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
}


void cMarkAdStandalone::LowerBorderOptimization() {
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::LowerBorderOptimization(): start mark optimization with lower black or white border");
    if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::LowerBorderOptimization(): broadcast has hborder, no optimization with lower border possible");
        return;
    }
    DebugMarks();
    bool save = false;
    cMark *mark = marks.GetFirst();
    while (mark) {
        // only for VPS marks
        if (((mark->type & 0xF0) != MT_MOVED) || ((mark->newType & 0xF0) != MT_VPSCHANGE)) {
            mark = mark->Next();
            continue;
        }

        int lengthBefore   = 0;
        int lengthAfter    = 0;

        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            bool moved = false;
            // get lower border before start mark
            int diffBefore     = INT_MAX;
            cMark *stopBefore  = nullptr;
            cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOLOWERBORDERSTOP);  // start of lower border before logo start mark
            if (startBefore) {
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOLOWERBORDERSTART);  // end   of lower border before logo start mark
                if (stopBefore) {
                    diffBefore = 1000 * (mark->position - startBefore->position) / decoder->GetVideoFrameRate();
                    lengthBefore = 1000 * (stopBefore->position - startBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::LowerBorderOptimization(): start mark (%6d): lower border from (%6d) to (%6d), %7dms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                }
                else startBefore = nullptr; // no pair, this is invalid
            }
            // get lower border after start mark
            cMark *startAfter = blackMarks.GetNext(mark->position - 1, MT_NOLOWERBORDERSTOP);  // start of lower border after logo start mark
            int diffAfter     = INT_MAX;
            cMark *stopAfter  = nullptr;
            while (startAfter) { // get first long lower border start/stop after start mark
                stopAfter = blackMarks.GetNext(startAfter->position, MT_NOLOWERBORDERSTART);  // get end of lower border
                if (!stopAfter) break;
                diffAfter   = 1000 * (startAfter->position - mark->position)       / decoder->GetVideoFrameRate();
                lengthAfter = 1000 * (stopAfter->position  - startAfter->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::LowerBorderOptimization(): start mark (%6d): lower border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                if ((lengthAfter >= MIN_LOWER_BORDER) && (lengthAfter <= MAX_LOWER_BORDER)) break;
                startAfter = blackMarks.GetNext(startAfter->position, MT_NOLOWERBORDERSTOP);  // next start of lower border
            }
            if ((lengthAfter < MIN_LOWER_BORDER) || (lengthAfter > MAX_LOWER_BORDER)) { // we got no valid result
                startAfter = nullptr;
                stopAfter  = nullptr;
            }

            // try lower border before start mark
            if (startBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if ((lengthBefore >= MIN_LOWER_BORDER) && (lengthBefore <= MAX_LOWER_BORDER)) {
                            if (criteria->GoodVPS()) maxBefore =   7859;
                            else                     maxBefore = 254920;
                        }
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, stopBefore->position, MT_NOLOWERBORDERSTART);  // move to end of lower border (closing credits)
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // lower border after start mark
            if (!moved && stopAfter) {
                int maxAfter = -1;
                switch (mark->type) {
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if (criteria->GoodVPS()) maxAfter =  16639;
                        else                     maxAfter = 304480;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {
                    mark = marks.Move(mark, stopAfter->position, MT_NOLOWERBORDERSTART);  // use end of lower closing credits
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            if (!moved) dsyslog("cMarkAdStandalone::LowerBorderOptimization(): no matching lower border found");
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            bool moved = false;

            // get lower border before stop mark
            long int diffBefore      = INT_MAX;
            const cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOLOWERBORDERSTOP);
            const cMark *stopBefore  = nullptr;
            while (startBefore) {
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOLOWERBORDERSTART);
                if (!stopBefore) break;
                diffBefore = 1000 * (mark->position - startBefore->position) / decoder->GetVideoFrameRate();
                lengthBefore = 1000 * (stopBefore->position - startBefore->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::LowerBorderOptimization(): stop  mark (%6d): lower border from (%6d) to (%6d), %7ldms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                if ((lengthBefore >= MIN_LOWER_BORDER) && (lengthBefore <= MAX_LOWER_BORDER)) break;
                startBefore = blackMarks.GetPrev(startBefore->position, MT_NOLOWERBORDERSTOP);  // previous start of lower border
            }
            if ((lengthBefore < MIN_LOWER_BORDER) || (lengthBefore > MAX_LOWER_BORDER)) { // we got no valid result
                startBefore = nullptr;
                stopBefore  = nullptr;
            }

            // get lower border after stop mark
            int diffAfter     = INT_MAX;
            cMark *startAfter = blackMarks.GetNext(mark->position - 1, MT_NOLOWERBORDERSTOP);
            cMark *stopAfter  = nullptr;
            while (startAfter) { // get first long lower border start/stop after start mark
                stopAfter = blackMarks.GetNext(startAfter->position, MT_NOLOWERBORDERSTART);  // get end of lower border
                if (!stopAfter) break;
                diffAfter   = 1000 * (startAfter->position - mark->position)       / decoder->GetVideoFrameRate();
                lengthAfter = 1000 * (stopAfter->position  - startAfter->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::LowerBorderOptimization(): stop  mark (%6d): lower border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                if ((lengthAfter >= MIN_LOWER_BORDER) && (lengthAfter <= MAX_LOWER_BORDER)) break;
                startAfter = blackMarks.GetNext(startAfter->position, MT_NOLOWERBORDERSTOP);  // next start of lower border
            }
            if ((lengthAfter < MIN_LOWER_BORDER) || (lengthAfter > MAX_LOWER_BORDER)) { // we got no valid result
                startAfter = nullptr;
                stopAfter  = nullptr;
            }

            // try lower border after stop marks
            if (stopAfter) {
                int maxAfter = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxAfter = 68200;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (criteria->GoodVPS()) maxAfter =  71519;
                        else                     maxAfter = 257760;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {  // move even to same position to prevent scene change for move again
                    mark = marks.Move(mark, stopAfter->position, MT_NOLOWERBORDERSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }

            // try lower border before stop mark
            if (!moved && stopBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    if (lengthBefore >= 1560) maxBefore = 216200;
                    else                      maxBefore =  51040;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (criteria->GoodVPS()) maxBefore =   6579;
                        else                     maxBefore = 120040;
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, stopBefore->position, MT_NOLOWERBORDERSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            if (!moved) dsyslog("cMarkAdStandalone::LowerBorderOptimization(): no matching lower border found");
        }
        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
}


void cMarkAdStandalone::SilenceOptimization() {
    LogSeparator(true);
    bool save = false;
    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark optimization with silence scenes");
    DebugMarks();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            // log available marks
            bool moved = false;
            int diffBefore   = INT_MAX;
            int diffAfter    = INT_MAX;
            cMark *soundStartBefore = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTART);
            cMark *soundStopBefore  = nullptr;
            cMark *soundStartAfter  = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTART);
            cMark *soundStopAfter   = nullptr;
            if (soundStartBefore) {
                diffBefore = 1000 * (mark->position - soundStartBefore->position) / decoder->GetVideoFrameRate();
                soundStopBefore = silenceMarks.GetPrev(soundStartBefore->position, MT_SOUNDSTOP);
                if (soundStopBefore) {
                    int lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopBefore->position, MT_BLACKCHANGE, 0xF0);
                    bool blackBefore = false;
                    if (black) blackBefore = true;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): silence from (%6d) to (%6d) %8dms before, length %4dms, black %d", mark->position, soundStopBefore->position, soundStartBefore->position, diffBefore, lengthBefore, blackBefore);
                }
            }
            if (soundStartAfter) {
                diffAfter = 1000 * (soundStartAfter->position - mark->position) / decoder->GetVideoFrameRate();
                soundStopAfter  = silenceMarks.GetPrev(soundStartAfter->position, MT_SOUNDSTOP);
                if (soundStopAfter) {
                    int lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / decoder->GetVideoFrameRate();
                    bool blackAfter  = false;
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopAfter->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackAfter = true;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): silence from (%6d) to (%6d) %8dms after,  length %4dms, black %d", mark->position, soundStopAfter->position, soundStartAfter->position, diffAfter, lengthAfter, blackAfter);
                }
            }
            // try silence before start position
            if (soundStartBefore && (soundStartBefore->position != mark->position)) { // do not move to same frame
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxBefore = 178080;
                    break;
                case MT_NOLOWERBORDERSTART:
                    maxBefore = 82980;
                    break;
                case MT_LOGOSTART:
                    // rule 1: logo start is in ad, silence before and after ad
                    if ((diffBefore >=  3140) && (diffAfter <=  3740)) diffBefore = INT_MAX;

                    if (criteria->LogoFadeInOut() & FADE_IN) maxBefore = 3119;
                    else                                     maxBefore = 1599;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // rule 1: refer silence after VPS start event
                        if (diffAfter <= 231440) diffBefore = INT_MAX;

                        if (criteria->GoodVPS()) maxBefore =  30259;
                        else                     maxBefore = 171080;
                        break;
                    case MT_INTRODUCTIONSTART:
                        maxBefore = 2040;
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if (diffBefore <= maxBefore) {
                    int newPos = soundStartBefore->position;   // assume start of silence is in ad or separator, sound start is in broadcast
                    if (!macontext.Config->fullDecode) newPos = index->GetIFrameBefore(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try silence after start position
            if (!moved && soundStartAfter) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxAfter = 227620;
                    break;
                case MT_LOGOSTART:
                    maxAfter = 0;    // silence after logo start is always invalid
                    break;
                case MT_VBORDERSTART:
                    if (mark->position == marks.GetFirst()->position) maxAfter = 359;
                    else                                              maxAfter =   0;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if (criteria->GoodVPS()) maxAfter = 116959;
                        else                     maxAfter = 231440;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if ((diffAfter <= maxAfter) && (soundStartAfter->position != mark->position)) {
                    int newPos = soundStartAfter->position;                 // assume start of silence is in ad or separator, sound start is in broadcas
                    if (!macontext.Config->fullDecode) newPos = index->GetIFrameBefore(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTART);
                    if (mark) {
                        save  = true;
                    }
                    else break;
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available marks
            bool moved = false;
            long int diffBefore   = INT_MAX;
            int lengthBefore      = 0;
            int lengthAfter       = 0;
            int diffAfter         = INT_MAX;
            bool lowerBefore      = false;
            cMark *soundStopBefore  = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            cMark *soundStartBefore = nullptr;
            cMark *soundStopAfter   = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            cMark *soundStartAfter  = nullptr;
            if (soundStopBefore) {
                diffBefore = 1000 * (mark->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                soundStartBefore = silenceMarks.GetNext(soundStopBefore->position, MT_SOUNDSTART);
                if (soundStartBefore) {
                    lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): silence from (%6d) to (%6d) %8ldms before, length %4dms", mark->position, soundStopBefore->position, soundStartBefore->position, diffBefore, lengthBefore);
                }
            }
            if (soundStopAfter) {
                diffAfter = 1000 * (soundStopAfter->position - mark->position) / decoder->GetVideoFrameRate();
                soundStartAfter = silenceMarks.GetNext(soundStopAfter->position, MT_SOUNDSTART);
                if (soundStartAfter) {
                    bool blackAfter       = false;
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopAfter->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackAfter = true;
                    lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): silence from (%6d) to (%6d) %8dms after,  length %4dms, black screen %d", mark->position, soundStopAfter->position, soundStartAfter->position, diffAfter, lengthAfter, blackAfter);
                }
            }
            // try silence after stop mark
            if (soundStopAfter && (soundStopAfter->position != mark->position)) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxAfter = 135719;  // silence in next broadcast after 135720ms
                    break;
                case MT_LOGOSTOP:
                    // rule 1: second silence is after preview
                    if ((diffBefore <= 11680) && (diffAfter >= 1040) && (diffAfter <= 6640)) diffAfter = INT_MAX;

                    if (criteria->LogoFadeInOut() & FADE_OUT) maxAfter = 4640;
                    else                                                               maxAfter = 1079;
                    break;
                case MT_VBORDERSTOP:
                    maxAfter = 0;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        // rule 1: silence with black border, second silence is from next broadcast
                        if ((diffBefore <= 57280) && lowerBefore && (diffAfter >= 2640)) diffAfter = INT_MAX;

                        // rule 2: silence short before and far after
                        else if ((diffBefore <= 5480) && (diffAfter >= 20600)) diffAfter = INT_MAX;

                        // rule 3: very long silence before, short silence after
                        else if ((diffBefore <= 31960) && (lengthBefore >= 3160) && (lengthAfter <= 340)) diffAfter = INT_MAX;

                        if (criteria->GoodVPS()) maxAfter = 31479;
                        else                                              maxAfter = 98479;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if (diffAfter <= maxAfter) {
                    int newPos = soundStopAfter->position;  // assume start of silence is in ad or separator
                    mark = marks.Move(mark, newPos, MT_SOUNDSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try silence before stop mark
            if (!moved && soundStopBefore) {
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxBefore = 173639;
                    break;
                case MT_LOGOSTOP:
                    if (criteria->GoodVPS()) maxBefore = 1279;
                    else                                              maxBefore = 4440;
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 1100;
                    break;
                case MT_VBORDERSTOP:
                    maxBefore = 0;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (lowerBefore) maxBefore = 57280;
                        else             maxBefore = 31960;
                        break;
                    case MT_TYPECHANGESTOP:
                        maxBefore = 6200;
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if ((diffBefore <= maxBefore) && (soundStopBefore->position != mark->position)) {
                    int newPos = soundStopBefore->position;   // assume start of silence is in ad or separator
                    if (!macontext.Config->fullDecode) newPos = index->GetIFrameAfter(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTOP);
                    if (mark) {
                        save = true;
                    }
                    else break;
                }
            }
        }
        mark = mark->Next();
    }
// save marks
    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
}


void cMarkAdStandalone::SceneChangeOptimization() {
    bool save = false;
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark optimization with scene changes");
    DebugMarks();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // check start mark
        if ((mark->type & 0x0F) == MT_START) {
            // log available marks
            bool moved     = false;
            int diffAfter  = INT_MAX;
            int diffBefore = INT_MAX;
            cMark *sceneStartBefore = nullptr;
            cMark *sceneStartAfter  = nullptr;
            if (mark->type == MT_ASPECTSTART) {   // change of aspect ratio results in a scene change, but they are sometimes a few frames to early
                sceneStartBefore = sceneMarks.GetPrev(mark->position, MT_SCENESTART);      // do not allow one to get same position
                sceneStartAfter  = sceneMarks.GetNext(mark->position, MT_SCENESTART);      // do not allow one to get same position
            }
            else {
                sceneStartBefore = sceneMarks.GetPrev(mark->position + 1, MT_SCENESTART);  // allow one to get same position
                sceneStartAfter  = sceneMarks.GetNext(mark->position - 1, MT_SCENESTART);  // allow one to get same position
            }

            if (sceneStartBefore) {
                diffBefore = 1000 * (mark->position - sceneStartBefore->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%6d): found scene start (%6d) %5dms before", mark->position, sceneStartBefore->position, diffBefore);
            }
            if (sceneStartAfter) {
                diffAfter = 1000 * (sceneStartAfter->position - mark->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%6d): found scene start (%6d) %5dms after", mark->position, sceneStartAfter->position, diffAfter);
            }
            // try scene change before start mark
            if (sceneStartBefore && (sceneStartBefore->position != mark->position)) {
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxBefore = 600;
                    break;
                case MT_LOGOSTART:
                    // rule 1: logo start very short before broadcast start
                    if (!(criteria->LogoFadeInOut() & FADE_IN) && (diffBefore >= 1120) && (diffAfter <= 120)) diffBefore = INT_MAX;

                    // rule 2: scene start very short after logo start in old recording without fade in of fade in channel
                    else if ((criteria->LogoFadeInOut() & FADE_IN) && (diffBefore >= 1700) && (diffAfter <= 20)) diffBefore = INT_MAX;

                    if (criteria->LogoFadeInOut() & FADE_IN) maxBefore = 4800;
                    else                                     maxBefore = 1599;
                    break;
                case MT_CHANNELSTART:
                    if (diffAfter <= 460) diffBefore = INT_MAX;
                    maxBefore = 1060;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_SOUNDSTART:
                        // rule 1: prefer scene change short after silence
                        if ((diffBefore >= 60) && (diffBefore <= 1680) && (diffAfter <= 920)) diffBefore = INT_MAX;

                        // rule 2: scene blend around silence, both are invalid
                        else if ((diffBefore >= 4120) && (diffAfter >= 1160)) {
                            diffBefore = INT_MAX;
                            diffAfter  = INT_MAX;
                        }
                        maxBefore = 1180;
                        break;
                    case MT_VPSSTART:
                        // rule 1: use nearest scene change to VPS event
                        if (diffAfter < diffBefore) diffBefore = INT_MAX;

                        maxBefore = 3340;
                        break;
                    case MT_INTRODUCTIONSTART:
                        maxBefore = 4880;  // changed from 3799 to 4880
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, index->GetFrameAfter(sceneStartBefore->position - 1), MT_SCENESTART);  // adjust to i-frame if no full decoding
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try scene change after start mark
            if (!moved && sceneStartAfter) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxAfter = 9160;  // changed from 1760 to 9160
                    break;
                case MT_LOGOSTART:
                    if (criteria->LogoFadeInOut() & FADE_IN) maxAfter =  0; // with fade in logo, scene after is always false
                    else                                     maxAfter = 80; // some channels starts logo short before broadcast
                    break;
                case MT_ASPECTSTART:
                    maxAfter =  320;
                    break;
                case MT_CHANNELSTART:
                    maxAfter = 1800;
                    break;
                case MT_TYPECHANGESTART:
                    maxAfter =   40;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_SOUNDSTART:
                        maxAfter = 920;
                        break;
                    case MT_NOLOWERBORDERSTART:
                        maxAfter = 5000;
                        break;
                    case MT_VPSSTART:
                        maxAfter = 13440;
                        break;
                    case MT_INTRODUCTIONSTART:
                        maxAfter = 1200;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if ((diffAfter <= maxAfter) && (sceneStartAfter->position != mark->position)) {
                    mark = marks.Move(mark, index->GetFrameAfter(sceneStartAfter->position - 1), MT_SCENESTART);  // adjust to i-frame if no full decoding
                    if (mark) {
                        save = true;
                    }
                    else break;
                }
            }
        }
        // check stop mark
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available marks
            bool moved          = false;
            int diffAfter       = INT_MAX;
            long int diffBefore = INT_MAX;
            cMark *sceneStopBefore = sceneMarks.GetPrev(mark->position + 1, MT_SCENESTOP);
            cMark *sceneStopAfter  = sceneMarks.GetNext(mark->position - 1, MT_SCENESTOP);  // allow one to get same position
            if (sceneStopBefore) {
                diffBefore = 1000 * (mark->position - sceneStopBefore->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%6d): found scene stop  (%6d) %5ldms before", mark->position, sceneStopBefore->position, diffBefore);
            }
            if (sceneStopAfter) {
                diffAfter = 1000 * (sceneStopAfter->position - mark->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%6d): found scene stop  (%6d) %5dms after", mark->position, sceneStopAfter->position, diffAfter);
            }
            // try scene change after stop mark
            if ((sceneStopAfter) && (sceneStopAfter->position != mark->position)) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    if ((diffBefore <= 760) && (diffAfter >= 1120)) diffAfter = INT_MAX;
                    maxAfter = 8520;  // changed from 6480 to 8520
                    break;
                case MT_LOGOSTOP:
                    // rule 1: if no fade out logo, usually we have delayed logo stop from detection fault (bright picture or patten in background)
                    // very near scene change can be valid from very short fade out logo
                    if (!(criteria->LogoFadeInOut() & FADE_OUT) && (diffAfter > 200)) diffAfter = INT_MAX;

                    maxAfter = 5639;
                    break;
                case MT_HBORDERSTOP:
                    if ((diffBefore <= 440) && (diffAfter >= 1720)) diffAfter = INT_MAX;
                    maxAfter = 799;
                    break;
                case MT_CHANNELSTOP:
                    // rule 1:
                    if ((diffBefore >= 80) && (diffBefore <= 640) && (diffAfter >= 160) && (diffAfter <= 1460)) diffAfter = INT_MAX;

                    // rule 2:
                    else if (diffBefore <= 40) diffAfter = INT_MAX;

                    maxAfter = 919;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_NOLOWERBORDERSTOP:   // move after closing credits
                        // rule 1: scene change very short before and long after
                        if ((diffBefore <= 80) && (diffAfter >= 880)) diffAfter = INT_MAX;
                        maxAfter = 2520;
                        break;
                    case MT_SOUNDSTOP:
                        // rule1: use scene change near by end of silence
                        if (diffBefore < diffAfter) diffAfter = INT_MAX;

                        maxAfter = 360;
                        break;
                    case MT_VPSSTOP:
                        // rule 1: scene change short before
                        if ((diffBefore >= 160) && (diffBefore <= 480) && (diffAfter >= 680) && (diffAfter <= 3840)) diffAfter = INT_MAX;

                        // rule 2: long opening scene from next broadcast
                        else if ((diffBefore <= 80) && (diffAfter >= 6720)) diffAfter = INT_MAX;   // long opening scene from next broadcast

                        maxAfter = 17480;  // changed from 11200 to 17480
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxAfter = 1080;
                        break;
                    case MT_NOADINFRAMESTOP:
                        // select best mark (before / after), default: after
                        if ((diffBefore <= 440) && (diffAfter >= 360)) diffAfter = INT_MAX;
                        maxAfter = 760;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if (diffAfter <= maxAfter) {  // logo is fading out before end of broadcast scene, move forward
                    mark = marks.Move(mark, index->GetFrameBefore(sceneStopAfter->position + 1), MT_SCENESTOP);  // adjust to i-frame if no full decoding
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try scene change before stop mark
            if (!moved && (sceneStopBefore) && (sceneStopBefore->position != mark->position)) { // logo stop detected too late, move backwards
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxBefore = 12840;
                    break;
                case MT_LOGOSTOP:
                    if (!(criteria->LogoFadeInOut() & FADE_OUT)) maxBefore = 3000;
                    break;
                case MT_HBORDERSTOP:
                    if (mark->position == marks.GetLast()->position) maxBefore = 440;
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 640;  // changed from 600 to 640
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_NOLOWERBORDERSTOP:
                        maxBefore = 80;
                        break;
                    case MT_SOUNDSTOP:
                        maxBefore = 2120;
                        break;
                    case MT_VPSSTOP:
                        maxBefore = 8440;   // chaned from 1320 to 1440 to 8440
                        break;
                    case MT_NOADINFRAMESTOP:  // correct the missed start of ad in frame before stop mark
                        maxBefore = 999;
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, index->GetFrameBefore(sceneStopBefore->position + 1), MT_SCENESTOP);  // adjust to i-frame if no full decoding
                    if (mark) {
                        save = true;
                    }
                    else break;
                }
            }
        }
        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
}


void cMarkAdStandalone::ProcessOverlap() {
    if (abortNow)      return;
    if (duplicate)     return;
    if (!decoder)      return;
    if ((length == 0) || (startTime == 0)) {  // no recording length or start time from info file
        return;
    }
    bool save = false;

    // overlap detection
    LogSeparator(true);
    dsyslog("ProcessOverlap(): start overlap detection");
    DebugMarks();     //  only for debugging
    cOverlap *overlap = new cOverlap(decoder, index);
    ALLOC(sizeof(*overlap), "overlap");
    save = overlap->DetectOverlap(&marks);
    FREE(sizeof(*overlap), "overlap");
    delete overlap;

    // check last stop mark if closing credits follows
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::ProcessOverlap(): check last stop mark for advertisement in frame with logo or closing credits in frame without logo");
    cMark *lastStop = marks.GetLast();
    // check end mark
    if (lastStop && ((lastStop->type == MT_LOGOSTOP) ||  // prevent double detection of ad in frame and closing credits
                     ((lastStop->type == MT_MOVEDSTOP) && (lastStop->newType != MT_NOADINFRAMESTOP) && (lastStop->newType != MT_TYPECHANGESTOP)))) {
        if (MoveLastStopAfterClosingCredits(lastStop)) {
            lastStop = nullptr;  // pointer lastStop after move invalid
            save = true;
            dsyslog("cMarkAdStandalone::ProcessOverlap(): moved logo end mark after closing credit");
        }
    }
    // check border end mark
    if (lastStop && (lastStop->type == MT_HBORDERSTOP)) {
        dsyslog("cMarkAdStandalone::ProcessOverlap(): search for closing credits after border or moved end mark");
        if (MoveLastStopAfterClosingCredits(lastStop)) {
            save = true;
            dsyslog("cMarkAdStandalone::ProcessOverlap(): moved border end mark after closing credit");
        }
    }

    if (save) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
    dsyslog("cMarkAdStandalone::ProcessOverlap(): end");
    return;
}


bool cMarkAdStandalone::ProcessFrame() {
    int frameNumber = decoder->GetFrameNumber();
    if (decoder->IsVideoFrame()) {

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
        if ((frameNumber > (DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) && (frameNumber < (DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE))) {

            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d.pgm", macontext.Config->recDir, frameNumber) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                FREE(strlen(fileName) + 1, "fileName");
                free(fileName);
            }
        }
#endif
#ifdef DEBUG_PICTURE
        if ((frameNumber > (DEBUG_PICTURE - DEBUG_PICTURE_RANGE)) && (frameNumber < (DEBUG_PICTURE + DEBUG_PICTURE_RANGE))) {
            char *baseName = nullptr;
            if (asprintf(&baseName,"%s/F__%07d", macontext.Config->recDir, frameNumber) >= 1) {
                ALLOC(strlen(baseName) + 1, "baseName");
                SaveVideoPicture(baseName, decoder->GetVideoPicture());
                FREE(strlen(baseName) + 1, "baseName");
                free(baseName);
            }
        }
#endif

        // detect video based marks
        if (criteria->GetDetectionState(MT_VIDEO)) {
            sMarkAdMarks *vmarks = video->Process();
            if (vmarks) {
                for (int i = 0; i < vmarks->Count; i++) {
                    AddMark(&vmarks->Number[i]);
                }
            }
        }

        // check start
        if (!doneCheckStart && inBroadCast && (frameNumber > frameCheckStart)) CheckStart();

        // check stop
        if (!doneCheckStop && (frameNumber > frameCheckStop)) {
            if (!doneCheckStart) {
                dsyslog("cMarkAdStandalone::ProcessFrame(): assumed end reached but still no CheckStart() called, do it now");
                CheckStart();
            }
            CheckStop();
            return false;   // return false to signal end of file processing
        }
    }

    // detect audio channel based marks
    if ((!macontext.Config->fullDecode || decoder->IsAudioPacket()) &&   // if we decode only i-frames, we will have no audio frames
            criteria->GetDetectionState(MT_AUDIO)) {
        sMarkAdMarks *amarks = audio->Detect();
        if (amarks) {
            for (int i = 0; i < amarks->Count; i++) AddMark(&amarks->Number[i]);
        }
    }

    // turn on all detection for end part even if we use stronger marks, just in case we will get no strong end mark
    if (!restartLogoDetectionDone && (frameNumber > (stopA - (decoder->GetVideoFrameRate() * MAX_ASSUMED)))) {
        dsyslog("cMarkAdStandalone::ProcessFrame(): enter end part at frame (%d), reset detector status", frameNumber);
        video->Clear(true);
        criteria->SetDetectionState(MT_ALL, true);
        restartLogoDetectionDone = true;
    }
    return true;
}


void cMarkAdStandalone::Recording() {
    if (abortNow) return;

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::Recording(): start processing recording");
    criteria->ListDetection();
    if (macontext.Config->backupMarks) marks.Backup(directory);

    // force init decoder to get infos about video
    if (!decoder->ReadNextFile()) {
        esyslog("cMarkAdStandalone::Recording(): failed to open first video file");
        abortNow = true;
        return;
    }

    // create object to analyse video picture
    video = new cVideo(decoder, index, criteria, macontext.Config->recDir, macontext.Config->logoCacheDirectory);
    ALLOC(sizeof(*video), "video");

    // check aspect ratio of broadcast
    // if we don't know video aspect ratio, assume 16:9 and correct this in CheckStart()
    if ((macontext.Info.AspectRatio.num == 0) || (macontext.Info.AspectRatio.den == 0)) {
        dsyslog("cMarkAdStandalone::Recording(): unknown broadcast aspect ratio, assume 16:9");
        macontext.Info.AspectRatio.num = 16;
        macontext.Info.AspectRatio.den =  9;
    }
    video->SetAspectRatioBroadcast(macontext.Info.AspectRatio);

    // create object to analyse audio picture
    audio = new cAudio(decoder, index, criteria);
    ALLOC(sizeof(*audio), "audio");

    // video type
    if (decoder->GetVideoType() == 0) {
        dsyslog("cMarkAdStandalone::Recording(): video type not set");
        abortNow = true;
        return;
    }
    // video width
    dsyslog("cMarkAdStandalone::Recording(): video width: %4d", decoder->GetVideoWidth());
    // video height
    dsyslog("cMarkAdStandalone::Recording(): video hight: %4d", decoder->GetVideoHeight());
    // frame rate
    dsyslog("cMarkAdStandalone::Recording(): frame rate:         %d frames per second", decoder->GetVideoFrameRate());
    if (decoder->GetVideoFrameRate() <= 0) {
        esyslog("average frame rate of %d frames per second is invalid, recording is damaged", decoder->GetVideoFrameRate());
        abortNow = true;
        return;
    }

    // calculate assumed start and end position
    CalculateCheckPositions(macontext.Info.tStart * decoder->GetVideoFrameRate());

    // write an early start mark for running recordings
    if (macontext.Info.isRunningRecording) {
        dsyslog("cMarkAdStandalone::Recording(): recording is running, save dummy start mark at pre timer position %ds", macontext.Info.tStart);
        // use new marks object because we do not want to have this mark in final file
        cMarks *marksTMP = new cMarks();
        ALLOC(sizeof(*marksTMP), "marksTMP");
        marksTMP->SetIndex(index);  // register framerate to write a guessed start position, will be overridden later
        marksTMP->Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, macontext.Info.tStart * decoder->GetVideoFrameRate(), "timer start", true);
        marksTMP->Save(macontext.Config->recDir, macontext.Info.isRunningRecording, macontext.Config->pts, true);
        FREE(sizeof(*marksTMP), "marksTMP");
        delete marksTMP;
    }

    CheckIndexGrowing();   // check if we have a running recording and have to wait to get new frames

    while (decoder->DecodeNextFrame(criteria->GetDetectionState(MT_AUDIO))) {  // only decode audio if we need it
        if (abortNow) return;

        if (!ProcessFrame()) {   // no error, false if stopA reached
            break;
        }
        CheckIndexGrowing();  // check if we have a running recording and have to wait to get new frame
    }

    // we reached end of recording without CheckStart() or CheckStop() called
    if (!doneCheckStop && (decoder->GetFrameNumber() <= stopA)) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): stopA (%d)", decoder->GetFrameNumber(), stopA);
        esyslog("end of recording before recording length from VDR info file reached");
    }
    if (!doneCheckStart) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): recording ends before CheckStart() done, call it now", decoder->GetFrameNumber());
        CheckStart();
    }
    if (!doneCheckStop) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): recording ends before CheckStop() done, call it now", decoder->GetFrameNumber());
        CheckStop();
    }

// cleanup marks that make no sense
    CheckMarks();

    if (!abortNow) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
    dsyslog("cMarkAdStandalone::Recording(): end processing files");
}


bool cMarkAdStandalone::SetFileUID(char *file) {
    if (!file) return false;
    struct stat statbuf;
    if (!stat(directory, &statbuf)) {
        if (chown(file, statbuf.st_uid, statbuf.st_gid) == -1) return false;
    }
    return true;
}


time_t cMarkAdStandalone::GetRecordingStart(time_t start, int fd) {
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat   statbuf;
    FILE *mounts = setmntent(_PATH_MOUNTED, "r");
    int  mlen;
    int  oldmlen  = 0;
    bool useatime = false;
    while ((ent = getmntent(mounts)) != nullptr) {
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
            time_t now = time(nullptr);
            struct tm tm_r;
            struct tm t = *localtime_r(&now, &tm_r); // init timezone
            if (sscanf(timestr, "%4d-%02d-%02d.%02d%*c%02d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, & t.tm_min)==5) {
                t.tm_year -= 1900;
                t.tm_mon--;
                t.tm_sec = 0;
                t.tm_isdst = -1;
                dsyslog("cMarkAdStandalone::GetRecordingStart(): getting recording start from directory");
                return mktime(&t);
            }
        }
    }
    return (time_t) 0;
}


bool cMarkAdStandalone::CheckLogo(const int frameRate) {
    if (!macontext.Config) return false;
    if (!*macontext.Config->logoCacheDirectory) return false;
    if (!macontext.Info.ChannelName) return false;
    if (macontext.Config->perftest) return false;    // nothing to do in perftest

    int len = strlen(macontext.Info.ChannelName);
    if (!len) return false;

    gettimeofday(&startTime1, nullptr);
    dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s", macontext.Config->logoCacheDirectory);
    dsyslog("cMarkAdStandalone::CheckLogo(): searching logo for %s", macontext.Info.ChannelName);
    DIR *dir = opendir(macontext.Config->logoCacheDirectory);
    if (!dir) {
        esyslog("logo cache directory %s does not exist, use /tmp", macontext.Config->logoCacheDirectory);
        strcpy( macontext.Config->logoCacheDirectory, "/tmp");
        dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s", macontext.Config->logoCacheDirectory);
        dir = opendir(macontext.Config->logoCacheDirectory);
        if (!dir) exit(1);
    }

    struct dirent *dirent = nullptr;
    while ((dirent = readdir(dir))) {
        if (!strncmp(dirent->d_name, macontext.Info.ChannelName, len)) {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);

    if (macontext.Config->autoLogo > 0) {
        isyslog("no logo for %s %d:%d found in logo cache directory %s, trying to find logo in recording directory", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->logoCacheDirectory);
        DIR *recDIR = opendir(macontext.Config->recDir);
        if (recDIR) {
            struct dirent *direntRec = nullptr;
            while ((direntRec = readdir(recDIR))) {
                if (!strncmp(direntRec->d_name, macontext.Info.ChannelName, len)) {
                    closedir(recDIR);
                    isyslog("logo found in recording directory");
                    gettimeofday(&endTime1, nullptr);
                    return true;
                }
            }
            closedir(recDIR);
        }
        isyslog("no logo for %s %d:%d found in recording directory %s, trying to extract logo from recording", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->recDir);

        extractLogo = new cExtractLogo(macontext.Config->recDir, macontext.Info.ChannelName, macontext.Config->threads, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Info.AspectRatio);
        ALLOC(sizeof(*extractLogo), "extractLogo");

        // write an early start mark for running recordings to provide a guest start mark for direct play, marks file will be overridden by save of first real mark
        if (macontext.Info.isRunningRecording) {
            dsyslog("cMarkAdStandalone::CheckLogo(): recording is aktive, now save dummy start mark at pre timer position %ds", macontext.Info.tStart);
            cMarks marksTMP;
            marksTMP.SetFrameRate(extractLogo->GetFrameRate());
            marksTMP.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, macontext.Info.tStart, "timer start", true);
            marksTMP.Save(macontext.Config->recDir, macontext.Info.isRunningRecording, macontext.Config->pts, true);
        }

        int startPos =  macontext.Info.tStart * frameRate;  // search logo from assumed start
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = extractLogo->SearchLogo(startPos, false);
        for (int retry = 2; retry <= 8; retry++) {  // do not reduce, we will not get some logos
            startPos = endpos;          // next try after end of try before
            if (endpos > LOGO_FOUND) {  // no logo found, endpos is last frame of search
                dsyslog("cMarkAdStandalone::CheckLogo(): no logo found in recording, retry in %ind part of the recording at frame (%d)", retry, startPos);
                endpos = extractLogo->SearchLogo(startPos, false);
            }
            else break;
        }
        FREE(sizeof(*extractLogo), "extractLogo");
        delete extractLogo;
        extractLogo = nullptr;

        if (endpos == 0) {
            dsyslog("cMarkAdStandalone::CheckLogo(): found extracted logo in recording recording directory");
            gettimeofday(&endTime1, nullptr);
            return true;
        }
        else {
            dsyslog("cMarkAdStandalone::CheckLogo(): logo search failed");
            gettimeofday(&endTime1, nullptr);
            return false;
        }
    }
    gettimeofday(&endTime1, nullptr);
    return false;
}


void cMarkAdStandalone::LoadInfo() {
    char *buf;
    if (asprintf(&buf, "%s/info", directory) == -1) return;
    ALLOC(strlen(buf)+1, "buf");

    if (macontext.Config->before) {
        macontext.Info.isRunningRecording = true;
        dsyslog("parameter before is set, markad is called with a running recording");
    }

    FILE *f;
    f = fopen(buf, "r");
    FREE(strlen(buf)+1, "buf");
    free(buf);
    buf = nullptr;
    if (!f) {
        // second try for reel vdr
        if (asprintf(&buf, "%s/info.txt", directory) == -1) return;
        ALLOC(strlen(buf)+1, "buf");
        f = fopen(buf,"r");
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    if (!f) {  // no vdr info file found
        esyslog("vdr info file: not found");
        return;
    }

    char *line = nullptr;
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
            }
            else {
                esyslog("vdr info file: no channel name found");
            }
        }
        if ((line[0] == 'E') && (!bLiveRecording)) {
            long st;
            int result = sscanf(line,"%*c %*10i %20li %6i %*2x %*2x", &st, &length);
            startTime=(time_t)st;
            if (result != 2) {
                esyslog("vdr info file: could not read start time and length");
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
            }
            else dsyslog("frame rate %d (from vdr info)", fps);
        }
        if ((line[0] == 'X') && (!bLiveRecording)) {
            int stream = 0, type = 0;
            char descr[256];
            memset(descr, 0, sizeof(descr));
            int result=sscanf(line, "%*c %3i %3i %250c", &stream, &type, reinterpret_cast<char *>(&descr));
            if ((result != 0) && (result != EOF)) {
                if ((stream == 1) || (stream == 5)) {
                    if ((type != 1) && (type != 5) && (type != 9) && (type != 13)) {
                        dsyslog("aspect ratio 16:9 (from vdr info)");
                        macontext.Info.AspectRatio.num = 16;
                        macontext.Info.AspectRatio.den = 9;
                    }
                    else {
                        dsyslog("aspect ratio 4:3 (from vdr info)");
                        macontext.Info.AspectRatio.num = 4;
                        macontext.Info.AspectRatio.den = 3;
                    }
                }

                if (stream == 2) {
                    if (type == 5) {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr, '2')) {
                            dsyslog("broadcast with DolbyDigital2.0 (from vdr info)");
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr, '5')) {
                            dsyslog("broadcast with DolbyDigital5.1 (from vdr info)");
                        }
                    }
                }
            }
        }
    }
    // create criteria object
    criteria = new cCriteria(macontext.Info.ChannelName);
    ALLOC(sizeof(*criteria), "criteria");

    if ((macontext.Info.AspectRatio.num == 0) && (macontext.Info.AspectRatio.den == 0)) isyslog("no aspect ratio found in vdr info");
    if (line) free(line);

    if ((length) && (startTime)) {
        time_t rStart = GetRecordingStart(startTime, fileno(f));
        if (rStart) {
            dsyslog("cMarkAdStandalone::LoadInfo(): recording start at %s", strtok(ctime(&rStart), "\n"));
            dsyslog("cMarkAdStandalone::LoadInfo(): timer     start at %s", strtok(ctime(&startTime), "\n"));

            //  start offset of broadcast from timer event
            int startEvent = static_cast<int> (startTime - rStart);
            dsyslog("cMarkAdStandalone::LoadInfo(): event start at offset:               %5ds -> %d:%02d:%02dh", startEvent, startEvent / 3600, (startEvent % 3600) / 60, startEvent % 60);
            // start offset of broadcast from VPS event
            int startVPS = vps->GetStart();
            if (startVPS >= 0) {
                dsyslog("cMarkAdStandalone::LoadInfo(): VPS   start at offset:               %5ds -> %d:%02d:%02dh", startVPS, startVPS / 3600, (startVPS % 3600) / 60, startVPS % 60);
                if ((macontext.Info.ChannelName && criteria->GoodVPS()) || (abs(startVPS - startEvent) <= 10 * 60)) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): VPS start event seems to be valid");
                    macontext.Info.tStart = startVPS;
                }
                else dsyslog("cMarkAdStandalone::LoadInfo(): VPS start event seems to be invalid, %ds away from boadcast start event", abs(startVPS - startEvent));
            }
            else dsyslog("cMarkAdStandalone::LoadInfo(): no VPS start event found");


            // try to get broadcast length from VPS start/stop events
            if (macontext.Info.tStart >= 0) {
                int vpsStop = vps->GetStop();
                if (vpsStop > macontext.Info.tStart) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop  event at offset:           %5ds -> %d:%02d:%02dh", vpsStop, vpsStop / 3600, (vpsStop % 3600) / 60, vpsStop % 60);
                    dsyslog("cMarkAdStandalone::LoadInfo(): broadcast length from vdr info file: %5ds -> %d:%02d:%02dh", length, length / 3600, (length % 3600) / 60, length % 60);
                    int lengthVPS = vpsStop - macontext.Info.tStart;
                    int diff      = lengthVPS - length;
                    dsyslog("cMarkAdStandalone::LoadInfo(): broadcast length from VPS events:    %5ds -> %d:%02d:%02dh, %ds longer than length from vdr info file", lengthVPS, lengthVPS / 3600, (lengthVPS % 3600) / 60, lengthVPS % 60, diff);
                    // invalid examples:
                    // -615 (accepted invalid VPS sequence, false stop, running after)
                    // changed from  506 to  298
                    // changed from -620 to 615
                    if ((diff >= 285) || (diff <= -615)) {  // changed from 298 to 285 (how should be longer broadcast from VPS event be valid ?)
                        dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop event seems to be invalid, use length from vdr info file");
                        vps->SetStop(-1);  // set VPS stop event to invalid
                    }
                    else {
                        dsyslog("cMarkAdStandalone::LoadInfo(): VPS events seems to be valid, use length from VPS events");
                        length = lengthVPS;
                    }
                }
            }
            if (vps->IsVPSTimer()) { //  VPS controlled recording start, we guess assume broascast start 45s after recording start
                isyslog("VPS controlled recording start");
                if (macontext.Info.tStart < 0) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): no VPS start event found");
                    macontext.Info.tStart = 45;
                }
            }

            // no valid VPS start event, try to get broadcast start offset and broadcast length from info file
            if (macontext.Info.tStart < 0) {
                macontext.Info.tStart = startEvent;
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
        else macontext.Info.tStart = 0;
    }
    else {
        dsyslog("cMarkAdStandalone::LoadInfo(): start time and length from vdr info file not valid");
        macontext.Info.tStart = 0;
    }
    dsyslog("cMarkAdStandalone::LoadInfo(): broadcast start %is after recording start", macontext.Info.tStart);

    if ((!length) && (!bLiveRecording)) {
        esyslog("cannot read broadcast length from info, marks can be wrong!");
        macontext.Info.AspectRatio.num = 0;
        macontext.Info.AspectRatio.den = 0;
    }
    fclose(f);
    dsyslog("cMarkAdStandalone::LoadInfo(): length of broadcast  %5ds -> %d:%02d:%02dh", length, length / 3600, (length % 3600) / 60, length % 60);
    return;
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
        buf = nullptr;
    }
    if (buf) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    return true;
}


bool cMarkAdStandalone::CreatePidfile() {
    char *buf = nullptr;
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


cMarkAdStandalone::cMarkAdStandalone(const char *directoryParam, sMarkAdConfig *config) {
    setlocale(LC_MESSAGES, "");
    directory        = directoryParam;
    inBroadCast      = false;
    iStopinBroadCast = false;
    indexFile        = nullptr;
    video            = nullptr;
    audio            = nullptr;
    osd              = nullptr;
    length           = 0;
    sleepcnt         = 0;
    waittime         = 0;
    iwaittime        = 0;
    duplicate        = false;
    title[0]         = 0;

    macontext = {};
    macontext.Config = config;

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

    // log hostname and user
    char hostname[64];
    gethostname(hostname, 64);
    dsyslog("running on %s", hostname);
    if (config->cmd) dsyslog("called with parameter cmd = %s", config->cmd);

    //  give vdr markad plugin time to pause this process until recording end
    if (strcmp(config->cmd, "after") == 0) {
        isyslog("started from markad plugin for processing after recording");
        sleep(10);
    }

    // ignore --vps if markad runs during recording
    if ((strcmp(config->cmd, "before") == 0) && config->useVPS) {
        esyslog("markad runs during recording, ignore invalid --vps parameter");
        config->useVPS = false;
    }

    // check avcodec library version
#if LIBAVCODEC_VERSION_INT < LIBAVCODEC_VERSION_DEPRECATED
#error "libavcodec not installed or version not supported, please install or update libavcodec"
#endif
#if LIBAVCODEC_VERSION_INT < LIBAVCODEC_VERSION_VALID
#warning "libavcodec version is deprecated for markad, please update"
#endif

    unsigned int ver = avcodec_version();
    char *libver = nullptr;
    if (asprintf(&libver, "%i.%i.%i", ver >> 16 & 0xFF, ver >> 8 & 0xFF, ver & 0xFF) != -1) {
        ALLOC(strlen(libver)+1, "libver");
        dsyslog("using libavcodec.so.%s (%d) with %i threads", libver, ver, config->threads);
        if (ver != LIBAVCODEC_VERSION_INT) {
            esyslog("libavcodec header version %s", AV_STRINGIFY(LIBAVCODEC_VERSION));
            esyslog("header and library mismatch, do not report decoder bugs");
        }
        if (ver < LIBAVCODEC_VERSION_VALID) isyslog("your libavcodec is deprecated, please update");
        FREE(strlen(libver)+1, "libver");
        free(libver);
    }
    dsyslog("libavcodec config: %s",avcodec_configuration());
    isyslog("on %s", directory);

    if (config->before) sleep(10);

    char *tmpDir = strdup(directory);
#ifdef DEBUG_MEM
    ALLOC(strlen(tmpDir)+1, "tmpDir");
    int memsize_tmpDir = strlen(directory) + 1;
#endif
    char *datePart = strrchr(tmpDir, '/');
    if (!datePart) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): failed to find last '/'");
        FREE(strlen(tmpDir+1), "tmpDir");
        free(tmpDir);
        return;
    }
    *datePart = 0;    // cut off date part

    char *recName = strrchr(tmpDir, '/');  // "/" exists, testet with variable datePart
    if (strstr(recName, "/@")) {
        isyslog("live-recording, disabling pre-/post timer");
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

    if (asprintf(&indexFile, "%s/index", directory) == -1) indexFile = nullptr;
    if (indexFile) ALLOC(strlen(indexFile) + 1, "indexFile");

    // load VPS events
    vps = new cVPS(directory);
    ALLOC(sizeof(*vps), "vps");

    // load data from vdr info file
    LoadInfo();
    if (!macontext.Info.ChannelName) {
        if (asprintf(&macontext.Info.ChannelName, "unknown") == -1) {};
        ALLOC(strlen(macontext.Info.ChannelName) + 1, "macontext.Info.ChannelName");
    }

    // manually extract logo from recording
    if (config->logoExtraction >= 0) {
        extractLogo = new cExtractLogo(macontext.Config->recDir, macontext.Info.ChannelName, macontext.Config->threads, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Info.AspectRatio);
        ALLOC(sizeof(*extractLogo), "extractLogo");
        extractLogo->ManuallyExtractLogo(config->logoExtraction, config->logoWidth, config->logoHeight);
        ALLOC(sizeof(*extractLogo), "extractLogo");
        delete extractLogo;
        return;
    }

    // check if requested decoding parameter are valid for this video codec
    char hwaccel[1] = {0};      //!< no hardware acceleration
    cDecoder *decoderTest = new cDecoder(macontext.Config->recDir, macontext.Config->threads, true, hwaccel, false, false, nullptr); // full decocode, no hwaccel, no force interlaced, no index
    ALLOC(sizeof(*decoderTest), "decoderTest");
    decoderTest->DecodeNextFrame(false);  // decode one video frame to get video info
    dsyslog("cMarkAdStandalone::cMarkAdStandalone(): video characteristics: %s, frame rate %d, type %d, pixel format %d", (decoderTest->IsInterlacedFrame()) ? "interlaced" : "progressive", decoderTest->GetVideoFrameRate(), decoderTest->GetVideoType(), decoderTest->GetVideoPixelFormat());
    // store frameRate for logo extraction
    int frameRate = decoderTest->GetVideoFrameRate();
    // pixel format yuv420p10le (from UHD) does not work with hwaccel
    if (decoderTest->GetVideoPixelFormat() == AV_PIX_FMT_YUV420P10LE) {
        isyslog("FFmpeg does not support hwaccel with pixel format yuv420p10le");
        macontext.Config->hwaccel[0] = 0;
        macontext.Config->forceHW    = false;
    }
    // inform decoder who use hwaccel, the video is interlaaced. In this case this is not possible to detect from decoder because hwaccel deinterlaces frames
    if ((macontext.Config->hwaccel[0] != 0) && decoderTest->IsInterlacedFrame() && (decoderTest->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264)) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): inform decoder with hwaccel about H.264 interlaced video and force full decode");
        macontext.Config->forceInterlaced = true;
        macontext.Config->fullDecode      = true;
    }
    FREE(sizeof(*decoderTest), "decoderTest");
    delete decoderTest;

    // check if we have a logo or we can extract it from recording
    if (!CheckLogo(frameRate) && (config->autoLogo == 0)) {
        isyslog("no logo found, logo detection disabled");
        criteria->SetDetectionState(MT_LOGOCHANGE, false);
    }

    if (macontext.Info.tStart > 1) {
        if ((macontext.Info.tStart < 60) && (!vps->IsVPSTimer())) macontext.Info.tStart = 60;
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
        osd = nullptr;
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (macontext.Info.ChannelName) isyslog("channel: %s", macontext.Info.ChannelName);

    // create index object
    index = new cIndex(macontext.Config->fullDecode);
    ALLOC(sizeof(*index), "index");
    marks.SetIndex(index);

    // create decoder object
    decoder = new cDecoder(macontext.Config->recDir, macontext.Config->threads, macontext.Config->fullDecode, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Config->forceInterlaced, index);
    ALLOC(sizeof(*decoder), "decoder");
}


cMarkAdStandalone::~cMarkAdStandalone() {
    dsyslog("cMarkAdStandalone::~cMarkAdStandalone(): delete object");
    if (abortNow) return;
    marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, true);


    // cleanup used objects
    if (detectLogoStopStart) {
        FREE(sizeof(*detectLogoStopStart), "detectLogoStopStart");
        delete detectLogoStopStart;
    }
    if (indexFile) {
        FREE(strlen(indexFile)+1, "indexFile");
        free(indexFile);
    }
    if (video) {
        FREE(sizeof(*video), "video");
        delete video;
        video = nullptr;
    }
    if (audio) {
        FREE(sizeof(*audio), "audio");
        delete audio;
        audio = nullptr;
    }
    if (osd) {
        FREE(sizeof(*osd), "osd");
        delete osd;
        osd = nullptr;
    }
    if (decoder) {
        if (decoder->GetErrorCount() > 0) isyslog("decoding errors: %d", decoder->GetErrorCount());
    }
    if (evaluateLogoStopStartPair) {
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
    }

    FREE(sizeof(*index), "index");
    delete index;
    FREE(sizeof(*criteria), "criteria");
    delete criteria;

// log statistics
    if ((!abortNow) && (!duplicate)) {
        LogSeparator();

        // broadcast length without advertisement
        dsyslog("recording statistics: -----------------------------------------------------------------------");
        int lengthFrames = marks.Length();
        int lengthSec    = lengthFrames / decoder->GetVideoFrameRate();
        dsyslog("broadcast length without advertisement: %6d frames, %6ds -> %d:%02d:%02dh", marks.Length(), lengthSec, lengthSec / 3600, (lengthSec % 3600) / 60,  lengthSec % 60);

        // recording length from VPS eventsA
        int vpsLength = vps->Length();
        if (vpsLength > 0) {
            dsyslog("recording length from VPS events:                      %6ds -> %d:%02d:%02dh", vpsLength, vpsLength / 3600, (vpsLength % 3600) / 60,  vpsLength % 60);
            int adQuote = 100 * (vpsLength - lengthSec) / vpsLength;
            if (adQuote > 41) esyslog("advertisement quote: %d%% very high, marks can be wrong", adQuote);  // changed from 40 to 41
            else dsyslog("advertisement quote: %d%%", adQuote);
        }
        // log match of VPS events
        if (macontext.Info.ChannelName) vps->LogMatch(macontext.Info.ChannelName, &marks);

        dsyslog("processing statistics: ----------------------------------------------------------------------");
        time_t sec = endTime1.tv_sec - startTime1.tv_sec;
        suseconds_t usec = endTime1.tv_usec - startTime1.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 1 (initial logosearch): time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        sec = endTime2.tv_sec - startTime2.tv_sec;
        usec = endTime2.tv_usec - startTime2.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 2 (mark detection):     time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        sec = endTime3.tv_sec - startTime3.tv_sec;
        usec = endTime3.tv_usec - startTime3.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 3 (mark optimization):  time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        sec = endTime4.tv_sec - startTime4.tv_sec;
        usec = endTime4.tv_usec - startTime4.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 4 (overlap detection):  time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        sec = endTime5.tv_sec - startTime5.tv_sec;
        usec = endTime5.tv_usec - startTime5.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 5 (cut recording):      time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        sec = endTime6.tv_sec - startTime6.tv_sec;
        usec = endTime6.tv_usec - startTime6.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec) > 0) dsyslog("pass 6 (mark pictures):      time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        dsyslog("global statistics: --------------------------------------------------------------------------");
        int decodeTime_s = decodeTime_us / 1000000;
        dsyslog("decoding:                    time %5ds -> %d:%02d:%02dh", decodeTime_s, decodeTime_s / 3600, (decodeTime_s % 3600) / 60,  decodeTime_s % 60);

        gettimeofday(&endAll, nullptr);
        sec = endAll.tv_sec - startAll.tv_sec;
        usec = endAll.tv_usec - startAll.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        double etime = 0;
        etime = sec + ((double) usec / 1000000) - waittime;
        isyslog("duration:                    time %5ds -> %d:%02d:%02dh", static_cast<int> (etime), static_cast<int> (etime / 3600),  (static_cast<int> (etime) % 3600) / 60, static_cast<int> (etime) % 60);
        dsyslog("----------------------------------------------------------------------------------------------");
    }

    if ((osd) && (!duplicate)) {
        if (abortNow) {
            osd->Send("%s '%s'", tr("markad aborted for"), ptitle);
        }
        else {
            osd->Send("%s '%s'", tr("markad finished for"), ptitle);
        }
    }

// cleanup objects used in statistics
    FREE(sizeof(*decoder), "decoder");
    delete decoder;
    if (vps) {
        FREE(sizeof(*vps), "vps");
        delete vps;
        vps = nullptr;
    }
    if (macontext.Info.ChannelName) {
        FREE(strlen(macontext.Info.ChannelName) + 1, "macontext.Info.ChannelName");
        free(macontext.Info.ChannelName);
        macontext.Info.ChannelName = nullptr;
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
           "                  extracts logo to recording directory as pgm files (must be renamed)\n"
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
           "                  only valid together with cmd \"before\"\n"
           "                  start markad immediately when called together with \"before\" as cmd\n"
           "                  if online=1, markad starts online for live-recordings only\n"
           "                     online=2, markad starts online for every recording\n"
           "                  live-recordings are identified by having a '@' in the filename\n"
           "                --svdrphost=<ip/hostname> (default is 127.0.0.1)\n"
           "                  ip/hostname of a remote VDR for OSD messages\n"
           "                --svdrpport=<port> (default is %i)\n"
           "                  port of a remote VDR for OSD messages\n"
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
           "                  full re-encode video generated by --cut\n"
           "                  use it only on powerful CPUs, it will double overall run time\n"
           "                  <streams>  all  = keep all video and audio streams of the recording\n"
           "                             best = only encode best video and best audio stream, drop rest\n"
           "                --hwaccel=<hardware acceleration method>\n"
           "                  use hardware acceleration for decoding\n"
           "                  <hardware acceleration method> all methods supported by FFmpeg (ffmpeg -hide_banner -hwaccels)\n"
           "                                                 e.g.: vdpau, cuda, vaapi, vulkan, ...\n"
           "                --perftest>\n"
           "                  run decoder performance test and compare software and hardware decoder\n"
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
    return EXIT_FAILURE;
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


char *recDir = nullptr;


void freedir(void) {
    if (recDir) free(recDir);
}


int main(int argc, char *argv[]) {
    bool bAfter         = false;
    bool bEdited        = false;
    bool bFork          = false;
    bool bNice          = false;
    bool bImmediateCall = false;
    int niceLevel       = 19;
    int ioprio_class    = 3;
    int ioprio          = 7;
    char *tok           = nullptr;
    char *str           = nullptr;
    int ntok            = 0;
    struct sMarkAdConfig config = {};

    gettimeofday(&startAll, nullptr);

    // set defaults
    config.logoExtraction = -1;
    config.logoWidth      =  0;
    config.logoHeight     =  0;
    config.threads        = -1;
    strcpy(config.svdrphost, "127.0.0.1");
    strcpy(config.logoCacheDirectory, "/var/lib/markad");

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
            {"background",   0, 0, 'b'},
            {"disable",      1, 0, 'd'},
            {"logocachedir", 1, 0, 'l'},
            {"priority",     1, 0, 'p'},
            {"ioprio",       1, 0, 'r'},
            {"verbose",      0, 0, 'v'},
            {"backupmarks",  0, 0, 'B'},
            {"extractlogo",  1, 0, 'L'},
            {"OSD",          0, 0, 'O' },
            {"log2rec",      0, 0, 'R'},
            {"threads",      1, 0, 'T'},
            {"version",      0, 0, 'V'},
            {"markfile",     1, 0,  1},
            {"loglevel",     1, 0,  2},
            {"online",       2, 0,  3},
            {"nopid",        0, 0,  4},
            {"svdrphost",    1, 0,  5},
            {"svdrpport",    1, 0,  6},
            {"cut",          0, 0,  7},
            {"ac3reencode",  0, 0,  8},
            {"vps",          0, 0,  9},
            {"logfile",      1, 0, 10},
            {"autologo",     1, 0, 11},
            {"fulldecode",   0, 0, 12},
            {"fullencode",   1, 0, 13},
            {"pts",          0, 0, 14},     // undocumented, only for development use
            {"hwaccel",      1, 0, 15},
            {"perftest",     0, 0, 16},     // undocumented, only for development use

            {0, 0, 0, 0}
        };

        int option = getopt_long(argc, argv, "bd:i:l:p:r:vBGIL:ORT:V", long_options, &option_index);
        if (option == -1) break;

        switch (option) {
        case 'b':
            // --background
            bFork = SYSLOG = true;
            break;
        case 'l':
            if ((strlen(optarg) + 1) > sizeof(config.logoCacheDirectory)) {
                fprintf(stderr, "markad: logo path too long: %s\n", optarg);
                return EXIT_FAILURE;
            }
            strncpy(config.logoCacheDirectory, optarg, sizeof(config.logoCacheDirectory) - 1);
            break;
        case 'p':
            // --priority
            if (isnumber(optarg) || *optarg == '-') niceLevel = atoi(optarg);
            else {
                fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                return EXIT_FAILURE;
            }
            bNice = true;
            break;
        case 'r':
            // --ioprio
            str=strchr(optarg, ',');
            if (str) {
                *str = 0;
                ioprio = atoi(str + 1);
                *str = ',';
            }
            ioprio_class = atoi(optarg);
            if ((ioprio_class < 1) || (ioprio_class > 3)) {
                fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                return EXIT_FAILURE;
            }
            if ((ioprio < 0) || (ioprio > 7)) {
                fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                return EXIT_FAILURE;
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
                        return EXIT_FAILURE;
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
                str = nullptr;
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
            return EXIT_SUCCESS;
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
                return EXIT_FAILURE;
            }
            strncpy(config.markFileName, optarg, sizeof(config.markFileName) - 1);
            break;
        case 2: // --loglevel
            SysLogLevel = atoi(optarg);
            if (SysLogLevel > 10) SysLogLevel = 10;
            if (SysLogLevel <  0) SysLogLevel =  2;
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
                return EXIT_FAILURE;
            }
            break;
        case 4: // --nopid
            config.noPid = true;
            break;
        case 5: // --svdrphost
            if ((strlen(optarg) + 1) > sizeof(config.svdrphost)) {
                fprintf(stderr, "markad: svdrphost too long: %s\n", optarg);
                return EXIT_FAILURE;
            }
            strncpy(config.svdrphost, optarg, sizeof(config.svdrphost) - 1);
            break;
        case 6: // --svdrpport
            if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536) {
                config.svdrpport = atoi(optarg);
            }
            else {
                fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 7: // --cut
            config.MarkadCut = true;
            break;
        case 8: // --ac3reencode
            config.ac3ReEncode = true;
            break;
        case 9: // --vps
            config.useVPS = true;
            break;
        case 10: // --logfile
            strncpy(config.logFile, optarg, sizeof(config.logFile) - 1);
            config.logFile[sizeof(config.logFile) - 1] = 0;
            break;
        case 11: // --autologo
            if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 2) config.autoLogo = atoi(optarg);
            else {
                fprintf(stderr, "markad: invalid autologo value: %s\n", optarg);
                return EXIT_FAILURE;
            }
            if (config.autoLogo == 1) {
                fprintf(stderr,"markad: --autologo=1 is removed, will use --autologo=2 instead, please update your configuration\n");
                config.autoLogo = 255;
            }
            break;
        case 12: // --fulldecode
            config.fullDecode = true;
            break;
        case 13: // --fullencode
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
                        return EXIT_FAILURE;
                    }
                    break;
                default:
                    break;
                }
                str = nullptr;
                ntok++;
            }
            break;
        case 14: // --pts
            config.pts = true;
            break;
        case 15: // --hwaccel
            str = optarg;
            tok = strtok(str, ",");
            if (tok) {
                if ((strlen(tok) + 1) > sizeof(config.hwaccel)) {
                    fprintf(stderr, "markad: hwaccel type too long: %s\n", tok);
                    return EXIT_FAILURE;
                }
                strncpy(config.hwaccel, tok, sizeof(config.hwaccel) - 1);
                tok = strtok(nullptr, ",");
                if ((tok) && (strcmp(tok, "force") == 0)) config.forceHW = true;
            }
            else {
                if ((strlen(optarg) + 1) > sizeof(config.hwaccel)) {
                    fprintf(stderr, "markad: hwaccel type too long: %s\n", optarg);
                    return EXIT_FAILURE;
                }
                strncpy(config.hwaccel, optarg, sizeof(config.hwaccel) - 1);
            }
            break;
        case 16: // --perftest
            config.perftest = true;
            break;
        default:
            printf ("markad: invalid option -%c\n", option);
        }
    }

    if (optind < argc) {
        while (optind < argc) {
            if (strcmp(argv[optind], "after" ) == 0 ) {
                config.cmd = argv[optind];
                bAfter = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "before" ) == 0 ) {
                config.cmd = argv[optind];
                if (!config.online) config.online = 1;
                config.before = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "edited" ) == 0 ) {
                config.cmd = argv[optind];
                bEdited = true;
            }
            else if (strcmp(argv[optind], "nice" ) == 0 ) {
                config.cmd = argv[optind];
                bNice = true;
            }
            else if (strcmp(argv[optind], "-" ) == 0 ) {
                config.cmd = argv[optind];
                bImmediateCall = true;
            }
            else {
                if (strstr(argv[optind], ".rec") != nullptr ) {
                    recDir = realpath(argv[optind], nullptr);
                    if (!recDir) {
                        fprintf(stderr, "invalid recording directory: %s\n", argv[optind]);
                        return EXIT_FAILURE;
                    }
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
    if (bEdited) return EXIT_SUCCESS;
    if ((bAfter) && (config.online)) return EXIT_SUCCESS;
    if ((config.before) && (config.online == 1) && recDir && (!strchr(recDir, '@'))) return EXIT_SUCCESS;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || config.before || bAfter || bNice) && recDir ) {
        // if bFork is given go in background
        if ( bFork ) {
#ifdef POSIX
            //close_files();
            pid_t pid = fork();
            if (pid < 0) {
                const char *err = strerror(errno);
                fprintf(stderr, "%s\n", err);
                return EXIT_FAILURE;
            }
            if (pid != 0) {
                return EXIT_SUCCESS; // initial program immediately returns
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
            return EXIT_FAILURE;
        }

        if (!S_ISDIR(statbuf.st_mode)) {
            fprintf(stderr, "%s is not a directory\n", recDir);
            return EXIT_FAILURE;
        }

        if (access(recDir, W_OK|R_OK) == -1) {
            fprintf(stderr,"cannot access %s\n", recDir);
            return EXIT_FAILURE;
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

        // init cMarkAdStandalone here, we need now log to recording
        cMarkAdStandalone *cmasta = new cMarkAdStandalone(recDir, &config);
        if (!cmasta) return EXIT_FAILURE;
        ALLOC(sizeof(*cmasta), "cmasta");

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
        dsyslog("markad IO priority class  %i",IOPrio);

        dsyslog("parameter --logocachedir is set to %s", config.logoCacheDirectory);
        dsyslog("parameter --threads is set to %i", config.threads);
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
        if (config.hwaccel[0] != 0) dsyslog("parameter --hwaccel=%s is set", config.hwaccel);
        else dsyslog("use software decoder/encoder");

        if (config.logoExtraction == -1) {
            // performance test
            if (config.perftest) {
                cTest *test = new cTest(config.recDir, config.fullDecode, config.hwaccel);
                test->Perf();
                delete test;
            }
            else {

                // detect marks
                gettimeofday(&startTime2, nullptr);
                cmasta->Recording();
                gettimeofday(&endTime2, nullptr);

                // logo mark optimization
                gettimeofday(&startTime3, nullptr);
                cmasta->LogoMarkOptimization();      // logo mark optimization
                gettimeofday(&endTime3, nullptr);

                // overlap detection
                gettimeofday(&startTime4, nullptr);
                cmasta->ProcessOverlap();            // overlap and closing credits detection
                gettimeofday(&endTime4, nullptr);

                // minor mark position optimization
                cmasta->BlackScreenOptimization();   // mark optimization with black scene
                cmasta->SilenceOptimization();       // mark optimization with mute scene
                cmasta->LowerBorderOptimization();   // mark optimization with lower border
                cmasta->SceneChangeOptimization();   // final optimization with scene changes (if we habe nothing else, try this as last resort)

                // video cut
                if (config.MarkadCut) {
                    gettimeofday(&startTime5, nullptr);
                    cmasta->MarkadCut();
                    gettimeofday(&endTime5, nullptr);
                }

                // write debug mark pictures
                gettimeofday(&startTime6, nullptr);
#ifdef DEBUG_MARK_FRAMES
                cmasta->DebugMarkFrames(); // write frames picture of marks to recording directory
#endif

            }
        }
        FREE(sizeof(*cmasta), "cmasta");
        delete cmasta;
        cmasta = nullptr;

        gettimeofday(&endTime6, nullptr);

#ifdef DEBUG_MEM
        memList();
#endif
        return EXIT_SUCCESS;
    }
    return usage(config.svdrpport);
}
