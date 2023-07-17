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
    chkSTART = iStartA + macontext.Video.Info.framesPerSecond * 480; //  fit for later broadcast start
    chkSTOP = startframe + macontext.Video.Info.framesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:   %4ds (%3dmin %2ds)", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame:  %5d  (%3dmin %2ds)", iStartA, static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStartA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:  %6d  (%3dmin %2ds)", iStopA, static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(iStopA / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTART set to:     %6d  (%3dmin %2ds)", chkSTART, static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTART / macontext.Video.Info.framesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTOP set to:      %6d  (%3dmin %2ds)", chkSTOP, static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond / 60), static_cast<int>(chkSTOP / macontext.Video.Info.framesPerSecond) % 60);
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
        if (markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) {
            cMark *hBorderLast = marks.GetPrev(INT_MAX, MT_HBORDERCHANGE, 0xF0);
            if (hBorderLast && (hBorderLast->type == MT_HBORDERSTOP)) {
                dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): last hboder mark (%d) is stop mark, this must be end mark", hBorderLast->position);
                end = hBorderLast;
            }
        }
    }
    if (!end) dsyslog("cMarkAdStandalone::Check_HBORDERSTOP(): no MT_HBORDERSTOP mark found");
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
        if (macontext.Video.Logo.isInBorder) {
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
                else {
                    dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): vertial border start and stop found, delete weak marks except start mark");
                    marks.DelWeakFromTo(marks.GetFirst()->position + 1, INT_MAX, MT_VBORDERCHANGE);
                }
            }
        }
    }
    else dsyslog("cMarkAdStandalone::Check_VBORDERSTOP(): no MT_VBORDERSTOP mark found");
    return end;
}


