/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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
#include <pthread.h>
#include <poll.h>
#include <locale.h>
#include <libintl.h>
#include <execinfo.h>
#include <mntent.h>
#include <utime.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include <dirent.h>

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
struct timeval startPass1, startPass2, startPass3, startPass4, endPass1, endPass2, endPass3, endPass4 = {};
int logoSearchTime_ms = 0;
int logoChangeTime_ms = 0;
int decodeTime_us = 0;


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


cOSDMessage::cOSDMessage(const char *Host, int Port) {
    msg=NULL;
    host=strdup(Host);
    ALLOC(strlen(host)+1, "host");
    port = Port;
    send(this);
}


cOSDMessage::~cOSDMessage() {
    if (tid) pthread_join(tid, NULL);
    if (msg) {
        FREE(strlen(msg)+1, "msg");
        free(msg);
    }
    if (host) {
        FREE(strlen(host)+1, "host");
        free((void*) host);
    }
}


bool cOSDMessage::readreply(int fd, char **reply) {
    usleep(400000);
    char c = ' ';
    int repsize = 0;
    int msgsize = 0;
    bool skip = false;
    if (reply) *reply = NULL;
    do {
        struct pollfd fds;
        fds.fd = fd;
        fds.events = POLLIN;
        fds.revents = 0;
        int ret = poll(&fds, 1, 600);

        if (ret <= 0) return false;
        if (fds.revents != POLLIN) return false;
        if (read(fd, &c, 1) < 0) return false;
        if ((reply) && (!skip) && (c != 10) && (c != 13)) {
            msgsize++;
            while ((msgsize + 5) > repsize) {
                repsize += 80;
                char *tmp = (char *) realloc(*reply, repsize);
                if (!tmp) {
                    free(*reply);
                    *reply = NULL;
                    skip = true;
                }
                else {
                    *reply = tmp;
                }
            }
            (*reply)[msgsize - 1] = c;
            (*reply)[msgsize] = 0;
        }
    }
    while (c != '\n');
    return true;
}


void *cOSDMessage::send(void *posd) {
    cOSDMessage *osd = static_cast<cOSDMessage *>(posd);

    struct hostent *host = gethostbyname(osd->host);
    if (!host) {
        osd->tid = 0;
        return NULL;
    }

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(osd->port);
    memcpy(&name.sin_addr.s_addr, host->h_addr, host->h_length);
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    if (connect(sock, (struct sockaddr *)&name, size) != 0 ) {
        close(sock);
        return NULL;
    }

    char *reply = NULL;
    if (!osd->readreply(sock, &reply)) {
        if (reply) free(reply);
        close(sock);
        return NULL;
    }

    ssize_t ret;
    if (osd->msg) {
        if (reply) free(reply);
        ret=write(sock, "MESG ", 5);
        if (ret != (ssize_t) - 1) ret = write(sock,osd->msg,strlen(osd->msg));
        if (ret != (ssize_t) - 1) ret = write(sock, "\r\n", 2);

        if (!osd->readreply(sock) || (ret == (ssize_t) - 1)) {
            close(sock);
            return NULL;
        }
    }
    else {
        if (reply) {
            char *cs = strrchr(reply, ';');
            if (cs) {
                cs += 2;
                trcs(cs);
            }
            else {
                trcs("UTF-8"); // just a guess
            }
            free(reply);
        }
        else {
            trcs("UTF-8"); // just a guess
        }
    }
    ret=write(sock, "QUIT\r\n", 6);
    if (ret != (ssize_t) - 1) osd->readreply(sock);
    close(sock);
    return NULL;
}


int cOSDMessage::Send(const char *format, ...) {
    if (tid) pthread_join(tid, NULL);
    if (msg) free(msg);
    va_list ap;
    va_start(ap, format);
    if (vasprintf(&msg, format, ap) == -1) {
        va_end(ap);
        return -1;
    }
    ALLOC(strlen(msg)+1, "msg");
    va_end(ap);

    if (pthread_create(&tid, NULL, (void *(*) (void *))&send, (void *) this) != 0 ) return -1;
    return 0;
}


void cMarkAdStandalone::CalculateCheckPositions(int startframe) {
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): startframe %i (%dmin %2ds)", startframe, static_cast<int>(startframe / macontext.Video.Info.FramesPerSecond / 60), static_cast<int>(startframe / macontext.Video.Info.FramesPerSecond) % 60);

    if (!length) {
        esyslog("length of recording not found");
        length = 100 * 60 * 60; // try anyway, set to 100h
        startframe = macontext.Video.Info.FramesPerSecond * 2 * 60;  // assume default pretimer of 2min
    }
    if (!macontext.Video.Info.FramesPerSecond) {
        esyslog("video frame rate of recording not found");
        return;
    }

    if (startframe < 0) {   // recodring start is too late
        isyslog("recording started too late, set start mark to start of recording");
        MarkAdMark mark = {};
        mark.Position = 1;  // do not use position 0 because this will later be deleted
        mark.Type = MT_RECORDINGSTART;
        AddMark(&mark);
        startframe = macontext.Video.Info.FramesPerSecond * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): use frame rate %i", static_cast<int>(macontext.Video.Info.FramesPerSecond));

    iStart = -startframe;
    iStop = -(startframe + macontext.Video.Info.FramesPerSecond * length) ;   // iStop change from - to + when frames reached iStop

    iStartA = abs(iStart);
    // iStopA = startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->astopoffs - 30);
    iStopA = startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->astopoffs);
    chkSTART = iStartA + macontext.Video.Info.FramesPerSecond * 4 * MAXRANGE; //  fit for later broadcast start
    chkSTOP = startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording:   %4ds (%3dmin %2ds)", length, length / 60, length % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame:  %5d  (%3dmin %2ds)", iStartA, static_cast<int>(iStartA / macontext.Video.Info.FramesPerSecond / 60), static_cast<int>(iStartA / macontext.Video.Info.FramesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame:  %6d  (%3dmin %2ds)", iStopA, static_cast<int>(iStopA / macontext.Video.Info.FramesPerSecond / 60), static_cast<int>(iStopA / macontext.Video.Info.FramesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTART set to:     %6d  (%3dmin %2ds)", chkSTART, static_cast<int>(chkSTART / macontext.Video.Info.FramesPerSecond / 60), static_cast<int>(chkSTART / macontext.Video.Info.FramesPerSecond) % 60);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTOP set to:      %6d  (%3dmin %2ds)", chkSTOP, static_cast<int>(chkSTOP / macontext.Video.Info.FramesPerSecond / 60), static_cast<int>(chkSTOP / macontext.Video.Info.FramesPerSecond) % 60);
}


