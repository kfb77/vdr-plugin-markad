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
double decodeTime_ms           = 0;

struct timeval startAll, endAll = {};


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
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): timer or VPS start:    (%6d)  %3d:%02dmin", startFrame, static_cast<int>(startFrame / decoder->GetVideoFrameRate() / 60), startFrame /  decoder->GetVideoFrameRate() % 60);

    if (!length) {
        dsyslog("CalculateCheckPositions(): length of recording not found, set to 100h");
        length = 100 * 60 * 60; // try anyway, set to 100h
        startFrame = decoder->GetVideoFrameRate() * 2 * 60;  // assume default pretimer of 2min
    }

    if (startFrame < 0) {   // recodring start is too late
        isyslog("recording started too late, set start mark to start of recording");
        sMarkAdMark mark = {};
        mark.position    = 1;  // do not use position 0 because this will later be deleted
        mark.framePTS    = index->GetStartPTS();
        mark.type        = MT_RECORDINGSTART;
        AddMark(&mark);
        startFrame = decoder->GetVideoFrameRate() * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }

    startA = startFrame;
    stopA  = startA + decoder->GetVideoFrameRate() * length;
    packetCheckStart = startA + decoder->GetVideoFrameRate() * (1.6 * MAX_ASSUMED) ; //  adjust for later broadcast start, changed from 1.5 to 1.6
    packetCheckStop  = startFrame + decoder->GetVideoFrameRate() * (length + (1.5 * MAX_ASSUMED));
    packetEndPart    = stopA - (decoder->GetVideoFrameRate() * MAX_ASSUMED);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:      %4ds  %3d:%02dmin", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame:   (%6d)  %3d:%02dmin", startA, static_cast<int>(startA / decoder->GetVideoFrameRate() / 60), static_cast<int>(startA / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:    (%6d)  %3d:%02dmin", stopA, static_cast<int>(stopA / decoder->GetVideoFrameRate() / 60), static_cast<int>(stopA / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): check start set to:    (%6d)  %3d:%02dmin", packetCheckStart, static_cast<int>(packetCheckStart / decoder->GetVideoFrameRate() / 60), static_cast<int>(packetCheckStart / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): check stop set to:     (%6d)  %3d:%02dmin", packetCheckStop, static_cast<int>(packetCheckStop / decoder->GetVideoFrameRate() / 60), static_cast<int>(packetCheckStop / decoder->GetVideoFrameRate()) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): start end part set to: (%6d)  %3d:%02dmin", packetEndPart, static_cast<int>(packetEndPart / decoder->GetVideoFrameRate() / 60), static_cast<int>(packetEndPart / decoder->GetVideoFrameRate()) % 60);
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
        const cMark *startMark = marks.GetFirst();
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
    end = marks.GetAround(2 * MAX_ASSUMED * decoder->GetVideoFrameRate(), stopA, MT_HBORDERSTOP);  // 10 minutes, more trust for hborder marks
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
                // if we use MT_HBORDERCHANGE and last mark is MT_HBORDERSTOP, this is broadcast end
                // even it is far before assumed stop, maybe recording length is wrong
                if (diffAssumed <= (6 * MAX_ASSUMED)) {
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
                evaluateLogoStopStartPair->SetIsAdInFrameAroundStop(end->position, STATUS_DISABLED);  // prevent to false detect hborder as adinframe
            }
        }
        // optimize hborder end mark with logo stop mark in case of next broadcast is also with hborder and too early hborder stop from closing credits overlays hborder
        // check sequence MT_HBORDERSTOP (end) -> MT_LOGOSTOP -> MT_HBORDERSTART (start of next broadcast)
        const cMark *logoStart = marks.GetNext(end->position, MT_LOGOSTART);
        logoStop         = marks.GetNext(end->position, MT_LOGOSTOP);
        hborderStart     = marks.GetNext(end->position, MT_HBORDERSTART);
        if (logoStop && hborderStart &&
                (!logoStart || (logoStart->position > logoStop->position))) {  // no logo start between hborder stop (end mark) and logo stop
            int hBorderStopLogoStop  = 1000 * (logoStop->position     - end->position)      / decoder->GetVideoFrameRate();
            int logoStophBorderStart = 1000 * (hborderStart->position - logoStop->position) /  decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_HBORDERSTOP (%d) -> %dms -> MT_LOGOSTOP (%d) -> %dms -> MT_HBORDERSTART (%d)", end->position, hBorderStopLogoStop, logoStop->position, logoStophBorderStart, hborderStart->position);
            // valid example
            // MT_HBORDERSTOP (196569) -> 28000ms -> MT_LOGOSTOP (197269) -> 6040ms -> MT_HBORDERSTART (197420) RTLZWEI
            if ((hBorderStopLogoStop <= 28000) && (logoStophBorderStart >= 0) && (logoStophBorderStart <= 6040)) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): hborder end mark (%d) from closing credits overlays hborder, use logo stop after (%d)", end->position, logoStop->position);
                end = logoStop;
                evaluateLogoStopStartPair->SetIsAdInFrameAroundStop(end->position, STATUS_DISABLED);  // closing credits overlay hborder, prevent to false detect this as adinframe
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): no MT_HBORDERSTOP end mark found");

    // cleanup false vborder stop/start from dark scene
    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): cleanup false vborder stop/start from dark scene");
    if (end && criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
        const cMark *stopMark  = end;  // can be logo stop mark selected above
        const cMark *startMark = marks.GetPrev(stopMark->position, MT_HBORDERSTART);
        while (stopMark && startMark) {
            marks.DelFromTo(startMark->position, stopMark->position, MT_VBORDERCHANGE, 0xF0);
            stopMark                = marks.GetPrev(startMark->position, MT_HBORDERSTOP);
            if (stopMark) startMark = marks.GetPrev(stopMark->position, MT_HBORDERSTART);
        }
    }
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
                    CleanupUndetectedInfoLogo(end); // we are sure this is correct end, cleanup invalid logo marks
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
    else evaluateLogoStopStartPair->SetDecoder(decoder);

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
        evaluateLogoStopStartPair->SetIsAdInFrameAroundStop(end->position, STATUS_DISABLED);  // before closing credits oder separator there is no ad in frame
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
// valid examples: 105, 117, 240, 292
#define MAX_LOGO_BEFORE_ASSUMED 292
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
            const cMark *nextLogoStop = nullptr;
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
        if (criteria->GoodVPS()) {
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
        // if we use logo stop in recording with a channel mark double episode, no ad in frame can be before end mark because we found no channel stop mark
        if (criteria->GetMarkTypeState(MT_CHANNELCHANGE) == CRITERIA_USED) {
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): channel mark double episode, no ad in frame before end mark (%d)", end->position);
            evaluateLogoStopStartPair->SetIsAdInFrameAroundStop(end->position, STATUS_DISABLED);  // prevent to false detect ad in frame
        }
    }
    else dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no MT_LOGOSTOP mark found");
    return end;
}



