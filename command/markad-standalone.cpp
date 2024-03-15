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


bool SYSLOG                    = false;
bool LOG2REC                   = false;
cDecoder *ptr_cDecoder         = NULL;
cExtractLogo *ptr_cExtractLogo = NULL;
cMarkAdStandalone *cmasta      = NULL;
bool restartLogoDetectionDone  = false;
int SysLogLevel                = 2;
bool abortNow                  = false;
int logoSearchTime_ms          = 0;
long int decodeTime_us         = 0;

struct timeval startAll, endAll = {};
struct timeval startTime1, endTime1 = {}; // pass 1 (logo search) time
struct timeval startTime2, endTime2 = {}; // pass 2 (mark detection) time
struct timeval startTime3, endTime3 = {}; // pass 3 (mark optimation) time
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
        const time_t now = time(NULL);
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
    chkSTART = iStartA + macontext.Video.Info.framesPerSecond * 480; //  fit for later broadcast start
    chkSTOP = startframe + macontext.Video.Info.framesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:   %4ds (%3dmin %2ds)", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame:  %5d  (%3dmin %2ds)", iStartA, static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:  %6d  (%3dmin %2ds)", iStopA, static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTART set to:     %6d  (%3dmin %2ds)", chkSTART, static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTOP set to:      %6d  (%3dmin %2ds)", chkSTOP, static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond) % 60);
}


// try MT_CHANNELSTOP
cMark *cMarkAdStandalone::Check_CHANNELSTOP() {
    // cleanup short channel stop/start pairs, they are stream errors
    cMark *channelStop = marks.GetNext(-1, MT_CHANNELSTOP);
    while (true) {
        if (!channelStop) break;
        cMark *channelStart = marks.GetNext(channelStop->position, MT_CHANNELSTART);
        if (!channelStart) break;
        int lengthChannel = 1000 * (channelStart->position - channelStop->position) / macontext.Video.Info.framesPerSecond;
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
    cMark *end = marks.GetAround(MAX_BEFORE_CHANNEL * macontext.Video.Info.framesPerSecond, iStopA, MT_CHANNELSTOP);
    if (end) {
        int diffAssumed = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTOP (%d) found %ds before assumed stop (%d)", end->position, diffAssumed, iStopA);
    }
    else {
        end = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);   // try last channel stop
        if (end) {
            int diffAssumed = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP (%d) found %ds before assumed stop (%d)", end->position, diffAssumed, iStopA);
            if (diffAssumed >= MAX_BEFORE_CHANNEL) {
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP too far before assumed stop");
                end = NULL;
            }
            if (diffAssumed <= -547) { // changed from -561 to -547
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): last MT_CHANNELSTOP too far after assumed stop");
                end = NULL;
            }
        }
    }
    // check if channel stop mark is valid end mark
    if (end) {
        cMark *cStart = marks.GetPrev(end->position, MT_CHANNELSTART);      // if there is short before a channel start, this stop mark belongs to next recording
        if (cStart) {
            if ((end->position - cStart->position) < (macontext.Video.Info.framesPerSecond * 120)) {
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTART short before MT_CHANNELSTOP found at frame %d with delta %ds, MT_CHANNELSTOP is not valid, try to find stop mark short before", cStart->position, static_cast<int> ((end->position - cStart->position) / macontext.Video.Info.framesPerSecond));
                end = marks.GetAround(macontext.Video.Info.framesPerSecond * 120, iStopA - (macontext.Video.Info.framesPerSecond * 120), MT_CHANNELSTOP);
                if (end) dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): new MT_CHANNELSTOP found short before at frame (%d)", end->position);
            }
            else {
                cMark *cStartFirst = marks.GetNext(0, MT_CHANNELSTART);  // get first channel start mark
                cMark *movedFirst  = marks.First();                      // maybe first mark is a moved channel mark
                if (movedFirst && (movedFirst->type == MT_MOVEDSTART) && (movedFirst->oldType == MT_CHANNELSTART)) cStartFirst = movedFirst;
                if (cStartFirst) {
                    int deltaC = (end->position - cStartFirst->position) / macontext.Video.Info.framesPerSecond;
                    if (deltaC < 244) {  // changed from 287 to 244, found shortest last part, do not reduce
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): first channel start mark (%d) and possible channel end mark (%d) to near %ds, this belongs to the next recording", cStartFirst->position, end->position, deltaC);
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): delete channel marks at (%d) and (%d)", cStartFirst->position, end->position);
                        marks.Del(cStartFirst->position);
                        marks.Del(end->position);
                        end = NULL;
                    }
                }
            }
        }
    }
    if (criteria.GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): cleanup logo start/stop marks near by channel start marks, they are useless info logo");
        cMark *channelStart = marks.GetNext(-1, MT_CHANNELSTART);
        while (channelStart) {
#define CHANNEL_LOGO_MARK 60
            cMark *logoMark = marks.GetAround(CHANNEL_LOGO_MARK * macontext.Video.Info.framesPerSecond, channelStart->position, MT_LOGOCHANGE, 0xF0);
            while (logoMark) {
                int diff = abs((channelStart->position - logoMark->position) / macontext.Video.Info.framesPerSecond);
                dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): delete logo mark (%d), %ds around channel start (%d)", logoMark->position, diff, channelStart->position);
                marks.Del(logoMark->position);
                logoMark = marks.GetAround(CHANNEL_LOGO_MARK * macontext.Video.Info.framesPerSecond, channelStart->position, MT_LOGOCHANGE, 0xF0);
            }
            channelStart = marks.GetNext(channelStart->position, MT_CHANNELSTART);
        }
    }

    if (end) {  // we found a channel end mark
        marks.DelWeakFromTo(marks.GetFirst()->position + 1, end->position, MT_CHANNELCHANGE); // delete all weak marks, except start mark
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): MT_CHANNELSTOP end mark (%d) found", end->position);
        return end;
    }
    else {
        dsyslog("cMarkAdStandalone::Check_CHANNELSTOP(): no MT_CHANNELSTOP mark found");
        return NULL;
    }
}


// try MT_HBORDERSTOP
cMark *cMarkAdStandalone::Check_HBORDERSTOP() {
    cMark *end = NULL;
    // cleanup very short hborder start/stop pair after detection restart, this can be a very long dark scene
    cMark *hStart = marks.GetNext(iStopA - (macontext.Video.Info.framesPerSecond * 240), MT_HBORDERSTART);
    if (hStart) {
        cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);
        if (hStop && (hStop->position < iStopA)) {
            int broadcastLength = (hStop->position - hStart->position) / macontext.Video.Info.framesPerSecond;
            if (broadcastLength <= 92) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): found short hborder start (%d) stop (%d) pair, length %ds after detection restart, this can be a very long dark scene, delete marks", hStart->position, hStop->position, broadcastLength);
                marks.Del(hStart->position);
                marks.Del(hStop->position);
            }
        }
    }
    // search hborder stop mark around iStopA
    end = marks.GetAround(600 * macontext.Video.Info.framesPerSecond, iStopA, MT_HBORDERSTOP);  // 10 minutes
    if (end) {
        dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_HBORDERSTOP found at frame %i", end->position);
        cMark *prevHStart = marks.GetPrev(end->position, MT_HBORDERSTART);
        if (prevHStart && (prevHStart->position > iStopA)) {
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder start mark (%d) is after assumed stop (%d), hborder stop mark (%d) is invalid", prevHStart->position, iStopA, end->position);
            // check if we got first hborder stop of next broadcast
            cMark *hBorderStopPrev = marks.GetPrev(end->position, MT_HBORDERSTOP);
            if (hBorderStopPrev) {
                int diff = (iStopA - hBorderStopPrev->position) / macontext.Video.Info.framesPerSecond;
                if (diff <= 476) { // maybe recording length is wrong
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder stop mark (%d) is %ds before assumed stop, take this as stop mark", hBorderStopPrev->position, diff);
                    end = hBorderStopPrev;
                }
                else {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): previous hborder stop mark (%d) is %ds before assumed stop, not valid", hBorderStopPrev->position, diff);
                    end = NULL;
                }
            }
            else {
                end = NULL;
            }
        }
    }
    else {
        if (criteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
            cMark *hBorderLast = marks.GetPrev(INT_MAX, MT_HBORDERCHANGE, 0xF0);
            if (hBorderLast && (hBorderLast->type == MT_HBORDERSTOP)) {
                int diffAssumed = (iStopA - hBorderLast->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): last hboder mark (%d) is stop mark, %ds before assumed stop (%d)", hBorderLast->position, diffAssumed, iStopA);
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
            int diff = 1000 * (end->position - channelStart->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): channel start (%d) from next braodcast %dms before hborder stop (%d) found, delete channel mark", channelStart->position, diff, end->position);
            if (diff <= 1000) marks.Del(channelStart->position);
        }
        // optimize hborder end mark with logo stop mark in case of next broadcast is also with hborder
        if (criteria.LogoInBorder(macontext.Info.ChannelName)) {
            cMark *logoStop     = marks.GetPrev(end->position, MT_LOGOSTOP);
            cMark *hborderStart = marks.GetPrev(end->position, MT_HBORDERSTART);
            if (logoStop && hborderStart) {
                int deltaLogoStop = (end->position - logoStop->position) / macontext.Video.Info.framesPerSecond;
                int deltaAssumed  = (iStopA        - logoStop->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): MT_LOGOSTOP at (%d) %ds before hborder stop, %ds before assumed stop found", logoStop->position, deltaLogoStop, deltaAssumed);
                if ((logoStop->position > hborderStart->position) && (deltaLogoStop <= 415) && (deltaAssumed <= 12)) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): use logo stop mark at (%d) before hborder stop (%d)", logoStop->position, end->position);
                    end = logoStop;
                }
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): no MT_HBORDERSTOP end mark found");
    return end;
}