void cMarkAdStandalone::CheckStop() {
    LogSeparator(true);
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckStop(): start check stop (%i)", lastiframe);

    char *indexToHMSF = marks.IndexToHMSF(iStopA, &macontext);
        if (indexToHMSF) {
            dsyslog("assumed stop position (%i) at %s", iStopA, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
    DebugMarks();     //  only for debugging

    // remove logo change marks
    int lastClosingCreditsStart = RemoveLogoChangeMarks();
    if (ptr_cDecoderLogoChange) {  // no longer need this object
        FREE(sizeof(*ptr_cDecoderLogoChange), "ptr_cDecoderLogoChange");
        delete ptr_cDecoderLogoChange;
        ptr_cDecoderLogoChange = NULL;
    }

// try MT_CHANNELSTOP
    int delta = macontext.Video.Info.FramesPerSecond * MAXRANGE;
    clMark *end = marks.GetAround(3*delta, iStopA, MT_CHANNELSTOP);      // try if we can get a good stop mark, start with MT_ASPECTSTOP
    if (end) {
        dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found at frame %i", end->position);
        clMark *cStart = marks.GetPrev(end->position, MT_CHANNELSTART);      // if there is short befor a channel start, this stop mark belongs to next recording
        if (cStart) {
            if ((end->position - cStart->position) < delta) {
                dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTART found short before at frame %i with delta %ds, MT_CHANNELSTOP is not valid, try to find stop mark short before", cStart->position, static_cast<int> ((end->position - cStart->position) / macontext.Video.Info.FramesPerSecond));
                end = marks.GetAround(delta, iStopA - delta, MT_CHANNELSTOP);
                if (end) dsyslog("cMarkAdStandalone::CheckStop(): MT_CHANNELSTOP found short before at frame (%d)", end->position);
            }
            else {
                clMark *cStartFirst = marks.GetNext(0, MT_CHANNELSTART);  // get first channel start mark
                if (cStartFirst) {
                    int deltaC = (end->position - cStartFirst->position) / macontext.Video.Info.FramesPerSecond;
                    if (deltaC < 300) {
                    dsyslog("cMarkAdStandalone::CheckStop(): first channel start mark and possible channel end mark to near, this belongs to the next recording");
                    end = NULL;
                    }
                }
            }
        }
    }
    else dsyslog("cMarkAdStandalone::CheckStop(): no MT_CHANNELSTOP mark found");

// try MT_ASPECTSTOP
    if (!end) {
        end = marks.GetAround(3*delta, iStopA, MT_ASPECTSTOP);      // try MT_ASPECTSTOP
        if (end) dsyslog("cMarkAdStandalone::CheckStop(): MT_ASPECTSTOP found at frame %i", end->position);
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_ASPECTSTOP mark found");
    }

// try MT_HBORDERSTOP
    if (!end) {
        end = marks.GetAround(5 * delta, iStopA, MT_HBORDERSTOP);         // increased from 3 to 5
        if (end) dsyslog("cMarkAdStandalone::CheckStop(): MT_HBORDERSTOP found at frame %i", end->position);
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_HBORDERSTOP mark found");
    }

// try MT_VBORDERSTOP
    if (!end) {
        end = marks.GetAround(3*delta, iStopA, MT_VBORDERSTOP);
        if (end) {
            dsyslog("cMarkAdStandalone::CheckStop(): MT_VBORDERSTOP found at frame %i", end->position);
            clMark *prevVStart = marks.GetPrev(end->position, MT_VBORDERSTART);
            if (prevVStart) {
                int deltaV = (end->position - prevVStart->position) / macontext.Video.Info.FramesPerSecond;
                if (deltaV < 240) {  // start/stop is too short, this is the next recording
                    dsyslog("cMarkAdStandalone::CheckStop(): vertial border start/stop to short %ds, ignoring MT_VBORDERSTOP", deltaV);
                    end = NULL;
                }
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_VBORDERSTOP mark found");
    }

// try MT_LOGOSTOP
#define MAX_LOGO_END_MARK_FACTOR 3
    if (!end && (lastClosingCreditsStart > iStopA - delta) && (lastClosingCreditsStart < iStopA + delta)) {  // try stop mark from closing credit
        end = marks.Get(lastClosingCreditsStart);
        if (end) {
            dsyslog("cMarkAdStandalone::CheckStop(): MT_LOGOSTOP at frame (%d) found before detected closing credit (%d)", end->position, lastClosingCreditsStart);
        }
    }
    if (!end) {  // try any logo stop
        // delete possible logo end marks with very near logo start mark before
        bool isInvalid = true;
        while (isInvalid) {
            end = marks.GetAround(MAX_LOGO_END_MARK_FACTOR * delta, iStopA, MT_LOGOSTOP);
            if (end) {
                dsyslog("cMarkAdStandalone::CheckStop(): MT_LOGOSTOP found at frame %i", end->position);
                clMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
                if (prevLogoStart) {
                    int deltaLogoStart = (end->position - prevLogoStart->position) / macontext.Video.Info.FramesPerSecond;
                    if (deltaLogoStart < 9) { // changed from 12 to 10 to 9 because of SIXX and SAT.1 has short logo change at the end of recording
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) is invalid, logo start mark (%d) only %ds before", end->position, prevLogoStart->position, deltaLogoStart);
                        marks.Del(end);
                        marks.Del(prevLogoStart);
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStop(): logo stop mark (%d) is valid, logo start mark (%d) is %ds before", end->position, prevLogoStart->position, deltaLogoStart);
                        isInvalid = false;
                    }
                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStop(): no logo start mark before found");
                    isInvalid = false;
                }
            }
            else {
                dsyslog("cMarkAdStandalone::CheckStop(): no logo stop mark found");
                isInvalid = false;
            }
        }
        if (end) {
            // detect very short logo stop/start before assumed stop mark, they are text previews over the logo (e.g. SAT.1)
            clMark *prevLogoStart = marks.GetPrev(end->position, MT_LOGOSTART);
            clMark *prevLogoStop  = marks.GetPrev(end->position, MT_LOGOSTOP);
            if (prevLogoStart && prevLogoStop) {
                int deltaLogoStart = (end->position - prevLogoStart->position) / macontext.Video.Info.FramesPerSecond;
                int deltaLogoPrevStartStop = (prevLogoStart->position - prevLogoStop->position) / macontext.Video.Info.FramesPerSecond;
#define CHECK_START_DISTANCE_MAX 13  // changed from 12 to 13
#define CHECK_START_STOP_LENGTH_MAX 4  // changed from 2 to 4
                if ((deltaLogoStart <= CHECK_START_DISTANCE_MAX) && (deltaLogoPrevStartStop <= CHECK_START_STOP_LENGTH_MAX)) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) stop (%d) pair before assumed end mark too near %ds (expect >%ds) and too short %ds (expect >%ds), this is a text preview over the logo, delete marks", prevLogoStart->position, prevLogoStop->position, deltaLogoStart, CHECK_START_DISTANCE_MAX, deltaLogoPrevStartStop, CHECK_START_STOP_LENGTH_MAX);
                    marks.Del(prevLogoStart);
                    marks.Del(prevLogoStop);
                }
                else dsyslog("cMarkAdStandalone::CheckStop(): logo start (%d) stop (%d) pair before assumed end mark is far %ds (expect >%ds) or long %ds (expect >%ds), end mark is valid", prevLogoStart->position, prevLogoStop->position, deltaLogoStart, CHECK_START_DISTANCE_MAX, deltaLogoPrevStartStop, CHECK_START_STOP_LENGTH_MAX);
            }
            prevLogoStop = marks.GetPrev(end->position, MT_LOGOSTOP); // maybe different if deleted above
            if (prevLogoStop) {
                int deltaLogo = (end->position - prevLogoStop->position) / macontext.Video.Info.FramesPerSecond;
#define CHECK_STOP_BEFORE_MIN 14 // if stop before is too near, maybe recording length is too big
                if (deltaLogo < CHECK_STOP_BEFORE_MIN) {
                    dsyslog("cMarkAdStandalone::CheckStop(): logo stop before too near %ds (expect >=%ds), use (%d) as stop mark", deltaLogo, CHECK_STOP_BEFORE_MIN, prevLogoStop->position);
                    end = prevLogoStop;
                }
                else dsyslog("cMarkAdStandalone::CheckStop(): logo stop before at (%d) too far away %ds (expect >=%ds), no alternative", prevLogoStop->position, deltaLogo, CHECK_STOP_BEFORE_MIN);
            }
        }
        else dsyslog("cMarkAdStandalone::CheckStop(): no MT_LOGOSTOP mark found");
    }

    if (!end) {
        end = marks.GetAround(delta, iStopA + delta, MT_STOP, 0x0F);    // try any type of stop mark, only accept after iStopA
        if (end) dsyslog("cMarkAdStandalone::CheckStop(): weak end mark found at frame %i", end->position);
        else dsyslog("cMarkAdStandalone::CheckStop(): no end mark found");
    }

    clMark *lastStart = marks.GetAround(INT_MAX, lastiframe, MT_START, 0x0F);
    if (end) {
        dsyslog("cMarkAdStandalone::CheckStop(): found end mark at (%i)", end->position);
        clMark *mark = marks.GetFirst();
        while (mark) {
            if ((mark->position >= iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) && (mark->position < end->position) && ((mark->type & 0xF0) < (end->type & 0xF0))) { // delete all weak marks
                dsyslog("cMarkAdStandalone::CheckStop(): found stronger end mark delete mark (%i)", mark->position);
                if ((mark->type & 0xF0) == MT_BLACKCHANGE) blackMarks.Add(mark->type, mark->position, NULL, mark->inBroadCast); // add mark to blackscreen list
                clMark *tmp = mark;
                mark = mark->Next();
                marks.Del(tmp);
                continue;
            }
            mark = mark->Next();
        }

        if ((end->type == MT_NOBLACKSTOP) && (end->position < iStopA)) {        // if stop mark is MT_NOBLACKSTOP and it is not after iStopA try next, better save than sorry
           clMark *end2 = marks.GetAround(delta, end->position + 2*delta, MT_STOP, 0x0F);
           if (end2) {
               dsyslog("cMarkAdStandalone::CheckStop(): stop mark is week, use next stop mark at (%i)", end2->position);
               end = end2;
           }
        }

        indexToHMSF = marks.IndexToHMSF(end->position, &macontext);
        if (indexToHMSF) {
            isyslog("using mark on position (%i) type 0x%X at %s as stop mark", end->position,  end->type, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }

        dsyslog("cMarkAdStandalone::CheckStop(): delete all marks after final stop mark at (%d)", end->position);
        marks.DelTill(end->position, &blackMarks, false);

        if ( end->position < iStopA - 5 * delta ) {    // last found stop mark too early, adding STOP mark at the end, increased from 3 to 5
                                                     // this can happen by audio channel change too if the next broadcast has also 6 channels
            if ( ( lastStart) && ( lastStart->position > end->position ) ) {
                isyslog("last STOP mark results in to short recording, set STOP at the end of the recording (%i)", lastiframe);
                MarkAdMark markNew = {};
                markNew.Position = lastiframe;
                markNew.Type = MT_ASSUMEDSTOP;
                AddMark(&markNew);
            }
        }
    }
    else {  // no valid stop mark found
        // try if there is any late MT_ASPECTSTOP
        clMark *aFirstStart = marks.GetNext(0, MT_ASPECTSTART);
        if (aFirstStart) {
            clMark *aLastStop = marks.GetPrev(INT_MAX, MT_ASPECTSTOP);
            if (aLastStop && (aLastStop->position > iStopA)) {
                dsyslog("cMarkAdStandalone::CheckStop(): start mark is MT_ASPECTSTART (%d) found very late MT_ASPECTSTOP at (%d)", aFirstStart->position, aLastStop->position);
                end = aLastStop;
                marks.DelTill(end->position, &blackMarks, false);
            }
        }
        if (!end) {
            dsyslog("cMarkAdStandalone::CheckStop(): no stop mark found, add stop mark at the last frame (%i)",lastiframe);
            MarkAdMark mark = {};
            mark.Position = lastiframe;  // we are lost, add a end mark at the last iframe
            mark.Type = MT_ASSUMEDSTOP;
            AddMark(&mark);
        }
    }

    // delete all black sceen marks expect start or end mark
    dsyslog("cMarkAdStandalone::CheckStop(): move all black screen marks except start and end mark to black screen list");
    clMark *mark = marks.GetFirst();
    while (mark) {
        if (mark != marks.GetFirst()) {
            if (mark == marks.GetLast()) break;
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) {
                blackMarks.Add(mark->type, mark->position, NULL, mark->inBroadCast); // add mark to blackscreen list
                clMark *tmp = mark;
                mark = mark->Next();
                dsyslog("cMarkAdStandalone::CheckStop(): delete black screen mark (%i)", tmp->position);
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

}


// check if stop mark is start of closing credits without logo
// move stop mark to end of closing credit
// <stopMark> last logo stop mark
// return: true if closing credits was found and last logo stop mark position was changed
//
bool cMarkAdStandalone::MoveLastLogoStopAfterClosingCredits(clMark *stopMark) {
    if (!stopMark) return false;
    dsyslog("cMarkAdStandalone::MoveLastLogoStopAfterClosingCredits(): check closing credits without logo after position (%d)", stopMark->position);

    cExtractLogo *ptr_cExtractLogoChange = new cExtractLogo(macontext.Video.Info.AspectRatio, recordingIndexMark);
    ALLOC(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");

    int newPosition = ptr_cExtractLogoChange->isClosingCredit(&macontext, ptr_cDecoder, stopMark->position);

    FREE(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");
    delete ptr_cExtractLogoChange;

    if (newPosition > stopMark->position) {
        dsyslog("cMarkAdStandalone::MoveLastLogoStopAfterClosingCredits(): closing credits found, move logo stop mark to position (%d)", newPosition);
        marks.Move(&macontext, stopMark, newPosition, "closing credits");
        return true;
    }
    else {
        dsyslog("cMarkAdStandalone::MoveLastLogoStopAfterClosingCredits(): no closing credits found");
        return false;
    }
}


// remove stop/start logo mark pair if it detecs a part in the broadcast with logo changes
// some channel e.g. TELE5 plays with the logo in the broadcast
// return: last stop position with isClosingCredits = 1
//
int cMarkAdStandalone::RemoveLogoChangeMarks() {
    if (strcmp(macontext.Info.ChannelName, "TELE_5") != 0) return 0;  // for performance reason only known channels

    struct timeval startTime, stopTime;
    gettimeofday(&startTime, NULL);
    int lastClosingCreditsEnd = -1;

    LogSeparator();
    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): start detect and remove logo stop/start mark pairs with special logo");

    if (marks.Count(MT_LOGOSTOP) <= 1) {
        dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): only %d logo stop mark, do not delete any", marks.Count(MT_LOGOSTOP));
    }
    else {
        cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = new cEvaluateLogoStopStartPair(&marks, macontext.Video.Info.FramesPerSecond, iStart, iStopA);
        ALLOC(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");

        char *indexToHMSFStop = NULL;
        char *indexToHMSFStart = NULL;

        cExtractLogo *ptr_cExtractLogoChange = NULL;
        int stopPosition = 0;
        int startPosition = 0;

        while (evaluateLogoStopStartPair->GetNextPair(&stopPosition, &startPosition)) {
            if (indexToHMSFStop) {
                FREE(strlen(indexToHMSFStop)+1, "indexToHMSF");
                free(indexToHMSFStop);
            }
            if (indexToHMSFStart) {
                FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
                free(indexToHMSFStart);
            }
            indexToHMSFStop = marks.IndexToHMSF(stopPosition, &macontext);
            indexToHMSFStart = marks.IndexToHMSF(startPosition, &macontext);

            if (indexToHMSFStop && indexToHMSFStart) {
                dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): check logo stop (%6d) at %s and logo start (%6d) at %s", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
            }

            if (!ptr_cDecoderLogoChange) {  // init new instance of cDecoder
                ptr_cDecoderLogoChange = new cDecoder(macontext.Config->threads, recordingIndexMark);
                ALLOC(sizeof(*ptr_cDecoderLogoChange), "ptr_cDecoderLogoChange");
                ptr_cDecoderLogoChange->DecodeDir(directory);
            }
            if (!ptr_cExtractLogoChange) { // init new instance of cExtractLogo
                ptr_cExtractLogoChange = new cExtractLogo(macontext.Video.Info.AspectRatio, recordingIndexMark);
                ALLOC(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");
            }
            if (ptr_cExtractLogoChange->isLogoChange(&macontext, ptr_cDecoderLogoChange, evaluateLogoStopStartPair, stopPosition, startPosition)) {
                if (indexToHMSFStop && indexToHMSFStart) {
                    isyslog("logo has changed between frame (%i) at %s and (%i) at %s, deleting marks", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
                }
                clMark *del1 = marks.Get(stopPosition);
                clMark *del2 = marks.Get(startPosition);
                marks.Del(del1);
                marks.Del(del2);
             }
             else {
                 dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): ^^^^^       logo stop (%6d) at %s and logo start (%6d) at %s pair no logo change, keep marks", stopPosition, indexToHMSFStop, startPosition, indexToHMSFStart);
            }
        }

        lastClosingCreditsEnd = evaluateLogoStopStartPair->GetLastClosingCreditsStart();
        // delete last timer string
        if (indexToHMSFStop) {
            FREE(strlen(indexToHMSFStop)+1, "indexToHMSF");
            free(indexToHMSFStop);
        }
        if (indexToHMSFStart) {
            FREE(strlen(indexToHMSFStart)+1, "indexToHMSF");
            free(indexToHMSFStart);
        }

        if (ptr_cExtractLogoChange) {
            FREE(sizeof(*ptr_cExtractLogoChange), "ptr_cExtractLogoChange");
            delete ptr_cExtractLogoChange;
        }
        FREE(sizeof(*evaluateLogoStopStartPair), "evaluateLogoStopStartPair");
        delete evaluateLogoStopStartPair;
    }

    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): marks after detect and remove logo stop/start mark pairs with special logo");
    DebugMarks();     //  only for debugging

    gettimeofday(&stopTime, NULL);
    time_t sec = stopTime.tv_sec - startTime.tv_sec;
    suseconds_t usec = stopTime.tv_usec - startTime.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        sec--;
    }
    logoChangeTime_ms += sec * 1000 + usec / 1000;

    dsyslog("cMarkAdStandalone::RemoveLogoChangeMarks(): end detect and remove logo stop/start mark pairs with special logo");
    LogSeparator();
    return lastClosingCreditsEnd;
}


void cMarkAdStandalone::CheckStart() {
    LogSeparator(true);
    dsyslog("cMarkAdStandalone::CheckStart(): checking start at frame (%d) check start planed at (%d)", lastiframe, chkSTART);
    dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %i", iStartA);
    DebugMarks();     //  only for debugging

    RemoveLogoChangeMarks();
    clMark *begin = NULL;
    int hBorderStopPosition = 0;
    int delta = macontext.Video.Info.FramesPerSecond * MAXRANGE;
//    macontext.Video.Options.IgnoreBlackScreenDetection = true;   // use black sceen setection only to find start mark

    begin = marks.GetAround(delta, 1, MT_RECORDINGSTART);  // do we have an incomplete recording ?
    if (begin) {
        dsyslog("cMarkAdStandalone::CheckStart(): found MT_RECORDINGSTART (%i), use this as start mark for the incomplete recording", begin->position);
    }

// try to find a audio channel mark
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

        if (macontext.Config->DecodeAudio && macontext.Info.Channels[stream]) {
            if ((macontext.Info.Channels[stream] == 6) && (macontext.Audio.Options.IgnoreDolbyDetection == false)) {
                isyslog("DolbyDigital5.1 audio whith 6 Channels in stream %i detected", stream);
                if (macontext.Audio.Info.channelChange) {
                    dsyslog("cMarkAdStandalone::CheckStart(): channel change detected, disable logo/border/aspect detection");
                    bDecodeVideo = false;
                    macontext.Video.Options.IgnoreAspectRatio = true;
                    macontext.Video.Options.IgnoreLogoDetection = true;
                    macontext.Video.Options.IgnoreBlackScreenDetection = true;
                    marks.Del(MT_ASPECTSTART);
                    marks.Del(MT_ASPECTSTOP);

                    // start mark must be around iStartA
                    begin=marks.GetAround(delta * 3, iStartA, MT_CHANNELSTART);  // decrease from 4
                    if (!begin) {          // previous recording had also 6 channels, try other marks
                        dsyslog("cMarkAdStandalone::CheckStart(): no audio channel start mark found");
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStart(): audio channel start mark found at %d", begin->position);
                        if (begin->position > iStopA) {  // this could be a very short recording, 6 channel is in post recording
                            dsyslog("cMarkAdStandalone::CheckStart(): audio channel start mark after assumed stop mark not valid");
                            begin = NULL;
                        }
                        else {
                            if (marks.GetNext(begin->position, MT_HBORDERSTART) || marks.GetNext(begin->position, MT_VBORDERSTART)) macontext.Video.Info.hasBorder = true;
                            marks.Del(MT_LOGOSTART);   // we do not need the weaker marks if we found a MT_CHANNELSTART
                            marks.Del(MT_LOGOSTOP);
                            marks.Del(MT_HBORDERSTART);
                            marks.Del(MT_HBORDERSTOP);
                            marks.Del(MT_VBORDERSTART);
                            marks.Del(MT_VBORDERSTOP);
                        }
                    }
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): no audio channel change found till now, do not disable logo/border/aspect detection");
            }
            else {
                if (macontext.Audio.Options.IgnoreDolbyDetection == true) isyslog("disabling AC3 decoding (from logo)");
                if ((macontext.Info.Channels[stream]) && (macontext.Audio.Options.IgnoreDolbyDetection == false))
                    isyslog("broadcast with %i audio channels of stream %i",macontext.Info.Channels[stream], stream);
                if (inBroadCast) {  // if we have channel marks but we are now with 2 channels inBroascast, delete these
                    macontext.Video.Options.IgnoreAspectRatio = false;   // then we have to find other marks
                    macontext.Video.Options.IgnoreLogoDetection = false;
                    macontext.Video.Options.IgnoreBlackScreenDetection = false;
                }
            }
        }
    }
    if (begin && inBroadCast) { // set recording aspect ratio for logo search at the end of the recording
        macontext.Info.AspectRatio.Num = macontext.Video.Info.AspectRatio.Num;
        macontext.Info.AspectRatio.Den = macontext.Video.Info.AspectRatio.Den;
        macontext.Info.checkedAspectRatio = true;
        isyslog("Video with aspectratio of %i:%i detected", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
    }
    if (!begin && inBroadCast) {
        clMark *chStart = marks.GetNext(0, MT_CHANNELSTART);
        clMark *chStop = marks.GetPrev(INT_MAX, MT_CHANNELSTOP);
        if (chStart && chStop && (chStart->position > chStop->position)) {
            dsyslog("cMarkAdStandalone::CheckStart(): channel start after channel stop found, delete all weak marks inbetween");
            marks.DelWeakFromTo(chStop->position, chStart->position, MT_CHANNELCHANGE);
        }
    }
    if ( !begin && !inBroadCast) {
        dsyslog("cMarkAdStandalone::CheckStart(): we are not in broadcast at frame (%d), trying to find channel start mark anyway", lastiframe);
        begin=marks.GetAround(delta*4, iStartA, MT_CHANNELSTART);
    }

// try to find a aspect ratio mark
    if (!begin) {
        if ((macontext.Info.AspectRatio.Num == 0) || (macontext.Video.Info.AspectRatio.Den == 0)) {
            isyslog("no video aspect ratio found in vdr info file");
            macontext.Info.AspectRatio.Num = macontext.Video.Info.AspectRatio.Num;
            macontext.Info.AspectRatio.Den = macontext.Video.Info.AspectRatio.Den;
        }
        // check marks and correct if necessary
        clMark *aStart = marks.GetAround(chkSTART, chkSTART + 1, MT_ASPECTSTART);   // check if we have ascpect ratio START/STOP in start part
        clMark *aStopAfter = NULL;
        clMark *aStopBefore = NULL;
        if (aStart) {
            aStopAfter = marks.GetNext(aStart->position, MT_ASPECTSTOP);
            aStopBefore = marks.GetPrev(aStart->position, MT_ASPECTSTOP);
            if (aStopBefore && (aStopBefore->position == 0)) aStopBefore = NULL;   // do not accept initial aspect ratio
        }
        bool earlyAspectChange = false;
        if (aStart && aStopAfter) {  // we are in the first ad, do not correct aspect ratio from info file
            dsyslog("cMarkAdStandalone::CheckStart(): found very early aspect ratio change at (%i) and (%i)", aStart->position, aStopAfter->position);
            earlyAspectChange = true;
        }
        bool wrongAspectInfo = false;
        if ((macontext.Info.AspectRatio.Num == 16) && (macontext.Info.AspectRatio.Den == 9)) {
            if ((aStart && aStopBefore)) {
                dsyslog("cMarkAdStandalone::CheckStart(): found aspect ratio change 16:9 to 4:3 at (%d) to 16:9 at (%d), video info is 16:9, this must be wrong", aStopBefore->position, aStart->position);
                wrongAspectInfo = true;
            }
            if ((macontext.Video.Info.AspectRatio.Num == 4) && (macontext.Video.Info.AspectRatio.Den == 3) && inBroadCast) {
                dsyslog("cMarkAdStandalone::CheckStart(): vdr info tells 16:9 but we are in broadcast and found 4:3, vdr info file must be wrong");
                wrongAspectInfo = true;
            }
        }
        // cerrect wrong aspect ratio from vdr info file
        if (wrongAspectInfo || ((!earlyAspectChange) && ((macontext.Info.AspectRatio.Num != macontext.Video.Info.AspectRatio.Num) ||
                                                         (macontext.Info.AspectRatio.Den != macontext.Video.Info.AspectRatio.Den)))) {
            MarkAdAspectRatio newMarkAdAspectRatio;
            newMarkAdAspectRatio.Num = 16;
            newMarkAdAspectRatio.Den = 9;
            if ((macontext.Info.AspectRatio.Num == 16) && (macontext.Info.AspectRatio.Den == 9)) {
                newMarkAdAspectRatio.Num = 4;
                newMarkAdAspectRatio.Den = 3;
            }
            isyslog("video aspect description in info (%d:%d) wrong, correct to (%d:%d)", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den, newMarkAdAspectRatio.Num, newMarkAdAspectRatio.Den);
            macontext.Info.AspectRatio.Num = newMarkAdAspectRatio.Num;
            macontext.Info.AspectRatio.Den = newMarkAdAspectRatio.Den;
            // we have to invert MT_ASPECTSTART and MT_ASPECTSTOP
            clMark *aMark = marks.GetFirst();
            while (aMark) {
                if (aMark->type == MT_ASPECTSTART) aMark->type = MT_ASPECTSTOP;
                else if (aMark->type == MT_ASPECTSTOP) aMark->type = MT_ASPECTSTART;
                aMark = aMark->Next();
            }
        }
        if (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H264) {
            isyslog("HD Video with aspectratio of %i:%i detected", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
        }
        else {
            isyslog("SD Video with aspectratio of %i:%i detected", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
            if (((macontext.Info.AspectRatio.Num == 4) && (macontext.Info.AspectRatio.Den == 3))) {
                isyslog("logo/border detection disabled");
                bDecodeVideo = false;
                macontext.Video.Options.IgnoreAspectRatio = false;
                macontext.Video.Options.IgnoreLogoDetection = true;
                macontext.Video.Options.IgnoreBlackScreenDetection = true;
                marks.Del(MT_CHANNELSTART);
                marks.Del(MT_CHANNELSTOP);
                // start mark must be around iStartA
                begin = marks.GetAround(delta * 4, iStartA, MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART found at (%i)", begin->position);
                    if (begin->position > abs(iStartA) / 4) {    // this is a valid start
                        dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART at (%i) is valid, delete all logo and border marks", begin->position);
                        marks.Del(MT_LOGOSTART);  // we found MT_ASPECTSTART, we do not need weeker marks
                        marks.Del(MT_LOGOSTOP);
                        marks.Del(MT_HBORDERSTART);
                        marks.Del(MT_HBORDERSTOP);
                        marks.Del(MT_VBORDERSTART);
                        marks.Del(MT_VBORDERSTOP);
                        macontext.Video.Options.ignoreHborder = true;
                        macontext.Video.Options.ignoreVborder = true;
                   }
                   else { // if there is a MT_ASPECTSTOP, delete all marks after this position
                       clMark *aStopNext = marks.GetNext(begin->position, MT_ASPECTSTOP);
                       if (aStopNext) {
                           dsyslog("cMarkAdStandalone::CheckStart(): found MT_ASPECTSTOP (%i), delete all weaker marks after", aStopNext->position);
                           marks.DelWeakFromTo(aStopNext->position, INT_MAX, aStopNext->type);
                       }
                       else {
                           dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTOP is not valid (%i), ignoring", begin->position);
                           begin = NULL;
                       }
                   }

                }
                else {
                    dsyslog("cMarkAdStandalone::CheckStart(): no MT_ASPECTSTART found");   // previous is 4:3 too, try another start mark
                    clMark *begin2 = marks.GetAround(iStartA, iStartA + delta, MT_START, 0x0F);
                    if (begin2) {
                        dsyslog("cMarkAdStandalone::CheckStart(): using mark at position (%i) as start mark", begin2->position);
                        begin = begin2;
                    }
                }
            }
            else { // recording is 16:9 but maybe we can get a MT_ASPECTSTART mark if previous recording was 4:3
                begin = marks.GetAround(delta * 3, iStartA, MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART found at (%i) because previous recording was 4:3", begin->position);
                    clMark *begin3 = marks.GetAround(delta, begin->position, MT_VBORDERSTART);  // do not use this mark if there is a later vborder start mark
                    if (begin3 && (begin3->position >  begin->position)) {
                        dsyslog("cMarkAdStandalone::CheckStart(): found later MT_VBORDERSTAT, do not use MT_ASPECTSTART");
                        begin = NULL;
                    }
                    else {
                        begin3 = marks.GetAround(delta * 4, begin->position, MT_LOGOSTART);  // do not use this mark if there is a later logo start mark
                        if (begin3 && (begin3->position >  begin->position)) {
                            dsyslog("cMarkAdStandalone::CheckStart(): found later MT_LOGOSTART, do not use MT_ASPECTSTART");
                            begin = NULL;
                        }
                    }
                }
            }
        }
        macontext.Info.checkedAspectRatio = true;
    }

// try to find a horizontal border mark
    if (!begin) {
        clMark *hStart = marks.GetAround(iStartA + delta, iStartA + delta, MT_HBORDERSTART);
        if (!hStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no horizontal border at start found, ignore horizontal border detection");
            macontext.Video.Options.ignoreHborder = true;
            clMark *hStop = marks.GetAround(iStartA + delta, iStartA + delta, MT_HBORDERSTOP);
            if (hStop) {
                int pos = hStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): horizontal border stop without start mark found (%i), assume as start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from horizontal border stop (%d)", pos) == -1) comment = NULL;
                ALLOC(strlen(comment)+1, "comment");
                begin=marks.Add(MT_ASSUMEDSTART, pos, comment);
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        else { // we found a hborder start mark
            dsyslog("cMarkAdStandalone::CheckStart(): horizontal border start found at (%i)", hStart->position);
            clMark *hStop = marks.GetNext(hStart->position, MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
            if ( hStop && ((hStop->position - hStart->position) < (2 * delta))) {
                dsyslog("cMarkAdStandalone::CheckStart(): horizontal border STOP (%i) short after horizontal border START (%i) found, this is not valid, delete marks", hStop->position, hStart->position);
                hBorderStopPosition = hStop->position;  // maybe we can use it as start mark if we found nothing else
                marks.Del(hStart);
                marks.Del(hStop);

            }
            else {
                if (hStart->position != 0) {  // position 0 is a hborder previous recording
                    dsyslog("cMarkAdStandalone::CheckStart(): delete VBORDER marks if any");
                    marks.Del(MT_VBORDERSTART);
                    marks.Del(MT_VBORDERSTOP);
                    begin = hStart;   // found valid horizontal border start mark
                    macontext.Video.Options.ignoreVborder = true;
                }
            }
        }
    }

// try to find a vertical border mark
    if (!begin) {
        clMark *vStart = marks.GetAround(iStartA+delta, iStartA + delta, MT_VBORDERSTART);
        if (!vStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no vertical border at start found, ignore vertical border detection");
            macontext.Video.Options.ignoreVborder = true;
            clMark *vStop = marks.GetAround(iStartA + delta, iStartA + delta, MT_VBORDERSTOP);
            if (vStop) {
                int pos = vStop->position;
                char *comment = NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment = NULL;
                ALLOC(strlen(comment)+1, "comment");
                marks.Add(MT_ASSUMEDSTART, pos, comment);
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): vertical border start found at (%i)", vStart->position);
            clMark *vStop = marks.GetNext(vStart->position, MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
            if (vStop) {
                clMark *vNextStart = marks.GetNext(vStop->position, MT_VBORDERSTART);
                int markDiff = static_cast<int> (vStop->position - vStart->position) / macontext.Video.Info.FramesPerSecond;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop found at (%d), %ds after vertical border start", vStop->position, markDiff);
                if ((markDiff < 70) ||    // reduced from 90 to 35 increased to 60 increased to 70
                   ((lastiframe > iStopA) && !vNextStart)) {  // we have only start/stop vborder in start part, this is the opening or closing credits
                    isyslog("vertical border STOP at (%d) %ds after vertical border START (%i) in start part found, this is not valid, delete marks", vStop->position, markDiff, vStart->position);
                    marks.Del(vStop);
                    marks.Del(vStart);
                    vStart = NULL;
                }
            }
            if (vStart) {
                if (vStart->position != 0) {  // position 0 is a vborder previous recording
                    dsyslog("cMarkAdStandalone::CheckStart(): delete HBORDER marks if any");
                    marks.Del(MT_HBORDERSTART);
                    marks.Del(MT_HBORDERSTOP);
                    begin = vStart;   // found valid vertical border start mark
                    macontext.Video.Info.hasBorder = true;
                    macontext.Video.Options.ignoreHborder = true;
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): ignore vertical border start found at (0)");
            }
        }
    }

// try to find a logo mark
    if (!begin) {
        clMark *lStart = marks.GetAround(iStartA + (2 * delta), iStartA, MT_LOGOSTART);   // increase from 1
        if (!lStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no logo start mark found");
        }
        else {
            bool isInvalid = true;
            while (isInvalid) {
                char *indexToHMSF = marks.IndexToHMSF(lStart->position, &macontext);
                if (indexToHMSF) {
                    dsyslog("cMarkAdStandalone::CheckStart(): logo start mark found on position (%i) at %s", lStart->position, indexToHMSF);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
                clMark *lStop = marks.GetNext(lStart->position, MT_LOGOSTOP);  // get next logo stop mark
                if (lStop) {  // there is a next stop mark in the start range
                    int distanceStartStop = (lStop->position - lStart->position) / macontext.Video.Info.FramesPerSecond;
                    if (distanceStartStop < 144) {  // very short logo part, lStart is possible wrong, do not increase, first ad can be early
                        indexToHMSF = marks.IndexToHMSF(lStop->position, &macontext);
                        if (indexToHMSF) {
                            dsyslog("cMarkAdStandalone::CheckStart(): logo stop mark found very short after start mark on position (%i) at %s, distance %ds", lStop->position, indexToHMSF, distanceStartStop);
                            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                            free(indexToHMSF);
                        }
                        clMark *lNextStart = marks.GetNext(lStop->position, MT_LOGOSTART); // get next logo start mark
                        if (lNextStart) {  // now we have logo start/stop/start, this can be a preview before broadcast start
                            indexToHMSF = marks.IndexToHMSF(lNextStart->position, &macontext);
                            int distanceStopNextStart = (lNextStart->position - lStop->position) / macontext.Video.Info.FramesPerSecond;
                            if ((distanceStopNextStart <= 76) || // found start mark short after start/stop, use this as start mark, changed from 21 to 68 to 76
                                (distanceStartStop <= 10)) { // very short logo start stop is not valid
                                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): found start mark short after logo start/stop marks on position (%i) at %s", lNextStart->position, indexToHMSF);
                                lStart = lNextStart;
                            }
                            else {
                                isInvalid = false;
                                if (indexToHMSF) dsyslog("cMarkAdStandalone::CheckStart(): next logo start mark (%i) at %s too far away %d", lNextStart->position, indexToHMSF, distanceStopNextStart);
                            }
                            if (indexToHMSF) {
                                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                                free(indexToHMSF);
                            }
                        }
                        else isInvalid = false;
                    }
                    else {  // there is a next stop mark but too far away
                        dsyslog("cMarkAdStandalone::CheckStart(): next logo stop mark (%d) but too far away %ds", lStop->position, distanceStartStop);
                        isInvalid = false;
                    }
                }
                else { // the is no next stop mark
                    isInvalid = false;
                }
            }
            if (lStart->position  >= (iStart / 8)) {
                begin = lStart;   // found valid logo start mark
                marks.Del(MT_HBORDERSTART);  // there could be hborder from an advertising in the recording
                marks.Del(MT_HBORDERSTOP);
            }
            else {  // logo start mark too early, try if there is a later logo start mark
                clMark *lNextStart = marks.GetNext(lStart->position, MT_LOGOSTART);
                if (lNextStart && (lNextStart->position  > (iStart / 8)) && ((lNextStart->position - lStart->position) < (5 * delta))) {  // found later logo start mark
                    char *indexToHMSF = marks.IndexToHMSF(lNextStart->position, &macontext);
                    if (indexToHMSF) {
                        dsyslog("cMarkAdStandalone::CheckStart(): later logo start mark found on position (%i) at %s", lNextStart->position, indexToHMSF);
                        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                        free(indexToHMSF);
                    }
                    begin = lNextStart;   // found valid logo start mark
                    macontext.Video.Info.hasBorder = true;
                }
            }
        }
    }

    if (begin && ((begin->position == 0))) { // we found the correct type but the mark is too early because the previous recording has same type
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%d) type 0x%X dropped because it is too early", begin->position, begin->type);
        begin = NULL;
    }

    if (!begin) {    // try anything
        marks.DelTill(1, &blackMarks);    // we do not want to have a start mark at position 0
        begin = marks.GetAround(iStartA + 3 * delta, iStartA, MT_START, 0x0F);  // increased from 2 to 3
        if (begin) {
            if ((begin->type == MT_NOBLACKSTART) && (begin->position > (iStartA + 2 * delta))) {
                char *indexToHMSF = marks.IndexToHMSF(begin->position, &macontext);
                if (indexToHMSF) {
                    dsyslog("cMarkAdStandalone::CheckStart(): found only very late black screen start mark (%i), ignoring", begin->position);
                    FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                    free(indexToHMSF);
                }
                begin = NULL;
            }
            else {
                if ((begin->inBroadCast) || macontext.Video.Options.IgnoreLogoDetection){  // test on inBroadCast because we have to take care of black screen marks in an ad
                    char *indexToHMSF = marks.IndexToHMSF(begin->position, &macontext);
                    if (indexToHMSF) {
                        dsyslog("cMarkAdStandalone::CheckStart(): found start mark (%i) type 0x%X at %s inBroadCast %i", begin->position, begin->type, indexToHMSF, begin->inBroadCast);
                        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                        free(indexToHMSF);
                    }
                }
                else { // mark in ad
                    char *indexToHMSF = marks.IndexToHMSF(begin->position, &macontext);
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

    if (begin) {
        marks.DelTill(begin->position, &blackMarks);    // delete all marks till start mark
        CalculateCheckPositions(begin->position);
        char *indexToHMSF = marks.IndexToHMSF(begin->position, &macontext);
        if (indexToHMSF) {
            isyslog("using mark on position (%i) type 0x%X at %s as start mark", begin->position, begin->type, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }


        if ((begin->type == MT_VBORDERSTART) || (begin->type == MT_HBORDERSTART)) {
            isyslog("found %s borders, logo detection disabled",(begin->type == MT_HBORDERSTART) ? "horizontal" : "vertical");
            macontext.Video.Options.IgnoreLogoDetection = true;
            macontext.Video.Options.IgnoreBlackScreenDetection = true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

        dsyslog("cMarkAdStandalone::CheckStart(): move all black screen marks except start mark to black screen list");
        clMark *mark = marks.GetFirst();   // delete all black screen marks because they are weak, execpt the start mark
        while (mark) {
            if (((mark->type & 0xF0) == MT_BLACKCHANGE) && (mark->position > begin->position) ) {
                blackMarks.Add(mark->type, mark->position, NULL, mark->inBroadCast); // add mark to blackscreen list
                clMark *tmp = mark;
                mark = mark->Next();
                marks.Del(tmp);  // delete mark from normal list
                continue;
            }
            mark = mark->Next();
        }
    }
    else { //fallback
        // try hborder stop mark as start mark
        if (hBorderStopPosition > 0) {
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, use MT_HBORDERSTOP from previous recoring as start mark");
            marks.Add(MT_ASSUMEDSTART, hBorderStopPosition, "start mark from border stop of previous recording*", true);
            marks.DelTill(hBorderStopPosition, &blackMarks);
        }
        else {  // set start after pre timer
            dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, assume start time at pre recording time");
            marks.DelTill(iStart, &blackMarks);
            marks.Del(MT_NOBLACKSTART);  // delete all black screen marks
            marks.Del(MT_NOBLACKSTOP);
            MarkAdMark mark = {};
            mark.Position = iStart;
            mark.Type = MT_ASSUMEDSTART;
            AddMark(&mark);
            CalculateCheckPositions(iStart);
        }
    }

// count logo STOP/START pairs
    int countStopStart = 0;
    clMark *mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && (mark->Next()->type == MT_LOGOSTART)) {
            countStopStart++;
        }
        mark = mark->Next();
    }
    if ((countStopStart >= 3) && begin) {
        isyslog("%d logo STOP/START pairs found after start mark, something is wrong with your logo", countStopStart);
        if (video->ReducePlanes()) {
            dsyslog("cMarkAdStandalone::CheckStart(): reduce logo processing to first plane and delete all marks after start mark (%d)", begin->position);
            marks.DelFrom(begin->position);
        }
    }

    iStart = 0;
    marks.Save(directory, &macontext, isTS, false);
    DebugMarks();     //  only for debugging
    LogSeparator();
    return;
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
    clMark *mark = marks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position, &macontext);
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
        char *indexToHMSF = marks.IndexToHMSF(mark->position, &macontext);
        if (indexToHMSF) {
            tsyslog("mark at position %6i type 0x%X at %s inBroadCast %i", mark->position, mark->type, indexToHMSF, mark->inBroadCast);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
    dsyslog("*************************************************************");
}


void cMarkAdStandalone::CheckMarks() {           // cleanup marks that make no sense
    LogSeparator();
    clMark *mark = NULL;

// delete logo and border marks if we have channel marks
    dsyslog("cMarkAdStandalone::CheckMarks(): check marks first pass (delete logo marks if we have channel or border marks)");
    DebugMarks();     //  only for debugging
    clMark *channelStart = marks.GetNext(0, MT_CHANNELSTART);
    clMark *channelStop = marks.GetNext(0, MT_CHANNELSTOP);
    clMark *hborderStart = marks.GetNext(0, MT_HBORDERSTART);
    clMark *hborderStop = marks.GetNext(0, MT_HBORDERSTOP);
    clMark *vborderStart = marks.GetNext(0, MT_VBORDERSTART);
    clMark *vborderStop = marks.GetNext(0, MT_VBORDERSTOP);
    if (hborderStart && hborderStop) {
        int hDelta = (hborderStop->position - hborderStart->position) / macontext.Video.Info.FramesPerSecond;
        if (hDelta < 120) {
            dsyslog("cMarkAdStandalone::CheckMarks(): found hborder stop/start, but distance %d too short, try if there is a next pair", hDelta);
            hborderStart = marks.GetNext(hborderStart->position, MT_HBORDERSTART);
            hborderStop = marks.GetNext(hborderStop->position, MT_HBORDERSTOP);
        }
    }
    if (vborderStart && vborderStop) {
        int vDelta = (vborderStop->position - vborderStart->position) / macontext.Video.Info.FramesPerSecond;
        if (vDelta < 120) {
            dsyslog("cMarkAdStandalone::CheckMarks(): found vborder stop/start, but distance %d too short, try if there is a next pair", vDelta);
            vborderStart = marks.GetNext(vborderStart->position, MT_VBORDERSTART);
            vborderStop = marks.GetNext(vborderStop->position, MT_VBORDERSTOP);
        }
    }
    if ((channelStart && channelStop) || (hborderStart && hborderStop) || (vborderStart && vborderStop)) {
        mark = marks.GetFirst();
        while (mark) {
            if (mark != marks.GetFirst()) {
                if (mark == marks.GetLast()) break;
                if ((mark->type == MT_LOGOSTART) || (mark->type == MT_LOGOSTOP)) {
                    clMark *tmp = mark;
                    mark = mark->Next();
                    dsyslog("cMarkAdStandalone::CheckMarks(): delete logo mark (%i)", tmp->position);
                    marks.Del(tmp);
                    continue;
                }
            }
            mark = mark->Next();
        }
    }

// delete all black sceen marks expect start or end mark
    dsyslog("cMarkAdStandalone::CheckMarks(): check marks 2nd pass (delete invalid black sceen marks)");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if (mark != marks.GetFirst()) {
            if (mark == marks.GetLast()) break;
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) {
                clMark *tmp = mark;
                mark = mark->Next();
                dsyslog("cMarkAdStandalone::CheckMarks(): delete black screen mark (%i)", tmp->position);
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

// delete short START STOP logo marks because they are previews in the advertisement
// delete short START STOP hborder marks because they are advertisement in the advertisement
    dsyslog("cMarkAdStandalone::CheckMarks(): check marks 2nd pass (detect previews in advertisement)");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
// check logo marks
        if ((mark->type == MT_LOGOSTART) && (mark->position != marks.GetFirst()->position) && mark->Next()) {  // not start or end mark
            if ((mark->Next()->type == MT_LOGOSTOP) && (mark->Next()->position != marks.GetLast()->position)) { // next mark not end mark
                clMark *stopBefore = marks.GetPrev(mark->position, MT_LOGOSTOP);
                if (stopBefore) {  // if advertising before is long this is the really the next start mark
                    int lengthAdBefore = static_cast<int> ((mark->position - stopBefore->position) / macontext.Video.Info.FramesPerSecond);
                    int lengthPreview = static_cast<int> ((mark->Next()->position - mark->position) / macontext.Video.Info.FramesPerSecond);
                    if ((lengthAdBefore >= 1) && (lengthAdBefore <= 585)) {  // if advertising before is long this is the really the next start mark
                                                                             // previews can be at start of advertising (e.g. DMAX)
                                                                             // max changed from 500 to 560 to 585
                                                                             // min changed from 7 to 5 to 1
                        if (lengthPreview < 110) {  // if logo part is long, removed check of min (was >=2)
                            isyslog("found preview of length %is between logo mark (%i) and logo mark (%i) in advertisement (length %is), deleting marks", lengthPreview, mark->position, mark->Next()->position, lengthAdBefore);
                            clMark *tmp=mark;
                            mark = mark->Next()->Next();
                            marks.Del(tmp->Next());
                            marks.Del(tmp);
                            continue;
                        }
                        else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%i) and (%i), length %is not valid",  mark->position, mark->Next()->position, lengthPreview);
                    }
                    else dsyslog("cMarkAdStandalone::CheckMarks(): no preview between (%d) and (%d), length advertising before %ds is not valid (expect >=1s and <110s)", mark->position, mark->Next()->position, lengthAdBefore);
                }
                else dsyslog("cMarkAdStandalone::CheckMarks(): no preview because no MT_LOGOSTOP before found");
            }
        }
// check hborder marks
        if ((mark->type == MT_HBORDERSTART) && (mark->position != marks.GetFirst()->position) && mark->Next()) {  // not start or end mark
            clMark *bStop = marks.GetNext(mark->position, MT_HBORDERSTOP);
            if (bStop && (bStop->position != marks.GetLast()->position)) { // next mark not end mark
                int lengthAd = static_cast<int> ((bStop->position - mark->position) / macontext.Video.Info.FramesPerSecond);
                if (lengthAd < 130) { // increased from 70 to 130
                    isyslog("found advertisement of length %is between hborder mark (%i) and hborder mark (%i), deleting marks", lengthAd, mark->position, bStop->position);
                    clMark *tmp = mark;
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

// delete short START STOP logo marks because they are previes not detected above
// delete short STOP START logo marks because they are logo detection failure
// delete short STOP START hborder marks because some channels display information in the border
    dsyslog("cMarkAdStandalone::CheckMarks(): check marks 3nd pass (remove logo and hborder detection failure marks)");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if ((mark->type == MT_LOGOSTART) && mark->Next() && mark->Next()->type == MT_LOGOSTOP) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.FramesPerSecond * 18); // changed from 8 to 18
            double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.FramesPerSecond;
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                isyslog("mark distance between logo START and STOP too short %.1fs, deleting (%i,%i)", distance, mark->position, mark->Next()->position);
                clMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): mark distance between logo START and STOP %.1fs, keep (%i,%i)", distance, mark->position, mark->Next()->position);
        }
        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.FramesPerSecond * 23);   // assume thre is shortest advertising, changed from 20s to 23s
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between logo STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        if ((mark->type == MT_HBORDERSTOP) && mark->Next() && mark->Next()->type == MT_HBORDERSTART) {
            int MARKDIFF = static_cast<int> (macontext.Video.Info.FramesPerSecond * 20);  // increased from 15 to 20
            if ((mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance = (mark->Next()->position - mark->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between horizontal STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp = mark;
                mark = mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        mark = mark->Next();
    }

    dsyslog("cMarkAdStandalone::CheckMarks(): check marks 4nd pass (remove invalid marks)");
    DebugMarks();     //  only for debugging
    mark = marks.GetFirst();
    while (mark) {
        if (((mark->type & 0x0F) == MT_STOP) && (mark == marks.GetFirst())){
            dsyslog("Start with STOP mark, delete first mark");
            clMark *tmp = mark;
            mark = mark->Next();
            marks.Del(tmp);
            continue;
        }
        if (((mark->type & 0x0F)==MT_START) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_START)) {  // two start marks, delete second
            dsyslog("cMarkAdStandalone::CheckMarks(): start mark (%i) followed by start mark (%i) delete second", mark->position, mark->Next()->position);
            marks.Del(mark->Next());
            continue;
        }
        if (((mark->type & 0x0F)==MT_STOP) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_STOP)) {  // two stop marks, delete second
            dsyslog("cMarkAdStandalone::CheckMarks(): stop mark (%i) followed by stop mark (%i) delete first", mark->position, mark->Next()->position);
            clMark *tmp=mark;
            mark = mark->Next();
            marks.Del(tmp);
            continue;
        }
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

// if we have a VPS events, move start and stop mark to VPS event
    if (macontext.Config->useVPS) {
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): apply VPS events");
        DebugMarks();     //  only for debugging
        if (ptr_cDecoder) {
            int vpsOffset = marks.LoadVPS(macontext.Config->recDir, "START:"); // VPS start mark
            if (vpsOffset >= 0) {
                isyslog("found VPS start event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_START, false);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS start event found");

            vpsOffset = marks.LoadVPS(macontext.Config->recDir, "PAUSE_START:");     // VPS pause start mark = stop mark
            if (vpsOffset >= 0) {
                isyslog("found VPS pause start event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_STOP, true);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause start event found");

            vpsOffset = marks.LoadVPS(macontext.Config->recDir, "PAUSE_STOP:");     // VPS pause stop mark = start mark
            if (vpsOffset >= 0) {
                isyslog("found VPS pause stop event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_START, true);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause stop event found");

            vpsOffset = marks.LoadVPS(macontext.Config->recDir, "STOP:");     // VPS stop mark
            if (vpsOffset >= 0) {
                isyslog("found VPS stop event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_STOP, false);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS stop event found");
        }
        else isyslog("VPS info usage requires --cDecoder");

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
                    clMark *tmp=mark;
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
                    clMark *tmp=mark;
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
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckMarks(): final marks:");
    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type, const bool isPause) {
    if (!ptr_cDecoder) return;
    int delta = macontext.Video.Info.FramesPerSecond * MAXRANGE;
    int vpsFrame = recordingIndexMark->GetFrameFromOffset(offset * 1000);
    if (vpsFrame < 0) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): failed to get frame from offset %d", offset);
    }
    clMark *mark = NULL;
    char *comment = NULL;
    char *timeText = NULL;
    if (!isPause) {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame, &macontext);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "start" : "stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        char *indexToHMSF = marks.IndexToHMSF(vpsFrame, &macontext);
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d) at %s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
        mark = ((type == MT_START)) ? marks.GetAround(delta, vpsFrame, MT_START, 0x0F) :  marks.GetAround(delta, vpsFrame, MT_STOP, 0x0F);
    }
    if (!mark) {
        if (isPause) {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): no mark found to replace with pause mark, add new mark");
            if (asprintf(&comment,"VPS %s (%d)%s", (type == MT_START) ? "pause start" : "pause stop", vpsFrame, (type == MT_START) ? "*" : "") == -1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, vpsFrame, comment);
            FREE(strlen(comment)+1,"comment");
            free(comment);
            return;
        }
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found no mark found to replace");
        return;
    }
    if ( (type & 0x0F) != (mark->type & 0x0F)) return;

    timeText = marks.IndexToHMSF(mark->position, &macontext);
    if (timeText) {
        if ((mark->type > MT_LOGOCHANGE) && (mark->type != MT_RECORDINGSTART)) {  // keep strong marks, they are better than VPS marks
                                                                                  // for VPS recording we replace recording start mark
            dsyslog("cMarkAdStandalone::AddMarkVPS(): keep mark at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
        }
        else {
            dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);
            if (asprintf(&comment,"VPS %s (%d), moved from mark (%d) type 0x%X at %s %s", (type == MT_START) ? "start" : "stop", vpsFrame, mark->position, mark->type, timeText, (type == MT_START) ? "*" : "") == -1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            dsyslog("cMarkAdStandalone::AddMarkVPS(): delete mark on position (%d)", mark->position);
            marks.Del(mark->position);
            marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, vpsFrame, comment);
            FREE(strlen(comment)+1,"comment");
            free(comment);
            if ((type == MT_START) && !isPause) {   // delete all marks before vps start
                marks.DelWeakFromTo(0, vpsFrame, 0xFF);
            }
            else if ((type == MT_STOP) && isPause) {  // delete all marks between vps start and vps pause start
                clMark *startVPS = marks.GetFirst();
                if (startVPS && (startVPS->type == MT_VPSSTART)) {
                    marks.DelWeakFromTo(startVPS->position, vpsFrame, MT_VPSCHANGE);
                }
            }
        }
        FREE(strlen(timeText)+1, "indexToHMSF");
        free(timeText);
    }
}


void cMarkAdStandalone::AddMark(MarkAdMark *Mark) {
    if (!Mark) return;
    if (!Mark->Type) return;
    if ((macontext.Config) && (macontext.Config->logoExtraction != -1)) return;
    if (gotendmark) return;

    char *comment = NULL;
    switch (Mark->Type) {
        case MT_ASSUMEDSTART:
            if (asprintf(&comment, "assuming start (%i)*", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_ASSUMEDSTOP:
            if (asprintf(&comment, "assuming stop (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_NOBLACKSTART:
            if (asprintf(&comment, "detected end of black screen (%i)*", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_NOBLACKSTOP:
            if (asprintf(&comment, "detected start of black screen (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_LOGOSTART:
            if (asprintf(&comment, "detected logo start (%i)*", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_LOGOSTOP:
            if (asprintf(&comment, "detected logo stop (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_HBORDERSTART:
            if (asprintf(&comment, "detected start of horiz. borders (%i)*", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_HBORDERSTOP:
            if (asprintf(&comment, "detected stop of horiz. borders (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_VBORDERSTART:
            if (asprintf(&comment, "detected start of vert. borders (%i)*", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_VBORDERSTOP:
            if (asprintf(&comment, "detected stop of vert. borders (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_ASPECTSTART:
            if (!Mark->AspectRatioBefore.Num) {
                if (asprintf(&comment, "aspectratio start with %i:%i (%i)*", Mark->AspectRatioAfter.Num, Mark->AspectRatioAfter.Den, Mark->Position) == -1) comment = NULL;
                ALLOC(strlen(comment)+1, "comment");
            }
            else {
                if (asprintf(&comment, "aspectratio change from %i:%i to %i:%i (%i)*", Mark->AspectRatioBefore.Num, Mark->AspectRatioBefore.Den,
                         Mark->AspectRatioAfter.Num, Mark->AspectRatioAfter.Den, Mark->Position) == -1) comment = NULL;
                ALLOC(strlen(comment)+1, "comment");
                if ((macontext.Config->autoLogo > 0) &&( Mark->Position > 0) && bDecodeVideo) {
                    isyslog("logo detection reenabled at frame (%d), trying to find a logo from this position", Mark->Position);
                    macontext.Video.Options.IgnoreLogoDetection = false;
                    macontext.Video.Options.IgnoreBlackScreenDetection = false;
                }
            }
            break;
        case MT_ASPECTSTOP:
            if (asprintf(&comment, "aspectratio change from %i:%i to %i:%i (%i)", Mark->AspectRatioBefore.Num, Mark->AspectRatioBefore.Den,
                     Mark->AspectRatioAfter.Num, Mark->AspectRatioAfter.Den, Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            if ((macontext.Config->autoLogo > 0) && (Mark->Position > 0) && bDecodeVideo) {
                isyslog("logo detection reenabled at frame (%d), trying to find a logo from this position", Mark->Position);
                macontext.Video.Options.IgnoreLogoDetection = false;
                macontext.Video.Options.IgnoreBlackScreenDetection = false;
            }
            break;
        case MT_CHANNELSTART:
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment, "audio channel change from %i to %i (%i)*", Mark->ChannelsBefore, Mark->ChannelsAfter, Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_CHANNELSTOP:
            if ((Mark->Position > chkSTART) && (Mark->Position < iStopA / 2) && !macontext.Audio.Info.channelChange) {
                dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after chkSTART, disable logo/border/aspect detection now");
                if (iStart == 0) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we heve selected a start mark
                bDecodeVideo = false;
                macontext.Video.Options.IgnoreAspectRatio = true;
                macontext.Video.Options.IgnoreLogoDetection = true;
                macontext.Video.Options.IgnoreBlackScreenDetection = true;
            }
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment, "audio channel change from %i to %i (%i)", Mark->ChannelsBefore, Mark->ChannelsAfter, Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_RECORDINGSTART:
            if (asprintf(&comment, "start of recording (%i)", Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_RECORDINGSTOP:
            if (asprintf(&comment, "stop of recording (%i)",Mark->Position) == -1) comment = NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        default:
            dsyslog("cMarkAdStandalone::AddMark(): unknown mark type 0x%X", Mark->Type);
    }

    clMark *prev = marks.GetLast();
    if (prev) {
        if ((prev->type & 0x0F) == (Mark->Type & 0x0F)) {
            int markDiff;
            if (iStart != 0) markDiff = static_cast<int> (macontext.Video.Info.FramesPerSecond * 2);  // before chkStart: let more marks untouched, we need them for start detection
            else markDiff = static_cast<int> (macontext.Video.Info.FramesPerSecond * 30);
            int diff = abs(Mark->Position-prev->position);
            if (diff < markDiff) {
                if (prev->type > Mark->Type) {
                    isyslog("previous mark (%i) type 0x%X stronger than actual mark, deleting (%i) type 0x%X", prev->position, prev->type, Mark->Position, Mark->Type);
                    if ((Mark->Type == MT_NOBLACKSTOP) || (Mark->Type == MT_NOBLACKSTART)) {
                        blackMarks.Add(Mark->Type, Mark->Position, NULL, false); // add mark to blackscreen list
                    }
                    if (comment) {
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                    }
                    return;
                }
                else {
                    isyslog("actual mark (%i) type 0x%X stronger then previous mark, deleting %i type 0x%X", Mark->Position, Mark->Type, prev->position, prev->type);
                    if ((prev->type == MT_NOBLACKSTOP) || (prev->type == MT_NOBLACKSTART)) {
                        blackMarks.Add(prev->type, prev->position, NULL, false); // add mark to blackscreen list
                    }
                    marks.Del(prev);
                }
            }
        }
    }

// set inBroadCast status
    if ((Mark->Type & 0xF0) != MT_BLACKCHANGE){ //  dont use BLACKSCEEN to detect if we are in broadcast
        if (!((Mark->Type <= MT_ASPECTSTART) && (marks.GetPrev(Mark->Position, MT_CHANNELSTOP) && marks.GetPrev(Mark->Position, MT_CHANNELSTART)))) { // if there are MT_CHANNELSTOP and MT_CHANNELSTART marks, wait for MT_CHANNELSTART
            if ((Mark->Type & 0x0F) == MT_START) {
                inBroadCast = true;
            }
            else {
                inBroadCast = false;
            }
        }
    }

// add mark
    char *indexToHMSF = marks.IndexToHMSF(Mark->Position, &macontext);
    if (indexToHMSF) {
        if (comment) isyslog("%s at %s inBroadCast: %i",comment, indexToHMSF, inBroadCast);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    marks.Add(Mark->Type, Mark->Position, comment, inBroadCast);
    if (comment) {
        FREE(strlen(comment)+1, "comment");
        free(comment);
    }

// save marks
    if (iStart == 0) marks.Save(directory, &macontext, isTS, false);  // save after start mark is valid
}


// save currect content of the frame buffer to /tmp
// if path and suffix is set, this will set as target path and file name suffix
//
#if defined(DEBUG_LOGO_DETECT_FRAME_CORNER) || defined(DEBUG_MARK_FRAMES)
void cMarkAdStandalone::SaveFrame(const int frame, const char *path, const char *suffix) {
    if (!macontext.Video.Info.Height) {
        dsyslog("cMarkAdStandalone::SaveFrame(): macontext.Video.Info.Height not set");
        return;
    }
    if (!macontext.Video.Info.Width) {
        dsyslog("cMarkAdStandalone::SaveFrame(): macontext.Video.Info.Width not set");
        return;
    }
    if (!macontext.Video.Data.Valid) {
        dsyslog("cMarkAdStandalone::SaveFrame():  macontext.Video.Data.Valid not set");
        return;
    }
    char szFilename[1024];

    for (int plane = 0; plane < PLANES; plane++) {
        int height;
        int width;
        if (plane == 0) {
            height = macontext.Video.Info.Height;
            width  = macontext.Video.Info.Width;
        }
        else {
            height = macontext.Video.Info.Height / 2;
            width  = macontext.Video.Info.Width  / 2;
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
                    if (time(NULL) > (startTime + (time_t) length)) {
                        // "old" recording
//                        tsyslog("assuming old recording, now>startTime+length");
                        return;
                    }
                    else {
                        sleepcnt = 0;
                        if (!iwaittime) esyslog("recording interrupted, waiting for continuation...");
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


bool cMarkAdStandalone::ProcessMark2ndPass(clMark **mark1, clMark **mark2) {
    if (!ptr_cDecoder) return false;
    if (!mark1) return false;
    if (!*mark1) return false;
    if (!mark2) return false;
    if (!*mark2) return false;

    MarkAdPos *ptr_MarkAdPos = NULL;

    if (!Reset(false)) {
        // reset all, but marks
        esyslog("failed resetting state");
        return false;
    }

// calculate overlap check positions
#define OVERLAP_CHECK_BEFORE 120  // start 2 min before stop mark
    int fRangeBegin = (*mark1)->position - (macontext.Video.Info.FramesPerSecond * OVERLAP_CHECK_BEFORE);
    if (fRangeBegin < 0) fRangeBegin = 0;                    // not before beginning of broadcast
    fRangeBegin = recordingIndexMark->GetIFrameBefore(fRangeBegin);
    if (fRangeBegin < 0) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
#define OVERLAP_CHECK_AFTER 300  // start 5 min after start mark
    int fRangeEnd = (*mark2)->position + (macontext.Video.Info.FramesPerSecond * OVERLAP_CHECK_AFTER);

    clMark *prevStart = marks.GetPrev((*mark1)->position, MT_START, 0x0F);
    if (prevStart) {
        if (fRangeBegin <= (prevStart->position + ((OVERLAP_CHECK_AFTER + 1) * macontext.Video.Info.FramesPerSecond))) { // previous start mark less than OVERLAP_CHECK_AFTER away, prevent overlapping check
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): previous stop mark at (%d) very near, unable to check overlap", prevStart->position);
            return false;
        }
    }

    clMark *nextStop = marks.GetNext((*mark2)->position, MT_STOP, 0x0F);
    if (nextStop) {
        if (nextStop->position != marks.GetLast()->position) {
            if (fRangeEnd >= (nextStop->position - ((OVERLAP_CHECK_BEFORE + OVERLAP_CHECK_AFTER + 1) * macontext.Video.Info.FramesPerSecond))) { // next start mark less than OVERLAP_CHECK_AFTER + OVERLAP_CHECK_BEFORE away, prevent overlapping check
                fRangeEnd = nextStop->position - ((OVERLAP_CHECK_BEFORE + 1) * macontext.Video.Info.FramesPerSecond);
                if (fRangeEnd <= (*mark2)->position) {
                    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): next stop mark at (%d) very near, unable to check overlap", nextStop->position);
                    return false;
                }
                dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): next stop mark at (%d) to near, reduce check end position", nextStop->position);
            }
        }
        if (nextStop->position < fRangeEnd) fRangeEnd = nextStop->position;  // do not check after next stop mark position
    }

    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): preload from frame       (%5d) to (%5d)", fRangeBegin, (*mark1)->position);
    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): compare with frames from (%5d) to (%5d)", (*mark2)->position, fRangeEnd);

// seek to start frame of overlap check
    char *indexToHMSF = marks.IndexToHMSF(fRangeBegin, &macontext);
    if (indexToHMSF) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): start check %ds before at frame (%d) and start overlap check at %s", OVERLAP_CHECK_BEFORE, fRangeBegin, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    if (!ptr_cDecoder->SeekToFrame(&macontext, fRangeBegin)) {
        esyslog("could not seek to frame (%i)", fRangeBegin);
        return false;
    }

// get iFrame count of range to check for overlap
    int iFrameCount = recordingIndexMark->GetIFrameRangeCount(fRangeBegin, (*mark1)->position);
    if (iFrameCount < 0) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
        return false;
    }
    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): %d iFrames to preload between start of check (%d) and stop mark (%d)", iFrameCount, fRangeBegin, (*mark1)->position);

// preload frames before stop mark
    while (ptr_cDecoder->GetFrameNumber() <= (*mark1)->position ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetNextFrame failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())  // if we have interlaced video this is expected, we have to read the next half picture
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() before mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos = video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(), iFrameCount, true, (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
    }

// seek to iFrame before start mark
    fRangeBegin = recordingIndexMark->GetIFrameBefore((*mark2)->position);
    if (fRangeBegin <= 0) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    if (fRangeBegin <  ptr_cDecoder->GetFrameNumber()) fRangeBegin = ptr_cDecoder->GetFrameNumber(); // on very short stop/start pairs we have no room to go before start mark
    indexToHMSF = marks.IndexToHMSF(fRangeBegin, &macontext);
    if (indexToHMSF) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): seek forward to iFrame (%d) at %s before start mark (%d) and start overlap check", fRangeBegin, indexToHMSF, (*mark2)->position);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    if (!ptr_cDecoder->SeekToFrame(&macontext, fRangeBegin)) {
        esyslog("could not seek to frame (%d)", fRangeBegin);
        return false;
    }

    iFrameCount = recordingIndexMark->GetIFrameRangeCount(fRangeBegin, fRangeEnd) - 2;
    if (iFrameCount < 0) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
            return false;
    }
    char *indexToHMSFbegin = marks.IndexToHMSF(fRangeBegin, &macontext);
    char *indexToHMSFend = marks.IndexToHMSF(fRangeEnd, &macontext);
    if (indexToHMSFbegin && indexToHMSFend) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): process overlap detection between frame (%d) at %s and frame (%d) at %s", fRangeBegin, indexToHMSFbegin, fRangeEnd, indexToHMSFend);
    }
    if (indexToHMSFbegin) {
        FREE(strlen(indexToHMSFbegin)+1, "indexToHMSF");
        free(indexToHMSFbegin);
    }
    if (indexToHMSFend) {
        FREE(strlen(indexToHMSFend)+1, "indexToHMSF");
        free(indexToHMSFend);
    }

// process frames after start mark and detect overlap
    while (ptr_cDecoder->GetFrameNumber() <= fRangeEnd ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): GetNextFrame failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() after mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos = video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(), iFrameCount, false, (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
        if (ptr_MarkAdPos) {
            // found overlap
            char *indexToHMSFbefore = marks.IndexToHMSF(ptr_MarkAdPos->FrameNumberBefore, &macontext);
            char *indexToHMSFmark1 = marks.IndexToHMSF((*mark1)->position, &macontext);
            char *indexToHMSFmark2 = marks.IndexToHMSF((*mark2)->position, &macontext);
            char *indexToHMSFafter = marks.IndexToHMSF(ptr_MarkAdPos->FrameNumberAfter, &macontext);
            if (indexToHMSFbefore && indexToHMSFmark1 && indexToHMSFmark2 && indexToHMSFafter) {
                dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): found overlap from (%6d) at %s to (%6d) at %s are identical with",
                            ptr_MarkAdPos->FrameNumberBefore, indexToHMSFbefore, (*mark1)->position, indexToHMSFmark1);
                dsyslog("cMarkAdStandalone::ProcessMark2ndPass():                    (%6d) at %s to (%6d) at %s",
                            (*mark2)->position, indexToHMSFmark2, ptr_MarkAdPos->FrameNumberAfter, indexToHMSFafter);
            }
            if (indexToHMSFbefore) {
                FREE(strlen(indexToHMSFbefore)+1, "indexToHMSF");
                free(indexToHMSFbefore);
            }
            if (indexToHMSFmark1) {
                FREE(strlen(indexToHMSFmark1)+1, "indexToHMSF");
                free(indexToHMSFmark1);
            }
            if (indexToHMSFmark2) {
                FREE(strlen(indexToHMSFmark2)+1, "indexToHMSF");
                free(indexToHMSFmark2);
            }
            if (indexToHMSFafter) {
                FREE(strlen(indexToHMSFafter)+1, "indexToHMSF");
                free(indexToHMSFafter);
            }
            *mark1 = marks.Move(&macontext, *mark1, ptr_MarkAdPos->FrameNumberBefore, "overlap");
            *mark2 = marks.Move(&macontext, *mark2, ptr_MarkAdPos->FrameNumberAfter, "overlap");
            marks.Save(directory, &macontext, isTS, false);
            return true;
        }
    }
    return false;
}


#ifdef DEBUG_MARK_FRAMES
void cMarkAdStandalone::DebugMarkFrames() {
    if (!ptr_cDecoder) return;

    ptr_cDecoder->Reset();
    clMark *mark = marks.GetFirst();
    if (!mark) return;

    int writePosition = mark->position;
    for (int i = 0; i < DEBUG_MARK_FRAMES; i++) {
        writePosition = recordingIndexMark->GetIFrameBefore(writePosition - 1);
    }
    int writeOffset = -DEBUG_MARK_FRAMES;

    // read and decode all video frames, we want to be sure we have a valid decoder state, this is a debug function, we dont care about performance
    while(mark && (ptr_cDecoder->DecodeDir(directory))) {
        while(mark && (ptr_cDecoder->GetNextFrame())) {
            if (ptr_cDecoder->isVideoPacket()) {
                if (ptr_cDecoder->GetFrameInfo(&macontext)) {
                    if (ptr_cDecoder->GetFrameNumber() >= writePosition) {
                        dsyslog("cMarkAdStandalone::DebugMarkFrames(): mark at frame (%5d) write frame (%5d)", mark->position, writePosition);
                        if (writePosition == mark->position) {
                            if ((mark->type & 0x0F) == MT_START) SaveFrame(mark->position, directory, "START");
                            else if ((mark->type & 0x0F) == MT_STOP) SaveFrame(mark->position, directory, "STOP");
                                 else SaveFrame(mark->position, directory, "MOVED");
                        }
                        else {
                            SaveFrame(writePosition, directory, (writePosition < mark->position) ? "BEFORE" : "AFTER");
                        }
                        writePosition = recordingIndexMark->GetIFrameAfter(writePosition + 1);
                        if (writeOffset >= DEBUG_MARK_FRAMES) {
                            mark = mark->Next();
                            if (!mark) break;
                            for (int i = 0; i < DEBUG_MARK_FRAMES; i++) {
                                writePosition = recordingIndexMark->GetIFrameBefore(mark->position - 1);
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
        isyslog("video cut function only supported with --cDecoder");
        return;
    }
    ptr_cDecoder->Reset();
    cEncoder* ptr_cEncoder = NULL;
    LogSeparator(true);
    isyslog("start cut video based on marks");
    if (marks.Count() < 2) {
        isyslog("need at least 2 marks to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("final marks are:");
    DebugMarks();     //  only for debugging

    clMark *StartMark = marks.GetFirst();
    if ((StartMark->type & 0x0F) != MT_START) {
        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
        return;
    }
    clMark *StopMark = StartMark->Next();
    if ((StopMark->type & 0x0F) != MT_STOP) {
        esyslog("got invalid stop mark at (%i) type 0x%X", StopMark->position, StopMark->type);
        return;
    }
    int stopPosition = recordingIndexMark->GetIFrameBefore(StopMark->position);
    ptr_cEncoder = new cEncoder(macontext.Config->threads, macontext.Config->ac3ReEncode);
    ALLOC(sizeof(*ptr_cEncoder), "ptr_cEncoder");
    if (!ptr_cEncoder->OpenFile(directory, ptr_cDecoder)) {
        esyslog("failed to open output file");
        FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
        delete ptr_cEncoder;
        ptr_cEncoder = NULL;
        return;
    }
    while(ptr_cDecoder->DecodeDir(directory)) {
        while(ptr_cDecoder->GetNextFrame()) {
            if  (ptr_cDecoder->GetFrameNumber() < StartMark->position) ptr_cDecoder->SeekToFrame(&macontext, StartMark->position);
            if  (ptr_cDecoder->GetFrameNumber() > stopPosition) {
                if (StopMark->Next() && StopMark->Next()->Next()) {
                    StartMark = StopMark->Next();
                    if ((StartMark->type & 0x0F) != MT_START) {
                        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
                        return;
                    }
                    StopMark = StartMark->Next();
                    if ((StopMark->type & 0x0F) != MT_STOP) {
                        esyslog("got invalid stop mark at (%i) type 0x%X", StopMark->position, StopMark->type);
                        return;
                    }
                    stopPosition = recordingIndexMark->GetIFrameBefore(StopMark->position);
                }
                else break;
            }
            AVPacket *pkt = ptr_cDecoder->GetPacket();
            if ( !pkt ) {
                esyslog("failed to get packet from input stream");
                return;
            }
            if (!ptr_cEncoder->WritePacket(pkt, ptr_cDecoder)) {
                dsyslog("cMarkAdStandalone::MarkadCut(): failed to write frame %d to output stream", ptr_cDecoder->GetFrameNumber());
            }
            if (abortNow) {
                if (ptr_cDecoder) {
                    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                    delete ptr_cDecoder;
                    ptr_cDecoder = NULL;
                }
                ptr_cEncoder->CloseFile();  // ptr_cEncoder must be valid here because it is used above
                FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
                delete ptr_cEncoder;
                ptr_cEncoder = NULL;
                return;
            }
        }
    }
    if (!ptr_cEncoder->CloseFile()) {
        dsyslog("failed to close output file");
        return;
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): end at frame %d", ptr_cDecoder->GetFrameNumber());
    FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
    delete ptr_cEncoder;  // ptr_cEncoder must be valid here because it is used above
    ptr_cEncoder = NULL;
    framecnt4 = ptr_cDecoder->GetFrameNumber();
}


// 3nd pass
// move logo marks:
//     - if closing credits are detected after last logo stop mark
//     - if silence was detected before start mark or after/before end mark
//     - if black screen marks are direct before stop mark or direct after start mark
//
void cMarkAdStandalone::Process3ndPass() {
    if (!ptr_cDecoder) return;

    LogSeparator(true);
    isyslog("start 3nd pass (optimze logo marks)");
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Process3ndPass(): check last logo stop mark if closing credits follows");

    bool save = false;
// check last logo stop mark if closing credits follows
    if (ptr_cDecoder) {  // we use file position from 2ndPass call
        clMark *lastStop = marks.GetLast();
        if (lastStop->type == MT_LOGOSTOP) {
            dsyslog("cMarkAdStandalone::Process3ndPass(): search for closing credits");
            if (MoveLastLogoStopAfterClosingCredits(lastStop)) {
                dsyslog("cMarkAdStandalone::Process3ndPass(): moved last logo stop mark after closing credit");
            }
            save = true;
            framecnt3 = ptr_cDecoder->GetFrameNumber() - framecnt2;
        }
    }

// check for advertising in frame with logo before logo stop mark
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Process3ndPass(): check for advertising in frame with logo before logo stop mark");
// for performance reason only for known channels for now
    if ((strcmp(macontext.Info.ChannelName, "SIXX") == 0) ||
        (strcmp(macontext.Info.ChannelName, "RTL2") == 0) || // maybe there also exists broadcast in frame TODO
        (strcmp(macontext.Info.ChannelName, "kabel_eins") == 0)) {
        ptr_cDecoder->Reset();
        ptr_cDecoder->DecodeDir(directory);

        cExtractLogo *ptr_cExtractLogoAdInFrame = NULL;
        clMark *markLogo = marks.GetFirst();
        while (markLogo) {
            if (markLogo->type == MT_LOGOSTART) {
                int searchEndPosition = markLogo->position + (35 * macontext.Video.Info.FramesPerSecond); // advertising in frame are usually 30s
                char *indexToHMSFStopMark = marks.IndexToHMSF(markLogo->position, &macontext);
                char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchEndPosition, &macontext);
                if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::Process3ndPass(): search advertising in frame with logo after logo start mark (%d) at %s from position %d at %s", markLogo->position, indexToHMSFStopMark, searchEndPosition, indexToHMSFSearchPosition);
                if (indexToHMSFStopMark) {
                    FREE(strlen(indexToHMSFStopMark)+1, "indexToHMSF");
                    free(indexToHMSFStopMark);
                }
                if (indexToHMSFSearchPosition) {
                    FREE(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
                    free(indexToHMSFSearchPosition);
                }

                if (!ptr_cDecoder->SeekToFrame(&macontext, markLogo->position)) {
                    esyslog("could not seek to frame (%d)", markLogo->position);
                    break;
                }
                if (!ptr_cExtractLogoAdInFrame) {
                    ptr_cExtractLogoAdInFrame = new cExtractLogo(macontext.Video.Info.AspectRatio, recordingIndexMark);
                    ALLOC(sizeof(*ptr_cExtractLogoAdInFrame), "ptr_cExtractLogoAdInFrame");
                }
                int newStopPosition = ptr_cExtractLogoAdInFrame->SearchAdInFrame(&macontext, ptr_cDecoder, searchEndPosition, true);
                if (newStopPosition != -1) {
                    newStopPosition = recordingIndexMark->GetIFrameAfter(newStopPosition);  // we got last frame of ad, go to next iFrame for start mark
                    markLogo = marks.Move(&macontext, markLogo, newStopPosition, "advertising in frame");
                    save = true;
                    continue;
                }
            }
            if (markLogo->type == MT_LOGOSTOP) {
                int searchStartPosition = markLogo->position - (35 * macontext.Video.Info.FramesPerSecond); // advertising in frame are usually 30s
                char *indexToHMSFStopMark = marks.IndexToHMSF(markLogo->position, &macontext);
                char *indexToHMSFSearchPosition = marks.IndexToHMSF(searchStartPosition, &macontext);
                if (indexToHMSFStopMark && indexToHMSFSearchPosition) dsyslog("cMarkAdStandalone::Process3ndPass(): search advertising in frame with logo before logo stop mark (%d) at %s from position %d at %s", markLogo->position, indexToHMSFStopMark, searchStartPosition, indexToHMSFSearchPosition);
                if (indexToHMSFStopMark) {
                    FREE(strlen(indexToHMSFStopMark)+1, "indexToHMSF");
                    free(indexToHMSFStopMark);
                }
                if (indexToHMSFSearchPosition) {
                    FREE(strlen(indexToHMSFSearchPosition)+1, "indexToHMSF");
                    free(indexToHMSFSearchPosition);
                }

                if (!ptr_cDecoder->SeekToFrame(&macontext, searchStartPosition)) {
                    esyslog("could not seek to frame (%d)", searchStartPosition);
                    break;
                }
                if (!ptr_cExtractLogoAdInFrame) {
                    ptr_cExtractLogoAdInFrame = new cExtractLogo(macontext.Video.Info.AspectRatio, recordingIndexMark);
                    ALLOC(sizeof(*ptr_cExtractLogoAdInFrame), "ptr_cExtractLogoAdInFrame");
                }
                int newStopPosition = ptr_cExtractLogoAdInFrame->SearchAdInFrame(&macontext, ptr_cDecoder, markLogo->position, false);
                if (newStopPosition != -1) {
                    newStopPosition = recordingIndexMark->GetIFrameBefore(newStopPosition);  // we got first frame of ad, go one iFrame back for stop mark
                    markLogo = marks.Move(&macontext, markLogo, newStopPosition, "advertising in frame");
                    save = true;
                    continue;
                }
            }
            markLogo=markLogo->Next();
        }
        if (ptr_cExtractLogoAdInFrame) {
            FREE(sizeof(*ptr_cExtractLogoAdInFrame), "ptr_cExtractLogoAdInFrame");
            delete(ptr_cExtractLogoAdInFrame);
        }
    }
    else dsyslog("skip for this channel");

// search for audio silence near logo marks
    LogSeparator(false);
    dsyslog("cMarkAdStandalone::Process3ndPass(): search for audio silence");
#define BLACKSCREEN_RANGE 6 // in s
    int silenceRange = 5;  // do not increase, otherwise we got stop marks behind separation images
    if (strcmp(macontext.Info.ChannelName, "DMAX") == 0) silenceRange = 12; // logo color change at the begin

    ptr_cDecoder->Reset();
    ptr_cDecoder->DecodeDir(directory);

    char *indexToHMSF = NULL;
    clMark *mark = marks.GetFirst();
    while (mark) {
        if (indexToHMSF) {
           FREE(strlen(indexToHMSF)+1, "indexToHMSF");
           free(indexToHMSF);
        }
        indexToHMSF = marks.IndexToHMSF(mark->position, &macontext);

        if (mark->type == MT_LOGOSTART) {
            if (indexToHMSF) dsyslog("cMarkAdStandalone::Process3ndPass(): detect audio silence before logo mark at frame (%6i) type 0x%X at %s range %is", mark->position, mark->type, indexToHMSF, silenceRange);
            int seekPos =  mark->position - (silenceRange * macontext.Video.Info.FramesPerSecond);
            if (seekPos < 0) seekPos = 0;
            if (!ptr_cDecoder->SeekToFrame(&macontext, seekPos)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            framecnt3 += silenceRange * macontext.Video.Info.FramesPerSecond;
            int beforeSilence = ptr_cDecoder->GetNextSilence(mark->position, true);
            if ((beforeSilence >= 0) && (beforeSilence != mark->position)) {
                dsyslog("cMarkAdStandalone::Process3ndPass(): found audio silence before logo start at iFrame (%i)", beforeSilence);
                mark = marks.Move(&macontext, mark, beforeSilence, "silence");
                save = true;
                continue;
            }
            if (indexToHMSF) dsyslog("cMarkAdStandalone::Process3ndPass(): no audio silence before logo mark at frame (%6i) type 0x%X at %s found", mark->position, mark->type, indexToHMSF);

        }
        if (mark->type == MT_LOGOSTOP) {
            // search before stop mark
            if (indexToHMSF) dsyslog("cMarkAdStandalone::Process3ndPass(): detect audio silence before logo stop mark at frame (%6i) type 0x%X at %s range %i", mark->position, mark->type, indexToHMSF, silenceRange);
            int seekPos =  mark->position - (silenceRange * macontext.Video.Info.FramesPerSecond);
            if (seekPos < ptr_cDecoder->GetFrameNumber()) seekPos = ptr_cDecoder->GetFrameNumber();  // will retun -1 before first frame read
            if (seekPos < 0) seekPos = 0;
            if (!ptr_cDecoder->SeekToFrame(&macontext, seekPos)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            int beforeSilence = ptr_cDecoder->GetNextSilence(mark->position, true);
            if (beforeSilence >= 0) dsyslog("cMarkAdStandalone::Process3ndPass(): found audio silence before logo stop mark (%i) at iFrame (%i)", mark->position, beforeSilence);

            // search after stop mark
            if (indexToHMSF) dsyslog("cMarkAdStandalone::Process3ndPass(): detect audio silence after logo stop mark at frame (%6i) type 0x%X at %s range %i", mark->position, mark->type, indexToHMSF, silenceRange);
            if (!ptr_cDecoder->SeekToFrame(&macontext, mark->position)) {
                esyslog("could not seek to frame (%i)", mark->position);
                break;
            }
            int stopFrame =  mark->position + (silenceRange * macontext.Video.Info.FramesPerSecond);
            int afterSilence = ptr_cDecoder->GetNextSilence(stopFrame, false);
            if (afterSilence >= 0) dsyslog("cMarkAdStandalone::Process3ndPass(): found audio silence after logo stop mark (%i) at iFrame (%i)", mark->position, afterSilence);
            framecnt3 += 2 * silenceRange * macontext.Video.Info.FramesPerSecond;
            bool before = false;

            // use before silence only if we found no after silence
            if (afterSilence < 0) {
                afterSilence = beforeSilence;
                before = true;
            }

            if ((afterSilence >= 0) && (afterSilence != mark->position)) {
                if (indexToHMSF) dsyslog("cMarkAdStandalone::Process3ndPass(): detect audio silence for mark at frame (%6i) type 0x%X at %s range %i", mark->position, mark->type, indexToHMSF, silenceRange);
                dsyslog("cMarkAdStandalone::Process3ndPass(): use audio silence %s logo stop at iFrame (%i)", (before) ? "before" : "after", afterSilence);
                mark = marks.Move(&macontext, mark, afterSilence, "silence");
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
    dsyslog("cMarkAdStandalone::Process3ndPass(): start search for blackscreen near logo marks");
    mark = marks.GetFirst();
    while (mark) {
        if (mark->type == MT_LOGOSTART) {
            clMark *blackMark = blackMarks.GetAround(BLACKSCREEN_RANGE * macontext.Video.Info.FramesPerSecond, mark->position, MT_NOBLACKSTART);
            if (blackMark) {
                int distance = mark->position - blackMark->position;
                int distance_s = distance / macontext.Video.Info.FramesPerSecond;
                if (distance > 0)  { // blackscreen is before logo start mark
                    dsyslog("cMarkAdStandalone::Process3ndPass(): blackscreen (%d) distance (%d) %ds (expect >0 and <=%ds) before logo start mark (%d), move mark", blackMark->position, distance, distance_s, BLACKSCREEN_RANGE, mark->position);
                    mark = marks.Move(&macontext, mark, blackMark->position, "black screen");
                    save = true;
                    continue;
                }
                else dsyslog("cMarkAdStandalone::Process3ndPass(): blackscreen (%d) distance (%d) %ds (expect >0 and <=%ds) before (-after) logo start mark (%d), keep mark", blackMark->position, distance, distance_s, BLACKSCREEN_RANGE, mark->position);
            }
            else dsyslog("cMarkAdStandalone::Process3ndPass(): no black screen mark found before logo start mark (%d)", mark->position);
        }
        if (mark->type == MT_LOGOSTOP) {
            clMark *blackMark = blackMarks.GetAround(BLACKSCREEN_RANGE * macontext.Video.Info.FramesPerSecond, mark->position, MT_NOBLACKSTOP);
            if (blackMark) {
                int distance = blackMark->position - mark->position;
                int distance_s = distance / macontext.Video.Info.FramesPerSecond;
                if (distance == 0) { // found blackscreen at same position
                    mark=mark->Next();
                    continue;
                }
                if (distance > 0)  { // blackscreen is after logo stop mark
                    dsyslog("cMarkAdStandalone::Process3ndPass(): blackscreen (%d) distance (%d) %ds (expect >0 and <=%ds) after logo stop mark (%d), move mark", blackMark->position, distance, distance_s, BLACKSCREEN_RANGE, mark->position);
                }
                else {  // blackscreen is before logo stop mark
                    dsyslog("cMarkAdStandalone::Process3ndPass(): blackscreen (%d) distance (%d) %ds (expect >0 and <=%ds) before logo stop mark (%d), move mark", blackMark->position, -distance, -distance_s, BLACKSCREEN_RANGE, mark->position);
                }
                mark = marks.Move(&macontext, mark, blackMark->position, "black screen");
                save = true;
                continue;
            }
            else dsyslog("cMarkAdStandalone::Process3ndPass(): no black screen mark found after logo stop mark (%d)", mark->position);
        }
        mark=mark->Next();
    }

    if (save) marks.Save(directory, &macontext, isTS, false);
    return;
}


void cMarkAdStandalone::Process2ndPass() {
    if (abortNow) return;
    if (duplicate) return;
    if (!ptr_cDecoder) return;
    if (!length) return;
    if (!startTime) return;
    if (time(NULL) < (startTime+(time_t) length)) return;

    LogSeparator(true);
    isyslog("start 2nd pass (detect overlaps)");

    if (!macontext.Video.Info.FramesPerSecond) {
        isyslog("WARNING: assuming fps of 25");
        macontext.Video.Info.FramesPerSecond = 25;
    }

    if (!marks.Count()) {
        marks.Load(directory, macontext.Video.Info.FramesPerSecond, isTS);
    }
    clMark *p1 = NULL,*p2 = NULL;

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
                dsyslog("cMarkAdStandalone::Process2ndPass(): ->->->->-> check overlap before stop frame (%d) and after start frame (%d)", p1->position, p2->position);
                if (!ProcessMark2ndPass(&p1, &p2)) {
                    dsyslog("cMarkAdStandalone::Process2ndPass(): no overlap found for marks before frames (%d) and after (%d)", p1->position, p2->position);
                }
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
    framecnt2 = ptr_cDecoder->GetFrameNumber();
    dsyslog("end 2ndPass");
    return;
}


bool cMarkAdStandalone::Reset(bool FirstPass) {
    bool ret = true;
    if (FirstPass) framecnt1 = 0;
    lastiframe = 0;
    iframe = 0;
    gotendmark = false;
    chkSTART = chkSTOP = INT_MAX;

    if (FirstPass) {
        marks.DelAll();
    }
    macontext.Video.Info.Pict_Type = 0;
    macontext.Video.Info.AspectRatio.Den = 0;
    macontext.Video.Info.AspectRatio.Num = 0;
    memset(macontext.Audio.Info.Channels, 0, sizeof(macontext.Audio.Info.Channels));

    if (video) video->Clear(false);
    if (audio) audio->Clear();
    return ret;
}


bool cMarkAdStandalone::ProcessFrame(cDecoder *ptr_cDecoder) {
    if (!ptr_cDecoder) return false;

    if ((macontext.Config->logoExtraction != -1) && (ptr_cDecoder->GetIFrameCount() >= 512)) {    // extract logo
        isyslog("finished logo extraction, please check /tmp for pgm files");
        abortNow=true;
    }

    if (ptr_cDecoder->GetFrameInfo(&macontext)) {
        if (ptr_cDecoder->isVideoPacket()) {
            if (ptr_cDecoder->isInterlacedVideo() && !macontext.Video.Info.Interlaced && (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) &&
                                                     (ptr_cDecoder->GetVideoFramesPerSecond() == 25) && (ptr_cDecoder->GetVideoRealFrameRate() == 50)) {
                dsyslog("change internal frame rate to handle H.264 interlaced video");
                macontext.Video.Info.FramesPerSecond *= 2;
                macontext.Video.Info.Interlaced = true;
                CalculateCheckPositions(macontext.Info.tStart * macontext.Video.Info.FramesPerSecond);
            }
            lastiframe=iframe;
            if ((iStart < 0) && (lastiframe > -iStart)) iStart = lastiframe;
            if ((iStop < 0) && (lastiframe > -iStop)) {
                iStop = lastiframe;
                iStopinBroadCast = inBroadCast;
            }
            if ((iStopA < 0) && (lastiframe > -iStopA)) {
                iStopA = lastiframe;
            }
            iframe = ptr_cDecoder->GetFrameNumber();

            if (!video) {
                esyslog("cMarkAdStandalone::ProcessFrame() video not initialized");
                return false;
            }
            if (!macontext.Video.Data.Valid) {
                isyslog("cMarkAdStandalone::ProcessFrame faild to get video data of frame (%d)", ptr_cDecoder->GetFrameNumber());
                return false;
            }

            if ( !restartLogoDetectionDone && (lastiframe > (iStopA-macontext.Video.Info.FramesPerSecond * 2 * MAXRANGE)) &&
                                     ((macontext.Video.Options.IgnoreBlackScreenDetection) || (macontext.Video.Options.IgnoreLogoDetection))) {
                isyslog("restart logo and black screen detection at frame (%d)", ptr_cDecoder->GetFrameNumber());
                restartLogoDetectionDone = true;
                bDecodeVideo = true;
                macontext.Video.Options.IgnoreBlackScreenDetection = false;   // use black sceen setection only to find end mark
                if (macontext.Video.Options.IgnoreLogoDetection == true) {
                    if (macontext.Video.Info.hasBorder) { // we do not need logos, we have hborder
                        dsyslog("cMarkAdStandalone::ProcessFrame(): we do not need to look for logos, we have a broadcast with border");
                    }
                    else {
                        macontext.Video.Options.IgnoreLogoDetection = false;
                        if (video) video->Clear(true, inBroadCast);    // reset logo detector status
                    }
                }
            }

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
            if ((lastiframe > (DEBUG_LOGO_DETECT_FRAME_CORNER - 200)) && (lastiframe < (DEBUG_LOGO_DETECT_FRAME_CORNER + 200))) {
//                dsyslog("save frame (%i) to /tmp", lastiframe);
                SaveFrame(lastiframe);
            }
#endif

            if (!bDecodeVideo) macontext.Video.Data.Valid = false; // make video picture invalid, we do not need them
            MarkAdMarks *vmarks = video->Process(lastiframe, iframe);
            if (vmarks) {
                for (int i = 0; i < vmarks->Count; i++) {
                    if (((vmarks->Number[i].Type & 0xF0) == MT_LOGOCHANGE) && (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H265)) {   // we are one iFrame to late with logo marks, these is to much (2s) with H.265 codec
                        int iFrameBefore = recordingIndexMark->GetIFrameBefore(vmarks->Number[i].Position);
                        if (iFrameBefore < 0) {
                            dsyslog("cMarkAdStandalone::ProcessFrame(): could not get iFrame before frame %d)", vmarks->Number[i].Position);
                            return false;
                        }
                        dsyslog("cMarkAdStandalone::ProcessFrame(): found logo mark in H.265 recording at (%d) move to (%d)", vmarks->Number[i].Position, iFrameBefore);
                        vmarks->Number[i].Position = iFrameBefore;
                    }
                    AddMark(&vmarks->Number[i]);
                }
            }

            if (iStart > 0) {
                if ((inBroadCast) && (lastiframe > chkSTART)) CheckStart();
            }
            if ((iStop > 0) && (iStopA > 0)) {
                if (lastiframe > chkSTOP) {
                    if (iStart != 0) {
                        dsyslog("still no chkStart called, doing it now");
                        CheckStart();
                    }
                    CheckStop();
                    return false;
                }
            }
        }
        if(ptr_cDecoder->isAudioAC3Packet()) {
             MarkAdMark *amark = audio->Process(lastiframe, iframe);
            if (amark) AddMark(amark);
        }
    }
    return true;
}


void cMarkAdStandalone::ProcessFile_cDecoder() {

    LogSeparator();
    dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): start processing files");
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
            macontext.Info.VPid.Type = ptr_cDecoder->GetVideoType();
            if (macontext.Info.VPid.Type == 0) {
                dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): video type not set");
                return;
            }
            macontext.Video.Info.Height = ptr_cDecoder->GetVideoHeight();
            isyslog("video hight: %i", macontext.Video.Info.Height);

            macontext.Video.Info.Width = ptr_cDecoder->GetVideoWidth();
            isyslog("video width: %i", macontext.Video.Info.Width);

            macontext.Video.Info.FramesPerSecond = ptr_cDecoder->GetVideoFramesPerSecond();
            isyslog("average frame rate %i frames per second", static_cast<int> (macontext.Video.Info.FramesPerSecond));
            isyslog("real frame rate    %i frames per second", ptr_cDecoder->GetVideoRealFrameRate());

            CalculateCheckPositions(macontext.Info.tStart * macontext.Video.Info.FramesPerSecond);
        }
        while(ptr_cDecoder && ptr_cDecoder->GetNextFrame()) {
            if (abortNow) {
                if (ptr_cDecoder) {
                    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                    delete ptr_cDecoder;
                    ptr_cDecoder = NULL;
                }
                break;
            }
            // write an early start mark for running recordings
            if (macontext.Info.isRunningRecording && !macontext.Info.isStartMarkSaved && (ptr_cDecoder->GetFrameNumber() >= (macontext.Info.tStart * macontext.Video.Info.FramesPerSecond))) {
                dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): recording is aktive, read frame (%d), now save dummy start mark at pre timer position %ds", ptr_cDecoder->GetFrameNumber(), macontext.Info.tStart);
                clMarks marksTMP;
                marksTMP.Add(MT_ASSUMEDSTART, ptr_cDecoder->GetFrameNumber(), "timer start", true);
                marksTMP.Save(macontext.Config->recDir, &macontext, true, true);
                macontext.Info.isStartMarkSaved = true;
            }

            if (!cMarkAdStandalone::ProcessFrame(ptr_cDecoder)) break;
            CheckIndexGrowing();
        }
    }

    if (!abortNow) {
        if (iStart !=0 ) {  // iStart will be 0 if iStart was called
            dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): recording ends unexpected before chkSTART (%d) at frame %d", chkSTART, lastiframe);
            isyslog("got end of recording before recording length from info file reached");
            CheckStart();
        }
        if (iStopA > 0) {
            if (iStop <= 0) {  // unexpected end of recording reached
                iStop = lastiframe;
                iStopinBroadCast = true;
                dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): recording ends unexpected before chkSTOP (%d) at frame %d", chkSTOP, lastiframe);
                isyslog("got end of recording before recording length from info file reached");
            }
            CheckStop();
        }
        CheckMarks();
        if ((inBroadCast) && (!gotendmark) && (lastiframe)) {
            MarkAdMark tempmark;
            tempmark.Type = MT_RECORDINGSTOP;
            tempmark.Position = lastiframe;
            AddMark(&tempmark);
        }
    }
    dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): end processing files");
}



void cMarkAdStandalone::Process_cDecoder() {
    if (abortNow) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);
    ProcessFile_cDecoder();

    if (!abortNow) {
        if (marks.Save(directory, &macontext, isTS, false)) {
            if (length && startTime)
                    if (macontext.Config->SaveInfo) SaveInfo();

        }
    }
}


bool cMarkAdStandalone::SetFileUID(char *File) {
    if (!File) return false;
    struct stat statbuf;
    if (!stat(directory, &statbuf)) {
        if (chown(File, statbuf.st_uid, statbuf.st_gid) == -1) return false;
    }
    return true;
}


bool cMarkAdStandalone::SaveInfo() {
    isyslog("writing info file");
    char *src, *dst;
    if (isREEL) {
        if (asprintf(&src, "%s/info.txt", directory) == -1) return false;
    }
    else {
        if (asprintf(&src, "%s/info%s", directory, isTS ? "" : ".vdr") == -1) return false;
    }
    ALLOC(strlen(src)+1, "src");

    if (asprintf(&dst, "%s/info.bak", directory) == -1) {
        free(src);
        return false;
    }
    ALLOC(strlen(dst)+1, "src");

    FILE *r,*w;
    r = fopen(src, "r");
    if (!r) {
        free(src);
        free(dst);
        return false;
    }

    w=fopen(dst, "w+");
    if (!w) {
        fclose(r);
        free(src);
        free(dst);
        return false;
    }

    char *line = NULL;
    char *lline = NULL;
    size_t len = 0;

    char lang[4] = "";

    int component_type_add = 0;
    if (macontext.Video.Info.Height > 576) component_type_add = 8;

    int stream_content = 0;
    if (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H262) stream_content = 1;
    if (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H264) stream_content = 5;

    int component_type_43;
    int component_type_169;
    if ((macontext.Video.Info.FramesPerSecond == 25) || (macontext.Video.Info.FramesPerSecond == 50)) {
        component_type_43 = 1;
        component_type_169 = 3;
    }
    else {
        component_type_43 = 5;
        component_type_169 = 7;
    }

    bool err = false;
    for (int i = 0; i < MAXSTREAMS; i++) {
        dsyslog("stream %i has %i channels", i, macontext.Info.Channels[i]);
    }
    unsigned int stream_index = 0;
    if (ptr_cDecoder) stream_index++;
    while (getline(&line, &len, r) != -1) {
        dsyslog("info file line: %s", line);
        if (line[0] == 'X') {
            int stream = 0;
            unsigned int type = 0;
            char descr[256] = "";

            int result=sscanf(line, "%*c %3i %3X %3c %250c", &stream, &type, (char *) &lang, (char *) &descr);
            if ((result != 0) && (result != EOF)) {
                switch (stream) {
                    case 1:
                    case 5:
                        if (stream == stream_content) {
                            if ((macontext.Info.AspectRatio.Num == 4) && (macontext.Info.AspectRatio.Den == 3)) {
                                if (fprintf(w, "X %i %02i %s 4:3\n", stream_content, component_type_43 + component_type_add, lang) <= 0) err = true;
                                macontext.Info.AspectRatio.Num = 0;
                                macontext.Info.AspectRatio.Den = 0;
                            }
                            else if ((macontext.Info.AspectRatio.Num == 16) && (macontext.Info.AspectRatio.Den == 9)) {
                                if (fprintf(w, "X %i %02X %s 16:9\n", stream_content, component_type_169 + component_type_add, lang) <= 0) err = true;
                                macontext.Info.AspectRatio.Num = 0;
                                macontext.Info.AspectRatio.Den = 0;
                            }
                            else {
                                if (fprintf(w, "%s",line) <=0 ) err = true;
                            }
                        }
                        else {
                            if (fprintf(w, "%s", line) <= 0) err = true;
                        }
                        break;
                    case 2:
                        if (type == 5) {
                            if (macontext.Info.Channels[stream_index] == 6) {
                                if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n", lang) <=0 ) err = true;
                                macontext.Info.Channels[stream_index] = 0;
                            }
                            else if (macontext.Info.Channels[stream_index] == 2) {
                                if (fprintf(w, "X 2 05 %s Dolby Digital 2.0\n", lang) <=0 ) err = true;
                                macontext.Info.Channels[stream_index] = 0;
                            }
                            else {
                                if (fprintf(w, "%s", line) <=0 ) err = true;
                            }
                        }
                        else {
                            if (fprintf(w, "%s", line) <=0 ) err = true;
                        }
                        break;
                    case 4:
                        if (type == 0x2C) {
                            if (fprintf(w, "%s", line) <=0 ) err = true;
                            macontext.Info.Channels[stream_index] = 0;
                            stream_index++;
                        }
                        break;
                    default:
                        if (fprintf(w, "%s", line) <=0 ) err = true;
                        break;
                }
            }
        }
        else {
            if (line[0] != '@') {
                if (fprintf(w, "%s", line) <=0 ) err = true;
            }
            else {
                if (lline) {
                    free(lline);
                    err = true;
                    esyslog("multiple @lines in info file, please report this!");
                }
                lline=strdup(line);
                ALLOC(strlen(lline)+1, "lline");
            }
        }
        if (err) break;
    }
    if (line) free(line);
    line=lline;

    if (lang[0] == 0) strcpy(lang, "und");

    if (stream_content) {
        if ((macontext.Info.AspectRatio.Num == 4) && (macontext.Info.AspectRatio.Den == 3) && (!err)) {
            if (fprintf(w, "X %i %02i %s 4:3\n", stream_content, component_type_43 + component_type_add, lang) <= 0 ) err = true;
        }
        if ((macontext.Info.AspectRatio.Num == 16) && (macontext.Info.AspectRatio.Den == 9) && (!err)) {
            if (fprintf(w, "X %i %02i %s 16:9\n", stream_content, component_type_169 + component_type_add, lang) <= 0) err = true;
        }
    }
    for (short int stream = 0; stream < MAXSTREAMS; stream++) {
        if (macontext.Info.Channels[stream] == 0) continue;
        if ((macontext.Info.Channels[stream] == 2) && (!err)) {
            if (fprintf(w, "X 2 05 %s Dolby Digital 2.0\n", lang) <= 0) err = true;
        }
        if ((macontext.Info.Channels[stream] == 6) && (!err)) {
            if (fprintf(w, "X 2 05 %s Dolby Digital 5.1\n", lang) <= 0) err = true;
       }
    }
    if (line) {
        if (fprintf(w, "%s", line) <=0 ) err = true;
        free(line);
    }
    fclose(w);
    struct stat statbuf_r;
    if (fstat(fileno(r), &statbuf_r) == -1) err = true;

    fclose(r);
    if (err) {
        unlink(dst);
    }
    else {
        if (rename(dst, src) == -1) {
            err = true;
        }
        else {
            // preserve timestamps from old file
            struct utimbuf oldtimes;
            oldtimes.actime = statbuf_r.st_atime;
            oldtimes.modtime = statbuf_r.st_mtime;
            if (utime(src, &oldtimes)) {};
            SetFileUID(src);
        }
    }

    free(src);
    free(dst);
    return (err==false);
}


bool cMarkAdStandalone::isVPSTimer() {
    if (!directory) return false;
    bool timerVPS = false;

    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s", directory, "markad.vps") == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    FILE *mf;
    mf = fopen(fpath, "r");
    if (!mf) {
        dsyslog("cMarkAdStandalone::isVPSTimer(): %s not found", fpath);
        FREE(strlen(fpath)+1, "fpath");
        free(fpath);
        return false;
    }
    FREE(strlen(fpath)+1, "fpath");
    free(fpath);

    char *line = NULL;
    size_t length;
    char vpsTimer[12] = "";
    while (getline(&line, &length, mf) != -1) {
        sscanf(line, "%12s", (char *) &vpsTimer);
        if (strcmp(vpsTimer, "VPSTIMER=YES") == 0) {
            timerVPS = true;
            break;
        }
    }
    if (line) free(line);
    fclose(mf);
    return timerVPS;
}


time_t cMarkAdStandalone::GetBroadcastStart(time_t start, int fd) {
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat statbuf;
    FILE *mounts = setmntent(_PATH_MOUNTED, "r");
    int mlen;
    int oldmlen = 0;
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

    if (useatime) dsyslog("cMarkAdStandalone::GetBroadcastStart(): mount option noatime is set, use atime from directory %s to get creation time", directory);
    else dsyslog("cMarkAdStandalone::GetBroadcastStart(): mount option noatime is not set");

    if ((useatime) && (stat(directory, &statbuf) != -1)) {
        if (fabs(difftime(start,statbuf.st_atime)) < 60 * 60 * 12) {  // do not beleave recordings > 12h
            dsyslog("cMarkAdStandalone::GetBroadcastStart(): got recording start from directory creation time");
            return statbuf.st_atime;
        }
        dsyslog("cMarkAdStandalone::GetBroadcastStart(): got no valid directory creation time, maybe recording was copied %s", strtok(ctime(&statbuf.st_atime), "\n"));
        dsyslog("cMarkAdStandalone::GetBroadcastStart(): broadcast start time from vdr info file                          %s", strtok(ctime(&start), "\n"));
    }

    // try to get from mtime
    // (and hope info.vdr has not changed after the start of the recording)
    if (fstat(fd,&statbuf) != -1) {
        if (fabs(difftime(start, statbuf.st_mtime)) < 7200) {
            dsyslog("cMarkAdStandalone::GetBroadcastStart(): getting recording start from VDR info file modification time     %s", strtok(ctime(&statbuf.st_mtime), "\n"));
            return (time_t) statbuf.st_mtime;
        }
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
    if (!macontext.Config->logoDirectory) return false;
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
        isyslog("no logo found in logo directory, trying to find logo in recording directory");
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
        isyslog("no logo found in recording directory, trying to extract logo from recording");
        ptr_cExtractLogo = new cExtractLogo(macontext.Info.AspectRatio, recordingIndexMark);
        ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
        int startPos =  macontext.Info.tStart * 25;  // search logo from assumed start, we do not know the frame rate at this point, so we use 25
        if (startPos < 0) startPos = 0;  // consider late start of recording
        int endpos = ptr_cExtractLogo->SearchLogo(&macontext, startPos);
        for (int retry = 2; retry <= 4; retry++) {  // reduced from 6 to 4
            startPos += 5 * 60 * 25; // next try 5 min later
            if (endpos > 0) {
                isyslog("no logo found in recording, retry in %ind recording part", retry);
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
    if (asprintf(&buf, "%s/info%s", directory, isTS ? "" : ".vdr") == -1) return false;
    ALLOC(strlen(buf)+1, "buf");

    if (macontext.Config->Before) {
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
    size_t linelen;
    while (getline(&line, &linelen, f) != -1) {
        if (line[0] == 'C') {
            char channelname[256] = "";
            int result = sscanf(line, "%*c %*80s %250c", (char *) &channelname);
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
                if (strcmp(macontext.Info.ChannelName, "SAT_1") == 0) {
                    dsyslog("cMarkAdStandalone::LoadInfo(): channel %s has a rotating logo", macontext.Info.ChannelName);
                    macontext.Video.Logo.isRotating = true;
                }
            }
        }
        if ((line[0] == 'E') && (!bLiveRecording)) {
            int result = sscanf(line,"%*c %*10i %20li %6i %*2x %*2x", &startTime, &length);
            if (result != 2) {
                dsyslog("cMarkAdStandalone::LoadInfo(): vdr info file not valid, could not read start time and length");
                startTime = 0;
                length = 0;
            }
        }
        if (line[0] == 'T') {
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
                macontext.Video.Info.FramesPerSecond = 0;
            }
            else {
                macontext.Video.Info.FramesPerSecond = fps;
            }
        }
        if ((line[0] == 'X') && (!bLiveRecording)) {
            int stream = 0, type = 0;
            char descr[256] = "";
            int result=sscanf(line, "%*c %3i %3i %250c", &stream, &type, (char *) &descr);
            if ((result != 0) && (result != EOF)) {
                if ((stream == 1) || (stream == 5)) {
                    if ((type != 1) && (type != 5) && (type != 9) && (type != 13)) {
                        isyslog("broadcast aspectratio 16:9 (from info)");
                        macontext.Info.AspectRatio.Num = 16;
                        macontext.Info.AspectRatio.Den = 9;
                    }
                    else {
                        isyslog("broadcast aspectratio 4:3 (from info)");
                        macontext.Info.AspectRatio.Num = 4;
                        macontext.Info.AspectRatio.Den = 3;
                    }
                }

                if (stream == 2) {
                    if (type == 5) {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr, '2')) {
                            isyslog("broadcast with DolbyDigital2.0 (from info)");
                            macontext.Info.Channels[stream] = 2;
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr, '5')) {
                            isyslog("broadcast with DolbyDigital5.1 (from info)");
                            macontext.Info.Channels[stream] = 6;
                        }
                    }
                }
            }
        }
    }
    if ((macontext.Info.AspectRatio.Num == 0) && (macontext.Info.AspectRatio.Den == 0)) isyslog("no broadcast aspectratio found in info");
    if (line) free(line);

    macontext.Info.timerVPS = isVPSTimer();
    if ((length) && (startTime)) {
        if (!bIgnoreTimerInfo) {
            time_t rStart = GetBroadcastStart(startTime, fileno(f));
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
                            isyslog("missed broadcast start by %d:%02d min, length will be corrected", -macontext.Info.tStart / 60, -macontext.Info.tStart % 60);
                            startTime = rStart;
                            length += macontext.Info.tStart;
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
        macontext.Info.AspectRatio.Num = 0;
        macontext.Info.AspectRatio.Den = 0;
        bDecodeVideo = macontext.Config->DecodeVideo;
        macontext.Video.Options.IgnoreAspectRatio = false;
    }

    if (!macontext.Info.ChannelName) {
        return false;
    }
    else {
        return true;
    }
}


bool cMarkAdStandalone::CheckTS()
{
    MaxFiles = 0;
    isTS = false;
    if (!directory) return false;
    char *buf;
    if (asprintf(&buf, "%s/00001.ts", directory) == -1) return false;
    ALLOC(strlen(buf)+1, "buf");
    struct stat statbuf;
    if (stat(buf,&statbuf) == -1) {
        if (errno != ENOENT) {
            FREE(strlen(buf)+1, "buf");
            free(buf);
            return false;
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
        buf=NULL;
        if (asprintf(&buf,"%s/001.vdr",directory) == -1) return false;
        ALLOC(strlen(buf)+1, "buf");
        if (stat(buf,&statbuf) == -1) {
            FREE(strlen(buf)+1, "buf");
            free(buf);
            return false;
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
        // .VDR detected
        isTS = false;
        MaxFiles = 999;
        return true;
    }
    FREE(strlen(buf)+1, "buf");
    free(buf);
    // .TS detected
    isTS = true;
    MaxFiles = 65535;
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


cMarkAdStandalone::cMarkAdStandalone(const char *Directory, const MarkAdConfig *config, cIndex *recordingIndex) {
    setlocale(LC_MESSAGES, "");
    directory = Directory;
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

    noticeVDR_VID = false;
    noticeVDR_AC3 = false;
    noticeHEADER = false;
    noticeFILLER = false;

    length = 0;
    sleepcnt = 0;
    waittime = iwaittime = 0;
    duplicate = false;
    title[0] = 0;

    macontext = {};
    macontext.Config = config;

    bDecodeVideo = config->DecodeVideo;
    bDecodeAudio = config->DecodeAudio;

    macontext.Info.tStart = iStart = iStop = iStopA = 0;

    if ((config->ignoreInfo & IGNORE_TIMERINFO) == IGNORE_TIMERINFO) {
        bIgnoreTimerInfo = true;
    }
    else {
        bIgnoreTimerInfo = false;
    }
    macontext.Info.APid.Type = MARKAD_PIDTYPE_AUDIO_MP2;

    if (!config->NoPid) {
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
    if (errno == 0) {
        isyslog("starting v%s (%libit)", VERSION, lb);
    }
    else {
        isyslog("starting v%s", VERSION);
    }

    int ver = avcodec_version();
    char *libver = NULL;
    if (asprintf(&libver, "%i.%i.%i", ver >> 16 & 0xFF, ver >> 8 & 0xFF, ver & 0xFF) != -1) {
        ALLOC(strlen(libver)+1, "libver");
        isyslog("using libavcodec.so.%s with %i threads", libver, config->threads);
        if (ver!=LIBAVCODEC_VERSION_INT) {
            esyslog("libavcodec header version %s", AV_STRINGIFY(LIBAVCODEC_VERSION));
            esyslog("header and library mismatch, do not report decoder bugs");
        }
        if ((ver >> 16) < MINLIBAVCODECVERSION) esyslog("update libavcodec to at least version %d, do not report decoder bugs", MINLIBAVCODECVERSION);
        FREE(strlen(libver)+1, "libver");
        free(libver);
    }

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(41<<8)+0)
    tsyslog("libavcodec config: %s",avcodec_configuration());
#endif
    if (((ver >> 16)<52)) {
        dsyslog("dont report bugs about H264, use libavcodec >= 52 instead!");
    }

    isyslog("on %s", Directory);

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
    if (config->Before) sleep(10);

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

    if (isTS) {
        if (asprintf(&indexFile, "%s/index", Directory) == -1) indexFile = NULL;
        ALLOC(strlen(indexFile)+1, "indexFile");
    }
    else {
        macontext.Info.APid.Num = -1;
        macontext.Info.VPid.Num = -1;
        if (asprintf(&indexFile, "%s/index.vdr", Directory) == -1) indexFile = NULL;
        ALLOC(strlen(indexFile)+1, "indexFile");
    }
    macontext.Info.APid.Num = 0; // till now we do just nothing with stereo-sound

    if (!LoadInfo()) {
        if (bDecodeVideo) {
            esyslog("failed loading info - logo %s%sdisabled",
                    (config->logoExtraction != -1) ? "extraction" : "detection",
                    bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
            macontext.Info.tStart = iStart = iStop = iStopA = 0;
            macontext.Video.Options.IgnoreLogoDetection = true;
        }
    }
    else {
        if (!CheckLogo() && (config->logoExtraction==-1) && (config->autoLogo == 0)) {
            isyslog("no logo found, logo detection disabled");
            macontext.Video.Options.IgnoreLogoDetection = true;
        }
    }

    if (macontext.Info.tStart > 1) {
        if ((macontext.Info.tStart < 60) && (!macontext.Info.timerVPS)) macontext.Info.tStart = 60;
    }
    isyslog("pre-timer %is", macontext.Info.tStart);

    if (length) isyslog("broadcast length %imin", static_cast<int> (round(length / 60)));

    if (title[0]) {
        ptitle = title;
    }
    else {
        ptitle = (char *) Directory;
    }

    if (config->OSD) {
        osd= new cOSDMessage(config->svdrphost, config->svdrpport);
        if (osd) osd->Send("%s '%s'", tr("starting markad for"), ptitle);
    }
    else {
        osd = NULL;
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (macontext.Info.VPid.Num) {
        if (isTS) {
            isyslog("found %s-video (0x%04x)", macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262", macontext.Info.VPid.Num);
        }
    }
    if (macontext.Info.APid.Num) {
        if (macontext.Info.APid.Num != -1)
            isyslog("found MP2 (0x%04x)", macontext.Info.APid.Num);
    }

    if (!abortNow) {
        video = new cMarkAdVideo(&macontext, recordingIndex);
        ALLOC(sizeof(*video), "video");
        audio = new cMarkAdAudio(&macontext);
        ALLOC(sizeof(*audio), "audio");
        if (macontext.Info.ChannelName)
            isyslog("channel %s", macontext.Info.ChannelName);
        if (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H264)
            macontext.Video.Options.IgnoreAspectRatio = true;
    }

    framecnt1 = 0;
    framecnt2 = 0;
    framecnt3 = 0;
    framecnt4 = 0;
    lastiframe = 0;
    iframe = 0;
    chkSTART = chkSTOP = INT_MAX;
}


cMarkAdStandalone::~cMarkAdStandalone() {
    marks.Save(directory, &macontext, isTS, true);
    if ((!abortNow) && (!duplicate)) {
        LogSeparator();
        dsyslog("time for decoding:              %2ds %3dms", decodeTime_us / 1000000, (decodeTime_us % 1000000) / 1000);
        if (logoSearchTime_ms > 0) dsyslog("time to find logo in recording: %2ds %3dms", logoSearchTime_ms / 1000, logoSearchTime_ms % 1000);
        if (logoChangeTime_ms > 0) dsyslog("time to find logo changes:      %2ds %3dms", logoChangeTime_ms / 1000, logoChangeTime_ms % 1000);

        time_t sec = endPass1.tv_sec - startPass1.tv_sec;
        suseconds_t usec = endPass1.tv_usec - startPass1.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) isyslog("pass 1: time %3lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt1, framecnt1 / (sec + usec / 1000000));


        sec = endPass2.tv_sec - startPass2.tv_sec;
        usec = endPass2.tv_usec - startPass2.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) isyslog("pass 2: time %3lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt2, framecnt2 / (sec + usec / 1000000));

        sec = endPass3.tv_sec - startPass3.tv_sec;
        usec = endPass3.tv_usec - startPass3.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) isyslog("pass 3: time %3lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt3, framecnt3 / (sec + usec / 1000000));

        sec = endPass4.tv_sec - startPass4.tv_sec;
        usec = endPass4.tv_usec - startPass4.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        if ((sec + usec / 1000000) > 0) isyslog("pass 4: time %3lds %03ldms, frames %6i, fps %6ld", sec, usec / 1000, framecnt4, framecnt4 / (sec + usec / 1000000));

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
        if (etime > 0) ftime = (framecnt1 + framecnt2 + framecnt3) / etime;
        isyslog("processed time %d:%02d min with %.1f fps", static_cast<int> (etime / 60), static_cast<int> (etime - (static_cast<int> (etime / 60) * 60)), ftime);
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
           "-I              --saveinfo\n"
           "                  correct information in info file\n"
           "-L              --extractlogo=<direction>[,width[,height]]\n"
           "                  extracts logo to /tmp as pgm files (must be renamed)\n"
           "                  <direction>  0 = top left,    1 = top right\n"
           "                               2 = bottom left, 3 = bottom right\n"
           "                  [width]  range from 50 to %3i, default %3i (SD)\n"
           "                                                 default %3i (HD)\n"
           "                  [height] range from 20 to %3i, default %3i (SD)\n"
           "                                                 default %3i (HD)\n"
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
           "                  cut vidio based on marks and write it in the recording directory\n"
           "                --ac3reencode\n"
           "                  re-encode AC3 stream to fix low audio level of cutted video on same devices\n"
           "                  requires --cut\n"
           "                --autologo=<option>\n"
           "                  <option>   0 = disable, only use logos from logo cache directory (default)\n"
           "                             1 = enable, find logo from recording and store it in the recording directory\n"
           "                                 memory usage optimized operation mode, but runs slow\n"
           "                             2 = enable, find logo from recording and store it in the recording directory\n"
           "                                 speed optimized operation mode, but needs a lot of memonry, use it only > 1 GB memory\n"
           "\ncmd: one of\n"
           "-                            dummy-parameter if called directly\n"
           "nice                         runs markad directly and with nice(19)\n"
           "after                        markad started by vdr after the recording is complete\n"
           "before                       markad started by vdr before the recording is complete, only valid together with --online\n"
           "edited                       markad started by vdr in edit function and exits immediately\n"
           "\n<record>                     is the name of the directory where the recording\n"
           "                             is stored\n\n",
           LOGO_MAXWIDTH,LOGO_DEFWIDTH,LOGO_DEFHDWIDTH,
           LOGO_MAXHEIGHT,LOGO_DEFHEIGHT,LOGO_DEFHDHEIGHT,svdrpport
          );
    return -1;
}


static void signal_handler(int sig) {
    void *trace[32];
    char **messages = (char **)NULL;
    int i, trace_size = 0;

    switch (sig) {
        case SIGTSTP:
            isyslog("paused by signal");
            kill(getpid(), SIGSTOP);
            break;
        case SIGCONT:
            isyslog("continued by signal");
            break;
        case SIGABRT:
            esyslog("aborted by signal");
            abortNow = true;;
            break;
        case SIGSEGV:
            esyslog("segmentation fault");

            trace_size = backtrace(trace, 32);
            messages = backtrace_symbols(trace, trace_size);
            esyslog("[bt] Execution path:");
            for (i=0; i < trace_size; ++i) {
                esyslog("[bt] %s", messages[i]);
            }
            _exit(1);
            break;
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
    struct config config = {};

    gettimeofday(&startAll, NULL);

    // set defaults
    config.DecodeVideo = true;
    config.DecodeAudio = true;
    config.SaveInfo = false;
    config.logoExtraction = -1;
    config.logoWidth = -1;
    config.logoHeight = -1;
    config.threads = -1;
    config.astopoffs = 0;
    config.posttimer = 600;
    strcpy(config.svdrphost, "127.0.0.1");
    strcpy(config.logoDirectory, "/var/lib/markad");

    struct servent *serv=getservbyname("svdrp", "tcp");
    if (serv) {
        config.svdrpport = htons(serv->s_port);
    }
    else {
        config.svdrpport = 2001;
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
            {"autologo",1,0,14},
            {"vps",0,0,15},
            {"logfile",1,0,16},

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
                        config.DecodeVideo = false;
                        break;
                    case 2:
                        config.DecodeAudio = false;
                        break;
                    case 3:
                        config.DecodeVideo = false;
                        config.DecodeAudio = false;
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
                strncpy(config.logoDirectory, optarg, sizeof(config.logoDirectory));
                config.logoDirectory[sizeof(config.logoDirectory) - 1]=0;
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
                config.BackupMarks = true;
                break;
            case 'I':
                config.SaveInfo = true;
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
                            if ((config.logoWidth < 50) || (config.logoWidth > LOGO_MAXWIDTH)) {
                                fprintf(stderr, "markad: invalid width value: %s\n", tok);
                                return 2;
                            }
                            break;
                        case 2:
                            config.logoHeight = atoi(tok);
                            if ((config.logoHeight < 20) || (config.logoHeight > LOGO_MAXHEIGHT)) {
                                fprintf(stderr, "markad: invalid height value: %s\n", tok);
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
            case 'O':
                // --OSD
                config.OSD = true;
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
                strncpy(config.markFileName, optarg, sizeof(config.markFileName));
                config.markFileName[sizeof(config.markFileName) - 1] = 0;
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
                config.NoPid = true;
                break;
            case 5: // --svdrphost
                strncpy(config.svdrphost, optarg, sizeof(config.svdrphost));
                config.svdrphost[sizeof(config.svdrphost) - 1] = 0;
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
            case 14: // --autoLogo
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 2) config.autoLogo = atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid autologo value: %s\n", optarg);
                    return 2;
                }
                break;
            case 15: // --vps
                config.useVPS = true;
                break;
            case 16: // --logfile
                strncpy(config.logFile, optarg, sizeof(config.logFile));
                config.logFile[sizeof(config.logFile) - 1] = 0;
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
                config.Before = bFork = bNice = SYSLOG = true;
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
    if ((config.Before) && (config.online == 1) && recDir && (!strchr(recDir, '@'))) return 0;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || config.Before || bAfter || bNice) && recDir ) {
        // if bFork is given go in background
        if ( bFork ) {
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
        if (! IOPrio) {
            fprintf(stderr,"failed to get ioprio\n");
        }
        IOPrio = IOPrio >> 13;

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
        signal(SIGUSR1, signal_handler);
        signal(SIGTSTP, signal_handler);
        signal(SIGCONT, signal_handler);

        cIndex *recordingIndex = new cIndex();
        ALLOC(sizeof(*recordingIndex), "recordingIndex");

        cmasta = new cMarkAdStandalone(recDir,&config, recordingIndex);
        ALLOC(sizeof(*cmasta), "cmasta");
        if (!cmasta) return -1;

        isyslog("parameter --loglevel is set to %i", SysLogLevel);

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


        if (!bPass2Only) {
            gettimeofday(&startPass1, NULL);
            cmasta->Process_cDecoder();
            gettimeofday(&endPass1, NULL);
        }

        if (!bPass1Only) {
            gettimeofday(&startPass2, NULL);
            cmasta->Process2ndPass();  // overlap detection
            gettimeofday(&endPass2, NULL);

            gettimeofday(&startPass3, NULL);
            cmasta->Process3ndPass();  // Audio silence detection
            gettimeofday(&endPass3, NULL);
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


// evaluate logo stop/start pairs
// used by logo change detection
cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(clMarks *marks, const int framesPerSecond, const int iStart, const int iStopA) {
    if (!marks) return;

#define LOGO_CHANGE_NEXT_STOP_MIN   7  // in s, do not increase, 7s is the shortest found distance between two logo changes
                                       // next stop max (=lenght next valid broadcast) found: 1242
#define LOGO_CHANGE_STOP_START_MIN 11  // in s, changed from 12 to 11
#define LOGO_CHANGE_STOP_START_MAX 21  // in s
#define LOGO_CHANGE_IS_ADVERTISING_MIN 300  // in s
#define LOGO_CHANGE_IS_BROADCAST_MIN 240  // in s

    logoStopStartPair newPair;
    clMark *mark = marks->GetFirst();

    while (mark) {
        if (mark->type == MT_LOGOSTOP) newPair.stopPosition = mark->position;
        if ((mark->type == MT_LOGOSTART) && (newPair.stopPosition >= 0)) {
            newPair.startPosition = mark->position;
            logoPairVector.push_back(newPair);
            ALLOC(sizeof(logoStopStartPair), "logoPairVector");
            // reset for next pair
            newPair.stopPosition = -1;
            newPair.startPosition = -1;
        }
        mark=mark->Next();
    }

// evaluate stop/start pairs
    for (std::vector<logoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        // mark after pair
        clMark *markStop_AfterPair = marks->GetNext(logoPairIterator->stopPosition, MT_LOGOSTOP);

        // check length of stop/start logo pair
        int deltaStopStart = (logoPairIterator->startPosition - logoPairIterator->stopPosition ) / framesPerSecond;
        if (deltaStopStart < LOGO_CHANGE_STOP_START_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: delta too small %ds (expect >=%ds)", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_STOP_START_MIN);
            logoPairIterator->isLogoChange = -1;
        }
        if (deltaStopStart > LOGO_CHANGE_STOP_START_MAX) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: delta too big %ds (expect <=%ds)", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_STOP_START_MAX);
            logoPairIterator->isLogoChange = -1;
        }
        if (deltaStopStart >= LOGO_CHANGE_IS_ADVERTISING_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: delta %ds (expect >=%ds) is a advertising", logoPairIterator->stopPosition, logoPairIterator->startPosition, deltaStopStart, LOGO_CHANGE_IS_ADVERTISING_MIN);
            logoPairIterator->isAdvertising = 1;
        }

        // check next stop distance after stop/start pair
        int delta_Stop_AfterPair = 0;
        if (markStop_AfterPair) {  // we have a next logo stop
            delta_Stop_AfterPair = (markStop_AfterPair->position - logoPairIterator->startPosition) / framesPerSecond;
        }
        else {  // this is the last logo stop we have
            if (iStart > 0) { // we were called by CheckStart, the next stop is not yet detected
                              // we can not ignore early stop start pairs because they can be logo changed short after start
                delta_Stop_AfterPair = LOGO_CHANGE_NEXT_STOP_MIN;
            }
            else { // we are called by CheckStop()
                if (logoPairIterator->stopPosition < iStopA) logoPairIterator->isLogoChange = -1; // this is the last stop mark and it is before assumed end mark, this is the end mark
            }
        }
        if ((delta_Stop_AfterPair > 0) && (delta_Stop_AfterPair < LOGO_CHANGE_NEXT_STOP_MIN)) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: stop mark after stop/start pair too fast %ds (expect >=%ds)", logoPairIterator->stopPosition, logoPairIterator->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_NEXT_STOP_MIN);
            logoPairIterator->isLogoChange = -1;
        }
        if (delta_Stop_AfterPair >= LOGO_CHANGE_IS_BROADCAST_MIN) {
            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: next stop mark after stop/start pair in %ds (expect >=%ds, start mark is in braoscast)", logoPairIterator->stopPosition, logoPairIterator->startPosition, delta_Stop_AfterPair, LOGO_CHANGE_IS_BROADCAST_MIN);
            logoPairIterator->isStartMarkInBroadcast = 1;
        }
    }

    // check sequenz of stop/start pairs
    // search for part between advertising and broadcast, keep this mark, because it contains the start mark of the broadcast
    //
    for (std::vector<logoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->isAdvertising == 1) {  // advertising pair
            std::vector<logoStopStartPair>::iterator nextLogoPairIterator = logoPairIterator;
            ++nextLogoPairIterator;
            if (nextLogoPairIterator != logoPairVector.end()) {
                if ((nextLogoPairIterator->isLogoChange == 0) && (nextLogoPairIterator->isStartMarkInBroadcast  == 0)){ // unknown pair
                    std::vector<logoStopStartPair>::iterator next2LogoPairIterator = nextLogoPairIterator;
                    ++next2LogoPairIterator;
                    if (next2LogoPairIterator != logoPairVector.end()) {
                        if (next2LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                            dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", nextLogoPairIterator->stopPosition, nextLogoPairIterator->startPosition);
                            nextLogoPairIterator->isLogoChange = -1;
                        }
                        if ((next2LogoPairIterator->isLogoChange == 0) && (next2LogoPairIterator->isStartMarkInBroadcast  == 0)) { // unknown pair
                            std::vector<logoStopStartPair>::iterator next3LogoPairIterator = next2LogoPairIterator;
                            ++next3LogoPairIterator;
                            if (next3LogoPairIterator != logoPairVector.end()) {
                                if (next3LogoPairIterator->isStartMarkInBroadcast  == 1) { // pair with bradcast start mark
                                    dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): ----- stop (%d) start (%d) pair: part between advertising and broadcast, keep this mark because it contains start mark of broadcast)", next2LogoPairIterator->stopPosition, next2LogoPairIterator->startPosition);
                                    next2LogoPairIterator->isLogoChange = -1;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    for (std::vector<logoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair(): add stop (%d) start (%d) pair:", logoPairIterator->stopPosition, logoPairIterator->startPosition);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isLogoChange           %2d", logoPairIterator->isLogoChange);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isAdvertising          %2d", logoPairIterator->isAdvertising);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isStartMarkInBroadcast %2d", logoPairIterator->isStartMarkInBroadcast);
        dsyslog("cEvaluateLogoStopStartPair::cEvaluateLogoStopStartPair():                  isClosingCredits       %2d", logoPairIterator->isClosingCredits);
    }
    nextLogoPairIterator = logoPairVector.begin();
}


cEvaluateLogoStopStartPair::~cEvaluateLogoStopStartPair() {
#ifdef DEBUG_MEM
    int size =  logoPairVector.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(logoStopStartPair), "logoPairVector");
    }
#endif
     logoPairVector.clear();
}


bool cEvaluateLogoStopStartPair::GetNextPair(int *stopPosition, int *startPosition) {
    if (!stopPosition) return false;
    if (!startPosition) return false;
    if (nextLogoPairIterator == logoPairVector.end()) return false;

    while (nextLogoPairIterator->isLogoChange == -1) {
        ++nextLogoPairIterator;
        if (nextLogoPairIterator == logoPairVector.end()) return false;
    }
    *stopPosition = nextLogoPairIterator->stopPosition;
    *startPosition = nextLogoPairIterator->startPosition;
    ++nextLogoPairIterator;
    return true;
}


void cEvaluateLogoStopStartPair::SetClosingCredits(const int stopPosition, const int isClosingCredits) {
    for (std::vector<logoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->stopPosition == stopPosition) {
            logoPairIterator->isClosingCredits = isClosingCredits;
            dsyslog("cEvaluateLogoStopStartPair::SetClosingCredits(): mark pair stop (%d) start (%d) set to isClosingCredits to: %d", stopPosition, logoPairIterator->startPosition, isClosingCredits);
            return;
        }
    }
}


int cEvaluateLogoStopStartPair::GetLastClosingCreditsStart() {
    int lastClosingCreditsStart = -1;
    for (std::vector<logoStopStartPair>::iterator logoPairIterator = logoPairVector.begin(); logoPairIterator != logoPairVector.end(); ++logoPairIterator) {
        if (logoPairIterator->isClosingCredits == 1) {
            lastClosingCreditsStart = logoPairIterator->stopPosition;  // stop position of pair is start position of closing credits
        }
    }
    return lastClosingCreditsStart;
}