// detect short logo stop/start short after final start mark or short before final end mark
// they can be logo detection failure, undetected info logos, introduction logos or text previews over the logo (e.g. SAT.1)
// short after a valid start mark/short before a valid stop mark can not be a valid logo stop/start pair
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
            // example of valid logo stop/start pair in start part
            // MT_LOGOSTART (12669) -> 146520ms -> MT_LOGOSTOP (16332) ->   1320ms -> MT_LOGOSTART (16365)  -> Comedy Central: short first broadcast part
            //
            // example of invald logo stop/start pairs in start part
            // MT_LOGOSTART ( 5343) ->  33280ms -> MT_LOGOSTOP ( 6175) ->   1240ms -> MT_LOGOSTART ( 6206)
            // MT_LOGOSTART ( 5439) ->  33320ms -> MT_LOGOSTOP ( 6272) ->   1120ms -> MT_LOGOSTART ( 6300)
            // MT_LOGOSTART ( 5439) ->  41040ms -> MT_LOGOSTOP ( 6465) ->    400ms -> MT_LOGOSTART ( 6475)
            // MT_LOGOSTART (13421) ->  55220ms -> MT_LOGOSTOP (16182) ->    900ms -> MT_LOGOSTART (16227)  -> arte HD: logo detection failure
            // MT_LOGOSTART ( 9442) -> 157000ms -> MT_LOGOSTOP (13367) ->   6160ms -> MT_LOGOSTART (13521)  -> Nickelodeon: logo detection failure  (conflict)
            if ((deltaStop <= 55220) && (adLength <= 1240)) {
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
            // no info logo sequence, do not delete
            // logo stop (99273) -> 14760ms -> start (99642) -> 9400ms -> end mark (99877)
            //
            // info logo sequence before end mark, delete marks
            // logo stop (77361) -> 12840ms -> start (77682) -> 71680ms -> end mark (79474)
            // logo stop (78852) -> 12720ms -> start (79170) -> 75400ms -> end mark (81055)
            if ((stopStart <= 30000) &&
                    (startEnd > 9400) && (startEnd <= 75400)) {
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
                    const cMark *nextLogoStop = marks.GetNext(mark->position, MT_LOGOSTOP);
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
        // check sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTART (mark) -> MT_LOGOSTOP
        // long broadcast part after valid logo start mark required to prevent false positiv
        cMark *silenceStop  = silenceMarks.GetPrev(mark->position, MT_SOUNDSTART);
        const cMark *nextLogoStop = marks.GetNext(mark->position, MT_LOGOSTOP);
        int nextLogoStopPosition = decoder->GetPacketNumber();  // start mark can be last mark from detected start part, use current read position
        if (nextLogoStop) nextLogoStopPosition = nextLogoStop->position;
        if (silenceStop) {
            cMark *silenceStart = silenceMarks.GetPrev(silenceStop->position, MT_SOUNDSTOP);
            if (silenceStart) {
                cMark *logoStop = marks.GetPrev(silenceStart->position, MT_LOGOSTOP);
                if (logoStop) {
                    int logoStopSilenceStart    = 1000 * (silenceStart->position - logoStop->position)     / decoder->GetVideoFrameRate();
                    int silenceStartSilenceStop = 1000 * (silenceStop->position  - silenceStart->position) / decoder->GetVideoFrameRate();
                    int silenceStopLogoStart    = 1000 * (mark->position         - silenceStop->position)  / decoder->GetVideoFrameRate();
                    int logoStartLogoStop       = 1000 * (nextLogoStopPosition   - mark->position)         / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%5d) -> %6dms -> MT_SOUNDSTOP (%5d) -> %5dms -> MT_SOUNDSTART (%5d) -> %5dms -> MT_LOGOSTART (%5d) -> %dms -> MT_LOGOSTOP (%d) -> %s", logoStop->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, mark->position, logoStartLogoStop, nextLogoStopPosition, macontext.Info.ChannelName);
// valid logo start example
// MT_LOGOSTOP ( 4496) ->    560ms -> MT_SOUNDSTOP ( 4510) ->   160ms -> MT_SOUNDSTART ( 4514) ->  1520ms -> MT_LOGOSTART ( 4548)
// MT_LOGOSTOP ( 3536) ->   1520ms -> MT_SOUNDSTOP ( 3574) ->   440ms -> MT_SOUNDSTART ( 3585) ->  1880ms -> MT_LOGOSTART ( 3632)
// MT_LOGOSTOP ( 6960) ->   6120ms -> MT_SOUNDSTOP ( 7113) ->   160ms -> MT_SOUNDSTART ( 7117) ->    40ms -> MT_LOGOSTART ( 7118)                                    -> VOX
// MT_LOGOSTOP (11369) ->   5160ms -> MT_SOUNDSTOP (11498) ->   760ms -> MT_SOUNDSTART (11517) ->    80ms -> MT_LOGOSTART (11519)                                    -> Comedy Central
// MT_LOGOSTOP (12724) ->  13520ms -> MT_SOUNDSTOP (13062) ->   360ms -> MT_SOUNDSTART (13071) ->   120ms -> MT_LOGOSTART (13074)                                    -> Comedy Central
// MT_LOGOSTOP ( 5923) ->  10200ms -> MT_SOUNDSTOP ( 6178) ->   840ms -> MT_SOUNDSTART ( 6199) ->    80ms -> MT_LOGOSTART ( 6201)                                    -> Comedy Central
// MT_LOGOSTOP (10171) ->  12280ms -> MT_SOUNDSTOP (10478) ->   880ms -> MT_SOUNDSTART (10500) ->  1240ms -> MT_LOGOSTART (10531)                                    -> DMAX
// MT_LOGOSTOP (10074) -> 125760ms -> MT_SOUNDSTOP (13218) ->   120ms -> MT_SOUNDSTART (13221) ->   680ms -> MT_LOGOSTART (13238) -> 251520ms -> MT_LOGOSTOP (19526) -> Comedy_Central
//
// invalid logo start example
// MT_LOGOSTOP (  204) ->  28460ms -> MT_SOUNDSTOP ( 1627) ->   240ms -> MT_SOUNDSTART ( 1639) ->  1840ms -> MT_LOGOSTART ( 1731)
// MT_LOGOSTOP (10728) ->     80ms -> MT_SOUNDSTOP (10730) ->   480ms -> MT_SOUNDSTART (10742) ->   600ms -> MT_LOGOSTART (10757)
// MT_LOGOSTOP ( 1984) ->   6840ms -> MT_SOUNDSTOP ( 2155) ->   280ms -> MT_SOUNDSTART ( 2162) ->  1840ms -> MT_LOGOSTART ( 2208) -> 21520ms -> MT_LOGOSTOP (2746)
// MT_LOGOSTOP ( 1393) ->    480ms -> MT_SOUNDSTOP ( 1405) ->   280ms -> MT_SOUNDSTART ( 1412) ->   360ms -> MT_LOGOSTART ( 1421) -> 22520ms -> MT_LOGOSTOP (1984)
// MT_LOGOSTOP ( 4160) ->    760ms -> MT_SOUNDSTOP ( 4179) ->   400ms -> MT_SOUNDSTART ( 4189) ->  1440ms -> MT_LOGOSTART ( 4225) -> 28360ms -> MT_LOGOSTOP (4934) -> RTL Television
                    if (    (logoStopSilenceStart    >= 560) && (logoStopSilenceStart    <= 125760) &&
                            (silenceStartSilenceStop >= 120) && (silenceStartSilenceStop <=    880) &&
                            (silenceStopLogoStart    >=  40) && (silenceStopLogoStart    <=   1880) &&
                            (logoStartLogoStop > 28360)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence before logo start is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence before logo start is invalid", mark->position);
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
// MT_LOGOSTOP (  6556) -> 42040ms -> MT_SOUNDSTOP (  7607) ->  360ms -> MT_LOGOSTART (  7616) ->   40ms -> MT_SOUNDSTART (  7617) -> RTL Television
// MT_LOGOSTOP (  6774) -> 36560ms -> MT_SOUNDSTOP (  7688) ->  360ms -> MT_LOGOSTART (  7697) ->    0ms -> MT_SOUNDSTART (  7697) -> RTL Television
// MT_LOGOSTOP ( 13138) -> 13520ms -> MT_SOUNDSTOP ( 13476) ->  480ms -> MT_LOGOSTART ( 13488) ->   80ms -> MT_SOUNDSTART ( 13490) -> Comedy Central
// MT_LOGOSTOP (  9990) -> 13800ms -> MT_SOUNDSTOP ( 10335) ->  200ms -> MT_LOGOSTART ( 10340) ->   80ms -> MT_SOUNDSTART ( 10342) -> Comedy Central
//
// invalid example
// MT_LOGOSTOP (  1358) ->   920ms -> MT_SOUNDSTOP (  1381) ->  280ms -> MT_LOGOSTART (  1388) ->    0ms -> MT_SOUNDSTART (  1388) -> RTL Television preview start
// MT_LOGOSTOP (   181) ->  4760ms -> MT_SOUNDSTOP (   300) ->  200ms -> MT_LOGOSTART (   305) ->    0ms -> MT_SOUNDSTART (   305) -> RTL Television preview start
                    if ((logoStopSilenceStart > 4760) && (logoStopSilenceStart <= 42040) &&
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
        // silence between logo stop and logo start of next broadcast
        cMark *logoStartBefore = marks.GetPrev(mark->position, MT_LOGOSTART);
        if (logoStartBefore) {
            cMark *silenceStart = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP); // silence can start at the same position as logo stop
            if (silenceStart) {
                cMark *silenceStop = silenceMarks.GetNext(silenceStart->position, MT_SOUNDSTART);
                if (silenceStop) {
                    cMark *startAfter = marks.GetNext(silenceStop->position, MT_START, 0x0F);
                    if (startAfter) {
                        int logoStartLogoStop       = 1000 * (mark->position         - logoStartBefore->position) / decoder->GetVideoFrameRate();
                        int logoStopSilenceStart    = 1000 * (silenceStart->position - mark->position)            / decoder->GetVideoFrameRate();
                        int silenceStartSilenceStop = 1000 * (silenceStop->position  - silenceStart->position)    / decoder->GetVideoFrameRate();
                        int silenceStopLogoStart    = 1000 * (startAfter->position   - silenceStop->position)     / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTART (%6d) -> %7ds -> MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %5dms -> MT_START (%6d) -> %s", logoStartBefore->position, logoStartLogoStop, mark->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, startAfter->position, macontext.Info.ChannelName);
// valid sequence
// MT_LOGOSTART ( 16056) -> 1505140s -> MT_LOGOSTOP ( 91313) ->  140ms -> MT_SOUNDSTOP ( 91320) ->  180ms -> MT_SOUNDSTART ( 91329) ->  1920ms -> MT_START ( 91425) -> RTL Television
// MT_LOGOSTART ( 84526) ->   10400s -> MT_LOGOSTOP ( 84786) ->  160ms -> MT_SOUNDSTOP ( 84790) ->   80ms -> MT_SOUNDSTART ( 84792) ->  1920ms -> MT_START ( 84840) -> Pro7 MAXX
// MT_LOGOSTART ( 81870) ->   10400s -> MT_LOGOSTOP ( 82130) ->    0ms -> MT_SOUNDSTOP ( 82130) ->  320ms -> MT_SOUNDSTART ( 82138) ->  1840ms -> MT_START ( 82184) -> Pro7 MAXX
// MT_LOGOSTART ( 56844) ->   31760s -> MT_LOGOSTOP ( 57638) ->  840ms -> MT_SOUNDSTOP ( 57659) ->  440ms -> MT_SOUNDSTART ( 57670) ->  2360ms -> MT_START ( 57729) -> Comedy Central
// MT_LOGOSTART ( 56372) ->   26000s -> MT_LOGOSTOP ( 57022) ->   80ms -> MT_SOUNDSTOP ( 57024) -> 1160ms -> MT_SOUNDSTART ( 57053) ->  2440ms -> MT_START ( 57114) -> Comedy Central
// MT_LOGOSTART ( 79759) ->  224920s -> MT_LOGOSTOP ( 85382) ->  200ms -> MT_SOUNDSTOP ( 85387) ->   40ms -> MT_SOUNDSTART ( 85388) -> 27560ms -> MT_START ( 86077) -> VOX
// MT_LOGOSTART ( 79759) ->  224880s -> MT_LOGOSTOP ( 85381) ->  240ms -> MT_SOUNDSTOP ( 85387) ->   40ms -> MT_SOUNDSTART ( 85388) ->   240ms -> MT_START ( 85394) -> VOX
// MT_LOGOSTART ( 96625) ->   11840s -> MT_LOGOSTOP ( 96921) -> 1640ms -> MT_SOUNDSTOP ( 96962) ->   80ms -> MT_SOUNDSTART ( 96964) ->  1320ms -> MT_START ( 96997) -> TLC
// MT_LOGOSTART ( 37067) ->   26000s -> MT_LOGOSTOP ( 37717) ->  880ms -> MT_SOUNDSTOP ( 37739) ->  160ms -> MT_SOUNDSTART ( 37743) -> 14960ms -> MT_LOGOT ( 38117) -> Comedy_Central
// MT_LOGOSTART ( 55133) ->   37680s -> MT_LOGOSTOP ( 56075) ->  840ms -> MT_SOUNDSTOP ( 56096) ->   40ms -> MT_SOUNDSTART ( 56097) ->  2120ms -> MT_START ( 56150) -> Comedy_Central
//
// invalid sequence
// MT_LOGOSTART ( 87985) ->    1400s -> MT_LOGOSTOP ( 88020) -> 4840ms -> MT_SOUNDSTOP ( 88141) ->  160ms -> MT_SOUNDSTART ( 88145) ->  8400ms -> MT_START ( 88355) -> DMAX
// MT_LOGOSTART ( 48324) ->   95520s -> MT_LOGOSTOP ( 50712) ->  240ms -> MT_SOUNDSTOP ( 50718) ->  320ms -> MT_SOUNDSTART ( 50726) -> 12720ms -> MT_START ( 51044) -> Comedy_Central
                        if (  (((logoStartLogoStop >= 10400)  && (logoStartLogoStop <= 37680)) ||  // end of undetected info logo or short logo interuption from TLC/Comedy_Central
                                (logoStartLogoStop >  95520)) &&                                   // no info logo before
                                (logoStopSilenceStart    <= 1640) &&   // silence start short after end mark
                                (silenceStartSilenceStop >=   40) && (silenceStartSilenceStop <=  1160) &&
                                (silenceStopLogoStart    >=  240) && (silenceStopLogoStart    <= 27560)) {
                            dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence between end mark and next start mark  is valid", mark->position);
                            return true;
                        }
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence between end mark and next start mark is invalid", mark->position);
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
        // black screen between logo stop and logo start
        cMark *blackStop = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART);
        if (blackStop) {  // from above
            cMark *blackStart = blackMarks.GetPrev(blackStop->position, MT_NOBLACKSTOP);
            if (blackStart) {
                cMark *stopBefore = marks.GetPrev(blackStart->position, MT_LOGOSTOP);
                if (stopBefore) {
                    const cMark *stopAfter = marks.GetNext(mark->position, MT_LOGOSTOP);
                    int diffLogoStartLogoStop  = INT_MAX;
                    int stopAfterPosition      = INT_MAX;
                    if (stopAfter) {
                        stopAfterPosition           = stopAfter->position;
                        diffLogoStartLogoStop       = 1000 * (stopAfter->position  - mark->position)       / decoder->GetVideoFrameRate();
                    }
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - stopBefore->position) / decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart  = 1000 * (mark->position       - blackStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP(%5d)->%6dms->MT_NOBLACKSTOP(%5d)->%4dms->MT_NOBLACKSTART(%5d)->%5dms->MT_LOGOSTART(%5d)->%10dms->MT_LOGOSTOP(%10d) -> %s", stopBefore->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, mark->position, diffLogoStartLogoStop, stopAfterPosition, macontext.Info.ChannelName);
// black screen short after end mark of previous broadcast
// valid example
//
// invalid example
// MT_LOGOSTOP(11443)->    40ms->MT_NOBLACKSTOP(11444)-> 120ms->MT_NOBLACKSTART(11447)-> 8440ms->MT_LOGOSTART(11658)->      9760ms->MT_LOGOSTOP(     11902)
// MT_LOGOSTOP( 1908)->   560ms->MT_NOBLACKSTOP( 1922)-> 640sm->MT_NOBLACKSTART( 1938)-> 5000ms->MT_LOGOSTART( 2063)->    192200ms->MT_LOGOSTOP(      6868)
// MT_LOGOSTOP(  445)->   520ms->MT_NOBLACKSTOP(  458)-> 640ms->MT_NOBLACKSTART(  474)->24160ms->MT_LOGOSTART( 1078)->    231720ms->MT_LOGOSTOP(      6871)
// MT_LOGOSTOP( 8166)->  2160ms->MT_NOBLACKSTOP( 8220)-> 160ms->MT_NOBLACKSTART( 8224)-> 5800ms->MT_LOGOSTART( 8369)->2147483647ms->MT_LOGOSTOP(2147483647)
                    if ((diffLogoStopBlackStart <= 2159) && (diffBlackStartBlackStop >= 40) && (diffBlackStopLogoStart <= 38720) && (diffLogoStartLogoStop > 231720)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence short after previous stop is valid");
                        return true;
                    }

// black screen short before start mark of broadcast
// valid logo start mark example
// MT_LOGOSTOP( 8177)->31200ms->MT_NOBLACKSTOP( 8957)->120ms->MT_NOBLACKSTART(   8960)-> 680ms->MT_LOGOSTART( 8977)->2147483647ms->MT_LOGOSTOP(2147483647) Disney Channel
// MT_LOGOSTOP( 6035)-> 8240ms->MT_NOBLACKSTOP( 6241)->160ms->MT_NOBLACKSTART(   6245)->1400ms->MT_LOGOSTART( 6280)->2147483647ms->MT_LOGOSTOP(2147483647) DMAX
// MT_LOGOSTOP( 9047)->47380ms->MT_NOBLACKSTOP(11416)->180ms->MT_NOBLACKSTART(  11425)->1660ms->MT_LOGOSTART(11508)->2147483647ms->MT_LOGOSTOP(2147483647) KiKA
// MT_LOGOSTOP( 8313)->10800ms->MT_NOBLACKSTOP( 8583)->200ms->MT_NOBLACKSTART(   8588)->5840ms->MT_LOGOSTART( 8734)->2147483647ms->MT_LOGOSTOP(2147483647) sixx
// MT_LOGOSTOP( 6931)->21280ms->MT_NOBLACKSTOP( 7463)->160ms->MT_NOBLACKSTART(   7467)->4760ms->MT_LOGOSTART( 7586)->2147483647ms->MT_LOGOSTOP(2147483647) RTL Television
//
// invalid logo start mark example
// logo start with black screen before preview
// MT_LOGOSTOP( 6578)->165640ms->MT_NOBLACKSTOP(10719)-> 840ms->MT_NOBLACKSTART(10740)->  680ms->MT_LOGOSTART(10757)->     27440ms->MT_LOGOSTOP(     11443) Comedy Central
// MT_LOGOSTOP(    3)-> 23880ms->MT_NOBLACKSTOP(  600)->1080ms->MT_NOBLACKSTART(  627)-> 1800ms->MT_LOGOSTART(  672)->     28840ms->MT_LOGOSTOP(      1393)
// MT_LOGOSTOP(    1)-> 22920ms->MT_NOBLACKSTOP(  574)-> 160ms->MT_NOBLACKSTART(  578)->    0ms->MT_LOGOSTART(  578)->     34840ms->MT_LOGOSTOP(      1449) RTL_Television
// MT_LOGOSTOP(  534)->  9920ms->MT_NOBLACKSTOP(  782)-> 120ms->MT_NOBLACKSTART(  785)-> 5040ms->MT_LOGOSTART(  911)->     36920ms->MT_LOGOSTOP(      1834)
// MT_LOGOSTOP( 1694)-> 39400ms->MT_NOBLACKSTOP( 2679)-> 200ms->MT_NOBLACKSTART( 2684)->  640ms->MT_LOGOSTART( 2700)->    202440ms->MT_LOGOSTOP(      7761) -> SAT_1
                    if (    (diffLogoStopBlackStart  >=  8240) &&
                            (diffBlackStartBlackStop >=   120) && (diffBlackStopLogoStart <= 5840) &&
                            (diffLogoStartLogoStop   >  202440)) {  // long broadcast part must follow
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen short before logo start is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen short before logo start is invalid");
                }
            }
        }

        // check sequence MT_NOBLACKSTOP -> MT_LOGOSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART (mark)
        // black screen around logo stop before logo start
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
        // black screen after logo start
        cMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
        if (stopBefore) {
            cMark *blackStart = blackMarks.GetNext(mark->position, MT_NOBLACKSTOP);
            if (blackStart) {
                blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopLogoStart   = 1000 * (mark->position       - stopBefore->position) / decoder->GetVideoFrameRate();
                    int diffLogoStartBlackStart = 1000 * (blackStart->position - mark->position)       / decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%4d) -> %4dms -> MT_LOGOSTART (%4d) -> %3dms -> MT_NOBLACKSTOP (%4d) -> %3dms -> MT_NOBLACKSTART (%4d) -> %s", stopBefore->position, diffLogoStopLogoStart, mark->position, diffLogoStartBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, macontext.Info.ChannelName);
// valid example
// MT_LOGOSTOP (3996) -> 2040ms -> MT_LOGOSTART (4047) -> 680ms -> MT_NOBLACKSTOP (4064) ->  200ms -> MT_NOBLACKSTART (4069)
//
// invalid example
// MT_LOGOSTOP (8702) ->  360ms -> MT_LOGOSTART (8711) ->  80ms -> MT_NOBLACKSTOP (8713) -> 1800ms -> MT_NOBLACKSTART (8758) -> Comedy Central, black screen after logo interuption
// MT_LOGOSTOP (8702) ->  360ms -> MT_LOGOSTART (8711) ->  80ms -> MT_NOBLACKSTOP (8713) -> 1760ms -> MT_NOBLACKSTART (8757) -> Comedy Central, black screen after logo interuption
                    if ((diffLogoStopLogoStart > 360) && (diffLogoStopLogoStart <= 2040) &&
                            (diffLogoStartBlackStart <= 680) &&
                            (diffBlackStartBlackStop >= 200) && (diffBlackStartBlackStop < 1760)) {
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
// invalid example
// MT_LOGOSTOP (11912)->     0ms -> MT_NOBLACKSTOP (11912) ->  480ms -> MT_LOGOSTART (11924) ->   40ms -> MT_NOBLACKSTART (11925) -> Comedy Central: black screen after info logo
// MT_LOGOSTOP ( 4540)->  7240ms -> MT_NOBLACKSTOP ( 4721) ->    0ms -> MT_LOGOSTART ( 4721) ->   80ms -> MT_NOBLACKSTART ( 4723) -> Comedy Central: last part of broadcast before
//
// valid example
// MT_LOGOSTOP (16022)-> 79080ms -> MT_NOBLACKSTOP (19976) ->   40ms -> MT_LOGOSTART (19978) ->  440ms -> MT_NOBLACKSTART (20000)
// MT_LOGOSTOP ( 7582)->   720ms -> MT_NOBLACKSTOP ( 7600) -> 2800ms -> MT_LOGOSTART ( 7670) -> 1880ms -> MT_NOBLACKSTART ( 7717) long black opening credits, fade in logo  (TELE 5)
// MT_LOGOSTOP ( 8017)-> 21000ms -> MT_NOBLACKSTOP ( 8542) ->    0ms -> MT_LOGOSTART ( 8542) -> 2920ms -> MT_NOBLACKSTART ( 8615) -> Comedy Central
                    if ((diffLogoStopBlackStart      >= 720) && (diffLogoStopBlackStart  <= 79080) &&
                            (diffBlackStartLogoStart >=   0) && (diffBlackStartLogoStart <= 2800) &&
                            (diffLogoStartBlackStop  >= 440) && (diffLogoStartBlackStop  <= 2920)) {
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
            cMark *blackStart = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP);     // black screen can start at the same position as logo stop
            if (blackStart) {
                cMark *blackStop = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTART); // black screen can start at the same position as logo stop
                if (blackStop) {
                    int diffBlackStartLogoStop = 1000 * (mark->position       - blackStart->position) / decoder->GetVideoFrameRate();
                    int diffLogoStopBlackStop  = 1000 * (blackStop->position  - mark->position)       / decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart = 1000 * (startAfter->position - blackStop->position)  / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%6d) -> %4dms -> MT_LOGOSTOP (%6d) -> %4dms -> MT_NOBLACKSTART (%6d) -> %5dms -> MT_LOGOSTART (%6d) -> %s", blackStart->position, diffBlackStartLogoStop, mark->position, diffLogoStopBlackStop, blackStop->position, diffBlackStopLogoStart, startAfter->position, macontext.Info.ChannelName);
// valid sequence
// MT_NOBLACKSTOP ( 90520) ->  360ms -> MT_LOGOSTOP ( 90529) ->  200ms -> MT_NOBLACKSTART ( 90534) ->  3960ms -> MT_LOGOSTART ( 90633)
// MT_NOBLACKSTOP ( 47364) -> 1320ms -> MT_LOGOSTOP ( 47397) ->  280ms -> MT_NOBLACKSTART ( 47404) -> 18080ms -> MT_LOGOSTART ( 47856)
// MT_NOBLACKSTOP ( 84098) -> 1400ms -> MT_LOGOSTOP ( 84133) ->   40ms -> MT_NOBLACKSTART ( 84134) ->  1560ms -> MT_LOGOSTART ( 84173) -> RTL2
// MT_NOBLACKSTOP ( 42629) ->  760ms -> MT_LOGOSTOP ( 42648) ->   40ms -> MT_NOBLACKSTART ( 42649) ->   840ms -> MT_LOGOSTART ( 42670) -> SIXX
// MT_NOBLACKSTOP ( 44025) ->  840ms -> MT_LOGOSTOP ( 44046) ->  360ms -> MT_NOBLACKSTART ( 44055) ->   520ms -> MT_LOGOSTART ( 44068) -> SIXX
// MT_NOBLACKSTOP (260383) ->  400ms -> MT_LOGOSTOP (260403) ->   80ms -> MT_NOBLACKSTART (260407) -> 24320ms -> MT_LOGOSTART (261623) -> zdf neo HD
// MT_NOBLACKSTOP ( 42686) ->  880ms -> MT_LOGOSTOP ( 42708) ->    0ms -> MT_NOBLACKSTART ( 42708) ->   880ms -> MT_LOGOSTART ( 42730) -> SIXX
// MT_NOBLACKSTOP ( 50967) ->    0ms -> MT_LOGOSTOP ( 50967) ->   80ms -> MT_NOBLACKSTART ( 50969) ->   760ms -> MT_LOGOSTART ( 50988) -> sixx
                    if (    (diffBlackStartLogoStop >=   0) && (diffBlackStartLogoStop <=  1400) &&
                            (diffLogoStopBlackStop  >=   0) && (diffLogoStopBlackStop  <=   360) &&
                            (diffBlackStopLogoStart >= 520) && (diffBlackStopLogoStart <= 24320)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence around end mark is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence around end mark is invalid", mark->position);
                }
            }
        }

        // check sequence MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTOP
        // blackscreen short before logo stop mark
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

        // check sequence: MT_LOGOSTART -> MT_LOGOSTOP (mark) -> MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_START
        // black screen between logo end mark and start of next broadcast, near (depends on fade out logo) by logo end mark
        cMark *logoStartBefore = marks.GetPrev(mark->position, MT_LOGOSTART);
        cMark *blackStart      = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
        if (logoStartBefore && blackStart) {
            blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
            if (blackStop) {
                cMark *nextStart = marks.GetNext(mark->position, MT_START, 0x0F);  // next broadcast can have border start mark and no logo start mark
                if (nextStart) {
                    int diffLogoStartLogoStop   = 1000 * (mark->position       - logoStartBefore->position) /  decoder->GetVideoFrameRate();
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - mark->position)            /  decoder->GetVideoFrameRate();
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position)      /  decoder->GetVideoFrameRate();
                    int diffBlackStopLogoStart  = 1000 * (nextStart->position  - blackStop->position)       /  decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTART (%5d) -> %4dms -> MT_LOGOSTOP (%5d) -> %5dms -> MT_NOBLACKSTOP (%5d) -> %5dms ->  MT_NOBLACKSTART (%5d) -> %6dms -> MT_START (%5d) -> %s", logoStartBefore->position, diffLogoStartLogoStop, mark->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, nextStart->position, macontext.Info.ChannelName);
// channel with fade out logo
// valid sequence:
//                                     MT_LOGOSTOP (72210) ->  1400ms -> MT_NOBLACKSTOP (72245) ->   200ms ->  MT_NOBLACKSTART (72250) -> 104320ms -> MT_START (74858) -> TLC
//                                     MT_LOGOSTOP (86133) ->  1320ms -> MT_NOBLACKSTOP (86166) ->  2120ms ->  MT_NOBLACKSTART (86219) ->  16000ms -> MT_START (86619) -> Disney Channel
//                                     MT_LOGOSTOP (72310) ->  4240ms -> MT_NOBLACKSTOP (72416) ->   440ms ->  MT_NOBLACKSTART (72427) ->    800ms -> MT_START (72447) -> Disney Channel
//                                     MT_LOGOSTOP (47508) ->   840ms -> MT_NOBLACKSTOP (47529) ->   120ms ->  MT_NOBLACKSTART (47532) ->  13040ms -> MT_START (47858) -> Comedy Central
//                                     MT_LOGOSTOP (43346) ->  6560ms -> MT_NOBLACKSTOP (43510) ->   120ms ->  MT_NOBLACKSTART (43513) ->  20040ms -> MT_START (44014) -> Comedy Central
//
// invalid sequence:
// MT_LOGOSTART (56190) ->    360ms -> MT_LOGOSTOP (56199) ->   520ms -> MT_NOBLACKSTOP (56212) ->   240ms ->  MT_NOBLACKSTART (56218) ->  16880ms -> MT_START (56640) -> Comedy_Central
                    if ((criteria->LogoFadeInOut() & FADE_OUT) &&
                            (diffLogoStartLogoStop > 360) &&
                            (diffLogoStopBlackStart <= 6560) && (diffBlackStartBlackStop >=    120) &&
                            (diffBlackStopLogoStart >=  800) && (diffBlackStopLogoStart  <= 104320)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence between logo end mark and start of next broadcast is valid (fade out logo)", mark->position);
                        return true;
                    }
// channel without fade out logo
// valid sequence:
//                                     MT_LOGOSTOP (81055) ->     0ms -> MT_NOBLACKSTOP (81055) ->    40ms ->  MT_NOBLACKSTART (81056) ->   1680ms -> MT_START (81098) -> RTL2
//                                     MT_LOGOSTOP (84786) ->   120ms -> MT_NOBLACKSTOP (84789) ->   240ms ->  MT_NOBLACKSTART (84795) ->   1800ms -> MT_START (84840) -> Pro7 MAXX
//                                     MT_LOGOSTOP (82130) ->    80ms -> MT_NOBLACKSTOP (82132) ->   240ms ->  MT_NOBLACKSTART (82138) ->   1840ms -> MT_START (82184) -> Pro7 MAXX
//                                     MT_LOGOSTOP (78161) ->    80ms -> MT_NOBLACKSTOP (78163) ->   240ms ->  MT_NOBLACKSTART (78169) ->   2800ms -> MT_START (78239) -> Pro7 MAXX
//                                     MT_LOGOSTOP (46818) ->   840ms -> MT_NOBLACKSTOP (46839) ->   160ms ->  MT_NOBLACKSTART (46843) ->   2680ms -> MT_START (46910) -> Comedy Central
//                                     MT_LOGOSTOP (51734) ->    40ms -> MT_NOBLACKSTOP (51735) ->   320ms ->  MT_NOBLACKSTART (51743) ->    520ms -> MT_START (51756) -> sixx
// MT_LOGOSTART (36696) -> 710920ms -> MT_LOGOSTOP (54469) ->    40ms -> MT_NOBLACKSTOP (54470) ->   120ms ->  MT_NOBLACKSTART (54473) ->    440ms -> MT_START (54484) -> ProSieben
//
// invalid sequence:
//                                     MT_LOGOSTOP (81485) ->  4040ms -> MT_NOBLACKSTOP (81586) ->   160ms ->  MT_NOBLACKSTART (81590) ->  95920ms -> MT_LOGOSTART (83988) -> RTLZWEI
                    if (!(criteria->LogoFadeInOut() & FADE_OUT) &&
                            (diffLogoStopBlackStart <= 840) && (diffBlackStartBlackStop >=   40) &&
                            (diffBlackStopLogoStart >= 440) && (diffBlackStopLogoStart  <= 2800)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence between logo end mark and start of next broadcast is valid (no fade out logo)", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence between logo end mark and start of next broadcast is invalid", mark->position);
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
    if (!criteria->IsInfoLogoChannel()) return false;
    // check logo start mark
    if (mark->type == MT_LOGOSTART) {
        // MT_LOGOSTART  (mark, info logo detected as logo) -> MT_LOGOSTOP (change from info logo to logo) -> MT_LOGOSTART (logo) -> MT_LOGOSTOP (long broadcast to end of first part)
        cMark *stop1After = marks.GetNext(mark->position, MT_LOGOSTOP);
        if (!stop1After) return false;
        cMark *start2After = marks.GetNext(stop1After->position, MT_LOGOSTART);
        if (!start2After) return false;
        const cMark *stop2After = marks.GetNext(start2After->position, MT_LOGOSTOP);
        int endPart1Pos = stopA;
        if (stop2After) endPart1Pos = stop2After->position;
        int diffMarkStop1After          = 1000 * (stop1After->position  - mark->position)        / decoder->GetVideoFrameRate();
        int diffStop1AfterStart2After   = 1000 * (start2After->position - stop1After->position)  / decoder->GetVideoFrameRate();
        int diffStStart2AfterStop2After = 1000 * (endPart1Pos            - start2After->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTART (%5d) -> %5dms -> MT_LOGOSTOP (%5d) -> %4dms -> MT_LOGOSTART (%5d) -> %7ds -> MT_LOGOSTOP (%6d) -> %s", mark->position, diffMarkStop1After, stop1After->position, diffStop1AfterStart2After, start2After->position, diffStStart2AfterStop2After, endPart1Pos, macontext.Info.ChannelName);
        // valid logo start mark example
        // MT_LOGOSTART ( 8374) ->  6240ms -> MT_LOGOSTOP ( 8530) ->  840ms -> MT_LOGOSTART ( 8551) -> 1992840s -> MT_LOGOSTOP ( 58372) -> kabel_eins
        // MT_LOGOSTART ( 8610) ->  6320ms -> MT_LOGOSTOP ( 8768) ->  760ms -> MT_LOGOSTART ( 8787) -> 1149640s -> MT_LOGOSTOP ( 37528) -> kabel_eins
        // MT_LOGOSTART ( 8441) -> 33640ms -> MT_LOGOSTOP ( 9282) ->  680ms -> MT_LOGOSTART ( 9299) -> 7709040s -> MT_LOGOSTOP (202025) -> kabel_eins
        // MT_LOGOSTART ( 8348) ->  6040ms -> MT_LOGOSTOP ( 8499) -> 1040ms -> MT_LOGOSTART ( 8525) ->   26280s -> MT_LOGOSTOP (  9182) -> kabel_eins
        // MT_LOGOSTART ( 8973) ->  6040ms -> MT_LOGOSTOP ( 9124) -> 1080ms -> MT_LOGOSTART ( 9151) ->   26240s -> MT_LOGOSTOP (  9807) -> kabel_eins
        // MT_LOGOSTART ( 9043) ->  6000ms -> MT_LOGOSTOP ( 9193) -> 1080ms -> MT_LOGOSTART ( 9220) ->   26200s -> MT_LOGOSTOP (  9875) -> kabel_eins
        //
        // invald logo start mark example
        // MT_LOGOSTART ( 3459) ->  5880ms -> MT_LOGOSTOP ( 3606) -> 1160ms -> MT_LOGOSTART ( 3635) ->   20800s -> MT_LOGOSTOP ( 4155) -> kabel eins, end sequence of broadcast before
        // MT_LOGOSTART ( 3744) ->  6040ms -> MT_LOGOSTOP ( 3895) -> 1160ms -> MT_LOGOSTART ( 3924) ->   20960s -> MT_LOGOSTOP ( 4448) -> kabel eins, end sequence of broadcast before
        // MT_LOGOSTART ( 3704) ->  8360ms -> MT_LOGOSTOP ( 3913) ->  760ms -> MT_LOGOSTART ( 3932) ->   18960s -> MT_LOGOSTOP ( 4406) -> kabel_eins, end sequence of broadcast before
        if (    (diffMarkStop1After          >=  6000) && (diffMarkStop1After        <= 33640) &&
                (diffStop1AfterStart2After   >=   760) && (diffStop1AfterStart2After <=  1080) &&
                (diffStStart2AfterStop2After >  20960)) {
            dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): opening info logo sequence is valid");
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
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTOP (%6d) -> %4dms -> MT_LOGOSTART (%6d) -> %4dms -> MT_LOGOSTOP (%6d) -> %4dms -> MT_LOGOSTART (%6d) -> %5dms -> MT_LOGOSTOP (%6d) -> %s", stop2Before->position, diffStop2Start2, start2Before->position, diffStart2Stop1, stop1Before->position, diffStop1Start1, start1Before->position, diffStart1Mark, mark->position, macontext.Info.ChannelName);
// valid examples
// MT_LOGOSTOP (185315) -> 1080ms -> MT_LOGOSTART (185342) -> 8160ms -> MT_LOGOSTOP (185546) ->  840ms -> MT_LOGOSTART (185567) -> 18880ms -> MT_LOGOSTOP (186039)
// MT_LOGOSTOP (128417) -> 1120ms -> MT_LOGOSTART (128445) -> 7840ms -> MT_LOGOSTOP (128641) -> 1160ms -> MT_LOGOSTART (128670) ->  7840ms -> MT_LOGOSTOP (128866)
// MT_LOGOSTOP (170880) ->  720ms -> MT_LOGOSTART (170898) -> 8360ms -> MT_LOGOSTOP (171107) ->  840ms -> MT_LOGOSTART (171128) ->  8080ms -> MT_LOGOSTOP (171330)
// MT_LOGOSTOP (68131) ->   920ms -> MT_LOGOSTART (68154) ->  6400ms -> MT_LOGOSTOP ( 68314) ->  720ms -> MT_LOGOSTART ( 68332) -> 10000ms -> MT_LOGOSTOP ( 68582)
// MT_LOGOSTOP (162049) -> 1040ms -> MT_LOGOSTART (162075) -> 8880ms -> MT_LOGOSTOP (162297) ->  840ms -> MT_LOGOSTART (162318) ->  6360ms -> MT_LOGOSTOP (162477)
// MT_LOGOSTOP (201203) ->  880ms -> MT_LOGOSTART (201225) -> 6360ms -> MT_LOGOSTOP (201384) ->  840ms -> MT_LOGOSTART (201405) -> 14360ms -> MT_LOGOSTOP (201764) -> kabel_eins
// MT_LOGOSTOP (201503) ->  600ms -> MT_LOGOSTART (201518) -> 6360ms -> MT_LOGOSTOP (201677) ->  840ms -> MT_LOGOSTART (201698) -> 14360ms -> MT_LOGOSTOP (202057) -> kabel_eins
//
// invalid example
// MT_LOGOSTART ( 3833) -> 480ms -> MT_LOGOSTOP ( 3845) -> 480ms -> MT_LOGOSTART ( 3857)
        if (    (diffStop2Start2 >=  600) && (diffStop2Start2 <=  1120) &&  // change from logo to closing logo
                (diffStart2Stop1 >= 6360) && (diffStart2Stop1 <=  8880) &&  // closing logo deteted as logo
                (diffStop1Start1 >=  720) && (diffStop1Start1 <=  1160) &&  // change from closing logo to logo
                (diffStart1Mark  >= 6360) && (diffStart1Mark  <= 18880)) { // end part between closing logo and broadcast end
            dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): found closing info logo sequence");
            // cleanup invalid logo marks
            marks.Del(stop2Before->position);
            marks.Del(start2Before->position);
            marks.Del(stop1Before->position);
            marks.Del(start1Before->position);
            return true;
        }
        else dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): logo stop mark (%d): closing logo info sequence is invalid", mark->position);
    }
    return false;
}


cMark *cMarkAdStandalone::Check_ASPECTSTOP() {
    // if we have 16:9 broadcast, aspect ratio stop mark without aspect start before is start of next broadcast
    if ((criteria->GetMarkTypeState(MT_HBORDERCHANGE) < CRITERIA_USED) && (macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
        cMark *aspectStop = marks.GetNext(0, MT_ASPECTSTOP);
        if (aspectStop) {
            const cMark *aspectStart = marks.GetPrev(aspectStop->position, MT_ASPECTSTOP);
            if (!aspectStart) {
                // do not use this aspect stop mark, broadcast stop can be before aspect stop if there is an ad between
                dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): we have 16:9 bradcast and MT_ASPECTSTOP at frame (%d) without MT_ASPECTSTART before, this is start of next broadcast", aspectStop->position);
                marks.DelAfterFromToEnd(aspectStop->position);  // keep aspect stop mark, can be a valid end mark if there is no ad
                return nullptr;
            }
        }
    }
    cMark *end = marks.GetAround(400 * (decoder->GetVideoFrameRate()), stopA, MT_ASPECTSTOP);      // changed from 360 to 400
    if (end) {
        dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): MT_ASPECTSTOP found at frame (%d)", end->position);
        if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) {
            dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): delete all weak marks");
            marks.DelWeakFromTo(marks.GetFirst()->position + 1, end->position, MT_ASPECTCHANGE); // delete all weak marks, except start mark
        }
        else { // 16:9 broadcast with 4:3 broadcast after, maybe ad between and we have a better hborder or logo stop mark
            cMark *stopBefore  = marks.GetPrev(end->position, MT_HBORDERSTOP);         // try hborder
            if (!stopBefore) {
                stopBefore  = marks.GetPrev(end->position, MT_LOGOSTOP);  // try logo stop
                // check position of logo start mark before aspect ratio mark, if it is after logo stop mark, logo stop mark end can be end of preview in advertising
                if (stopBefore) {
                    int diffAspectStop = (end->position - stopBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): logo stop mark (%d), %ds before aspect ratio stop mark", stopBefore->position, diffAspectStop);
                    if (diffAspectStop > 234) {  // check only for early logo stop marks, do not increase, there can be a late advertising and aspect stop on same frame as logo stop
                        // changed from 153 to 234
                        cMark *startLogoBefore = marks.GetPrev(end->position, MT_LOGOSTART);
                        if (startLogoBefore && (startLogoBefore->position > stopBefore->position)) {
                            dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): logo start mark (%d) between logo stop mark (%d) and aspect ratio mark (%d), this logo stop mark is end of advertising", startLogoBefore->position, stopBefore->position, end->position);
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
                dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): found %s stop mark (%d) %ds before aspect ratio end mark (%d), %ds before assumed stop", markType, stopBefore->position, diffAspectStop, end->position, diffStopA);
                FREE(strlen(markType)+1, "text");
                free(markType);
                if ((diffStopA <= 867) && (diffAspectStop <= 66)) { // changed from 760 to 867, for broadcast length from info file too long
                    // changed from 39 to 40 to 66, for longer ad found between broadcasts
                    dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): there is an advertising before aspect ratio change, use stop mark (%d) before as end mark", stopBefore->position);
                    end = stopBefore;
                    // cleanup possible info logo or logo detection failure short before end mark
                    if (end->type == MT_LOGOSTOP) marks.DelFromTo(end->position - (60 * decoder->GetVideoFrameRate()), end->position - 1, MT_LOGOCHANGE, 0xF0);
                }
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_ASPECTSTOP(): no MT_ASPECTSTOP mark found");
    return end;
}


void cMarkAdStandalone::CheckStop() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): start check stop (%d)", decoder->GetPacketNumber());

    char *indexToHMSF = marks.IndexToHMSF(stopA, AV_NOPTS_VALUE, false);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("assumed stop position (%d) at %s", stopA, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    DebugMarks();     //  only for debugging
    // check start mark, re-calculate assumed stop if later start mark was selected
    if (CheckStartMark()) {
        stopA = marks.GetFirst()->position + decoder->GetVideoFrameRate() * length;
        indexToHMSF = marks.IndexToHMSF(stopA, AV_NOPTS_VALUE, false);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            dsyslog("new assumed stop position after final start mark selection (%d) at %s", stopA, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
            DebugMarks();     //  only for debugging
        }
    }

    // cleanup invalid marks
    //
    // cleanup near logo stop and start marks around aspect ratio marks, they are from a fading in/out logo
    cMark *aspectMark = marks.GetNext(0, MT_ASPECTCHANGE, 0xF0);
    while (aspectMark) {
        cMark *logoMark = marks.GetAround(5 * decoder->GetVideoFrameRate(), aspectMark->position, MT_LOGOCHANGE, 0xF0);
        while (logoMark) {
            dsyslog("cMarkAdStandalone::CheckStop(): logo mark (%d) near aspect ratio mark (%d) found, this is a fading in/out logo, delete", logoMark->position, aspectMark->position);
            marks.Del(logoMark->position);
            logoMark = marks.GetAround(5 * decoder->GetVideoFrameRate(), aspectMark->position, MT_LOGOCHANGE, 0xF0);
        }
        aspectMark = marks.GetNext(aspectMark->position, MT_ASPECTCHANGE, 0xF0);
    }

    // remove logo change marks
    RemoveLogoChangeMarks(false);

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): marks after first cleanup:");
    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): start end mark selection");

// try MT_CHANNELSTOP
    cMark *end = nullptr;
    if (criteria->GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_UNKNOWN) end = Check_CHANNELSTOP();

// try MT_ASPECTSTOP
    if (!end) end = Check_ASPECTSTOP();

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

    // cleanup all marks after vborder start from next broadcast
    if (!end && (criteria->GetMarkTypeState(MT_VBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
        cMark *vBorderStart = marks.GetNext(stopA - (MAX_ASSUMED * decoder->GetVideoFrameRate()), MT_VBORDERSTART); // changed from 60 to MAX_ASSUMED
        if (vBorderStart) {
            // use logo stop mark short after vborder as end mark
            cMark *logoStop = marks.GetNext(vBorderStart->position, MT_LOGOSTOP);
            if (logoStop) {
                int diff = (logoStop->position - vBorderStart->position) / decoder->GetVideoFrameRate();
                if (diff <= 2) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) %ds after vborder start mark (%d), use it as end mark", logoStop->position, diff, vBorderStart->position);
                    marks.Del(vBorderStart->position);
                    end = logoStop;
                }
            }
            if (!end) {
                dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after vborder start (%d) from next broadcast", vBorderStart->position);
                marks.DelTill(vBorderStart->position, false);
            }
        }
    }

// try MT_LOGOSTOP
    if (!end) end = Check_LOGOSTOP();   // logo detection can be disabled in end part after aspect ratio change
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

// no end mark found, try if we can use a start mark of next bradcast as end mark, but not for channel with good VPS and length is from VPS event
    if (!macontext.Info.lengthFromVPS || !criteria->GoodVPS()) {
        // no valid stop mark found, use MT_ASPECTSTOP (broadcast must be 16:9, start of next broadcast with 4:3)
        if (!end && (criteria->GetMarkTypeState(MT_ASPECTCHANGE) != CRITERIA_USED)) { // not possible is we use aspect marks in this broadcast
            cMark *aspectStop = marks.GetNext(stopA, MT_ASPECTSTOP);
            if (aspectStop) {
                dsyslog("cMarkAdStandalone::CheckStop(): use aspect stop mark (%d) from start of next 4:3 broadcast as end mark", aspectStop->position);
                end = aspectStop;
            }
        }
        // no valid stop mark found, try if there is a MT_CHANNELSTART from next broadcast
        if (!end && (criteria->GetMarkTypeState(MT_CHANNELCHANGE) != CRITERIA_USED)) { // not possible is we use channel mark in this broadcast
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
                cMark *hBorderStop = marks.GetNext(hBorderStart->position, MT_HBORDERSTOP);
                if (hBorderStop) { // check length of hborder
                    int diff = (hBorderStop->position - hBorderStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::CheckStop(): MT_HBORDERSTART (%ds) -> %ds -> MT_HBORDERSTOP (%d)", hBorderStart->position, diff, hBorderStop->position);
                    // example of hborder from closing credits
                    //  MT_HBORDERSTART (284293s) -> 81s -> MT_HBORDERSTOP (288356)
                    if (diff <= 81) {
                        dsyslog("cMarkAdStandalone::CheckStop(): hborder too short, maybe from closing credists, delete marks");
                        marks.Del(hBorderStart);
                        marks.Del(hBorderStop);
                        hBorderStart = nullptr;
                    }
                }
                if (hBorderStart) {  // can be deleted above
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
                            if (end) end = marks.Move(end, end->position, end->pts, MT_TYPECHANGESTOP);  // one frame before hborder start is end mark
                        }
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStop(): use stop mark (%d) before hborder start mark (%d) ", prevMark->position, hBorderStart->position);
                        end = prevMark;
                    }
                }
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
            int lastPacketNumber = index->GetLastPacket()->packetNumber;
            int stopPos          = stopA;
            if (stopPos > lastPacketNumber) {
                dsyslog("cMarkAdStandalone::CheckStop(): assumed stop (%d) after recording end (%d), use recording end", stopA, lastPacketNumber);
                stopPos = lastPacketNumber;
            }
            sMarkAdMark mark  = {};
            mark.position = index->GetKeyPacketNumberBefore(stopPos, &mark.framePTS);  // adjust to i-frame if no full decoding
            mark.type     = MT_ASSUMEDSTOP;
            AddMark(&mark);
            end = marks.Get(mark.position);
        }
    }


    // delete all marks after end mark
    if (end) { // be save, if something went wrong end = nullptr
        dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after final stop mark at (%d)", end->position);
        const cMark *startBefore = marks.GetPrev(end->position, MT_START, 0x0F);
        if (!startBefore) {
            esyslog("cMarkAdStandalone::CheckStop(): invalid marks, no start mark before end mark");
            sMarkAdMark mark = {};
            mark.position    = 0;
            mark.type        = MT_RECORDINGSTART;
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
    // check if channel uses closing credits without logo
    int closingCreditsState = criteria->GetClosingCreditsState(stopMark->position);
    if (closingCreditsState < CRITERIA_UNKNOWN) {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): no check necessary, closing credits state: %d", closingCreditsState);
        return false;
    }
    dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): detect closing credits in frame without logo after position (%d)", stopMark->position);
    // check if we already know if there is a closing credits
    sMarkPos endClosingCredits = {-1};
    enum eEvaluateStatus isClosingStatus = STATUS_UNKNOWN;
    if (evaluateLogoStopStartPair) isClosingStatus = evaluateLogoStopStartPair->GetIsClosingCredits(stopMark->position, stopMark->position + (MAX_CLOSING_CREDITS_SEARCH * decoder->GetVideoFrameRate()), &endClosingCredits);
    switch (isClosingStatus) {
    case STATUS_DISABLED:
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): IsClosingCredits state DISABLED");
        break;
    case STATUS_NO:
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): IsClosingCredits state NO");
        break;
    case STATUS_ERROR:
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): IsClosingCredits state ERROR"); // pair not found, try detection now
    case STATUS_UNKNOWN:  // we have to detect now
    {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): IsClosingCredits state UNKNOWN or ERROR, detect now");
        if (!detectLogoStopStart) {  // init in RemoveLogoChangeMarks(), but maybe not used
            detectLogoStopStart = new cDetectLogoStopStart(decoder, index, criteria, evaluateLogoStopStartPair, video->GetLogoCorner());
            ALLOC(sizeof(*detectLogoStopStart), "detectLogoStopStart");
        }
        // check current read position of decoder
        if (stopMark->position < decoder->GetPacketNumber()) decoder->Restart();
        int endPos = stopMark->position + (MAX_CLOSING_CREDITS_SEARCH * decoder->GetVideoFrameRate());  // try till MAX_CLOSING_CREDITS_SEARCH after stopMarkPosition
        endClosingCredits = {-1};
        if (detectLogoStopStart->Detect(stopMark->position, endPos)) detectLogoStopStart->ClosingCredit(stopMark->position, endPos, &endClosingCredits);
    }
    break;
    case STATUS_YES:
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): use known closing credits from (%d) to (%d)", stopMark->position, endClosingCredits.position);
        break;
    default:
        esyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): IsClosingCredits state invalid %d", isClosingStatus);
        break;
    }

    // move mark if closing credits found
    if (endClosingCredits.position > stopMark->position) {
        dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): closing credits found, move stop mark to position (%d) PTS %" PRId64, endClosingCredits.position, endClosingCredits.pts);
        marks.Move(stopMark, endClosingCredits.position, endClosingCredits.pts, MT_CLOSINGCREDITSSTOP);
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
    if (abortNow) return;
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): start detect and remove logo stop/start marks from special logo");

    // only if there are at last one corect logo stop/start and one logo/stop/start from special logo
    if (marks.Count(MT_LOGOCHANGE, 0xF0) < 4) {
        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): to less logo marks");
        return;
    }
    // check if this channel has special logos, for performance reason only known and tested channels
    if (!criteria->IsInfoLogoChannel() && !criteria->IsLogoChangeChannel() && !criteria->IsClosingCreditsChannel() && !criteria->IsAdInFrameWithLogoChannel()) {
        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): channel not in list for special logo");
        return;
    }

    // do not messup decoder read position if called by CheckStart(), use new instance for detection
    // use local variables with same name as global
    cDecoder *decoder_local = decoder;
    if (checkStart) {
        decoder_local = new cDecoder(macontext.Config->recDir, macontext.Config->threads, macontext.Config->fullDecode, macontext.Config->hwaccel, macontext.Config->forceHW,  macontext.Config->forceInterlaced, nullptr);  // no index
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
    else evaluateLogoStopStartPair->SetDecoder(decoder_local);

    evaluateLogoStopStartPair->CheckLogoStopStartPairs(&marks, &blackMarks, startA, packetCheckStart, packetEndPart, stopA);

    char *indexToHMSFStop      = nullptr;
    char *indexToHMSFStart     = nullptr;
    sLogoStopStartPair logoStopStartPair;

    // loop through all logo stop/start pairs
    while (evaluateLogoStopStartPair->GetNextPair(&logoStopStartPair)) {
        if (abortNow) return;
        if (logoStopStartPair.stopPosition <= IGNORE_AT_START) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): ignore initial mark (%d)", logoStopStartPair.stopPosition);
            continue;
        }
        if (!marks.Get(logoStopStartPair.startPosition) || !marks.Get(logoStopStartPair.stopPosition)) continue;  // at least one of the mark from pair was deleted, nothing to do
        if ((logoStopStartPair.isLogoChange <= STATUS_NO) && (logoStopStartPair.isInfoLogo <= STATUS_NO) && (logoStopStartPair.isClosingCredits <= STATUS_NO)) continue;

        if (decoder_local->GetPacketNumber() >= logoStopStartPair.stopPosition) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): packet (%d): overlapping pairs from info logo merge, skip pair logo stop (%d) start (%d)", decoder_local->GetPacketNumber(), logoStopStartPair.stopPosition, logoStopStartPair.startPosition);
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
        indexToHMSFStop = marks.IndexToHMSF(logoStopStartPair.stopPosition, AV_NOPTS_VALUE, false);
        if (indexToHMSFStop) {
            ALLOC(strlen(indexToHMSFStop)+1, "indexToHMSF");
        }
        indexToHMSFStart = marks.IndexToHMSF(logoStopStartPair.startPosition, AV_NOPTS_VALUE, false);
        if (indexToHMSFStart) {
            ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF");
        }
        if (indexToHMSFStop && indexToHMSFStart) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%d) at %s and logo start (%d) at %s, isInfoLogo %d", logoStopStartPair.stopPosition, indexToHMSFStop, logoStopStartPair.startPosition, indexToHMSFStart, logoStopStartPair.isInfoLogo);
        }
        // only closing credits check have to be done, limit search range
        if ((logoStopStartPair.isInfoLogo <= STATUS_NO) && (logoStopStartPair.isLogoChange <= STATUS_NO)) {
            if ((logoStopStartPair.startPosition - logoStopStartPair.stopPosition) / decoder->GetVideoFrameRate() > MAX_CLOSING_CREDITS_SEARCH) {
                // set new search range
                logoStopStartPair.startPosition = logoStopStartPair.stopPosition + (MAX_CLOSING_CREDITS_SEARCH * decoder->GetVideoFrameRate());
                dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): search range too big for only closing credits search, reduce search range from stop mark (%d) to (%d)", logoStopStartPair.stopPosition,  logoStopStartPair.startPosition);
            }
        }
        // start detection
        if (detectLogoStopStart->Detect(logoStopStartPair.stopPosition, logoStopStartPair.startPosition)) {
            bool doInfoCheck = true;
            // check for closing credits if no other checks will be done, only part of the loop elements in recording end range
            sMarkPos endClosingCredits = {-1};
            if ((logoStopStartPair.isInfoLogo <= STATUS_NO) && (logoStopStartPair.isLogoChange <= STATUS_NO)) detectLogoStopStart->ClosingCredit(logoStopStartPair.stopPosition, logoStopStartPair.startPosition, &endClosingCredits);

            // check for info logo if  we are called by CheckStart and we are in broadcast
            if ((startA > 0) && criteria->IsIntroductionLogoChannel() && (logoStopStartPair.isStartMarkInBroadcast == STATUS_YES)) {
                // do not delete info logo, it can be introduction logo, it looks the same
                // expect we have another start very short before
                cMark *lStartBefore = marks.GetPrev(logoStopStartPair.stopPosition, MT_LOGOSTART);
                if (lStartBefore) {
                    int diffStart = 1000 * (logoStopStartPair.stopPosition - lStartBefore->position) / decoder_local->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): logo start (%d) %dms before stop mark (%d)", lStartBefore->position, diffStart, logoStopStartPair.stopPosition);
                    if (diffStart > 1240) {  // do info logo check if we have a logo start mark short before, some channel send a early info log after broadcast start
                        // changed from 1160 to 1240
                        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): do not check for info logo, we are in start range, it can be introducion logo");
                        doInfoCheck = false;
                    }
                }
            }
            if (doInfoCheck && (logoStopStartPair.isInfoLogo >= STATUS_UNKNOWN) && detectLogoStopStart->IsInfoLogo(logoStopStartPair.stopPosition, logoStopStartPair.startPosition, logoStopStartPair.hasBorderAroundStart)) {
                // found info logo part
                if (indexToHMSFStop && indexToHMSFStart) {
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): info logo found between frame (%i) at %s and (%i) at %s, deleting marks between this positions", logoStopStartPair.stopPosition, indexToHMSFStop, logoStopStartPair.startPosition, indexToHMSFStart);
                }
                evaluateLogoStopStartPair->SetIsInfoLogo(logoStopStartPair.stopPosition, logoStopStartPair.startPosition);
                marks.DelFromTo(logoStopStartPair.stopPosition, logoStopStartPair.startPosition, MT_LOGOCHANGE, 0xF0);  // maybe there a false start/stop inbetween
            }
            // check logo change
            if ((logoStopStartPair.isLogoChange >= STATUS_UNKNOWN) && detectLogoStopStart->IsLogoChange(logoStopStartPair.stopPosition, logoStopStartPair.startPosition)) {
                if (indexToHMSFStop && indexToHMSFStart) {
                    isyslog("logo change between frame (%6d) at %s and (%6d) at %s, deleting marks between this positions", logoStopStartPair.stopPosition, indexToHMSFStop, logoStopStartPair.startPosition, indexToHMSFStart);
                }
                marks.DelFromTo(logoStopStartPair.stopPosition, logoStopStartPair.startPosition, MT_LOGOCHANGE, 0xF0);  // maybe there a false start/stop inbetween
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

    // delete only one time used object from CheckStart() call, keep evaluateLogoStopStartPair for later use
    if (checkStart) {
        FREE(sizeof(*decoder_local), "decoder_local");
        delete decoder_local;
        decoder_local = nullptr;
        FREE(sizeof(*detectLogoStopStart), "detectLogoStopStart");
        delete detectLogoStopStart;
        detectLogoStopStart = nullptr;
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
    // we will be one packet before/after start/stop, correct later with scene change
    cMark *aMark = marks.GetFirst();
    while (aMark) {
        if      (aMark->type == MT_ASPECTSTART) aMark->type = MT_ASPECTSTOP;
        else if (aMark->type == MT_ASPECTSTOP)  aMark->type = MT_ASPECTSTART;
        aMark = aMark->Next();
    }
    video->SetAspectRatioBroadcast(macontext.Info.AspectRatio);
    dsyslog("cMarkAdStandalone::SwapAspectRatio(): new aspect ratio %d:%d, fixed marks are:", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    DebugMarks();     //  only for debugging
}


cMark *cMarkAdStandalone::Check_CHANNELSTART() {
    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): search for channel start mark");

    // cleanup very short channel start/stop pairs, they are from stream error
    cMark *channelStart = marks.GetNext(-1, MT_CHANNELSTART);
    while (channelStart) {
        cMark *channelStop = marks.GetNext(channelStart->position, MT_CHANNELSTOP);
        if (channelStop) {
            int diff = 1000 * (channelStop->position - channelStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): channel start (%d), channel stop (%d), length broadcast %dms", channelStart->position, channelStop->position, diff);
            if (diff < 200) {
                dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): broadcast too short, delete invalid marks");
                cMark *channelStartNext = marks.GetNext(channelStart->position, MT_CHANNELSTART);
                marks.Del(channelStart->position);
                marks.Del(channelStop->position);
                channelStart = channelStartNext;
                continue;
            }
        }
        channelStart = marks.GetNext(channelStart->position, MT_CHANNELSTART);
    }

    // delete very early first mark, if channels send ad with 6 channels, this can be wrong
    channelStart = marks.GetNext(-1, MT_CHANNELSTART);
    if (channelStart) {
        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);  // there is a 6 channel audio in broadcast, may we can use it later
        if (channelStart->position < IGNORE_AT_START) marks.Del(channelStart->position);
    }
    // 6 channel double episode, there is no channel start mark
    if (decoder->GetAC3ChannelCount() >= 5) criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);  // there is a 6 channel audio in broadcast, may we can use it later

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
            const cMark *logoStop = marks.GetNext(channelStart->position, MT_LOGOSTOP);
            if (logoStop) {  // if channel start is from previous recording, we should have a logo stop mark near assumed start
                cMark *logoStart = marks.GetNext(logoStop->position, MT_LOGOSTART);
                if (logoStart) {
                    int diffLogoStart = (logoStart->position - startA) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): found logo start mark (%d) %ds after assumed start", logoStart->position, diffLogoStart);
                    if ((diffLogoStart >= -4) && (diffLogoStart <= 56)) {  // changed from -1 to -4, changed from 17 to 56
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): use logo start mark (%d) as start mark", logoStart->position);
                        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
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
        criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
        return channelStart;
    }
    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): no audio channel start mark found");
    return nullptr;
}