// detect short logo stop/start before assumed stop mark, they can be undetected info logos or text previews over the logo (e.g. SAT.1)
// only called if we are sure this is the correct logo end mark (closing credit detected)
// prevent to later move end mark to previous logo stop mark from undetected logo change
void cMarkAdStandalone::CleanupUndetectedInfoLogo(const cMark *end) {
    while (true) {
        cMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
        if (!prevLogoStart) break;
        cMark *prevLogoStop = marks.GetPrev(prevLogoStart->position, MT_LOGOSTOP);
        if (!prevLogoStop) break;
        int deltaEndLogoStart = 1000 * (end->position - prevLogoStart->position)          / macontext.Video.Info.framesPerSecond;
        int adLength          = 1000 * (prevLogoStart->position - prevLogoStop->position) / macontext.Video.Info.framesPerSecond;
        dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): logo start (%5d) stop (%5d): start %dms before end mark, ad length %dms", prevLogoStart->position, prevLogoStop->position, deltaEndLogoStart, adLength);
        if ((deltaEndLogoStart <= 30000) && (adLength <= 17000)) {
            dsyslog("cMarkAdStandalone::CleanupUndetectedInfoLogo(): logo start (%5d) stop (%5d): undetected info logo or text preview over the logo, delete marks", prevLogoStart->position, prevLogoStop->position);
            marks.Del(prevLogoStart);
            marks.Del(prevLogoStop);
        }
        else break;
    }
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
    // if we use channel marks
    if (markCriteria.GetMarkTypeState(MT_CHANNELCHANGE) == CRITERIA_USED) {
        // delete short channel stop/start pairs, they are stream errors
        cMark *channelStop = marks.GetNext(-1, MT_CHANNELSTOP);
        while (true) {
            if (!channelStop) break;
            cMark *channelStart = marks.GetNext(channelStop->position, MT_CHANNELSTART);
            if (!channelStart) break;
            int lengthChannel = 1000 * (channelStart->position - channelStop->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckStop(): channel stop (%6d) start (%6d): length %6dms", channelStop->position, channelStart->position, lengthChannel);
            if (lengthChannel <= 280) {
                dsyslog("cMarkAdStandalone::CheckStop(): channel stop (%6d) start (%6d): length too short, delete marks", channelStop->position, channelStart->position);
                int tmp = channelStop->position;
                marks.Del(channelStop->position);
                marks.Del(channelStart->position);
                channelStop = marks.GetNext(tmp, MT_CHANNELSTOP);
            }
            else channelStop = marks.GetNext(channelStop->position, MT_CHANNELSTOP);
        }
    // cleanup logo marks near by channel start marks, they are useless info logo
        cMark *channelStart = marks.GetNext(-1, MT_CHANNELSTART);
        while (channelStart) {
#define CHANNEL_LOGO_MARK 60
            cMark *logo = marks.GetAround(CHANNEL_LOGO_MARK * macontext.Video.Info.framesPerSecond, channelStart->position, MT_LOGOCHANGE, 0xF0);
            while (logo) {
                dsyslog("cMarkAdStandalone::CheckStop(): delete logo mark (%d) around channel start (%d)", logo->position, channelStart->position);
                marks.Del(logo->position);
                logo = marks.GetAround(CHANNEL_LOGO_MARK * macontext.Video.Info.framesPerSecond, channelStart->position, MT_LOGOCHANGE, 0xF0);
            }
            channelStart = marks.GetNext(channelStart->position, MT_CHANNELSTART);
        }
    }

    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStop(): marks after first cleanup:");
    DebugMarks();     //  only for debugging
    dsyslog("cMarkAdStandalone::CheckStop(): start end mark selection");

// try MT_CHANNELSTOP
    cMark *end = marks.GetAround(390 * macontext.Video.Info.framesPerSecond, iStopA, MT_CHANNELSTOP);   // changed from 360 to 390
    if (end) {
        dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found at frame %i", end->position);
        cMark *cStart = marks.GetPrev(end->position, MT_CHANNELSTART);      // if there is short befor a channel start, this stop mark belongs to next recording
        if (cStart) {
            if ((end->position - cStart->position) < (macontext.Video.Info.framesPerSecond * 120)) {
                dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTART found short before at frame %i with delta %ds, MT_CHANNELSTOP is not valid, try to find stop mark short before", cStart->position, static_cast<int> ((end->position - cStart->position) / macontext.Video.Info.framesPerSecond));
                end = marks.GetAround(macontext.Video.Info.framesPerSecond * 120, iStopA - (macontext.Video.Info.framesPerSecond * 120), MT_CHANNELSTOP);
                if (end) dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found short before at frame (%d)", end->position);
            }
            else {
                cMark *cStartFirst = marks.GetNext(0, MT_CHANNELSTART);  // get first channel start mark
                cMark *movedFirst  = marks.First();                      // maybe first mark is a moved channel mark
                if (movedFirst && (movedFirst->type == MT_MOVEDSTART) && (movedFirst->oldType == MT_CHANNELSTART)) cStartFirst = movedFirst;
                if (cStartFirst) {
                    int deltaC = (end->position - cStartFirst->position) / macontext.Video.Info.framesPerSecond;
                    if (deltaC < 287) {  // changed from 305 to 287, found shortest last part, do not reduce
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
        // if we have 16:9 broadcast, every aspect stop without aspect start before is end mark, maybe it is very early in case of wrong recording length
        if ((macontext.Info.AspectRatio.num == 16) && (macontext.Info.AspectRatio.den == 9)) {
            cMark *aspectStop = marks.GetNext(0, MT_ASPECTSTOP);
            if (aspectStop) {
                const cMark *aspectStart = marks.GetPrev(aspectStop->position, MT_ASPECTSTOP);
                if (!aspectStart) {
                    dsyslog("cMarkAdStandalone::CheckStop(): we have 16:9 bradcast and MT_ASPECTSTOP at frame (%d) without MT_ASPECTSTART before, this is and mark", aspectStop->position);
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
                if (stopBefore) { // maybe real stop mark was deleted because on same frame as logo/hborder stop mark
                    int diff = (iStopA - stopBefore->position) /  macontext.Video.Info.framesPerSecond;
                    char *markType = marks.TypeToText(stopBefore->type);
                    dsyslog("cMarkAdStandalone::CheckStop(): found %s stop mark (%d) before aspect ratio end mark (%d), %ds before assumed stop", markType, stopBefore->position, end->position, diff);
                    FREE(strlen(markType)+1, "text");
                    free(markType);
                    if (diff <= 682) { // changed from 312 to 682, for broadcast length from info file too long
                        dsyslog("cMarkAdStandalone::CheckStop(): advertising before aspect ratio change, use stop mark before as end mark");
                        end = stopBefore;
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_ASPECTSTOP mark found");
    }

// try MT_HBORDERSTOP
    if ((markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) ||   // try hborder end if hborder used even if we got another end mark, maybe we found a better one
        (!end && (markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) >= CRITERIA_UNKNOWN))) end = Check_HBORDERSTOP();

// try MT_VBORDERSTOP
    if (!end && (markCriteria.GetMarkTypeState(MT_VBORDERCHANGE) >= CRITERIA_UNKNOWN)) end = Check_VBORDERSTOP();

// try MT_LOGOSTOP
    if (!end) {  // try logo stop mark
        // remove logo change marks
        RemoveLogoChangeMarks();
        // cleanup very short start/stop pairs around possible end marks, these are logo detection failures
        LogSeparator(false);
        dsyslog("cMarkAdStandalone::CheckStop(): check logo end mark (cleanup very short logo start/stop pairs around possible logo end marks)");
        while (true) {
            end = marks.GetAround(400 * macontext.Video.Info.framesPerSecond, iStopA, MT_LOGOSTOP);
            if (end) {
                int iStopDelta = (iStopA - end->position) / macontext.Video.Info.framesPerSecond;
                #define MAX_LOGO_BEFORE_ASSUMED 395   // changed from 304 to 395
                dsyslog("cMarkAdStandalone::CheckStop(): MT_LOGOSTOP found at frame (%d), %ds (expect < %ds) before assumed stop (%d)", end->position, iStopDelta, MAX_LOGO_BEFORE_ASSUMED, iStopA);
                if (iStopDelta > MAX_LOGO_BEFORE_ASSUMED) {
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

        // for broadcast without hborder check border start mark from next bradcast before logo stop
        // in this case logo stop mark is from next recording, use border start mark as end mark
        bool typeChange = false;
        if (end && (markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) <= CRITERIA_UNKNOWN)) {
            cMark *hBorderStart = marks.GetPrev(end->position, MT_HBORDERSTART);
            if (hBorderStart) {
                const cMark *hBorderStartPrev = marks.GetPrev(hBorderStart->position, MT_HBORDERSTART);
                if (!hBorderStartPrev) {
                    int deltahBorder = (hBorderStart->position - iStopA) / macontext.Video.Info.framesPerSecond;
                    int deltaLogo    = (end->position          - iStopA) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStop(): found MT_HBORDERSTART at (%d) %ds after assumed end (and no other MT_HBORDERSTART before), logo stop mark at (%d) %ds after assumed end", hBorderStart->position, deltahBorder, end->position, deltaLogo);
                    if ((deltaLogo >= 0) && (deltahBorder >= -1)) {
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark at (%d) %ds after assumed end is invalid, use MT_HBORDERSTART (%d) as end mark", end->position, deltaLogo, hBorderStart->position);
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
            const cMark *logoStart = marks.GetNext(end->position, MT_LOGOSTART);
            if (logoStart && evaluateLogoStopStartPair && (evaluateLogoStopStartPair->GetIsClosingCredits(end->position, logoStart->position) == STATUS_YES)) {
                dsyslog("cMarkAdStandalone::CheckStop(): closing credits after this logo stop (%d), this is the end mark", end->position);
                CleanupUndetectedInfoLogo(end);
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
                    CleanupUndetectedInfoLogo(end);
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

        // check previous logo stop mark against VPS stop event, if any
        if (end) {
            cMark *prevLogoStop = marks.GetPrev(end->position, MT_LOGOSTOP); // maybe different if deleted above
            if (prevLogoStop) {
                int vpsOffset = vps->GetStop(); // get VPS stop mark
                if (vpsOffset >= 0) {
                    int vpsStopFrame = recordingIndexMark->GetFrameFromOffset(vpsOffset * 1000);
                    int diffAfterVPS = (prevLogoStop->position - vpsStopFrame) / macontext.Video.Info.framesPerSecond;
                    if (diffAfterVPS >= 0) {
                        dsyslog("cMarkAdStandalone::CheckStop(): VPS stop event at (%d) is %ds after previous logo stop (%d), use this as end mark", vpsStopFrame, diffAfterVPS, prevLogoStop->position);
                        end = prevLogoStop;
                    }
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_LOGOSTOP mark found");
    }

// try black screen mark as end mark
    if (!end) {
        cMark *blackEnd = blackMarks.GetNext(iStopA - (4 * macontext.Video.Info.framesPerSecond), MT_NOBLACKSTOP);    // accept 4s before iStopA
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
                end = marks.Add(MT_NOBLACKSTOP, MT_UNDEFINED, MT_UNDEFINED, blackStop->position, "black screen", false);
                dsyslog("cMarkAdStandalone::CheckStop(): black screen end mark (%d) %ds after assumed stop (%d)", end->position, diff, iStopA);
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no black screen end mark found near assumed stop (%d)", iStopA);
    }

    if (end) {
        indexToHMSF    = marks.IndexToHMSF(end->position);
        if (indexToHMSF) { ALLOC(strlen(indexToHMSF)+1, "indexToHMSF"); }
        char *markType = marks.TypeToText(end->type);
        if (indexToHMSF && markType) {
            isyslog("using %s stop mark on position (%i) at %s as end mark", markType, end->position, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
            FREE(strlen(markType)+1, "text");
            free(markType);
        }
    }

    // no end mark found, try if we can use a start mark of next bradcast as end mark
    if (!end) {  // no valid stop mark found, try if there is a MT_CHANNELSTART from next broadcast
        cMark *channelStart = marks.GetNext(iStopA, MT_CHANNELSTART);
        if (channelStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): use channel start mark (%d) from next broadcast as end mark", channelStart->position);
            marks.ChangeType(channelStart, MT_STOP);
            end = channelStart;
        }
    }
    if (!end || (end->type == MT_NOBLACKSTOP)) { // try to get hborder start mark from next broadcast as stop mark
        cMark *hBorderStart = marks.GetNext(iStopA, MT_HBORDERSTART);
        if (hBorderStart) {
            dsyslog("cMarkAdStandalone::CheckStop(): use hborder start mark (%d) from next broadcast as end mark", hBorderStart->position);
            marks.ChangeType(hBorderStart, MT_STOP);
            end = hBorderStart;
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

    // now we have a end mark, check if the could follow closing credits
    if (end && (end->type == MT_LOGOSTOP)) {
        cMark *nextLogoStart = marks.GetNext(end->position);
        if (nextLogoStart) {
            int closingCreditsLength = (nextLogoStart->position - end->position) / macontext.Video.Info.framesPerSecond;
            if (closingCreditsLength <= 2) {
                dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) %ds after end mark (%d), no closing credits without logo follows", nextLogoStart->position, closingCreditsLength, end->position);
                markCriteria.SetClosingCreditsState(CRITERIA_UNAVAILABLE);
            }
        }
    }

    // delete all marks after end mark
    if (end) { // be save, if something went wrong end = NULL
        dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after final stop mark at (%d)", end->position);
        marks.DelTill(end->position, false);
    }
    else esyslog("could not find a end mark");

    // delete all black screen marks expect start or end mark
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

    // cleanup detection failures (e.g. very long dark scenes)
    if (markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelType(MT_HBORDERCHANGE, 0xF0);
    if (markCriteria.GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_UNAVAILABLE) marks.DelType(MT_VBORDERCHANGE, 0xF0);

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

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &markCriteria, ptr_cDecoder, recordingIndexMark, NULL);
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
        marks.Move(stopMark, newPosition, MT_UNDEFINED, "closing credits");
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

    cExtractLogo *ptr_cExtractLogoChange = new cExtractLogo(&macontext, macontext.Video.Info.AspectRatio, recordingIndexMark);
    ALLOC(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &markCriteria, ptr_cDecoderLogoChange, recordingIndexMark, evaluateLogoStopStartPair);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    // loop through all logo stop/start pairs
    int endRange = 0;  // if we are called by CheckStart, get all pairs to detect at least closing credits
    if (iStart == 0) endRange = iStopA - (26 * macontext.Video.Info.framesPerSecond); // if we are called by CheckStop, get all pairs after this frame to detect at least closing credits
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
        if (indexToHMSFStop) { ALLOC(strlen(indexToHMSFStop)+1, "indexToHMSF"); }

        indexToHMSFStart = marks.IndexToHMSF(startPosition);
        if (indexToHMSFStart) { ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF"); }

        if (indexToHMSFStop && indexToHMSFStart) {
            dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%d) at %s and logo start (%d) at %s, isInfoLogo %d", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart, isInfoLogo);
        }
        if (ptr_cDetectLogoStopStart->Detect(stopPosition, startPosition)) {
            bool doInfoCheck = true;
            // check for closing credits if no other checks will be done, only part of the loop elements in recording end range
            if ((isInfoLogo <= STATUS_NO) && (isLogoChange <= STATUS_NO)) ptr_cDetectLogoStopStart->ClosingCredit();

            // check for info logo
            if ((iStart > 0) && (isStartMarkInBroadcast == STATUS_YES)) {  // we are called by CheckStart and we are in broadcast
                                                                           // do not delete info logo, it can be introduction logo, it looks the same
                                                                           // expect we have another start very short before
                cMark *lStartBefore = marks.GetPrev(stopPosition, MT_LOGOSTART);
                if (lStartBefore) {
                    int diffStart = 1000 * (stopPosition - lStartBefore->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): logo start (%d) %dms before stop mark (%d)", lStartBefore->position, diffStart, stopPosition);
                    if (diffStart > 1240) {  // do info logo check if we have a logo start mark short before, some channel send a early info log after boradcast start
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
                    if (macontext.Video.Info.interlaced) aMark->position = aMark->position + 2;
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


void cMarkAdStandalone::CheckStart() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", frameCurrent, chkSTART);
    dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %i", iStartA);
    DebugMarks();     //  only for debugging
#define IGNORE_AT_START 12   // ignore this number of frames at the start for start marks, they are initial marks from recording before, changed from 11 to 12

    // set initial mark criterias
    if (marks.Count(MT_HBORDERSTART) == 0) markCriteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no hborder start, broadcast can not have hborder
    else if ((marks.Count(MT_HBORDERSTART) == 1) && (marks.Count(MT_HBORDERSTOP) == 0)) markCriteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

    if (marks.Count(MT_VBORDERSTART) == 0) markCriteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);  // if we have no vborder start, broadcast can not have vborder
    else if ((marks.Count(MT_VBORDERSTART) == 1) && (marks.Count(MT_VBORDERSTOP) == 0)) markCriteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_AVAILABLE);  // if we have a vborder start and no vboder stop

    int hBorderStopPosition = -1;
    int delta = macontext.Video.Info.framesPerSecond * 120;

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
        dsyslog("cMarkAdStandalone::CheckStart(): search for channel start mark");
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

            if (macontext.Info.Channels[stream]) {
                if ((macontext.Info.Channels[stream] == 6) && (macontext.Audio.Options.ignoreDolbyDetection == false)) {
                    isyslog("DolbyDigital5.1 audio whith 6 channels in stream %d detected", stream);
                    markCriteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_AVAILABLE);
                    if (macontext.Audio.Info.channelChange) {
                        dsyslog("cMarkAdStandalone::CheckStart(): channel change detected");
                        video->ClearBorder();
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
                        markCriteria.SetDetectionState(MT_BLACKCHANGE, true);
                        markCriteria.SetDetectionState(MT_LOGOCHANGE,  true);
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
            const cMark *chStart = marks.GetNext(0,       MT_CHANNELSTART);
            const cMark *chStop  = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);
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
            marks.DelWeakFromTo(0, INT_MAX, MT_CHANNELCHANGE); // we have a channel start mark, delete all weak marks
            markCriteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
        }
        else {     // no channel start mark found, cleanup invalid channel stop marks
            const cMark *cStart = marks.GetNext(0, MT_CHANNELSTART);
            const cMark *cStop  = marks.GetNext(0, MT_CHANNELSTOP);
            if (!cStart && cStop) {  // channel stop mark and no channel start mark
                if (cStop->position > (600 * macontext.Video.Info.framesPerSecond)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): late channel stop (%d) without start mark found, assume this is stop of the first part, use channel for detection", cStop->position);
                    markCriteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): early channel stop (%d) without start mark found, assume as start mark of the following recording, convert it to assumed start mark", cStop->position);  // for later use, if we found nothing else
                    char *comment = NULL;
                    int pos = cStop->position;
                    marks.Del(pos);
                    if (asprintf(&comment,"assumed start from channel stop (%d)", pos) == -1) comment = NULL;
                    if (comment) {
                        ALLOC(strlen(comment)+1, "comment");
                    }
                    marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, pos, comment);
                    if (comment) {
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                    }
                }
            }
        }
    }

// check if aspect ratio from VDR info file is valid
    if ((macontext.Info.AspectRatio.num == 0) || (macontext.Video.Info.AspectRatio.den == 0)) {
        if (marks.Count(MT_ASPECTCHANGE, 0xF0) > 0) {
            isyslog("no video aspect ratio found in vdr info file, we have aspect ratio changes, assume 4:3");
            macontext.Info.AspectRatio.num = 4;
            macontext.Info.AspectRatio.den = 3;
        }
        else {
            isyslog("no video aspect ratio found in vdr info file, we have no aspect ratio changes, assume 16:9");
            macontext.Info.AspectRatio.num = 16;
            macontext.Info.AspectRatio.den =  9;
        }
    }
    // end of start part can not be 4:3 if broadcast is 16:9
    if ((macontext.Info.AspectRatio.num       == 16) && (macontext.Info.AspectRatio.den       == 9) &&
        (macontext.Video.Info.AspectRatio.num ==  4) && (macontext.Video.Info.AspectRatio.den == 3)) {
        dsyslog("cMarkAdStandalone::CheckStart(): broadcast at end of start part is 4:3, VDR info tells 16:9, info file is wrong");
        SwapAspectRatio();
   }
   // very short start/stop pairs (broadcast) are impossible, these must be stop/start (ad) pairs
   cMark *aspectStart = marks.GetNext(-1, MT_ASPECTSTART); // first start can be on position 0
   if (aspectStart) {
       cMark *aspectStop = marks.GetNext(aspectStart->position, MT_ASPECTSTOP);
       if (aspectStop) {
           const cMark *aspectNextStart = marks.GetNext(aspectStop->position, MT_ASPECTSTART); // check only if there is no next start mark
               if (!aspectNextStart) {
               int diff    = (aspectStop->position - aspectStart->position) / macontext.Video.Info.framesPerSecond;
               int iStartS = iStart / macontext.Video.Info.framesPerSecond;
               dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio start (%d) stop (%d): length %ds", aspectStart->position, aspectStop->position, diff);
               if (diff <= abs(iStartS - 20)) {  // we expect at least 20s less than pre timer length for first recording
                   dsyslog("cMarkAdStandalone::CheckStart(): length %ds for first broadcast too short, pre recording time is %ds, VDR info file must be wrong", diff, iStartS);
                   SwapAspectRatio();
               }
           }
       }
   }
   // very long stop/start pairs (ad) are impossible, these must be start/stop (broadcast) pairs
   cMark *aspectStop = marks.GetNext(-1, MT_ASPECTSTOP);  // first stop can be on position 0
   if (aspectStop) {
       aspectStart = marks.GetNext(aspectStop->position, MT_ASPECTSTART);
       if (aspectStart) {
           int diff = (aspectStart->position - aspectStop->position) / macontext.Video.Info.framesPerSecond;
           dsyslog("cMarkAdStandalone::CheckStart(): aspect ratio stop (%d) start (%d): length %ds", aspectStop->position, aspectStart->position, diff);
           if (diff > 306) {  // changed from 282 to 306
               dsyslog("cMarkAdStandalone::CheckStart(): length %ds for first advertisement too long, VDR info file must be wrong", diff);
               SwapAspectRatio();
           }
       }
   }
   macontext.Info.checkedAspectRatio = true;  // now we are sure, aspect ratio is correct

// aspect ratio start
   if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for aspect ratio start mark");
        // search for aspect ratio start mark
        cMark *aStart = marks.GetAround(480 * macontext.Video.Info.framesPerSecond, iStartA, MT_ASPECTSTART);
        if (aStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio start mark at (%d), video info is %d:%d", aStart->position, macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
            if ((macontext.Info.AspectRatio.num == 4) && (macontext.Info.AspectRatio.den == 3)) { // we have a aspect ratio start mark, check if valid
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
                    marks.Del(MT_CHANNELSTART);  // delete channel marks from previos recording
                    marks.Del(MT_CHANNELSTOP);
                    video->ClearBorder();
                    markCriteria.SetMarkTypeState(MT_ASPECTCHANGE, CRITERIA_USED);
                }
            }
        }
    }

    // log video infos
    if (macontext.Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264) isyslog("HD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);
    else isyslog("SD video with aspect ratio of %d:%d detected", macontext.Info.AspectRatio.num, macontext.Info.AspectRatio.den);

    // before we can check border marks, we have to check with is valid
    if (macontext.Video.Info.frameDarkOpeningCredits >= 0) { // check for very long dark opening credits
        dsyslog("cMarkAdStandalone::CheckStart(): found very long dark opening credits start at frame (%d), check which type of border mark is valid", macontext.Video.Info.frameDarkOpeningCredits);
        const cMark *hStop = marks.GetNext(iStartA, MT_HBORDERSTOP);
        const cMark *vStop = marks.GetNext(iStartA, MT_VBORDERSTOP);
        if (hStop && !vStop) {
            dsyslog("cMarkAdStandalone::CheckStart(): hborder stop found but no vborder stop, recording has vborder, change hborder start to vborder start at (%d), delete all hborder marks", macontext.Video.Info.frameDarkOpeningCredits);
            marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
            marks.Add(MT_VBORDERSTART, MT_UNDEFINED, MT_UNDEFINED, macontext.Video.Info.frameDarkOpeningCredits, "start of opening credits", true);
        }
        macontext.Video.Info.frameDarkOpeningCredits = 0; // reset state for long dark opening credits of next braodcast
    }

// horizontal border start
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for hborder start mark");
        cMark *hStart = marks.GetAround(iStartA + delta, iStartA + delta, MT_HBORDERSTART);
        if (hStart) { // we found a hborder start mark
            dsyslog("cMarkAdStandalone::CheckStart(): horizontal border start found at (%i)", hStart->position);

            // check if next broadcast is long enough
            cMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
            if (hStop) {
                int lengthBroadcast = (hStop->position - hStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): next horizontal border stop mark (%d), length of broadcast %ds", hStop->position, lengthBroadcast);
                const cMark *hNextStart = marks.GetNext(hStop->position, MT_HBORDERSTART);
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
                    begin = hStart;                         // found valid horizontal border start mark
                    markCriteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
                    // we found a hborder, check logo stop/start after to prevent to get closing credit from previous recording as start
                    cMark *logoStop  = marks.GetNext(begin->position, MT_LOGOSTOP);
                    cMark *logoStart = marks.GetNext(begin->position, MT_LOGOSTART);
                    if (logoStop && logoStart && (logoStart->position > logoStop->position)) {
                        int diffStop  = (logoStop->position  - begin->position) / macontext.Video.Info.framesPerSecond;
                        int diffStart = (logoStart->position - begin->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckStart(): found logo stop (%d) %ds and logo start (%d) %ds after hborder start (%d)", logoStop->position, diffStop, logoStart->position, diffStart, begin->position);
                        if ((diffStop <= 13) && (diffStart <= 17)) {
                            dsyslog("cMarkAdStandalone::CheckStart(): hborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", begin->position, logoStart->position);
                            marks.Del(begin->position);
                            begin = logoStart;
                        }
                    }
                    if (begin->type != MT_LOGOSTART) {
                        dsyslog("cMarkAdStandalone::CheckStart(): delete logo marks if any");
                        marks.DelType(MT_LOGOCHANGE, 0xF0);
                    }
                    dsyslog("cMarkAdStandalone::CheckStart(): delete vborder marks if any");
                    marks.DelType(MT_VBORDERCHANGE, 0xF0);
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): delete too early horizontal border mark (%d)", hStart->position);
                    marks.Del(hStart->position);
                    if (marks.Count(MT_HBORDERCHANGE, 0xF0) == 0) {
                        dsyslog("cMarkAdStandalone::CheckStart(): horizontal border since start, use it for mark detection");
                        markCriteria.SetMarkTypeState(MT_HBORDERCHANGE, CRITERIA_USED);
                        if (!macontext.Video.Logo.isInBorder) {
                            dsyslog("cMarkAdStandalone::CheckStart(): logo marks can not be valid, delete it");
                            marks.DelType(MT_LOGOCHANGE, 0xF0);
                        }
                    }
                }
            }
        }
        else { // we found no hborder start mark
            dsyslog("cMarkAdStandalone::CheckStart(): no horizontal border at start found, disable horizontal border detection");
            markCriteria.SetDetectionState(MT_HBORDERCHANGE, false);
            dsyslog("cMarkAdStandalone::CheckStart(): delete horizontal border marks, if any");
            marks.DelType(MT_HBORDERCHANGE, 0xF0);  // mybe the is a late invalid hborder start marks, exists sometimes with old vborder recordings
        }
    }

// vertical border start
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for vborder start mark");
        cMark *vStart = marks.GetAround(iStartA + delta, iStartA + delta, MT_VBORDERSTART);  // do not find initial vborder start from previous recording
        if (!vStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no vertical border at start found, ignore vertical border detection");
            markCriteria.SetDetectionState(MT_VBORDERCHANGE, false);
            marks.DelType(MT_VBORDERSTART, 0xFF);  // maybe we have a vborder start from a preview or in a doku, delete it
            const cMark *vStop = marks.GetAround(iStartA + delta, iStartA + delta, MT_VBORDERSTOP);
            if (vStop) {
                int pos = vStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
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
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): vertical border start found at (%d)", vStart->position);
            cMark *vStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
            if (vStop) {
                cMark *vNextStart = marks.GetNext(vStop->position, MT_VBORDERSTART);
                int markDiff = static_cast<int> (vStop->position - vStart->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop found at (%d), %ds after vertical border start", vStop->position, markDiff);
                if (vNextStart) {
                    dsyslog("cMarkAdStandalone::CheckStart(): vertical border start (%d) after vertical border stop (%d) found, start mark at (%d) is valid", vNextStart->position, vStop->position, vStart->position);
                }
                else { // we have only start/stop vborder in start part, this is from broadcast before
                    dsyslog("cMarkAdStandalone::CheckStart(): no vertical border start found after start (%d) and stop (%d)", vStart->position, vStop->position);
                    if ((vStart->position < IGNORE_AT_START) && (markDiff <= 140)) {  // vbordet start/stop from previous broadcast
                        dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop at (%d) %ds after vertical border start (%d) in start part found, this is from previous broadcast, delete marks", vStop->position, markDiff, vStart->position);
                        markCriteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_UNAVAILABLE);
                        marks.Del(vStop);
                        marks.Del(vStart);
                        vStart = NULL;
                    }
                }
            }
            if (vStart) {
                if (vStart->position >= IGNORE_AT_START) { // early position is a vborder from previous recording
                    // found valid vertical border start mark
                    begin = vStart;                        // found valid vertical border start mark
                    markCriteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);

                    // we found a vborder, check logo stop/start after to prevent to get closing credit from previous recording as start
                    cMark *logoStop  = marks.GetNext(begin->position, MT_LOGOSTOP);
                    cMark *logoStart = marks.GetNext(begin->position, MT_LOGOSTART);
                    if (logoStop && logoStart && (logoStart->position > logoStop->position)) {
                        int diffStop  = (logoStop->position  - begin->position) / macontext.Video.Info.framesPerSecond;
                        int diffStart = (logoStart->position - begin->position) / macontext.Video.Info.framesPerSecond;
                        dsyslog("cMarkAdStandalone::CheckStart(): found logo stop (%d) %ds and logo start (%d) %ds after vborder start (%d)", logoStop->position, diffStop, logoStart->position, diffStart, begin->position);
                        if ((diffStop <= 25) && (diffStart <= 28)) {  // changed from 1/4 to 10/13 to 25/28
                            dsyslog("cMarkAdStandalone::CheckStart(): vborder start mark position (%d) includes previous closing credits, use logo start (%d) instead", begin->position, logoStart->position);
                            marks.Del(begin->position);
                            begin = logoStart;
                        }
                    }
                    if (begin->type != MT_LOGOSTART) {
                        dsyslog("cMarkAdStandalone::CheckStart(): delete logo marks if any");
                        marks.DelType(MT_LOGOCHANGE, 0xF0); // delete logo marks, vborder is stronger
                    }
                    dsyslog("cMarkAdStandalone::CheckStart(): delete HBORDER marks if any");
                    marks.DelType(MT_HBORDERCHANGE, 0xF0); // delete wrong hborder marks
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): delete too early vertical border start found at (%d)", vStart->position);
                    const cMark *vBorderStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);
                    marks.Del(vStart->position);
                    if (!vBorderStop || (vBorderStop->position > iStart + 420 * macontext.Video.Info.framesPerSecond)) {
                        dsyslog("cMarkAdStandalone::CheckStart(): vertical border since start, use it for mark detection");
                        markCriteria.SetMarkTypeState(MT_VBORDERCHANGE, CRITERIA_USED);
                        if (!macontext.Video.Logo.isInBorder) {
                            dsyslog("cMarkAdStandalone::CheckStart(): logo marks can not be valid, delete it");
                            marks.DelType(MT_LOGOCHANGE, 0xF0);
                        }
                    }
                }
            }
        }
    }

// logo start
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for logo start mark");
        RemoveLogoChangeMarks();

        // remove very short logo start/stop pairs, this, is a false positive logo detection
        cMark *lStart = marks.GetFirst();
        while (lStart) {
            if (lStart->type == MT_LOGOSTART) {
                cMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);
                if (lStop) {
                    int diff = 1000 * (lStop->position - lStart->position) / macontext.Video.Info.framesPerSecond;
                    dsyslog("cMarkAdStandalone::CheckStart(): logo start (%5d) logo stop (%5d), distance %dms", lStart->position, lStop->position, diff);
                    if (diff <= 60) {
                        dsyslog("cMarkAdStandalone::CheckStart(): distance too short, deleting marks");
                        cMark *tmpMark = lStop->Next();
                        marks.Del(lStart->position);
                        marks.Del(lStop->position);
                        lStart = tmpMark;
                    }
                }
            }
            lStart = lStart->Next();
        }

        // searech for logo start mark around assumed start
        lStart = marks.GetAround(iStartA + (420 * macontext.Video.Info.framesPerSecond), iStartA, MT_LOGOSTART);
        if (lStart) {  // we got a logo start mark
            char *indexToHMSF = marks.IndexToHMSF(lStart->position);
            if (indexToHMSF) {
                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                dsyslog("cMarkAdStandalone::CheckStart(): logo start mark found on position (%i) at %s", lStart->position, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }

            // check if logo start mark is too early
            if (lStart->position  < (15 * macontext.Video.Info.framesPerSecond)) {  // need same seconds to have a stable logo status
                cMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                if (lNextStart && (lNextStart->position  >= (15 * macontext.Video.Info.framesPerSecond))) {  // found later logo start mark
                    int diffAssumed = (lNextStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                    char *indexToHMSFStart = marks.IndexToHMSF(lNextStart->position);
                    if (indexToHMSFStart) {
                        ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF");
                        dsyslog("cMarkAdStandalone::CheckStart(): later logo start mark found on position (%i) at %s, %ds after assumed start", lNextStart->position, indexToHMSFStart, diffAssumed);
                        FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
                        free(indexToHMSFStart);
                    }
#define MAX_LOGO_AFTER_ASSUMED 398  // changed from 518 to 398
                                    // do not increase, sometimes there is a early first advertising
                    if (diffAssumed < MAX_LOGO_AFTER_ASSUMED) lStart = lNextStart;   // found better logo start mark
                    else dsyslog("cMarkAdStandalone::CheckStart(): next logo start mark too far after assumed start");
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): logo start mark (%d) too early, delete mark, no valid later logo start mark found", lStart->position);
                    marks.Del(lStart->position);  // prevent from later selection
                    lStart = NULL;
               }
           }

           if (lStart) {
                // check if logo start mark is before hborder stop mark from previous recording
                if (lStart->position  < hBorderStopPosition) {  // start mark is before hborder stop of previous recording (hBorderStopPosition = -1 of no hborder stop)
                    dsyslog("cMarkAdStandalone::CheckStart(): logo start mark (%d) is before hborder stop mark (%d) from previous recording", lStart->position, hBorderStopPosition);
                    cMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                    if (lNextStart && (lNextStart->position  > hBorderStopPosition)) {  // found later logo start mark
                        int diffAssumed = (lNextStart->position - iStartA) / macontext.Video.Info.framesPerSecond;
                        char *indexToHMSFStart = marks.IndexToHMSF(lNextStart->position);
                        if (indexToHMSFStart) {
                            ALLOC(strlen(indexToHMSFStart)+1, "indexToHMSF");
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

                    // trust sequence blackscreen start / logo stop / blackscreen end / logo start as start of broadcast
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
                                    dsyslog("cMarkAdStandalone::CheckStart(): sequence blackscreen start / logo stop / blackscreen end / logo start found, this is the broadcast start");
                                    dsyslog("cMarkAdStandalone::CheckStart(): delete all other logo stop/start marks next 300s (min broadcast length)");
                                    marks.DelFromTo(lStart->position + 1, lStart->position + (300 *  macontext.Video.Info.framesPerSecond), MT_LOGOCHANGE);
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
                                ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                                dsyslog("cMarkAdStandalone::CheckStart(): next logo stop mark found very short after start mark on position (%i) at %s, distance %ds", lStop->position, indexToHMSF, distanceStartStop);
                                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                                free(indexToHMSF);
                            }
                            cMark *lNextStart = marks.GetNext(lStop->position, MT_LOGOSTART); // get next logo start mark
                            if (lNextStart) {  // now we have logo start/stop/start, this can be a preview before broadcast start
                                indexToHMSF = marks.IndexToHMSF(lNextStart->position);
                                if (indexToHMSF) { ALLOC(strlen(indexToHMSF)+1, "indexToHMSF"); }
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
                begin = lStart;   // found valid logo start mark
                dsyslog("cMarkAdStandalone::CheckStart(): found logo start mark (%d)", begin->position);
                marks.DelWeakFromTo(0, INT_MAX, MT_LOGOCHANGE);   // maybe the is a assumed start from converted channel stop
                if ((markCriteria.GetMarkTypeState(MT_HBORDERCHANGE) == CRITERIA_USED) ||
                    (markCriteria.GetMarkTypeState(MT_VBORDERCHANGE) == CRITERIA_USED) ||
                    (markCriteria.GetMarkTypeState(MT_CHANNELCHANGE) == CRITERIA_USED)) {
                    dsyslog("cMarkAdStandalone::CheckStart(): stronger marks are set for detection, use logo mark only for start mark, delete logo marks after (%d)", begin->position);
                    marks.DelFromTo(begin->position + 1, INT_MAX, MT_LOGOCHANGE);
                }
                else markCriteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_USED);
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStart(): no logo start mark found");

        if (begin && (!macontext.Video.Logo.isInBorder)) {
            dsyslog("cMarkAdStandalone::CheckStart(): disable border detection and delete border marks");  // avoid false detection of border
            marks.DelType(MT_HBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
            marks.DelType(MT_VBORDERCHANGE, 0xF0);  // there could be hborder from an advertising in the recording
            markCriteria.SetDetectionState(MT_HBORDERCHANGE, false);
            markCriteria.SetDetectionState(MT_VBORDERCHANGE, false);
        }
    }

    // drop too early marks of all types
    if (begin && (begin->type != MT_RECORDINGSTART) && (begin->position <= IGNORE_AT_START)) {  // first frames are from previous recording
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

// try black screen mark
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for end of black screen as start mark");
        const cMark *noBlackStart = blackMarks.GetAround(120 * macontext.Video.Info.framesPerSecond, iStartA, MT_NOBLACKSTART);
        if (noBlackStart) {
            begin = marks.Add(MT_NOBLACKSTART, MT_UNDEFINED, MT_UNDEFINED, noBlackStart->position, "black screen end", false);
            dsyslog("cMarkAdStandalone::CheckStart(): found end of black screen as start mark (%d)", begin->position);
        }
    }

// no mark found, try anything
    if (!begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): search for any start mark");
        marks.DelTill(IGNORE_AT_START);    // we do not want to have a initial mark from previous recording as a start mark
        begin = marks.GetAround(2 * delta, iStartA, MT_START, 0x0F);  // not too big search range
        if (begin) {
            dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%d) type 0x%X after search for any type", begin->position, begin->type);
            if ((begin->type == MT_ASSUMEDSTART) || (begin->inBroadCast) || !markCriteria.GetDetectionState(MT_LOGOCHANGE)){  // test on inBroadCast because we have to take care of black screen marks in an ad, MT_ASSUMEDSTART is from converted channel stop of previous broadcast
                char *indexToHMSF = marks.IndexToHMSF(begin->position);
                if (indexToHMSF) {
                    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                    dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%i) type 0x%X at %s inBroadCast %i", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
            }
            else { // mark in ad
                char *indexToHMSF = marks.IndexToHMSF(begin->position);
                if (indexToHMSF) {
                    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                    dsyslog("cMarkAdStandalone::CheckStart(): start mark found but not inBroadCast (%i) type 0x%X at %s inBroadCast %i, ignoring", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
                begin = NULL;
            }
        }
    }

    if (begin && ((begin->position  / macontext.Video.Info.framesPerSecond) < 1) && (begin->type != MT_RECORDINGSTART)) { // do not accept marks in the first second, the are from previous recording, expect manual set MT_RECORDINGSTART fpr missed recording start
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

    // still no start mark found, try stop marks from previous broadcast
    if (!begin) { // try hborder stop mark as start mark
        if (hBorderStopPosition >= 0) {
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP from previous recoring as start mark");
            marks.Add(MT_ASSUMEDSTART, MT_UNDEFINED, MT_UNDEFINED, hBorderStopPosition, "start mark from border stop of previous recording*", true);
            begin = marks.Get(hBorderStopPosition);
            marks.DelTill(hBorderStopPosition);
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
    marks.DelTill(begin->position);    // delete all marks till start mark
    char *indexToHMSF = marks.IndexToHMSF(begin->position);
    if (indexToHMSF) { ALLOC(strlen(indexToHMSF)+1, "indexToHMSF"); }
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

    // if we have border marks, delete logo marks
    if ((begin->type == MT_VBORDERSTART) || (begin->type == MT_HBORDERSTART)) {
        marks.Del(MT_LOGOSTART);
        marks.Del(MT_LOGOSTOP);
    }

    // check hborder start mark position
    if (begin->type == MT_HBORDERSTART) { // we found a valid hborder start mark, check black screen because of closing credits from broadcast before
        cMark *blackMark = blackMarks.GetNext(begin->position, MT_NOBLACKSTART);
        if (blackMark) {
            int diff =(blackMark->position - begin->position) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::CheckStart(): black screen (%d) after, distance %ds", blackMark->position, diff);
            if (diff <= 6) {
                dsyslog("cMarkAdStandalone::CheckStart(): move horizontal border (%d) to end of black screen (%d)", begin->position, blackMark->position);
                marks.Move(begin, blackMark->position, MT_UNDEFINED, "black screen");
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
    markCriteria.ListMarkTypeState();
    markCriteria.ListDetection();
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
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
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
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
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
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else esyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current silence marks:");
    mark = silenceMarks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current scene change marks:");
    mark = sceneMarks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position);
        if (indexToHMSF) {
            ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
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

// delete all black screen marks expect start or end mark
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): delete invalid black screen marks");
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
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && (mark->Next()->type == MT_LOGOSTART)) {
            int diff = 1000 * (mark->Next()->position - mark->position) /  macontext.Video.Info.framesPerSecond;
            int lengthAfter  = -1;
            int lengthBefore = -1;
            if (mark->Next()->Next() && (mark->Next()->Next()->type == MT_LOGOSTOP)) {
                lengthAfter = (mark->Next()->Next()->position - mark->Next()->position) /  macontext.Video.Info.framesPerSecond;
            }
            if (mark->Prev() && (mark->Prev()->type == MT_LOGOSTART)) {
                lengthBefore = (mark->position - mark->Prev()->position) /  macontext.Video.Info.framesPerSecond;
            }
            dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair, length %dms, distance before %ds after %ds", mark->position, mark->Next()->position, diff, lengthBefore, lengthAfter);
            if ((diff <= 80) || ((diff < 160 ) && (lengthBefore < 696) && (lengthAfter < 139))) {
                                                                                 // do not increase length because of very short real logo interuption between broacast and preview
                                                                                 // changed from 520 to 200 to 160
                                                                                 // do not delete a short stop/start before or after a long broadcast part, changed from 816 to 696
                                                                                 // this pair contains start mark, lengthAfter changed from 203 to 139
                dsyslog("cMarkAdStandalone::CheckMarks(): logo stop (%5d) and logo start (%5d) pair, too short, deleting", mark->position, mark->Next()->position);
                cMark *tmp = mark->Next()->Next();
                marks.Del(mark->Next());
                marks.Del(mark);
                mark = tmp;
                continue;
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

// check for short start/stop pairs at the start part
    CheckStartMark();

// check for short stop/start logo pair at start part in case of late recording start, ad between broadcasts are short, ad in broadcast are long
    dsyslog("cMarkAdStandalone::CheckMarks(): final check start mark");
    cMark *firstStart = marks.GetNext(-1, MT_LOGOSTART);
    if (firstStart) {
        cMark *firstStop  = marks.GetNext(firstStart->position, MT_LOGOSTOP);
        if (firstStop) {
            cMark *secondStart = marks.GetNext(firstStop->position, MT_LOGOSTART);
            if (secondStart) {
                int lengthFirstAd       = (secondStart->position - firstStop->position) / macontext.Video.Info.framesPerSecond;
                int newDiffAfterAssumed = (secondStart->position - iStart) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::CheckMarks(): first logo stop mark (%d), next logo start mark (%d), length ad %ds, start %ds after assumed start", firstStop->position, secondStart->position, lengthFirstAd, newDiffAfterAssumed);
                if ((lengthFirstAd <= 41) && (newDiffAfterAssumed <= 387)) {  // changed from 359 to 363 to 365 to 387
                    dsyslog("cMarkAdStandalone::CheckMarks(): first ad too short for in broadcast, delete start (%d) and stop (%d) mark", firstStart->position, firstStop->position);
                    marks.Del(firstStart->position);
                    marks.Del(firstStop->position);
                }
            }
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
                if ((lastStopMark->type & 0xF0) < MT_CHANNELCHANGE) {  // trust channel marks and better
                    int minLastStopAssumed;    // trusted distance to assumed assumed stop depents on hardness of marks
                    int minLastStartAssumed;
                    int minPrevStopAssumed;
                    int maxLastBroadcast;
                    int maxLastAd;
                    switch(lastStopMark->type) {
                        case MT_ASSUMEDSTOP:
                            // too long broadcast length from info file, delete last stop:
                            // 296 / -383 / -419
                            // 346 / -224 / -535
                            // 351 / -154 / -506
                            // 369 /   25 / -542
                            // 400 / -190 / -535
                            // 423 /  -50 / -549
                            // 442 / -130 / -564
                            // 471 / -154 / -542
                            // 547 /  -83 / -567
                            minLastStopAssumed  =  296;
                            minLastStartAssumed = -383;
                            minPrevStopAssumed  = -567;
                            maxLastBroadcast    =   -1;
                            maxLastAd           =   -1;
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
                            maxLastBroadcast    =   -1;
                            maxLastAd           =   -1;
                            break;
                        case MT_LOGOSTOP:
                            // too long broadcast length from info file, delete last stop:
                            // -12 /  -97 / -132
                            //  52 / -165 / -289
                            //  63 /  -86 / -195
                            // 106 /  -79 / -182
                            // 251 /  -54 / -496  (conflict)
                            // 306 /  -19 / -483  (conflict)
                            // correct end mark, do not delete last stop
                            // 176 /   30 / -449
                            // 238 /   76 / -376
                            minLastStopAssumed  =  -12;
                            minLastStartAssumed = -165;
                            minPrevStopAssumed  = -375;
                            maxLastBroadcast    =   79;  // shortest last part of a broadcast with logo end mark
                            maxLastAd           =   15;  // changed from 4 to 15
                            break;
                        case MT_VBORDERSTOP:
                            minLastStopAssumed  =  288;
                            minLastStartAssumed =   56;
                            minPrevStopAssumed  = -477;
                            maxLastBroadcast    =   -1;
                            maxLastAd           =   -1;
                            break;
                        default:
                            minLastStopAssumed  = 1000;  // do nothing
                            minLastStartAssumed = 1000;
                            minPrevStopAssumed  = 1000;
                            maxLastBroadcast    =   -1;
                            maxLastAd           =   -1;
                    }
                    dsyslog("cMarkAdStandalone::CheckMarks(): select previous stop if: end mark        >= %4ds after assumed end (%d)", minLastStopAssumed, newStopA);
                    dsyslog("cMarkAdStandalone::CheckMarks():                          last start mark >= %4ds after assumed end (%d)", minLastStartAssumed, newStopA);
                    dsyslog("cMarkAdStandalone::CheckMarks():                          last stop  mark >= %4ds after assumed end (%d)", minPrevStopAssumed, newStopA);
                    dsyslog("cMarkAdStandalone::CheckMarks():         max length of last broadcast     <  %4ds", maxLastBroadcast);
                    dsyslog("cMarkAdStandalone::CheckMarks():         max length of last advertisement <  %4ds", maxLastBroadcast);

                    if (((diffLastStopAssumed >= minLastStopAssumed) && (diffLastStartAssumed >= minLastStartAssumed) && (diffPrevStopAssumed >= minPrevStopAssumed)) ||
                        // very short last broadcast is preview after broadcast
                        ((lastBroadcast <= maxLastBroadcast) && (lastStopMark->position > (newStopA - (19 * macontext.Video.Info.framesPerSecond)))) ||
                        ((lastAd        <= maxLastAd)        && (lastStopMark->position > newStopA))) {  // very short ads are only between broadcast
                        dsyslog("cMarkAdStandalone::CheckMarks(): use stop mark (%d) before as end mark, assume too big recording length", prevStopMark->position);
                        marks.Del(lastStopMark->position);
                        marks.Del(lastStartMark->position);
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
        if (indexToHMSF) { ALLOC(strlen(indexToHMSF)+1, "indexToHMSF"); }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame);
        if (indexToHMSF) { ALLOC(strlen(indexToHMSF)+1, "indexToHMSF"); }
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

    timeText = marks.IndexToHMSF(mark->position);
    if (timeText) {
        ALLOC(strlen(timeText)+1, "indexToHMSF");
        if (((mark->type & 0xF0) >= MT_LOGOCHANGE) || (mark->type == MT_RECORDINGSTART)) {  // keep strong marks, they are better than VPS marks
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep mark at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
        }
        else { // replace weak marks
            int diff = abs(mark->position - vpsFrame) / macontext.Video.Info.framesPerSecond;
            dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s, %ds after mark", mark->position, mark->type, timeText, diff);
            char *markTypeText =  marks.TypeToText(mark->type);
            if (asprintf(&comment,"VPS %s                        (%6d), moved from %s mark (%6d) at %s %s", (type == MT_START) ? "start" : "stop ", vpsFrame, markTypeText, mark->position, timeText, (type == MT_START) ? "*" : "") == -1) comment=NULL;
            if (comment) ALLOC(strlen(comment)+1, "comment");
            else return;
            FREE(strlen(markTypeText)+1, "text");  // alloc in TypeToText
            free(markTypeText);

            if (abs(diff) < 1225) {  // do not replace very far marks, there can be an invalid VPS events
                dsyslog("cMarkAdStandalone::AddMarkVPS(): delete mark on position (%d)", mark->position);
                marks.Del(mark->position);
                marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, MT_UNDEFINED, MT_UNDEFINED, vpsFrame, comment);
                if ((type == MT_START) && !isPause) {   // delete all marks before vps start
                    marks.DelWeakFromTo(0, vpsFrame, 0xFF);
                }
                else if ((type == MT_STOP) && isPause) {  // delete all marks between vps start and vps pause start
                    const cMark *startVPS = marks.GetFirst();
                    if (startVPS && (startVPS->type == MT_VPSSTART)) {
                        marks.DelWeakFromTo(startVPS->position, vpsFrame, MT_VPSCHANGE);
                    }
                }
            }
            else dsyslog("cMarkAdStandalone::AddMarkVPS(): VPS event too far from mark, ignoring");
            FREE(strlen(comment)+1,"comment");
            free(comment);
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


    // set comment of the new mark
    char *comment = NULL;
    switch (mark->type) {
        case MT_ASSUMEDSTART:
            if (asprintf(&comment, "assuming start                   (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_ASSUMEDSTOP:
            if (asprintf(&comment, "assuming stop                    (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_SCENESTART:
            if (asprintf(&comment, "detected start of scene          (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_SOUNDSTART:
            if (asprintf(&comment, "detected end  of silence         (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_SOUNDSTOP:
            if (asprintf(&comment, "detected start of silence        (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_SCENESTOP:
            if (asprintf(&comment, "detected end   of scene          (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_NOBLACKSTART:
            if (asprintf(&comment, "detected end   of black screen   (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_NOBLACKSTOP:
            if (asprintf(&comment, "detected start of black screen   (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_LOGOSTART:
            if (asprintf(&comment, "detected logo start              (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_LOGOSTOP:
            if (asprintf(&comment, "detected logo stop               (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_HBORDERSTART:
            if (asprintf(&comment, "detected start of horiz. borders (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_HBORDERSTOP:
            if (asprintf(&comment, "detected stop  of horiz. borders (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_VBORDERSTART:
            if (asprintf(&comment, "detected start of vert. borders  (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_VBORDERSTOP:
            if (asprintf(&comment, "detected stop  of vert. borders  (%6d) ", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_ASPECTSTART:
            if (!mark->AspectRatioBefore.num) {
                if (asprintf(&comment, "aspect ratio start with %2d:%d (%6d)*", mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
                if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            }
            else {
                if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%6d)*", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
                if (comment) { ALLOC(strlen(comment)+1, "comment"); }
                if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (markCriteria.GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
                    isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
                    markCriteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
                }
            }
            break;
        case MT_ASPECTSTOP:
            if (asprintf(&comment, "aspect ratio change from %2d:%d to %2d:%d (%6d) ", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            if ((macontext.Config->autoLogo > 0) && (mark->position > 0) && (markCriteria.GetMarkTypeState(MT_LOGOCHANGE) == CRITERIA_DISABLED)) {
                isyslog("aspect ratio change from %2d:%d to %2d:%d at frame (%d), logo detection reenabled", mark->AspectRatioBefore.num, mark->AspectRatioBefore.den, mark->AspectRatioAfter.num, mark->AspectRatioAfter.den, mark->position);
                markCriteria.SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_UNKNOWN);
            }
            break;
        case MT_CHANNELSTART:
            if (asprintf(&comment, "audio channel change from %d to %d (%6d)*", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            if (!macontext.Audio.Info.channelChange && (mark->position > iStopA / 2)) {
                dsyslog("AddMark(): first channel start at frame (%d) after half of assumed recording length at frame (%d), this is start mark of next braoscast", mark->position, iStopA / 2);
            }
            else macontext.Audio.Info.channelChange = true;
            break;
        case MT_CHANNELSTOP:
            if ((mark->position > chkSTART) && (mark->position < iStopA * 2 / 3) && !macontext.Audio.Info.channelChange) {
                dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after chkSTART, disable video decoding detection now");
                if (iStart == 0) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we have selected a start mark
                // disable all video detection
                video->ClearBorder();
                markCriteria.SetMarkTypeState(MT_CHANNELCHANGE, CRITERIA_USED);
            }
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment, "audio channel change from %d to %d (%6d) ", mark->channelsBefore, mark->channelsAfter, mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_RECORDINGSTART:
            if (asprintf(&comment, "start of recording (%6d)*", mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        case MT_RECORDINGSTOP:
            if (asprintf(&comment, "stop of recording (%6d) ",mark->position) == -1) comment = NULL;
            if (comment) { ALLOC(strlen(comment)+1, "comment"); }
            break;
        default:
            esyslog("cMarkAdStandalone::AddMark(): unknown mark type 0x%X", mark->type);
            return;
    }

    // add weak marks only to seperate marks object
    switch (mark->type & 0xF0) {
        case MT_SCENECHANGE:
            sceneMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
            if (comment) {
#ifdef DEBUG_WEAK_MARKS
                char *indexToHMSF = marks.IndexToHMSF(mark->position);
                if (indexToHMSF) {
                    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                    dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
#endif
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
            break;
        case MT_SOUNDCHANGE:
            silenceMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
            if (comment) {
#ifdef DEBUG_WEAK_MARKS
                char *indexToHMSF = marks.IndexToHMSF(mark->position);
                if (indexToHMSF) {
                    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                    dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
#endif
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
            break;
        case MT_BLACKCHANGE:
            blackMarks.Add(mark->type, MT_UNDEFINED, MT_UNDEFINED, mark->position, NULL, inBroadCast);
            if (comment) {
#ifdef DEBUG_WEAK_MARKS
                char *indexToHMSF = marks.IndexToHMSF(mark->position);
                if (indexToHMSF) {
                    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
                    dsyslog("cMarkAdStandalone::AddMark(): %s at %s", comment, indexToHMSF);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
#endif
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
    if (!*mark1)       return false;
    if (!mark2)        return false;
    if (!*mark2)       return false;

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
        if (!ptr_cDecoder->GetNextPacket(false, false)) {
            dsyslog("cMarkAdStandalone::ProcessMarkOverlap(): GetNextPacket() failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->IsVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false)) continue; // interlaced videos will not decode each frame
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
        if (!ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false)) continue; // interlaced videos will not decode each frame
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
            if (indexToHMSFbeforeStart) { ALLOC(strlen(indexToHMSFbeforeStart)+1, "indexToHMSFbeforeStart"); }

            char *indexToHMSFbeforeEnd   = marks.IndexToHMSF(overlapPos.similarBeforeEnd);
            if (indexToHMSFbeforeEnd) { ALLOC(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSFbeforeEnd"); }

            char *indexToHMSFafterStart  = marks.IndexToHMSF(overlapPos.similarAfterStart);
            if (indexToHMSFafterStart) { ALLOC(strlen(indexToHMSFafterStart)+1, "indexToHMSFafterStart"); }

            char *indexToHMSFafterEnd    = marks.IndexToHMSF(overlapPos.similarAfterEnd);
            if (indexToHMSFafterEnd) { ALLOC(strlen(indexToHMSFafterEnd)+1, "indexToHMSFafterEnd"); }

            dsyslog("cMarkAdOverlap::ProcessMarkOverlap(): similar from (%5d) at %s to (%5d) at %s, length %5dms", overlapPos.similarBeforeStart, indexToHMSFbeforeStart, overlapPos.similarBeforeEnd, indexToHMSFbeforeEnd, lengthBefore);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              (%5d) at %s to (%5d) at %s, length %5dms",     overlapPos.similarAfterStart,  indexToHMSFafterStart,  overlapPos.similarAfterEnd,  indexToHMSFafterEnd, lengthAfter);
            dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              maximum deviation in overlap %6d", overlapPos.similarMax);
            if (overlapPos.similarEnd > 0) dsyslog("cMarkAdOverlap::ProcessMarkOverlap():              next deviation after overlap %6d", overlapPos.similarEnd); // can be 0 if overlap ends at the mark

            char *indexToHMSFmark1  = marks.IndexToHMSF((*mark1)->position);
            if (indexToHMSFmark1) { ALLOC(strlen(indexToHMSFmark1)+1, "indexToHMSFmark1"); }

            char *indexToHMSFmark2  = marks.IndexToHMSF((*mark2)->position);
            if (indexToHMSFmark2) { ALLOC(strlen(indexToHMSFmark2)+1, "indexToHMSFmark2"); }

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

            if (indexToHMSFmark1) {
                FREE(strlen(indexToHMSFmark1)+1, "indexToHMSFmark1");
                free(indexToHMSFmark1);
            }
            if (indexToHMSFmark2) {
                FREE(strlen(indexToHMSFmark2)+1, "indexToHMSFmark2");
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
                *mark1 = marks.Move(*mark1, overlapPos.similarBeforeEnd, MT_UNDEFINED, "overlap");
                *mark2 = marks.Move(*mark2, overlapPos.similarAfterEnd,  MT_UNDEFINED, "overlap");
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

    // read and decode all video frames, we want to be sure we have a valid decoder state, this is a debug function, we dont care about performance
    while(mark && (ptr_cDecoder->DecodeDir(directory))) {
        while(mark && (ptr_cDecoder->GetNextPacket(false, false))) {
            if (abortNow) return;
            if (ptr_cDecoder->IsVideoPacket()) {
                if (ptr_cDecoder->GetFrameInfo(&macontext, true, macontext.Config->fullDecode, false)) {
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
            esyslog("got invalid start mark at (%i) type 0x%X", startMark->position, startMark->type);
            return;
        }
        int startPosition;
        startPosition = recordingIndexMark->GetIFrameBefore(startMark->position - 1);  // go before mark position to preload decoder pipe
        startPosition = recordingIndexMark->GetIFrameBefore(startPosition - 1);        // go again one iFrame back to be sure we can decode start mark position
        if (startPosition < 0) startPosition = 0;

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
            while(ptr_cDecoder->GetNextPacket(false, false)) {
                int frameNumber = ptr_cDecoder->GetFrameNumber();
                // seek to startPosition
                if  (frameNumber < startPosition) {
                    LogSeparator();
                    dsyslog("cMarkAdStandalone::MarkadCut(): decoding from frame (%d) for start mark (%d) to frame (%d) in pass: %d", startPosition, startMark->position, stopMark->position, pass);
                    ptr_cDecoder->SeekToFrame(&macontext, startPosition);
                    frameNumber = ptr_cDecoder->GetFrameNumber();
                }
                // stop mark reached, set next startPosition
                if  (frameNumber > stopMark->position) {
                    if (stopMark->Next() && stopMark->Next()->Next()) {  // next mark pair
                        startMark = stopMark->Next();
                        if ((startMark->type & 0x0F) != MT_START) {
                            esyslog("got invalid start mark at (%i) type 0x%X", startMark->position, startMark->type);
                            return;
                        }

                        startPosition = recordingIndexMark->GetIFrameBefore(startMark->position - 1);  // go before mark position to preload decoder pipe
                        startPosition = recordingIndexMark->GetIFrameBefore(startPosition - 1);        // go again one iFrame back to be sure we can decode start mark position
                        if (startPosition < 0) startPosition = frameNumber;

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
            dsyslog("cMarkAdStandalone::BlackMarkOptimization(): black screen (%d) %dms after border start mark (%d)", blackMark->position, diffBlack, mark->position);
            if (diffBlack <= 2560) { // changed from 1520 to 2560
                marks.Move(mark, blackMark->position - 1, MT_UNDEFINED, "black screen before border");
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
    if (markCriteria.GetMarkTypeState(MT_LOGOCHANGE) != CRITERIA_USED) {
        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): no logo marks used");
        return;
    }
    bool save = false;

// check for advertising in frame with logo after logo start mark and before logo stop mark and check for introduction logo
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): check for advertising in frame with logo after logo start and before logo stop mark and check for introduction logo");

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    cDetectLogoStopStart *ptr_cDetectLogoStopStart = new cDetectLogoStopStart(&macontext, &markCriteria, ptr_cDecoder, recordingIndexMark, NULL);
    ALLOC(sizeof(*ptr_cDetectLogoStopStart), "ptr_cDetectLogoStopStart");

    cMark *markLogo = marks.GetFirst();
    while (markLogo) {
        if (markLogo->type == MT_LOGOSTART) {

            char *indexToHMSFStartMark = marks.IndexToHMSF(markLogo->position);
            if (indexToHMSFStartMark) { ALLOC(strlen(indexToHMSFStartMark)+1, "indexToHMSFStartMark"); }

            // check for introduction logo before logo mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (30 * macontext.Video.Info.framesPerSecond); // introduction logos are usually 10s, somettimes longer, changed from 12 to 30
            if (searchStartPosition < 0) searchStartPosition = 0;

            char *indexToHMSFSearchStart = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFSearchStart) { ALLOC(strlen(indexToHMSFSearchStart)+1, "indexToHMSFSearchStart"); }

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
                                                                                                          // somtimes advertising in frame has text in "e.g. Werbung"
                                                                                                          // check longer range to prevent to detect text as second logo
                                                                                                          // changed from 35 to 60

                char *indexToHMSFSearchEnd = marks.IndexToHMSF(searchEndPosition);
                if (indexToHMSFSearchEnd) { ALLOC(strlen(indexToHMSFSearchEnd)+1, "indexToHMSFSearchEnd"); }
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
                markLogo = marks.Move(markLogo, adInFrameEndPosition, MT_UNDEFINED, "advertising in frame");
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
                    if (move) markLogo = marks.Move(markLogo, introductionStartPosition, MT_INTRODUCTIONSTART, "introduction logo");
                    save = true;
                }
            }
            if (indexToHMSFStartMark) {
                FREE(strlen(indexToHMSFStartMark)+1, "indexToHMSFStartMark");
                free(indexToHMSFStartMark);
            }
        }
        if (markLogo->type == MT_LOGOSTOP) {
            // check for advertising in frame with logo before logo stop mark position
            LogSeparator(false);
            int searchStartPosition = markLogo->position - (45 * macontext.Video.Info.framesPerSecond); // advertising in frame are usually 30s, changed from 35 to 45
                                                                                                        // somtimes there is a closing credit in frame with logo before
            char *indexToHMSFStopMark = marks.IndexToHMSF(markLogo->position);
            if (indexToHMSFStopMark) { ALLOC(strlen(indexToHMSFStopMark)+1, "indexToHMSF"); }

            char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition);
            if (indexToHMSFStopMark) { ALLOC(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF"); }

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
                if (newStopPosition >= 0) {
                    dsyslog("cMarkAdStandalone::LogoMarkOptimization(): ad in frame between (%d) and (%d) found", newStopPosition, markLogo->position);
                    if (evaluateLogoStopStartPair && (evaluateLogoStopStartPair->IncludesInfoLogo(newStopPosition, markLogo->position))) {
                        dsyslog("cMarkAdStandalone::LogoMarkOptimization(): deleted info logo part in this range, this could not be a advertising in frame");
                        newStopPosition = -1;
                    }
                }
                if (newStopPosition != -1) {
                    if (!macontext.Config->fullDecode) newStopPosition = recordingIndexMark->GetIFrameBefore(newStopPosition - 1);  // we got first frame of ad, go one iFrame back for stop mark
                    else newStopPosition--; // get frame before ad in frame as stop mark
                    if (evaluateLogoStopStartPair) evaluateLogoStopStartPair->AddAdInFrame(newStopPosition, markLogo->position);  // store info that we found here adinframe
                    markLogo = marks.Move(markLogo, newStopPosition, MT_UNDEFINED, "advertising in frame");
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
    while (mark) {
        if ((mark->type == MT_LOGOSTART) ||
           ((mark->type == MT_MOVEDSTART) && (mark->oldType == MT_LOGOSTART) && ((mark->newType == MT_SOUNDSTART) ||
                                                                                 (mark->newType == MT_INTRODUCTIONSTART)))) {
            cMark *startBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTART); // new part starts after the black screen
            cMark *startAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTART); // new part starts after the black screen
            int diffBefore = INT_MAX;
            int diffAfter  = INT_MAX;
            if (startBefore) {
                diffBefore = 1000 * (mark->position - startBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%5d): found end   of black screen (%5d) %6dms before", mark->position, startBefore->position, diffBefore);
            }
            if (startAfter) {
                diffAfter = 1000 * (startAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::BlackScreenOptimization(): start mark (%5d): found end   of black screen (%5d) %6dms after", mark->position, startAfter->position, diffAfter);
            }
            if (startBefore && (startBefore->position != mark->position) && (diffBefore <= 2880)) {  // changed from 2600 to 2880
                char *comment = NULL;
                if (mark->type == MT_MOVEDSTART) {
                    char *markType = marks.TypeToText(mark->newType);
                    if (markType) {
                        if (asprintf(&comment, "black screen end before %s start", markType) == -1) esyslog("cMarkAdStandalone::BlackScreenOptimization(): asprintf failed");
                        FREE(strlen(markType)+1, "text");
                        free(markType);
                    }
                }
                else if (asprintf(&comment, "black screen end before") == -1) esyslog("cMarkAdStandalone::BlackScreenOptimization(): asprintf failed");

                if (comment) {
                    ALLOC(strlen(comment)+1, "comment");
                    int newPos =  startBefore->position;
                    if ((mark->position == marks.First()->position) && (newPos > 0)) newPos--;  // start broadcast with last black picture
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART, comment);
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                    save = true;
                }
            }
            else {
                if (startAfter && (startAfter->position != mark->position) && (diffAfter <= 1000)) {
                    int newPos =  startAfter->position;
                    if ((mark->position == marks.First()->position) && (newPos > 0)) newPos--;  // start broadcast with last black picture
                    mark = marks.Move(mark, newPos, MT_NOBLACKSTART, "black screen end ");
                    save = true;
                }
            }
        }
        if ((mark->type == MT_LOGOSTOP) ||
           ((mark->type == MT_MOVEDSTOP) && (mark->oldType == MT_LOGOSTOP) && (mark->newType == MT_SOUNDSTOP))) {
            const cMark *stopBefore = blackMarks.GetPrev(mark->position + 1, MT_NOBLACKSTOP);
            const cMark *stopAfter  = blackMarks.GetNext(mark->position - 1, MT_NOBLACKSTOP);
            int diffAfter    = INT_MAX;
            int diffBefore   = INT_MAX;
            int lengthBefore = 0;
            if (stopBefore) {
                diffBefore = 1000 * (mark->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                const cMark *startBefore = blackMarks.GetNext(stopBefore->position, MT_NOBLACKSTART);
                if (startBefore) lengthBefore = 1000 * (startBefore->position - stopBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%5d): found start of black screen (%5d) %6dms before -> length %5dms", mark->position, stopBefore->position, diffBefore, lengthBefore);
            }
            if (stopAfter) {
                int lengthAfter  = 0;
                diffAfter = 1000 * (stopAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                const cMark *startAfter = blackMarks.GetNext(stopAfter->position, MT_NOBLACKSTART);
                if (startAfter) lengthAfter = 1000 * (startAfter->position - stopAfter->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::BlackScreenOptimization(): stop  mark (%5d): found start of black screen (%5d) %6dms after  -> length %5dms", mark->position, stopAfter->position, diffAfter, lengthAfter);
            }
            if (stopBefore && (stopBefore->position != mark->position) && (diffBefore <= 600) && (lengthBefore >= 640)) { // short before mark and long black screen, logo stops after end of part
                mark = marks.Move(mark, stopBefore->position, MT_NOBLACKSTOP, "black screen end ");
                save = true;
            }
            else {
                if (stopAfter && (stopAfter->position != mark->position) && (diffAfter <= 4920)) {
                    char *comment = NULL;
                    if (mark->type == MT_MOVEDSTART) {
                        char *markType = marks.TypeToText(mark->newType);
                        if (markType) {
                            if (asprintf(&comment, "black screen end after %s stop", markType) == -1) esyslog("cMarkAdStandalone::BlackScreenOptimization(): asprintf failed");
                            FREE(strlen(markType)+1, "text");
                            free(markType);
                        }
                    }
                    else if (asprintf(&comment, "black screen end before") == -1) esyslog("cMarkAdStandalone::BlackScreenOptimization(): asprintf failed");

                    if (comment) {
                        ALLOC(strlen(comment)+1, "comment");
                        mark = marks.Move(mark, stopAfter->position, MT_NOBLACKSTOP, comment);
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                        save = true;
                    }
                }
            }
        }
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
        if ((mark->type == MT_LOGOSTART) ||
            (mark->type == MT_VPSSTART)) {
            int diffBefore = INT_MAX;
            int diffAfter  = INT_MAX;
            cMark *soundStartBefore = silenceMarks.GetPrev(mark->position + 1, MT_SOUNDSTART);
            cMark *soundStartAfter  = silenceMarks.GetNext(mark->position - 1, MT_SOUNDSTART);
            if (soundStartBefore) {
                diffBefore = 1000 * (mark->position - soundStartBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): found sound start (%5d) %7dms before", mark->position, soundStartBefore->position, diffBefore);
            }
            if (soundStartAfter) {
                diffAfter = 1000 * (soundStartAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SilenceOptimization(): start mark (%6d): found sound start (%5d) %7dms after", mark->position, soundStartAfter->position, diffAfter);
            }
            if (soundStartBefore && (soundStartBefore->position != mark->position) && (diffBefore <= 3400)) {
                int newPos = soundStartBefore->position;
                if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameBefore(newPos);
                mark = marks.Move(mark, newPos, MT_SOUNDSTART, "sound start");
                save = true;
            }
            else {
                int maxAfter = 6200;
                if (mark->type == MT_VPSSTART) maxAfter = 7160;
                if (soundStartAfter && (soundStartAfter->position != mark->position) && (diffAfter <= maxAfter)) {
                    int newPos = soundStartAfter->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameBefore(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTART, "sound start");
                    save = true;
                }
            }
        }
        if ((mark->type == MT_LOGOSTOP) ||
            (mark->type == MT_VPSSTOP)) {
            int maxAfter = 3879;  // do not increase, we will get position after seperation picture (e.g. "Werbung")
            if (strcmp(macontext.Info.ChannelName, "Disney_Channel") == 0) maxAfter = 4560; // long before fading out logo
            if (mark->type == MT_VPSSTOP) maxAfter = 5040; // VPS is not exact

            cMark *soundStop = silenceMarks.GetNext(mark->position, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
            if (soundStop) {
                int diff = 1000 * (soundStop->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): found sound stop  (%5d) %7dms after", mark->position, soundStop->position, diff);
                if ((soundStop->position != mark->position) && (diff <= maxAfter)) {
                    int newPos = soundStop->position;
                    if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameAfter(newPos);
                    mark = marks.Move(mark, newPos, MT_SOUNDSTOP, "sound stop after");
                    save = true;
                }
            }
            if (mark->type != MT_MOVEDSTOP) { // nothing valid after stop mark found, try short before stop mark
                soundStop = silenceMarks.GetPrev(mark->position, MT_SOUNDSTOP);  // try after stop mark for fading out logo in broadcast
                if (soundStop) {
                   int diff = 1000 * (mark->position - soundStop->position) / macontext.Video.Info.framesPerSecond;
                   dsyslog("cMarkAdStandalone::SilenceOptimization(): stop  mark (%6d): found sound stop  (%5d) %7dms before", mark->position, soundStop->position, diff);
                    if ((soundStop->position != mark->position) && (diff <= 1000)) {
                        int newPos = soundStop->position;
                        if (!macontext.Config->fullDecode) newPos = recordingIndexMark->GetIFrameAfter(newPos);
                        mark = marks.Move(mark, newPos, MT_SOUNDSTOP, "sound stop before");
                        save = true;
                    }
                }
            }
        }
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
        if ((mark->type    == MT_LOGOSTART) ||
            (mark->type    == MT_VPSSTART)  ||
            (mark->oldType == MT_VPSSTART)  ||
           ((mark->type    == MT_MOVEDSTART) && (mark->oldType == MT_LOGOSTART) && ((mark->newType == MT_SOUNDSTART) ||
                                                                                    (mark->newType == MT_INTRODUCTIONSTART)))) {
            cMark *sceneStartBefore = sceneMarks.GetPrev(mark->position + 1, MT_SCENESTART);  // allow to get same position
            cMark *sceneStartAfter  = sceneMarks.GetNext(mark->position - 1, MT_SCENESTART);  // allow to get same position
            if (sceneStartBefore) {
                int diffBefore = 1000 * (mark->position - sceneStartBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%5d): found scene start (%5d) %5dms before", mark->position, sceneStartBefore->position, diffBefore);
                if ((sceneStartBefore->position != mark->position) && (diffBefore <= 4120)) {  // changed from 2880 to 4120
                    char *comment = NULL;
                    if (mark->type == MT_MOVEDSTART) {
                        char *markType = marks.TypeToText(mark->newType);
                        if (markType) {
                            if (asprintf(&comment, "scene start before %s start", markType) == -1) esyslog("cMarkAdStandalone::SceneChangeOptimization(): asprintf failed");
                            FREE(strlen(markType)+1, "text");
                            free(markType);
                        }
                    }
                    else if (asprintf(&comment, "scene start before") == -1) esyslog("cMarkAdStandalone::SceneChangeOptimization(): asprintf failed");

                    if (comment) {
                        ALLOC(strlen(comment)+1, "comment");
                        mark = marks.Move(mark, sceneStartBefore->position, MT_SCENESTART, comment);
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                        save = true;
                    }
                }
            }
            if (sceneStartAfter) {
                int diffAfter = 1000 * (sceneStartAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%5d): found scene start (%5d) %5dms After", mark->position, sceneStartAfter->position, diffAfter);
                if ((sceneStartAfter->position != mark->position) && (diffAfter <= 1200)) {
                    char *comment = NULL;
                    if (mark->type == MT_MOVEDSTART) {
                        char *markType = marks.TypeToText(mark->newType);
                        if (markType) {
                            if (asprintf(&comment, "scene start after %s start", markType) == -1) esyslog("cMarkAdStandalone::SceneChangeOptimization(): asprintf failed");
                            FREE(strlen(markType)+1, "text");
                            free(markType);
                        }
                    }
                    else if (asprintf(&comment, "scene start after") == -1) esyslog("cMarkAdStandalone::SceneChangeOptimization(): asprintf failed");

                    if (comment) {
                        ALLOC(strlen(comment)+1, "comment");
                        mark = marks.Move(mark, sceneStartAfter->position, MT_SCENESTART, comment);
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                        save = true;
                    }
                }
            }
        }
        if ((mark->type == MT_LOGOSTOP) ||
            (mark->type == MT_VPSSTOP)  ||
           ((mark->type == MT_MOVEDSTOP) && (mark->oldType == MT_VPSSTOP) && (mark->newType == MT_SOUNDSTOP))) {
            cMark *sceneStopBefore = sceneMarks.GetPrev(mark->position + 1, MT_SCENESTOP);
            cMark *sceneStopAfter  = sceneMarks.GetNext(mark->position - 1, MT_SCENESTOP);  // allow to get same position
            int diffStopAfter = INT_MAX;
            if (sceneStopAfter) {
                diffStopAfter = 1000 * (sceneStopAfter->position - mark->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%5d): found scene stop  (%5d) %5dms after", mark->position, sceneStopAfter->position, diffStopAfter);
            }
            int diffStopBefore = INT_MAX;
            if (sceneStopBefore) {
                diffStopBefore = 1000 * (mark->position - sceneStopBefore->position) / macontext.Video.Info.framesPerSecond;
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%5d): found scene stop  (%5d) %5dms before", mark->position, sceneStopBefore->position, diffStopBefore);
            }
            if ((sceneStopAfter) && (sceneStopAfter->position != mark->position) && (diffStopAfter <= 3000)) {  // logo is fading out before end of broadcast scene, move forward
                mark = marks.Move(mark, sceneStopAfter->position, MT_SCENESTOP, "scene end after logo stop");
                save = true;
            }
            else {
                if ((sceneStopBefore) && (sceneStopBefore->position != mark->position) && (diffStopBefore <= 1000)) { // logo stop detected too late, move backwards
                    mark = marks.Move(mark, sceneStopBefore->position, MT_SCENESTOP, "scene end before logo stop");
                    save = true;
                }
            }
        }
        // optimize silence moved logo marks
        if ((mark->type == MT_MOVEDSTART) && (mark->oldType == MT_LOGOSTART) && (mark->newType == MT_SOUNDSTART)) {
            cMark *sceneStart = sceneMarks.GetAround(1 *  macontext.Video.Info.framesPerSecond, mark->position, MT_SCENESTART);
            if (sceneStart && (sceneStart->position != mark->position)) {
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): start mark (%5d): found scene start (%5d) near by", mark->position, sceneStart->position);
                mark = marks.Move(mark, sceneStart->position, MT_SCENESTART, "scene start near silence");
                save = true;
            }
        }
        if ((mark->type == MT_MOVEDSTOP) && (mark->oldType == MT_LOGOSTOP) && (mark->newType == MT_SOUNDSTOP)) {
            cMark *sceneStop = sceneMarks.GetAround(1 *  macontext.Video.Info.framesPerSecond, mark->position, MT_SCENESTOP);
            if (sceneStop && (sceneStop->position != mark->position)) {
                dsyslog("cMarkAdStandalone::SceneChangeOptimization(): stop  mark (%5d): found scene stop (%5d) near by", mark->position, sceneStop->position);
                mark = marks.Move(mark, sceneStop->position, MT_SCENESTOP, "scene stop near silence");
                save = true;
            }
        }
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
        // check logo end mark
        if (lastStop->type == MT_LOGOSTOP) {
            if ((evaluateLogoStopStartPair->GetIsAdInFrame(lastStop->position) != STATUS_YES)) {
                if ((markCriteria.GetClosingCreditsState() >= CRITERIA_UNKNOWN) && ((lastStop->type == MT_LOGOSTOP) || (lastStop->type == MT_MOVEDSTOP))) {
                    dsyslog("cMarkAdStandalone::ProcessOverlap(): search for closing credits after logo end mark");
                    if (MoveLastStopAfterClosingCredits(lastStop)) {
                        save = true;
                        dsyslog("cMarkAdStandalone::ProcessOverlap(): moved logo end mark after closing credit");
                    }
                }
            }
            else dsyslog("cMarkAdStandalone::ProcessOverlap(): last stop mark (%d) is moved because of advertisement in frame, no closing credits can follow", lastStop->position);
        }
        // check border end mark
        if ((lastStop->type == MT_HBORDERSTOP) || (lastStop->type == MT_MOVEDSTOP)) {
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
    if (ptr_cDecoder->GetFrameInfo(&macontext, markCriteria.GetDetectionState(MT_VIDEO), macontext.Config->fullDecode, markCriteria.GetDetectionState(MT_SOUNDCHANGE))) {
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

            if (markCriteria.GetDetectionState(MT_VIDEO)) {
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
            markCriteria.SetDetectionState(MT_ALL, true);
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
        ptr_cExtractLogo = new cExtractLogo(&macontext, macontext.Info.AspectRatio, recordingIndexMark);
        ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
        int startPos =  macontext.Info.tStart * 25;  // search logo from assumed start, we do not know the frame rate at this point, so we use 25
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = ptr_cExtractLogo->SearchLogo(&macontext, &markCriteria, startPos);
        for (int retry = 2; retry <= 7; retry++) {  // do not reduce, we will not get some logos
            startPos += 5 * 60 * macontext.Video.Info.framesPerSecond; // next try 5 min later, now we know the frame rate
            if (endpos > 0) {
                dsyslog("cMarkAdStandalone::CheckLogo(): no logo found in recording, retry in %ind part of the recording at frame (%d)", retry, startPos);
                endpos = ptr_cExtractLogo->SearchLogo(&macontext, &markCriteria, startPos);
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
                        if (abs(diff) >= 506) dsyslog("cMarkAdStandalone::LoadInfo(): VPS stop event seems to be invalid, use length from vdr info file");
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

    // load VPS events
    vps = new cVPS(directory);
    ALLOC(sizeof(*vps), "vps");

    if (!LoadInfo()) {
        esyslog("failed loading info - logo %s%sdisabled", (config->logoExtraction != -1) ? "extraction" : "detection", bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
        macontext.Info.tStart = iStart = iStop = iStopA = 0;
        markCriteria.SetDetectionState(MT_LOGOCHANGE, false);
    }
    else {
        if (!CheckLogo() && (config->logoExtraction==-1) && (config->autoLogo == 0)) {
            isyslog("no logo found, logo detection disabled");
            markCriteria.SetDetectionState(MT_LOGOCHANGE, false);
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
        video = new cMarkAdVideo(&macontext, &markCriteria, recordingIndex);
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
            gettimeofday(&startTime2, NULL);
            cmasta->ProcessFiles();
            gettimeofday(&endTime2, NULL);
        }
        if (!bPass1Only) {
            gettimeofday(&startTime3, NULL);
            cmasta->LogoMarkOptimization();      // logo mark optimization
            cmasta->SilenceOptimization();       // mark optimization with mute scene (seems to be more reliable than black screen)
            cmasta->BlackScreenOptimization();   // mark optimization with black scene
            cmasta->BorderMarkOptimization();    // vborder and hborder mark optimization (to correct too eary black screen start marks from closing credit of previous recording)
            cmasta->SceneChangeOptimization();   // final optimization with scene changes (if we habe nothing else, try this as last resort)
            gettimeofday(&endTime3, NULL);

            gettimeofday(&startTime4, NULL);
            cmasta->ProcessOverlap();  // overlap detection
            gettimeofday(&endTime4, NULL);
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