// try MT_VBORDERSTOP
cMark *cMarkAdStandalone::Check_VBORDERSTOP() {
    cMark *end = marks.GetAround(360 * macontext.Video.Info.framesPerSecond, iStopA, MT_VBORDERSTOP); // 3 minutes
    if (end) {
        int deltaStopA = (end->position - iStopA) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): MT_VBORDERSTOP found at frame (%d), %ds after assumed stop", end->position, deltaStopA);
        if (deltaStopA >= 326) {  // we found start of first ad from next broadcast, changed from 353 to 326
            dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): MT_VBORDERSTOP too far after assumed stop, ignoring");
            return NULL;
        }
        if (criteria.LogoInBorder(macontext.Info.ChannelName)) {
            cMark *logoStop = marks.GetPrev(end->position, MT_LOGOSTOP);
            if (logoStop) {
                int deltaLogoStop = 1000 * (end->position - logoStop->position) / macontext.Video.Info.framesPerSecond;
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
                if (prevVStart->position > iStopA) {
                    dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): previous vertial border start (%d) is after assumed stop (%d), delete this marks, they are form next brodcast", prevVStart->position, iStopA);
                    marks.Del(prevVStart->position);
                    marks.Del(end->position);
                    end = NULL;
                }
            }
            // we use vborder and we found final vborder end mark
            if (end && (criteria.GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_USED)) {
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
    cMark *end         = NULL;
    cMark *lEndAssumed = marks.GetAround(400 * macontext.Video.Info.framesPerSecond, iStopA, MT_LOGOSTOP);
    if (!lEndAssumed) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no logo stop mark found");
        return NULL;  // no logo stop mark around assumed stop
    }

    // try to select best logo end mark based on closing credits follow
    if (evaluateLogoStopStartPair) {
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): search for best logo end mark based on closing credits after logo stop");
        // search from nearest logo stop mark to end
        cMark *lEnd = lEndAssumed;
        while (!end && lEnd) {
            int diffAssumed = (lEnd->position - iStopA) / macontext.Video.Info.framesPerSecond;
            if (diffAssumed >= 0) {
                int status = evaluateLogoStopStartPair->GetIsClosingCreditsAfter(lEnd->position);
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d): %ds after assumed stop (%d), closing credits status %d", lEnd->position, diffAssumed, iStopA, status);
                if (diffAssumed >= 300) break;
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
            int diffAssumed = (iStopA - lEnd->position) / macontext.Video.Info.framesPerSecond;
            if (diffAssumed > 0) {
                int status = evaluateLogoStopStartPair->GetIsClosingCreditsAfter(lEnd->position);
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): stop mark (%d): %ds before assumed stop (%d), closing credits status %d", lEnd->position, diffAssumed, iStopA, status);
                if (diffAssumed >= 300) break;
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
    cMark *lEnd = lEndAssumed;
    while (!end && lEnd) {
        int diffAssumed = (lEnd->position - iStopA) / macontext.Video.Info.framesPerSecond;
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): check for separator for logo stop (%d), %ds after assumed end (%d)", lEnd->position, diffAssumed, iStopA);
        if (diffAssumed > 300 || diffAssumed <= -378) break;
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
        int diffAssumed = (iStopA - lEnd->position) / macontext.Video.Info.framesPerSecond;
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): check for separator for logo stop (%d), %ds before assumed end (%d)", lEnd->position, diffAssumed, iStopA);
        if (diffAssumed >= 314) break;   // changed from 349 to 314
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
            int diffStart = 1000 * (logoStart->position - end->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): next logo start mark (%d) %dms after end mark (%d)", logoStart->position, diffStart, end->position);
            if (diffStart <= 880) criteria.SetClosingCreditsState(end->position, CRITERIA_UNAVAILABLE);  // early logo start after, there are no closing credits without logo
        }
        return end;
    }

    // cleanup very short start/stop pairs around possible end marks, these are logo detection failures
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): search for nearest logo end mark to assumed stop");
    while (true) {
        end = marks.GetAround(400 * macontext.Video.Info.framesPerSecond, iStopA, MT_LOGOSTOP);
        if (end) {
            int iStopDelta = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
#define MAX_LOGO_BEFORE_ASSUMED 98   // changed from 253 to 177 to 98, do not increase, we will lost last part
            dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): MT_LOGOSTOP found at frame (%d), %ds (expect < %ds) before assumed stop (%d)", end->position, iStopDelta, MAX_LOGO_BEFORE_ASSUMED, iStopA);
            if (iStopDelta >= MAX_LOGO_BEFORE_ASSUMED) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo stop mark too far before assumed stop");
                end = NULL;
                break;
            }
            else {
                cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                if (prevLogoStart) {
                    int deltaLogoStart = 1000 * (end->position - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
#define MIN_LOGO_START_STOP 1480   // very short logo start/stop can be false positiv logo detection, changed from 600 to 1480
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
    if (end && (criteria.GetMarkTypeState(MT_HBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
        cMark *hBorderStart = marks.GetPrev(end->position, MT_HBORDERSTART);
        if (hBorderStart) {
            const cMark *hBorderStartPrev = marks.GetPrev(hBorderStart->position, MT_HBORDERSTART);
            if (!hBorderStartPrev) {
                int deltahBorder = (hBorderStart->position - iStopA) / macontext.Video.Info.framesPerSecond;
                int deltaLogo    = (end->position          - iStopA) / macontext.Video.Info.framesPerSecond;
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
        int beforeAssumed = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): end mark (%d) %ds before assumed stop (%d)", end->position, beforeAssumed, iStopA);
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
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): advertising before from (%d) to (%d) %3ds", prevLogoStart->position, prevLogoStop->position, adBefore);
                int adAfter = (nextLogoStart->position - end->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): advertising after  from (%d) to (%d) %3ds", end->position, nextLogoStart->position, adAfter);
                int broadcastBefore = (end->position - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): broadcast   before from (%d) to (%d) %3ds", prevLogoStart->position, end->position, broadcastBefore);

                if (broadcastBefore <= 115) {  // end mark invalid there is only a very short broadcast after end mark, changed from 34 to 115
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): broadcast before only %ds, end mark (%d) is invalid", broadcastBefore, end->position);
                    end = NULL;
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
                int vpsStopFrame = recordingIndexMark->GetFrameFromOffset(vpsOffset * 1000);
                int diffAfterVPS = (prevLogoStop->position - vpsStopFrame) / macontext.Video.Info.framesPerSecond;
                if (diffAfterVPS >= 0) {
                    dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): VPS stop event at (%d) is %ds after previous logo stop (%d), use this as end mark", vpsStopFrame, diffAfterVPS, prevLogoStop->position);
                    end = prevLogoStop;
                }
            }
        }
        // check if there could follow closing credits, prevent false detection of closing credits from opening creditis of next broadcast
        cMark *nextLogoStart = marks.GetNext(end->position);
        if (nextLogoStart) {
            int closingCreditsLength = (nextLogoStart->position - end->position) / macontext.Video.Info.framesPerSecond;
            if (closingCreditsLength <= 2) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): logo start (%d) %ds after end mark (%d), no closing credits without logo can follow", nextLogoStart->position, closingCreditsLength, end->position);
                criteria.SetClosingCreditsState(end->position, CRITERIA_UNAVAILABLE);
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_LOGOSTOP(): no MT_LOGOSTOP mark found");

    // clean undeteced info logos / logo changes
    if (end) CleanupUndetectedInfoLogo(end);
    return end;
}



// detect short logo stop/start before final end mark or after final start mark
// they can be undetected info logos, introduction logos or text previews over the logo (e.g. SAT.1)
// only called if we are sure this is the correct logo end mark (closing credit detected or separator detected)
// prevent to later move end mark/start mark to previous/after logo mark from undetected logo change
void cMarkAdStandalone::CleanupUndetectedInfoLogo(const cMark *mark) {
    if (!mark) return;
    if (mark->type == MT_LOGOSTART) { // cleanup logo
        while (true) {
            cMark *nextLogoStop = marks.GetNext(mark->position, MT_LOGOSTOP);
            if (!nextLogoStop) return;
            cMark *nextLogoStart = marks.GetNext(nextLogoStop->position, MT_LOGOSTART);
            if (!nextLogoStart) return;
            int deltaStop = 1000 * (nextLogoStop->position  - mark->position)         / macontext.Video.Info.framesPerSecond;
            int adLength  = 1000 * (nextLogoStart->position - nextLogoStop->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): MT_LOGOSTART (%5d) -> %6dms -> MT_LOGOSTOP (%5d) -> %6dms -> MT_LOGOSTART (%5d)", mark->position, deltaStop, nextLogoStop->position, adLength, nextLogoStart->position);
            // valid example
            // MT_LOGOSTART ( 5343) ->  33280ms -> MT_LOGOSTOP ( 6175) ->   1240ms -> MT_LOGOSTART ( 6206)
            // MT_LOGOSTART ( 5439) ->  33320ms -> MT_LOGOSTOP ( 6272) ->   1120ms -> MT_LOGOSTART ( 6300)
            // MT_LOGOSTART ( 5439) ->  41040ms -> MT_LOGOSTOP ( 6465) ->    400ms -> MT_LOGOSTART ( 6475)
            if ((deltaStop <= 41040) && (adLength <= 6240)) {
                dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): undetected info logo from (%d) to (%d), delete marks", nextLogoStop->position, nextLogoStart->position);
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
            int stopStart = 1000 * (prevLogoStart->position - prevLogoStop->position)  / macontext.Video.Info.framesPerSecond;
            int startEnd  = 1000 * (mark->position          - prevLogoStart->position) / macontext.Video.Info.framesPerSecond;
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


bool cMarkAdStandalone::HaveSilenceSeparator(const cMark *mark) {
    if (!mark) return false;
    // check start mark
    if (mark->type == MT_LOGOSTART) {
        // check sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTART (mark)
        cMark *silenceStop = silenceMarks.GetPrev(mark->position, MT_SOUNDSTART);
        if (silenceStop) {
            cMark *silenceStart = silenceMarks.GetPrev(silenceStop->position, MT_SOUNDSTOP);
            if (silenceStart) {
                cMark *logoStop = marks.GetPrev(silenceStart->position, MT_LOGOSTOP);
                if (logoStop) {
                    int logoStopSilenceStart    = 1000 * (silenceStart->position - logoStop->position)     / macontext.Video.Info.framesPerSecond;
                    int silenceStartSilenceStop = 1000 * (silenceStop->position  - silenceStart->position) / macontext.Video.Info.framesPerSecond;
                    int silenceStopLogoStart    = 1000 * (mark->position         - silenceStop->position)  / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%4d) -> %5dms -> MT_SOUNDSTOP (%4d) -> %5dms -> MT_SOUNDSTART (%4d) -> %5dms -> MT_LOGOSTART (%4d)", logoStop->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, mark->position);
                    // valid example
                    // MT_LOGOSTOP (8870) -> 29440ms -> MT_SOUNDSTOP (9606) ->   680ms -> MT_SOUNDSTART (9623) ->  1400ms -> MT_LOGOSTART (9641)
                    // MT_LOGOSTOP (4496) ->   560ms -> MT_SOUNDSTOP (4510) ->   160ms -> MT_SOUNDSTART (4514) ->  1520ms -> MT_LOGOSTART (4548)
                    // MT_LOGOSTOP (3536) ->  1520ms -> MT_SOUNDSTOP (3574) ->   440ms -> MT_SOUNDSTART (3585) ->  1880ms -> MT_LOGOSTART (3632)
                    if ((logoStopSilenceStart <= 29440) && (silenceStartSilenceStop >= 160) && (silenceStopLogoStart <= 1880)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
        // check sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_LOGOSTART -> MT_SOUNDSTART
        silenceStop = silenceMarks.GetNext(mark->position, MT_SOUNDSTART);
        if (silenceStop) {
            cMark *silenceStart = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);
            if (silenceStart) {
                cMark *logoStop = marks.GetPrev(silenceStart->position, MT_LOGOSTOP);
                if (logoStop) {
                    int logoStopSilenceStart  = 1000 * (silenceStart->position - logoStop->position)     / macontext.Video.Info.framesPerSecond;
                    int silenceStartLogoStart = 1000 * (mark->position         - silenceStart->position) / macontext.Video.Info.framesPerSecond;
                    int logoStartSilenceStop  = 1000 * (silenceStop->position  - mark->position)         / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_LOGOSTART (%6d) -> %4dms -> MT_SOUNDSTART (%6d)", logoStop->position, logoStopSilenceStart, silenceStart->position, silenceStartLogoStart, mark->position, logoStartSilenceStop, silenceStop->position);
                    // valid example
                    // MT_LOGOSTOP (8282) -> 3920ms -> MT_SOUNDSTOP (  8380) ->  80ms -> MT_LOGOSTART (  8382) ->   40ms -> MT_SOUNDSTART (  8383)
                    if ((logoStopSilenceStart <= 3920) && (silenceStartLogoStart <= 80) && (logoStartSilenceStop <= 40)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo start mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }
    }

    // check stop mark
    if (mark->type == MT_LOGOSTOP) {
        // sequence MT_LOGOSTOP -> MT_SOUNDSTOP -> MT_SOUNDSTART -> MT_LOGOSTART
        cMark *silenceStart = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP); // silence can start at the same position as logo stop
        if (silenceStart) {
            cMark *silenceStop = silenceMarks.GetNext(silenceStart->position, MT_SOUNDSTART);
            if (silenceStop) {
                cMark *logoStart = marks.GetNext(silenceStop->position, MT_LOGOSTART);
                if (logoStart) {
                    int logoStopSilenceStart    = 1000 * (silenceStart->position - mark->position)         / macontext.Video.Info.framesPerSecond;
                    int silenceStartSilenceStop = 1000 * (silenceStop->position  - silenceStart->position) / macontext.Video.Info.framesPerSecond;
                    int silenceStopLogoStart    = 1000 * (logoStart->position    - silenceStop->position)  / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %5dms -> MT_LOGOSTART (%6d)", mark->position, logoStopSilenceStart, silenceStart->position, silenceStartSilenceStop, silenceStop->position, silenceStopLogoStart, logoStart->position);
                    // valid sequence
                    // MT_LOGOSTOP ( 77290) -> 5040ms -> MT_SOUNDSTOP ( 77416) ->  160ms -> MT_SOUNDSTART ( 77420) ->    40ms -> MT_LOGOSTART ( 77421)
                    // MT_LOGOSTOP ( 86346) ->  120ms -> MT_SOUNDSTOP ( 86349) ->  120ms -> MT_SOUNDSTART ( 86352) ->  4920ms -> MT_LOGOSTART ( 86475)
                    //
                    // invalid sequence
                    // MT_LOGOSTOP ( 42715) -> 4840ms -> MT_SOUNDSTOP ( 42836) ->  360ms -> MT_SOUNDSTART ( 42845) -> 94760ms -> MT_LOGOSTART ( 45214) -> late logo stop mark
                    // MT_LOGOSTOP ( 39358) -> 4920ms -> MT_SOUNDSTOP ( 39481) ->  440ms -> MT_SOUNDSTART ( 39492) -> 29880ms -> MT_LOGOSTART ( 40239) -> end of preview
                    if ((logoStopSilenceStart <= 5040) && (silenceStartSilenceStop >= 120) && (silenceStartSilenceStop <= 160) &&
                            (silenceStopLogoStart <= 4920)) {
                        dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): logo stop mark (%d): silence sequence is invalid", mark->position);
                }
            }
        }

        // sequence MT_SOUNDSTOP -> MT_LOGOSTOP -> MT_SOUNDSTART -> MT_LOGOSTART
        silenceStart = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);
        if (silenceStart) {
            cMark *silenceStop = silenceMarks.GetNext(mark->position, MT_SOUNDSTART);
            if (silenceStop) {
                cMark *logoStart = marks.GetNext(silenceStop->position, MT_LOGOSTART);
                if (logoStart) {
                    int silenceStartLogoStop = 1000 * (mark->position         - silenceStart->position) / macontext.Video.Info.framesPerSecond;
                    int logoStopSilenceStop  = 1000 * (silenceStop->position  - mark->position)         / macontext.Video.Info.framesPerSecond;
                    int silenceStopLogoStart = 1000 * (logoStart->position    - silenceStop->position)  / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveSilenceSeparator(): MT_SOUNDSTOP (%6d) -> %4dms MT_LOGOSTOP (%6d) -> %4dms -> MT_SOUNDSTART (%6d) -> %6dms -> MT_LOGOSTART (%6d)", silenceStart->position, silenceStartLogoStop, mark->position, logoStopSilenceStop, silenceStop->position, silenceStopLogoStart, logoStart->position);
                    // valid sequence
                    // MT_SOUNDSTOP ( 72667) ->  560ms MT_LOGOSTOP ( 72681) ->  240ms -> MT_SOUNDSTART ( 72687) ->   1920ms -> MT_LOGOSTART ( 72735)
                    //
                    // invalid example
                    // MT_SOUNDSTOP ( 88721) ->  120ms MT_LOGOSTOP ( 88724) ->  240ms -> MT_SOUNDSTART ( 88730) ->  65560ms -> MT_LOGOSTART ( 90369)  -> stop mark before last ad
                    if ((silenceStartLogoStop <= 560) && (logoStopSilenceStop <= 240) && (silenceStopLogoStart < 65560)) {
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
                    int silenceStartSilenceStop = 1000 * (silenceStop->position - silenceStart->position) / macontext.Video.Info.framesPerSecond;
                    int silenceStopLogoStop     = 1000 * (mark->position        - silenceStop->position)  / macontext.Video.Info.framesPerSecond;
                    int logoStopLogoStart       = 1000 * (logoStart->position   - mark->position)         / macontext.Video.Info.framesPerSecond;
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
        // check sequence MT_NOBLACKSTOP -> MT_LOGOSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART (mark)
        cMark *blackStop = blackMarks.GetPrev(mark->position, MT_NOBLACKSTART);
        if (blackStop) {
            cMark *stopBefore = marks.GetPrev(blackStop->position, MT_LOGOSTOP);
            if (stopBefore) {
                cMark *blackStart = blackMarks.GetPrev(stopBefore->position, MT_NOBLACKSTOP);
                if (blackStart) {
                    int diffBlackStartLogoStop = 1000 * (stopBefore->position - blackStart->position) / macontext.Video.Info.framesPerSecond;
                    int diffLogoStopBlackStop  = 1000 * (blackStop->position - stopBefore->position)  / macontext.Video.Info.framesPerSecond;
                    int diffBlackStopLogoStart = 1000 * (mark->position - blackStop->position)        / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%d) -> %3dms -> MT_LOGOSTOP (%d) -> %3dms -> MT_NOBLACKSTART (%d) -> %3dms -> MT_LOGOSTART (%d)", blackStart->position, diffBlackStartLogoStop, stopBefore->position, diffLogoStopBlackStop, blackStop->position, diffBlackStopLogoStart, mark->position);
                    if ((diffBlackStartLogoStop <= 0) && (diffLogoStopBlackStop <= 0) && (diffBlackStopLogoStart <= 0)) { // TODO
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is valid");
                        return true;
                    }
                    else dsyslog("cMarkAdStandalone::HaveBlackSeparator(): black screen sequence is invalid");
                }
            }
        }

        // check squence MT_LOGOSTOP -> MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART (mark)
        blackStop = blackMarks.GetPrev(mark->position, MT_NOBLACKSTART);
        if (blackStop) {  // from above
            cMark *blackStart = blackMarks.GetPrev(blackStop->position, MT_NOBLACKSTOP);
            if (blackStart) {
                cMark *stopBefore = marks.GetPrev(blackStart->position, MT_LOGOSTOP);
                if (stopBefore) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / macontext.Video.Info.framesPerSecond;
                    int diffBlackStopLogoStart  = 1000 * (mark->position       - blackStop->position)  / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%4d)-> %5dms -> MT_NOBLACKSTOP (%4d) -> %4dms -> MT_NOBLACKSTART (%4d) -> %4dms -> MT_LOGOSTART (%4d)", stopBefore->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, mark->position);
// valid example
// MT_LOGOSTOP (6419)->    40ms -> MT_NOBLACKSTOP (6420) ->  360ms -> MT_NOBLACKSTART (6429) ->  200ms -> MT_LOGOSTART (6434)
// MT_LOGOSTOP (3536)->  1680ms -> MT_NOBLACKSTOP (3578) ->  320ms -> MT_NOBLACKSTART (3586) -> 1800ms -> MT_LOGOSTART (3631)  (TELE 5, fade in logo)
// MT_LOGOSTOP (5887)->  4880ms -> MT_NOBLACKSTOP (6009) ->  440ms -> MT_NOBLACKSTART (6020) -> 1040ms -> MT_LOGOSTART (6046)  (Kabel 1 Austria, separator picture before)
// MT_LOGOSTOP (4860)-> 30040ms -> MT_NOBLACKSTOP (5611) ->  440ms -> MT_NOBLACKSTART (5622) -> 1040ms -> MT_LOGOSTART (5648)  (kabel eins, ad in frame without logo before)
                    if ((diffLogoStopBlackStart <= 30040) && (diffBlackStartBlackStop >= 320) && (diffBlackStopLogoStart <= 1800)) {
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
                    int diffLogoStopLogoStart   = 1000 * (mark->position       - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                    int diffLogoStartBlackStart = 1000 * (blackStart->position - mark->position)       / macontext.Video.Info.framesPerSecond;
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%d)-> %3dms ->  MT_LOGOSTART (%d) -> %3dms -> MT_NOBLACKSTOP (%d) -> %3dms -> MT_NOBLACKSTART (%d)", stopBefore->position, diffLogoStopLogoStart, mark->position, diffLogoStartBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position);
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
    }

    // check logo stop mark
    if (mark->type == MT_LOGOSTOP) {
        // check sequence MT_NOBLACKSTOP -> MT_LOGOSTOP (mark) -> MT_NOBLACKSTART -> MT_LOGOSTART
        cMark *startAfter = marks.GetNext(mark->position, MT_LOGOSTART);
        if (startAfter) {
            cMark *blackStart = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP); // black screen can start at the same position as logo stop
            if (blackStart) {
                cMark *blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffStopBlack  = 1000 * (mark->position       - blackStart->position) / macontext.Video.Info.framesPerSecond;
                    int diffStopLogo   = 1000 * (blackStop->position  - mark->position)       / macontext.Video.Info.framesPerSecond;
                    int diffStartBlack = 1000 * (startAfter->position - blackStop->position)  / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%5d) -> %dms -> MT_LOGOSTOP (%5d) -> %4dms -> MT_NOBLACKSTART (%5d) -> %4dms -> MT_LOGOSTART (%5d)", blackStart->position, diffStopBlack, mark->position, diffStopLogo, blackStop->position, diffStartBlack, startAfter->position);
// valid sequence
// MT_NOBLACKSTOP (90520) -> 360ms -> MT_LOGOSTOP (90529) ->  200ms -> MT_NOBLACKSTART (90534) -> 3960ms -> MT_LOGOSTART (90633)
                    if ((diffStopBlack <= 2920) &&  (diffStopLogo >= 0) && (diffStopLogo <= 3000) && (diffStartBlack <= 3960)) {
                        dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                        return true;
                    }
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
                }
            }
        }

        // check sequence MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTOP  (very long blackscreen short before logo stop mark)
        cMark *blackStart = blackMarks.GetPrev(mark->position, MT_NOBLACKSTOP);
        if (blackStart) {
            cMark *blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);   // this can be different from above
            if (blackStop) {
                int lengthBlack  = 1000 * (blackStop->position - blackStart->position)  / macontext.Video.Info.framesPerSecond;
                int distLogoStop = 1000 * (mark->position      - blackStop->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_NOBLACKSTOP (%d) -> %dms ->  MT_NOBLACKSTART (%d) -> %dms -> MT_LOGOSTOP (%5d)", blackStart->position, lengthBlack, blackStop->position, distLogoStop, mark->position);
                if ((distLogoStop >= -2880)  && (distLogoStop <= 2200) && (lengthBlack >= 2320)) {
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is valid", mark->position);
                    return true;
                }
                dsyslog("cMarkAdStandalone::HaveBlackSeparator(): logo stop mark (%d): black screen sequence is invalid", mark->position);
            }
        }

        // check sequence: MT_LOGOSTOP -> MT_NOBLACKSTOP -> MT_NOBLACKSTART -> MT_LOGOSTART  (black screen between logo end mark and start of next broadcast)
        blackStart = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
        if (blackStart) {
            cMark *blackStop = blackMarks.GetNext(blackStart->position, MT_NOBLACKSTART);
            if (blackStop) {
                cMark *logoStart = marks.GetNext(blackStop->position - 1, MT_LOGOSTART);
                if (logoStart) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - mark->position)       /  macontext.Video.Info.framesPerSecond;
                    int diffBlackStartBlackStop = 1000 * (blackStop->position  - blackStart->position) /  macontext.Video.Info.framesPerSecond;
                    int diffBlackStopLogoStart  = 1000 * (logoStart->position  - blackStop->position)  /  macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::HaveBlackSeparator(): MT_LOGOSTOP (%5d) -> %5dms -> MT_NOBLACKSTOP (%5d) -> %5dms ->  MT_NOBLACKSTART (%5d) -> %5dms -> MT_LOGOSTART (%5d)", mark->position, diffLogoStopBlackStart, blackStart->position, diffBlackStartBlackStop, blackStop->position, diffBlackStopLogoStart, logoStart->position);
                    // valid sequence
                    // MT_LOGOSTOP (72310) ->  4240ms -> MT_NOBLACKSTOP (72416) ->   440ms ->  MT_NOBLACKSTART (72427) ->   800ms -> MT_LOGOSTART (72447)
                    // MT_LOGOSTOP (84786) ->   120ms -> MT_NOBLACKSTOP (84789) ->   240ms ->  MT_NOBLACKSTART (84795) ->  1800ms -> MT_LOGOSTART (84840)
                    // MT_LOGOSTOP (86549) ->    80ms -> MT_NOBLACKSTOP (86551) ->   280ms ->  MT_NOBLACKSTART (86558) -> 31800ms -> MT_LOGOSTART (87353)
                    // MT_LOGOSTOP (91313) ->   180ms -> MT_NOBLACKSTOP (91322) ->   120ms ->  MT_NOBLACKSTART (91328) ->  1940ms -> MT_LOGOSTART (91425)
                    //
                    // invalid example
                    // MT_LOGOSTOP (96429) ->  2920ms -> MT_NOBLACKSTOP (96502) ->  2920ms ->  MT_NOBLACKSTART (96575) ->  4520ms -> MT_LOGOSTART (96688)
                    // info logo                         start opening credits                 end opening credits                   end info logo
                    if ((diffLogoStopBlackStart <= 4240) && (diffBlackStartBlackStop >= 120) && (diffBlackStartBlackStop < 2920) && (diffBlackStopLogoStart <= 31800)) {
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
                cMark *blackStop = blackMarks.GetNext(logoStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopBlackStart  = 1000 * (blackStart->position - mark->position)       /  macontext.Video.Info.framesPerSecond;
                    int diffBlackStartLogoStart = 1000 * (logoStart->position  - blackStart->position) /  macontext.Video.Info.framesPerSecond;
                    int diffLogoStartBlackStop  = 1000 * (blackStop->position  - logoStart->position)  /  macontext.Video.Info.framesPerSecond;
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
                cMark *blackStop = blackMarks.GetNext(logoStart->position, MT_NOBLACKSTART);
                if (blackStop) {
                    int diffLogoStopLogoStart  = 1000 * (logoStart->position - mark->position)       /  macontext.Video.Info.framesPerSecond;
                    int diffLogoStartBlackStart  = 1000 * (blackStart->position - logoStart->position)       /  macontext.Video.Info.framesPerSecond;
                    int diffBlackStartBlackStop  = 1000 * (blackStop->position - blackStart->position)       /  macontext.Video.Info.framesPerSecond;
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


bool cMarkAdStandalone::HaveInfoLogoSequence(const cMark *mark) {  // special case opening/closing logo sequence from kabel eins, unable to detect info logo change from this channel
    if (!mark) return false;
    // check logo start mark
    if (mark->type == MT_LOGOSTART) {
        cMark *stop1After = marks.GetNext(mark->position, MT_LOGOSTOP);
        if (!stop1After) return false;
        cMark *start2After = marks.GetNext(stop1After->position, MT_LOGOSTART);
        if (!start2After) return false;
        int diffMarkStop1After        = 1000 * (stop1After->position  - mark->position)       / macontext.Video.Info.framesPerSecond;
        int diffStop1AfterStart2After = 1000 * (start2After->position - stop1After->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTART (%5d) -> %dms -> MT_LOGOSTOP (%5d) -> %dms -> MT_LOGOSTART (%5d)", mark->position, diffMarkStop1After, stop1After->position, diffStop1AfterStart2After, start2After->position);
        // valid example
        // MT_LOGOSTART ( 5439) -> 5920ms -> MT_LOGOSTOP ( 5587) -> 1120ms -> MT_LOGOSTART ( 5615)
        if ((diffMarkStop1After <= 5920) && (diffStop1AfterStart2After <= 1120)) {
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
        int diffStart1Mark  = 1000 * (mark->position         - start1Before->position) / macontext.Video.Info.framesPerSecond;
        int diffStop1Start1 = 1000 * (start1Before->position - stop1Before->position)  / macontext.Video.Info.framesPerSecond;
        int diffStart2Stop1 = 1000 * (stop1Before->position  - start2Before->position) / macontext.Video.Info.framesPerSecond;
        int diffStop2Start2 = 1000 * (start2Before->position - stop2Before->position)  / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): MT_LOGOSTOP (%5d) %dms MT_LOGOSTART (%5d) %dms MT_LOGOSTOP (%5d) %dms MT_LOGOSTART (%5d) %dms MT_LOGOSTOP (%5d)", stop2Before->position, diffStop2Start2, start2Before->position, diffStart2Stop1, stop1Before->position, diffStop1Start1, start1Before->position, diffStart1Mark, mark->position);
        // valid examples
        // MT_LOGOSTOP (185315) 1080ms MT_LOGOSTART (185342)  8160ms MT_LOGOSTOP (185546) 840ms MT_LOGOSTART (185567)  18880ms MT_LOGOSTOP (186039)
        //
        // invalid example
        // MT_LOGOSTOP (29039)   240ms MT_LOGOSTART (29045) 742640ms MT_LOGOSTOP  (47611) 200ms MT_LOGOSTART  (47616) 769200ms MT_LOGOSTOP (66846)
        if ((diffStop2Start2 <=  1080) &&                                   // change from logo to closing logo
                (diffStart2Stop1 >=  8160) && (diffStart2Stop1 <= 10000) && // closing logo deteted as logo
                (diffStop1Start1 <=   840) &&                               // change from closing logo to logo
                (diffStart1Mark  >= 18800) && (diffStart1Mark  <= 20000)) { // end part between closing logo and broadcast end
            dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): found closing info logo sequence");
            return true;
        }
        else dsyslog("cMarkAdStandalone::HaveInfoLogoSequence(): logo stop mark (%d): closing logo info sequence is invalid", mark->position);
    }
    return false;
}


int cMarkAdStandalone::CheckStop() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): start check stop (%d)", frameCurrent);

    char *indexToHMSF = marks.IndexToHMSF(iStopA);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("assumed stop position (%d) at %s", iStopA, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    DebugMarks();     //  only for debugging

    // cleanup invalid marks
    //
    // check for very long dark opening credits of next braodcast and cleanup marks
    if (macontext.Video.Info.frameDarkOpeningCredits >= 0) {
        dsyslog("cMarkAdStandalone::CheckStop(): found very long dark opening credits start at frame (%d), check which type of border mark is valid", macontext.Video.Info.frameDarkOpeningCredits);
        const cMark *hStart = marks.GetNext(macontext.Video.Info.frameDarkOpeningCredits - 1, MT_HBORDERSTART);
        const cMark *vStart = marks.GetNext(macontext.Video.Info.frameDarkOpeningCredits - 1, MT_VBORDERSTART);
        if (hStart && !vStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): hborder start found but no vborder start, next broadcast has hborder");
            cMark *vStop = marks.GetNext(iStopA, MT_VBORDERSTOP);
            if (vStop) {
                dsyslog("cMarkAdStandalone::CheckStop(): cleanup invalid vborder stop mark (%d)", vStop->position);
                marks.Del(vStop->position); // delete wrong vborder stop marks
            }
        }
    }
    // cleanup very short logo stop/start after aspect ratio start, they are from a fading in logo
    cMark *aStart = marks.GetNext(0, MT_ASPECTSTART);
    while (true) {
        if (!aStart) break;
        cMark *lStop = marks.GetNext(aStart->position, MT_LOGOSTOP);
        if (lStop) {
            cMark *lStart = marks.GetNext(lStop->position, MT_LOGOSTART);
            if (lStart) {
                int diffStop  = 1000 * (lStop->position  - aStart->position) / macontext.Video.Info.framesPerSecond;
                int diffStart = 1000 * (lStart->position - aStart->position) / macontext.Video.Info.framesPerSecond;
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
    if (criteria.GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_UNKNOWN) RemoveLogoChangeMarks();

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): marks after first cleanup:");
    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): start end mark selection");

// try MT_CHANNELSTOP
    cMark *end = NULL;
    if (criteria.GetMarkTypeState(MT_CHANNELCHANGE) >= CRITERIA_UNKNOWN) end = Check_CHANNELSTOP();

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
        if (!end) end = marks.GetAround(360 * (macontext.Video.Info.framesPerSecond), iStopA, MT_ASPECTSTOP);      // try MT_ASPECTSTOP
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
                // now we may have a hborder or a logo stop mark before aspect stop mark, check if valid
                if (stopBefore) { // maybe real stop mark was deleted because on same frame as logo/hborder stop mark
                    int diffStopA      = (iStopA - stopBefore->position)        /  macontext.Video.Info.framesPerSecond;
                    int diffAspectStop = (end->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                    char *markType = marks.TypeToText(stopBefore->type);
                    dsyslog("cMarkAdStandalone::CheckStop(): found %s stop mark (%d) %ds before aspect ratio end mark (%d), %ds before assumed stop", markType, stopBefore->position, diffAspectStop, end->position, diffStopA);
                    FREE(strlen(markType)+1, "text");
                    free(markType);
                    if ((diffStopA <= 760) && (diffAspectStop <= 66)) { // changed from 682 to 760, for broadcast length from info file too long
                        // changed from 39 to 40 to 66, for longer ad found between broadcasts
                        dsyslog("cMarkAdStandalone::CheckStop(): there is an advertising before aspect ratio change, use stop mark (%d) before as end mark", stopBefore->position);
                        end = stopBefore;
                        // cleanup possible info logo or logo detection failure short before end mark
                        if (end->type == MT_LOGOSTOP) marks.DelFromTo(end->position - (60 * macontext.Video.Info.framesPerSecond), end->position - 1, MT_LOGOCHANGE);
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_ASPECTSTOP mark found");
    }

// try MT_HBORDERSTOP
    if ((criteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) ||   // try hborder end if hborder used even if we got another end mark, maybe we found a better one
            (!end && (criteria.GetMarkTypeState(MT_HBORDERCHANGE) >= CRITERIA_UNKNOWN))) {
        cMark *hBorder = Check_HBORDERSTOP();
        if (hBorder) end = hBorder;  // do not override an existing end mark with NULL
    }
    // cleanup all marks after hborder start from next broadcast
    if (!end && (criteria.GetMarkTypeState(MT_HBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
        cMark *hBorderStart = marks.GetNext(iStopA - (60 * macontext.Video.Info.framesPerSecond), MT_HBORDERSTART);
        if (hBorderStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after hborder start (%d) from next broadcast", hBorderStart->position);
            marks.DelTill(hBorderStart->position, false);
        }
    }

// try MT_VBORDERSTOP
    if (!end && (criteria.GetMarkTypeState(MT_VBORDERCHANGE) >= CRITERIA_UNKNOWN)) end = Check_VBORDERSTOP();

// try MT_LOGOSTOP
    if (!end && (criteria.GetMarkTypeState(MT_LOGOCHANGE) >= CRITERIA_UNKNOWN)) end = Check_LOGOSTOP();
    // detect very short channel start before, this is start from next broadcast
    if (end && (criteria.GetMarkTypeState(MT_CHANNELCHANGE) < CRITERIA_USED)) {
        cMark *prevChannelStart = marks.GetPrev(end->position, MT_CHANNELSTART);
        if (prevChannelStart) {
            int deltaChannelStart = 1000 * (end->position - prevChannelStart->position) / macontext.Video.Info.framesPerSecond;
            if (deltaChannelStart <= 1000) {
                dsyslog("cMarkAdStandalone::CheckStop(): channel start mark (%d) %dms before logo end mark (%d) is start of next broadcast, delete mark", prevChannelStart->position, deltaChannelStart, end->position);
                marks.Del(prevChannelStart->position);
            }
        }
    }

// no end mark found, try if we can use a start mark of next bradcast as end mark
    // no valid stop mark found, try if there is a MT_CHANNELSTART from next broadcast
    if (!end && (criteria.GetMarkTypeState(MT_CHANNELCHANGE) != CRITERIA_USED)) {
        // not possible is we use channel mark in this broadcast
        cMark *channelStart = marks.GetNext(iStopA, MT_CHANNELSTART);
        if (channelStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): use channel start mark (%d) from next broadcast as end mark", channelStart->position);
            marks.ChangeType(channelStart, MT_STOP);
            end = channelStart;
        }
    }
    // try to get hborder start mark from next broadcast as stop mark
    if (!end) {
        cMark *hBorderStart = marks.GetNext((iStopA - (4 *  macontext.Video.Info.framesPerSecond)), MT_HBORDERSTART);  // accept 4s before iStopA
        if (hBorderStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): found hborder start mark (%d) from next broadcast at end of recording", hBorderStart->position);
            cMark *prevMark = marks.GetPrev(hBorderStart->position, MT_ALL);
            if ((prevMark->type & 0x0F) == MT_START) {
                dsyslog("cMarkAdStandalone::CheckStop(): start mark (%d) before found, use hborder start mark (%d) from next broadcast as end mark", prevMark->position, hBorderStart->position);
                if (criteria.GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_USED) {  // if we use logo marks, there must be a valid logo stop mark before hborder start
                    cMark *lStop = marks.GetPrev(hBorderStart->position, MT_LOGOSTOP);  // try to find a early logo stop, maybe too long broadcast from info fileA
                    if (lStop) {
                        int diffAssumed = (iStopA - lStop->position) / macontext.Video.Info.framesPerSecond;
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

// try black screen mark as end mark
    if (!end) {
        cMark *blackEnd = blackMarks.GetAround(1 * 60 * macontext.Video.Info.framesPerSecond, iStopA, MT_NOBLACKSTOP);
        if (blackEnd) {
            int diff = (blackEnd->position - iStopA) / macontext.Video.Info.framesPerSecond;
            cMark *blackStop  = blackEnd;
            cMark *blackStart = blackMarks.GetNext(blackEnd->position, MT_NOBLACKSTART);
            while (true) {
                if (!blackStop || !blackStart) break;
                if ((blackStart->position - blackStop->position) > 1) break;
                blackStop                 = blackMarks.GetNext(blackStop->position, MT_NOBLACKSTOP);
                if (blackStop) blackStart = blackMarks.GetNext(blackStop->position, MT_NOBLACKSTART);
            }
            if (blackStop) {
                char *comment = NULL;
                if (asprintf(&comment, "end   black screen (%d)*", blackStop->position) == -1) comment = NULL;
                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                }
                end = marks.Add(MT_NOBLACKSTOP, MT_UNDEFINED, MT_UNDEFINED, blackStop->position, comment, false);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
                dsyslog("cMarkAdStandalone::CheckStop(): black screen end mark (%d) %ds after assumed stop (%d)", end->position, diff, iStopA);
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no black screen end mark found near assumed stop (%d)", iStopA);
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


    if (!end) {  // no end mark found at all, set end mark to assumed end
        dsyslog("cMarkAdStandalone::CheckStop(): no stop mark found, add end mark at assumed end (%d)", iStopA);
        sMarkAdMark mark = {};
        mark.position = iStopA;  // we are lost, add a end mark at assumed end
        mark.type     = MT_ASSUMEDSTOP;
        AddMark(&mark);
        end = marks.Get(iStopA);
    }


    // delete all marks after end mark
    if (end) { // be save, if something went wrong end = NULL
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

    // cleanup detection failures (e.g. very long dark scenes)
    if (criteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelType(MT_HBORDERCHANGE, 0xF0);
    if (criteria.GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelType(MT_VBORDERCHANGE, 0xF0);

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
    dsyslog("cMarkAdStandalone::MoveLastStopAfterClosingCredits(): check closing credits after position (%d)", stopMark->position);

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &criteria, ptr_cDecoder, recordingIndexMark, NULL);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    int endPos = stopMark->position + (25 * macontext.Video.Info.framesPerSecond);  // try till 25s after stopMarkPosition
    int newPosition = -1;
    if (ptr_cDetectLogoStopStart->Detect(stopMark->position, endPos)) {
        newPosition = ptr_cDetectLogoStopStart->ClosingCredit();
    }

    FREE(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");
    delete ptr_cDetectLogoStopStart;

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


// remove stop/start logo mark pair if it detecs a part in the broadcast with logo changes
// some channel e.g. TELE5 plays with the logo in the broadcast
//
void cMarkAdStandalone::RemoveLogoChangeMarks() {  // for performance reason only known and tested channels
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): start detect and remove logo stop/start mark pairs with special logo");

    if (!evaluateLogoStopStartPair) {
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair();
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }
    evaluateLogoStopStartPair->CheckLogoStopStartPairs(&macontext, &marks, &blackMarks, iStart, chkSTART, iStopA);

    char *indexToHMSFStop      = NULL;
    char *indexToHMSFStart     = NULL;
    int stopPosition           = 0;
    int startPosition          = 0;
    int isLogoChange           = STATUS_UNKNOWN;
    int isInfoLogo             = STATUS_UNKNOWN;
    int isStartMarkInBroadcast = STATUS_UNKNOWN;

    // alloc new objects
    ptr_cDecoderLogoChange = new cDecoder(macontext.Config->threads, recordingIndexMark);
    ALLOC(sizeof(*ptr_cDecoderLogoChange), "ptr_cDecoderLogoChange");
    ptr_cDecoderLogoChange->DecodeDir(directory);

    cExtractLogo *ptr_cExtractLogoChange = new cExtractLogo(&macontext, &criteria, macontext.Video.Info.AspectRatio, recordingIndexMark);
    ALLOC(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &criteria, ptr_cDecoderLogoChange, recordingIndexMark, evaluateLogoStopStartPair);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    // loop through all logo stop/start pairs
    int endRange = 0;  // if we are called by CheckStart, get all pairs to detect at least closing credits
    if (iStart == 0) endRange = iStopA - (27 * macontext.Video.Info.framesPerSecond); // if we are called by CheckStop, get all pairs after this frame to detect at least closing credits
    // changed from 26 to 27
    while (evaluateLogoStopStartPair->GetNextPair(&stopPosition, &startPosition, &isLogoChange, &isInfoLogo, &isStartMarkInBroadcast, endRange)) {
        if (!marks.Get(startPosition) || !marks.Get(stopPosition)) continue;  // at least one of the mark from pair was deleted, nothing to do
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
        if (indexToHMSFStop) {
            ALLOC(strlen(indexToHMSFStop)+1, "indexToHMSF");
        }

        indexToHMSFStart = marks.IndexToHMSF(startPosition);
        if (indexToHMSFStart) {
            ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF");
        }

        if (indexToHMSFStop && indexToHMSFStart) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%d) at %s and logo start (%d) at %s, isInfoLogo %d", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart, isInfoLogo);
        }
        if (ptr_cDetectLogoStopStart->Detect(stopPosition, startPosition)) {
            bool doInfoCheck = true;
            // check for closing credits if no other checks will be done, only part of the loop elements in recording end range
            if ((isInfoLogo <= STATUS_NO) && (isLogoChange <= STATUS_NO)) ptr_cDetectLogoStopStart->ClosingCredit();

            // check for info logo if  we are called by CheckStart and we are in broadcast
            if ((iStart > 0) && evaluateLogoStopStartPair->IntroductionLogoChannel(macontext.Info.ChannelName) && (isStartMarkInBroadcast == STATUS_YES)) {
                // do not delete info logo, it can be introduction logo, it looks the same
                // expect we have another start very short before
                cMark *lStartBefore = marks.GetPrev(stopPosition, MT_LOGOSTART);
                if (lStartBefore) {
                    int diffStart = 1000 * (stopPosition - lStartBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): logo start (%d) %dms before stop mark (%d)", lStartBefore->position, diffStart, stopPosition);
                    if (diffStart > 1240) {  // do info logo check if we have a logo start mark short before, some channel send a early info log after broadcast start
                        // changed from 1160 to 1240
                        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): do not check for info logo, we are in start range, it can be introducion logo");
                        doInfoCheck = false;
                    }
                }
            }
            if (doInfoCheck && (isInfoLogo >= STATUS_UNKNOWN) && ptr_cDetectLogoStopStart->IsInfoLogo()) {
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
                if (macontext.Video.Info.interlaced) aMark->position = aMark->position - 2;
                else                                 aMark->position = aMark->position - 1;
            }
            else aMark->position = recordingIndexMark->GetIFrameBefore(aMark->position - 1);
        }
        else {
            if (aMark->type == MT_ASPECTSTOP) {
                aMark->type = MT_ASPECTSTART;
                if (macontext.Config->fullDecode) {
                    if (macontext.Video.Info.interlaced) aMark->position = aMark->position + 3;  // one full picture forward and the îget the next half picture for full decode
                    else                                 aMark->position = aMark->position + 1;
                }
                else aMark->position = recordingIndexMark->GetIFrameAfter(aMark->position + 1);
            }
        }
        aMark = aMark->Next();
    }
    dsyslog("cMarkAdStandalone::SwapAspectRatio(): new aspect ratio %d:%d, fixed marks are:", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    DebugMarks();     //  only for debugging
}


cMark *cMarkAdStandalone::Check_CHANNELSTART() {
    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): search for channel start mark");
    if (macontext.Audio.Options.ignoreDolbyDetection) {
        isyslog("AC3 channel detection disabled from logo");
        return NULL;
    }

    cMark *channelStart = NULL;
    // check audio streams
    for (short int stream = 0; stream < MAXSTREAMS; stream++) {
        if ((macontext.Info.Channels[stream] > 0) && (macontext.Audio.Info.Channels[stream] > 0) && (macontext.Info.Channels[stream] != macontext.Audio.Info.Channels[stream])) {
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

        if (macontext.Info.Channels[stream] > 0) {
            isyslog("audio with %d channels in stream %d", macontext.Info.Channels[stream], stream);
            if (!channelStart && macontext.Info.Channels[stream] == 6) {
                criteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE);
                if (macontext.Audio.Info.channelChange) {
                    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): channel change detected");
                    // we have a channel change, cleanup border and aspect ratio
                    video->ClearBorder();
                    marks.DelType(MT_ASPECTCHANGE, 0xF0);

                    // start mark must be around iStartA
                    channelStart = marks.GetAround(360 * macontext.Video.Info.framesPerSecond, iStartA, MT_CHANNELSTART);

                    if (!channelStart) {          // previous recording had also 6 channels, try other marks
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): no audio channel start mark found");
                        return NULL;
                    }

                    int diffAssumed = (channelStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): audio channel start mark found at (%d) %ds after assumed start", channelStart->position, diffAssumed);
                    if (channelStart->position > iStopA) {  // this could be a very short recording, 6 channel is in post recording
                        dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): audio channel start mark after assumed stop mark not valid");
                        return NULL;
                    }

                    // for early channel start mark, check if there is a logo start mark stop/start pair near assumed start
                    // this can happen if previous broadcast has also 6 channel
                    if (diffAssumed <= -121) {
                        cMark *logoStop = marks.GetNext(channelStart->position, MT_LOGOSTOP);
                        if (logoStop) {
                            cMark *logoStart = marks.GetNext(logoStop->position, MT_LOGOSTART);
                            if (logoStart) {
                                int diffLogoStart = (logoStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                                dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): found logo start mark (%d) %ds after assumed start", logoStart->position, diffLogoStart);
                                if ((diffLogoStart >= -1) && (diffLogoStart <= 1)) {
                                    dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): use logo start mark (%d) as start mark", logoStart->position);
                                    criteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
                                    return logoStart;
                                }
                            }
                        }
                    }
                }
                else dsyslog("cMarkAdStandalone::Check_CHANNELSTART(): no audio channel change found till now, do not disable logo/border/aspect detection");
            }
        }
    }

    // now we have a final channel start mark
    if (channelStart) {
        marks.DelType(MT_LOGOCHANGE,    0xF0);
        marks.DelType(MT_HBORDERCHANGE, 0xF0);
        marks.DelType(MT_VBORDERCHANGE, 0xF0);
        marks.DelWeakFromTo(0, INT_MAX, MT_CHANNELCHANGE); // we have a channel start mark, delete all weak marks
        criteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
    }
    return channelStart;
}



cMark *cMarkAdStandalone::Check_LOGOSTART() {
    cMark *begin = NULL;

    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): search for logo start mark");
    RemoveLogoChangeMarks();

    // cleanup invalid logo start marks
    cMark *lStart = marks.GetFirst();
    while (lStart) {
        if (lStart->type == MT_LOGOSTART) {
            bool delMark = false;
            // remove very early logo start marks, this can be delayed logo start detection
            int diff = lStart->position / macontext.Video.Info.framesPerSecond;
            if (diff <= 10) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start (%5d) %ds after recording start too early", lStart->position, diff);
                delMark = true;
            }
            else {
                // remove very short logo start/stop pairs, this, is a false positive logo detection
                cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);
                if (lStop) {
                    diff = 1000 * (lStop->position - lStart->position) / macontext.Video.Info.framesPerSecond;
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
    cMark *lStartAssumed = marks.GetAround(iStartA + (420 * macontext.Video.Info.framesPerSecond), iStartA, MT_LOGOSTART);
    if (!lStartAssumed) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): no logo start mark found");
        return NULL;
    }

    // try to select best logo end mark based on closing credits follow
    if (evaluateLogoStopStartPair) {
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): search for best logo start mark based on closing credits before logo start");
        // search from nearest logo start mark to end
        lStart = lStartAssumed;
        while (!begin && lStart) {
            int status = evaluateLogoStopStartPair->GetIsClosingCreditsBefore(lStart->position);
            int diffAssumed = (lStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d): %ds after assumed start (%d), closing credits status %d", lStart->position, diffAssumed, iStartA, status);
            if (diffAssumed >= 300) break;
            if (status == STATUS_YES) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) closing credits before, valid start mark found", lStart->position);
                begin = lStart;
                break;
            }
            lStart = marks.GetNext(lStart->position, MT_LOGOSTART);
        }
        // search before nearest logo stop mark
        lStart = lStartAssumed;
        while (!begin) {
            lStart = marks.GetPrev(lStart->position, MT_LOGOSTART);
            if (!lStart) break;
            int status = evaluateLogoStopStartPair->GetIsClosingCreditsBefore(lStart->position);
            int diffAssumed = (iStartA - lStart->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d): %ds before assumed start (%d), closing credits status %d", lStart->position, diffAssumed, iStartA, status);
            if (diffAssumed >= 300) break;
            if (status == STATUS_YES) {
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): start mark (%d) closing credits before, valid start mark found", lStart->position);
                begin = lStart;
            }
        }
    }

    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check for logo start mark with black screen, silence separator or closing credits");
    lStart = lStartAssumed;
    while (!begin && lStart) {
        int diffAssumed = (lStart->position - iStartA)          / macontext.Video.Info.framesPerSecond;
#define MAX_AFTER_ASSUMED 266    // changed from 239 to 266
        LogSeparator();
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): check logo start mark (%d), %ds (<= %ds) after assumed start", lStart->position, diffAssumed, MAX_AFTER_ASSUMED);
        if (diffAssumed > MAX_AFTER_ASSUMED) {
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
    if (begin) CleanupUndetectedInfoLogo(begin);  // strong broadcast start found, cleanup undetected info logos, introduction logos short after final start mark
    LogSeparator();


    lStart = lStartAssumed;
    while (!begin && lStart) {
        // check for too early, can be start of last part from previous broadcast
        int diffAssumed = (iStartA - lStart->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds before assumed start (%d)", lStart->position, diffAssumed, iStartA);
        if (diffAssumed >= 124) {  // changed from 132 to 124
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds before assumed start too early", lStart->position, diffAssumed);
            cMark *lNext = marks.GetNext(lStart->position, MT_LOGOSTART);  // get next logo start mark
            marks.Del(lStart);
            lStart = lNext;
            continue;
        }
        // check for too late logo start, can be of first ad
        if (diffAssumed <= -296) {  // not more then 296s after assumed start, later start mark can be start of first ad
            dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo start mark (%d) %ds after assumed start too late", lStart->position, -diffAssumed);
            break;
        }
        else {
            begin = lStart;  // start with nearest start mark to assumed start
        }

        // check next logo stop/start pair
        cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);  // get next logo stop mark
        if (lStop) {  // there is a next stop mark in the start range
            int distanceStartStop = (lStop->position - lStart->position) / macontext.Video.Info.framesPerSecond;
            if (distanceStartStop < 20) {  // very short logo part, lStart is possible wrong, do not increase, first ad can be early
                // change from 55 to 20 because of too short logo change detected about 20s after start mark
                dsyslog("cMarkAdStandalone::Check_LOGOSTART(): next logo stop mark found very short after start mark on position (%d), distance %ds", lStop->position, distanceStartStop);
                cMark *lNextStart = marks.GetNext(lStop->position, MT_LOGOSTART); // get next logo start mark
                if (lNextStart) {  // now we have logo start/stop/start, this can be a preview before broadcast start
                    int distanceStopNextStart = (lNextStart->position - lStop->position) / macontext.Video.Info.framesPerSecond;
                    if (distanceStopNextStart > 1) {        // very short stop/start can be undetected info logo
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
                    else {
                        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): found start mark (%d) %ds after logo start/stop marks, length too small, delete marks", lNextStart->position, distanceStopNextStart);
                        marks.Del(lStop);
                        marks.Del(lNextStart);
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
        return NULL;
    }

    // valid logo start mark found
    dsyslog("cMarkAdStandalone::Check_LOGOSTART(): found logo start mark (%d)", begin->position);
    marks.DelWeakFromTo(0, INT_MAX, MT_LOGOCHANGE);   // maybe the is a assumed start from converted channel stop
    if ((criteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) ||
            (criteria.GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_USED) ||
            (criteria.GetMarkTypeState(MT_ASPECTCHANGE)  == CRITERIA_USED) ||
            (criteria.GetMarkTypeState(MT_CHANNELCHANGE) == CRITERIA_USED)) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): stronger marks are set for detection, use logo mark only for start mark, delete logo marks after (%d)", begin->position);
        marks.DelFromTo(begin->position + 1, INT_MAX, MT_LOGOCHANGE);
    }
    else {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): logo marks set for detection, cleanup late hborder and vborder stop marks from previous broadcast");
        const cMark *delMark = marks.GetAround(10 * macontext.Video.Info.framesPerSecond, begin->position, MT_VBORDERSTOP, 0xFF);
        if (delMark) marks.Del(delMark->position);
        delMark = marks.GetAround(10 * macontext.Video.Info.framesPerSecond, begin->position, MT_HBORDERSTOP, 0xFF);
        if (delMark) marks.Del(delMark->position);
        criteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_USED);
    }
    if (!criteria.LogoInBorder(macontext.Info.ChannelName)) {
        dsyslog("cMarkAdStandalone::Check_LOGOSTART(): disable border detection and delete border marks");  // avoid false detection of border
        marks.DelType(MT_HBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        marks.DelType(MT_VBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
        criteria.SetDetectionState(MT_HBORDERCHANGE, false);
        criteria.SetDetectionState(MT_VBORDERCHANGE, false);
    }
    return begin;
}

#define IGNORE_AT_START 12   // ignore this number of frames at the start for start marks, they are initial marks from recording before, changed from 11 to 12

cMark *cMarkAdStandalone::Check_HBORDERSTART() {
    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): search for hborder start mark");
    cMark *hStart = marks.GetAround(240 * macontext.Video.Info.framesPerSecond, iStartA, MT_HBORDERSTART);
    if (hStart) { // we found a hborder start mark
        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start found at (%d)", hStart->position);

        // check if first broadcast is long enough
        cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
        if (hStop) {
            int lengthBroadcast = (hStop->position - hStart->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): next horizontal border stop mark (%d), length of broadcast %ds", hStop->position, lengthBroadcast);
            const cMark *hNextStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
            if ((((hStart->position == 0) || (lengthBroadcast <= 291)) && !hNextStart) || // hborder preview or hborder brodcast before broadcast start, changed from 235 to 291
                    (lengthBroadcast <=  74)) {                                           // very short broadcast length is never valid
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border start (%d) and stop (%d) mark from previous recording, delete all marks from up to hborder stop", hStart->position, hStop->position);
                // delete hborder start/stop marks because we ignore hborder start mark
                marks.DelTill(hStop->position, true);
                return NULL;
            }
        }

        // check hborder start position
        if (hStart->position >= IGNORE_AT_START) {  // position < IGNORE_AT_START is a hborder start from previous recording
            // found valid horizontal border start mark
            criteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
            // we found a hborder, check logo stop/start after to prevent to get closing credit from previous recording as start
            cMark *logoStop  = marks.GetNext(hStart->position, MT_LOGOSTOP);        // logo stop mark can be after hborder start
            if (!logoStop) logoStop = marks.GetPrev(hStart->position, MT_LOGOSTOP); //                   or before hborder start
            cMark *logoStart = marks.GetNext(hStart->position, MT_LOGOSTART);
            if (logoStop && logoStart && (logoStart->position > logoStop->position)) {
                int diffStop  = (logoStop->position  - hStart->position) / macontext.Video.Info.framesPerSecond;
                int diffStart = (logoStart->position - hStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): found logo stop (%d) %ds and logo start (%d) %ds after hborder start (%d)", logoStop->position, diffStop, logoStart->position, diffStart, hStart->position);
                if ((diffStop >= -1) && (diffStop <= 13) && (diffStart <= 17)) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): hborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", hStart->position, logoStart->position);
                    marks.Del(hStart->position);
                    hStart = logoStart;
                }
            }
            if (hStart->type != MT_LOGOSTART) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete logo marks if any");
                marks.DelType(MT_LOGOCHANGE, 0xF0);
            }
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete vborder marks if any");
            marks.DelType(MT_VBORDERCHANGE, 0xF0);
        }
        else {
            dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete too early horizontal border mark (%d)", hStart->position);
            marks.Del(hStart->position);
            hStart = NULL;
            if (marks.Count(MT_HBORDERCHANGE, 0xF0) == 0) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): horizontal border since start, use it for mark detection");
                criteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
                if (!criteria.LogoInBorder(macontext.Info.ChannelName)) {
                    dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): logo marks can not be valid, delete it");
                    marks.DelType(MT_LOGOCHANGE, 0xF0);
                }
            }
        }
    }
    else { // we found no hborder start mark
        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): no horizontal border start mark found, disable horizontal border detection");
        criteria.SetDetectionState(MT_HBORDERCHANGE, false);
        dsyslog("cMarkAdStandalone::Check_HBORDERSTART(): delete horizontal border marks, if any");
        marks.DelType(MT_HBORDERCHANGE, 0xF0);  // mybe the is a late invalid hborder start marks, exists sometimes with old vborder recordings
        return NULL;
    }
    return hStart;
}

cMark *cMarkAdStandalone::Check_VBORDERSTART(const int maxStart) {
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): search for vborder start mark");
    cMark *vStart = marks.GetAround(240 * macontext.Video.Info.framesPerSecond + iStartA, iStartA, MT_VBORDERSTART);
    if (!vStart) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no vertical border at start found, ignore vertical border detection");
        criteria.SetDetectionState(MT_VBORDERCHANGE, false);
        marks.DelType(MT_VBORDERSTART, 0xFF);  // maybe we have a vborder start from a preview or in a doku, delete it
        const cMark *vStop = marks.GetAround(240 * macontext.Video.Info.framesPerSecond + iStartA, iStartA, MT_VBORDERSTOP);
        if (vStop) {
            int pos = vStop->position;
            char *comment = NULL;
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
            marks.Del(pos);
            if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, pos, comment);
            if (comment) {
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        return NULL;
    }

    // found vborder start
    dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border start found at (%d)", vStart->position);
    cMark *vStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
    if (vStop) {
        cMark *vNextStart = marks.GetNext(vStop->position, MT_VBORDERSTART);
        int markDiff = (vStop->position - vStart->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop found at (%d), %ds after vertical border start", vStop->position, markDiff);
        if (vNextStart) {
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border start (%d) after vertical border stop (%d) found, start mark at (%d) is valid", vNextStart->position, vStop->position, vStart->position);
        }
        else { // we have only start/stop vborder in start part, this is from broadcast before
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): no vertical border start found after start (%d) and stop (%d)", vStart->position, vStop->position);
            if ((vStart->position < IGNORE_AT_START) && (markDiff <= 140)) {  // vbordet start/stop from previous broadcast
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border stop at (%d) %ds after vertical border start (%d) in start part found, this is from previous broadcast, delete marks", vStop->position, markDiff, vStart->position);
                criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);
                marks.Del(vStop);
                marks.Del(vStart);
                return NULL;
            }
        }
    }

    // prevent to get start of next broadcast as start of this very short broadcast
    if (vStart->position > maxStart) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vborder start mark (%d) after max start mark (%d) is invalid", vStart->position, maxStart);
        return NULL;
    }

    if (vStart->position < IGNORE_AT_START) { // early position is a vborder from previous recording
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): delete too early vertical border start found at (%d)", vStart->position);
        const cMark *vBorderStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);
        marks.Del(vStart->position);
        if (!vBorderStop || (vBorderStop->position > iStart + 420 * macontext.Video.Info.framesPerSecond)) {
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vertical border since start, use it for mark detection");
            criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);
            if (!criteria.LogoInBorder(macontext.Info.ChannelName)) {
                dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): logo marks can not be valid, delete it");
                marks.DelType(MT_LOGOCHANGE, 0xF0);
            }
        }
        return NULL;
    }

    // found valid vertical border start mark
    if (criteria.GetMarkTypeState(MT_ASPECTCHANGE) == CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): use vertical border only as start mark, keep using aspect ratio detection");
        criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE);
    }
    else criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);

    // check logo stop after vborder stop to prevent to get closing credit from previous recording as start mark
    cMark *logoStop  = marks.GetNext(vStart->position, MT_LOGOSTOP);
    if (logoStop) {
        int diffStop  = (logoStop->position  - vStart->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): found logo stop (%d) %ds after vborder start (%d)", logoStop->position, diffStop, vStart->position);
        if ((diffStop <= 51)) {  // changed from 25 to 51
            dsyslog("cMarkAdStandalone::Check_VBORDERSTART(): vborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", vStart->position, logoStop->position);
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
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", frameCurrent, chkSTART);
    int maxStart = iStartA + (length * macontext.Video.Info.framesPerSecond / 2);  // half of recording
    char *indexToHMSFStart = marks.IndexToHMSF(iStartA);
    if (indexToHMSFStart) {
        ALLOC(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %d at %s, max allowed start frame (%d)", iStartA, indexToHMSFStart, maxStart);
        FREE(strlen(indexToHMSFStart) + 1, "indexToHMSFStart");
        free(indexToHMSFStart);
    }
    DebugMarks();     //  only for debugging

    // set initial mark criteria
    if (marks.Count(MT_HBORDERSTART) == 0) criteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no hborder start, broadcast can not have hborder
    else if ((marks.Count(MT_HBORDERSTART) == 1) && (marks.Count(MT_HBORDERSTOP) == 0)) criteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

    if (marks.Count(MT_VBORDERSTART) == 0) criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no vborder start, broadcast can not have vborder
    else if ((marks.Count(MT_VBORDERSTART) == 1) && (marks.Count(MT_VBORDERSTOP) == 0)) criteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

// recording start
    cMark *begin = marks.GetAround(iStartA, 1, MT_RECORDINGSTART);  // do we have an incomplete recording ?
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
    if (!begin) begin = Check_CHANNELSTART();

// check if aspect ratio from VDR info file is valid
    dsyslog("cMarkAdStandalone::CheckStart(): check aspect ratio from VDR info file");
    if ((macontext.Info.AspectRatio.num == 0) || (macontext.Video.Info.AspectRatio.den == 0)) {
        isyslog("no video aspect ratio found in vdr info file, assume 16:9");
        macontext.Info.AspectRatio.num = 16;
        macontext.Info.AspectRatio.den =  9;
    }
    // end of start part can not be 4:3 if broadcast is 16:9
    if ((macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9) &&
            (macontext.Video.Info.AspectRatio.num == 4) && (macontext.Video.Info.AspectRatio.den == 3)) {
        dsyslog("cMarkAdStandalone::CheckStart(): broadcast at end of start part is 4:3, VDR info tells 16:9, info file is wrong");
        SwapAspectRatio();
        macontext.Info.checkedAspectRatio = true;  // now we are sure, aspect ratio is correct
    }
    // very short start/stop pairs (broadcast) are impossible, these must be stop/start (ad) pairs
    if (!macontext.Info.checkedAspectRatio) {
        cMark *aspectStart = marks.GetNext(-1, MT_ASPECTSTART); // first start can be on position 0
        if (aspectStart) {
            cMark *aspectStop = marks.GetNext(aspectStart->position, MT_ASPECTSTOP);
            if (aspectStop) {
                int diff    = (aspectStop->position - aspectStart->position) / macontext.Video.Info.framesPerSecond;
                int iStartS = iStart / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start (%d) stop (%d): length %ds", aspectStart->position, aspectStop->position, diff);
                if (diff <= 60) {
                    dsyslog("cMarkAdStandalone::CheckStart(): length %ds for first broadcast too short, pre recording time is %ds, VDR info file must be wrong", diff, iStartS);
                    SwapAspectRatio();
                    macontext.Info.checkedAspectRatio = true;  // now we are sure, aspect ratio is correct
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
    if (!macontext.Info.checkedAspectRatio && (macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
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
                        macontext.Info.checkedAspectRatio = true;  // now we are sure, aspect ratio is correct
                    }
                }
            }
        }
    }
    if (!macontext.Info.checkedAspectRatio) macontext.Info.checkedAspectRatio = true;  // now we are sure, aspect ratio is correct

// aspect ratio start
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for aspect ratio start mark");
        // search for aspect ratio start mark
        cMark *aStart = marks.GetAround(480 * macontext.Video.Info.framesPerSecond, iStartA, MT_ASPECTSTART);
        if (aStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio start mark at (%d), video info is %d:%d", aStart->position, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) { // we have a aspect ratio start mark, check if valid
                criteria.SetMarkTypeState(MT_ASPECTCHANGE, CRITERIA_USED);  // use aspect ratio marks for detection, even if we have to use another start mark
                while (aStart && (aStart->position <= (16 * macontext.Video.Info.framesPerSecond))) {    // to near at start of recording is from broadcast before
                    dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start mark (%d) too early, try next", aStart->position);
                    aStart = marks.GetNext(aStart->position, MT_ASPECTSTART);
                    if (aStart && aStart->position > (iStartA +  (300 * macontext.Video.Info.framesPerSecond))) aStart = NULL; // too late, this can be start of second part
                }
                if (aStart) {
                    begin = aStart;
                    dsyslog("cMarkAdStandalone::CheckStart(): valid aspect ratio start mark (%d) found", aStart->position);
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
    if (macontext.Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264) isyslog("HD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    else isyslog("SD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);

    // before we can check border marks, we have to check with is valid
    if (macontext.Video.Info.frameDarkOpeningCredits >= 0) { // check for very long dark opening credits
        dsyslog("cMarkAdStandalone::CheckStart(): found very long dark opening credits start at frame (%d), check which type of border mark is valid", macontext.Video.Info.frameDarkOpeningCredits);
        const cMark *hStop  = marks.GetNext(iStartA, MT_HBORDERSTOP);
        const cMark *hStart = NULL;
        if (hStop) hStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
        const cMark *vStop  = marks.GetNext(iStartA, MT_VBORDERSTOP);
        if (hStop && !hStart && !vStop) {
            dsyslog("cMarkAdStandalone::CheckStart(): hborder stop and no hborder start found but no vborder stop, recording has vborder");
            dsyslog("cMarkAdStandalone::CheckStart(): change hborder start to vborder start at (%d), delete all hborder marks", macontext.Video.Info.frameDarkOpeningCredits);
            marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
            marks.Add(MT_VBORDERSTART, MT_UNDEFINED, MT_UNDEFINED, macontext.Video.Info.frameDarkOpeningCredits, "start of opening credits", true);
        }
        macontext.Video.Info.frameDarkOpeningCredits = 0; // reset state for long dark opening credits of next braodcast
    }

// horizontal border start
    if (!begin) begin = Check_HBORDERSTART();

// vertical border start
    if (!begin) begin = Check_VBORDERSTART(maxStart);

// try logo start mark
    if (!begin) begin = Check_LOGOSTART();

    // drop too early marks of all types
    if (begin && (begin->type != MT_RECORDINGSTART) && (begin->position <= IGNORE_AT_START)) {  // first frames are from previous recording
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

// try black screen as start mark
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for end of black screen as start mark");
        const cMark *noBlackStart = blackMarks.GetAround(120 * macontext.Video.Info.framesPerSecond, iStartA, MT_NOBLACKSTART);
        if (noBlackStart) {
            char *comment = NULL;
            if (asprintf(&comment, "start black screen (%d)*", noBlackStart->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            begin = marks.Add(MT_NOBLACKSTART, MT_UNDEFINED, MT_UNDEFINED, noBlackStart->position, comment, false);
            if (comment) {
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
            dsyslog("cMarkAdStandalone::CheckStart(): found end of black screen as start mark (%d)", begin->position);
        }
    }

// no mark found, try anything
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for any start mark");
        marks.DelTill(IGNORE_AT_START);    // we do not want to have a initial mark from previous recording as a start mark
        begin = marks.GetAround(240 * macontext.Video.Info.framesPerSecond, iStartA, MT_START, 0x0F);  // not too big search range
        if (begin) {
            dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) type 0x%X after search for any type", begin->position, begin->type);
            if ((begin->type == MT_ASSUMEDSTART) || (begin->inBroadCast) || !criteria.GetDetectionState(MT_LOGOCHANGE)) { // test on inBroadCast because we have to take care of black screen marks in an ad, MT_ASSUMEDSTART is from converted channel stop of previous broadcast
                const char *indexToHMSF = marks.GetTime(begin);
                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%i) type 0x%X at %s inBroadCast %i", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
            }
            else { // mark in ad
                const char *indexToHMSF = marks.GetTime(begin);
                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): start mark found but not inBroadCast (%i) type 0x%X at %s inBroadCast %i, ignoring", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                begin = NULL;
            }
        }
    }

    if (begin && ((begin->position  / macontext.Video.Info.framesPerSecond) < 1) && (begin->type != MT_RECORDINGSTART)) { // do not accept marks in the first second, the are from previous recording, expect manual set MT_RECORDINGSTART fpr missed recording start
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

    // still no start mark found, try hborder stop marks from previous broadcast
    if (!begin) { // try hborder stop mark as start mark
        cMark *hborderStop = marks.GetNext(0, MT_HBORDERSTOP);
        if (hborderStop) {
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP (%d) from previous recoring as start mark", hborderStop->position);
            begin = marks.ChangeType(hborderStop, MT_START);
            marks.DelTill(begin->position);
        }
    }

// try lower black border mark, this is end of previous recording
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for end of lower black border as start mark");
        const cMark *noLowerBlackStart = blackMarks.GetAround(120 * macontext.Video.Info.framesPerSecond, iStartA, MT_NOBLACKLOWERSTART);
        if (noLowerBlackStart) {
            begin = marks.Add(MT_NOBLACKLOWERSTART, MT_UNDEFINED, MT_UNDEFINED, noLowerBlackStart->position, "end   lower black border", false);
            dsyslog("cMarkAdStandalone::CheckStart(): found end of lower black border from previous broadcast, use as start mark (%d)", begin->position);
        }
    }

    // no start mark found at all, set start after pre timer
    if (!begin) {
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
    if (countStopStart >= 4) {  // changed from 3 to 4, sometimes there are a lot of previews in start area
        isyslog("%d logo STOP/START pairs found after start mark, something is wrong with your logo", countStopStart);
        if (video->ReducePlanes()) {
            dsyslog("cMarkAdStandalone::CheckStart(): reduce logo processing to first plane and delete all marks after start mark (%d)", begin->position);
            marks.DelAfterFromToEnd(begin->position);
        }
    }

    CheckStartMark();
    LogSeparator();
    CalculateCheckPositions(marks.GetFirst()->position);
    iStart = 0;
    marks.Save(directory, &macontext, false);
    DebugMarks();     //  only for debugging
    LogSeparator();
    criteria.ListMarkTypeState();
    criteria.ListDetection();
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
            else if (mark->type == MT_LOGOSTART) minFirstBroadcast = 52; // do not increase, there are broadcasts with early first advertising, changed from 68 to 52
            // there can be short stop/start from a undetected info logo (eg RTL2)
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


void cMarkAdStandalone::CheckMarks(const int endMarkPos) {           // cleanup marks that make no sense
    LogSeparator(true);

    const cMark *firstMark = marks.GetFirst();
    if (!firstMark) {
        esyslog("no marks at all detected, something went very wrong");
        return;
    }
    int newStopA = firstMark->position + macontext.Video.Info.framesPerSecond * (length + macontext.Config->astopoffs);  // we have to recalculate iStopA
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
    const cMark *channelStart = marks.GetNext(0, MT_CHANNELSTART);
    const cMark *channelStop  = marks.GetNext(0, MT_CHANNELSTOP);
    cMark *hborderStart       = marks.GetNext(0, MT_HBORDERSTART);
    cMark *hborderStop        = marks.GetNext(0, MT_HBORDERSTOP);
    cMark *vborderStart       = marks.GetNext(0, MT_VBORDERSTART);
    cMark *vborderStop        = marks.GetNext(0, MT_VBORDERSTOP);
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
                    int prevLogoStart_Stop      = 1000 * (mark->position          - prevLogoStart->position) /  macontext.Video.Info.framesPerSecond;
                    long int stop_nextLogoStart = 1000 * (nextLogoStart->position - mark->position)          /  macontext.Video.Info.framesPerSecond;
                    int nextLogoStart_nextStop  = 1000 * (nextStop->position      - nextLogoStart->position) /  macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckMarks(): MT_LOGOSTART (%6d) -> %7dms -> MT_LOGOSTOP (%6d) -> %7ldms -> MT_LOGOSTART (%6d) -> %7dms -> MT_STOP (%6d)", prevLogoStart->position, prevLogoStart_Stop, mark->position, stop_nextLogoStart, nextLogoStart->position, nextLogoStart_nextStop, nextStop->position);
// valid short stop/start, do not delete
// MT_LOGOSTART ( 48867) ->    4880ms -> MT_LOGOSTOP ( 48989) ->     760ms -> MT_LOGOSTART ( 49008) ->  795000ms -> MT_STOP (68883)
// MT_LOGOSTART ( 51224) ->   29800ms -> MT_LOGOSTOP ( 51969) ->     920ms -> MT_LOGOSTART ( 51992) ->  622840ms -> MT_STOP (67563)
// MT_LOGOSTART ( 49708) ->  593600ms -> MT_LOGOSTOP ( 64548) ->     120ms -> MT_LOGOSTART ( 64551) ->   14880ms -> MT_STOP (64923)
// MT_LOGOSTART ( 37720) ->   26280ms -> MT_LOGOSTOP ( 38377) ->     840ms -> MT_LOGOSTART ( 38398) ->  254120ms -> MT_STOP (44751)
//
// invalid stop/start pair from logo change short after logo start mark, delete pair
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
                    if (evaluateLogoStopStartPair->IsLogoChangeChannel(macontext.Info.ChannelName) &&
                            (prevLogoStart_Stop     >= 1120) && (prevLogoStart_Stop     <=    8440) &&
                            (stop_nextLogoStart     >=  280) && (stop_nextLogoStart     <=    1120) &&
                            (nextLogoStart_nextStop >=  560) && (nextLogoStart_nextStop <= 1303560)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair too short, deleting", mark->position, nextLogoStart->position);
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
                    const cMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
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
                                if (lengthPreview <= 143) {  // changed from 120 to 143, longest preview found
                                    // check if this logo stop and next logo start are closing credits, in this case stop mark is valid
                                    bool isNextClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCreditsAfter(startAfter->position) == STATUS_YES);
                                    if (!isNextClosingCredits || (stopMark->position != marks.GetLast()->position)) { // check valid only for last mark
                                        // check if this logo start mark and previuos logo stop mark are closing credits with logo, in this case logo start mark is valid
                                        // this only work on the first logo stark mark because there are closing credits in previews
                                        bool isThisClosingCredits = evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCreditsAfter(mark->position) == STATUS_YES);
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
                        else dsyslog("cMarkAdStandalone::CheckMarks(): not long enough ad before and after preview, maybe logo detection failure");
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

    // delete late first vborder start mark, they are start of next recording
    cMark *borderStart = marks.GetNext(0, MT_VBORDERSTART);
    if ((borderStart) && (marks.Count(MT_VBORDERCHANGE, 0xF0) == 1)) {
        int diffEnd = (borderStart->position - newStopA) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::CheckMarks(): late single vborder start mark (%d) %ds after assumed stop (%d), this is start of next broadcast", borderStart->position, diffEnd, newStopA);
        if (diffEnd >= -156) { // maybe reported recording length is too big
            dsyslog("cMarkAdStandalone::CheckMarks(): delete all marks from (%d) to end", borderStart->position);
            marks.DelAfterFromToEnd(borderStart->position - 1);
        }
    }


    // delete start/stop hborder pairs before chkSTART if there are no other hborder marks, they are a preview with hborder before recording start
    mark = marks.GetFirst();
    if (mark && (mark->type == MT_HBORDERSTART) && (marks.Count(MT_HBORDERSTART) == 1)) {
        cMark *markNext = mark->Next();
        if (markNext && (markNext->type == MT_HBORDERSTOP) && (markNext->position < chkSTART) && (markNext->position != marks.GetLast()->position) && (marks.Count(MT_HBORDERSTOP) == 1)) {
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

// check for short start/stop pairs at the start part
    CheckStartMark();

// check for short stop/start logo pair at start part in case of late recording start, ad between broadcasts are short, ad in broadcast are long
// example invalid current logo start mark
// first pair:  start ( 4795), stop ( 7444), length  105s, length ad after 24s
// second pair: start ( 8066), stop (42550), length 1379s, 322s after assumed start
//
// first pair:  start ( 1587), stop ( 4212), length  105s, length ad after 28s
// second pair: start ( 4912), stop (37267), length 1294s, 196s after assumed start
//
// first pair:  start ( 1933), stop ( 4559), length  105s, length ad after 30s
// second pair: start ( 5330), stop (53354), length 1920s, 213s after assumed start
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): final logo start mark selection");
    DebugMarks();     //  only for debugging
    cMark *logoStart1 = marks.GetNext(-1, MT_LOGOSTART);
    if (logoStart1) {
        cMark *logoStop1  = marks.GetNext(logoStart1->position, MT_LOGOSTOP);
        if (logoStop1) {
            cMark *logoStart2 = marks.GetNext(logoStop1->position, MT_LOGOSTART);
            if (logoStart2) {
                cMark *allStop2 = marks.GetNext(logoStart2->position, MT_STOP, 0x0F);
                if (allStop2) {
                    int lengthBroadcast1    = (logoStop1->position  - logoStart1->position) / macontext.Video.Info.framesPerSecond;
                    int lengthAd            = (logoStart2->position - logoStop1->position)  / macontext.Video.Info.framesPerSecond;
                    int lengthBroadcast2    = (allStop2->position   - logoStart2->position) / macontext.Video.Info.framesPerSecond;
                    int newDiffAfterAssumed = (logoStart2->position - iStart)               / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckMarks(): first pair:  start (%5d), stop (%5d), length %4ds, length ad after %ds", logoStart1->position, logoStop1->position, lengthBroadcast1, lengthAd);
                    dsyslog("cMarkAdStandalone::CheckMarks(): second pair: start (%5d), stop (%5d), length %4ds, %ds after assumed start", logoStart2->position, allStop2->position, lengthBroadcast2, newDiffAfterAssumed);
                    if ((lengthBroadcast1 <= 105)  && (lengthAd <= 30) && (lengthBroadcast2 >= 1294) && (newDiffAfterAssumed <= 387)) {
                        dsyslog("cMarkAdStandalone::CheckMarks(): current start mark invalid, delete start (%d) and stop (%d) mark", logoStart1->position, logoStop1->position);
                        marks.Del(logoStart1->position);
                        marks.Del(logoStop1->position);
                    }
                }
            }
        }
    }

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
    dsyslog("cMarkAdStandalone::CheckMarks(): check for better end mark in case of recording length is too big");
    DebugMarks();     //  only for debugging

    // get last 3 marks
    cMark *lastStopMark = marks.GetLast();
    if (lastStopMark && ((lastStopMark->type & 0x0F) == MT_STOP)) {
        cMark *lastStartMark = marks.GetPrev(lastStopMark->position);
        if (lastStartMark && ((lastStartMark->type & 0x0F) == MT_START)) {
            cMark *prevStopMark = marks.GetPrev(lastStartMark->position);
            if (prevStopMark && ((prevStopMark->type & 0x0F) == MT_STOP)) {
                int lastBroadcast        = (lastStopMark->position  - lastStartMark->position) / macontext.Video.Info.framesPerSecond;
                int diffLastStopAssumed  = (lastStopMark->position  - newStopA)                / macontext.Video.Info.framesPerSecond;
                int diffLastStartAssumed = (lastStartMark->position - newStopA)                / macontext.Video.Info.framesPerSecond;
                int diffPrevStopAssumed  = (prevStopMark->position  - newStopA)                / macontext.Video.Info.framesPerSecond;
                int lastAd               = (lastStartMark->position - prevStopMark->position)  / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckMarks(): end mark          (%5d) %4ds after assumed end (%5d), last broadcast length %ds, last ad length %ds", lastStopMark->position, diffLastStopAssumed, newStopA, lastBroadcast, lastAd);
                dsyslog("cMarkAdStandalone::CheckMarks(): start mark before (%5d) %4ds after assumed end (%5d)", lastStartMark->position, diffLastStartAssumed, newStopA);
                dsyslog("cMarkAdStandalone::CheckMarks(): stop  mark before (%5d) %4ds after assumed end (%5d)", prevStopMark->position,  diffPrevStopAssumed, newStopA);

                // check length of last broadcast and distance to assumed end
                if (((lastStopMark->type & 0xF0) < MT_CHANNELCHANGE) || ((lastStopMark->type & 0xF0) == MT_MOVED)) {  // trust channel marks and better
                    int minLastStopAssumed;    // trusted distance to assumed stop depents on hardness of marks
                    int minLastStartAssumed;
                    int minPrevStopAssumed;
                    int minLastBroadcast;
                    switch(lastStopMark->type) {
                    case MT_ASSUMEDSTOP:
                        // too long broadcast length from info file, delete last stop:
                        //   0 / -184 / -631 (conflict)
                        //   0 / -231 / -355 (conflict)
                        // correct end mark, do not delete last stop
                        //   0 / -230 / -581
                        //   0 / -220 / -353
                        //   0 / -273 / -284
                        minLastStopAssumed  =    0;
                        minLastStartAssumed = -272;
                        minPrevStopAssumed  = -352;
                        minLastBroadcast    =   89;  //  shortest valid last ad 89s
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
                        minLastBroadcast    =    0;
                        break;
                    case MT_LOGOSTOP:
                        // too long broadcast length from info file, delete last stop:
                        // -12 /  -97 / -132
                        //  13 / -205 / -328  (conflict)
                        //  52 / -165 / -289
                        //  63 /  -86 / -195
                        // 106 /  -79 / -182
                        // 251 /  -54 / -496  (conflict)
                        // 306 /  -19 / -483  (conflict)
                        // correct end mark, do not delete last stop
                        //  22 / -192 / -315  NEW
                        // 176 /   30 / -449
                        // 238 /   76 / -376
                        minLastStopAssumed  =  -12;
                        minLastStartAssumed = -205;
                        minPrevStopAssumed  = -314;
                        minLastBroadcast    =   79;  // shortest last part of a broadcast with logo end mark
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

        // check for logo detection failure, delete if logo stop/start pair is too short for an ad
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int lengthAd = 1000 * (mark->Next()->position - mark->position) / macontext.Video.Info.framesPerSecond;
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

    marks.DelInvalidSequence();

    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): final marks:");
    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type, const bool isPause) {
    if (!ptr_cDecoder) return;
    int delta = macontext.Video.Info.framesPerSecond * 120;
    int vpsFrame = recordingIndexMark->GetFrameFromOffset(offset * 1000);
    if (vpsFrame < 0) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): failed to get frame from offset %d", offset);
    }
    cMark *mark = NULL;
    char *comment = NULL;
    char *timeText = NULL;
    if (!isPause) {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        }
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
                ((mark->type != MT_TYPECHANGESTOP) || (vpsFrame >= mark->position))) {        // keep broadcast start from next recording only if before VPS event
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep mark at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
        }
        else { // replace weak marks
            int diff = abs(mark->position - vpsFrame) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s, %ds after mark", mark->position, mark->type, timeText, diff);
            if (abs(diff) < 1225) {  // do not replace very far marks, there can be an invalid VPS events
                dsyslog("cMarkAdStandalone::AddMarkVPS(): move mark on position (%d) to VPS event position (%d)", mark->position, vpsFrame);
                // remove marks witch will become invalid after applying VPS event
                switch (type) {
                case MT_START:
                    marks.DelFromTo(0, mark->position - 1, 0xFF);
                    break;
                case MT_STOP: {  // delete all marks between stop mark before VPS stop event (included) and current end mark (not included)
                    int delStart = vpsFrame;
                    const cMark *prevMark = marks.GetPrev(vpsFrame);
                    if (prevMark && ((prevMark->type & 0x0F) == MT_STOP)) delStart = prevMark->position;
                    marks.DelFromTo(delStart, mark->position - 1, 0xFF);
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
    if ((macontext.Config) && (macontext.Config->logoExtraction != -1)) return;
    if (gotendmark) return;
    if (mark->type <= MT_UNDEFINED) {
        esyslog("cMarkAdStandalone::AddMark(): mark type 0x%X invalid", mark->type);
        return;
    }
    if (mark->position < 0) {
        esyslog("cMarkAdStandalone::AddMark(): mark position (%d) invalidi, type 0x%X", mark->position, mark->type);
        return;
    }

    // set comment of the new mark
    char *comment = NULL;
    switch (mark->type) {
    case MT_ASSUMEDSTART:
        if (asprintf(&comment, "start assumed (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_ASSUMEDSTOP:
        if (asprintf(&comment, "end   assumed (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SCENESTART:
        if (asprintf(&comment, "start scene (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SOUNDSTART:
        if (asprintf(&comment, "end   silence (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SOUNDSTOP:
        if (asprintf(&comment, "start silence (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_SCENESTOP:
        if (asprintf(&comment, "end   scene (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKLOWERSTART:
        if (asprintf(&comment, "end   lower black border (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKLOWERSTOP:
        if (asprintf(&comment, "start lower black border (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKSTART:
        if (asprintf(&comment, "end   black screen (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_NOBLACKSTOP:
        if (asprintf(&comment, "start black screen (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_LOGOSTART:
        if (asprintf(&comment, "start logo (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_LOGOSTOP:
        if (asprintf(&comment, "stop  logo (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_HBORDERSTART:
        if (asprintf(&comment, "start horiz. borders (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_HBORDERSTOP:
        if (asprintf(&comment, "stop  horiz. borders (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_VBORDERSTART:
        if (asprintf(&comment, "start vert. borders (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_VBORDERSTOP:
        if (asprintf(&comment, "stop  vert. borders (%d) ", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_ASPECTSTART:
        if ((mark->AspectRatioBefore.num == 0) || (mark->AspectRatioBefore.den == 0)) {
            if (asprintf(&comment, "start recording with aspect ratio %2d:%d (%d)*", mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
        }
        else {
            if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%d)*", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
            if (comment) {
                ALLOC(strlen(comment)+1, "comment");
            }
            if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (criteria.GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
                isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
                criteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
            }
        }
        break;
    case MT_ASPECTSTOP:
        if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%d) ", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (criteria.GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
            isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
            criteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
        }
        break;
    case MT_CHANNELSTART:
        if (asprintf(&comment, "audio channel change from %d to %d (%d)*", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        if (!macontext.Audio.Info.channelChange && (mark->position > iStopA / 2)) {
            dsyslog("AddMark(): first channel start at frame (%d) after half of assumed recording length at frame (%d), this is start mark of next braoscast", mark->position, iStopA / 2);
        }
        else macontext.Audio.Info.channelChange = true;
        break;
    case MT_CHANNELSTOP:
        if ((mark->position > chkSTART) && (mark->position < iStopA * 2 / 3) && !macontext.Audio.Info.channelChange) {
            dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after chkSTART, disable video decoding detection now");
            // disable all video detection
            video->ClearBorder();
            // use now channel change for detection
            criteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
            if (criteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
                criteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE);
                cMark *hborderStop = marks.GetAround(1 * macontext.Video.Info.framesPerSecond, mark->position, MT_HBORDERSTOP);
                if (hborderStop) {  // use hborder stop, we have no scene change or black screen to optimize channel stop mark, will result false optimization
                    dsyslog("cMarkAdStandalone::AddMark(): keep hborder stop (%d), ignore channel stop (%d)", hborderStop->position, mark->position);
                    return;
                }
            }
            if (iStart == 0) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we have selected a start mark
        }
        macontext.Audio.Info.channelChange = true;
        if (asprintf(&comment, "audio channel change from %d to %d (%d) ", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_RECORDINGSTART:
        if (asprintf(&comment, "start of recording (%d)*", mark->position) == -1) comment = NULL;
        if (comment) {
            ALLOC(strlen(comment)+1, "comment");
        }
        break;
    case MT_RECORDINGSTOP:
        if (asprintf(&comment, "stop of recording (%d) ",mark->position) == -1) comment = NULL;
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
        sceneMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
        if (comment) {
#ifdef DEBUG_WEAK_MARKS
            char *indexToHMSF = marks.IndexToHMSF(mark->position);
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
        silenceMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position);
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
    case MT_BLACKLOWERCHANGE:
    case MT_BLACKCHANGE:
        blackMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
        if (comment) {
            char *indexToHMSF = marks.IndexToHMSF(mark->position);
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
        char *indexToHMSF = NULL;
        if (mark->type == MT_RECORDINGSTART) {
            if (asprintf(&indexToHMSF, "00:00:00.00") == -1) esyslog("cMarkAdStandalone::AddMark(): asprintf failed");  // we have no index to get time for position 0
        }
        else indexToHMSF = marks.IndexToHMSF(mark->position);
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
        if (iStart == 0) marks.Save(directory, &macontext, false);  // save after start mark is valid
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
    if (ptr_cDecoder) framecnt = ptr_cDecoder->GetFrameNumber();
    bool notenough = true;
    do {
        struct stat statbuf;
        if (stat(indexFile,&statbuf) == -1) {
            return;
        }

        int maxframes = statbuf.st_size / 8;
        if (maxframes < (framecnt + 200)) {
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
    if (!mark1)        return false;
    if (!(*mark1))     return false;
    if (!mark2)        return false;
    if (!(*mark2))     return false;

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
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): start check %ds before start mark (%d) from frame (%d) at %s", OVERLAP_CHECK_BEFORE, (*mark1)->position, fRangeBegin, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): preload from frame       (%5d) to (%5d)", fRangeBegin, (*mark1)->position);
    dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): compare with frames from (%5d) to (%5d)", (*mark2)->position, fRangeEnd);
    if (ptr_cDecoder->GetFrameNumber() > fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): current framenumber (%d) greater then start frame (%d), set start to current frame", ptr_cDecoder->GetFrameNumber(), fRangeBegin);
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
        if (!ptr_cDecoder->GetNextPacket(false, false)) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetNextPacket() failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->IsVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false, false)) continue; // interlaced videos will not decode each frame
        if (!macontext.Config->fullDecode && !ptr_cDecoder->IsVideoIFrame()) continue;

#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif


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
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
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
        if (!ptr_cDecoder->GetNextPacket(false, false)) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetNextPacket failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->IsVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false, false)) continue; // interlaced videos will not decode each frame
        if (!macontext.Config->fullDecode && !ptr_cDecoder->IsVideoIFrame()) continue;

#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif

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
            if (indexToHMSFbeforeStart) {
                ALLOC(strlen(indexToHMSFbeforeStart)+1, "indexToHMSFbeforeStart");
            }

            char *indexToHMSFbeforeEnd   = marks.IndexToHMSF(overlapPos.similarBeforeEnd);
            if (indexToHMSFbeforeEnd) {
                ALLOC(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSFbeforeEnd");
            }

            char *indexToHMSFafterStart  = marks.IndexToHMSF(overlapPos.similarAfterStart);
            if (indexToHMSFafterStart) {
                ALLOC(strlen(indexToHMSFafterStart)+1, "indexToHMSFafterStart");
            }

            char *indexToHMSFafterEnd    = marks.IndexToHMSF(overlapPos.similarAfterEnd);
            if (indexToHMSFafterEnd) {
                ALLOC(strlen(indexToHMSFafterEnd)+1, "indexToHMSFafterEnd");
            }

            dsyslog("cMarkAdOverlap::ProcessMarkOverlap(): similar from (%5d) at %s to (%5d) at %s, length %5dms", overlapPos.similarBeforeStart, indexToHMSFbeforeStart, overlapPos.similarBeforeEnd, indexToHMSFbeforeEnd, lengthBefore);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              (%5d) at %s to (%5d) at %s, length %5dms",     overlapPos.similarAfterStart,  indexToHMSFafterStart,  overlapPos.similarAfterEnd,  indexToHMSFafterEnd, lengthAfter);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              maximum deviation in overlap %6d", overlapPos.similarMax);
            if (overlapPos.similarEnd > 0) dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              next deviation after overlap %6d", overlapPos.similarEnd); // can be 0 if overlap ends at the mark

            const char *indexToHMSFmark1  = marks.GetTime(*mark1);
            const char *indexToHMSFmark2  = marks.GetTime(*mark2);

            int gapStop         = ((*mark1)->position - overlapPos.similarBeforeEnd)   / macontext.Video.Info.framesPerSecond;
            int lengthBeforeStop = ((*mark1)->position - overlapPos.similarBeforeStart) / macontext.Video.Info.framesPerSecond;
            int gapStart        = (overlapPos.similarAfterStart - (*mark2)->position)  / macontext.Video.Info.framesPerSecond;
            int lengthAfterStart = (overlapPos.similarAfterEnd - (*mark2)->position)    / macontext.Video.Info.framesPerSecond;

            if (indexToHMSFbeforeStart && indexToHMSFbeforeEnd && indexToHMSFafterStart && indexToHMSFafterEnd && indexToHMSFmark1 && indexToHMSFmark2) {
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): overlap from (%6d) at %s to (%6d) at %s, before stop mark gap %3ds length %3ds, are identical with",overlapPos.similarBeforeEnd, indexToHMSFbeforeEnd, (*mark1)->position, indexToHMSFmark1, gapStop, lengthBeforeStop);
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap():              (%6d) at %s to (%6d) at %s, after start mark gap %3ds length %3ds", (*mark2)->position, indexToHMSFmark2, overlapPos.similarAfterEnd, indexToHMSFafterEnd, gapStart, lengthAfterStart);
            }
            if (indexToHMSFbeforeStart) {
                FREE(strlen(indexToHMSFbeforeStart)+1, "indexToHMSFbeforeStart");
                free(indexToHMSFbeforeStart);
            }
            if (indexToHMSFbeforeEnd) {
                FREE(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSFbeforeEnd");
                free(indexToHMSFbeforeEnd);
            }
            if (indexToHMSFafterStart) {
                FREE(strlen(indexToHMSFafterStart)+1, "indexToHMSFafterStart");
                free(indexToHMSFafterStart);
            }
            if (indexToHMSFafterEnd) {
                FREE(strlen(indexToHMSFafterEnd)+1, "indexToHMSFafterEnd");
                free(indexToHMSFafterEnd);
            }
            // check overlap gap
            int gapStartMax = 16;                                   // changed gapStart from 21 to 18 to 16
            if (gapStop > 0) {                                      // smaller valid diff if we do not hit stop mark, if both are not 0, this can be a invalid overlap
                if (length <= 4920) gapStartMax = 9;                // short overlaps are weak, can be a false positive
                else gapStartMax = 14;
            }
            else if (((*mark2)->type == MT_LOGOSTART) && (lengthBefore >= 38080)) gapStartMax = 21;  // trust long overlaps, there can be info logo after logo start mark

            if (((*mark2)->type == MT_ASPECTSTART) || ((*mark2)->type == MT_VBORDERSTART)) gapStartMax = 7; // for strong marks we can check with a lower value
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): maximum valid gap after start mark: %ds", gapStartMax);
            if ((lengthBefore >= 46640) ||                            // very long overlaps should be valid
                    ((gapStop < 23) && (gapStart == 0)) ||            // if we hit start mark, trust greater stop gap, maybe we have no correct stop mark, changed from 34 to 23
                    ((gapStop < 15) && (gapStart <= gapStartMax))) {  // we can not detect all similars during a scene changes, changed from 27 to 15
                // but if it is too far away it is a false positiv
                // changed gapStop from 36 to 27
                dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): overlap gap to marks are valid, before stop mark %ds, after start mark %ds, length %dms", gapStop, gapStart, lengthBefore);
                *mark1 = marks.Move((*mark1), overlapPos.similarBeforeEnd, MT_OVERLAPSTOP);
                if (!(*mark1)) {
                    esyslog("cMarkAdStandalone::ProcessMarkOverlap(): move stop mark failed");
                    return false;
                }
                *mark2 = marks.Move((*mark2), overlapPos.similarAfterEnd,  MT_OVERLAPSTART);
                if (!(*mark2)) {
                    esyslog("cMarkAdStandalone::ProcessMarkOverlap(): move start mark failed");
                    return false;
                }
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
    if (!mark) return;

    if (!macontext.Config->fullDecode) {
        while (mark) {
            if (abortNow) return;
            if (mark->position != recordingIndexMark->GetIFrameBefore(mark->position)) esyslog("cMarkAdStandalone::DebugMarkFrames(): mark at (%d) type 0x%X is not an iFrame position", mark->position, mark->type);
            mark=mark->Next();
        }
    }

    mark = marks.GetFirst();
    int oldFrameNumber = 0;

    // read and decode all video frames, we want to be sure we have a valid decoder state, this is a debug function, we don't care about performance
    while(mark && (ptr_cDecoder->DecodeDir(directory))) {
        while(mark && (ptr_cDecoder->GetNextPacket(false, false))) {
            if (abortNow) return;
            if (ptr_cDecoder->IsVideoPacket()) {
                if (ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false, false)) {
                    int frameNumber = ptr_cDecoder->GetFrameNumber();
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
                        char *fileName = NULL;
                        if (asprintf(&fileName,"%s/F__%07d_%s_%s.pgm", macontext.Config->recDir, frameNumber, suffix1, suffix2) >= 1) {
                            ALLOC(strlen(fileName)+1, "fileName");
                            SaveFrameBuffer(&macontext, fileName);
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
        dsyslog("cMarkAdStandalone::MarkadCut(): start pass: %d", pass);
        ptr_cDecoder->Reset();
        ptr_cDecoder->DecodeDir(directory);
        ptr_cEncoder->Reset(pass);

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
        int stopPos = stopMark->position;

        // open output file
        ptr_cDecoder->SeekToFrame(&macontext, startMark->position - 1);  // seek to start posiition to get correct input video parameter
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
            while(ptr_cDecoder->GetNextPacket(false, false)) {  // no frame index, no PTS ring buffer
                int frameNumber = ptr_cDecoder->GetFrameNumber();
                // seek to frame before startPosition
                if  (frameNumber < startMark->position) {  // will be called until we got next video frame, skip audio frame before
                    if (frameNumber < (startMark->position - 1)) {  // seek to next video frame before start mark
                        LogSeparator();
                        dsyslog("cMarkAdStandalone::MarkadCut(): decoding for start mark (%d) to end mark (%d) in pass: %d", startMark->position, stopMark->position, pass);
                        ptr_cDecoder->SeekToFrame(&macontext, startMark->position - 1); // one frame before start frame, future iteratations will get video start frame
                    }
                    continue;
                }
                // stop mark reached, set next startPosition
                if  (frameNumber > stopPos) {
                    if (stopMark->Next() && stopMark->Next()->Next()) {  // next mark pair
                        startMark = stopMark->Next();
                        if ((startMark->type & 0x0F) != MT_START) {
                            esyslog("got invalid start mark at (%d) type 0x%X", startMark->position, startMark->type);
                            return;
                        }
                        stopMark = startMark->Next();
                        if ((stopMark->type & 0x0F) != MT_STOP) {
                            esyslog("got invalid stop mark at (%d) type 0x%X", stopMark->position, stopMark->type);
                            return;
                        }
                        stopPos = stopMark->position;
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
                    esyslog("failed to get packet from input stream at frame (%d)", frameNumber);
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
            esyslog("failed to close output file");
            return;
        }
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): end at frame %d", ptr_cDecoder->GetFrameNumber());
    FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
    delete ptr_cEncoder;  // ptr_cEncoder must be valid here because it is used above
    ptr_cEncoder = NULL;
}


void cMarkAdStandalone::BorderMarkOptimization() {
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::BorderMarkOptimization(): start border mark optimization");

    // first boder start mark can be too early if previous broacast has dark closing credits
    cMark *mark = marks.GetFirst();
    if (mark && ((mark->type == MT_VBORDERSTART) || (mark->type == MT_HBORDERSTART))) {
        cMark *blackMark = blackMarks.GetNext(mark->position, MT_NOBLACKSTART);
        if (blackMark) {
            int diffBlack = 1000 * (blackMark->position - mark->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::BorderMarkOptimization(): black screen (%d) %dms after border start mark (%d)", blackMark->position, diffBlack, mark->position);
            if (diffBlack <= 2600) { // changed from 1520 to 2560 to 2600
                marks.Move(mark, blackMark->position - 1, MT_NOBLACKSTART);
                marks.Save(directory, &macontext, false);
                return;
            }
        }
    }
    return;
}


// logo mark optimization
// do it with all mark types, because even with channel marks from a double episode, logo marks can be the only valid end mark type
// - move logo marks before intrudiction logo
// - move logo marks before/after ad in frame
// - remove stop/start from info logo
//
void cMarkAdStandalone::LogoMarkOptimization() {
    if (!ptr_cDecoder) return;

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): start logo mark optimization");
    if (marks.Count(MT_LOGOCHANGE, 0xF0) == 0) {
        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no logo marks used");
        return;
    }
    bool save = false;

// check for advertising in frame with logo after logo start mark and before logo stop mark and check for introduction logo
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): check for advertising in frame with logo after logo start and before logo stop mark and check for introduction logo");

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &criteria, ptr_cDecoder, recordingIndexMark, NULL);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    cMark *markLogo = marks.GetFirst();
    while (markLogo) {
        if (markLogo->type == MT_LOGOSTART) {

            const char *indexToHMSFStartMark = marks.GetTime(markLogo);

            // check for introduction logo before logo mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (30 * macontext.Video.Info.framesPerSecond); // introduction logos are usually 10s, somettimes longer, changed from 12 to 30
            if (searchStartPosition < 0) searchStartPosition = 0;

            char *indexToHMSFSearchStart = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFSearchStart) {
                ALLOC(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
            }

            if (indexToHMSFStartMark && indexToHMSFSearchStart) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search introduction logo from position (%d) at %s to logo start mark (%d) at %s", searchStartPosition, indexToHMSFSearchStart, markLogo->position, indexToHMSFStartMark);
            if (indexToHMSFSearchStart) {
                FREE(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart");
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
                int searchEndPosition = markLogo->position + (60 * macontext.Video.Info.framesPerSecond); // advertising in frame are usually 30s
                // sometimes advertising in frame has text in "e.g. Werbung"
                // check longer range to prevent to detect text as second logo
                // changed from 35 to 60

                char *indexToHMSFSearchEnd = marks.IndexToHMSF(searchEndPosition);
                if (indexToHMSFSearchEnd) {
                    ALLOC(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
                }
                if (indexToHMSFStartMark && indexToHMSFSearchEnd) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo after logo start mark (%d) at %s to position (%d) at %s", markLogo->position, indexToHMSFStartMark, searchEndPosition, indexToHMSFSearchEnd);
                if (indexToHMSFSearchEnd) {
                    FREE(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd");
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
                if (!macontext.Config->fullDecode) adInFrameEndPosition = recordingIndexMark->GetIFrameAfter(adInFrameEndPosition + 1);  // we got last frame of ad, go to next iFrame for start mark
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
                        int innerLength = 1000 * (blackMarkStart->position - blackMarkStop->position) / macontext.Video.Info.framesPerSecond;
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
        if (markLogo->type == MT_LOGOSTOP) {
            // check for advertising in frame with logo before logo stop mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (45 * macontext.Video.Info.framesPerSecond); // advertising in frame are usually 30s, changed from 35 to 45
            // sometimes there is a closing credit in frame with logo before
            const char *indexToHMSFStopMark = marks.GetTime(markLogo);
            char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFSearchPosition) {
                ALLOC(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
            }

            if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::LogoMarkOptimization(): search advertising in frame with logo from frame (%d) at %s to logo stop mark (%d) at %s", searchStartPosition, indexToHMSFSearchPosition, markLogo->position, indexToHMSFStopMark);
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
            if (evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsAdInFrame(markLogo->position) >= STATUS_UNKNOWN) && (ptr_cDetectLogoStopStart->Detect(searchStartPosition, markLogo->position))) {
                int newStopPosition = ptr_cDetectLogoStopStart->AdInFrameWithLogo(false);
                if (newStopPosition >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) found", newStopPosition, markLogo->position);
                    if (evaluateLogoStopStartPair->IncludesInfoLogo(newStopPosition, markLogo->position)) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        newStopPosition = -1;
                    }
                }
                if (newStopPosition != -1) {
                    if (!macontext.Config->fullDecode) newStopPosition = recordingIndexMark->GetIFrameBefore(newStopPosition - 1);  // we got first frame of ad, go one iFrame back for stop mark
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
    FREE(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");
    delete ptr_cDetectLogoStopStart;

    // save marks
    if (save) marks.Save(directory, &macontext, false);
}


void cMarkAdStandalone::BlackScreenOptimization() {
    bool save = false;
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark optimization with black screen");
    DebugMarks();
    cMark *mark = marks.GetFirst();
#define START_STOP_BLACK (macontext.Video.Info.framesPerSecond / 2)    // black picture before start and after stop mark
    while (mark) {
        int lengthBefore   = 0;
        int lengthAfter    = 0;
        // store old mark types
        char *markType    = marks.TypeToText(mark->type);
        char *markOldType = marks.TypeToText(mark->oldType);
        char *markNewType = marks.TypeToText(mark->newType);
        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            // log available start marks
            bool moved         = false;
            int diffBefore     = INT_MAX;
            int diffAfter      = INT_MAX;
            bool silenceBefore = false;
            bool silenceAfter  = false;
            // stop of black screen is start mark
            cMark *stopBefore  = NULL;
            cMark *stopAfter   = NULL;
            cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART); // new part starts after the black screen
            cMark *startAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTART); // new part starts after the black screen
            if (startBefore) {
                stopBefore = blackMarks.GetPrev(startBefore->position, MT_NOBLACKSTOP);
                if (stopBefore) {
                    diffBefore   = 1000 * (mark->position        - startBefore->position) / macontext.Video.Info.framesPerSecond;
                    lengthBefore = 1000 * (startBefore->position - stopBefore->position)  / macontext.Video.Info.framesPerSecond;
                    // check if there is silence between or very ahort before black screen
                    cMark *silence = silenceMarks.GetAround(1.1 * macontext.Video.Info.framesPerSecond, stopBefore->position, MT_SOUNDSTOP); // silence around black screen start
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms before -> length %5dms, silence around %d", mark->position, stopBefore->position, startBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else startBefore = NULL; // no pair, this is invalid
            }
            if (startAfter) {
                stopAfter = blackMarks.GetPrev(startAfter->position, MT_NOBLACKSTOP);
                if (stopAfter) {
                    diffAfter   = 1000 * (startAfter->position - mark->position)      / macontext.Video.Info.framesPerSecond;
                    lengthAfter = 1000 * (startAfter->position - stopAfter->position) / macontext.Video.Info.framesPerSecond;
                    // check if there is silence between black screen
                    cMark *silence = silenceMarks.GetAround(1.1 * macontext.Video.Info.framesPerSecond, stopAfter->position, MT_SOUNDSTOP); // silence around black screen start
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, stopAfter->position, startAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else startAfter = NULL; // no pair, this is invalid
            }
            // try black screen before start mark
            if (startBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_LOGOSTART:
                    // select best mark (before (length)/ after (length)), default: before
                    // <5600> (160s) /  879040 (1280)  fade in logo channel (TELE 5)
                    // <5880> (160s) / 1209800  (160)  fade in logo channel (TELE 5)
                    //
                    // invalid black screen after broadcast start
                    //     -     /   2440 (80)   black screen after broadcast start

                    // black screen before separator and between separator ans broadcast start
                    // 3020 (120) / <20> (120)   black screen before separator and between separator ans broadcast start
                    // 2020 (120) / <40> (140)   black screen before separator and between separator ans broadcast start
                    if ((diffBefore >= 2020) && (diffBefore <= 3020) && (diffAfter <= 40)) diffBefore = INT_MAX;

                    if ((lengthBefore >= 1640) && (diffBefore <= 8640)) maxBefore = 8640;  // allow for long blackscreen more distance
                    else if (silenceBefore && (lengthBefore >= 160))    maxBefore = 5880;
                    else if (lengthBefore <= 40)                        maxBefore = 4299;  // very short black screen before preview 4300ms (40) before logo stop
                    else                                                maxBefore = 5399;   // do not increase, will get black screen before last ad
                    break;
                case MT_CHANNELSTART:
                    maxBefore = 1240;   // changed from 80 to 1240
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_INTRODUCTIONSTART:
                        maxBefore = 4599;  // black screen before preview 4600ms before introduction logo
                        break;
                    case MT_VPSSTART:
                        // select best mark (before / after), default: before
                        // valid black screen without silence
                        // <50480> (520) /  89400 (1240)
                        //
                        // invalid black screen without silence
                        // 21600   (600) / 826200   (40)   blackscreen in broadcast before and in broadcast after  (conflict)
                        //
                        // blackscreen with silence before VPS start (preview) and black screen with silence after VPS start (broadcast start)
                        // 26660 (220) silence / <44100> (220) silence
                        if (silenceBefore && (diffBefore <= 26660) && silenceAfter && (diffAfter <= 44100)) diffBefore = INT_MAX;

                        // no silence before or after, or no black screen before
                        //       -       /  <4400>  (240)
                        else if (!silenceBefore && (diffBefore == INT_MAX) && !silenceAfter && (diffAfter <= 4400)) diffBefore = INT_MAX;

                        // first black screen in broadcast before, second blackscreen is start of broadcast, no silence around
                        //  48040 (1000) / <26560> (2400)   // first black screen in broadcast before
                        //  74080 (1520) / <13280>   (80)   // first black screen in broadcast before
                        else if (!silenceBefore && (diffBefore >= 48040) && (diffBefore <= 74080) && (lengthBefore >= 1000) && (lengthBefore <= 1520) &&
                                 !silenceAfter && (diffAfter >= 13280) && (diffAfter <= 26560) && (lengthAfter >= 80) && (lengthAfter <= 2400)) diffBefore = INT_MAX;

                        // valid blackscreen before with silence
                        //  69480ms before -> length    40ms, silence around 1
                        // 154880ms before -> length    40ms, silence around 1
                        if (silenceBefore)            maxBefore = 154880;
                        else if (lengthBefore >= 520) maxBefore =  50480;  // long black screen is start mark
                        else                          maxBefore =  11579;  // black screen before preview 11580ms before VPS start event
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
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try black screen after start mark
            if (!moved && startAfter) { // move even to same position to prevent scene change do a move
                int maxAfter = -1;
                switch (mark->type) {
                case MT_LOGOSTART:
                    maxAfter = 2439;  //  first black screen in broadcast 2440ms after logo start
                    break;
                case MT_HBORDERSTART:
                    if (silenceAfter && (lengthAfter >= 1520)) maxAfter = 4520;  // separator picture with hbrder before start
                    else                                       maxAfter =   -1;
                    break;
                case MT_CHANNELSTART:
                    maxAfter = 4319;   // black sceen after start of broadcast 4320ms (3840)
                    break;
                case MT_ASPECTSTART:
                    maxAfter = 1000;   // valid black screen 1000ms (650)
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // select best mark (before / after), default: before
                        // very long black screen without silence before broadcast start
                        // - / <66840> (4160)
                        // - / <12560>  (480)
                        // - /  <5880> (1880)
                        // - /  <4400>  (240)
                        //
                        // black screen after with silence around
                        if (silenceAfter)             maxAfter = 55640;
                        else if (lengthAfter >= 4160) maxAfter = 66840;
                        else if (lengthAfter >=  240) maxAfter = 12560;
                        else                          maxAfter =  2000;
                        break;
                    case MT_INTRODUCTIONSTART:
                        // select best mark (before / after), default: before
                        // - / 3760 (3800)  long black screen from end of previous broadcast is detected as part of the introduction logo
                        // - / 3200  (120)  first blackscreen in broadcast
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
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
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
            const cMark *startBefore = NULL;
            const cMark *startAfter  = NULL;
            const cMark *stopBefore  = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP);
            const cMark *stopAfter   = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
            if (stopBefore) {
                diffBefore = 1000 * (mark->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                startBefore = blackMarks.GetNext(stopBefore->position, MT_NOBLACKSTART);
                if (startBefore) {
                    lengthBefore = 1000 * (startBefore->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(macontext.Video.Info.framesPerSecond, stopBefore->position, MT_SOUNDSTOP);
                    if (silence) silenceBefore = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7ldms before -> length %5dms, silence around %d", mark->position, stopBefore->position, startBefore->position, diffBefore, lengthBefore, silenceBefore);
                }
                else stopBefore = NULL; // no pair, this is invalid
            }
            if (stopAfter) {
                diffAfter = 1000 * (stopAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                startAfter = blackMarks.GetNext(stopAfter->position, MT_NOBLACKSTART);
                if (startAfter) {
                    lengthAfter = 1000 * (startAfter->position - stopAfter->position) / macontext.Video.Info.framesPerSecond;
                    // check if there is silence around black screen
                    cMark *silence = silenceMarks.GetAround(macontext.Video.Info.framesPerSecond, stopAfter->position, MT_SOUNDSTOP);
                    if (silence) silenceAfter = true;
                    dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%6d): found black screen from (%6d) to (%6d), %7dms after  -> length %5dms, silence around %d", mark->position, stopAfter->position, startAfter->position, diffAfter, lengthAfter, silenceAfter);
                }
                else stopAfter = NULL; // no pair, this is invalid
            }
            // try black screen after stop marks
            if (stopAfter) {  // move even to same position to prevent scene change for move again
                int maxAfter = -1;
                switch (mark->type) {
                case MT_NOBLACKSTOP:
                    maxAfter =  0;  // use mid of black screen for end marks
                    break;
                case MT_LOGOSTOP:
                    // select best mark (before (length) / after (length)), default: after
                    //
                    // valid black screen without silence around after
                    // 1190120 (1720) /  <1600>  (120)  fade out logo channel (TELE 5)
                    // 1087680  (160) /  <1680>   (80)  fade out logo channel (TLC)
                    //
                    // valid black screen with silence around
                    //                   <5040> (160)  fade out logo
                    //
                    // black screen from before logo stop to after logo stop
                    //  <20>  (100) / 4000 (120)  second black screen after separator
                    //  <40>  (160) / 3920 (180)  second black screen after separator
                    // <280>  (320) / 3940 (640)  second black screen after separator
                    // <600>  (640) / 3240  (80)  second black screen after preview
                    // <800> (1000) / 2080 (520)  second black screen after preview
                    // <800> (1320) / 3640 (200)  second black screen after preview
                    if (lengthBefore >= diffBefore) diffAfter = INT_MAX;

                    // long black screen at end of broadcast, short black screen after preview
                    // <2480>  (600) /  2680 (320)  second black screen after preview  NEW
                    // <2680>  (760) /   160 (120)  second black screen after preview
                    // <4580>  (880) /  1300 (160)  second black screen after preview
                    else if ((diffBefore <= 4580) && (lengthBefore >= 600) && (diffAfter <= 3240) && (lengthAfter <= 520)) diffAfter = INT_MAX;

                    // invalid black screen from channel with fade out logo: no black screen near before, but short black screen after is after/in preview
                    // 1295800  (100) /  4540 (100)  black screen after preview (Das Erste)
                    // 1815820  (440) /  4620 (440)  black screen after preview (Das Erste)
                    //

                    // invalid black screen from channel without fade out logo: no black screen near before, but short black screen after is after/in preview
                    //   47560 (1120) /  1400 (120)  black screen in preview after stop (SIXX)
                    //   14200 (1440) /  2040  (80)  black screen after preview
                    //   42280 (1440) /  2040  (80)  black screen after preview
                    //                   2520  (80)  black screen after preview
                    //  231760  (520) /  2920  (80)  black screen after preview (SIXX)
                    //   49320 (2000) /  2960  (40)  black screen after add (RTLZWEI)
                    // 1466760  (160) /  3480  (80)  black screen after preview (RTLZWEI)
                    //  293600  (520) /  3600  (80)  black screen after preview (SIXX)
                    else if (!criteria.LogoFadeOut(macontext.Info.ChannelName) &&
                             (diffBefore >= 14200) && (diffAfter >= 1400) && (diffAfter <= 4540) && (lengthAfter >= 40) && (lengthAfter <= 120)) diffAfter = INT_MAX;

                    if (silenceAfter) maxAfter = 5040;    // trust black screen with silence around
                    else              maxAfter = 4619;
                    break;
                case MT_VBORDERSTOP:
                    maxAfter = 480;  // black closing credits
                    break;
                case MT_CHANNELSTOP:
                    maxAfter = 320;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        // select best mark (before (length) / after (length)), default: after
                        // 1061800 (240)  / <24600>  (200)
                        // 103880   (80)  / <82120> (3000)

                        // invalid black screen
                        //   15200  (80)  /  20120    (80)  both wrong, first in broadcast, second in next broadcast

                        // silence before and after, first blackscreen is end mark, second is after preview or in next broadcast
                        //  <6360> (220)  / 12560 (260)
                        if (silenceBefore && silenceAfter && (diffBefore < diffAfter) && (diffBefore <= 6360) && (diffAfter >= 12560)) diffAfter = INT_MAX;

                        // no silence before or after, first blackscreen is end mark, second is after preview or in next broadcast
                        //   <3520> (200) /   8500 (180)  second blackscreen after preview
                        //   <5300> (200) /  11200 (220)  second blackscreen after preview
                        //   <5800> (220) /  75580 (200)  second blackscreen after preview
                        // <116120> (360) /  75280 (280)  second backscreen in next broadcast
                        else if (!silenceBefore && (diffBefore <= 116120) && (lengthBefore >= 200) && !silenceAfter && (diffAfter >= 8500) && (lengthAfter <= 280)) diffAfter = INT_MAX;

                        // valid black screen with silence around before
                        // <6240> (240s) / 4180 (280)
                        else if (silenceBefore && !silenceAfter && (diffBefore <= 6260) && (diffAfter >= 4180)) diffAfter = INT_MAX;

                        if (silenceAfter)             maxAfter = 176680;   // black screen with silence between is a stronger indicator for valid end mark
                        else if (lengthAfter >= 3000) maxAfter =  82120;
                        else if (lengthAfter >=  200) maxAfter =  24600;
                        else                          maxAfter =  20119;   // black screen in next broadcast after 20120ms
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        // select best mark (before (length) / after (length)), default: after
                        // <15200> (240) / 15080 (160)  black screen between closind credis and ad in frame, all detected as closing credits
                        maxAfter = 11040;   // chnaged from 7200 to 11040 (incomplete detected closing credits)
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
                            if (lengthAfter < 4200) { // too long black screen is closing credits from broadcast before
                                int midBlack = (stopAfter->position + startAfter->position) / 2;  // for long black screen, take mid of a the black screen
                                if (newPos < midBlack) newPos = midBlack;
                            }
                        }
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try black screen before stop mark
            if (!moved && stopBefore) {  // move even to same position to prevent scene change for move again
                int maxBefore = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxBefore = 21960;
                    break;
                case MT_LOGOSTOP:
                    maxBefore = 6519;  // black screen between end of broadcast and closing credits with logo before 6520ms before logo stop
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        // valid blackscreen without silence before VPS stop
                        // <116120> (360) / 75280 (280)    // second backscreen in next broadcast
                        if (lengthBefore >= 360)  maxBefore = 116120;
                        else                      maxBefore =   5800;  // changed from 5300 to 5800
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxBefore = 15200;
                        break;
                    case MT_NOADINFRAMESTOP:
                        if (lengthBefore > diffBefore) maxBefore = -1;  // long black closing credits before ad in frame, keep this
                        else maxBefore = 17720;    // changed from 4520 to 17720, correct too short detected ad in frame
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 2040;
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    int newPos =  stopBefore->position;
                    if (mark->position == marks.GetLast()->position) {
                        newPos += START_STOP_BLACK;  // end broadcast with some black picture
                        if (newPos > (startBefore->position - 1)) newPos = (startBefore->position - 1);
                        else {
                            if (lengthBefore < 5040) {  // next broadcast starts with a long dark scene
                                int midBlack = (startBefore->position + stopBefore->position) / 2;  // for long black screen, take mid of a the black screen
                                if (newPos < midBlack) newPos = midBlack;
                            }
                        }
                    }
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTOP);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        FREE(strlen(markType)+1, "text");
        free(markType);
        FREE(strlen(markOldType)+1, "text");
        free(markOldType);
        FREE(strlen(markNewType)+1, "text");
        free(markNewType);

        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, &macontext, false);
}


void cMarkAdStandalone::BlackLowerOptimization() {
    bool save = false;
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): start mark optimization with lower black border");
    DebugMarks();
    cMark *mark = marks.GetFirst();
    while (mark) {
        int lengthBefore   = 0;
        int lengthAfter    = 0;
        // store old mark types
        char *markType    = marks.TypeToText(mark->type);
        char *markOldType = marks.TypeToText(mark->oldType);
        char *markNewType = marks.TypeToText(mark->newType);
        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            // log available start marks
            bool moved         = false;
            int diffBefore     = INT_MAX;
            int diffAfter      = INT_MAX;
            cMark *stopAfter   = NULL;
            cMark *stopBefore  = NULL;
            cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKLOWERSTOP);  // start of black lower border before logo start mark
            cMark *startAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKLOWERSTOP);  // start of black lower border after  logo start mark
            if (startBefore) {
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOBLACKLOWERSTART);  // end   of black lower border before logo start mark
                if (stopBefore) {
                    diffBefore = 1000 * (mark->position - startBefore->position) / macontext.Video.Info.framesPerSecond;
                    lengthBefore = 1000 * (stopBefore->position - startBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): start mark (%6d): found lower black border from (%6d) to (%6d), %7dms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                }
                else startBefore = NULL; // no pair, this is invalid
            }
            if (startAfter) {
                stopAfter = blackMarks.GetNext(startAfter->position, MT_NOBLACKLOWERSTART);  // get end of black lower border
                if (stopAfter) {
                    diffAfter   = 1000 * (startAfter->position - mark->position)       / macontext.Video.Info.framesPerSecond;
                    lengthAfter = 1000 * (stopAfter->position  - startAfter->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): stop  mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                    // sometime there is too much white text in the lower black border closing credits, we got a small interuption, use longest part
                    cMark *nextStartAfter = blackMarks.GetNext(stopAfter->position, MT_NOBLACKLOWERSTOP);  // near next lower border is part of the same closing credits
                    while (nextStartAfter) {
                        cMark *nextStopAfter = blackMarks.GetNext(nextStartAfter->position, MT_NOBLACKLOWERSTART);
                        if (!nextStopAfter) break;
                        int diffNext        = 1000 * (nextStopAfter->position  - stopAfter->position)      / macontext.Video.Info.framesPerSecond;
                        int nextDiffAfter   = 1000 * (nextStartAfter->position - mark->position)           / macontext.Video.Info.framesPerSecond;
                        int nextLengthAfter = 1000 * (nextStopAfter->position  - nextStartAfter->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::BlackLowerOptimization(): start mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms, distance %dms", mark->position, nextStartAfter->position, nextStopAfter->position, nextDiffAfter, nextLengthAfter, diffNext);
                        if (diffNext > 2360) break;
                        if (nextLengthAfter >= lengthAfter) {
                            startAfter  = nextStartAfter;
                            stopAfter   = nextStopAfter;
                            lengthAfter = nextLengthAfter;
                            diffAfter   = nextDiffAfter;
                        }
                        nextStartAfter = blackMarks.GetNext(nextStartAfter->position, MT_NOBLACKLOWERSTOP);
                    }
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): start mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                }
                else startAfter = NULL; // no pair, this is invalid
            }
            // try lower black border before start mark
            if (startBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // valid lower black border before from closing credits of broadcast before
                        //   7880ms before -> length 11840ms
                        //
                        // invalid lower black border before from broadcast before
                        //  10620ms before -> length   180ms
                        //  11200ms before -> length   280ms
                        //  43160ms before -> length   320ms
                        // 106600ms before -> length  3640ms    (lower part dark scene in broadcast)
                        if      (lengthBefore <=  280) maxBefore =  10619;   // too short for lower black border closing credits
                        else if (lengthBefore >= 3640) maxBefore = 106599;   // too long  for lower black border closing credits
                        else                           maxBefore = 116600;
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore < maxBefore) {
                    mark = marks.Move(mark, startBefore->position, MT_NOBLACKLOWERSTOP);  // move to end of black lower border (closing credits)
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // lower black border after start mark
            if (!moved && stopAfter) {
                int maxAfter = -1;
                switch (mark->type) {
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // first black border in broadcast
                        // 86640ms (40)
                        //
                        // valid black border closing credits
                        // 127840ms (1200)
                        if (lengthAfter >= 1200) maxAfter = 127840;
                        else                     maxAfter =  86639;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {
                    mark = marks.Move(mark, stopAfter->position, MT_NOBLACKLOWERSTART);  // use end of black lower closing credits
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available stop marks
            bool moved               = false;
            long int diffBefore      = INT_MAX;
            int diffAfter            = INT_MAX;
            const cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKLOWERSTOP);
            const cMark *startAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKLOWERSTOP);
            const cMark *stopBefore  = NULL;
            const cMark *stopAfter   = NULL;
            if (startBefore) {
                diffBefore = 1000 * (mark->position - startBefore->position) / macontext.Video.Info.framesPerSecond;
                stopBefore = blackMarks.GetNext(startBefore->position, MT_NOBLACKLOWERSTART);
                if (stopBefore) {
                    lengthBefore = 1000 * (stopBefore->position - startBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): stop  mark (%6d): lower black border from (%6d) to (%6d), %7ldms before -> length %5dms", mark->position, startBefore->position, stopBefore->position, diffBefore, lengthBefore);
                }
                else startBefore = NULL; // no pair, this is invalid
            }
            if (startAfter) {
                stopAfter = blackMarks.GetNext(startAfter->position, MT_NOBLACKLOWERSTART);
                if (stopAfter) {
                    diffAfter   = 1000 * (startAfter->position - mark->position)       / macontext.Video.Info.framesPerSecond;
                    lengthAfter = 1000 * (stopAfter->position  - startAfter->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): stop  mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                    // sometime there is too much white text in the lower black border closing credits, we got a small interuption, use longest part
                    cMark *nextStartAfter = blackMarks.GetNext(stopAfter->position, MT_NOBLACKLOWERSTOP);  // near next lower border is part of the same closing credits
                    while (nextStartAfter) {
                        cMark *nextStopAfter = blackMarks.GetNext(nextStartAfter->position, MT_NOBLACKLOWERSTART);
                        if (!nextStopAfter) break;
                        int diffNext        = 1000 * (nextStopAfter->position  - stopAfter->position)      / macontext.Video.Info.framesPerSecond;
                        int nextDiffAfter   = 1000 * (nextStartAfter->position - mark->position)           / macontext.Video.Info.framesPerSecond;
                        int nextLengthAfter = 1000 * (nextStopAfter->position  - nextStartAfter->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::BlackLowerOptimization(): stop  mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms, distance %dms", mark->position, nextStartAfter->position, nextStopAfter->position, nextDiffAfter, nextLengthAfter, diffNext);
                        if (diffNext > 3520) break;
                        if (nextLengthAfter >= lengthAfter) {
                            startAfter  = nextStartAfter;
                            stopAfter   = nextStopAfter;
                            lengthAfter = nextLengthAfter;
                            diffAfter   = nextDiffAfter;
                        }
                        nextStartAfter = blackMarks.GetNext(nextStartAfter->position, MT_NOBLACKLOWERSTOP);
                    }
                    dsyslog("cMarkAdStandalone::BlackLowerOptimization(): stop  mark (%6d): lower black border from (%6d) to (%6d), %7dms after  -> length %5dms", mark->position, startAfter->position, stopAfter->position, diffAfter, lengthAfter);
                }
                else startAfter = NULL; // no pair, this is invalid
            }
            // try lower black border after stop marks
            if (stopAfter) {
                int maxAfter = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxAfter = 68200;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        //  invalid black screen in next broadcast
                        //  19080ms  (220) -> in next broadcast
                        //  17800ms (2860) -> long dark scene in next broadcast
                        //
                        //  valid black lower border from closing credits
                        //   33280ms  (800)
                        //   66040ms (1200)
                        //  127840ms (1200)
                        //  177720ms (1200)
                        //
                        if ((lengthAfter >= 800) && (lengthAfter <= 1200)) maxAfter = 177720;
                        else                                               maxAfter =  17799;
                        break;
                    default:
                        maxAfter = -1;
                    }
                    break;
                default:
                    maxAfter = -1;
                }
                if (diffAfter <= maxAfter) {  // move even to same position to prevent scene change for move again
                    mark = marks.Move(mark, stopAfter->position, MT_NOBLACKLOWERSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try lower black border before stop mark
            if (!moved && stopBefore) {
                int maxBefore = -1;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    // valid black lower border before
                    // <216200> (1560) /  -
                    if (lengthBefore >= 1560) maxBefore = 216200;
                    else                      maxBefore =  51040;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        maxBefore = 117800;
                        break;
                    default:
                        maxBefore = -1;
                    }
                    break;
                default:
                    maxBefore = -1;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, stopBefore->position, MT_NOBLACKLOWERSTOP);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        FREE(strlen(markType)+1, "text");
        free(markType);
        FREE(strlen(markOldType)+1, "text");
        free(markOldType);
        FREE(strlen(markNewType)+1, "text");
        free(markNewType);

        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, &macontext, false);
}


void cMarkAdStandalone::SilenceOptimization() {
    LogSeparator(true);
    bool save = false;
    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark optimization with silence scenes");
    DebugMarks();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // store old mark types
        char *markType    = marks.TypeToText(mark->type);
        char *markOldType = marks.TypeToText(mark->oldType);
        char *markNewType = marks.TypeToText(mark->newType);
        // optimize start marks
        if ((mark->type & 0x0F) == MT_START) {
            // log available marks
            bool moved = false;
            int diffBefore        = INT_MAX;
            int diffAfter         = INT_MAX;
            int lengthBefore      = 0;
            int lengthAfter       = 0;
            bool blackLowerBefore = false;
            cMark *soundStartBefore = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTART);
            cMark *soundStartAfter  = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTART);
            if (soundStartBefore) {
                diffBefore = 1000 * (mark->position - soundStartBefore->position) / macontext.Video.Info.framesPerSecond;
                cMark *soundStopBefore = silenceMarks.GetPrev(soundStartBefore->position, MT_SOUNDSTOP);
                if (soundStopBefore) {
                    lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): sound silence from (%5d) to (%5d) %8dms before, length %4dms", mark->position, soundStopBefore->position, soundStartBefore->position, diffBefore, lengthBefore);
                }
                cMark *blackLowerStart = blackMarks.GetPrev(soundStartBefore->position, MT_NOBLACKLOWERSTOP);
                if (blackLowerStart) {
                    cMark *blackLowerStop = blackMarks.GetNext(blackLowerStart->position, MT_NOBLACKLOWERSTART);
                    if (blackLowerStop) {
                        int diffBlackLowerBefore = 1000 * (soundStartBefore->position - blackLowerStop->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): black lower border before start (%d) end (%d), %dms before", mark->position, blackLowerStart->position, blackLowerStop->position, diffBlackLowerBefore);
                        if (diffBlackLowerBefore <= 80) blackLowerBefore = true;
                    }
                }
            }
            if (soundStartAfter) {
                diffAfter = 1000 * (soundStartAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                cMark *soundStopAfter  = silenceMarks.GetPrev(soundStartAfter->position, MT_SOUNDSTOP);
                if (soundStopAfter) {
                    lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): found silence from (%5d) to (%5d) %8dms after,  length %4dms", mark->position, soundStopAfter->position, soundStartAfter->position, diffAfter, lengthAfter);
                }
            }
            // try silence before start position
            if (soundStartBefore && (soundStartBefore->position != mark->position)) { // do not move to same frame
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxBefore = 4479;
                    break;
                case MT_NOBLACKLOWERSTART:
                    maxBefore = 82980;
                    break;
                case MT_LOGOSTART:
                    // select best mark (before / after), default: before
                    // <4320> (400) / 1646880  (80)  fade in logo (Das Erste)
                    // <4520> (200) / 1435720 (320)  delayed logo start from bright backgraound
                    if ((diffBefore >=  600) && (diffAfter <=  360)) diffBefore = INT_MAX;
                    if ((diffBefore >= 2040) && (diffAfter <= 3460)) diffBefore = INT_MAX;
                    maxBefore = 4520;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // select best mark (before / after), default: before
                        // valid silence before without black screen / black border
                        //  <94360> (120) /  830600  (240)         (conflict)
                        // <141480> (120) /  423520  (200)     ntv (conflict)
                        //
                        // valid silence after without black screen / lower border
                        //        -       /  <41880> (120)
                        //        -       /  <43440> (880)
                        //        -       /  <45040> (120)
                        //        -       /  <65040> (120)   WELT
                        //        -       / <213920> (120)   ntv
                        //
                        // invalid silence without black screen / black border
                        //  37860  (440) /      -          silence before preview
                        //        -      /  26600  (360)   silence before first ad (conflict)
                        //        -      / 141960  (160)   silence before first ad
                        //
                        // with black border
                        // <51440> (bb) / 6400              second silence in broadcast
                        //
                        // silence before preview and before broadcast start, no black border
                        //   7440 (480) /  <2400> (120)   silence before preview and before broadcast start
                        if (!blackLowerBefore && (diffBefore >= 7440) && (lengthBefore >= 480) && (diffAfter <= 2400) && (lengthAfter <= 120)) diffBefore = INT_MAX;

                        if (blackLowerBefore) maxBefore = 51440;   // black lower border short before silence
                        else                  maxBefore =  5139;   // silence before preview 5140ms before VPS start
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
                    int newPos = soundStartBefore->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameBefore(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try silence after start position
            if (!moved && soundStartAfter) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxAfter = 227620;  // changed from 55600 to 227620
                    break;
                case MT_LOGOSTART:
                    maxAfter = 4319;  // silence in broadcast after openen credists 4320ms after logo start
                    break;
                case MT_VBORDERSTART:
                    // only optimize start mark, we can miss real vborder start because of black screen between broadcast
                    if (mark->position == marks.GetFirst()->position) maxAfter = 7340; // changed from 1800 to 7340
                    else maxAfter = 0;
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_VPSSTART:
                        // invalid silence without black screen / black border
                        //        -      / 141960  (160)   silence before first ad
                        if      (lengthAfter >  160) maxAfter = 213920;  // trust long silence
                        else if (lengthAfter >= 120) maxAfter = 141959;  // trust long silence
                        else                         maxAfter =  34119;  // first silence in broadcast 34120ms after start
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if ((diffAfter <= maxAfter) && (soundStartAfter->position != mark->position)) {
                    int newPos = soundStartAfter->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameBefore(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTART);
                    if (mark) {
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        // optimize stop marks
        if ((mark->type & 0x0F) == MT_STOP) {
            // log available marks
            bool moved = false;
            long int diffBefore   = INT_MAX;
            int lengthAfter       = 0;
            int diffAfter         = INT_MAX;
            bool blackLowerBefore = false;
            cMark *soundStopBefore = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            cMark *soundStopAfter  = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            if (soundStopBefore) {
                diffBefore = 1000 * (mark->position - soundStopBefore->position) / macontext.Video.Info.framesPerSecond;
                cMark *soundStartBefore = silenceMarks.GetNext(soundStopBefore->position, MT_SOUNDSTART);
                if (soundStartBefore) {
                    int lengthBefore = 1000 * (soundStartBefore->position - soundStopBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): found silence from (%5d) to (%5d) %8ldms before, length %4dms", mark->position, soundStopBefore->position, soundStartBefore->position, diffBefore, lengthBefore);
                }
                // check black lower border for silence before
                const cMark *blackLowerStart = blackMarks.GetPrev(soundStopBefore->position, MT_NOBLACKLOWERSTOP);
                if (blackLowerStart) {
                    const cMark *blackLowerStop = blackMarks.GetNext(blackLowerStart->position, MT_NOBLACKLOWERSTART);
                    if (blackLowerStop && (blackLowerStop->position >= soundStopBefore->position)) blackLowerBefore = true;
                }
            }
            if (soundStopAfter) {
                diffAfter = 1000 * (soundStopAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                cMark *soundStartAfter = silenceMarks.GetNext(soundStopAfter->position, MT_SOUNDSTART);
                if (soundStartAfter) {
                    lengthAfter = 1000 * (soundStartAfter->position - soundStopAfter->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): found silence from (%5d) to (%5d) %8dms after,  length %4dms", mark->position, soundStopAfter->position, soundStartAfter->position, diffAfter, lengthAfter);
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
                    // select best mark (before / after), default: after
                    //  96880 (2400) /  4560 (240)  long fade out logo (Disney_Channel)
                    // 218000  (480) / 17800 (120)  very early logo stop before end (TLC)
                    //
                    //  <640>       / 6480    second silence is after preview (Disney_Channel)
                    //
                    // both silence invalid
                    // 244080 (560) / 2440 (120)  // after silence is in preview
                    //
                    // second silence is after preview
                    //  <5840>      / 4000    second silence is after preview, logo stop delayed from bright background
                    // <11600>      / 1120    second silence is after preview
                    // <11680>      / 1040    second silence is after preview
                    if ((diffBefore >= 5840) && (diffBefore <= 11680) && (diffAfter >= 1040) && (diffAfter <= 4000)) diffAfter = INT_MAX;

                    if (criteria.LogoFadeOut(macontext.Info.ChannelName) &&
                            (mark->position == marks.GetLast()->position) && (lengthAfter >= 120)) maxAfter = 17800;  // very early logo stop before end
                    else if (criteria.LogoFadeOut(macontext.Info.ChannelName))              maxAfter =  7960;  // very early fade out logo channels
                    else                                                                           maxAfter =  2239;  // silence after separator picture 2240ms after stop
                    break;
                case MT_VBORDERSTOP:
                    // select best mark (before / after), default: after
                    // <540> (320) / 43820 (740)  vborder stop in dark opening credits from next broadcast
                    maxAfter = 0;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        // select best mark (before / after), default: after
                        //  575160 (200) / <45400> (120)
                        // 1053720 (200) / <52920> (280)
                        //
                        // <172240> (200) /   720  (200) (conflict)
                        //
                        // silence with black border
                        // <50400> (bb) / 7320  second silence is from next broadcast
                        // <57280> (bb) / 2640  second silence is from next broadcast
                        if ((diffBefore <= 57280) && blackLowerBefore && (diffAfter >= 2640)) diffAfter = INT_MAX;

                        //   <5480>       / 25940
                        else if ((diffBefore <= 5480) && (diffAfter >= 20600)) diffAfter = INT_MAX;

                        if      (lengthAfter >= 280) maxAfter = 52920;
                        else if (lengthAfter >= 120) maxAfter = 45400;
                        else                         maxAfter = 31479;  // silence after preview 31480ms after VPS stop
                        break;
                    default:
                        maxAfter = 0;
                    }
                    break;
                default:
                    maxAfter = 0;
                }
                if (diffAfter <= maxAfter) {
                    int newPos = soundStopAfter->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameAfter(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try silence before stop mark
            if (!moved && soundStopBefore) {
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    // valid silence before
                    maxBefore =  76760;   // changed from 76620 to 76760
                    break;
                case MT_LOGOSTOP:
                    maxBefore = 11680;  // changed from 4240 to 5840 to 11680
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 1100;
                    break;
                case MT_VBORDERSTOP:
                    maxBefore = 540;
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_VPSSTOP:
                        if (blackLowerBefore) maxBefore = 57280;  // lower black screen around sound stop
                        else maxBefore = 28279;  // silence in broadcast 28280ms before VPS stop
                        break;
                    case MT_TYPECHANGESTOP:
                        maxBefore = 6200;   // changed from 5600 to 6200
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if ((diffBefore <= maxBefore) && (soundStopBefore->position != mark->position)) {
                    int newPos = soundStopBefore->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameAfter(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTOP);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        FREE(strlen(markType)+1, "text");
        free(markType);
        FREE(strlen(markOldType)+1, "text");
        free(markOldType);
        FREE(strlen(markNewType)+1, "text");
        free(markNewType);

        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, &macontext, false);
}


void cMarkAdStandalone::SceneChangeOptimization() {
    bool save = false;
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark optimization with scene changes");
    DebugMarks();
    cMark *mark = marks.GetFirst();
    while (mark) {
        // store old mark types
        char *markType    = marks.TypeToText(mark->type);
        char *markOldType = marks.TypeToText(mark->oldType);
        char *markNewType = marks.TypeToText(mark->newType);
        // check start mark
        if ((mark->type & 0x0F) == MT_START) {
            // log available marks
            bool moved     = false;
            int diffAfter  = INT_MAX;
            int diffBefore = INT_MAX;
            cMark *sceneStartBefore = NULL;
            cMark *sceneStartAfter  = NULL;
            if (mark->type == MT_ASPECTSTART) {   // change of aspect ratio results in a scene change, but they are sometimes a few frames to early
                sceneStartBefore = sceneMarks.GetPrev(mark->position, MT_SCENESTART);      // do not allow one to get same position
                sceneStartAfter  = sceneMarks.GetNext(mark->position, MT_SCENESTART);      // do not allow one to get same position
            }
            else {
                sceneStartBefore = sceneMarks.GetPrev(mark->position + 1, MT_SCENESTART);  // allow one to get same position
                sceneStartAfter  = sceneMarks.GetNext(mark->position - 1, MT_SCENESTART);  // allow one to get same position
            }

            if (sceneStartBefore) {
                diffBefore = 1000 * (mark->position - sceneStartBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%6d): found scene start (%6d) %5dms before", mark->position, sceneStartBefore->position, diffBefore);
            }
            if (sceneStartAfter) {
                diffAfter = 1000 * (sceneStartAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%6d): found scene start (%6d) %5dms after", mark->position, sceneStartAfter->position, diffAfter);
            }
            // try scene change before start mark
            if (sceneStartBefore && (sceneStartBefore->position != mark->position)) {
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTART:
                    maxBefore = 600;   // changed from 360 to 600
                    break;
                case MT_LOGOSTART:
                    // select best mark, default: use before
                    // before / after
                    //  <560> /  160    fade in logo
                    //  <880> /  240    fade in logo
                    // <1200> /  120    fade in logo
                    // <1600> /  360    fade in logo
                    // <1800> /   80    fade in logo
                    // <2640> /  200    fade in logo
                    // <2840> /  320    delayed logo start on bright background
                    // <8080> / 3560    delayed logo start on bright background
                    //
                    //   360 /  <80>                                     (conflict)
                    //  2320 / <140>                                     (conflict)
                    //  4840 / <240>
                    //  1760 / <280>                                     (conflict)
                    //  4380 / <280>   scene fade in after logo start
                    //    40 / <400>   logo start before broadcast start (conflict)
                    if ((diffBefore > 2840) && (diffAfter <= 280)) diffBefore = INT_MAX;
                    maxBefore = 8080;  // changed from 7400 to 8080
                    break;
                case MT_CHANNELSTART:
                    // select best mark, default: use before
                    // before / after
                    //  40 /  <120>
                    // 720 /  <160>
                    // 600 /  <200>
                    // 720 /  <280>
                    // 400 /  <460>
                    // 820 / <1060>
                    if (diffAfter <= 1060) diffBefore = INT_MAX;
                    maxBefore = 1060;   // chnaged from 1040 to 1060
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_SOUNDSTART:
                        // select best mark, default: use before
                        // before / after
                        //   60 /  <20>
                        //  760 / <880>
                        // 1180 / <920>
                        if ((diffBefore >= 60) && (diffAfter <= 920)) diffBefore = INT_MAX;
                        maxBefore = 5060;  // changed from 4400 to 5060
                        break;
                    case MT_VPSSTART:
                        // select best mark, default: use before
                        // before / after
                        // <1900> /    60
                        // <1520> /  3680  (conflict)
                        // <2820> /  1740

                        // long scene before VPS start (closing scene), short scene after VPS start (broadcast start)
                        //  2160  /  <200>
                        //  2360  /  <320>
                        if ((diffBefore >= 2160) && (diffAfter <= 320)) diffBefore = INT_MAX;

                        //  long scene after VPS start, long static scene or closing credits from end of previous broadcast
                        //   580 /  <1300>
                        //  1860 /  <1580>
                        //  1860 /  <1640>
                        //  2720 /  <2760>
                        //   320 /  <3640>
                        //    80 /  <3720>
                        //  1720 /  <4640>
                        //    40 /  <5480>
                        //  2200 /  <6400>
                        //  1760 /  <6640>
                        //  1440 /  <8240>
                        //   960 / <11320>
                        //   880 / <13440>
                        else if ((diffBefore >= 40) && (diffBefore <= 2720) && (diffAfter >= 1300) && (diffAfter <= 13440)) diffBefore = INT_MAX;

                        maxBefore = 2820;  // changd from 580 to 2820
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
                    mark = marks.Move(mark, sceneStartBefore->position, MT_SCENESTART);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
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
                    maxAfter = 1200;
                    break;
                case MT_ASPECTSTART:
                    maxAfter =  120;
                    break;
                case MT_CHANNELSTART:
                    maxAfter = 1800;  // changed from 1020 to 1800
                    break;
                case MT_MOVEDSTART:
                    switch (mark->newType) {
                    case MT_SOUNDSTART:
                        maxAfter = 2880;  // changed from 1200 to 2880
                        break;
                    case MT_NOBLACKLOWERSTART:
                        maxAfter = 3520;
                        break;
                    case MT_VPSSTART:
                        maxAfter = 11320;  // changed from 9560 to 11320
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
                    mark = marks.Move(mark, sceneStartAfter->position, MT_SCENESTART);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
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
                diffBefore = 1000 * (mark->position - sceneStopBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%6d): found scene stop  (%6d) %5ldms before", mark->position, sceneStopBefore->position, diffBefore);
            }
            if (sceneStopAfter) {
                diffAfter = 1000 * (sceneStopAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%6d): found scene stop  (%6d) %5dms after", mark->position, sceneStopAfter->position, diffAfter);
            }
            // try scene change after stop mark
            if ((sceneStopAfter) && (sceneStopAfter->position != mark->position)) {
                int maxAfter = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    // select best mark (before / after), default: after
                    //  <760> / 1120
                    if ((diffBefore <= 760) && (diffAfter >= 1120)) diffAfter = INT_MAX;
                    maxAfter = 8520;  // changed from 6480 to 8520
                    break;
                case MT_LOGOSTOP:
                    // select best mark (before / after), default: after
                    //    240 /   <80>   very short fade out logo
                    //   2800 /  <160>   short fade out logo (Disney Channel) NEW
                    //   2200 /  <200>   short fade out logo
                    //   1200 /  <200>   short fade out logo
                    //    960 /  <240>   short fade out logo
                    //   1040 /  <280>   short fade out logo (Nickelodeon)
                    //    520 /  <360>   early logo stop before end (TLC)
                    //    400 /  <800>   early logo stop before end
                    //    520 /  <800>   early logo stop before end (Nickelodeon)
                    //    640 /  <840>   early logo stop before end (Nickelodeon)
                    //    440 /  <960>   early logo stop before end (Nickelodeon)
                    //    120 / <2000>   fade out logo (Nickelodeon)
                    //    200 / <2080>   fade out logo (Nickelodeon)
                    //    240 / <4640>   early fade out logo (Disney Channel)
                    //    440 / <4800>   early fade out logo (Nickelodeon)
                    //
                    //  <200> /    80    logo fading out after broadcast end           (conflict)
                    //  <240> /  3760    delayed logo stop                             (conflict)
                    //  <440> /   120    delayed logo stop from frame in background    (conflict)
                    //  <680> /  1160    delayed logo stop from frame in background    (conflict)
                    //  <920> /   120    delayed logo stop from pattern in background  (conflict)
                    // <1720> /  2040    delayed logo stop from pattern in background  (conflict)
                    //
                    // use very near scene change before
                    //   <40> /   240    logo stop detected too late because of bright background
                    //   <40> /   320    logo stop detected too late
                    //   <40> /   760    logo stop detected too late
                    //   <80> /  1040    logo stop detected too late
                    //   <80> /  3240    delayed logo stop from bright background
                    if ((diffBefore <= 80) && (diffAfter >= 240)) diffAfter = INT_MAX;

                    // near scene change before and far scene change after, delayed logo stop
                    //  <120> /  1120    delayed logo stop from bright background
                    //  <120> /  1840    delayed logo stop from bright background
                    //  <120> /  2000    logo stop at start of next broadcast
                    //  <120> /  2960    logo stop at start of next broadcast
                    //  <160> /   240    logo stop in ad
                    //  <160> /  2720    logo stop at start of next broadcast
                    //  <200> /  1520    logo stop after broadcast end
                    //  <240> /  1080    delayed logo stop from pattern in background
                    //  <240> /  2080    delayed logo stop from pattern in background
                    //  <280> /  1600    delayed logo stop from pattern in background
                    //  <320> /   160    delayed logo stop from pattern in background
                    //  <320> /  1680    delayed logo stop from pattern in background
                    //  <360> /   880    delayed logo stop from pattern in background
                    //  <560> /   360    delayed logo stop from pattern in background
                    else if (!criteria.LogoFadeOut(macontext.Info.ChannelName) &&
                             (diffBefore >= 120) && (diffBefore <= 560) && (diffAfter >= 160) && (diffAfter <= 2960)) diffAfter = INT_MAX;

                    // scene change after too far for short fading out logo and too near for long fading out logo -> delayed logo stop
                    //  <760> /   320    delayed logo stop
                    // <1600> /   600    delayed logo stop
                    // <1800> /   760    delayed logo stop
                    // <2400> /   280    delayed logo stop
                    // <2640> /   360    delayed logo stop from pattern in background
                    else if (!criteria.LogoFadeOut(macontext.Info.ChannelName) &&
                             (diffBefore >= 760) && (diffBefore <= 2640) && (diffAfter >= 280) && (diffAfter <= 760)) diffAfter = INT_MAX;

                    // logo stop in separator picture
                    // <4280> / 2080   N24 DOKU
                    else if ((diffBefore == 4280) && (diffAfter == 2080)) diffAfter = INT_MAX;

                    // scene change after too far away, better use scene change before, expect channels with very early fade out logo
                    //  <280> /  2680    delayed logo stop from pattern in background  (conflict)
                    //  <160> /  3920    delayed logo stop from bright background
                    //  <440> /  4080    delayed logo stop from pattern in background
                    //  <680> /  4320    fade out logo in ad
                    //  <840> /  4960    delayed logo stop from bright background
                    // <1080> /  4920    delayed logo stop from pattern in background
                    else if ((diffBefore <= 1080) && (diffAfter >= 2680) &&
                             !CompareChannelName(macontext.Info.ChannelName, "Nickelodeon", IGNORE_HD) &&
                             !CompareChannelName(macontext.Info.ChannelName, "Disney_Channel", IGNORE_HD)) diffAfter = INT_MAX;

                    maxAfter = 5139;  // do not increase, will get scene change in ad, no schene change before because broadcast fade out to ad
                    // TODO detect fade out scene changes
                    break;
                case MT_HBORDERSTOP:
                    // select best mark (before / after), default: after
                    // <440> / 1720    dark opening credits
                    if ((diffBefore <= 440) && (diffAfter >= 1720)) diffAfter = INT_MAX;
                    if (mark->position == marks.GetLast()->position) maxAfter = 10160;   // closing credits overlaps border
                    break;
                case MT_CHANNELSTOP:
                    // select best mark (before / after), default: after
                    //   360 /  <80>  channel change short before last scene

                    //  <80> /  360
                    // <120> /  160
                    // <140> / 1460
                    // <160> / 1440
                    // <340> / 1300
                    // <480> /  200
                    // <600> /  840
                    // <640> /  880
                    if ((diffBefore >= 80) && (diffBefore <= 640) && (diffAfter >= 160) && (diffAfter <= 1460)) diffAfter = INT_MAX;

                    //  <20> /   20
                    //  <40> /  960
                    else if (diffBefore <= 40) diffAfter = INT_MAX;

                    maxAfter = 1460;  // changed from 240 to 1460
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_NOBLACKLOWERSTOP:   // move after closing credits
                        // select best mark (before / after), default: after
                        //  <80> / 2240
                        if ((diffBefore <= 80) && (diffAfter >= 2240)) diffAfter = INT_MAX;
                        maxAfter = 2360;   // changed from 1840 to 2360
                        break;
                    case MT_SOUNDSTOP:
                        // select best mark (before / after), default: after
                        //   800 /   <80>  sound stop short before last scene
                        //  1640 /   <80>
                        //  3680 /   <80>
                        //  4560 /   <80>  long static scene before end of broadcast
                        //  2800 /  <120>
                        //  2840 /  <120>
                        //  2560 /  <160>
                        //   280 /  <680>   sound stop before last scene (conflict)
                        //  9780 / <3260>   sound stop in closing scene
                        // 36100 / <3380>   sound stop in closing scene
                        //
                        //  <800> /   80    sound stop short after last scene (conflict)
                        //
                        //  sound stop short after last scene
                        //   <40> /   40    sound stop short after last scene
                        //   <40> / 1760    sound stop short after last scene
                        //   <40> /  800    sound stop short after last scene
                        //  <400> / 2800    sound stop short after last scene
                        if ((diffBefore >= 40) && (diffBefore <= 400) && (diffAfter >= 40) && (diffAfter <= 2800)) diffAfter = INT_MAX;

                        // long static scene before sound stop is separator picture
                        // <4360> /  840    delayed logo stop from bright background, sound stop after separator picture
                        // <4440> /  560    delayed logo stop from bright background, sound stop after separator picture
                        // <4720> /  280    delayed logo stop from bright background, sound stop after separator picture
                        // <5080> / 1960    delayed logo stop from bright background, sound stop after separator picture
                        // <5080> / 2120    delayed logo stop from bright background, sound stop after separator picture
                        else if ((diffBefore >= 4360) && (diffBefore <= 5080) && (diffAfter >= 280) && (diffAfter <= 2120)) diffAfter = INT_MAX;

                        maxAfter = 3380;
                        break;
                    case MT_VPSSTOP:
                        // select best mark (before / after), default: after
                        //  8400 /    <40>   long static scene before VPS stop
                        //   680 /   <960>
                        //  7640 /  <1540>   static scene after VPS stop and before broadcast end
                        //   960 /  <3000>   static scene after VPS stop and before broadcast end
                        //  2800 /  <3880>   static scene after VPS stop and before broadcast end
                        //  1080 /  <4680>
                        //  3160 /  <4920>
                        //  1200 /  <6960>
                        //  1440 / <12480>   static scene after VPS stop and before broadcast end
                        //  2360 / <17480>   static scene after VPS stop and before broadcast end

                        // scene change short before
                        //  <160> /  3840
                        //  <240> /  2660
                        //  <320> /   680
                        //  <320> /  1360
                        //  <440> /  1560
                        // <1040> /   800   (conflict)
                        // <1440> /  6040   (conflict)
                        // <8440> /  3920   (conflict)
                        if ((diffBefore >= 160) && (diffBefore <= 440) && (diffAfter >= 680) && (diffAfter <= 3840)) diffAfter = INT_MAX;

                        // long opening scene from next broadcast
                        //   <80> /  6720    long opening scene from next broadcast
                        else if ((diffBefore <= 80) && (diffAfter >= 6720)) diffAfter = INT_MAX;   // long opening scene from next broadcast

                        maxAfter = 17480;  // changed from 11200 to 17480
                        break;
                    case MT_CLOSINGCREDITSSTOP:
                        maxAfter = 1080;
                        break;
                    case MT_NOADINFRAMESTOP:
                        // select best mark (before / after), default: after
                        // <200> / 760
                        // <440> / 360
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
                    mark = marks.Move(mark, sceneStopAfter->position, MT_SCENESTOP);
                    if (mark) {
                        moved = true;
                        save  = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
            // try scene change before stop mark
            if (!moved && (sceneStopBefore) && (sceneStopBefore->position != mark->position)) { // logo stop detected too late, move backwards
                int maxBefore = 0;
                switch (mark->type) {
                case MT_ASSUMEDSTOP:
                    maxBefore = 1120;
                    break;
                case MT_LOGOSTOP:
                    maxBefore = 4719;  // changed from 13279 to 4719, do not increse, closing credits detection will fail
                    break;
                case MT_HBORDERSTOP:
                    if (mark->position == marks.GetLast()->position) maxBefore = 440;
                    break;
                case MT_CHANNELSTOP:
                    maxBefore = 640;  // changed from 600 to 640
                    break;
                case MT_MOVEDSTOP:
                    switch (mark->newType) {
                    case MT_NOBLACKLOWERSTOP:
                        maxBefore = 80;
                        break;
                    case MT_SOUNDSTOP:
                        maxBefore = 9960;   // changed from 4720 to 9960
                        break;
                    case MT_VPSSTOP:
                        maxBefore = 8440;   // chaned from 1320 to 1440 to 8440
                        break;
                    case MT_NOADINFRAMESTOP:  // correct the missed start of ad in frame before stop mark
                        maxBefore = 1600;
                        break;
                    default:
                        maxBefore = 0;
                    }
                    break;
                default:
                    maxBefore = 0;
                }
                if (diffBefore <= maxBefore) {
                    mark = marks.Move(mark, sceneStopBefore->position, MT_SCENESTOP);
                    if (mark) {
                        save = true;
                    }
                    else {
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                        break;
                    }
                }
            }
        }
        FREE(strlen(markType)+1, "text");
        free(markType);
        FREE(strlen(markOldType)+1, "text");
        free(markOldType);
        FREE(strlen(markNewType)+1, "text");
        free(markNewType);

        mark = mark->Next();
    }
    // save marks
    if (save) marks.Save(directory, &macontext, false);
}


void cMarkAdStandalone::ProcessOverlap() {
    if (abortNow) return;
    if (duplicate) return;
    if (!ptr_cDecoder) return;
    if ((length == 0) || (startTime == 0)) {  // no recording length or start time from info file
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
        evaluateLogoStopStartPair = NULL;
        return;
    }
    if (time(NULL) < (startTime+(time_t) length)) return;  // we are running during recording and has not reached end of recording

    LogSeparator(true);
    dsyslog("ProcessOverlap(): start overlap detection");
    DebugMarks();     //  only for debugging

    if (!macontext.Video.Info.framesPerSecond) {
        isyslog("WARNING: assuming fps of 25");
        macontext.Video.Info.framesPerSecond = 25;
    }

    bool save = false;
    cMark *p1 = NULL;
    cMark *p2 = NULL;

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    if (marks.Count() >= 4) {  // overlap is in inner marks, so we must have at least 4 marks
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
            if (!p1 || !p2) break;  // failed move will return NULL pointer
            p1 = p2->Next();
            if (p1) {
                p2 = p1->Next();
            }
            else {
                p2 = NULL;
            }
        }
    }

    // check last stop mark if closing credits follows
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::ProcessOverlap(): check last stop mark for advertisement in frame with logo or closing credits");
    if (!evaluateLogoStopStartPair) {  // not set if we have no logo marks at all
        evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair();
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    }
    cMark *lastStop = marks.GetLast();
    if (lastStop) {
        // check end mark
        if ((lastStop->type == MT_LOGOSTOP) ||
                ((lastStop->type == MT_MOVEDSTOP) && (lastStop->newType != MT_NOADINFRAMESTOP) && (lastStop->newType != MT_TYPECHANGESTOP))) { // prevent double detection of ad in frame and closing credits
            if (criteria.GetClosingCreditsState(lastStop->position) >= CRITERIA_UNKNOWN) {
                dsyslog("cMarkAdStandalone::ProcessOverlap(): search for closing credits after logo end mark");
                if (MoveLastStopAfterClosingCredits(lastStop)) {
                    save = true;
                    dsyslog("cMarkAdStandalone::ProcessOverlap(): moved logo end mark after closing credit");
                }
            }
        }
        // check border end mark
        if ((lastStop->type == MT_HBORDERSTOP)) {
            dsyslog("cMarkAdStandalone::ProcessOverlap(): search for closing credits after border or moved end mark");
            if (MoveLastStopAfterClosingCredits(lastStop)) {
                save = true;
                dsyslog("cMarkAdStandalone::ProcessOverlap(): moved border end mark after closing credit");
            }
        }
    }
    FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
    delete evaluateLogoStopStartPair;
    evaluateLogoStopStartPair = NULL;

    if (save) marks.Save(directory, &macontext, false);
    dsyslog("cMarkAdStandalone::ProcessOverlap(): end");
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
        esyslog("cMarkAdStandalone::ProcessFrame(): video not initialized");
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
    if (ptr_cDecoder->GetFrameInfo(&macontext, criteria.GetDetectionState(MT_VIDEO), macontext.Config->fullDecode, criteria.GetDetectionState(MT_SOUNDCHANGE),  criteria.GetDetectionState(MT_CHANNELCHANGE))) {
        if (ptr_cDecoder->IsVideoPacket()) {
            if ((ptr_cDecoder->GetFileNumber() == 1) && ptr_cDecoder->IsInterlacedVideo()) {
                // found some Finnish H.264 interlaced recordings who changed real bite rate in second TS file header
                // frame rate can not change, ignore this and keep frame rate from first TS file
                if (!macontext.Video.Info.interlaced && (macontext.Info.vPidType==MARKAD_PIDTYPE_VIDEO_H264) && (ptr_cDecoder->GetVideoAvgFrameRate() == 25) && (ptr_cDecoder->GetVideoRealFrameRate() == 50)) {
                    dsyslog("cMarkAdStandalone::ProcessFrame(): change internal frame rate to handle H.264 interlaced video");
                    macontext.Video.Info.framesPerSecond *= 2;
                    CalculateCheckPositions(macontext.Info.tStart * macontext.Video.Info.framesPerSecond);  // recalculate position with new frame rate
                }
                macontext.Video.Info.interlaced = true;
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

            if (criteria.GetDetectionState(MT_VIDEO)) {
                sMarkAdMarks *vmarks = video->Process(iFrameBefore, iFrameCurrent, frameCurrent);
                if (vmarks) {
                    for (int i = 0; i < vmarks->Count; i++) {
                        AddMark(&vmarks->Number[i]);
                    }
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
        // check audio marks (channel changes and silence)
        if (ptr_cDecoder->IsAudioPacket()) {
            sMarkAdMarks *amarks = audio->Process(frameCurrent);  // class audio will take frame number from macontext->Audio.Info
            if (amarks) {
                for (int i = 0; i < amarks->Count; i++) AddMark(&amarks->Number[i]);
            }
        }

        // turn on all detection for end part even if we use stronger marks, just in case we will get no strong end mark
        if (!restartLogoDetectionDone && (frameCurrent > (iStopA - (macontext.Video.Info.framesPerSecond * 300))) && (iStart == 0)) { // not before start part done
            // changed from 240 to 300
            dsyslog("cMarkAdStandalone::ProcessFrame(): enter end part at frame (%d), reset detector status", frameCurrent);
            video->Clear(true);
            criteria.SetDetectionState(MT_ALL, true);
            restartLogoDetectionDone = true;
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
    if (ptr_cDecoder) ALLOC(sizeof(*ptr_cDecoder), "ptr_cDecoder");
    else              return;
    CheckIndexGrowing();
    bool nextFile = true;
    while(nextFile && ptr_cDecoder && ptr_cDecoder->DecodeDir(directory)) {
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
        while(ptr_cDecoder && ptr_cDecoder->GetNextPacket(true, true)) {
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
                marksTMP.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, ptr_cDecoder->GetFrameNumber(), "timer start", true);
                marksTMP.Save(macontext.Config->recDir, &macontext, true);
                macontext.Info.isStartMarkSaved = true;
            }

            if (!cMarkAdStandalone::ProcessFrame(ptr_cDecoder)) {
                nextFile = false;
                break;
            }
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
                dsyslog("cMarkAdStandalone::GetRecordingStart(): getting recording start from directory");
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

    int len = strlen(macontext.Info.ChannelName);
    if (!len) return false;

    gettimeofday(&startTime1, NULL);
    dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s", macontext.Config->logoDirectory);
    dsyslog("cMarkAdStandalone::CheckLogo(): searching logo for %s", macontext.Info.ChannelName);
    DIR *dir = opendir(macontext.Config->logoDirectory);
    if (!dir) {
        esyslog("logo cache directory %s does not exist, use /tmp", macontext.Config->logoDirectory);
        strcpy( macontext.Config->logoDirectory, "/tmp");
        dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s", macontext.Config->logoDirectory);
        dir = opendir(macontext.Config->logoDirectory);
        if (!dir) exit(1);
    }

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
                    gettimeofday(&endTime1, NULL);
                    return true;
                }
            }
            closedir(recDIR);
        }
        isyslog("no logo for %s %d:%d found in recording directory %s, trying to extract logo from recording", macontext.Info.ChannelName, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den, macontext.Config->recDir);
        ptr_cExtractLogo = new cExtractLogo(&macontext, &criteria, macontext.Info.AspectRatio, recordingIndexMark);
        ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
        int startPos =  macontext.Info.tStart * 25;  // search logo from assumed start, we do not know the frame rate at this point, so we use 25
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = ptr_cExtractLogo->SearchLogo(&macontext, &criteria, startPos, false);
        for (int retry = 2; retry <= 8; retry++) {  // do not reduce, we will not get some logos
            startPos += 5 * 60 * macontext.Video.Info.framesPerSecond; // next try 5 min later, now we know the frame rate
            if (endpos > LOGOSEARCH_FOUND) {  // no logo found, endpos is last frame of search
                dsyslog("cMarkAdStandalone::CheckLogo(): no logo found in recording, retry in %ind part of the recording at frame (%d)", retry, startPos);
                endpos = ptr_cExtractLogo->SearchLogo(&macontext, &criteria, startPos, false);
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
            gettimeofday(&endTime1, NULL);
            return true;
        }
        else {
            dsyslog("cMarkAdStandalone::CheckLogo(): logo search failed");
            gettimeofday(&endTime1, NULL);
            return false;
        }
    }
    gettimeofday(&endTime1, NULL);
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

                // try VPS start event from markad.vps
                macontext.Info.tStart = vps->GetStart(); // VPS start mark
                if (macontext.Info.tStart >= 0) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): VPS start event at offset:           %5ds -> %d:%02d:%02dh", macontext.Info.tStart,  macontext.Info.tStart / 3600, (macontext.Info.tStart % 3600) / 60, macontext.Info.tStart % 60);
                    int vpsStop = vps->GetStop();
                    if (vpsStop > macontext.Info.tStart) {
                        dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop  event at offset:           %5ds -> %d:%02d:%02dh", vpsStop, vpsStop / 3600, (vpsStop % 3600) / 60, vpsStop % 60);
                        dsyslog("cMarkAdStandalone::LoadInfo(): broadcast length from vdr info file: %5ds -> %d:%02d:%02dh", length, length / 3600, (length % 3600) / 60, length % 60);
                        int lengthVPS = vpsStop - macontext.Info.tStart;
                        int diff      = lengthVPS - length;
                        dsyslog("cMarkAdStandalone::LoadInfo(): broadcast length from VPS events:    %5ds -> %d:%02d:%02dh, %ds longer than length from vdr info file", lengthVPS, lengthVPS / 3600, (lengthVPS % 3600) / 60, length % 60, diff);
                        // changed from  506 to  298
                        // changed from -570 to -620
                        if ((diff >= 298) || (diff < -620)) {
                            dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop event seems to be invalid, use length from vdr info file");
                            vps->SetStop(-1);  // set VPS stop event to invalid
                        }
                        else {
                            dsyslog("cMarkAdStandalone::LoadInfo(): VPS events seems to be valid, use length from VPS events");
                            length = lengthVPS;
                        }
                    }
                }
                if (macontext.Info.timerVPS) { //  VPS controlled recording start, we guess assume broascast start 45s after recording start
                    isyslog("VPS controlled recording start");
                    if (macontext.Info.tStart < 0) {
                        dsyslog("cMarkAdStandalone::LoadInfo(): no VPS start event found");
                        macontext.Info.tStart = 45;
                    }
                }

                // try to get broadcast start offset from file infos
                if (macontext.Info.tStart < 0) {
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
    char *libver = NULL;
    if (asprintf(&libver, "%i.%i.%i", ver >> 16 & 0xFF, ver >> 8 & 0xFF, ver & 0xFF) != -1) {
        ALLOC(strlen(libver)+1, "libver");
        isyslog("using libavcodec.so.%s (%d) with %i threads", libver, ver, config->threads);
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

    if (bIgnoreTimerInfo) {
        isyslog("timer info usage disabled by user");
    }
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
    char *recName = strrchr(tmpDir, '/');
    if (!recName) {
        dsyslog("cMarkAdStandalone::cMarkAdStandalone(): failed to find last '/'");
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

    // load VPS events
    vps = new cVPS(directory);
    ALLOC(sizeof(*vps), "vps");

    if (!LoadInfo()) {
        esyslog("failed loading info - logo %s%sdisabled", (config->logoExtraction != -1) ? "extraction" : "detection", bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
        macontext.Info.tStart = iStart = iStop = iStopA = 0;
        criteria.SetDetectionState(MT_LOGOCHANGE, false);
    }
    else {
        if (!CheckLogo() && (config->logoExtraction==-1) && (config->autoLogo == 0)) {
            isyslog("no logo found, logo detection disabled");
            criteria.SetDetectionState(MT_LOGOCHANGE, false);
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
        video = new cMarkAdVideo(&macontext, &criteria, recordingIndex);
        ALLOC(sizeof(*video), "video");
        audio = new cMarkAdAudio(&macontext, recordingIndex);
        ALLOC(sizeof(*audio), "audio");
        if (macontext.Info.ChannelName) isyslog("channel: %s", macontext.Info.ChannelName);
    }

    chkSTART = chkSTOP = INT_MAX;
}


cMarkAdStandalone::~cMarkAdStandalone() {
    if (!abortNow) marks.Save(directory, &macontext, true);
    if ((!abortNow) && (!duplicate)) {
        LogSeparator();

        // broadcast length without advertisement
        dsyslog("recording statistics: -----------------------------------------------------------------------");
        int lengthFrames = marks.Length();
        int lengthSec    = lengthFrames / macontext.Video.Info.framesPerSecond;
        dsyslog("broadcast length without advertisement: %6d frames, %6ds -> %d:%02d:%02dh", marks.Length(), lengthSec, lengthSec / 3600, (lengthSec % 3600) / 60,  lengthSec % 60);

        // recording length from VPS eventsA
        int vpsLength = vps->Length();
        if (vpsLength > 0) {
            dsyslog("recording length from VPS events:                      %6ds -> %d:%02d:%02dh", vpsLength, vpsLength / 3600, (vpsLength % 3600) / 60,  vpsLength % 60);
            int adQuote = 100 * (vpsLength - lengthSec) / vpsLength;
            if (adQuote > 37) esyslog("advertisement quote: %d%% very high, marks can be wrong", adQuote);  // changed from 34 to 37
            else dsyslog("advertisement quote: %d%%", adQuote);
        }

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
        if ((sec + usec) > 0) dsyslog("pass 3 (mark optimation):    time %5lds -> %ld:%02ld:%02ldh", sec, sec / 3600, (sec % 3600) / 60,  sec % 60);

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

        gettimeofday(&endAll, NULL);
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
    if (vps) {
        FREE(sizeof(*vps), "vps");
        delete vps;
        vps = NULL;
    }

    if (evaluateLogoStopStartPair) {
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
        evaluateLogoStopStartPair = NULL;
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
           "                  only valid together with cmd \"before\"\n"
           "                  start markad immediately when called together with \"before\" as cmd\n"
           "                  if online=1, markad starts online for live-recordings only\n"
           "                     online=2, markad starts online for every recording\n"
           "                  live-recordings are identified by having a '@' in the filename\n"
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
           "                  full re-encode video generated by --cut\n"
           "                  use it only on powerful CPUs, it will double overall run time\n"
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

        int option = getopt_long(argc, argv, "bd:i:l:p:r:vBGIL:ORT:V", long_options, &option_index);
        if (option == -1) break;

        switch (option) {
        case 'b':
            // --background
            bFork = SYSLOG = true;
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
                if ( strstr(argv[optind], ".rec") != NULL ) {
                    recDir = realpath(argv[optind], NULL);
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
                const char *err = strerror(errno);
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
        dsyslog("markad IO priority class  %i",IOPrio);

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
            gettimeofday(&startTime2, NULL);
            cmasta->ProcessFiles();
            gettimeofday(&endTime2, NULL);
        }
        if (!bPass1Only) {
            gettimeofday(&startTime3, NULL);
            cmasta->LogoMarkOptimization();      // logo mark optimization
            gettimeofday(&endTime3, NULL);

            gettimeofday(&startTime4, NULL);
            cmasta->ProcessOverlap();  // overlap detection
            gettimeofday(&endTime4, NULL);

            cmasta->BlackScreenOptimization();   // mark optimization with black scene
            cmasta->SilenceOptimization();       // mark optimization with mute scene
            cmasta->BorderMarkOptimization();    // vborder and hborder mark optimization (to correct too eary black screen start marks from closing credit of previous recording)
            cmasta->BlackLowerOptimization();    // mark optimization with lower black border
            cmasta->SceneChangeOptimization();   // final optimization with scene changes (if we habe nothing else, try this as last resort)

        }
        if (config.MarkadCut) {
            gettimeofday(&startTime5, NULL);
            cmasta->MarkadCut();
            gettimeofday(&endTime5, NULL);
        }

        gettimeofday(&startTime6, NULL);
#ifdef DEBUG_MARK_FRAMES
        cmasta->DebugMarkFrames(); // write frames picture of marks to recording directory
#endif
        gettimeofday(&endTime6, NULL);

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