cMark *cMarkAdStandalone::Check_LOGOSTART() {
    if (!index) {
        esyslog("cMarkAdStandalone::Check_LOGOSTART(): index not valid");
        return nullptr;
    }
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): search for logo start mark");
    cMark *begin = nullptr;
    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }
    else evaluateLogoStopStartPair->SetDecoder(decoder);

    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): cleanup invalid logo marks");
    // remove very early logo start marks, this can be delayed logo start detection
    cMark *lStart = marks.GetNext(-1, MT_LOGOSTART);  // get first logo start mark
    while (lStart) {
        int startOffset = index->GetTimeOffsetFromPTS(lStart->pts) / 1000;  // in seconds
        if (startOffset <= 14) {  // changed from 11 to 14
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start (%5d) %ds after recording start too early", lStart->position, startOffset);
            cMark *tmpMark = marks.GetNext(lStart->position, MT_LOGOSTART);  // there can be more than one early logo start
            marks.Del(lStart->position);
            lStart = tmpMark;
            continue;
        }
        else break;
        lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
    }
    // remove very short logo start/stop pairs, this, is a false positive logo detection
    lStart = marks.GetNext(-1, MT_LOGOSTART);  // get first logo start mark
    while (lStart) {
        cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);
        if (lStop) {
            int diff = 1000 * (lStop->position - lStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start (%5d) logo stop  (%5d), distance %5dms", lStart->position, lStop->position, diff);
            if (diff <= 60) {  // in ms
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): false positiv logo detection: distance too short, deleting marks");
                cMark *tmpMark = marks.GetNext(lStart->position, MT_LOGOSTART);  // there can be more than one early logo start
                marks.Del(lStop->position);
                marks.Del(lStart->position);
                lStart = tmpMark;
                continue;
            }
        }
        lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
    }
    // remove very short logo stop/start pairs for channel with logo interuption, these are no valid start marks
    if (criteria->IsLogoInterruptionChannel()) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART():remove very short logo stop/start pairs for channel with logo interuption");
        cMark *lStop = marks.GetNext(-1, MT_LOGOSTOP);  // get first logo stop mark
        while (lStop) {
            lStart = marks.GetNext(lStop->position, MT_LOGOSTART);
            if (lStart) {
                int diff = 1000 * (lStart->position - lStop->position) / decoder->GetVideoFrameRate();
                int startAdiff = (lStop->position - startA) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo stop  (%5d) logo start (%5d), distance %6dms, %ds after assumed start (%d)", lStop->position, lStart->position, diff, startAdiff, startA);
                // valid logo stop/start pair
                // do not delete late logo stop, this can be a valid stop mark of first part
                // logo stop  (18837) logo start (18854), distance    680ms, 452s after assumed start (7525)
                if ((diff <= 1000) && (startAdiff < 452)) {  // changed from 480 to 1000, do not delete late logo stop, this can be a valid stop mark of first part
                    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo interuption channel: distance too short, deleting marks");
                    cMark *tmpMark = marks.GetNext(lStop->position, MT_LOGOSTOP);  // there can be more than one early logo start
                    marks.Del(lStart->position);
                    marks.Del(lStop->position);
                    lStop = tmpMark;
                    continue;
                }
            }
            lStop = marks.GetNext(lStop->position, MT_LOGOSTOP);
        }
    }

// search for logo start mark around assumed start
    int maxAssumed = MAX_ASSUMED;
    if (macontext.Info.startFromVPS && criteria->GoodVPS()) {
        maxAssumed = 30;  // if we use a valid VPS event based start time do only near search, found preview with logo 31s before broadcast, changed from 56 to 30
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): channel with good VPS, max distance from VPS start event %ds", maxAssumed);
    }
    cMark *lStartAssumed = marks.GetAround(startA + (maxAssumed * decoder->GetVideoFrameRate()), startA, MT_LOGOSTART);
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
            if ((diffAssumed < -maxAssumed) || (diffAssumed > maxAssumed)) break;
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
            if (diffAssumed > maxAssumed) break;
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
        if ((diffAssumed < -maxAssumed) || (diffAssumed > maxAssumed)) break;
    }
    lStart = lStartAssumed;
    while (!begin) {  // search from nearest logo start mark to recording start
        lStart = marks.GetPrev(lStart->position, MT_LOGOSTART);
        if (!lStart) break;
        int diffAssumed = (lStart->position - startA) / decoder->GetVideoFrameRate();
        if ((diffAssumed < -maxAssumed) || (diffAssumed > maxAssumed)) break;
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
        if (diffAssumed > maxAssumed) {
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
        if (diffAssumed > maxAssumed) break;
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
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) near to assumed start, %ds before assumed start (%d), max %ds", lStart->position, diffAssumed, startA, maxAssumed);
        if (diffAssumed >= maxAssumed) { // do not accept start mark if it is more than maxAssumed min before assumed start
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds before assumed start too early", lStart->position, diffAssumed);
            cMark *lNext = marks.GetNext(lStart->position, MT_LOGOSTART);  // get next logo start mark
            marks.Del(lStart);
            lStart = lNext;
            continue;
        }
        // check for too late logo start, can be of first ad
        if (diffAssumed < -maxAssumed) {  // do not accept start mark if it is more than maxAssumed min after assumed start
            // if logo start mark is after a long part without logo, it should be valid, maybe too late broadcast start
            cMark *logoStopBefore = marks.GetPrev(lStart->position, MT_LOGOSTOP);
            if (!logoStopBefore) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds after assumed start is valid, first logo start mark", lStart->position, -diffAssumed);
                begin = lStart;  // start with nearest start mark to assumed start
            }
            else {
                int adLength = (lStart->position - logoStopBefore->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds after assumed start, length ad before %d ", lStart->position, -diffAssumed, adLength);
                if (adLength >= 114) { // changed from 137 to 114
                    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) after long ad should be valid", lStart->position);
                    begin = lStart;  // start with nearest start mark to assumed start
                }
            }
            if (!begin) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds after assumed start too late", lStart->position, -diffAssumed);
                break;
            }
        }
        else {
            begin = lStart;  // start with nearest start mark to assumed start
        }

        // check next logo stop/start pair
        cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);  // get next logo stop mark
        if (lStop) {  // there is a next stop mark in the start range
            int distanceStartStop = (lStop->position - lStart->position) / decoder->GetVideoFrameRate();
            if (distanceStartStop < 6) {  // change from 8 to 6 because of very short stop/start from logo change 6s after start mark
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
// invalid hborder/vborder stop marks can left over from border stop as fallback start mark
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): delete invalid hborder and vborder marks from previous broadcast or detection error");
    if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_DISABLED) marks.DelType(MT_HBORDERCHANGE, 0xF0);
    if (criteria->GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_DISABLED) marks.DelType(MT_VBORDERCHANGE, 0xF0);

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
        criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
    }
    if (!criteria->LogoInBorder()) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): disable border detection and delete border marks");  // avoid false detection of border
        marks.DelType(MT_HBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        marks.DelType(MT_VBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        criteria->SetDetectionState(MT_HBORDERCHANGE, false);
        criteria->SetDetectionState(MT_VBORDERCHANGE, false);
    }
    else {  // delete delayed vborder stop from previous broadcast and black opening credits in this boaddcast
        cMark *vborderStop = marks.GetAround(30 * decoder->GetVideoFrameRate(), begin->position, MT_VBORDERSTOP);  // trust late hborder start mark
        if (vborderStop) {
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): delete vborder stop (%d) from previous broadcast", vborderStop->position);  // avoid false detection of border
            marks.Del(vborderStop->position);
        }
    }
    return begin;
}


cMark *cMarkAdStandalone::Check_HBORDERSTART() {
    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): search for hborder start mark");
    cMark *hStart = marks.GetAround(1.3 * MAX_ASSUMED * decoder->GetVideoFrameRate(), startA, MT_HBORDERSTART);  // trust late hborder start mark
    if (hStart) { // we found a hborder start mark
        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start found at (%d)", hStart->position);
        if (hStart->position <= IGNORE_AT_START) {  // hborder start at recording start is from previous recording
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): ignore too early hborder start (%d), try next", hStart->position);
            hStart = marks.GetNext(hStart->position, MT_HBORDERSTART);
            if (hStart) dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): next horizontal border start found at (%d)", hStart->position);
        }
    }
    if (hStart) { // we found a hborder start mark
        cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);
        if (hStop) { // we have a hborder stop mark in start area, check if hborder marks are valid
            // check if hbrder start/stop is end part of previous broadcast
            if (hStop->position < startA) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start (%d) and stop (%d) mark before assumed start (%d), hborder marks are from previous broadcast", hStart->position, hStop->position, startA);
                marks.Del(hStart->position); // keep hborder stop as end of previous broadcast, may we can use it as fallback
                return nullptr;
            }

            int lengthBroadcast   = (hStop->position - hStart->position) / decoder->GetVideoFrameRate();
            int hBorderStopStartA = (hStop->position - startA)           / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start (%d) and stop (%d) mark, length of first broadcast %ds, ends %ds after assumed start (%d)", hStart->position, hStop->position, lengthBroadcast, hBorderStopStartA, startA);
            // very short broadcast without next hborder start is invalid
            const cMark *hStartNext = marks.GetNext(hStop->position, MT_HBORDERSTART);
            if (!hStartNext &&
                    ((lengthBroadcast <= 142) ||  // very short broadcast can be from preview or hborder part in documentation, changed from 94 to 142
                     ((lengthBroadcast <= 350) && (hBorderStopStartA <= 194)))) {  // very early hborder part can be last part of previous broadcast
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): first broadcast too short, no next hborder start, delete hborder marks");
                marks.Del(hStart->position);
                marks.Del(hStop->position);
                return nullptr;
            }
            // check if hborder marks are from long black opening credits
            // false hborder from opening credits or dokus always have logo in border
            cMark *logoStartBefore = marks.GetAround(20 * decoder->GetVideoFrameRate(), hStart->position, MT_LOGOSTART);  // logo start can be short after hborder start (fade in logo)
            if (!logoStartBefore) logoStartBefore = marks.GetPrev(hStart->position, MT_LOGOSTART); // false hborder start/stop in dokus can be far after logo start
            if (logoStartBefore) {
                cMark *logoStopAfter  = marks.GetNext(hStop->position, MT_LOGOSTOP);
                if (logoStopAfter) {
                    int diffLogoStarthBorderStart  = (hStart->position        - logoStartBefore->position) / decoder->GetVideoFrameRate();
                    int diffBorderStarthBorderStop = (hStop->position         - hStart->position)          / decoder->GetVideoFrameRate();
                    int diffhBorderStopLogoStop    = (logoStopAfter->position - hStop->position)           / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): check for false detected hborder from opening credits or documentation");
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): MT_LOGOSTART (%6d) -> %3ds -> MT_HBORDERSTART (%6d) -> %3ds -> MT_HBORDERSTOP (%6d) -> %4ds -> MT_LOGOSTOP (%6d) -> %s", logoStartBefore->position, diffLogoStarthBorderStart, hStart->position, diffBorderStarthBorderStop, hStop->position, diffhBorderStopLogoStop, logoStopAfter->position, macontext.Info.ChannelName);
                    // exampe false detected hborder from opening credits
                    // MT_LOGOSTART ( 15994) ->   3s -> MT_HBORDERSTART ( 16074) ->  82s -> MT_HBORDERSTOP ( 18131) -> 1460s -> MT_LOGOSTOP ( 52588)
                    // MT_LOGOSTART ( 39468) -> -19s -> MT_HBORDERSTART ( 38502) -> 119s -> MT_HBORDERSTOP ( 44474) -> 1593s -> MT_LOGOSTOP (118182)
                    // MT_LOGOSTART (  7421) ->   0s -> MT_HBORDERSTART (  7422) -> 223s -> MT_HBORDERSTOP ( 13016) ->  960s -> MT_LOGOSTOP ( 31422)
                    // MT_LOGOSTART ( 39468) -> -19s -> MT_HBORDERSTART ( 38502) -> 118s -> MT_HBORDERSTOP ( 44428) -> 1474s -> MT_LOGOSTOP (118130) -> TMC
                    if ((diffLogoStarthBorderStart >= -19) && (diffLogoStarthBorderStart <= 3) &&
                            (diffBorderStarthBorderStop <= 223) && (diffhBorderStopLogoStop >= 960)) {
                        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): false detected hborder from opening credits found, delete hborder marks");
                        // there can also be a false vborder start from opening credits around logo start
                        const cMark *vStart = marks.GetAround(decoder->GetVideoFrameRate(), logoStartBefore->position, MT_VBORDERSTART);
                        if (vStart) marks.Del(vStart->position);
                        marks.Del(hStart->position);
                        marks.Del(hStop->position);
                        return nullptr;
                    }
                    // some dokus have one hborder parts at the beginning of the broadcast
                    // example of one hborder part in broadcast
                    // MT_LOGOSTART ( 21731) -> 189s -> MT_HBORDERSTART ( 31188) -> 141s -> MT_HBORDERSTOP ( 38279) -> 2210s -> MT_LOGOSTOP (148817)
                    // MT_LOGOSTART ( 23761) -> 159s -> MT_HBORDERSTART ( 31756) -> 178s -> MT_HBORDERSTOP ( 40669) -> 2242s -> MT_LOGOSTOP (152787) -> zdf_neo_HD
                    if ((diffLogoStarthBorderStart <= 189) && (diffBorderStarthBorderStop <= 178) && (diffhBorderStopLogoStop >= 2210)) {
                        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): invalid hborder marks from hborder scene in broadcast, delete hborder marks");
                        marks.Del(hStart->position);
                        marks.Del(hStop->position);
                        return nullptr;
                    }
                }
            }
            // some dokus have more than one hborder parts
            // check hborder sequence MT_HBORDERSTART -> MT_HBORDERSTOP -> MT_HBORDERSTART
            cMark *hNextStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
            if (hNextStart) {
                int lengthAd = (hNextStart->position - hStop->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): MT_HBORDERSTART (%d) -> %ds -> MT_HBORDERSTOP (%d) -> %ds -> MT_HBORDERSTART (%d) -> %s", hStart->position, lengthBroadcast, hStop->position, lengthAd, hNextStart->position, macontext.Info.ChannelName);
                // example of more than one hborder part in broadcast
                // MT_HBORDERSTART (36238) -> 87s -> MT_HBORDERSTOP (40601) -> 1602s -> MT_HBORDERSTART (120704)
                if ((lengthBroadcast <= 87) && (lengthAd >= 1602)) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): hborder sequence invalid, assume hborder are in broadcast");
                    marks.DelType(MT_HBORDERCHANGE, 0xF0);
                    return nullptr;
                }
            }

        }

        // found valid horizontal border start mark
        criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
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
                // example of hborder start in dark closing credits from previous recording
                // found logo stop (7569) 17s and logo start (7649) 21s after hborder start (7122)
                if ((diffStop >= -1) && (diffStop <= 17) && (diffStart <= 21)) {
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
    else { // we found no valid hborder start mark
        // check if we have a hborder double episode from recording start
        const cMark *firstBorderStart = marks.GetNext(-1, MT_HBORDERSTART);
        cMark *lastBorderStop = marks.GetPrev(INT_MAX, MT_HBORDERSTOP);
        int diffBorderStopStartA = 0;
        if (lastBorderStop)  diffBorderStopStartA = (lastBorderStop->position - startA) /  decoder->GetVideoFrameRate();
        if (firstBorderStart && (firstBorderStart->position <= IGNORE_AT_START) &&
                ((marks.Count(MT_HBORDERSTART) > marks.Count(MT_HBORDERSTOP) || // we end start part with hborder start
                  (diffBorderStopStartA >= MAX_ASSUMED)))) {                    // we have a hborder stop, but not in start part
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start mark at recording start found, we have a double episode");
            criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
        }
        else { // broadcast has no valid hborder
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): no horizontal border start mark found, disable horizontal border detection and cleanup marks");
            criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_DISABLED, macontext.Config->fullDecode);
            // keep last hborder stop, maybe can use it as start mark
            if (lastBorderStop) { // delete all marks before hborder stop, they can not be a valid start mark
                // delete all invalid hborder marks before last MT_HBORDERSTOP, they are invalid (detection error or from previous broadcast)
                marks.DelFromTo(0, lastBorderStop->position - 1, MT_HBORDERCHANGE, 0xF0);
                cMark *logoStart = marks.GetPrev(lastBorderStop->position);
                // if there is a logo start short before hborder stop, we have a delayed hborder stop from dark opening credits, keep logo start mark
                if (logoStart) {
                    int diff = (lastBorderStop->position - logoStart->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): logo start (%d) %ds before hborder stop (%d) found", logoStart->position, diff, lastBorderStop->position);
                    // valid logo start mark can be some seconds before hborder stop if there are dark opening credits
                    if (diff <= 18) marks.Del(lastBorderStop->position);  // we do not need hborder stop as fallback, we have a near logo start mark
                }
            }
            else marks.DelType(MT_HBORDERCHANGE, 0xF0);  // maybe the is a late invalid hborder start mark, exists sometimes together with old vborder recordings
        }
        return nullptr;
    }
    return hStart;
}

cMark *cMarkAdStandalone::Check_VBORDERSTART(const int maxStart) {
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): search for vborder start mark");
    // check if we have short vbroder start/stop marks from an unreliable small vborder
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): check if vborder marks are valid");
    cMark *vStart = marks.GetNext(-1, MT_VBORDERSTART);
    while (vStart) {
        cMark *vStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);
        if (vStop) {
            int diff = (vStop->position - vStart->position) /  decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): MT_VBORDERSTART (%5d) -> %3ds -> MT_VBORDERSTOP (%5d)", vStart->position, diff, vStop->position);
            if (diff < 82) { // changed from 90 to 82, short first valid part found
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): too short vborder start/stop, delete marks");
                cMark *tmpMark = marks.GetNext(vStart->position, MT_VBORDERSTART);
                marks.Del(vStart->position);
                marks.Del(vStop->position);
                vStart = tmpMark;
                continue;
            }
        }
        vStart = marks.GetNext(vStart->position, MT_VBORDERSTART);
    }

    // search vborder start mark
    vStart = marks.GetAround(240 * decoder->GetVideoFrameRate() + startA, startA, MT_VBORDERSTART);
    if (!vStart) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no vertical border at start found, ignore vertical border detection");
        criteria->SetDetectionState(MT_VBORDERCHANGE, false);
        marks.DelType(MT_VBORDERSTART, 0xFF);  // maybe we have a vborder start from a preview or in a doku, delete it
        const cMark *vStop = marks.GetAround(240 * decoder->GetVideoFrameRate() + startA, startA, MT_VBORDERSTOP);
        if (vStop) {
            int pos         = vStop->position;
            int64_t pts     = vStop->pts;
            char *comment   = nullptr;
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
            marks.Del(pos);
            if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment = nullptr;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, pos, pts, comment);
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
        int vBorderStartvBorderStop = (vStopAfter->position - vStart->position) / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop found at (%d), %ds after vertical border start", vStopAfter->position, vBorderStartvBorderStop);
        // prevent to get start of last part of previous broadcast as start mark
        const cMark *vNextStart = marks.GetNext(vStopAfter->position, MT_VBORDERSTART);
        const cMark *vPrevStart = marks.GetPrev(vStart->position,     MT_VBORDERSTART);
        if (!vPrevStart && !vNextStart) {
            // we have only start/stop vborder sequence in start part, this can be from broadcast before or false vborder detection from dark scene in vborder
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no next vertical border start found after start (%d) and stop (%d)", vStart->position, vStopAfter->position);
            // check if it is false vborder detection from dark scene in vborder
            int startAvBorderStart          = (vStart->position           - startA)               / decoder->GetVideoFrameRate();
            int vBorderStoppacketCheckStart = (decoder->GetPacketNumber() - vStopAfter->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): startA (%5d) -> %4ds -> MT_VBORDERSTART (%5d) -> %3ds -> MT_VBORDERSTOP (%5d) -> %3ds -> packetCheckStart (%5d)", startA, startAvBorderStart,  vStart->position, vBorderStartvBorderStop, vStopAfter->position, vBorderStoppacketCheckStart, packetCheckStart);
            // example of valid vborder marks
            // startA ( 7475) ->  -31s -> MT_VBORDERSTART ( 6685) -> 149s -> MT_VBORDERSTOP (10432) -> 331s -> packetCheckStart (18725)
            // example of invalid vborder from dark scene or from broadcast before
            // startA ( 4075) ->    9s -> MT_VBORDERSTART ( 4310) -> 115s -> MT_VBORDERSTOP ( 7188) -> 355s -> packetCheckStart (16075)
            // startA (16350) -> -288s -> MT_VBORDERSTART ( 1933) -> 281s -> MT_VBORDERSTOP (16019) -> 456s -> packetCheckStart (38850) -> vborder from previous recording
            // startA ( 7450) -> -298s -> MT_VBORDERSTART (    0) -> 329s -> MT_VBORDERSTOP ( 8238) -> 448s -> PacketCheckStart (19450)
            if ((startAvBorderStart <= 9) && (vBorderStartvBorderStop <= 329) && (vBorderStoppacketCheckStart > 331)) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border start (%d) and stop (%d) from closing credits or dark scene, delete marks", vStart->position, vStopAfter->position);
                marks.Del(vStart->position);
                marks.Del(vStopAfter->position);
                criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE, macontext.Config->fullDecode);
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
            criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
            if (!criteria->LogoInBorder()) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): logo marks can not be valid, delete it");
                marks.DelType(MT_LOGOCHANGE, 0xF0);
            }
        }
        return nullptr;
    }

    // check if we have a logo start direct before vborder start, prevent a false vborder start/stop from dark scene as start mark
    if (!criteria->LogoInBorder()) {  // not possible for logo in border channel because vborder start and logo start can be on same position or logo start after vborder start
        cMark *logoStart  = marks.GetPrev(vStart->position, MT_ALL);
        if (logoStart && (logoStart->type == MT_LOGOSTART) && (logoStart->position >= IGNORE_AT_START)) {
            int diff = (vStart->position - logoStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): MT_LOGOSTART (%4d) -> %3ds -> MT_VBORDERSTART (%4d)", logoStart->position, diff, vStart->position);
            // valid vborder example:
            // MT_LOGOSTART (7599) -> 371s -> MT_VBORDERSTART (26172) -> rbb HB, no logo stop before vborder broadcast, maybe logo in border
            // MT_LOGOSTART (  13) -> 352s -> MT_VBORDERSTART (17660) -> ARD alpha HD, start of recoring, logo start from previous broadcast, no logo stop
            // MT_LOGOSTART ( 186) -> 323s -> MT_VBORDERSTART (16358) -> ARD alpha HD, start of recoring, logo start from previous broadcast, no logo stop
            // MT_LOGOSTART (  13) -> 316s -> MT_VBORDERSTART (15846) -> ARD alpha HD, start of recoring, logo start from previous broadcast, no logo stop
            if ((diff > 50) && (diff < 316)) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): logo start mark before vborder start found, delete invalid vborder marks from dark scene");
                marks.DelType(MT_VBORDERCHANGE, 0xF0);
                return nullptr;
            }

        }
    }

    // found valid vertical border start mark
    if (criteria->GetMarkTypeState(MT_ASPECTCHANGE) == CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): use vertical border only as start mark, keep using aspect ratio detection");
        criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);
    }
    else criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED, macontext.Config->fullDecode);

    // check logo start after vborder start to prevent to get closing credit from previous recording as start mark
    if (criteria->LogoInBorder()) {  // prevent to get logo interruption as false start mark
        cMark *logoStart = marks.GetNext(vStart->position, MT_LOGOSTART);
        if (logoStart) {
            int diffStart = (logoStart->position - vStart->position) / decoder->GetVideoFrameRate();
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): found logo start (%d) %ds after vborder start (%d)", logoStart->position, diffStart, vStart->position);
            // near logo start is fade in logo, undetected info logo start mark 12s after valid vborder start
            // changed from 10 to 5
            if ((diffStart >= 5) && (diffStart < 12)) {
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
        if (diffStop < 11) {  // undetected info logo stop mark 11s after valid vborder start
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
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", decoder->GetPacketNumber(), packetCheckStart);
    int maxStart = startA + (length * decoder->GetVideoFrameRate() / 2);  // half of recording
    char *indexToHMSFStart = marks.IndexToHMSF(startA, AV_NOPTS_VALUE, false);
    if (indexToHMSFStart) {
        ALLOC(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %d at %s, max allowed start frame (%d)", startA, indexToHMSFStart, maxStart);
        FREE(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        free(indexToHMSFStart);
    }
    DebugMarks();     //  only for debugging
    const sAspectRatio *aspectRatioFrame = decoder->GetFrameAspectRatio();  // aspect ratio of last read packet in start part

    // set initial mark criteria
    // if we have no hborder start, broadcast can not have hborder
    if (marks.Count(MT_HBORDERSTART) == 0) criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_UNAVAILABLE, macontext.Config->fullDecode);
    else if ((marks.Count(MT_HBORDERSTART) == 1) && (marks.Count(MT_HBORDERSTOP) == 0)) criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);

    // if we have no vborder start, broadcast can not have vborder
    if (marks.Count(MT_VBORDERSTART) == 0) criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE, macontext.Config->fullDecode);
    else if ((marks.Count(MT_VBORDERSTART) == 1) && (marks.Count(MT_VBORDERSTOP) == 0)) criteria->SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);

    // if we have no aspect change in 16:9 video, we have no aspect ratio marks
    if ((marks.Count(MT_ASPECTCHANGE, 0xF0) == 0) && aspectRatioFrame && (aspectRatioFrame->num == 16) && (aspectRatioFrame->den == 9))criteria->SetMarkTypeState(MT_ASPECTCHANGE, CRITERIA_UNAVAILABLE, macontext.Config->fullDecode);

    // if we have no channel change and 2 channel audio, we have no channel marks (GetAC3ChannelCount will return 0 if there is no AC3 stream)
    if ((marks.Count(MT_CHANNELCHANGE, 0xF0) == 0) && (decoder->GetAC3ChannelCount() <= 2)) criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_UNAVAILABLE, macontext.Config->fullDecode);

// check recording start mark
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
    if (!begin && (criteria->GetMarkTypeState(MT_CHANNELCHANGE) > CRITERIA_UNAVAILABLE)) begin = Check_CHANNELSTART();

// check if aspect ratio from VDR info file is valid
    bool checkedAspectRatio = false;
    dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio from VDR info: %d:%d", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
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
    // if aspect ratio from VDR info file is 16:9 and we have aspect ratio marks, check if sequence is valid
    // invalid sequence (all aspect change marks):
    // MT_ASPECTSTOP (start of broadcast, near startA) -> long broadcast -> MT_ASPECTSTOP (start of ad)
    // startA (7600) -> 318s -> MT_ASPECTSTOP (8808) -> 270s -> MT_ASPECTSTART (15565)
    if (!checkedAspectRatio && (macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
        cMark *aspectStop = marks.GetNext(-1, MT_ASPECTCHANGE, 0xF0);
        if (aspectStop && aspectStop->type == MT_ASPECTSTOP) {  // with aspect mark must be MT_ASPECTSTOP
            cMark *aspectStart = marks.GetNext(aspectStop->position, MT_ASPECTSTART);
            if (aspectStart) {
                int aspectStopAfterStartA      = (aspectStart->position - startA)               / decoder->GetVideoFrameRate();
                int aspectStartAfterAspectStop = (aspectStart->position - aspectStop->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::CheckStart(): startA (%d) -> %ds -> MT_ASPECTSTOP (%d) -> %ds -> MT_ASPECTSTART (%d)", startA, aspectStopAfterStartA, aspectStop->position, aspectStartAfterAspectStop, aspectStart->position);
                if ((aspectStopAfterStartA >= 318) && (aspectStartAfterAspectStop >= 270)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): sequence is invalid for 16:9 broadcast, fix to 4:3");
                    SwapAspectRatio();
                }
            }
        }
    }
    video->SetAspectRatioBroadcast(macontext.Info.AspectRatio);  // now aspect ratio is correct, tell it video based detection
    // for 4:3 broadcast cleanup all vborder marks, these are false detected from dark scene or, if realy exists, they are too small for reliable detection
    if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) marks.DelType(MT_VBORDERCHANGE, 0xF0); // delete wrong vborder marks


// aspect ratio start
    if (!begin && (criteria->GetMarkTypeState(MT_ASPECTCHANGE) > CRITERIA_UNAVAILABLE)) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for aspect ratio start mark");
        // search for aspect ratio start mark
        cMark *aStart = marks.GetAround(480 * decoder->GetVideoFrameRate(), startA, MT_ASPECTSTART);
        if (aStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio start mark at (%d), video info is %d:%d", aStart->position, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) { // we have a aspect ratio start mark, check if valid
                criteria->SetMarkTypeState(MT_ASPECTCHANGE, CRITERIA_USED, macontext.Config->fullDecode);  // use aspect ratio marks for detection, even if we have to use another start mark
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
    if (!begin && (criteria->GetMarkTypeState(MT_HBORDERCHANGE) > CRITERIA_UNAVAILABLE)) begin = Check_HBORDERSTART();

// vertical border start
    if (!begin && (criteria->GetMarkTypeState(MT_VBORDERCHANGE) > CRITERIA_UNAVAILABLE)) begin = Check_VBORDERSTART(maxStart);

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
        int maxAssumed = 160; // not too big search range, changed from 240 to 160
        if (macontext.Info.startFromVPS && criteria->GoodVPS()) maxAssumed = 70;  // if we use a valid VPS event based start time do only near search
        begin = marks.GetAround(maxAssumed * decoder->GetVideoFrameRate(), startA, MT_START, 0x0F);
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
            const cMark *channelStart = marks.GetNext(channelStop->position, MT_CHANNELSTART);
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
            cMark *vborderStop = marks.GetAround(decoder->GetVideoFrameRate(), hborderStop->position, MT_VBORDERSTOP);  // closing credit or documentation in frame can end with both stop types, use later
            if (vborderStop && (vborderStop->position > hborderStop->position)) {
                dsyslog("cMarkAdStandalone::CheckStart(): vborder stop (%d) short after hborder stop (%d), use vborder stop", vborderStop->position, hborderStop->position);
            }
            else {
                int diffStartA = (hborderStop->position - startA) /  decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::CheckStart(): MT_HBORDERSTOP (%d) found, %ds after assumed start", hborderStop->position, diffStartA);
                int maxAssumed = MAX_ASSUMED;
                if (macontext.Info.startFromVPS && criteria->GoodVPS()) maxAssumed = 22;  // if we use a valid VPS event based start time do only near search, changed from 58 to 22
                if (abs(diffStartA) <= maxAssumed) {
                    const cMark *hborderStart = marks.GetNext(hborderStop->position, MT_HBORDERSTART);
                    if (!hborderStart) { // if there is a hborder start mark after, hborder stop is not an end mark of previous broadcast
                        dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP (%d) from previous recoring as start mark", hborderStop->position);
                        begin = marks.ChangeType(hborderStop, MT_START);
                        marks.DelTill(begin->position);
                    }
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): MT_HBORDERSTOP (%d) invalid, %ds after assumed start", hborderStop->position, diffStartA);
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
        if (!nextMark || ((nextMark->type & 0x0F) == MT_STOP) || (nextMark->position > (startA + MAX_ASSUMED))) {
            marks.DelTill(startA);
            sMarkAdMark mark = {};
            mark.position    = index->GetKeyPacketNumberAfter(startA, &mark.framePTS);  // set to next key packet
            mark.type        = MT_ASSUMEDSTART;
            AddMark(&mark);
            begin = marks.GetFirst();
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): use start mark (%d) after assumed start (%d)", nextMark->position, startA);
            begin = nextMark;
        }
    }

    // now we have the final start mark, do fine tuning
    if (!begin) {  // can only be happen after abort
        esyslog("cMarkAdStandalone::CheckStart(): no start mark found");
        return;
    }
    marks.DelTill(begin->position, true);    // delete all marks till start mark
    const char *indexToHMSF = marks.GetTime(begin);
    char *typeName = marks.TypeToText(begin->type);
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

    criteria->SetDetectionState(MT_LOWERBORDERCHANGE, false);  // we only use lower border for start and end mark optimization
    CheckStartMark();
    LogSeparator();
    CalculateCheckPositions(marks.GetFirst()->position);
    marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
    doneCheckStart = true;
    restartLogoDetectionDone = false;  // in case of we call CheckStart() after end part in very short recordings

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
bool cMarkAdStandalone::CheckStartMark() {
    LogSeparator();
    bool deleted = false;
    cMark *startMark = marks.GetFirst(); // this is the start mark
    while (startMark) {
        if ((startMark->type & 0x0F) != MT_START) {
            dsyslog("cMarkAdStandalone::CheckStartMark(): invalid type, no start mark (%d)", startMark->position);
            return false;
        }
        // check logo start mark
        int startTimer = macontext.Info.tStart * decoder->GetVideoFrameRate();  // startA is changed to selected start mark
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
                        int lengthBroadcast2    = (stop2->position      - logoStart2->position) / decoder->GetVideoFrameRate();
                        int logoStartAssumed1   = (startMark->position  - startTimer)           / decoder->GetVideoFrameRate();  // delta after timer start event
                        int logoStartAssumed2   = (logoStart2->position - startTimer)           / decoder->GetVideoFrameRate();
                        dsyslog("cMarkAdStandalone::CheckStartMark(): MT_LOGOSTART (%5d) |%4ds| -> %3ds -> MT_LOGOSTOP (%5d) -> %4ds ->  MT_LOGOSTART (%5d) |%4ds| -> %4ds -> MT_LOGOSTOP (%6d) -> %s",  startMark->position, logoStartAssumed1, lengthBroadcast1,  logoStop1->position, lengthAd, logoStart2->position, logoStartAssumed2, lengthBroadcast2, stop2->position, macontext.Info.ChannelName);
// check for short broadcast before start mark (preview) and long broadcast after (first part of broadcast)
// example of invalid logo start mark
// MT_LOGOSTART ( 4675)         ->  11s -> MT_LOGOSTOP ( 4962) ->  150s ->  MT_LOGOSTART ( 8715)        -> 1312s -> MT_LOGOSTOP ( 41526) -> Nickelodeon, ad with false detected logo
// MT_LOGOSTART ( 2944)         ->  25s -> MT_LOGOSTOP ( 3584) ->   50s ->  MT_LOGOSTART ( 4848)        -> 1213s -> MT_LOGOSTOP ( 35173) -> sixx, start of preview before broadcast
// MT_LOGOSTART ( 3828)         ->  28s -> MT_LOGOSTOP ( 4549) ->   37s ->  MT_LOGOSTART ( 5487)        -> 1237s -> MT_LOGOSTOP ( 36427) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 3785)         ->  28s -> MT_LOGOSTOP ( 4507) ->   31s ->  MT_LOGOSTART ( 5285)        -> 1202s -> MT_LOGOSTOP ( 35355) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 2979)         ->  33s -> MT_LOGOSTOP ( 3824) ->   54s ->  MT_LOGOSTART ( 5183)        ->  955s -> MT_LOGOSTOP ( 29081) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 2870)         ->  56s -> MT_LOGOSTOP ( 4276) ->   50s ->  MT_LOGOSTART ( 5532)        -> 1218s -> MT_LOGOSTOP ( 35991) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 3867)         ->  34s -> MT_LOGOSTOP ( 4718) ->   29s ->  MT_LOGOSTART ( 5458)        -> 1229s -> MT_LOGOSTOP ( 36205) -> SIXX, start of preview before broadcast
// MT_LOGOSTART ( 1015)         ->  63s -> MT_LOGOSTOP ( 2611) ->   87s ->  MT_LOGOSTART ( 4804)        -> 1236s -> MT_LOGOSTOP ( 35723) -> SIXX, start of preview before broadcast
//
// example of valid logo start mark
// MT_LOGOSTART ( 8542) |  40s| -> 113s -> MT_LOGOSTOP (11377) ->  197s ->  MT_LOGOSTART (16305) | 351s| -> 1149s -> MT_LOGOSTOP ( 45049) -> Comedy_Central, vera short first broadcast
                        if ((lengthBroadcast1 < 113) && (lengthBroadcast2 >= 955)) {
                            dsyslog("cMarkAdStandalone::CheckStartMark(): too short first broadcast part, delete start (%d) and stop (%d) mark", startMark->position, logoStop1->position);
                            marks.Del(startMark->position);
                            marks.Del(logoStop1->position);
                            deleted = true;
                            startMark = marks.GetFirst();
                        }
// check for very short first ad, which is usually between broadcasts, but take care of undetected info logo
// example of valid logo start mark
// MT_LOGOSTART (13699) | 246s| -> 172s -> MT_LOGOSTOP (18002) ->    0s ->  MT_LOGOSTART (18011) | 419s| ->  397s -> MT_LOGOSTOP ( 27952) -> Comedy Central: info logo
// MT_LOGOSTART (11703) | 167s| -> 175s -> MT_LOGOSTOP (16094) ->    0s ->  MT_LOGOSTART (16105) | 343s| ->  425s -> MT_LOGOSTOP ( 26745) -> Comedy Central: info logo
// MT_LOGOSTART (11919) | 176s| -> 191s -> MT_LOGOSTOP (16717) ->    0s ->  MT_LOGOSTART (16728) | 369s| ->  574s -> MT_LOGOSTOP ( 31092) -> Comedy Central: info logo
// MT_LOGOSTART ( 4725) |   9s| -> 216s -> MT_LOGOSTOP (10145) ->    6s ->  MT_LOGOSTART (10303) | 232s| ->  501s -> MT_LOGOSTOP ( 22838) -> Comedy Central: info logo
// MT_LOGOSTART ( 7733) |   8s| -> 130s -> MT_LOGOSTOP (10984) ->    0s ->  MT_LOGOSTART (10994) | 138s| ->  501s -> MT_LOGOSTOP ( 23533) -> Comedy Central: info logo
// MT_LOGOSTART ( 7486) |   0s| ->  94s -> MT_LOGOSTOP ( 9858) ->    0s ->  MT_LOGOSTART ( 9868) |  94s| ->  472s -> MT_LOGOSTOP ( 21678) -> Comedy Central: info logo
// MT_LOGOSTART ( 5831) | -65s| -> 179s -> MT_LOGOSTOP (10322) ->    9s ->  MT_LOGOSTART (10559) | 123s| -> 1249s -> MT_LOGOSTOP ( 41785) -> Disney_Channel, preview in frame
//
// example of invalid logo start mark, delete first start/stop pair
// MT_LOGOSTART ( 4194) |-216s| -> 600s -> MT_LOGOSTOP (34217) ->   64s ->  MT_LOGOSTART (37442) | 448s| -> 2034s -> MT_LOGOSTOP (139176) -> Das_Erste_HD
// MT_LOGOSTART ( 2945) |-183s| -> 374s -> MT_LOGOSTOP (12312) ->    6s ->  MT_LOGOSTART (12462) | 197s| ->  470s -> MT_LOGOSTOP ( 24223) -> Comedy_Central
// MT_LOGOSTART ( 6975) | -21s| -> 389s -> MT_LOGOSTOP (16707) ->    3s ->  MT_LOGOSTART (16782) | 371s| ->  703s -> MT_LOGOSTOP ( 34377) -> Comedy_Central
// MT_LOGOSTART (  596) |-279s| -> 614s -> MT_LOGOSTOP (15954) ->   25s ->  MT_LOGOSTART (16584) | 360s| -> 2360s -> MT_LOGOSTOP ( 75585) -> RTLZWEI
                        else if ((logoStartAssumed1 <= 0) && (logoStartAssumed2 <= 448) && // should be near event start
                                 (lengthBroadcast1 >= 181) && (lengthBroadcast1 <= 614) &&
                                 (lengthAd >= 3) && (lengthAd <= 64) &&                    // very short ad can be undeteced info logo
                                 (lengthBroadcast2 >= 143)) {
                            dsyslog("cMarkAdStandalone::CheckStartMark(): too short first ad, delete start (%d) and stop (%d) mark", startMark->position, logoStop1->position);
                            marks.Del(startMark->position);
                            marks.Del(logoStop1->position);
                            deleted = true;
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
                int minFirstBroadcast = 60;                                        // more trust strong marks
                if (startMark->type == MT_CHANNELSTART)  minFirstBroadcast =  29;  // found very short first broadcast with channel marks (ProSieben)
                else if (startMark->type < MT_LOGOSTART) minFirstBroadcast = 106;  // changed from 77 to 80 to 106
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
                            deleted = true;
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
    return deleted;
}


void cMarkAdStandalone::DebugMarks() {           // write all marks to log file
    marks.Debug();
#ifdef DEBUG_WEAK_MARKS
    // weak marks
    dsyslog("cMarkAdStandalone::DebugMarks(): current black marks:");
    blackMarks.Debug();
    dsyslog("cMarkAdStandalone::DebugMarks(): current silence marks:");
    silenceMarks.Debug();
    dsyslog("cMarkAdStandalone::DebugMarks(): current scene change marks:");
    sceneMarks.Debug();
#endif
}


void cMarkAdStandalone::CheckMarks() {           // cleanup marks that make no sense
    LogSeparator(true);

    const cMark *firstMark = marks.GetFirst();
    if (!firstMark) {
        esyslog("no marks at all detected, something went very wrong");
        return;
    }
    int newStopA = firstMark->position + decoder->GetVideoFrameRate() * length;  // we have to recalculate stopA with final start mark
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
    const cMark *vborderStart = marks.GetNext(0, MT_VBORDERSTART);
    const cMark *vborderStop  = marks.GetNext(0, MT_VBORDERSTOP);
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
                    dsyslog("cMarkAdStandalone::CheckMarks(): MT_LOGOSTART (%6d) -> %7dms -> [MT_LOGOSTOP (%6d) -> %7ldms -> MT_LOGOSTART (%6d)] -> %7dms -> MT_STOP (%6d) -> %s", prevLogoStart->position, prevLogoStart_Stop, mark->position, stop_nextLogoStart, nextLogoStart->position, nextLogoStart_nextStop, nextStop->position, macontext.Info.ChannelName);

// cleanup logo detection failure
// delete sequence long broadcast -> very short stop/start -> long broadcast
// MT_LOGOSTART ( 15766) -> 1103180ms -> MT_LOGOSTOP ( 70925) ->    1040ms -> MT_LOGOSTART ( 70977) ->   83260ms -> MT_STOP ( 75140)
// MT_LOGOSTART ( 70977) ->   83260ms -> MT_LOGOSTOP ( 75140) ->    1020ms -> MT_LOGOSTART ( 75191) -> 1536500ms -> MT_STOP (152016)
// MT_LOGOSTART ( 13993) -> 1092500ms -> MT_LOGOSTOP ( 68618) ->    1700ms -> MT_LOGOSTART ( 68703) ->   82620ms -> MT_STOP ( 72834)
// MT_LOGOSTART ( 14595) ->  752000ms -> MT_LOGOSTOP ( 52195) ->    2500ms -> MT_LOGOSTART ( 52320) ->  718960ms -> MT_STOP ( 88268) -> ZDFinfo_HD
                    if (    (prevLogoStart_Stop     >= 83260) &&
                            (stop_nextLogoStart     <=  2500) &&
                            (nextLogoStart_nextStop >= 82620)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from logo detection failure, deleting", mark->position, nextLogoStart->position);
                        cMark *tmp = nextStop;
                        marks.Del(nextLogoStart);
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }

// delete very short stop/start pair from undetected introduction logo
// short after valid logo start -> short logo interruption -> long broadcast after
// valid short stop/start, do not delete
// MT_LOGOSTART ( 48867) ->    4880ms -> MT_LOGOSTOP ( 48989) ->     760ms -> MT_LOGOSTART ( 49008) ->  795000ms -> MT_STOP (68883)
// MT_LOGOSTART ( 51224) ->   29800ms -> MT_LOGOSTOP ( 51969) ->     920ms -> MT_LOGOSTART ( 51992) ->  622840ms -> MT_STOP (67563)
// MT_LOGOSTART ( 49708) ->  593600ms -> MT_LOGOSTOP ( 64548) ->     120ms -> MT_LOGOSTART ( 64551) ->   14880ms -> MT_STOP (64923)
// MT_LOGOSTART ( 37720) ->   26280ms -> MT_LOGOSTOP ( 38377) ->     840ms -> MT_LOGOSTART ( 38398) ->  254120ms -> MT_STOP (44751)
// MT_LOGOSTART ( 74875) ->    1400ms -> MT_LOGOSTOP ( 74910) ->     760ms -> MT_LOGOSTART ( 74929) ->  566480ms -> MT_STOP ( 89091)
//
// invalid stop/start pair from introduction logo change (detected as logo) to normal logo (kabel eins)
// MT_LOGOSTART ( 99981) ->    7720ms -> MT_LOGOSTOP (100174) ->    1120ms -> MT_LOGOSTART (100202) -> 1098840ms -> MT_STOP (127673) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 41139) ->    7760ms -> MT_LOGOSTOP ( 41333) ->    1080ms -> MT_LOGOSTART ( 41360) ->  872280ms -> MT_STOP ( 63167) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 74781) ->    7800ms -> MT_LOGOSTOP ( 74976) ->    1040ms -> MT_LOGOSTART ( 75002) ->  416280ms -> MT_STOP ( 85409) -> introdution logo change (kabel eins)
// MT_LOGOSTART ( 63508) ->    7920ms -> MT_LOGOSTOP ( 63706) ->     880ms -> MT_LOGOSTART ( 63728) -> 1204960ms -> MT_STOP ( 93852) -> introdution logo change (kabel eins)
// MT_LOGOSTART (172892) ->    6240ms -> MT_LOGOSTOP (173048) ->     840ms -> MT_LOGOSTART (173069) ->  156480ms -> MT_STOP (176981) -> kabel_eins
                    if (criteria->IsIntroductionLogoChannel() &&
                            (prevLogoStart_Stop     >=   6240) && (prevLogoStart_Stop <= 7920) &&
                            (stop_nextLogoStart     >=    840) && (stop_nextLogoStart <= 1120) &&
                            (nextLogoStart_nextStop >= 156480)) {
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
// MT_LOGOSTART ( 85792) ->    4320ms -> MT_LOGOSTOP ( 85900) ->     280ms -> MT_LOGOSTART ( 85907) -> 1428200ms -> MT_STOP (121612) -> TELE_5
                    if (criteria->IsLogoChangeChannel() &&
                            (prevLogoStart_Stop     >= 1120) && (prevLogoStart_Stop     <=    8440) &&
                            (stop_nextLogoStart     >=  280) && (stop_nextLogoStart     <=    1120) &&
                            (nextLogoStart_nextStop >=  560) && (nextLogoStart_nextStop <= 1428200)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from undetected logo change, deleting", mark->position, nextLogoStart->position);
                        cMark *tmp = nextStop;
                        marks.Del(nextLogoStart);
                        marks.Del(mark);
                        mark = tmp;
                        continue;
                    }
// invalid stop/start pair from short logo interruption channel, delete pair
// delete more aggressiv
// detect from long broadcast before (logo interuption at end of part) or long broadcast after (logo interuption at start of part
// example of invalid logo stop/start pair from logo interruption
// near after valid logo start
// MT_LOGOSTART ( 33854) ->   17800ms -> [MT_LOGOSTOP ( 34299) ->     800ms -> MT_LOGOSTART ( 34319)] ->  272280ms -> MT_STOP ( 41126) -> Comedy_Central
// MT_LOGOSTART ( 42010) ->   17800ms -> [MT_LOGOSTOP ( 42455) ->     840ms -> MT_LOGOSTART ( 42476)] ->  543960ms -> MT_STOP ( 56075) -> Comedy_Central
// MT_LOGOSTART ( 40019) ->   35080ms -> [MT_LOGOSTOP ( 40896) ->     960ms -> MT_LOGOSTART ( 40920)] ->  668720ms -> MT_STOP ( 57638) -> Comedy_Central
// MT_LOGOSTART ( 20580) ->   43920ms -> [MT_LOGOSTOP ( 21678) ->    1120ms -> MT_LOGOSTART ( 21706)] ->  499320ms -> MT_STOP ( 34189) -> Comedy_Central
// MT_LOGOSTART ( 27011) ->   11960ms -> [MT_LOGOSTOP ( 27310) ->     720ms -> MT_LOGOSTART ( 27328)] ->  551920ms -> MT_STOP ( 41126) -> Comedy_Central
// MT_LOGOSTART ( 24262) ->   11720ms -> [MT_LOGOSTOP ( 24555) ->     960ms -> MT_LOGOSTART ( 24579)] ->  314000ms -> MT_STOP ( 32429) -> Comedy_Central
// MT_LOGOSTART ( 41408) ->   17960ms -> [MT_LOGOSTOP ( 41857) ->     640ms -> MT_LOGOSTART ( 41873)] ->  393360ms -> MT_STOP ( 51707) -> Comedy_Central
//
// double logo interruption near after valid logo start
// MT_LOGOSTART ( 32649) ->   11640ms -> [MT_LOGOSTOP ( 32940) ->    1040ms -> MT_LOGOSTART ( 32966)] ->    5120ms -> MT_STOP ( 33094) -> Comedy_Central
// MT_LOGOSTART ( 30526) ->   11640ms -> [MT_LOGOSTOP ( 30817) ->    1080ms -> MT_LOGOSTART ( 30844)] ->    5080ms -> MT_STOP ( 30971) -> Comedy_Central
// MT_LOGOSTART ( 21647) ->   11720ms -> [MT_LOGOSTOP ( 21940) ->    1000ms -> MT_LOGOSTART ( 21965)] ->    5040ms -> MT_STOP ( 22091) -> Comedy_Central
//
// double logo interruption near before valid logo stop
// MT_LOGOSTART ( 32162) ->  556560ms -> [MT_LOGOSTOP ( 46076) ->     880ms -> MT_LOGOSTART ( 46098)] ->    5160ms -> MT_STOP ( 46227) -> Comedy_Central
//
// near before valid logo stop
// MT_LOGOSTART ( 10503) ->  158960ms -> [MT_LOGOSTOP ( 14477) ->     520ms -> MT_LOGOSTART ( 14490)] ->   11600ms -> MT_STOP ( 14780) -> Comedy_Central
// MT_LOGOSTART (  9887) ->  441480ms -> [MT_LOGOSTOP ( 20924) ->     520ms -> MT_LOGOSTART ( 20937)] ->   23840ms -> MT_STOP ( 21533) -> Comedy_Central
// MT_LOGOSTART ( 10503) ->  575520ms -> [MT_LOGOSTOP ( 24891) ->     520ms -> MT_LOGOSTART ( 24904)] ->   11760ms -> MT_STOP ( 25198) -> Comedy_Central
// MT_LOGOSTART (  8634) ->  500760ms -> [MT_LOGOSTOP ( 21153) ->     520ms -> MT_LOGOSTART ( 21166)] ->   23840ms -> MT_STOP ( 21762) -> Comedy_Central
// MT_LOGOSTART (  9887) ->  700920ms -> [MT_LOGOSTOP ( 27410) ->     520ms -> MT_LOGOSTART ( 27423)] ->   17760ms -> MT_STOP ( 27867) -> Comedy_Central
// MT_LOGOSTART ( 32162) ->  562600ms -> [MT_LOGOSTOP ( 46227) ->     720ms -> MT_LOGOSTART ( 46245)] ->   31800ms -> MT_STOP ( 47040) -> Comedy_Central (conflict)
//
// example of valid logo stop/start pair
// MT_LOGOSTART ( 25628) ->  507960ms -> [MT_LOGOSTOP ( 38327) ->     720ms -> MT_LOGOSTART ( 38345)] ->   25600ms -> MT_STOP ( 38985) -> Comedy_Central
// MT_LOGOSTART (  8004) ->  634320ms -> [MT_LOGOSTOP ( 23862) ->     680ms -> MT_LOGOSTART ( 23879)] ->   18160ms -> MT_STOP ( 24333) -> Comedy_Central
// MT_LOGOSTART ( 32071) ->  307720ms -> [MT_LOGOSTOP ( 39764) ->     600ms -> MT_LOGOSTART ( 39779)] ->   25120ms -> MT_STOP ( 40407) -> Comedy_Central
                    if (criteria->IsLogoInterruptionChannel() &&
                            ((prevLogoStart_Stop     >=  11720) && (prevLogoStart_Stop     <=  43920) &&    // short broadcast before, long after
                             (stop_nextLogoStart     >=    640) && (stop_nextLogoStart     <=   1120) &&
                             (nextLogoStart_nextStop >= 272280) && (nextLogoStart_nextStop <= 668720)) ||

                            ((prevLogoStart_Stop     >=  11640) && (prevLogoStart_Stop     <=  11720) &&    // short broadcast before, very short after
                             (stop_nextLogoStart     >=   1000) && (stop_nextLogoStart     <=   1080) &&
                             (nextLogoStart_nextStop >=   5040) && (nextLogoStart_nextStop <=   5120)) ||

                            ((prevLogoStart_Stop     >= 556560) && (prevLogoStart_Stop     <=  556561) &&    // long broadcast before, very short after
                             (stop_nextLogoStart     >=    880) && (stop_nextLogoStart     <=     881) &&
                             (nextLogoStart_nextStop >=   5160) && (nextLogoStart_nextStop <=    5161)) ||

                            ((prevLogoStart_Stop     >= 158960) && (prevLogoStart_Stop     <= 700920) &&    // long broadcast before, short after
                             (stop_nextLogoStart     >=    520) && (stop_nextLogoStart     <     680) &&
                             (nextLogoStart_nextStop >=  11600) && (nextLogoStart_nextStop <   25120))) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair from logo change channel, deleting", mark->position, nextLogoStart->position);
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
// logo start -> logo stop:  long broadcast is before advertisement or short broadcast is previous preview
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  preview  (this start mark is the current mark in the loop)
//                           first start mark and last stop mark could not be part of a preview
// logo stop  -> logo start: advertisement
// logo start -> logo stop:  long broadcast is after advertisement or short broadcast is next preview
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
        dsyslog("cMarkAdStandalone::CheckMarks(): MT_LOGOSTART(%6d)->%7dms->MT_LOGOSTOP(%6d)->%dms->[MT_LOGOSTART(%6d)->%dms->MT_LOGOSTOP(%6d)]->%dms->MT_LOGOSTART(%6d)->%dms->MT_STOP(%6d)", startBefore->position, lengthBroadcastBefore, stopBefore->position, lengthAdBefore, mark->position, lengthPreview, stopMark->position, lengthAdAfter, startAfter->position, lengthBroadcastAfter, stopAfter->position);
// preview example
// tbd
//
// no preview example
//            |- broadcast before --|         |---- ad before ----|           |---- broadcast ---|         |----- short ad ----|          |-- broadcast after -|
// MT_LOGOSTART( 40485)-> 940800ms->MT_LOGOSTOP( 64005)->612600ms->[MT_LOGOSTART(79320)->79800ms->MT_LOGOSTOP(81315)]->12000ms->MT_LOGOSTART(81615)->1365600ms->MT_STOP(115755)
//
//            |---- broadcast -----|          |----- short ad ------|         |----- broadcast ----|      |- short logo interruption -|    |- ad in frame with logo -|
// MT_LOGOSTART(149051)->1899920ms->MT_LOGOSTOP(196549)->11440ms->[MT_LOGOSTART(196835)->132360ms->MT_LOGOSTOP(200144)]->200ms->MT_LOGOSTART(200149)->19960ms->MT_LOGOSTOP(200648)
//
//            |---- broadcast ----|         |- logo detection error -|   |--- broadcast --|          |- logo detection error -|    |---- broadcast -----|
// MT_LOGOSTART(13928)->785120ms->MT_LOGOSTOP(53184)->500ms->[MT_LOGOSTART(53209)->9860ms->MT_LOGOSTOP(53702)]->2840ms->MT_LOGOSTART(53844)->1835720ms->MT_STOP(145630)

        // check if we have long broadcast before, a long broadcast after and only very short ads in sequence,
        // in this case it could not be a preview, it is a double logo detection error
        if ((lengthBroadcastBefore >= 785120) && (lengthBroadcastAfter >= 1835720) && (lengthAdBefore <= 500) && (lengthAdAfter <= 2840)) { // ignore double logo detection error
            dsyslog("cMarkAdStandalone::CheckMarks(): double logo detection failure");
            continue;
        }

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


// delete start/stop hborder pairs before packetCheckStart if there are no other hborder marks, they are a preview with hborder before recording start
    mark = marks.GetFirst();
    if (mark && (mark->type == MT_HBORDERSTART) && (marks.Count(MT_HBORDERSTART) == 1)) {
        const cMark *markNext = mark->Next();
        if (markNext && (markNext->type == MT_HBORDERSTOP) && (markNext->position < packetCheckStart) && (markNext->position != marks.GetLast()->position) && (marks.Count(MT_HBORDERSTOP) == 1)) {
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


// if we have a VPS events, move start and end mark of weak marks to VPS event
// do not use pause events, detected marks are usually better
    LogSeparator();
    if (macontext.Config->useVPS) {
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): apply VPS events");
        DebugMarks();     //  only for debugging
        int vpsOffset = vps->GetStart(); // VPS start mark
        if (vpsOffset >= 0) {
            isyslog("VPS start event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_START);
        }
        else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS start event found");

        vpsOffset = vps->GetStop();     // VPS stop mark
        if (vpsOffset >= 0) {
            isyslog("VPS stop  event at %d:%02d:%02d", vpsOffset / 60 / 60,  (vpsOffset / 60 ) % 60, vpsOffset % 60);
            AddMarkVPS(vpsOffset, MT_STOP);
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

    bool moreMarks = true;
    while (moreMarks) {
        if (abortNow) return;
        moreMarks = false;
        // get last 3 marks
        cMark *lastStopMark = marks.GetLast();
        if (!lastStopMark) break;
        if ((lastStopMark->type & 0x0F) != MT_STOP) break;
        if (((lastStopMark->type & 0xF0) >= MT_CHANNELCHANGE) && ((lastStopMark->type & 0xF0) != MT_MOVED)) break;  // trust channel marks and better
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
        int diffPrevStopAssumed  = (prevStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
        int diffLastStopAssumed  = (lastStopMark->position  - newStopA)                / decoder->GetVideoFrameRate();
        int lastAd               = (lastStartMark->position - prevStopMark->position)  / decoder->GetVideoFrameRate();
        dsyslog("cMarkAdStandalone::CheckMarks(): MT_START (%6d) -> %4ds -> MT_STOP (%6d) |%4ds| -> %3ds -> MT_START (%6d) -> %3ds -> MT_STOP (%6d) |%3ds| -> %3s", prevStartMark->position, prevBroadcast, prevStopMark->position, diffPrevStopAssumed, lastAd, lastStartMark->position, lastBroadcast, lastStopMark->position, diffLastStopAssumed, macontext.Info.ChannelName);
        switch(lastStopMark->type) {
        case MT_ASSUMEDSTOP:
            // example of invalid assumed stop mark sequence (short last ad is between two broadcasts)
            // MT_START ( 90722) -> 1317s -> MT_STOP (156579)         -> 170s -> MT_START (165114) -> 170s -> MT_STOP (173634)
            // MT_START (103247) -> 1096s -> MT_STOP (158068) |-348s| -> 140s -> MT_START (165090) -> 207s -> MT_STOP (175483) |   0s|
            // MT_START (109086) -> 1061s -> MT_STOP (162165) |-298s| ->  71s -> MT_START (165764) -> 226s -> MT_STOP (177074) |   0s|
            if ((prevBroadcast >= 1061) && (diffPrevStopAssumed >= -359) && (lastAd <= 170) && (lastBroadcast >= 150)) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last ad too short", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            // example of invalid assumed stop mark sequence (last broadcast too short)
            // MT_START (  4200) -> 2400s -> MT_STOP ( 64215) |-418s| -> 401s -> MT_START ( 74254) ->  17s -> MT_STOP ( 74685) |  0s|
            // MT_START (  4184) -> 2338s -> MT_STOP ( 62638) |-264s| -> 260s -> MT_START ( 69142) ->   4s -> MT_STOP ( 69259) |  0s|
            // MT_START ( 79040) ->   21s -> MT_STOP ( 79579) | -62s| ->  58s -> MT_START ( 81040) ->   4s -> MT_STOP ( 81151) |  0s|
            // MT_START (  7428) -> 1262s -> MT_STOP ( 38982) |-537s| -> 397s -> MT_START ( 48924) -> 140s -> MT_STOP ( 52427) |  0s|
            // MT_START (  7642) -> 1263s -> MT_STOP ( 39236) |-536s| -> 362s -> MT_START ( 48297) -> 173s -> MT_STOP ( 52641) |  0s|
            // MT_START (  5836) -> 1311s -> MT_STOP ( 38629) |-488s| -> 486s -> MT_START ( 50798) ->   1s -> MT_STOP ( 50835) |  0s|
            // MT_START (  8450) -> 1296s -> MT_STOP ( 40853) |-503s| -> 307s -> MT_START ( 48532) -> 196s -> MT_STOP ( 53449) |  0s|
            // MT_START ( 10748) -> 1311s -> MT_STOP ( 43547) |-488s| -> 265s -> MT_START ( 50185) -> 211s -> MT_STOP ( 55461) |-11s|   // assumed stop after recording end
            // MT_START (  7443) -> 1303s -> MT_STOP ( 40021) |-496s| -> 278s -> MT_START ( 46979) -> 218s -> MT_STOP ( 52442) |  0s|
            // MT_START (  7389) -> 1250s -> MT_STOP ( 38659) |-549s| -> 498s -> MT_START ( 51123) ->  50s -> MT_STOP ( 52380) |  0s|
            // MT_START (167649) -> 1163s -> MT_STOP (196735) |-621s| -> 515s -> MT_START (209631) -> 105s -> MT_STOP (212268) |  0s|
            else if ((diffPrevStopAssumed >= -621) && (lastBroadcast <= 218)) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last broadcast too short", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            break;
        case MT_LOGOSTOP:
            // example of invalid logo stop mark sequence (short last ad is between two long broadcasts)
            // MT_START ( 73336) ->  960s -> MT_STOP ( 97358)         ->  18s -> MT_START ( 97817) ->  83s -> MT_STOP ( 99916)
            // MT_START ( 97756) ->    0s -> MT_STOP ( 97761)         ->   0s -> MT_START ( 97766) ->  86s -> MT_STOP ( 99916)        -> more than one false logo stop
            // MT_START ( 97460) ->    0s -> MT_STOP ( 97465)         ->   0s -> MT_START ( 97469) ->  86s -> MT_STOP ( 99619)        -> more than one false logo stop
            // MT_START ( 66619) ->  694s -> MT_STOP ( 83973)         ->  10s -> MT_START ( 84240) -> 610s -> MT_STOP ( 99507)
            // MT_START ( 39172) ->  931s -> MT_STOP ( 62463) |-280s| ->   5s -> MT_START ( 62601) -> 408s -> MT_STOP ( 72811) |133s|
            // MT_START ( 42975) ->  423s -> MT_STOP ( 53574) |-166s| ->  39s -> MT_START ( 54564) -> 232s -> MT_STOP ( 60368) |105s|
            // MT_START ( 12594) -> 1226s -> MT_STOP ( 43264) |-273s| ->  21s -> MT_START ( 43789) -> 177s -> MT_STOP ( 48232) |-74s| -> Comedy_Central: length too big
            // MT_START ( 36120) ->  675s -> MT_STOP ( 53011) |-140s| ->  21s -> MT_START ( 53536) -> 113s -> MT_STOP ( 56371) | -5s| -> Comedy_Central: length too big
            // MT_START ( 30964) ->  635s -> MT_STOP ( 46844) |-124s| ->  59s -> MT_START ( 48324) ->  95s -> MT_STOP ( 50712) | 30s| -> Comedy_Central: length too big
            if ((diffPrevStopAssumed >= -280) && (lastAd <= 59) && (diffLastStopAssumed >= -74)) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last ad too short", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            // example of invalid log stop mark sequence (too short last broadcast is preview)
            // MT_START ( 45380) ->  290s -> MT_STOP ( 52642) |  57s| -> 141s -> MT_START ( 56190) ->   0s -> MT_STOP ( 56199) | 200s|
            // MT_START (  4106) -> 2700s -> MT_STOP ( 71623) |-691s| -> 595s -> MT_START ( 86503) ->  77s -> MT_STOP ( 88450) | -18s|
            // MT_START (  8469) -> 2395s -> MT_STOP ( 68361) |-401s| -> 222s -> MT_START ( 73925) ->  82s -> MT_STOP ( 75995) | -95s|
            // MT_START ( 26642) -> 1702s -> MT_STOP (111783) |-301s| ->  17s -> MT_START (112636) -> 129s -> MT_STOP (119135) |-154s| -> arte_HD  (conflict)
            //
            // example of valid logo end mark
            // MT_START ( 48141) -> 1050s -> MT_STOP ( 74406) |-644s| -> 468s -> MT_START ( 86116) -> 129s -> MT_STOP ( 89351) | -46s| -> VOXup
            // MT_START ( 51714) ->  955s -> MT_STOP ( 75611) |-638s| -> 436s -> MT_START ( 86513) -> 117s -> MT_STOP ( 89462) |-84s| -> VOXup
            else if (lastBroadcast < 117) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last broadcast too short", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            // example of invalid log stop mark sequence (short last broadcast, late logo stop)
            // MT_START ( 15111) -> 2550s -> MT_STOP (142613) | -31s| ->  71s -> MT_START (146196) -> 147s -> MT_STOP (153589) |187s| -> ZDF_H
            else if ((diffPrevStopAssumed >= -31) && (lastBroadcast <= 147)) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, last broadcast short and previous logo stop near assumed end", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            break;
        case MT_MOVEDSTOP:
            // example of invalid logo stop mark sequence (too short last broadcase)
            // MT_START ( 19164) -> 1565s -> MT_STOP ( 97440) |-247s| ->  57s -> MT_START (100294) ->  26s -> MT_STOP (101638) |-163s|  (VPS stop)
            // MT_START ( 18475) -> 1568s -> MT_STOP ( 96899) |-244s| ->  47s -> MT_START ( 99287) ->  47s -> MT_STOP (101638) |-149s|  (VPS stop)
            if (lastBroadcast <= 47) {
                dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, too short last broadcast part", prevStopMark->position);
                marks.Del(lastStopMark->position);
                marks.Del(lastStartMark->position);
                moreMarks = true;
            }
            break;
        default:
            moreMarks = false;
            break;
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


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type) {
    if (!decoder) return;
    if (!index)   return;
    int vpsFrame = index->GetFrameFromOffset(offset * 1000);
    if (vpsFrame < 0) {
        esyslog("cMarkAdStandalone::AddMarkVPS(): failed to get frame from offset %d", offset);
        return;
    }
    // get current start / end mark and nearest mark with same type
    cMark *mark     = nullptr;
    cMark *nearMark = nullptr;  // nearest mark with same type, must be same as start / end mark
    switch (type) {
    case MT_START: {
        mark = marks.GetFirst();
        if (!mark) {
            esyslog("cMarkAdStandalone::AddMarkVPS(): no marks found");
            return;
        }
        nearMark = marks.GetAround(INT_MAX, vpsFrame, MT_START, 0x0F);
        if (!nearMark) {
            esyslog("cMarkAdStandalone::AddMarkVPS(): no near mark with same type found");
            return;
        }
        char *nearMarkType = marks.TypeToText(nearMark->type);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): start mark (%d), VPS start event (%d), nearest start mark: (%d) %s start", mark->position, vpsFrame, nearMark->position, nearMarkType);
        FREE(strlen(nearMarkType) + 1, "text");
        free(nearMarkType);
        // only use VPS start event for weak marks
        if (mark->type != MT_ASSUMEDSTART) {
            char *markType = marks.TypeToText(mark->type);
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep strong %s start (%d) as start mark", markType, mark->position);
            FREE(strlen(markType) + 1, "text");
            free(markType);
            return;
        }
        break;
    }
    case MT_STOP: {
        mark = marks.GetLast();
        if (!mark) {
            esyslog("cMarkAdStandalone::AddMarkVPS(): no marks found");
            return;
        }
        nearMark = marks.GetAround(INT_MAX, vpsFrame, MT_STOP, 0x0F);
        if (!nearMark) {
            esyslog("cMarkAdStandalone::AddMarkVPS(): no near mark with same type found");
            return;
        }
        cMark *nextStartMark = marks.GetNext(vpsFrame, MT_START, 0x0F);
        if (nextStartMark) {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): start mark (%d) after VPS stop event found, VPS stop event seems to be invalid", nextStartMark->position);
            return;
        }
        int diff = abs(vpsFrame - nearMark->position) / decoder->GetVideoFrameRate();
        char *nearMarkType = marks.TypeToText(nearMark->type);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): current end mark (%d), VPS stop event (%d), nearest stop mark (%d) stop %s is %ds before VPS event", mark->position, vpsFrame, nearMark->position, nearMarkType, diff);
        FREE(strlen(nearMarkType) + 1, "text");
        free(nearMarkType);
        // keep strong end marks, they are better than VPS marks
        if ((mark->type != MT_ASSUMEDSTOP) &&
                ((mark->type != MT_TYPECHANGESTOP) || !criteria->GoodVPS())) {
            char *markType = marks.TypeToText(mark->type);
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep strong stop %s mark (%d) as end mark", markType, mark->position);
            FREE(strlen(markType) + 1, "text");
            free(markType);
            return;
        }
        // use near strong and reliable stop mark as end mark
        if ((diff <= 49) && (nearMark->type >= MT_CHANNELSTOP)) {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): assumed end mayby wrong, use reliable stop mark (%d) near VPS event (%d) as end mark", nearMark->position, vpsFrame);
            marks.DelFromTo(nearMark->position + 1, INT_MAX, MT_ALL, 0xFF);
            return;
        }
        break;
    }
    default:
        esyslog("cMarkAdStandalone::AddMarkVPS(): invalid mark type %d", type);
        return;
    }

    char *timeText = nullptr;
    char *indexToHMSF = marks.IndexToHMSF(vpsFrame, AV_NOPTS_VALUE, false);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF) + 1, "indexToHMSF");
    }
    dsyslog("cMarkAdStandalone::AddMarkVPS(): apply VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, (indexToHMSF) ? indexToHMSF : "unknown");
    if (indexToHMSF) {
        FREE(strlen(indexToHMSF) + 1, "indexToHMSF");
        free(indexToHMSF);
    }
    mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    if (!mark) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found no mark found to replace");
        return;
    }
    if ( (type & 0x0F) != (mark->type & 0x0F)) return;

    timeText = marks.GetTime(mark);
    if (timeText) {
        int diff = (mark->position - vpsFrame) / decoder->GetVideoFrameRate();
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
            marks.Move(mark, vpsFrame, index->GetPTSAfterKeyPacketNumber(vpsFrame), (type == MT_START) ? MT_VPSSTART : MT_VPSSTOP);
        }
        else dsyslog("cMarkAdStandalone::AddMarkVPS(): VPS event too far from mark, ignoring");
    }
}


void cMarkAdStandalone::AddMark(sMarkAdMark *mark) {
    if (!mark) return;
    if (mark->type <= MT_UNDEFINED) {
        esyslog("cMarkAdStandalone::AddMark(): mark type 0x%X invalid", mark->type);
        return;
    }
    if (mark->position < 0) {
        char *markType = marks.TypeToText(mark->type);
        esyslog("cMarkAdStandalone::AddMark(): mark packet number (%d) invalid, type 0x%X <%s>", mark->position, mark->type, markType);
        FREE(strlen(markType)+1, "text");
        free(markType);
        return;
    }
    if (mark->framePTS == AV_NOPTS_VALUE) {
        char *markType = marks.TypeToText(mark->type);
        esyslog("cMarkAdStandalone::AddMark(): mark packet number (%d), type 0x%X <%s %s>: no frame frame PTS %" PRId64, mark->position, mark->type, markType, ((mark->type & 0x0F) == MT_START)? "start" : "stop", mark->framePTS);
        FREE(strlen(markType)+1, "text");
        free(markType);
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
                criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN, macontext.Config->fullDecode);
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
            criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN, macontext.Config->fullDecode);
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
        if ((mark->position > packetCheckStart) && (mark->position < stopA * 2 / 3) && (marks.Count(MT_CHANNELSTART, 0xFF) == 0)) {
            dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after packetCheckStart, disable video decoding detection now");
            // disable all video detection
            video->ClearBorder();
            // use now channel change for detection
            criteria->SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED, macontext.Config->fullDecode);
            if (criteria->GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
                criteria->SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE, macontext.Config->fullDecode);
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
        sceneMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, mark->framePTS, nullptr, inBroadCast);
        if (comment) {
#ifdef DEBUG_WEAK_MARKS
            char *indexToHMSF = marks.IndexToHMSF(mark->position, mark->framePTS, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                if (indexToHMSF) {
                    dsyslog("cMarkAdStandalone::AddMark(): %s PTS %" PRId64 " at %s", comment, mark->framePTS, indexToHMSF);
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
        silenceMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, mark->framePTS, nullptr, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position, mark->framePTS, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                dsyslog("cMarkAdStandalone::AddMark(): %s PTS %" PRId64 " at %s", comment, mark->framePTS, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
        break;
    case MT_LOWERBORDERCHANGE:
    case MT_BLACKCHANGE:
        blackMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, mark->framePTS, nullptr, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position, mark->framePTS, false);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                dsyslog("cMarkAdStandalone::AddMark(): %s PTS %" PRId64 " at %s", comment, mark->framePTS, indexToHMSF);
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
            if (asprintf(&indexToHMSF, "00:00:00.00") == -1) esyslog("cMarkAdStandalone::AddMark(): asprintf failed");  // we have no index to get time for packet number 0
        }
        else indexToHMSF = marks.IndexToHMSF(mark->position, mark->framePTS, false);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            if (comment) isyslog("%s PTS %" PRId64 " at %s", comment, mark->framePTS, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        marks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, mark->framePTS, comment, inBroadCast);
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
    if (decoder) framecnt = decoder->GetPacketNumber();
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

    StartSection("debug mark pictures");
    dsyslog("cMarkAdStandalone::DebugMarkFrames(): final marks:");
    marks.Debug();

    decoder->Restart();   // restart decoder with first frame
    cMark *mark = marks.GetFirst();
    if (!mark) return;

    // if no fullDecode, check if all marks are on key packet position
    if (!macontext.Config->fullDecode) {
        while (mark) {
            if (abortNow) return;
            if (mark->position != index->GetKeyPacketNumberBefore(mark->position)) esyslog("cMarkAdStandalone::DebugMarkFrames(): mark at (%d) type 0x%X is not a key packet position", mark->position, mark->type);
            mark=mark->Next();
        }
    }

    // read and decode mark video frames
    mark = marks.GetFirst();
    int keyPacketBefore = index->GetKeyPacketNumberBeforePTS(mark->pts - 1);
    if (!macontext.Config->fullDecode) {
        for (int i = 1; i < DEBUG_MARK_FRAMES; i++) keyPacketBefore = index->GetKeyPacketNumberBefore(keyPacketBefore - 1);  // go back DEBUG_MARK_FRAMES key packets
    }
    if (!decoder->SeekToPacket(keyPacketBefore)) {
        esyslog("cMarkAdStandalone::DebugMarkFrames(): seek to packet (%d) failed", keyPacketBefore);
        return;
    }
    int64_t startPTS = mark->pts - (decoder->GetPacketDuration() * DEBUG_MARK_FRAMES);
    if (!macontext.Config->fullDecode) startPTS = decoder->GetPacketPTS();  //  without full decode use PTS of DEBUG_MARK_FRAMES back
    int countAfterStop = 0;

    while (decoder->DecodeNextFrame(false)) {  // no audio
        if (abortNow) return;
        int packetNumber = decoder->GetPacketNumber();
        int64_t framePTS = decoder->GetFramePTS();
        if (framePTS >= startPTS) {
            dsyslog("cMarkAdStandalone::DebugMarkFrames(): mark (%5d) type 0x%X, PTS %" PRId64 ": read packet (%5d), got frame PTS %" PRId64, mark->position, mark->type, mark->pts, packetNumber, framePTS);
            char suffix1[10] = "";
            char suffix2[10] = "";
            if ((mark->type & 0x0F) == MT_START) strcpy(suffix1, "START");
            if ((mark->type & 0x0F) == MT_STOP)  strcpy(suffix1, "STOP");

            if (framePTS < mark->pts) strcpy(suffix2, "BEFORE");
            if ((macontext.Config->fullDecode)  && (framePTS > mark->pts)) strcpy(suffix2, "AFTER");
            if ((!macontext.Config->fullDecode) && (framePTS > mark->pts + 1)) strcpy(suffix2, "AFTER");  // for interlaced stream we will get the picture after the iFrame
            const sVideoPicture *picture = decoder->GetVideoPicture();
            if (picture) {
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d_%s_%s.pgm", macontext.Config->recDir, packetNumber, suffix1, suffix2) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    SaveVideoPlane0(fileName, picture);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                }
            }
            else dsyslog("cMarkAdStandalone::DebugMarkFrames(): packet (%d): picture not valid", packetNumber);
            if (framePTS > mark->pts) countAfterStop++;
            if (countAfterStop >= DEBUG_MARK_FRAMES) {
                countAfterStop = 0;
                mark = mark->Next();
                if (!mark) break;
                // go to next start mark
                keyPacketBefore = index->GetKeyPacketNumberBeforePTS(mark->pts - 1);
                if (!macontext.Config->fullDecode) {
                    for (int i = 1; i < DEBUG_MARK_FRAMES; i++) keyPacketBefore = index->GetKeyPacketNumberBefore(keyPacketBefore - 1);  // go back DEBUG_MARK_FRAMES key packets
                }
                if (!decoder->SeekToPacket(keyPacketBefore)) {
                    esyslog("cMarkAdStandalone::DebugMarkFrames(): seek to packet (%d) failed", keyPacketBefore);
                    return;
                }
                startPTS = mark->pts - (decoder->GetPacketDuration() * DEBUG_MARK_FRAMES);
                if (!macontext.Config->fullDecode) startPTS = decoder->GetPacketPTS();  //  without full decode use PTS of DEBUG_MARK_FRAMES back
            }
        }
        else decoder->DropFrame();   // clenup frame buffer
    }
    elapsedTime.markPictures = EndSection("debug mark pictures");
}
#endif


void cMarkAdStandalone::MarkadCut() {
    if (!decoder) {
        esyslog("cMarkAdStandalone::MarkadCut(): decoder not set");
        return;
    }
    StartSection("cut");
    dsyslog("cMarkAdStandalone::MarkadCut(): cut video based on marks: fullDecode = %d, fullEncode = %d, ac3ReEncode = %d", macontext.Config->fullDecode, macontext.Config->fullEncode,  macontext.Config->ac3ReEncode);

    if (macontext.Config->fullEncode && !macontext.Config->fullDecode) {
        esyslog("full encode needs full decode, disable full encode");
        macontext.Config->fullEncode = false;
    }

    if (marks.Count() < 2) {
        esyslog("need at least one start mark and one stop mark to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): final marks are:");
    DebugMarks();     //  only for debugging

    // init encoder
    int cutMode = CUT_MODE_KEY;
    if (macontext.Config->smartEncode)     cutMode = CUT_MODE_SMART;
    else if (macontext.Config->fullEncode) cutMode = CUT_MODE_FULL;
    cEncoder *encoder = new cEncoder(decoder, index, macontext.Config->recDir, cutMode, macontext.Config->bestEncode, macontext.Config->ac3ReEncode);
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
            FREE(sizeof(*encoder), "encoder");
            delete encoder;  // encoder must be valid here because it is used above
            return;
        }
        cMark *stopMark = startMark->Next();
        if ((stopMark->type & 0x0F) != MT_STOP) {
            esyslog("got invalid stop mark at (%d) type 0x%X", stopMark->position, stopMark->type);
            FREE(sizeof(*encoder), "encoder");
            delete encoder;  // encoder must be valid here because it is used above
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
            if (!encoder->CutOut(startMark, stopMark)) break;

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
    elapsedTime.cut = EndSection("cut");
}


// logo mark optimization
// do it with all mark types, because even with channel marks from a double episode, logo marks can be the only valid end mark type
// - move logo marks before intrudiction logo
// - move logo marks before/after ad in frame
// - remove stop/start from info logo
//
void cMarkAdStandalone::LogoMarkOptimization() {
    if (!decoder)  return;
    if (!index)    return;
    if (!criteria) return;

    if (marks.Count(MT_LOGOCHANGE, 0xF0) == 0) {
        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no logo marks used");
        return;
    }

    StartSection("mark optimization");
    if (!evaluateLogoStopStartPair) {  // init in RemoveLogoChangeMarks(), but maybe not used
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(decoder, criteria);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }
    else evaluateLogoStopStartPair->SetDecoder(decoder);

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
            sMarkPos introductionStart = {-1};

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

                char *indexToHMSFSearchStart = marks.IndexToHMSF(searchStartPosition, AV_NOPTS_VALUE, false);
                if (indexToHMSFSearchStart) {
                    ALLOC(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
                }

                if (indexToHMSFStartMark && indexToHMSFSearchStart) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search introduction logo from position (%d) at %s to logo start mark (%d) at %s", searchStartPosition, indexToHMSFSearchStart, markLogo->position, indexToHMSFStartMark);
                if (indexToHMSFSearchStart) {
                    FREE(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
                    free(indexToHMSFSearchStart);
                }
                if (detectLogoStopStart->Detect(searchStartPosition, markLogo->position)) {
                    detectLogoStopStart->IntroductionLogo(searchStartPosition, markLogo->position, &introductionStart);
                }
                if (introductionStart.position >= 0) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found introduction logo start at (%d)", introductionStart.position);
            }

            // check for advertising in frame with logo after logo start mark position
            LogSeparator(false);
            sMarkPos adInFrameEnd = {-1};
            if (criteria->IsAdInFrameWithLogoChannel() && (evaluateLogoStopStartPair->GetIsAdInFrame(-1, markLogo->position) >= STATUS_UNKNOWN)) {
                int searchEndPosition = markLogo->position + (MAX_AD_IN_FRAME * decoder->GetVideoFrameRate());
                char *indexToHMSFSearchEnd = marks.IndexToHMSF(searchEndPosition, AV_NOPTS_VALUE, false);
                if (indexToHMSFSearchEnd) {
                    ALLOC(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
                }
                if (indexToHMSFStartMark && indexToHMSFSearchEnd) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo after logo start mark (%d) at %s to position (%d) at %s", markLogo->position, indexToHMSFStartMark, searchEndPosition, indexToHMSFSearchEnd);
                if (indexToHMSFSearchEnd) {
                    FREE(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
                    free(indexToHMSFSearchEnd);
                }
                if (detectLogoStopStart->Detect(markLogo->position, searchEndPosition)) {
                    detectLogoStopStart->AdInFrameWithLogo(markLogo->position, searchEndPosition, &adInFrameEnd, true, false);
                }
                if (adInFrameEnd.position >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) PTS %" PRId64 " found", markLogo->position, adInFrameEnd.position, adInFrameEnd.pts);
                    if (evaluateLogoStopStartPair->IncludesInfoLogo(markLogo->position, adInFrameEnd.position)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        adInFrameEnd.position = -1;
                    }
                }
                if (adInFrameEnd.position != -1) {  // if we found advertising in frame, use this
                    markLogo = marks.Move(markLogo, adInFrameEnd.position, adInFrameEnd.pts, MT_NOADINFRAMESTART);
                    if (!markLogo) {
                        esyslog("cMarkAdStandalone::LogoMarkOptimization(): move mark failed");
                        break;
                    }
                    save = true;
                }
            }
            if ((adInFrameEnd.position == -1) && (introductionStart.position != -1)) {
                bool move = true;
                // check blackscreen between introduction logo start and logo start, there should be no long blackscreen, short blackscreen are from retrospect
                cMark *blackMarkStart = blackMarks.GetNext(introductionStart.position, MT_NOBLACKSTART);
                cMark *blackMarkStop  = blackMarks.GetNext(introductionStart.position, MT_NOBLACKSTOP);
                if (blackMarkStart && blackMarkStop && (blackMarkStart->position <= markLogo->position) && (blackMarkStop->position <= markLogo->position)) {
                    int innerLength = 1000 * (blackMarkStart->position - blackMarkStop->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): found black screen start (%d) and stop (%d) between introduction logo (%d) and start mark (%d), length %dms", blackMarkStop->position, blackMarkStart->position, introductionStart.position, markLogo->position, innerLength);
                    if (innerLength > 1000) move = false;  // only move if we found no long blackscreen between introduction logo and logo start
                }
                if (move) {
                    markLogo = marks.Move(markLogo, introductionStart.position, introductionStart.pts, MT_INTRODUCTIONSTART);
                }
                if (!markLogo) {
                    esyslog("cMarkAdStandalone::LogoMarkOptimization(): move mark failed");
                    break;
                }
                save = true;
            }
        }
        if ((markLogo->type == MT_LOGOSTOP) && criteria->IsAdInFrameWithLogoChannel()) {
            // check for advertising in frame with logo before logo stop mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (45 * decoder->GetVideoFrameRate()); // advertising in frame are usually 30s, changed from 35 to 45
            // sometimes there is a closing credit in frame with logo before
            const char *indexToHMSFStopMark = marks.GetTime(markLogo);
            char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition, AV_NOPTS_VALUE, false);
            if (indexToHMSFSearchPosition) {
                ALLOC(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
            }

            if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo from frame (%d) at %s to logo stop mark (%d) at %s", searchStartPosition, indexToHMSFSearchPosition, markLogo->position, indexToHMSFStopMark);
            if (indexToHMSFSearchPosition) {
                FREE(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
                free(indexToHMSFSearchPosition);
            }
            // short start/stop pair can result in overlapping checks
            if (decoder->GetPacketNumber() > searchStartPosition) {
                dsyslog("cMarkAdStandalone::LogoMarkOptimization(): current framenumber (%d) greater than framenumber to seek (%d), restart decoder", decoder->GetPacketNumber(), searchStartPosition);
                decoder->Restart();
            }
            // detect frames
            if ((evaluateLogoStopStartPair->GetIsAdInFrame(markLogo->position, -1) >= STATUS_UNKNOWN) && (detectLogoStopStart->Detect(searchStartPosition, markLogo->position))) {
                bool isEndMark = false;
                if (markLogo->position == marks.GetLast()->position) isEndMark = true;
                sMarkPos newStop = {-1};
                detectLogoStopStart->AdInFrameWithLogo(searchStartPosition, markLogo->position, &newStop, false, isEndMark);
                if (newStop.position >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) PTS %" PRId64 " and (%d) found", newStop.position, newStop.pts, markLogo->position);
                    if (evaluateLogoStopStartPair->IncludesInfoLogo(newStop.position, markLogo->position)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        newStop.position = -1;
                    }
                }
                if (newStop.position != -1) {
                    evaluateLogoStopStartPair->AddAdInFrame(newStop.position, markLogo->position);  // store info that we found here adinframe
                    markLogo = marks.Move(markLogo, newStop.position, newStop.pts, MT_NOADINFRAMESTOP);
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
    elapsedTime.markOptimization = EndSection("mark optimization");
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
            cMark *startBlackBefore  = nullptr;
            cMark *startBlackAfter   = nullptr;
            cMark *stopBlackBefore   = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART); // new part starts after the black screen
            cMark *stopBlackAfter    = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTART); // new part starts after the black screen
            if (stopBlackBefore) {
                startBlackBefore = blackMarks.GetPrev(stopBlackBefore->position, MT_NOBLACKSTOP);
                if (startBlackBefore) {
                    diffBefore   = 1000 * (mark->position            - stopBlackBefore->position)  / decoder->GetVideoFrameRate();
                    lengthBefore = 1000 * (stopBlackBefore->position - startBlackBefore->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    const cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), startBlackBefore->position, MT_SOUNDCHANGE, 0xF0);         // around black screen start
                    if (!silence) silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopBlackBefore->position, MT_SOUNDCHANGE, 0xF0); // around black screen stop
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms before -> length %5dms, silence around %d", mark->position, startBlackBefore->position, stopBlackBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else stopBlackBefore = nullptr; // no pair, this is invalid
            }
            if (stopBlackAfter) {
                startBlackAfter = blackMarks.GetPrev(stopBlackAfter->position, MT_NOBLACKSTOP);
                if (startBlackAfter) {
                    diffAfter   = 1000 * (stopBlackAfter->position - mark->position)      / decoder->GetVideoFrameRate();
                    lengthAfter = 1000 * (stopBlackAfter->position - startBlackAfter->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    const cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), startBlackAfter->position, MT_SOUNDCHANGE, 0xF0);         // around black screen start
                    if (!silence) silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopBlackAfter->position, MT_SOUNDCHANGE, 0xF0); // around black screen stop
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, startBlackAfter->position, stopBlackAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else stopBlackAfter = nullptr; // no pair, this is invalid
            }
            // try black screen before start mark
            if (stopBlackBefore) {
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

                    if (criteria->LogoFadeInOut() & FADE_IN) {
                        if (silenceBefore) {
                            if (lengthBefore      >= 160) maxBefore = 6639;
                            else if (lengthBefore >=  40) maxBefore = 5519;
                        }
                        else if (lengthBefore < 2960) maxBefore = 5360;  // long blackscreen is from closing credits of previous broadcast
                    }
                    else maxBefore = 2999;
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
                        else if (silenceBefore && silenceAfter && (diffBefore >= 26660) && (diffAfter <= 44100)) diffBefore = INT_MAX;

                        // rule 3: not so far blackscreen after is start of broadcast, far before is from closing credits, no silence around
                        else if (!silenceBefore && !silenceAfter && (diffBefore >= 49920) && (diffAfter <= 20960)) diffBefore = INT_MAX;

                        if (criteria->GoodVPS())      maxBefore =  7419;
                        else if (silenceBefore)       maxBefore = 81800;
                        else if (lengthBefore >= 600) maxBefore = 88400;
                        else if (lengthBefore >= 240) maxBefore = 50480;
                        else                          maxBefore =     0;  // do not accept short black screen, too much false positiv
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {  // move even to same position to prevent scene change do a move
                    // start broadcast with some black picture, prevent to use closing credits before, changed from 9720 to 7100
                    if ((mark->position == marks.First()->position) && (lengthBefore < 7100))
                        mark = marks.Move(mark, startBlackBefore->position, startBlackBefore->pts, MT_NOBLACKSTART);
                    else mark = marks.Move(mark, stopBlackBefore->position,  stopBlackBefore->pts,  MT_NOBLACKSTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else break;
                }
            }
            // try black screen after start mark
            if (!moved && stopBlackAfter) { // move even to same position to prevent scene change do a move
                int maxAfter = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    if (lengthAfter > 40) maxAfter = 64519;
                    else maxAfter = 34159;
                    break;
                case MT_LOGOSTART:
                    if (criteria->LogoFadeInOut() & FADE_IN) maxAfter = 1440;
                    else                                     maxAfter = 2240;
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
                        else if (silenceAfter)          maxAfter = 149880;
                        else if (lengthAfter >= 3160)   maxAfter = 139240;  // black screen from separator or opening credits
                        else if (lengthAfter >    40)   maxAfter =  20960;
                        else                            maxAfter =      0;  // very short blackscreen are in broadcast
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
                    if (mark->position == marks.First()->position) mark = marks.Move(mark, startBlackAfter->position, startBlackAfter->pts, MT_NOBLACKSTART);  // start broadcast with some black picture
                    else                                           mark = marks.Move(mark, stopBlackAfter->position,  stopBlackAfter->pts,  MT_NOBLACKSTART);
                    if (mark) save = true;
                    else break;
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available stop marks
            bool  moved             = false;
            long int diffBefore     = INT_MAX;
            int   diffAfter         = INT_MAX;
            cMark *stopBlackAfter   = nullptr;
            cMark *startBlackAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
            cMark *blackStartBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP);
            cMark *blackStopBefore  = nullptr;
            bool  silenceAfter      = false;
            if (blackStartBefore) {
                diffBefore = 1000 * (mark->position - blackStartBefore->position) / decoder->GetVideoFrameRate();
                blackStopBefore = blackMarks.GetNext(blackStartBefore->position, MT_NOBLACKSTART);
                if (blackStopBefore) {
                    lengthBefore = 1000 * (blackStopBefore->position - blackStartBefore->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    bool  silenceBefore = false;
                    const cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), blackStartBefore->position, MT_SOUNDCHANGE, 0xF0);
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7ldms before -> length %5dms, silence around %d", mark->position, blackStartBefore->position, blackStopBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else blackStartBefore = nullptr; // no pair, this is invalid
            }
            if (startBlackAfter) {
                diffAfter = 1000 * (startBlackAfter->position - mark->position) / decoder->GetVideoFrameRate();
                stopBlackAfter = blackMarks.GetNext(startBlackAfter->position, MT_NOBLACKSTART);
                if (stopBlackAfter) {
                    lengthAfter = 1000 * (stopBlackAfter->position - startBlackAfter->position) / decoder->GetVideoFrameRate();
                    // check if there is silence around black screen
                    const cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), startBlackAfter->position, MT_SOUNDCHANGE, 0xF0);
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, startBlackAfter->position, stopBlackAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else startBlackAfter = nullptr; // no pair, this is invalid
            }

            // try black screen after stop marks
            if (startBlackAfter) {  // move even to same position to prevent scene change for move again
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
                    // rule 1: black screen before from before logo stop to after logo stop (black screen around logo stop)
                    if ((lengthBefore >= diffBefore)) diffAfter = INT_MAX;

                    // rule 2: long black screen at end of broadcast, short black screen after preview
                    else if ((!(criteria->LogoFadeInOut() & FADE_OUT)) &&
                             (diffBefore <= 4580) && (lengthBefore >= 600) && (diffAfter <= 3240) && (lengthAfter <= 520)) diffAfter = INT_MAX;

                    if (criteria->LogoFadeInOut() & FADE_OUT) {
                        if (lengthAfter > 40) maxAfter = 4960;
                        else                  maxAfter = 3039;
                    }
                    else                      maxAfter = 1399;
                    break;
                case MT_HBORDERSTOP:
                    // rule 1: black screen short before hborder stop is end of closing credits
                    if ((diffBefore <= 360) && (diffAfter >= 7000)) diffAfter = INT_MAX;

                    maxAfter = 0;
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
                        // rule 1: near black screen before is end mark, far after is from preview or in next broadcast
                        if ((diffBefore <= 6360) && (diffAfter >= 8500)) diffAfter = INT_MAX;

                        if (criteria->GoodVPS())    maxAfter = 12779;
                        else if (silenceAfter)      maxAfter = 82920;
                        else if (lengthAfter >= 80) maxAfter = 54600;
                        else                        maxAfter = 11560;
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        if (lengthAfter < 6240) maxAfter = 11040;  // long blackscreen after closing credits are opening credits from next broadcast
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
                    if (mark->position == marks.GetLast()->position) mark = marks.Move(mark, stopBlackAfter->position,  stopBlackAfter->pts,  MT_NOBLACKSTOP);  // allow some black pictures at end of broadcast
                    else                                             mark = marks.Move(mark, startBlackAfter->position, startBlackAfter->pts, MT_NOBLACKSTOP);
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
                        if (lengthBefore > diffBefore) maxBefore =   840;   // logo fade around black screen
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
                        maxBefore = 6360;
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxBefore = 9519;
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
                    if (mark->position == marks.GetLast()->position) mark = marks.Move(mark, blackStopBefore->position,  blackStopBefore->pts,  MT_NOBLACKSTOP);
                    else                                             mark = marks.Move(mark, blackStartBefore->position, blackStartBefore->pts, MT_NOBLACKSTOP);
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
        if (!((((mark->type & 0xF0) == MT_MOVED) && ((mark->newType & 0xF0) == MT_VPSCHANGE)) ||
                ((mark->type & 0xF0) == MT_ASSUMED))) { // skip mark if not VPS or ASSUMED
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
            while (startBefore) {
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOLOWERBORDERSTART);
                if (!stopBefore) break;
                diffBefore = 1000 * (mark->position - startBefore->position) / decoder->GetVideoFrameRate();
                lengthBefore = 1000 * (stopBefore->position - startBefore->position) / decoder->GetVideoFrameRate();
                dsyslog("cMarkAdStandalone::LowerBorderOptimization(): start mark (%6d): lower border from (%6d) to (%6d), %7dms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                if ((lengthBefore >= MIN_LOWER_BORDER) && (lengthBefore <= MAX_LOWER_BORDER)) break;
                startBefore = blackMarks.GetPrev(startBefore->position, MT_NOLOWERBORDERSTOP);  // previous start of lower border
            }
            if ((lengthBefore < MIN_LOWER_BORDER) || (lengthBefore > MAX_LOWER_BORDER)) { // we got no valid result
                diffBefore  = INT_MAX;
                startBefore = nullptr;
                stopBefore  = nullptr;
            }
            if (startBefore && stopBefore) {   // we found valid lower border start/stop
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOLOWERBORDERSTART);  // end   of lower border before logo start mark
                if (stopBefore) {
                    diffBefore = 1000 * (mark->position - startBefore->position) / decoder->GetVideoFrameRate();
                    lengthBefore = 1000 * (stopBefore->position - startBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::LowerBorderOptimization(): start mark (%6d): lower border from (%6d) to (%6d), %7dms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                }
                else startBefore = nullptr; // no pair, this is invalid
            }
            if (startBefore && stopBefore) {   // we found valid lower border start/stop
                cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopBefore->position, MT_SOUNDCHANGE, 0xF0);  // around lower border stop
                if (silence) dsyslog("cMarkAdStandalone::LowerBorderOptimization(): silence found (%d) around lower border from (%d) to (%d)", silence->position, startBefore->position, stopBefore->position);
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
            if (startAfter && stopAfter) {   // we found valid lower border start/stop
                cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopAfter->position, MT_SOUNDCHANGE, 0xF0);  // around lower border stop
                if (silence) dsyslog("cMarkAdStandalone::LowerBorderOptimization(): silence found (%d) around lower border from (%d) to (%d)", silence->position, startAfter->position, stopAfter->position);
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
                            else                     maxBefore = 230039;
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
                    mark = marks.Move(mark, stopBefore->position, stopBefore->pts, MT_NOLOWERBORDERSTART);  // move to end of lower border (closing credits)
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
                        else                     maxAfter = 302800;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {
                    mark = marks.Move(mark, stopAfter->position, stopAfter->pts, MT_NOLOWERBORDERSTART);  // use end of lower closing credits
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
            long int diffBefore = LONG_MAX;
            cMark *startBefore  = blackMarks.GetPrev(mark->position + 1, MT_NOLOWERBORDERSTOP);
            cMark *stopBefore   = nullptr;
            bool silenceBefore  = false;
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
                diffBefore  = LONG_MAX;
                startBefore = nullptr;
                stopBefore  = nullptr;
            }
            if (startBefore && stopBefore) {   // we found valid lower border start/stop
                cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopBefore->position, MT_SOUNDCHANGE, 0xF0);  // around lower border stop
                if (silence) {
                    dsyslog("cMarkAdStandalone::LowerBorderOptimization(): silence found (%d) around lower border from (%d) to (%d)", silence->position, startBefore->position, stopBefore->position);
                    silenceBefore = true;
                }
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
                diffAfter  = INT_MAX;
                startAfter = nullptr;
                stopAfter  = nullptr;
            }
            if (startAfter && stopAfter) {   // we found valid lower border start/stop
                cMark *silence = silenceMarks.GetAround(decoder->GetVideoFrameRate(), stopAfter->position, MT_SOUNDCHANGE, 0xF0);  // around lower border stop
                if (silence) dsyslog("cMarkAdStandalone::LowerBorderOptimization(): silence found (%d) around lower border from (%d) to (%d)", silence->position, startAfter->position, stopAfter->position);
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
                    mark = marks.Move(mark, stopAfter->position, stopAfter->pts, MT_NOLOWERBORDERSTOP);
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
                    if (silenceBefore) maxBefore = 218520;
                    else               maxBefore =  98599;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (criteria->GoodVPS())                                                             maxBefore =   6579;
                        else if ((diffAfter == INT_MAX) && (lengthBefore >= 1920) && (lengthBefore <= 4920)) maxBefore = 198960;  // no lower border after, typical length
                        else                                                                                 maxBefore =  60559;
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, stopBefore->position, stopBefore->pts, MT_NOLOWERBORDERSTOP);
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
            int lengthBefore = 0;
            int lengthAfter  = 0;
            bool blackBefore = false;
            bool blackAfter  = false;
            cMark *soundStartBefore = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTART);
            cMark *soundStopBefore  = nullptr;
            cMark *soundStartAfter  = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTART);
            cMark *soundStopAfter   = nullptr;
            if (soundStartBefore) {
                diffBefore = 1000 * (mark->position - soundStartBefore->position) / decoder->GetVideoFrameRate();
                soundStopBefore = silenceMarks.GetPrev(soundStartBefore->position, MT_SOUNDSTOP);
                if (soundStopBefore) {
                    lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopBefore->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackBefore = true;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): silence from (%6d) %10" PRId64 " to (%6d) %10" PRId64 ", %8dms before, length %4dms, black %d", mark->position, soundStopBefore->position, soundStopBefore->pts, soundStartBefore->position, soundStartBefore->pts, diffBefore, lengthBefore, blackBefore);
                }
            }
            if (soundStartAfter) {
                diffAfter = 1000 * (soundStartAfter->position - mark->position) / decoder->GetVideoFrameRate();
                soundStopAfter  = silenceMarks.GetPrev(soundStartAfter->position, MT_SOUNDSTOP);
                if (soundStopAfter) {
                    lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / decoder->GetVideoFrameRate();
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopAfter->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackAfter = true;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): silence from (%6d) %10" PRId64 " to (%6d) %10" PRId64 ", %8dms after,  length %4dms, black %d", mark->position, soundStopAfter->position, soundStopAfter->pts, soundStartAfter->position, soundStartAfter->pts, diffAfter, lengthAfter, blackAfter);
                }
            }
            // check if new position can be valid
            if (soundStartAfter) {
                cMark *nextStop = marks.GetNext(soundStartAfter->position, MT_STOP, 0x0F);
                if (nextStop) {
                    int diff = (nextStop->position - soundStartAfter->position) / decoder->GetVideoFrameRate();
                    if (diff < 60) { // min length broadcast after move to silence
                        dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): silence after (%d) is only %ds before next stop mark (%d), ignore invalid", mark->position, soundStartAfter->position, diff, nextStop->position);
                        soundStartAfter = nullptr;
                    }
                }
            }
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

                    if (criteria->LogoFadeInOut() & FADE_IN) {
                        if ((lengthBefore >= 880) && blackBefore) maxBefore = 10720;
                        else if (lengthBefore > 120)              maxBefore =  5399;
                        else                                      maxBefore =  3799;
                    }
                    else                                          maxBefore =  2480;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if (criteria->GoodVPS()) maxBefore =  24399;
                        else                     maxBefore = 136559;
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
                    mark = marks.Move(mark, soundStartBefore->position, soundStartBefore->pts, MT_SOUNDSTART);
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
                    maxAfter = 720;    // silence after logo start only if logo start before broadcast start
                    break;
                case MT_VBORDERSTART:
                    if (mark->position == marks.GetFirst()->position) maxAfter = 359;
                    else                                              maxAfter =   0;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        if (criteria->GoodVPS())    maxAfter = 116959;
                        else if (blackAfter)        maxAfter = 233440;
                        else if (lengthAfter > 120) maxAfter = 141839;
                        else                        maxAfter =  17019;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if ((diffAfter <= maxAfter) && (soundStartAfter->position != mark->position)) {
                    mark = marks.Move(mark, soundStartAfter->position, soundStartAfter->pts, MT_SOUNDSTART);
                    if (mark) save = true;
                    else break;
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available marks
            bool moved = false;
            long int diffBefore     = INT_MAX;
            int diffAfter           = INT_MAX;
            cMark *soundStopBefore  = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            cMark *soundStartBefore = nullptr;
            cMark *soundStopAfter   = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            cMark *soundStartAfter  = nullptr;
            int lengthBefore        = 0;
            int lengthAfter         = 0;
            if (soundStopBefore) {
                diffBefore = 1000 * (mark->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                soundStartBefore = silenceMarks.GetNext(soundStopBefore->position, MT_SOUNDSTART);
                if (soundStartBefore) {
                    bool blackBefore = false;
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopBefore->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackBefore = true;
                    lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): silence from (%6d) %10" PRId64 " to (%6d) %10" PRId64 ", %8ldms before, length %4dms, black %d", mark->position, soundStopBefore->position, soundStopBefore->pts, soundStartBefore->position, soundStartBefore->pts, diffBefore, lengthBefore, blackBefore);
                }
            }
            if (soundStopAfter) {
                diffAfter = 1000 * (soundStopAfter->position - mark->position) / decoder->GetVideoFrameRate();
                soundStartAfter = silenceMarks.GetNext(soundStopAfter->position, MT_SOUNDSTART);
                if (soundStartAfter) {
                    bool blackAfter = false;
                    const cMark *black = blackMarks.GetAround(decoder->GetVideoFrameRate(), soundStopAfter->position, MT_BLACKCHANGE, 0xF0);
                    if (black) blackAfter = true;
                    lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / decoder->GetVideoFrameRate();
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): silence from (%6d) %10" PRId64 " to (%6d) %10" PRId64 ", %8dms after,  length %4dms, black %d", mark->position, soundStopAfter->position, soundStopAfter->pts, soundStartAfter->position, soundStartAfter->pts, diffAfter, lengthAfter, blackAfter);
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
                    if (criteria->LogoFadeInOut() & FADE_OUT) maxAfter = 2360;
                    else                                      maxAfter = 1079;
                    break;
                case MT_VBORDERSTOP:
                    maxAfter = 0;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (criteria->GoodVPS())      maxAfter = 13479;
                        else if (lengthAfter >= 3000) maxAfter = 81920;
                        else if (lengthAfter >    40) maxAfter = 49959;   // ignore very short silence
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if (diffAfter <= maxAfter) {
                    mark = marks.Move(mark, soundStopAfter->position, soundStopAfter->pts, MT_SOUNDSTOP);
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
                    if (criteria->LogoFadeInOut() & FADE_OUT) maxBefore =  520;
                    else if (lengthBefore > 40)               maxBefore = 4440;
                    else                                      maxBefore = 3439;
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
                        if (criteria->GoodVPS()) maxBefore = 19980;
                        else                     maxBefore = 57280;
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
                    mark = marks.Move(mark, soundStopBefore->position, soundStopBefore->pts, MT_SOUNDSTOP);
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
                    maxBefore = 7920;
                    break;
                case MT_LOGOSTART:
                    // rule 1: logo start very short before broadcast start
                    if (!(criteria->LogoFadeInOut() & FADE_IN) && (diffBefore >= 320) && (diffAfter <= 120)) diffBefore = INT_MAX;

                    // rule 2: scene start very short after logo start in old recording without fade in of fade in channel
                    else if ((criteria->LogoFadeInOut() & FADE_IN) && (diffBefore >= 1700) && (diffAfter <= 20)) diffBefore = INT_MAX;

                    if (criteria->LogoFadeInOut() & FADE_IN) maxBefore = 6059;
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
                        if (diffAfter <= 120) diffBefore = INT_MAX;

                        // rule 2: silence in closing scene
                        else if ((diffBefore >= 22200) && (diffAfter >= 7120)) diffBefore = INT_MAX;

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
                    mark = marks.Move(mark, sceneStartBefore->position, sceneStartBefore->pts, MT_SCENESTART);
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
                    if (criteria->LogoFadeInOut() & FADE_IN) maxAfter =   0; // with fade in logo, scene after is always false
                    else if (diffBefore >= 45400)            maxAfter = 460; // logo start before broadcast start, long scene before
                    else                                     maxAfter =  80; // some channels starts logo short before broadcast
                    break;
                case MT_ASPECTSTART:
                    maxAfter = 400;
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
                        maxAfter = 4619;
                        break;
                    case MT_NOLOWERBORDERSTART:
                        maxAfter = 5000;
                        break;
                    case MT_NOADINFRAMESTART:
                        maxAfter = 40;
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
                    mark = marks.Move(mark, sceneStartAfter->position, sceneStartAfter->pts, MT_SCENESTART);
                    if (mark) save = true;
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
                    if (!(criteria->LogoFadeInOut() & FADE_OUT) && ((diffBefore <= 1720) || (diffAfter >= 760))) diffAfter = INT_MAX;

                    // rule 2: very short scene change before logo stop is valid
                    if (diffBefore <= 40) diffAfter = INT_MAX;

                    maxAfter = 4800;
                    break;
                case MT_HBORDERSTOP:
                    if ((diffBefore <= 440) && (diffAfter >= 1720)) diffAfter = INT_MAX;
                    maxAfter = 799;
                    break;
                case MT_VBORDERSTOP:
                    if (diffBefore > 20) maxAfter = 20720;
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
                        // rule1: use scene change short before silence (silence is at start of ad / separator / closing credits)
                        if (diffBefore <= 160) diffAfter = INT_MAX;

                        maxAfter = 360;
                        break;
                    case MT_VPSSTOP:
                        // rule 1: use nearest scene change for channel with good VPS
                        if (criteria->GoodVPS() && (diffBefore < diffAfter))  diffAfter = INT_MAX;

                        // rule 2: scene change short before
                        if ((diffBefore >= 160) && (diffBefore <= 480) && (diffAfter >= 680) && (diffAfter <= 3840)) diffAfter = INT_MAX;

                        // rule 3: long opening scene from next broadcast
                        else if ((diffBefore <= 80) && (diffAfter >= 6720)) diffAfter = INT_MAX;   // long opening scene from next broadcast

                        maxAfter = 17480;  // changed from 11200 to 17480
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxAfter = 1080;
                        break;
                    case MT_NOADINFRAMESTOP:
                        maxAfter = 0;
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if (diffAfter <= maxAfter) {  // logo is fading out before end of broadcast scene, move forward
                    mark = marks.Move(mark, sceneStopAfter->position, sceneStopAfter->pts, MT_SCENESTOP);
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
                case MT_ASPECTSTOP:
                    maxBefore = 200;
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 640;  // changed from 600 to 640
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_NOLOWERBORDERSTOP:
                        maxBefore = 160;
                        break;
                    case MT_SOUNDSTOP:
                        maxBefore = 579;
                        break;
                    case MT_VPSSTOP:
                        maxBefore = 8440;   // chaned from 1320 to 1440 to 8440
                        break;
                    case MT_NOADINFRAMESTOP:  // correct the missed start of ad in frame before stop mark
                        maxBefore = 440;
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, sceneStopBefore->position, sceneStopBefore->pts, MT_SCENESTOP);
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


void cMarkAdStandalone::ProcessOverlap() {
    if (abortNow)      return;
    if (duplicate)     return;
    if (!decoder)      return;
    if ((length == 0) || (startTime == 0)) {  // no recording length or start time from info file
        return;
    }
    StartSection("overlap detection");
    bool save = false;

    // overlap detection
    DebugMarks();     //  only for debugging
    cOverlap *overlap = new cOverlap(decoder, index);
    ALLOC(sizeof(*overlap), "overlap");
    save = overlap->DetectOverlap(&marks);
    FREE(sizeof(*overlap), "overlap");
    delete overlap;
    elapsedTime.overlap = EndSection("overlap detection");

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
    return;
}


bool cMarkAdStandalone::ProcessFrame() {
    int frameNumber = decoder->GetPacketNumber();
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
            // for performance reason only analyse i-frames if markad was called without full decoding and forced to full decoding because of codec
            if (!macontext.Config->forcedFullDecode || decoder->IsVideoIFrame()) {
                sMarkAdMarks *vmarks = video->Process();
                if (vmarks) {
                    for (int i = 0; i < vmarks->Count; i++) {
                        AddMark(&vmarks->Number[i]);
                    }
                }
            }
            else decoder->DropFrame();
        }

        // check start
        if (!doneCheckStart && inBroadCast && (frameNumber > packetCheckStart)) CheckStart();

        // check stop
        if (!doneCheckStop && (frameNumber > packetCheckStop)) {
            if (!doneCheckStart) {
                dsyslog("cMarkAdStandalone::ProcessFrame(): assumed end reached but still no CheckStart() called, do it now");
                CheckStart();
            }
            CheckStop();
            return false;   // return false to signal end of file processing
        }
    }

    // detect audio channel based marks
    if ((!macontext.Config->fullDecode ||                     // if we decode only i-frames, we will have no audio frames
            decoder->IsAudioPacket() ||
            !criteria->GetDetectionState(MT_SOUNDCHANGE)) &&  // without silence detection, we do not decode audio and will get no audio frames
            criteria->GetDetectionState(MT_AUDIO)) {
        sMarkAdMarks *amarks = audio->Detect();               // detect channel change and silence
        if (amarks) {
            for (int i = 0; i < amarks->Count; i++) AddMark(&amarks->Number[i]);
        }
    }

    // turn on all detection for end part even if we use stronger marks, just in case we will get no strong end mark
    if (!restartLogoDetectionDone && (frameNumber >= packetEndPart)) {
        dsyslog("cMarkAdStandalone::ProcessFrame(): enter end part at frame (%d), reset detector status", frameNumber);
        video->Clear(true);
        criteria->SetDetectionState(MT_ALL, true);
        if (!macontext.Config->fullDecode) criteria->SetDetectionState(MT_SCENECHANGE, false);  // does not work without full decode
        restartLogoDetectionDone = true;
    }
    return true;
}


void cMarkAdStandalone::Recording() {
    if (abortNow) return;

    StartSection("mark detection");
    if (!macontext.Config->fullDecode) criteria->SetDetectionState(MT_SCENECHANGE, false);  // does not work without full decode
    criteria->ListDetection();
    if (macontext.Config->backupMarks) marks.Backup(directory);

    // force init decoder to get infos about video
    if (!decoder->ReadNextFile()) {
        esyslog("cMarkAdStandalone::Recording(): failed to open first video file");
        abortNow = true;
        return;
    }

    // create object to analyse video picture
    video = new cVideo(decoder, index, criteria, macontext.Config->recDir, macontext.Config->autoLogo, macontext.Config->logoCacheDirectory);
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

    CheckIndexGrowing();   // check if we have a running recording and have to wait to get new frames

    while (decoder->DecodeNextFrame(criteria->GetDetectionState(MT_SOUNDCHANGE))) {  // only decode audio if we detect silence, channel change detection needs no decoding
        if (abortNow) return;

        if (!ProcessFrame()) {   // no error, false if stopA reached
            if (abortNow) return;  // false from abort request
            break;
        }
        CheckIndexGrowing();  // check if we have a running recording and have to wait to get new frame
    }

    // we reached end of recording without CheckStart() or CheckStop() called
    if (!doneCheckStop && (decoder->GetPacketNumber() <= stopA)) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): stopA (%d)", decoder->GetPacketNumber(), stopA);
        esyslog("end of recording before recording length from VDR info file reached");
    }
    if (!doneCheckStart) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): recording ends before CheckStart() done, call it now", decoder->GetPacketNumber());
        CheckStart();
    }
    if (!doneCheckStop) {
        dsyslog("cMarkAdStandalone::Recording(): frame (%d): recording ends before CheckStop() done, call it now", decoder->GetPacketNumber());
        CheckStop();
    }

// cleanup marks that make no sense
    CheckMarks();

    if (!abortNow) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, false);
    elapsedTime.markDetection = EndSection("mark detection");
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
    // get recording start from directory name (time part)
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
                time_t dirNameTime = mktime(&t);
                dsyslog("cMarkAdStandalone::GetRecordingStart(): recording start from directory name: %s", strtok(ctime(&dirNameTime), "\n"));
                return dirNameTime;
            }
        }
    }

    // fallback get recording start from atime of directory (if the volume is mounted with noatime)
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
    else esyslog("cMarkAdStandalone::GetRecordingStart(): mount option noatime is not set");

    if ((useatime) && (stat(directory, &statbuf) != -1)) {
        if (fabs(difftime(start,statbuf.st_atime)) < 60 * 60 * 12) {  // do not believe recordings > 12h
            dsyslog("cMarkAdStandalone::GetRecordingStart(): got recording start from directory creation time");
            return statbuf.st_atime;
        }
        dsyslog("cMarkAdStandalone::GetRecordingStart(): got no valid directory creation time, maybe recording was copied %s", strtok(ctime(&statbuf.st_atime), "\n"));
        dsyslog("cMarkAdStandalone::GetRecordingStart(): broadcast start time from vdr info file                          %s", strtok(ctime(&start), "\n"));
    }

    return (time_t) 0;
}


bool cMarkAdStandalone::CheckLogo(const int frameRate) {
    if (!macontext.Config)                      return false;
    if (!*macontext.Config->logoCacheDirectory) return false;
    if (!macontext.Info.ChannelName)            return false;
    if (macontext.Config->perftest)             return false;    // nothing to do in perftest

    StartSection("initial logo search");
    bool logoFound = false;
    int len = strlen(macontext.Info.ChannelName);
    if (!len) return false;

    dsyslog("cMarkAdStandalone::CheckLogo(): using logo cache directory %s, searching logo for %s", macontext.Config->logoCacheDirectory, macontext.Info.ChannelName);
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
        if (strncmp(dirent->d_name, macontext.Info.ChannelName, len) == 0) {
            dsyslog("cMarkAdStandalone::CheckLogo(): logo found: %s", dirent->d_name);
            if ((macontext.Config->autoLogo == 0) || (macontext.Config->autoLogo == 2)) {
                closedir(dir);
                elapsedTime.logoSearch = EndSection("initial logo search");
                return true; // use only logos from cache or prefer logo from cache
            }
            logoFound = true;
        }
    }
    closedir(dir);

    if (macontext.Config->autoLogo > 0) {  // we use logo from recording directory or self extracted logo
        isyslog("search for %s %d:%d logo in recording directory %s", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->logoCacheDirectory);
        DIR *recDIR = opendir(macontext.Config->recDir);
        if (recDIR) {
            const struct dirent *direntRec = nullptr;
            while ((direntRec = readdir(recDIR))) {
                if (!strncmp(direntRec->d_name, macontext.Info.ChannelName, len)) {
                    closedir(recDIR);
                    isyslog("logo found in recording directory");
                    elapsedTime.logoSearch = EndSection("initial logo search");
                    return true;
                }
            }
            closedir(recDIR);
        }
        isyslog("no logo for %s %d:%d found in recording directory %s, trying to extract logo from recording", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->recDir);

        // only full decode if we have to because og H.264 interlaced
        extractLogo = new cExtractLogo(macontext.Config->recDir, macontext.Info.ChannelName, macontext.Config->threads, macontext.Config->forcedFullDecode, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Info.AspectRatio);
        ALLOC(sizeof(*extractLogo), "extractLogo");

        int startPos =  (macontext.Info.tStart + 2 *60) * frameRate;  // search logo from assumed start + 2 min to prevent to get logos from ad
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = extractLogo->SearchLogo(startPos, false);
        for (int retry = 2; retry <= 8; retry++) {  // do not reduce, we will not get some logos
            startPos = endpos + 10 * frameRate;     // next try 10s after end of try before, maybe false border detection in dark scene
            if (endpos > LOGO_SEARCH_FOUND) {       // no logo found, endpos is last frame of search
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
            logoFound = true;
        }
        else dsyslog("cMarkAdStandalone::CheckLogo(): logo search failed");
    }
    elapsedTime.logoSearch = EndSection("initial logo search");
    return logoFound;
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

            //  start offset of broadcast from timer event to recording start
            int startEvent = static_cast<int> (startTime - rStart);
            dsyslog("cMarkAdStandalone::LoadInfo(): event start at offset:               %5ds -> %d:%02d:%02dh", startEvent, startEvent / 3600, (startEvent % 3600) / 60, startEvent % 60);
            if (startEvent > 60 * 60) {  // assume max 1h pre-timer
                isyslog("cMarkAdStandalone::LoadInfo(): offset invald, maybe recording was copied, set to default 2min");
                startEvent = 2 * 60;
            }
            // start offset of broadcast from VPS event
            int startVPS = vps->GetStart();
            if (startVPS >= 0) {
                dsyslog("cMarkAdStandalone::LoadInfo(): VPS   start at offset:               %5ds -> %d:%02d:%02dh", startVPS, startVPS / 3600, (startVPS % 3600) / 60, startVPS % 60);
                if ((macontext.Info.ChannelName && criteria->GoodVPS()) || (abs(startVPS - startEvent) <= 10 * 60)) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): VPS start event seems to be valid");
                    macontext.Info.tStart   = startVPS;
                    macontext.Info.startFromVPS = true;
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
                    // changed from -615 to -577  // found invalid length with -577
                    if ((diff >= 285) || (diff <= -577)) {
                        dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop event seems to be invalid, use length from vdr info file");
                        vps->SetStop(-1);  // set VPS stop event to invalid
                    }
                    else {
                        dsyslog("cMarkAdStandalone::LoadInfo(): VPS events seems to be valid, use length from VPS events");
                        length = lengthVPS;
                        macontext.Info.lengthFromVPS = true;
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
                        esyslog("recording start %02d:%02d min after timer start, recording incomplete, marks will be wrong", -macontext.Info.tStart / 60, -macontext.Info.tStart % 60);
                        dsyslog("event length from timer: %5ds", length);
                        length += macontext.Info.tStart;
                        dsyslog("event length corrected %5ds", length);
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

    // log version and hostname
    char hostname[64];
    gethostname(hostname, 64);
    long lb;
    errno = 0;
    lb=sysconf(_SC_LONG_BIT);
    if (errno == 0) isyslog("starting markad v%s (%libit) on %s", VERSION, lb, hostname);
    else            isyslog("starting markad v%s on %s", VERSION, hostname);
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
        isyslog("using libavcodec.so.%s (%d) with %i threads", libver, ver, config->threads);
        if (ver != LIBAVCODEC_VERSION_INT) {
            esyslog("markad build with libavcodec header version %s, but runs with libavcodec lib version %s", AV_STRINGIFY(LIBAVCODEC_VERSION), libver);
            esyslog("libav header and library mismatch, fix your system");
            exit(EXIT_FAILURE);
        }
        if (ver < LIBAVCODEC_VERSION_VALID) isyslog("your libavcodec is deprecated, please update");
        FREE(strlen(libver)+1, "libver");
        free(libver);
    }
    dsyslog("libavcodec config: %s",avcodec_configuration());
    isyslog("on %s", directory);

    if (config->before) sleep(10);

    char *tmpDir = strdup(directory);
    if (!tmpDir) {
        esyslog("cMarkAdStandalone::cMarkAdStandalone(): memory allocation for tmpDir failed");
        exit(EXIT_FAILURE);
    }
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
        extractLogo = new cExtractLogo(macontext.Config->recDir, macontext.Info.ChannelName, macontext.Config->threads, macontext.Config->forcedFullDecode, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Info.AspectRatio);
        ALLOC(sizeof(*extractLogo), "extractLogo");
        extractLogo->ManuallyExtractLogo(config->logoExtraction, config->logoWidth, config->logoHeight);
        ALLOC(sizeof(*extractLogo), "extractLogo");
        delete extractLogo;
        return;
    }

    // check if requested decoding parameter are valid for this video codec and used FFmpeg version
    dsyslog("cMarkAdStandalone::cMarkAdStandalone(): check codec");
    char hwaccel[1] = {0};      //!< no hardware acceleration
    cDecoder *decoderTest = new cDecoder(macontext.Config->recDir, 1, true, hwaccel, false, false, nullptr); // one thread, full decocode, no hwaccel, no force interlaced, no index
    ALLOC(sizeof(*decoderTest), "decoderTest");
    if (!decoderTest->DecodeNextFrame(false)) {  // decode one video frame to get video info
        esyslog("cMarkAdStandalone::cMarkAdStandalone(): decode of first video packet failed");
        exit(EXIT_FAILURE);
    }
    int frameRate = decoderTest->GetVideoFrameRate();   // store frameRate for logo extraction and start mark if markad runs during recording
    marks.SetFrameRate(frameRate);  // register framerate to calculate VDR timestamps
    sceneMarks.SetFrameRate(frameRate);
    silenceMarks.SetFrameRate(frameRate);
    blackMarks.SetFrameRate(frameRate);
    dsyslog("cMarkAdStandalone::cMarkAdStandalone(): video characteristics: %s, frame rate %d, type %d, pixel format %d", (decoderTest->IsInterlacedFrame()) ? "interlaced" : "progressive", frameRate, decoderTest->GetVideoType(), decoderTest->GetVideoPixelFormat());

    // FFmpeg version dependent restriction
#if LIBAVCODEC_VERSION_INT <= ((58<<16)+( 54<<8)+100)   // FFmpeg 4.2.7  (Ubuntu 20.04)
    if ((decoderTest->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264) && (macontext.Config->fullDecode == false)) {
        isyslog("FFmpeg <= 4.2.7 does not support hwaccel without full decoding for H.264 video, disable hwaccel");
        macontext.Config->hwaccel[0] = 0;
        macontext.Config->forceHW    = false;
    }
    if ((decoderTest->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264) && decoderTest->IsInterlacedFrame()) {
        isyslog("FFmpeg <= 4.2.7 does not support only i-frame decoding for interlaced H.264 video, enable full decode");
        macontext.Config->fullDecode = true;
    }
#endif
#if LIBAVCODEC_VERSION_INT <= ((58<<16)+( 91<<8)+100)   // FFmpeg 4.3.7
    // AVlog(): Assertion !p->parent->stash_hwaccel failed at libavcodec/pthread_frame.c:649
    if ((macontext.Config->hwaccel[0] != 0) && (macontext.Config->threads != 1)) {
        isyslog("FFmpeg <= 4.3.7 does not support multithreading hwaccel, reduce to one thread");
        macontext.Config->threads = 1;
    }
#endif

    // inform decoder who use hwaccel, the video is interlaced. In this case this is not possible to detect from decoder because hwaccel deinterlaces frames
    if ((macontext.Config->hwaccel[0] != 0) && decoderTest->IsInterlacedFrame() && (decoderTest->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264)) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): inform decoder with hwaccel about H.264 interlaced video and force full decode");
        macontext.Config->forceInterlaced  = true;
        macontext.Config->fullDecode       = true;
        macontext.Config->forcedFullDecode = true;
    }
    FREE(sizeof(*decoderTest), "decoderTest");
    delete decoderTest;

    // write an early start mark for running recordings
    if (macontext.Info.isRunningRecording) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): recording is running, save dummy start mark at pre timer position %ds (%d)", macontext.Info.tStart, macontext.Info.tStart * frameRate);
        // use new marks object because we do not want to have this mark in final file
        cMarks *marksTMP = new cMarks();
        ALLOC(sizeof(*marksTMP), "marksTMP");
        marksTMP->SetFrameRate(frameRate);  // register framerate to write a guessed start position, will be overridden later
        marksTMP->Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, macontext.Info.tStart * frameRate, -1, "timer start", true);  // frame number unknows
        marksTMP->Save(macontext.Config->recDir, macontext.Info.isRunningRecording, macontext.Config->pts, true);
        FREE(sizeof(*marksTMP), "marksTMP");
        delete marksTMP;
    }

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

    if (title[0]) ptitle = title;
    else ptitle = const_cast<char *>(directory);

    if (config->osd) {
        osd = new cOSDMessage(config->svdrphost, config->svdrpport);
        if (osd) {
            ALLOC(sizeof(*osd), "osd");
            osd->Send("%s '%s'", tr("starting markad for"), ptitle);
        }
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (macontext.Info.ChannelName) isyslog("channel: %s", macontext.Info.ChannelName);

    // create index object
    index = new cIndex(macontext.Config->fullDecode);
    ALLOC(sizeof(*index), "index");
    marks.SetIndex(index);
    sceneMarks.SetIndex(index);
    silenceMarks.SetIndex(index);
    blackMarks.SetIndex(index);

    // create decoder object
    decoder = new cDecoder(macontext.Config->recDir, macontext.Config->threads, macontext.Config->fullDecode, macontext.Config->hwaccel, macontext.Config->forceHW, macontext.Config->forceInterlaced, index);
    ALLOC(sizeof(*decoder), "decoder");
}


cMarkAdStandalone::~cMarkAdStandalone() {
    dsyslog("cMarkAdStandalone::~cMarkAdStandalone(): delete object");
    if (!abortNow) marks.Save(directory, macontext.Info.isRunningRecording, macontext.Config->pts, true);

    // cleanup all objects
    if (detectLogoStopStart) {
        FREE(sizeof(*detectLogoStopStart), "detectLogoStopStart");
        delete detectLogoStopStart;
    }
    if (indexFile) {
        FREE(strlen(indexFile) + 1, "indexFile");
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
        if (!duplicate) {
            if (abortNow) osd->Send("%s '%s'", tr("markad aborted for"), ptitle);
            else osd->Send("%s '%s'", tr("markad finished for"), ptitle);
        }
        FREE(sizeof(*osd), "osd");
        delete osd;
        osd = nullptr;
    }

    if (decoder && decoder->GetErrorCount() > 0) isyslog("decoding errors: %d", decoder->GetErrorCount());

    if (evaluateLogoStopStartPair) {
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
    }

    FREE(sizeof(*index), "index");
    delete index;
    FREE(sizeof(*criteria), "criteria");
    delete criteria;

// log statistics
    if ((!abortNow) && (!duplicate) && (decoder->GetVideoFrameRate() > 0)) {
        LogSeparator();

        // broadcast length without advertisement
        dsyslog("recording statistics: -----------------------------------------------------------------------");
        int lengthFrames = marks.Length();
        int lengthSec    = lengthFrames / decoder->GetVideoFrameRate();
        dsyslog("broadcast length without advertisement: %6d frames, %6ds -> %d:%02d:%02dh", marks.Length(), lengthSec, lengthSec / 3600, (lengthSec % 3600) / 60,  lengthSec % 60);

        // recording length from VPS events
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
        // initial logo search
        time_t sec = round(static_cast<double>(elapsedTime.logoSearch) / 1000);
        dsyslog("pass 1 (initial logosearch): time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);
        // mark detection
        sec = round(static_cast<double>(elapsedTime.markDetection) / 1000);
        dsyslog("pass 2 (mark detection):     time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);
        // mark optimization
        sec = round(static_cast<double>(elapsedTime.markOptimization) / 1000);
        dsyslog("pass 3 (mark optimization):  time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);
        // overlap detection
        sec = round(static_cast<double>(elapsedTime.overlap) / 1000);
        dsyslog("pass 4 (overlap detection):  time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);
        // video cut, only cut is done
        sec = round(static_cast<double>(elapsedTime.cut) / 1000);
        if (sec > 0) dsyslog("pass 5 (cut recording):      time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);
        // mark pictures
        sec = round(static_cast<double>(elapsedTime.markPictures) / 1000);
        if (sec > 0) dsyslog("pass 6 (mark pictures):      time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

        dsyslog("global statistics: --------------------------------------------------------------------------");
        int decodeTime_s = round(decodeTime_ms / 1000);
        dsyslog("decoding:                    time %5ds -> %d:%02d:%02dh", decodeTime_s, decodeTime_s / 3600, (decodeTime_s % 3600) / 60,  decodeTime_s % 60);

        gettimeofday(&endAll, nullptr);
        sec              = endAll.tv_sec  - startAll.tv_sec;
        suseconds_t usec = endAll.tv_usec - startAll.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        double etime = 0;
        etime = sec + ((double) usec / 1000000) - waittime;
        isyslog("duration:                    time %5ds -> %d:%02d:%02dh", static_cast<int> (etime), static_cast<int> (etime / 3600),  (static_cast<int> (etime) % 3600) / 60, static_cast<int> (etime) % 60);
        dsyslog("----------------------------------------------------------------------------------------------");
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
           "                  <option>   0 = use logo only from logo cache directory\n"
           "                             1 = extract logo from recording, if fails use logo from logo cache\n"
           "                             2 = use logo from logo cache directory, if missing extract logo from recording (default)\n"
           "                --fulldecode\n"
           "                  decode all video frame types and set mark position to all frame types\n"
           "                --smartencode\n"
           "                  re-encode only short before and short after cut position\n"
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
    const char *tok     = nullptr;
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
            {"smartencode",  0, 0, 13},
            {"fullencode",   1, 0, 14},
            {"pts",          0, 0, 15},     // undocumented, only for development use
            {"hwaccel",      1, 0, 16},
            {"perftest",     0, 0, 17},     // undocumented, only for development use

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
            break;
        case 12: // --fulldecode
            config.fullDecode = true;
            break;
        case 13: // --smartencode
            config.smartEncode = true;
            break;
        case 14: // --fullencode
            if (config.smartEncode) fprintf(stderr, "markad: ----smartencode is set, ignore invalid --fullencode\n");
            else {
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
            }
            break;
        case 15: // --pts
            config.pts = true;
            break;
        case 16: // --hwaccel
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
        case 17: // --perftest
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

    if (bEdited) return EXIT_SUCCESS; // do nothing if called from vdr before/after the video is cutted
    if ((bAfter) && (config.online > 0)) { // online not valid together with after
        fprintf(stderr, "parameter --online is invalid for start markad after recording end\n");
        return EXIT_FAILURE;
    }
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
        if (config.smartEncode) {
            dsyslog("parameter --smartencode is set");
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
            if (!abortNow && config.perftest) {
                cTest *test = new cTest(config.recDir, config.fullDecode, config.hwaccel);
                test->Perf();
                delete test;
            }
            else {

                // detect marks
                if (!abortNow) cmasta->Recording();

                // logo mark optimization
                if (!abortNow) cmasta->LogoMarkOptimization();      // logo mark optimization

                // overlap detection
                if (!abortNow) cmasta->ProcessOverlap();            // overlap and closing credits detection

                // minor mark position optimization
                if (!abortNow) cmasta->BlackScreenOptimization();   // mark optimization with black scene
                if (!abortNow) cmasta->SilenceOptimization();       // mark optimization with mute scene
                if (!abortNow) cmasta->LowerBorderOptimization();   // mark optimization with lower border
                if (!abortNow) cmasta->SceneChangeOptimization();   // final optimization with scene changes (if we habe nothing else, try this as last resort)

                // video cut
                if (!abortNow) if (config.MarkadCut) cmasta->MarkadCut();

                // write debug mark pictures
#ifdef DEBUG_MARK_FRAMES
                if (!abortNow) cmasta->DebugMarkFrames(); // write frames picture of marks to recording directory
#endif

            }
        }
        FREE(sizeof(*cmasta), "cmasta");
        delete cmasta;
        cmasta = nullptr;

#ifdef DEBUG_MEM
        memList();
#endif
        return EXIT_SUCCESS;
    }
    return usage(config.svdrpport);
}
