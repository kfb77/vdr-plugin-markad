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

bool SYSLOG = false;
bool LOG2REC = false;
cDecoder* ptr_cDecoder = NULL;
cExtractLogo* ptr_cExtractLogo = NULL;
cMarkAdStandalone *cmasta = NULL;
bool restartLogoDetectionDone = false;
int SysLogLevel = 2;
bool abortNow = false;


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
        snprintf(fmt, sizeof(fmt), "%s%s [%d] %s %s", LOG2REC ? "":"markad: ",buf, getpid(), prioText, format);
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
    memcpy(&name.sin_addr.s_addr,host->h_addr, host->h_length);
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    if (connect(sock, (struct sockaddr *)&name, size) !=0 ) {
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

    if (pthread_create(&tid,NULL,(void *(*) (void *))&send, (void *) this) !=0 ) return -1;
    return 0;
}


void cMarkAdStandalone::CalculateCheckPositions(int startframe) {
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): startframe %i",startframe);

    if (!length) {
        esyslog("length of recording not found");
        return;
    }
    if (!macontext.Video.Info.FramesPerSecond) {
        esyslog("video frame rate of recording not found");
        return;
    }

    if (startframe < 0) {   // recodring start ist too late
        isyslog("recording started too late, set start mark to start of recording");
        MarkAdMark mark = {};
        mark.Position = 1;  // do not use position 0 because this will later be deleted
        mark.Type = MT_RECORDINGSTART;
        AddMark(&mark);
        startframe = macontext.Video.Info.FramesPerSecond * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): use frame rate %i", (int) macontext.Video.Info.FramesPerSecond);

    iStart = -startframe;
    iStop = -(startframe + macontext.Video.Info.FramesPerSecond * length) ;   // iStop change from - to + when frames reached iStop

    iStartA = abs(iStart);
    iStopA = startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->astopoffs - 30);
    chkSTART = iStartA + macontext.Video.Info.FramesPerSecond * 4 * MAXRANGE; //  fit for later broadcast start
    chkSTOP = startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): length of recording %is", length);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed start frame %i", iStartA);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): assumed stop frame %i", iStopA);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTART set to %i", chkSTART);
    dsyslog("cMarkAdStandalone::CalculateCheckPositions(): chkSTOP set to %i", chkSTOP);
}


void cMarkAdStandalone::CheckStop() {
    LogSeparator();
    dsyslog("checking stop (%i)", lastiframe);
    dsyslog("assumed stop frame %i", iStopA);

    DebugMarks();     //  only for debugging

    int delta = macontext.Video.Info.FramesPerSecond * MAXRANGE;

    clMark *end = marks.GetAround(3*delta, iStopA, MT_CHANNELSTOP);      // try if we can get a good stop mark, start with MT_ASPECTSTOP
    if (!end) {
        dsyslog("no MT_CHANNELSTOP mark found");
        end = marks.GetAround(3*delta, iStopA, MT_ASPECTSTOP);      // try MT_ASPECTSTOP
        if (!end) {
            dsyslog("no MT_ASPECTSTOP mark found");
            end = marks.GetAround(3*delta, iStopA, MT_HBORDERSTOP);         // try MT_HBORDERSTOP
            if (!end) {
                dsyslog("no MT_HBORDERSTOP mark found");
                end = marks.GetAround(3*delta, iStopA, MT_VBORDERSTOP);         // try MT_VBORDERSTOP
                if (!end) {
                    dsyslog("no MT_VBORDERSTOP mark found");
                    end = marks.GetAround(3*delta, iStopA, MT_LOGOSTOP);        // try MT_LOGOSTOP
                    if (!end) {
                        dsyslog("no MT_LOGOSTOP mark found");
                        end = marks.GetAround(3*delta, iStopA, MT_STOP, 0x0F);    // try any type of stop mark
                    }
                    else dsyslog("MT_LOGOSTOP found at frame %i", end->position);
                }
                else dsyslog("MT_VBORDERSTOP found at frame %i", end->position);
            }
            else dsyslog("MT_HBORDERSTOP found at frame %i", end->position);
        }
        else dsyslog("MT_ASPECTSTOP found at frame %i", end->position);
    }
    else dsyslog("MT_CHANNELSTOP found at frame %i", end->position);

    clMark *lastStart = marks.GetAround(INT_MAX, lastiframe, MT_START, 0x0F);
    if (end) {
        dsyslog("found end mark at (%i)", end->position);
        clMark *mark = marks.GetFirst();
        while (mark) {
            if ((mark->position >= iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) && (mark->position < end->position) && ((mark->type & 0xF0) < (end->type & 0xF0))) { // delete all weak marks
                dsyslog("found stronger end mark delete mark (%i)", mark->position);
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
               dsyslog("stop mark is week, use next stop mark at (%i)", end2->position);
               end = end2;
           }
        }

        char *indexToHMSF = marks.IndexToHMSF(end->position, &macontext, ptr_cDecoder);
        if (indexToHMSF) {
            isyslog("using mark on position (%i) type 0x%X at %s as stop mark", end->position,  end->type, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        marks.DelTill(end->position,false);

        if ( end->position < iStopA - 3*delta ) {    // last found stop mark too early, adding STOP mark at the end
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
    else {
        dsyslog("no stop mark found, add stop mark at the last frame (%i)",lastiframe);
        MarkAdMark mark = {};
        mark.Position = lastiframe;  // we are lost, add a end mark at the last iframe
        mark.Type = MT_ASSUMEDSTOP;
        AddMark(&mark);
    }
    iStop = iStopA = 0;
    gotendmark = true;

    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::CheckStart() {
    LogSeparator();
    dsyslog("cMarkAdStandalone::CheckStart(); checking start at frame (%i)", lastiframe);
    dsyslog("cMarkAdStandalone::CheckStart(): assumed start frame %i", iStartA);
    DebugMarks();     //  only for debugging

    clMark *begin = NULL;
    int delta = macontext.Video.Info.FramesPerSecond * MAXRANGE;
    macontext.Video.Options.IgnoreBlackScreenDetection = true;   // use black sceen setection only to find start mark

// try to find a audio channel mark
    for (short int stream = 0; stream < MAXSTREAMS; stream++) {
        if ((macontext.Info.Channels[stream]) && (macontext.Audio.Info.Channels[stream]) && (macontext.Info.Channels[stream] != macontext.Audio.Info.Channels[stream])) {
            char as[20];
            switch (macontext.Info.Channels[stream]) {
                case 1:
                    strcpy(as,"mono");
                    break;
                case 2:
                    strcpy(as,"stereo");
                    break;
                case 6:
                    strcpy(as,"dd5.1");
                    break;
                default:
                    strcpy(as,"??");
                    break;
            }
            char ad[20];
            switch (macontext.Audio.Info.Channels[stream]) {
                case 1:
                    strcpy(ad,"mono");
                    break;
                case 2:
                    strcpy(ad,"stereo");
                    break;
                case 6:
                    strcpy(ad,"dd5.1");
                    break;
                default:
                    strcpy(ad,"??");
                break;
            }
            isyslog("audio description in info (%s) wrong, we have %s",as,ad);
        }
        macontext.Info.Channels[stream] = macontext.Audio.Info.Channels[stream];

        if ((macontext.Config->DecodeAudio) && (macontext.Info.Channels[stream])) {
            if ((macontext.Info.Channels[stream] == 6) && (macontext.Audio.Options.IgnoreDolbyDetection == false)) {
                if (macontext.Audio.Info.channelChange) {
                    isyslog("DolbyDigital5.1 audio whith 6 Channels in stream %i detected. disable logo/border/aspect detection", stream);
                    bDecodeVideo = false;
                    macontext.Video.Options.IgnoreAspectRatio = true;
                    macontext.Video.Options.IgnoreLogoDetection = true;
                    marks.Del(MT_ASPECTSTART);
                    marks.Del(MT_ASPECTSTOP);
                    // start mark must be around iStartA
                    begin=marks.GetAround(delta*3, iStartA, MT_CHANNELSTART);  // decrease from 4
                    if (!begin) {          // previous recording had also 6 channels, try other marks
                        dsyslog("cMarkAdStandalone::CheckStart(): no audio channel start mark found");
                    }
                    else {
                        dsyslog("cMarkAdStandalone::CheckStart(): audio channel start mark found at %d", begin->position);
                        marks.Del(MT_LOGOSTART);   // we do not need the weaker marks if we found a MT_CHANNELSTART
                        marks.Del(MT_LOGOSTOP);
                        marks.Del(MT_HBORDERSTART);
                        marks.Del(MT_HBORDERSTOP);
                    }
                }
                else dsyslog("cMarkAdStandalone::CheckStart(): no audio channel change found till now, do not disable logo/border/aspect detection");
            }
            else {
                if (macontext.Audio.Options.IgnoreDolbyDetection == true) isyslog("disabling AC3 decoding (from logo)");
                if ((macontext.Info.Channels[stream]) && (macontext.Audio.Options.IgnoreDolbyDetection == false))
                    isyslog("broadcast with %i audio channels of stream %i, disabling AC3 decoding",macontext.Info.Channels[stream], stream);
                if (inBroadCast) {  // if we have channel marks but we are now with 2 channels inBroascast, delete these
                    macontext.Video.Options.IgnoreAspectRatio = false;   // then we have to find other marks
                    macontext.Video.Options.IgnoreLogoDetection = false;
                }
#if defined CLASSIC_DECODER
                if (macontext.Info.DPid.Num) {
                    macontext.Info.DPid.Num = 0;
                    demux->DisableDPid();
                }
#endif
            }
        }
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

// try to find a ascpect ratio mark
    if (!begin) {
        clMark *aStart = marks.GetAround(chkSTART, chkSTART+1, MT_ASPECTSTART);   // check if ascpect ratio changed in start part
        clMark *aStop = NULL;
        if (aStart) aStop = marks.GetNext(aStart->position,MT_ASPECTSTOP);
        bool earlyAspectChange = false;
        if (aStart && aStop && (aStop->position > aStart->position)) {  // we are in the first ad, do not correct aspect ratio from info file
            dsyslog("cMarkAdStandalone::CheckStart(): found very early aspect ratio change at (%i) and (%i)", aStart->position,  aStop->position);
            earlyAspectChange = true;
        }

        if ((macontext.Info.AspectRatio.Num) && (! earlyAspectChange) &&
           ((macontext.Info.AspectRatio.Num != macontext.Video.Info.AspectRatio.Num) || (macontext.Info.AspectRatio.Den != macontext.Video.Info.AspectRatio.Den)))
        {
            isyslog("video aspect description in info (%i:%i) wrong", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
            macontext.Info.AspectRatio.Num = macontext.Video.Info.AspectRatio.Num;
            macontext.Info.AspectRatio.Den = macontext.Video.Info.AspectRatio.Den;
        }
        if ((macontext.Info.AspectRatio.Num == 0) || (macontext.Video.Info.AspectRatio.Den == 0)) {
            isyslog("no video aspect ratio found in info file");
            macontext.Info.AspectRatio.Num = macontext.Video.Info.AspectRatio.Num;
            macontext.Info.AspectRatio.Den = macontext.Video.Info.AspectRatio.Den;
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
#if defined CLASSIC_DECODER
                for (short int stream = 0;stream < MAXSTREAMS; stream++) {
                    if (macontext.Info.Channels[stream] == 6) {
                        macontext.Info.DPid.Num = 0;
                        demux->DisableDPid();
                    }
                }
#endif
                macontext.Video.Options.IgnoreLogoDetection = true;
                marks.Del(MT_CHANNELSTART);
                marks.Del(MT_CHANNELSTOP);
                // start mark must be around iStartA
                begin = marks.GetAround(delta*4, iStartA, MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): MT_ASPECTSTART found at (%i)",begin->position);
                    if (begin->position > abs(iStartA) / 4) {    // this is a valid start
                        marks.Del(MT_LOGOSTART);  // we found MT_ASPECTSTART, we do not need LOGOSTART
                        marks.Del(MT_LOGOSTOP);
                   }
                   else { // if there is a MT_ASPECTSTOP, delete all marks after this position
                       clMark *aStopNext = marks.GetNext(begin->position, MT_ASPECTSTOP);
                       if (aStopNext) {
                           dsyslog("cMarkAdStandalone::CheckStart(): found MT_ASPECTSTOP (%i), delete all weaker marks after", aStopNext->position);
                           marks.DelWeakFromTo(aStopNext->position, INT_MAX, aStopNext->type);
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
                begin=marks.GetAround(delta*3,iStartA,MT_ASPECTSTART);
                if (begin) {
                    dsyslog("cMarkAdStandalone::CheckStart(): use MT_ASPECTSTART found at (%i) because previous recording was 4:3",begin->position);
                    clMark *begin2=marks.GetAround(delta*4,begin->position,MT_LOGOSTART);  // do not use this mark if there is a later logo start mark
                    if (begin2 && begin2->position >  begin->position) {
                        dsyslog("cMarkAdStandalone::CheckStart(): found later MT_LOGOSTART, do not use MT_ASPECTSTART");
                        begin=NULL;
                    }
                }
            }
        }
        macontext.Info.checkedAspectRatio = true;
    }

// try to find a horizontal border mark
    if (!begin) {
        clMark *hStart=marks.GetAround(iStartA+delta,iStartA+delta+1,MT_HBORDERSTART);
        if (!hStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no horizontal border at start found, ignore horizontal border detection");
            macontext.Video.Options.ignoreHborder=true;
            clMark *hStop=marks.GetAround(iStartA+delta,iStartA+delta,MT_HBORDERSTOP);
            if (hStop) {
                int pos = hStop->position;
                char *comment=NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): horizontal border stop without start mark found (%i), assume as start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from horizontal border stop (%d)", pos) == -1) comment=NULL;
                ALLOC(strlen(comment)+1, "comment");
                begin=marks.Add(MT_ASSUMEDSTART, pos, comment);
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): horizontal border start found at (%i)", hStart->position);
            clMark *hStop=marks.GetAround(delta,hStart->position,MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
            if ( (hStop) && (hStop->position > hStart->position)) {
                isyslog("horizontal border STOP (%i) short after horizontal border START (%i) found, this is not valid, delete marks",hStop->position,hStart->position);
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
        clMark *vStart=marks.GetAround(iStartA+delta,iStartA+delta+1,MT_VBORDERSTART);
        if (!vStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no vertical border at start found, ignore vertical border detection");
            macontext.Video.Options.ignoreVborder=true;
            clMark *vStop=marks.GetAround(iStartA+delta,iStartA+delta,MT_VBORDERSTOP);
            if (vStop) {
                int pos = vStop->position;
                char *comment=NULL;
                dsyslog("cMarkAdStandalone::CheckStart(): vertical border stop without start mark found (%i), possible start mark of the following recording", pos);
                marks.Del(pos);
                if (asprintf(&comment,"assumed start from vertical border stop (%d)", pos) == -1) comment=NULL;
                ALLOC(strlen(comment)+1, "comment");
                marks.Add(MT_ASSUMEDSTART, pos, comment);
                FREE(strlen(comment)+1, "comment");
                free(comment);
            }
        }
        else {
            dsyslog("cMarkAdStandalone::CheckStart(): vertical border start found at (%i)", vStart->position);
            clMark *vStop=marks.GetAround(delta,vStart->position,MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
            if ( (vStop) && (vStop->position > vStart->position)) {
                isyslog("vertical border STOP (%i) short after vertical border START (%i) found, this is not valid, delete marks",vStop->position,vStart->position);
                marks.Del(vStart);
                marks.Del(vStop);

            }
            else {
                if (vStart->position != 0) {  // position 0 is a vborder previous recording
                    dsyslog("cMarkAdStandalone::CheckStart(): delete HBORDER marks if any");
                    marks.Del(MT_HBORDERSTART);
                    marks.Del(MT_HBORDERSTOP);
                    begin = vStart;   // found valid vertical border start mark
                    macontext.Video.Options.ignoreHborder = true;
                }
            }
        }
    }

// try to find a logo mark
    if (!begin) {
        clMark *lStart=marks.GetAround(iStartA+delta,iStartA,MT_LOGOSTART);
        if (!lStart) {
            dsyslog("cMarkAdStandalone::CheckStart(): no logo start mark found");
        }
        else {
            char *indexToHMSF = marks.IndexToHMSF(lStart->position,&macontext, ptr_cDecoder);
            if (indexToHMSF) {
                dsyslog("cMarkAdStandalone::CheckStart(): logo start mark found on position (%i) at %s", lStart->position, indexToHMSF);
                FREE(strlen(indexToHMSF)+1, "indexToHMSF");
                free(indexToHMSF);
            }
            begin=lStart;   // found valid logo start mark
        }
    }

    if (begin && ((begin->position == 0) || ((begin->type == MT_LOGOSTART) && (begin->position  < iStart/8)))) { // we found the correct type but the mark is too early because the previous recording has same type
        dsyslog("cMarkAdStandalone::CheckStart(): start mark (%i) dropped because it is too early", begin->position);
        begin = NULL;
    }

    if (!begin) {    // try anything
        marks.DelTill(1);    // we do not want to have a start mark at position 0
        begin=marks.GetAround(iStartA+2*delta,iStartA,MT_START,0x0F);
        if (begin) {
            dsyslog("cMarkAdStandalone::CheckStart(): found start mark at (%i)", begin->position);
        }
    }

    clMark *beginRec=marks.GetAround(delta,1,MT_RECORDINGSTART);  // do we have an incomplete recording ?
    if (beginRec) {
        dsyslog("cMarkAdStandalone::CheckStart(): found MT_RECORDINGSTART at %i, replace start mark", beginRec->position);
        begin = beginRec;
    }

    if (begin) {
        marks.DelTill(begin->position);    // delete all marks till start mark
        CalculateCheckPositions(begin->position);
        char *indexToHMSF = marks.IndexToHMSF(begin->position,&macontext, ptr_cDecoder);
        if (indexToHMSF) {
            isyslog("using mark on position %i type 0x%X at %s as start mark", begin->position, begin->type, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }


        if ((begin->type==MT_VBORDERSTART) || (begin->type==MT_HBORDERSTART)) {
            isyslog("found %s borders, logo detection disabled",(begin->type==MT_HBORDERSTART) ? "horizontal" : "vertical");
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

        clMark *mark=marks.GetFirst();   // delete all black screen marks because they are weak, execpt the start mark
        while (mark) {
            if (( (mark->type == MT_NOBLACKSTART) || (mark->type == MT_NOBLACKSTOP) ) && (mark->position > begin->position) ) {
                dsyslog("cMarkAdStandalone::CheckStart(): delete black screen mark at position (%i)", mark->position);
                clMark *tmp=mark;
                mark=mark->Next();
                marks.Del(tmp);
                continue;
            }
            mark=mark->Next();
        }
    }
    else {
        //fallback
        dsyslog("cMarkAdStandalone::CheckStart(): no valid start mark found, assume start time at pre recording time");
        marks.DelTill(iStart);
        marks.Del(MT_NOBLACKSTART);  // delete all black screen marks
        marks.Del(MT_NOBLACKSTOP);
        MarkAdMark mark={};
        mark.Position=iStart;
        mark.Type=MT_ASSUMEDSTART;
        AddMark(&mark);
        CalculateCheckPositions(iStart);
    }
    iStart=0;
    if (macontext.Config->Before) marks.Save(directory,&macontext, ptr_cDecoder, isTS, true);
    DebugMarks();     //  only for debugging
    LogSeparator();
    return;
}


void cMarkAdStandalone::LogSeparator() {
    dsyslog("------------------------------------------------------------------------------------------------");
}


void cMarkAdStandalone::DebugMarks() {           // write all marks to log file
    clMark *mark = marks.GetFirst();
    while (mark) {
        char *indexToHMSF = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (indexToHMSF) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, indexToHMSF);
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark=mark->Next();
    }
}


void cMarkAdStandalone::CheckMarks() {           // cleanup marks that make no sense
    LogSeparator();
    dsyslog(" cMarkAdStandalone::CheckMarks(): check marks");
    DebugMarks();     //  only for debugging

    clMark *mark=marks.GetFirst();
    while (mark) {
        if (((mark->type & 0x0F) == MT_STOP) && (mark == marks.GetFirst())){
            dsyslog("Start with STOP mark, delete first mark");
            clMark *tmp=mark;
            mark=mark->Next();
            marks.Del(tmp);
            continue;
        }
        if (((mark->type & 0x0F)==MT_START) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_START)) {  // two start marks, delete second
            dsyslog("start mark (%i) folowed by start mark (%i) delete second", mark->position, mark->Next()->position);
            marks.Del(mark->Next());
            continue;
        }
        if (((mark->type & 0x0F)==MT_STOP) && (mark->Next()) && ((mark->Next()->type & 0x0F)==MT_STOP)) {  // two stop marks, delete second
            dsyslog("stop mark (%i) folowed by stop mark (%i) delete first", mark->position, mark->Next()->position);
            clMark *tmp=mark;
            mark=mark->Next();
            marks.Del(tmp);
            continue;
        }

        if ((mark->type==MT_NOBLACKSTOP) && mark->Next() && (mark->Next()->type==MT_NOBLACKSTART)) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*4);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF) {
                double distance=(mark->Next()->position-mark->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance of black screen between STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp=mark;
                mark=marks.GetFirst();    // restart check from start
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }

        if ((mark->type==MT_NOBLACKSTOP) && mark->Next() && (mark->Next()->type==MT_NOBLACKSTART)) {
            if ((mark->Next()->position>iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) && (mark->position>iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE)) {
                isyslog("blackscreen start mark followed by blackscreen stop mark, deleting %i,%i", mark->position, mark->Next()->position);
                clMark *tmp=mark;
                mark=mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }

        if ((mark->type==MT_LOGOSTART) && mark->Next() && mark->Next()->type==MT_LOGOSTOP) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*60);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF) {
                double distance=(mark->Next()->position-mark->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp=mark;
                mark=mark->Next()->Next();
                marks.Del(tmp->Next());
                if (marks.GetFirst()->position == tmp->position) {
                    dsyslog("cMarkAdStandalone::CheckMarks(): mark on position (%i) not deleted because this is the start mark", tmp->position);
                    mark=marks.GetFirst(); // do not delete start mark, restart check from first mark
                }
                else marks.Del(tmp);
                continue;
            }
        }

        if ((mark->type == MT_LOGOSTOP) && mark->Next() && mark->Next()->type == MT_LOGOSTART) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond * 38);
            if (abs(mark->Next()->position - mark->position) <= MARKDIFF) {
                double distance=(mark->Next()->position - mark->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between logo STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp = mark;
                mark = marks.GetFirst();    // restart check from start
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }

        if (!inBroadCast || gotendmark) {  // in this case we will add a stop mark at the end of the recording
            if (((mark->type & 0x0F)==MT_START) && (!mark->Next())) {      // delete start mark at the end
                if (marks.GetFirst()->position != mark->position) {        // do not delete start mark
                    dsyslog("START mark at the end, deleting %i", mark->position);
                    marks.Del(mark);
                    break;
                }
            }
        }
        mark=mark->Next();
    }

// if we have a VPS events, move start and stop mark to VPS event
    if (macontext.Config->useVPS) {
        LogSeparator();
        dsyslog("cMarkAdStandalone::CheckMarks(): apply VPS events");
        DebugMarks();     //  only for debugging
        if (ptr_cDecoder) {
            int vpsOffset=marks.LoadVPS(macontext.Config->recDir, "START:"); // VPS start mark
            if (vpsOffset > 0) {
                isyslog("found VPS start event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_START, false);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS start event found");

            vpsOffset=marks.LoadVPS(macontext.Config->recDir, "PAUSE_START:");     // VPS pause start mark = stop mark
            if (vpsOffset > 0) {
                isyslog("found VPS pause start event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_STOP, true);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause start event found");

            vpsOffset=marks.LoadVPS(macontext.Config->recDir, "PAUSE_STOP:");     // VPS pause stop mark = start mark
            if (vpsOffset > 0) {
                isyslog("found VPS pause stop event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_START, true);
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS pause stop event found");

            vpsOffset=marks.LoadVPS(macontext.Config->recDir, "STOP:");     // VPS stop mark
            if (vpsOffset > 0) {
                isyslog("found VPS stop event at offset %ds", vpsOffset);
                AddMarkVPS(vpsOffset, MT_STOP, false);
                marks.DelWeakFromTo(0, INT_MAX, MT_VPSCHANGE);  // delete all non vps marks
            }
            else dsyslog("cMarkAdStandalone::CheckMarks(): no VPS stop event found");
        }
        else isyslog("VPS info usage requires --cDecoder");
    }

    LogSeparator();
    dsyslog(" cMarkAdStandalone::CheckMarks(): final marks:");
    DebugMarks();     //  only for debugging
    LogSeparator();
}


void cMarkAdStandalone::AddMarkVPS(const int offset, const int type, const bool isPause) {
    if (!ptr_cDecoder) return;
    int delta=macontext.Video.Info.FramesPerSecond*MAXRANGE;
    int vpsFrame = ptr_cDecoder->GetIFrameFromOffset(offset*100);
    clMark *mark = NULL;
    char *comment = NULL;
    char *timeText = NULL;
    if (!isPause) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d)", (type == MT_START) ? "start" : "stop", vpsFrame);
        mark =  ((type == MT_START)) ? marks.GetNext(0, MT_START, 0x0F) : marks.GetPrev(INT_MAX, MT_STOP, 0x0F);
    }
    else {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): found VPS %s at frame (%d)", (type == MT_START) ? "pause start" : "pause stop", vpsFrame);
        mark =  ((type == MT_START)) ? marks.GetAround(delta, vpsFrame, MT_START, 0x0F) :  marks.GetAround(delta, vpsFrame, MT_STOP, 0x0F);
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

    timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
    if (timeText) {
        dsyslog("cMarkAdStandalone::AddMarkVPS(): mark to replace at frame (%d) type 0x%X at %s", mark->position, mark->type, timeText);

        if (asprintf(&comment,"VPS %s (%d), moved from mark (%d) type 0x%X at %s %s", (type == MT_START) ? "start" : "stop", vpsFrame, mark->position, mark->type, timeText, (type == MT_START) ? "*" : "") == -1) comment=NULL;
        ALLOC(strlen(comment)+1, "comment");
        dsyslog("cMarkAdStandalone::AddMarkVPS(): delete mark on position (%d)", mark->position);
        marks.Del(mark->position);
        marks.Add((type == MT_START) ? MT_VPSSTART : MT_VPSSTOP, vpsFrame, comment);
        FREE(strlen(comment)+1,"comment");
        free(comment);
        FREE(strlen(timeText)+1, "indexToHMSF");
        free(timeText);
    }
}


void cMarkAdStandalone::AddMark(MarkAdMark *Mark) {
    if (!Mark) return;
    if (!Mark->Type) return;
    if ((macontext.Config) && (macontext.Config->logoExtraction!=-1)) return;
    if (gotendmark) return;

    char *comment=NULL;
    switch (Mark->Type) {
        case MT_ASSUMEDSTART:
            if (asprintf(&comment,"assuming start (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_ASSUMEDSTOP:
            if (asprintf(&comment,"assuming stop (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_NOBLACKSTART:
            if (asprintf(&comment,"detected end of black screen (%i)*",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_NOBLACKSTOP:
            if (asprintf(&comment,"detected start of black screen (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_LOGOSTART:
            if (asprintf(&comment,"detected logo start (%i)*",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_LOGOSTOP:
            if (asprintf(&comment,"detected logo stop (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_HBORDERSTART:
            if (asprintf(&comment,"detected start of horiz. borders (%i)*",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_HBORDERSTOP:
            if (asprintf(&comment,"detected stop of horiz. borders (%i)", Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_VBORDERSTART:
            if (asprintf(&comment,"detected start of vert. borders (%i)*", Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_VBORDERSTOP:
            if (asprintf(&comment,"detected stop of vert. borders (%i)", Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_ASPECTSTART:
            if (!Mark->AspectRatioBefore.Num) {
                if (asprintf(&comment,"aspectratio start with %i:%i (%i)*", Mark->AspectRatioAfter.Num, Mark->AspectRatioAfter.Den, Mark->Position)==-1) comment=NULL;
                ALLOC(strlen(comment)+1, "comment");
            }
            else {
                if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)*", Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                         Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den, Mark->Position)==-1) comment=NULL;
                ALLOC(strlen(comment)+1, "comment");
                if ((macontext.Config->autoLogo > 0) &&( Mark->Position > 0) && bDecodeVideo) {
                    isyslog("logo detection reenabled at frame (%d), trying to find a logo from this position", Mark->Position);
                    macontext.Video.Options.IgnoreLogoDetection=false;
                }
            }
            break;
        case MT_ASPECTSTOP:
            if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)", Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                     Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den, Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            if ((macontext.Config->autoLogo > 0) && (Mark->Position > 0) && bDecodeVideo) {
                isyslog("logo detection reenabled at frame (%d), trying to find a logo from this position", Mark->Position);
                macontext.Video.Options.IgnoreLogoDetection=false;
            }
            break;
        case MT_CHANNELSTART:
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment,"audio channel change from %i to %i (%i)*", Mark->ChannelsBefore,Mark->ChannelsAfter, Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_CHANNELSTOP:
            if ((Mark->Position > chkSTART) && (Mark->Position < iStopA / 2) && !macontext.Audio.Info.channelChange) {
                dsyslog("cMarkAdStandalone::AddMark(): first audio channel change is after chkSTART, disable logo/border/aspect detection now");
                if (iStart == 0) marks.DelWeakFromTo(marks.GetFirst()->position, INT_MAX, MT_CHANNELCHANGE); // only if we heve selected a start mark
                bDecodeVideo=false;
                macontext.Video.Options.IgnoreAspectRatio=true;
                macontext.Video.Options.IgnoreLogoDetection=true;
            }
            macontext.Audio.Info.channelChange = true;
            if (asprintf(&comment,"audio channel change from %i to %i (%i)", Mark->ChannelsBefore,Mark->ChannelsAfter, Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_RECORDINGSTART:
            if (asprintf(&comment,"start of recording (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        case MT_RECORDINGSTOP:
            if (asprintf(&comment,"stop of recording (%i)",Mark->Position)==-1) comment=NULL;
            ALLOC(strlen(comment)+1, "comment");
            break;
        default:
            dsyslog("cMarkAdStandalone::AddMark(): unknown mark type 0x%X", Mark->Type);
    }

    char *indexToHMSF = marks.IndexToHMSF(Mark->Position,&macontext, ptr_cDecoder);
    if (indexToHMSF) {
        if (comment) isyslog("%s at %s",comment, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }

    clMark *prev=marks.GetLast();
    if (prev) {
        if (prev->position==Mark->Position) {
            if (prev->type>Mark->Type) {
                isyslog("previous mark (%i) type 0x%X stronger than actual mark on same position, deleting (%i) type 0x%X", prev->position, prev->type, Mark->Position, Mark->Type);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
                return;
            }
            else {
                isyslog("actual mark (%d) type 0x%X stronger then previous mark on same position, deleting (%i) type 0x%X",Mark->Position, Mark->Type, prev->position, prev->type);
                marks.Del(prev);
            }
        }
    }

    if (((Mark->Type & 0x0F)==MT_START) && (!iStart) && (Mark->Position < (abs(iStopA) - 2*macontext.Video.Info.FramesPerSecond * MAXRANGE ))) {
        clMark *prevStop=marks.GetPrev(Mark->Position,(Mark->Type & 0xF0) | MT_STOP);
        if (prevStop) {
            int MARKDIFF = (int) (macontext.Video.Info.FramesPerSecond * 10);    // maybe this is only ia short logo detection failure
            if ( (Mark->Position - prevStop->position) < MARKDIFF )
            {
                double distance=(Mark->Position - prevStop->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between STOP and START too short (%.1fs), deleting %i,%i",distance, prevStop->position, Mark->Position);
                inBroadCast = true;
                marks.Del(prevStop);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
                return;
            }
        }
    }

    if (Mark->Type==MT_LOGOSTOP) {
        clMark *prevLogoStart=marks.GetPrev(Mark->Position,MT_LOGOSTART);
        if (prevLogoStart) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond * 30);    // maybe this is only ia short logo detection failure
            if (((Mark->Position - prevLogoStart->position) < MARKDIFF ) && (prevLogoStart->position == marks.GetFirst()->position))   // do not delete start mark
            {
                double distance = (Mark->Position-prevLogoStart->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i",distance, prevLogoStart->position, Mark->Position);
                inBroadCast = false;
                marks.Del(prevLogoStart);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
                return;
            }
        }
    }

    if (((Mark->Type & 0x0F) == MT_STOP) && (!iStart) && (Mark->Position < abs(iStopA) - macontext.Video.Info.FramesPerSecond * MAXRANGE )) {
        clMark *prevStart = marks.GetPrev(Mark->Position,(Mark->Type & 0xF0) | MT_START);
        if (prevStart) {
            int MARKDIFF;
            if ((Mark->Type & 0xF0) == MT_LOGOCHANGE) {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond * 120);
            }
            else {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond * 90);
            }
            if ((Mark->Position - prevStart->position) < MARKDIFF) {
                double distance = (Mark->Position - prevStart->position) / macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i", distance, prevStart->position, Mark->Position);
                inBroadCast = false;
                marks.Del(prevStart);
                if (comment) {
                    FREE(strlen(comment)+1, "comment");
                    free(comment);
                }
                return;
            }
        }
    }

    prev=marks.GetLast();
    if (prev) {
        if ((prev->type & 0x0F)==(Mark->Type & 0x0F)) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond * 30);
            int diff=abs(Mark->Position-prev->position);
            if (diff<MARKDIFF) {
                if (prev->type>Mark->Type) {
                    isyslog("previous mark (%i) type 0x%X stronger than actual mark, deleting (%i) type 0x%X", prev->position, prev->type, Mark->Position, Mark->Type);
                    if (comment) {
                        FREE(strlen(comment)+1, "comment");
                        free(comment);
                    }
                    return;
                }
                else {
                    isyslog("actual mark (%i) type 0x%X stronger then previous mark, deleting %i type 0x%X",Mark->Position, Mark->Type, prev->position, prev->type);
                    marks.Del(prev);
                }
            }
        }
    }
    marks.Add(Mark->Type,Mark->Position,comment);
    if (comment) {
        FREE(strlen(comment)+1, "comment");
        free(comment);
    }

// set inBroadCast status
    bool inBroadCastBefore = inBroadCast;
    if ((Mark->Type & 0xF0) != MT_BLACKCHANGE){ //  dont use BLACKSCEEN to detect if we are in broadcast
        if (!((Mark->Type <= MT_ASPECTSTART) && (marks.GetPrev(Mark->Position,MT_CHANNELSTOP) && marks.GetPrev(Mark->Position,MT_CHANNELSTART)))) { // if there are MT_CHANNELSTOP and MT_CHANNELSTART marks, wait for MT_CHANNELSTART
            if ((Mark->Type & 0x0F)==MT_START) {
                inBroadCast=true;
            }
            else {
                inBroadCast=false;
            }
        }
    }
    dsyslog("cMarkAdStandalone::AddMark(): status inBroadCast after %i", inBroadCast);
    if (inBroadCast != inBroadCastBefore) dsyslog("cMarkAdStandalone::AddMark(): status inBroadCast changed to %i", inBroadCast);
    if (macontext.Config->Before) {
        if (Mark->Position > chkSTART) marks.Save(directory,&macontext, ptr_cDecoder, isTS, true);
        else marks.Save(directory,&macontext, ptr_cDecoder, isTS, false);
    }
}


void cMarkAdStandalone::SaveFrame(int frame) {
    if (!macontext.Video.Info.Width) {
        dsyslog("cMarkAdStandalone::SaveFrame(): macontext.Video.Info.Width not set");
        return;
    }
    if (!macontext.Video.Data.Valid) {
        dsyslog("cMarkAdStandalone::SaveFrame():  macontext.Video.Data.Valid not set");
        return;
    }

    FILE *pFile;
    char szFilename[256];

    // Open file
    sprintf(szFilename, "/tmp/frame%06d.pgm", frame);
    pFile=fopen(szFilename, "wb");
    if (pFile==NULL) return;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", macontext.Video.Data.PlaneLinesize[0], macontext.Video.Info.Height);

    // Write pixel data
    if (fwrite(macontext.Video.Data.Plane[0],1, macontext.Video.Data.PlaneLinesize[0]*macontext.Video.Info.Height,pFile)) {};
    // Close file
    fclose(pFile);
}


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
    if (macontext.Config->logoExtraction!=-1) {
        return;
    }
    if (sleepcnt>=2) {
        dsyslog("slept too much");
        return; // we already slept too much
    }
    if (ptr_cDecoder) framecnt = ptr_cDecoder->GetFrameNumber();
    bool notenough=true;
    do {
        struct stat statbuf;
        if (stat(indexFile,&statbuf)==-1) {
#if defined CLASSIC_DECODER
            if (!macontext.Config->GenIndex)
                esyslog("failed to stat %s",indexFile);
#endif
            return;
        }

        int maxframes=statbuf.st_size/8;
        if (maxframes<(framecnt+200)) {
            if ((difftime(time(NULL),statbuf.st_mtime))>=WAITTIME) {
                if (length && startTime) {
                    if (time(NULL)>(startTime+(time_t) length)) {
                        // "old" recording
//                        tsyslog("assuming old recording, now>startTime+length");
                        return;
                    }
                    else {
                        sleepcnt=0;
                        if (!iwaittime) esyslog("recording interrupted, waiting for continuation...");
                        iwaittime+=WAITTIME;
                    }
                }
                else {
                    // "old" recording
                    dsyslog("assuming old recording - no length and startTime");
                    return;
                }
            }
            unsigned int sleeptime=WAITTIME;
            time_t sleepstart=time(NULL);
            double slepttime=0;
            while ((unsigned int)slepttime<sleeptime) {
                while (sleeptime>0) {
                    unsigned int ret=sleep(sleeptime); // now we sleep and hopefully the index will grow
                    if ((errno) && (ret)) {
                        if (abortNow) return;
                        esyslog("got errno %i while waiting for new data",errno);
                        if (errno!=EINTR) return;
                    }
                    sleeptime=ret;
                }
                slepttime=difftime(time(NULL),sleepstart);
                if (slepttime<WAITTIME) {
                    esyslog("what's wrong with your system? we just slept %.0fs",slepttime);
                }
            }
            waittime+=(int) slepttime;
            sleepcnt++;
            if (sleepcnt>=2) {
                esyslog("no new data after %is, skipping wait!", waittime);
                notenough=false; // something went wrong?
            }
        }
        else {
            if (iwaittime) {
                esyslog("resuming after %is of interrupted recording, marks can be wrong now!",iwaittime);
            }
            iwaittime=0;
            sleepcnt=0;
            notenough=false;
        }
    }
    while (notenough);
    return;
}


void cMarkAdStandalone::ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos) {
    if (!NewPos) return;
    if (!Mark1) return;
    if (!*Mark1) return;
    bool save=false;

    if ((*Mark1)->position!=NewPos->FrameNumberBefore) {
        char *buf=NULL;
        char *indexToHMSFBefore = marks.IndexToHMSF((*Mark1)->position,&macontext, ptr_cDecoder);
        char *indexToHMSFNewPos = marks.IndexToHMSF(NewPos->FrameNumberBefore,&macontext, ptr_cDecoder);
        if (asprintf(&buf,"overlap before %i at %s, moved to %i at %s",(*Mark1)->position, indexToHMSFBefore, NewPos->FrameNumberBefore, indexToHMSFNewPos)==-1) return;
        ALLOC(strlen(buf)+1, "buf");
        isyslog("%s",buf);
        marks.Del(*Mark1);
        *Mark1=marks.Add(MT_MOVED,NewPos->FrameNumberBefore,buf);
        FREE(strlen(buf)+1, "buf");
        free(buf);
        if (indexToHMSFBefore) {
            FREE(strlen(indexToHMSFBefore)+1, "indexToHMSF");
            free(indexToHMSFBefore);
        }
        if (indexToHMSFNewPos) {
            FREE(strlen(indexToHMSFNewPos)+1, "indexToHMSF");
            free(indexToHMSFNewPos);
        }
        save=true;
    }

    if (Mark2 && (*Mark2) && (*Mark2)->position!=NewPos->FrameNumberAfter) {
        char *buf=NULL;
        char *indexToHMSFBefore = marks.IndexToHMSF((*Mark2)->position,&macontext, ptr_cDecoder);
        char *indexToHMSFNewPos = marks.IndexToHMSF(NewPos->FrameNumberAfter,&macontext, ptr_cDecoder);
        if (asprintf(&buf,"overlap after %i at %s, moved to %i at %s",(*Mark2)->position, indexToHMSFBefore,
                     NewPos->FrameNumberAfter, indexToHMSFNewPos)==-1) return;
        ALLOC(strlen(buf)+1, "buf");
        isyslog("%s",buf);
        marks.Del(*Mark2);
        *Mark2=marks.Add(MT_MOVED,NewPos->FrameNumberAfter,buf);
        FREE(strlen(buf)+1, "buf");
        free(buf);
        if (indexToHMSFBefore) {
            FREE(strlen(indexToHMSFBefore)+1, "indexToHMSF");
            free(indexToHMSFBefore);
        }
        if (indexToHMSFNewPos) {
            FREE(strlen(indexToHMSFNewPos)+1, "indexToHMSF");
            free(indexToHMSFNewPos);
        }
        save=true;
    }
    if (save) marks.Save(directory,&macontext,ptr_cDecoder,isTS,true);
}


#if defined CLASSIC_DECODER
bool cMarkAdStandalone::ProcessFile2ndPass(clMark **Mark1, clMark **Mark2, int Number, off_t Offset, int Frame, int Frames) {
    if (!directory) return false;
    if (!Number) return false;
    if (!Frames) return false;
    if (!decoder) return false;
    if (!Mark1) return false;
    if (!*Mark1) return false;

    int pn; // process number 1=start mark, 2=before mark, 3=after mark
    if (Mark2) {
        if (!(*Mark1) || !(*Mark2)) return false;
        if (*Mark1==*Mark2) pn=mSTART;
        if (*Mark1!=*Mark2) pn=mAFTER;
    }
    else {
        pn=mBEFORE;
    }

    if (!Reset(false)) {
        // reset all, but marks
        esyslog("failed resetting state");
        return false;
    }
    iframe=Frame;
    int actframe=Frame;
    int framecounter=0;
    int pframe=-1;
    MarkAdPos *pos=NULL;

    while (framecounter<Frames) {
        if (abortNow) return false;

        const int datalen=319976;
        uchar data[datalen];

        char *fbuf;
        if (isTS) {
            if (asprintf(&fbuf,"%s/%05i.ts",directory,Number)==-1) return false;
            ALLOC(strlen(fbuf)+1, "fbuf");
        }
        else {
            if (asprintf(&fbuf,"%s/%03i.vdr",directory,Number)==-1) return false;
            ALLOC(strlen(fbuf)+1, "fbuf");
        }

        int f=open(fbuf,O_RDONLY);
        FREE(strlen(fbuf)+1, "fbuf");
        free(fbuf);
        if (f==-1) return false;

        int dataread;
        if (pn==mSTART) {
            dsyslog("processing file %05i (start mark)",Number);
        }
        else {
            if (pn==mBEFORE) {
                dsyslog("processing file %05i (before mark %i)",Number,(*Mark1)->position);
            }
            else {
                dsyslog("processing file %05i (after mark %i)",Number,(*Mark2)->position);
            }
        }

        if (lseek(f,Offset,SEEK_SET)!=Offset) {
            close(f);
            return false;
        }

        while ((dataread=read(f,data,datalen))>0) {
            if (abortNow) break;

            if ((demux) && (video) && (decoder) && (streaminfo)) {
                uchar *tspkt = data;
                int tslen = dataread;

                while (tslen>0) {
                    int len=demux->Process(tspkt,tslen,&pkt);
                    if (len<0) {
                        esyslog("error demuxing file");
                        abortNow=true;
                        break;
                    }
                    else {
                        if ((pkt.Data) && ((pkt.Type & PACKET_MASK)==PACKET_VIDEO)) {
                            bool dRes=false;
                            if (streaminfo->FindVideoInfos(&macontext,pkt.Data,pkt.Length)) {
                                actframe++;
                                framecnt2++;

                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE) {
                                    lastiframe=iframe;
                                    iframe=actframe-1;
                                    dRes=true;
                                }
                            }
                            if (pn>mSTART) dRes=decoder->DecodeVideo(&macontext,pkt.Data,pkt.Length);
                            if (dRes) {
                                if (pframe!=lastiframe) {
                                    if (pn>mSTART) pos=video->ProcessOverlap(lastiframe,Frames,(pn==mBEFORE),
                                                       (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
                                    framecounter++;
                                }
                                if ((pos) && (pn==mAFTER)) {
                                    // found overlap
                                    ChangeMarks(Mark1,Mark2,pos);
                                    close(f);
                                    return true;
                                }
                                pframe=lastiframe;
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
            }

            if (abortNow) {
                close(f);
                return false;
            }
            if (framecounter>Frames) {
                break;
            }

        }
        close(f);
        Number++;
        Offset=0;
    }
    return true;
}
#endif


bool cMarkAdStandalone::ProcessMark2ndPass(clMark **mark1, clMark **mark2) {
    if (!ptr_cDecoder) return false;
    if (!mark1) return false;
    if (!*mark1) return false;
    if (!mark2) return false;
    if (!*mark2) return false;

    int iFrameCount=0;
    int fRange=0;
    MarkAdPos *ptr_MarkAdPos=NULL;

    if (!Reset(false)) {
        // reset all, but marks
        esyslog("failed resetting state");
        return false;
    }
    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): check overlap for marks at frames (%d) and (%d)", (*mark1)->position, (*mark2)->position);

    fRange=macontext.Video.Info.FramesPerSecond*120;     // 40s + 80s
    int fRangeBegin=(*mark1)->position-fRange;           // 120 seconds before first mark
    if (fRangeBegin<0) fRangeBegin=0;                    // but not before beginning of broadcast
    fRangeBegin=ptr_cDecoder->GetIFrameBefore(fRangeBegin);
    if (!fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    dsyslog("cMarkAdStandalone::ProcessMark2ndPass(): check start at frame (%d)", fRangeBegin);
    if (!ptr_cDecoder->SeekToFrame(fRangeBegin)) {
        esyslog("could not seek to frame (%i)", fRangeBegin);
        return false;
    }
    iFrameCount=ptr_cDecoder->GetIFrameRangeCount(fRangeBegin, (*mark1)->position);
    if (iFrameCount<=0) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
            return false;
    }
    while (ptr_cDecoder->GetFrameNumber() <= (*mark1)->position ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetNextFrame failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())  // if we have interlaced video this is expected, we have to read the next half picture
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() before mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos=video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(),iFrameCount,true,(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
   }

    fRange=macontext.Video.Info.FramesPerSecond*320; // 160s + 160s
    fRangeBegin=ptr_cDecoder->GetIFrameBefore((*mark2)->position);
    if (!fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    int fRangeEnd=(*mark2)->position+fRange;         // 320 seconds after second mark
    if (!ptr_cDecoder->SeekToFrame((*mark2)->position)) {
        esyslog("could not seek to frame (%d)", fRangeBegin);
        return false;
    }
    iFrameCount=ptr_cDecoder->GetIFrameRangeCount(fRangeBegin, fRangeEnd)-2;
    if (iFrameCount<=0) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
            return false;
    }
    while (ptr_cDecoder->GetFrameNumber() <= fRangeEnd ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetNextFrame failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() after mark GetFrameInfo failed at frame (%d)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos=video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(),iFrameCount,false,(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
        if (ptr_MarkAdPos) {
            // found overlap
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass found overlap in frames (%d,%d)", ptr_MarkAdPos->FrameNumberBefore, ptr_MarkAdPos->FrameNumberAfter);
            ChangeMarks(mark1,mark2,ptr_MarkAdPos);
            return true;
        }
    }
    return false;
}


void cMarkAdStandalone::MarkadCut() {
    if (abortNow) return;
    if (!ptr_cDecoder) {
        isyslog("video cut function only supported with --cDecoder");
        return;
    }
    cEncoder* ptr_cEncoder = NULL;
    LogSeparator();
    dsyslog("start MarkadCut()");
    if (marks.Count()<2) {
        isyslog("need at least 2 marks to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("final marks are:");
    DebugMarks();     //  only for debugging

    clMark *StartMark= marks.GetFirst();
    if (((StartMark->type & 0x0F) != MT_START) && (StartMark->type != MT_MOVED)) {
        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
        return;
    }
    clMark *StopMark = StartMark->Next();
    if (((StopMark->type & 0x0F) != MT_STOP) && (StopMark->type != MT_MOVED)) {
        esyslog("got invalid stop mark at (%i) type 0x%X", StopMark->position, StopMark->type);
        return;
    }
    ptr_cEncoder = new cEncoder(macontext.Config->threads, macontext.Config->ac3ReEncode);
    ALLOC(sizeof(*ptr_cEncoder), "ptr_cEncoder");
    if ( ! ptr_cEncoder->OpenFile(directory,ptr_cDecoder)) {
        esyslog("failed to open output file");
        FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
        delete ptr_cEncoder;
        return;
    }
    while(ptr_cDecoder->DecodeDir(directory)) {
        while(ptr_cDecoder->GetNextFrame()) {
            if  (ptr_cDecoder->GetFrameNumber() < StartMark->position) ptr_cDecoder->SeekToFrame(StartMark->position);
            if  (ptr_cDecoder->GetFrameNumber() > StopMark->position) {
                if (StopMark->Next() && StopMark->Next()->Next()) {
                    StartMark = StopMark->Next();
                    if (((StartMark->type & 0x0F) != MT_START) && (StartMark->type != MT_MOVED)) {
                        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
                        return;
                    }
                    StopMark = StartMark->Next();
                    if (((StopMark->type & 0x0F) != MT_STOP) && (StopMark->type != MT_MOVED)) {
                        esyslog("got invalid stop mark at (%i) type 0x%X", StopMark->position, StopMark->type);
                        return;
                    }
                }
                else break;
            }
            AVPacket *pkt=ptr_cDecoder->GetPacket();
            if ( !pkt ) {
                esyslog("failed to get packet from input stream");
                return;
            }
            if ( ! ptr_cEncoder->WritePacket(pkt, ptr_cDecoder) ) {
                dsyslog("cMarkAdStandalone::MarkadCut(): failed to write frame %d to output stream", ptr_cDecoder->GetFrameNumber());
            }
            if (abortNow) {
                if (ptr_cDecoder) {
                    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
                    delete ptr_cDecoder;
                }
                if (ptr_cEncoder) {
                    ptr_cEncoder->CloseFile();
                    FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
                    delete ptr_cEncoder;
                    ptr_cEncoder = NULL;
                }
                return;
            }
        }
    }
    if ( ! ptr_cEncoder->CloseFile()) {
        dsyslog("failed to close output file");
        return;
    }
    dsyslog("cMarkAdStandalone::MarkadCut(): end at frame %d", ptr_cDecoder->GetFrameNumber());
    if (ptr_cEncoder) {
        FREE(sizeof(*ptr_cEncoder), "ptr_cEncoder");
        delete ptr_cEncoder;
        ptr_cEncoder = NULL;
    }
    framecnt3 = ptr_cDecoder->GetFrameNumber();
}


void cMarkAdStandalone::Process2ndPass() {
    if (abortNow) return;
    if (duplicate) return;
#if defined CLASSIC_DECODER
    if (!decoder) return;
#else
    if (!ptr_cDecoder) return;
#endif
    if (!length) return;
    if (!startTime) return;
    if (time(NULL)<(startTime+(time_t) length)) return;

    LogSeparator();
    isyslog("start 2nd pass");

    if (!macontext.Video.Info.FramesPerSecond) {
        isyslog("WARNING: assuming fps of 25");
        macontext.Video.Info.FramesPerSecond=25;
    }

    if (!marks.Count()) {
        marks.Load(directory,macontext.Video.Info.FramesPerSecond,isTS);
    }

    clMark *p1=NULL,*p2=NULL;

    if (marks.Count()<4) {
        dsyslog("only %i marks, abort 2nd pass", marks.Count());
        return; // we cannot do much without marks
    }
    p1=marks.GetFirst();
    if (!p1) return;

    p1=p1->Next();
    if (p1) p2=p1->Next();

    if (ptr_cDecoder) {
        ptr_cDecoder->Reset();
        ptr_cDecoder->DecodeDir(directory);
    }

    while ((p1) && (p2)) {
#if defined CLASSIC_DECODER
        off_t offset;
        int number,frame,iframes;
#endif
        int frange=macontext.Video.Info.FramesPerSecond*120; // 40s + 80s
        int frange_begin=p1->position-frange; // 120 seconds before first mark
        if (frange_begin<0) frange_begin=0; // but not before beginning of broadcast

        if (ptr_cDecoder) {
            dsyslog("cMarkAdStandalone::Process2ndPass(): check overlap for marks at frames (%i) and (%i)", p1->position, p2->position);
            if (!ProcessMark2ndPass(&p1,&p2)) {
                dsyslog("cMarkAdStandalone::Process2ndPass(): no overlap found for marks at frames (%i) and (%i)", p1->position, p2->position);
            }
        }
#if defined CLASSIC_DECODER
        else {
            if (marks.ReadIndex(directory,isTS,frange_begin,frange,&number,&offset,&frame,&iframes)) {
                if (!ProcessFile2ndPass(&p1,NULL,number,offset,frame,iframes)) break;

                frange=macontext.Video.Info.FramesPerSecond*320; // 160s + 160s
                if (marks.ReadIndex(directory,isTS,p2->position,frange,&number,&offset,&frame,&iframes)) {
                    if (!ProcessFile2ndPass(&p1,&p2,number,offset,frame,iframes)) break;
                }
            }
            else {
                esyslog("error reading index");
                return;
            }
        }
#endif
        p1=p2->Next();
        if (p1) {
            p2=p1->Next();
        }
        else {
            p2=NULL;
        }
    }
    if (ptr_cDecoder) framecnt2 = ptr_cDecoder->GetFrameNumber();
    dsyslog("end 2ndPass");
}


#if defined CLASSIC_DECODER
bool cMarkAdStandalone::ProcessFile(int Number) {
    if (!directory) return false;
    if (!Number) return false;

    CheckIndexGrowing();
    if (abortNow) return false;

    const int datalen=319976;
    uchar data[datalen];
    char *fbuf;
    if (isTS) {
        if (asprintf(&fbuf,"%s/%05i.ts",directory,Number)==-1) {
            ALLOC(strlen(fbuf)+1, "fbuf");
            esyslog("failed to allocate string, out of memory?");
            return false;
        }
    }
    else {
        if (asprintf(&fbuf,"%s/%03i.vdr",directory,Number)==-1) {
            esyslog("failed to allocate string, out of memory?");
            return false;
        }
        ALLOC(strlen(fbuf)+1, "fbuf");
    }

    int f=open(fbuf,O_RDONLY);
    if (f==-1) {
        if (isTS) {
            dsyslog("failed to open %05i.ts",Number);
        } else {
            dsyslog("failed to open %03i.vdr",Number);
        }
        return false;
    }
    FREE(strlen(fbuf)+1, "fbuf");
    free(fbuf);

    int dataread;
    dsyslog("processing file %05i",Number);

    int pframe=-1;
    demux->NewFile();

again:
    while ((dataread=read(f,data,datalen))>0) {
        if (abortNow) break;
        if ((demux) && (video) && (streaminfo)) {
            uchar *tspkt = data;
            int tslen = dataread;
            while (tslen>0) {
                int len=demux->Process(tspkt,tslen,&pkt);
                if (len<0) {
                    esyslog("error demuxing");
                    abortNow=true;
                    break;
                }
                else {
                    if (pkt.Data) {
                        if ((pkt.Type & PACKET_MASK)==PACKET_VIDEO) {
                            bool dRes=false;
                            if (streaminfo->FindVideoInfos(&macontext,pkt.Data,pkt.Length)) {
                                if ((macontext.Video.Info.Height) && (!noticeHEADER)) {
                                    if ((!isTS) && (!noticeVDR_VID)) {
                                        isyslog("found %s-video (0x%02X)",
                                                macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262",
                                                pkt.Stream);
                                        noticeVDR_VID=true;
                                    }

                                    isyslog("%s %ix%i%c%0.f",(macontext.Video.Info.Height>576) ? "HDTV" : "SDTV",
                                            macontext.Video.Info.Width,
                                            macontext.Video.Info.Height,
                                            macontext.Video.Info.Interlaced ? 'i' : 'p',
                                            macontext.Video.Info.FramesPerSecond);
                                    noticeHEADER=true;
                                }

                                if (!framecnt) {
                                    CalculateCheckPositions(tStart*macontext.Video.Info.FramesPerSecond);
                                }
                                if (macontext.Config->GenIndex) {
                                    marks.WriteIndex(directory,isTS,demux->Offset(),macontext.Video.Info.Pict_Type,Number);
                                }
                                framecnt++;
                                if ((macontext.Config->logoExtraction!=-1) && (framecnt>=256)) {
                                    isyslog("finished logo extraction, please check /tmp for pgm files");
                                    abortNow=true;
                                    if (f!=-1) close(f);
                                    return true;
                                }

                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE) {
                                    lastiframe=iframe;
                                    if ((iStart<0) && (lastiframe>-iStart)) iStart=lastiframe;
                                    if ((iStop<0) && (lastiframe>-iStop)) {
                                        iStop=lastiframe;
                                        iStopinBroadCast=inBroadCast;
                                    }
                                    if ((iStopA<0) && (lastiframe>-iStopA)) {
                                        iStopA=lastiframe;
                                    }
                                    iframe=framecnt-1;
                                    dRes=true;
                                }
                            }
                            if ((decoder) && (bDecodeVideo)) dRes=decoder->DecodeVideo(&macontext,pkt.Data,pkt.Length);
                            if (dRes) {
                                if (pframe!=lastiframe) {
                                    MarkAdMarks *vmarks=video->Process(lastiframe,iframe);
                                    if (vmarks) {
                                        for (int i=0; i<vmarks->Count; i++) {
                                            AddMark(&vmarks->Number[i]);
                                        }
                                    }
//                                    if (lastiframe == 14716) SaveFrame(lastiframe);  // TODO: JUST FOR DEBUGGING!
                                    if (iStart>0) {
                                        if ((inBroadCast) && (lastiframe>chkSTART)) CheckStart();
                                    }
                                    if ((lastiframe>iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) && (macontext.Video.Options.IgnoreBlackScreenDetection)) {
                                        dsyslog("start black screen detection");
                                        macontext.Video.Options.IgnoreBlackScreenDetection=false;   // use black sceen setection only to find end mark
                                    }
                                    if ((iStop>0) && (iStopA>0)) {
                                        if (lastiframe>chkSTOP) CheckStop();
                                    }
                                    pframe=lastiframe;
                                }
                            }
                        }

                        if ((pkt.Type & PACKET_MASK)==PACKET_AC3) {
                            if (streaminfo->FindAC3AudioInfos(&macontext, pkt.Data, pkt.Length)) {
                                if ((!isTS) && (!noticeVDR_AC3)) {
                                    isyslog("found AC3 (0x%02X)",pkt.Stream);
                                    noticeVDR_AC3=true;
                                }
                                if ((framecnt-iframe)<=3) {
                                    MarkAdMark *amark=audio->Process(lastiframe,iframe);
                                    if (amark) {
                                        AddMark(amark);
                                    }
                                }
                            }
                        }
                    }

                    tspkt+=len;
                    tslen-=len;
                }
            }
        }
        if ((gotendmark) && (!macontext.Config->GenIndex)) {
            if (f!=-1) close(f);
            return true;
        }

        CheckIndexGrowing();
        if (abortNow) {
            if (f!=-1) close(f);
            return false;
        }
    }
    if ((dataread==-1) && (errno==EINTR)) goto again; // i know this is ugly ;)

    close(f);
    return true;
}
#endif


bool cMarkAdStandalone::Reset(bool FirstPass) {
    bool ret=true;
    if (FirstPass) framecnt=0;
    lastiframe=0;
    iframe=0;

    gotendmark=false;

    memset(&pkt,0,sizeof(pkt));

    chkSTART=chkSTOP=INT_MAX;

    if (FirstPass) {
        marks.DelAll();
#if defined CLASSIC_DECODER
        marks.CloseIndex(directory,isTS);
#endif
    }

    macontext.Video.Info.Pict_Type=0;
    macontext.Video.Info.AspectRatio.Den=0;
    macontext.Video.Info.AspectRatio.Num=0;
    memset(macontext.Audio.Info.Channels, 0, sizeof(macontext.Audio.Info.Channels));

#if defined CLASSIC_DECODER
    if (streaminfo) streaminfo->Clear();
    if (decoder) ret=decoder->Clear();
    if (demux) demux->Clear();
#endif
    if (video) video->Clear(false);
    if (audio) audio->Clear();
    return ret;
}


bool cMarkAdStandalone::ProcessFrame(cDecoder *ptr_cDecoder) {
    if (!ptr_cDecoder) return false;

    if ((macontext.Config->logoExtraction!=-1) && (ptr_cDecoder->GetIFrameCount()>=512)) {    // extract logo
        isyslog("finished logo extraction, please check /tmp for pgm files");
        abortNow=true;
    }

    if (ptr_cDecoder->GetFrameInfo(&macontext)) {
        if (ptr_cDecoder->isVideoPacket()) {
            if (ptr_cDecoder->isInterlacedVideo() && !macontext.Video.Info.Interlaced && (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) &&
                                                     (ptr_cDecoder->GetVideoFramesPerSecond()==25) && (ptr_cDecoder->GetVideoRealFrameRate()==50)) {
                dsyslog("change internal frame rate to handle H.264 interlaced video");
                macontext.Video.Info.FramesPerSecond*=2;
                macontext.Video.Info.Interlaced=true;
                CalculateCheckPositions(tStart*macontext.Video.Info.FramesPerSecond);
            }
            lastiframe=iframe;
            if ((iStart<0) && (lastiframe>-iStart)) iStart=lastiframe;
            if ((iStop<0) && (lastiframe>-iStop)) {
                iStop=lastiframe;
                iStopinBroadCast=inBroadCast;
            }
            if ((iStopA<0) && (lastiframe>-iStopA)) {
                iStopA=lastiframe;
            }
            iframe=ptr_cDecoder->GetFrameNumber();

            if (!video) {
                esyslog("cMarkAdStandalone::ProcessFrame() video not initialized");
                return false;
            }
            if (!macontext.Video.Data.Valid) {
                isyslog("cMarkAdStandalone::ProcessFrame faild to get video data of frame (%d)", ptr_cDecoder->GetFrameNumber());
                return false;
            }

            if ( !restartLogoDetectionDone && (lastiframe > (iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE)) &&
                                     ((macontext.Video.Options.IgnoreBlackScreenDetection) || (macontext.Video.Options.IgnoreLogoDetection))) {
                    isyslog("restart logo and black screen detection at frame (%d)",ptr_cDecoder->GetFrameNumber());
                    restartLogoDetectionDone=true;
                    bDecodeVideo=true;
                    macontext.Video.Options.IgnoreBlackScreenDetection=false;   // use black sceen setection only to find end mark
                    if (macontext.Video.Options.IgnoreLogoDetection==true) {
                        macontext.Video.Options.IgnoreLogoDetection=false;
                        if (video) video->Clear(true);    // reset logo decoder status
                    }
            }

// #define TESTFRAME 17487     // TODO: JUST FOR DEBUGGING!
#ifdef TESTFRAME
            if ((lastiframe > (TESTFRAME-100)) && (lastiframe < (TESTFRAME+100))) {
                dsyslog("save frame (%i) to /tmp", lastiframe);
                SaveFrame(lastiframe);
            }
#endif

            if (!bDecodeVideo) macontext.Video.Data.Valid=false; // make video picture invalid, we do not need them
            MarkAdMarks *vmarks=video->Process(lastiframe, iframe);
            if (vmarks) {
                for (int i = 0; i < vmarks->Count; i++) {
                    if (((vmarks->Number[i].Type & 0xF0) == MT_LOGOCHANGE) && (macontext.Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H265)) {   // we are one iFrame to late with logo marks, these is to much (2s) with H.265 codec
                        int iFrameBefore = ptr_cDecoder->GetIFrameBefore(vmarks->Number[i].Position);
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
             MarkAdMark *amark = audio->Process(lastiframe,iframe);
            if (amark) AddMark(amark);
        }
    }
    return true;
}


void cMarkAdStandalone::ProcessFile_cDecoder() {
    LogSeparator();
    dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): start processing files");
    ptr_cDecoder = new cDecoder(macontext.Config->threads);
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
            macontext.Video.Info.Height=ptr_cDecoder->GetVideoHeight();
            isyslog("video hight: %i", macontext.Video.Info.Height);

            macontext.Video.Info.Width=ptr_cDecoder->GetVideoWidth();
            isyslog("video width: %i", macontext.Video.Info.Width);

            macontext.Video.Info.FramesPerSecond=ptr_cDecoder->GetVideoFramesPerSecond();
            isyslog("average frame rate %i frames per second",(int) macontext.Video.Info.FramesPerSecond);
            isyslog("real frame rate    %i frames per second",ptr_cDecoder->GetVideoRealFrameRate());

            CalculateCheckPositions(tStart*macontext.Video.Info.FramesPerSecond);
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
            if (! cMarkAdStandalone::ProcessFrame(ptr_cDecoder)) break;
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
                iStopinBroadCast= true;
                dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): recording ends unexpected before chkSTOP (%d) at frame %d", chkSTOP, lastiframe);
                isyslog("got end of recording before recording length from info file reached");
            }
            CheckStop();
        }
        CheckMarks();
        if ((inBroadCast) && (!gotendmark) && (lastiframe)) {
            MarkAdMark tempmark;
            tempmark.Type=MT_RECORDINGSTOP;
            tempmark.Position=lastiframe;
            AddMark(&tempmark);
        }
    }
    dsyslog("cMarkAdStandalone::ProcessFile_cDecoder(): end processing files");
}


#if defined CLASSIC_DECODER
void cMarkAdStandalone::ProcessFile() {
    LogSeparator();
    dsyslog("cMarkAdStandalone::ProcessFile(): start processing files");
    for (int i=1; i<=MaxFiles; i++) {
        if (abortNow) break;
        if (!ProcessFile(i)) break;
        if ((gotendmark) && (!macontext.Config->GenIndex)) break;
    }

    if (!abortNow) {
        if (iStart !=0 ) {  // iStart will be 0 if iStart was called
            dsyslog("recording ends unexpected before chkSTART (%d) at frame %d", chkSTART, lastiframe);
            isyslog("got end of recording before recording length from info file reached");
            CheckStart();
        }
        if (iStopA > 0) {
            if (iStop <= 0) {  // unexpected end of recording reached
                iStop = lastiframe;
                iStopinBroadCast= true;
                dsyslog("recording ends unexpected before chkSTOP (%d) at frame %d", chkSTOP, lastiframe);
                isyslog("got end of recording before recording length from info file reached");
            }
            CheckStop();
        }
        CheckMarks();
        if ((inBroadCast) && (!gotendmark) && (lastiframe)) {
            MarkAdMark tempmark;
            tempmark.Type=MT_RECORDINGSTOP;
            tempmark.Position=lastiframe;
            AddMark(&tempmark);
        }
    }
    if (demux) skipped=demux->Skipped();
    dsyslog("cMarkAdStandalone::ProcessFile(): end processing files");
}
#endif


void cMarkAdStandalone::Process_cDecoder() {
    if (abortNow) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);
    ProcessFile_cDecoder();

    if (!abortNow) {
        if (marks.Save(directory, &macontext, ptr_cDecoder, isTS, true)) {
            if (length && startTime)
                    if (macontext.Config->SaveInfo) SaveInfo();

        }
    }
}


#if defined CLASSIC_DECODER
void cMarkAdStandalone::Process() {
    if (abortNow) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);

    ProcessFile();

    marks.CloseIndex(directory,isTS);
    if (!abortNow) {
        if (marks.Save(directory,&macontext,ptr_cDecoder,isTS)) {
            if (length && startTime) {
                if (((time(NULL)>(startTime+(time_t) length)) || (gotendmark)) && !ptr_cDecoder ) {
                    int iIndexError=false;
                    int tframecnt=macontext.Config->GenIndex ? framecnt : 0;
                    if (marks.CheckIndex(directory,isTS,&tframecnt,&iIndexError)) {
                        if (iIndexError) {
                            if (macontext.Config->GenIndex) {
                                switch (iIndexError) {
                                    case IERR_NOTFOUND:
                                        isyslog("no index found");
                                        break;
                                    case IERR_TOOSHORT:
                                        isyslog("index too short");
                                        break;
                                    default:
                                        isyslog("index doesn't match marks");
                                    break;
                                }
                                if (RegenerateIndex()) {
                                    isyslog("recreated index");
                                }
                                else {
                                    esyslog("failed to recreate index");
                                }
                            }
                            else {
                                esyslog("index doesn't match marks%s", ((isTS) || ((macontext.Info.VPid.Type== MARKAD_PIDTYPE_VIDEO_H264) && (!isTS))) ?
                                            ", sorry you're lost" : ", please run genindex");
                            }
                        }
                    }
                    if (macontext.Config->SaveInfo) SaveInfo();
                }
                else { // this shouldn't be reached
                    if (macontext.Config->logoExtraction==-1) esyslog("ALERT: stopping before end of broadcast");
                }
            }
        }
    }
    if (macontext.Config->GenIndex) marks.RemoveGeneratedIndex(directory,isTS);
}
#endif


bool cMarkAdStandalone::SetFileUID(char *File) {
    if (!File) return false;
    struct stat statbuf;
    if (!stat(directory,&statbuf)) {
        if (chown(File,statbuf.st_uid, statbuf.st_gid)==-1) return false;
    }
    return true;
}


bool cMarkAdStandalone::SaveInfo() {
    isyslog("writing info file");
    char *src,*dst;
    if (isREEL) {
        if (asprintf(&src,"%s/info.txt",directory)==-1) return false;
    }
    else {
        if (asprintf(&src,"%s/info%s",directory,isTS ? "" : ".vdr")==-1) return false;
    }
    ALLOC(strlen(src)+1, "src");

    if (asprintf(&dst,"%s/info.bak",directory)==-1) {
        free(src);
        return false;
    }
    ALLOC(strlen(dst)+1, "src");

    FILE *r,*w;
    r=fopen(src,"r");
    if (!r) {
        free(src);
        free(dst);
        return false;
    }

    w=fopen(dst,"w+");
    if (!w) {
        fclose(r);
        free(src);
        free(dst);
        return false;
    }

    char *line=NULL;
    char *lline=NULL;
    size_t len=0;

    char lang[4]="";

    int component_type_add=0;
    if (macontext.Video.Info.Height>576) component_type_add=8;

    int stream_content=0;
    if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H262) stream_content=1;
    if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) stream_content=5;

    int component_type_43;
    int component_type_169;
    if ((macontext.Video.Info.FramesPerSecond==25) || (macontext.Video.Info.FramesPerSecond==50)) {
        component_type_43=1;
        component_type_169=3;
    }
    else {
        component_type_43=5;
        component_type_169=7;
    }

    bool err=false;
    for (int i=0; i<MAXSTREAMS; i++) {
        dsyslog("stream %i has %i channels", i, macontext.Info.Channels[i]);
    }
    unsigned int stream_index = 0;
    if (ptr_cDecoder) stream_index++;
    while (getline(&line,&len,r)!=-1) {
        dsyslog("info file line: %s", line);
        if (line[0]=='X') {
            int stream = 0;
            unsigned int type = 0;
            char descr[256] = "";

            int result=sscanf(line,"%*c %3i %3X %3c %250c",&stream,&type,(char *) &lang, (char *) &descr);
            if ((result!=0) && (result!=EOF)) {
                switch (stream) {
                    case 1:
                    case 5:
                        if (stream==stream_content) {
                            if ((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3)) {
                                if (fprintf(w,"X %i %02i %s 4:3\n",stream_content, component_type_43+component_type_add,lang)<=0) err=true;
                                macontext.Info.AspectRatio.Num=0;
                                macontext.Info.AspectRatio.Den=0;
                            }
                            else if ((macontext.Info.AspectRatio.Num==16) && (macontext.Info.AspectRatio.Den==9)) {
                                if (fprintf(w,"X %i %02X %s 16:9\n",stream_content, component_type_169+component_type_add,lang)<=0) err=true;
                                macontext.Info.AspectRatio.Num=0;
                                macontext.Info.AspectRatio.Den=0;
                            }
                            else {
                                if (fprintf(w,"%s",line)<=0) err=true;
                            }
                        }
                        else {
                            if (fprintf(w,"%s",line)<=0) err=true;
                        }
                        break;
                    case 2:
                        if (type==5) {
                            if (macontext.Info.Channels[stream_index]==6) {
                                if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
                                macontext.Info.Channels[stream_index]=0;
                            }
                            else if (macontext.Info.Channels[stream_index]==2) {
                                if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
                                macontext.Info.Channels[stream_index]=0;
                            }
                            else {
                                if (fprintf(w,"%s",line)<=0) err=true;
                            }
                        }
                        else {
                            if (fprintf(w,"%s",line)<=0) err=true;
                        }
                        break;
                    case 4:
                        if (type == 0x2C) {
                            if (fprintf(w,"%s",line)<=0) err=true;
                            macontext.Info.Channels[stream_index] = 0;
                            stream_index++;
                        }
                        break;
                    default:
                        if (fprintf(w,"%s",line)<=0) err=true;
                        break;
                }
            }
        }
        else {
            if (line[0]!='@') {
                if (fprintf(w,"%s",line)<=0) err=true;
            }
            else {
                if (lline) {
                    free(lline);
                    err=true;
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

    if (lang[0]==0) strcpy(lang,"und");

    if (stream_content) {
        if ((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3) && (!err)) {
            if (fprintf(w,"X %i %02i %s 4:3\n",stream_content, component_type_43+component_type_add,lang)<=0) err=true;
        }
        if ((macontext.Info.AspectRatio.Num==16) && (macontext.Info.AspectRatio.Den==9) && (!err)) {
            if (fprintf(w,"X %i %02i %s 16:9\n",stream_content, component_type_169+component_type_add,lang)<=0) err=true;
        }
    }
    for (short int stream=0; stream<MAXSTREAMS; stream++) {
        if (macontext.Info.Channels[stream] == 0) continue;
        if ((macontext.Info.Channels[stream]==2) && (!err)) {
            if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
        }
        if ((macontext.Info.Channels[stream]==6) && (!err)) {
            if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
       }
    }
    if (line) {
        if (fprintf(w,"%s",line)<=0) err=true;
        free(line);
    }
    fclose(w);
    struct stat statbuf_r;
    if (fstat(fileno(r),&statbuf_r)==-1) err=true;

    fclose(r);
    if (err) {
        unlink(dst);
    }
    else {
        if (rename(dst,src)==-1) {
            err=true;
        }
        else {
            // preserve timestamps from old file
            struct utimbuf oldtimes;
            oldtimes.actime=statbuf_r.st_atime;
            oldtimes.modtime=statbuf_r.st_mtime;
            if (utime(src,&oldtimes)) {};
            SetFileUID(src);
        }
    }

    free(src);
    free(dst);
    return (err==false);
}


time_t cMarkAdStandalone::GetBroadcastStart(time_t start, int fd) {
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat statbuf;
    FILE *mounts=setmntent(_PATH_MOUNTED,"r");
    int mlen;
    int oldmlen=0;
    bool useatime=false;
    while ((ent=getmntent(mounts))!=NULL) {
        if (strstr(directory,ent->mnt_dir)) {
            mlen=strlen(ent->mnt_dir);
            if (mlen>oldmlen) {
                if (strstr(ent->mnt_opts,"noatime")) {
                    useatime=true;
                }
                else {
                    useatime=false;
                }
            }
            oldmlen=mlen;
        }
    }
    endmntent(mounts);

    if (useatime) dsyslog("cMarkAdStandalone::GetBroadcastStart(): mount option noatime is set, use time from directory %s", directory);
    else dsyslog("cMarkAdStandalone::GetBroadcastStart(): mount option noatime is not set");

    if ((useatime) && (stat(directory, &statbuf) != -1)) {
        if (fabs(difftime(start,statbuf.st_atime)) < 60*60*12) {  // do not beleave recordings > 12h
            dsyslog("cMarkAdStandalone::GetBroadcastStart(): getting recording start from directory atime");
            return statbuf.st_atime;
        }
        dsyslog("cMarkAdStandalone::GetBroadcastStart(): got no valid atime %ld for start time %ld", statbuf.st_atime, start);
    }

    // try to get from mtime
    // (and hope info.vdr has not changed after the start of the recording)
    if (fstat(fd,&statbuf)!=-1) {
        if (fabs(difftime(start,statbuf.st_mtime))<7200) {
            dsyslog("cMarkAdStandalone::GetBroadcastStart(): getting recording start from VDR info file mtime");
            return (time_t) statbuf.st_mtime;
        }
    }

    // fallback to the directory name (time part)
    const char *timestr=strrchr(directory,'/');
    if (timestr) {
        timestr++;
        if (isdigit(*timestr)) {
            time_t now = time(NULL);
            struct tm tm_r;
            struct tm t = *localtime_r(&now, &tm_r); // init timezone
            if (sscanf(timestr, "%4d-%02d-%02d.%02d%*c%02d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, & t.tm_min)==5) {
                t.tm_year-=1900;
                t.tm_mon--;
                t.tm_sec=0;
                t.tm_isdst=-1;
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

    dsyslog("cMarkAdStandalone::CheckLogo(): using logo directory %s",macontext.Config->logoDirectory);
    dsyslog("cMarkAdStandalone::CheckLogo(): searching logo for %s",macontext.Info.ChannelName);
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
        ptr_cExtractLogo = new cExtractLogo();
        ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
        int endpos = ptr_cExtractLogo->SearchLogo(&macontext, 0);   // search logo from start
        for (int retry = 2; retry < 5; retry++) {
            if (endpos > 0) {
                isyslog("no logo found in recording, retry in %ind recording part", retry);
                endpos = ptr_cExtractLogo->SearchLogo(&macontext, endpos);   // search logo from last end position
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
                for (int i = 0; i < (int) strlen(macontext.Info.ChannelName); i++) {
                    if (macontext.Info.ChannelName[i] == ' ') macontext.Info.ChannelName[i] = '_';
                    if (macontext.Info.ChannelName[i] == '.') macontext.Info.ChannelName[i] = '_';
                    if (macontext.Info.ChannelName[i] == '/') macontext.Info.ChannelName[i] = '_';
                }
            }
        }
        if ((line[0] == 'E') && (!bLiveRecording)) {
            int result = sscanf(line,"%*c %*10i %20li %6i %*2x %*2x", &startTime, &length);
            if (result != 2) {
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

    if ((length) && (!bIgnoreTimerInfo) && (startTime)) {
        time_t rStart=GetBroadcastStart(startTime, fileno(f));
        if (rStart) {
            dsyslog("cMarkAdStandalone::LoadInfo(): recording start at %s", strtok(ctime(&rStart), "\n"));
            dsyslog("cMarkAdStandalone::LoadInfo() broadcast start at %s from VDR info file", strtok(ctime(&startTime), "\n"));
            tStart=(int) (startTime-rStart);
            if (tStart > 60*60) {   // more than 1h pre-timer make no sense, there must be a wrong directory time
                isyslog("pre-time %is not valid, possible wrong directory time, set pre-timer to vdr default (2min)", tStart);
                tStart = 120;
            }
            if (tStart < 0) {
                if (length+tStart > 0) {
                    isyslog("broadcast start truncated by %im, length will be corrected", -tStart / 60);
                    startTime = rStart;
                    length += tStart;
                    tStart = -1;
                }
                else {
                    esyslog("cannot determine broadcast start, assume VDR default pre timer of 120s");
                    tStart = 120;
                }
            }
        }
        else {
            tStart = 0;
        }
    }
    else {
        tStart = 0;
    }
    fclose(f);
    dsyslog("cMarkAdStandalone::LoadInfo() broadcast start %is after recording start", tStart);

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
    MaxFiles=0;
    isTS=false;
    if (!directory) return false;
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");
    struct stat statbuf;
    if (stat(buf,&statbuf)==-1) {
        if (errno!=ENOENT) {
            FREE(strlen(buf)+1, "buf");
            free(buf);
            return false;
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
        buf=NULL;
        if (asprintf(&buf,"%s/001.vdr",directory)==-1) return false;
        ALLOC(strlen(buf)+1, "buf");
        if (stat(buf,&statbuf)==-1) {
            FREE(strlen(buf)+1, "buf");
            free(buf);
            return false;
        }
        FREE(strlen(buf)+1, "buf");
        free(buf);
        // .VDR detected
        isTS=false;
        MaxFiles=999;
        return true;
    }
    FREE(strlen(buf)+1, "buf");
    free(buf);
    // .TS detected
    isTS=true;
    MaxFiles=65535;
    return true;
}


#if defined CLASSIC_DECODER
bool cMarkAdStandalone::CheckVDRHD() {
    char *buf;
    if (asprintf(&buf,"%s/001.vdr",directory)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    int fd=open(buf,O_RDONLY);
    FREE(strlen(buf)+1, "buf");
    free(buf);
    if (fd==-1) return false;

    uchar pes_buf[32];
    if (read(fd,pes_buf,sizeof(pes_buf))!=sizeof(pes_buf)) {
        close(fd);
        return false;
    }
    close(fd);

    if ((pes_buf[0]==0) && (pes_buf[1]==0) && (pes_buf[2]==1) && ((pes_buf[3] & 0xF0)==0xE0)) {
        int payloadstart=9+pes_buf[8];
        if (payloadstart>23) return false;
        uchar *start=&pes_buf[payloadstart];
        if ((start[0]==0) && (start[1]==0) && (start[2]==1) && (start[5]==0) && (start[6]==0) && (start[7]==0) && (start[8]==1)) {
            return true;
        }
    }
    return false;
}
#endif


#if defined CLASSIC_DECODER
off_t cMarkAdStandalone::SeekPATPMT()
{
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return (off_t) -1;
    ALLOC(strlen(buf)+1, "buf");

    int fd=open(buf,O_RDONLY);
    FREE(strlen(buf)+1, "buf");
    free(buf);
    if (fd==-1) return (off_t) -1;
    uchar peek_buf[188];
    for (int i=0; i<5000; i++) {
        int ret=read(fd,peek_buf,sizeof(peek_buf));
        if (!ret) {
            close(fd);
            return (off_t) -2;
        }
        if (ret!=sizeof(peek_buf)) {
            close(fd);
            return (off_t) -2;
        }
        if (ret<0) {
            if (errno!=EINTR) {
                close(fd);
                return (off_t) -3;
            } else {
                sleep(3);
            }
        }

        if ((peek_buf[0]==0x47) && ((peek_buf[1] & 0x5F)==0x40) && (peek_buf[2]==00))
        {
            off_t ret=lseek(fd,0,SEEK_CUR);
            close(fd);
            return ret-188;
        }

    }
    close(fd);
    return (off_t) -1;
}
#endif


#if defined CLASSIC_DECODER
bool cMarkAdStandalone::CheckPATPMT(off_t Offset)
{
    if (Offset<(off_t) 0) return false;
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    int fd=open(buf,O_RDONLY);
    FREE(strlen(buf)+1, "buf");
    free(buf);
    if (fd==-1) return false;

    if (lseek(fd,Offset,SEEK_SET)==(off_t)-1) {
        close(fd);
        return false;
    }

    uchar patpmt_buf[564];
    uchar *patpmt;

    if (read(fd,patpmt_buf,sizeof(patpmt_buf))!=sizeof(patpmt_buf)) {
        close(fd);
        return false;
    }
    close(fd);
    patpmt=patpmt_buf;

    if ((patpmt[0]==0x47) && ((patpmt[1] & 0x5F)==0x40) && (patpmt[2]==0x11) && ((patpmt[3] & 0x10)==0x10)) patpmt+=188; // skip SDT

    // some checks
    if ((patpmt[0]!=0x47) || (patpmt[188]!=0x47)) return false; // no TS-Sync
    if (((patpmt[1] & 0x5F)!=0x40) && (patpmt[2]!=0)) return false; // no PAT
    if ((patpmt[3] & 0x10)!=0x10) return false; // PAT not without AFC
    if ((patpmt[191] & 0x10)!=0x10) return false; // PMT not without AFC
    struct PAT *pat = (struct PAT *) &patpmt[5];

    // more checks
    if (pat->reserved1!=3) return false; // is always 11
    if (pat->reserved3!=7) return false; // is always 111

    int pid=pat->pid_L+(pat->pid_H<<8);
    int pmtpid=((patpmt[189] & 0x1f)<<8)+patpmt[190];
    if (pid!=pmtpid) return false; // pid in PAT differs from pid in PMT

    struct PMT *pmt = (struct PMT *) &patpmt[193];

    // still more checks
    if (pmt->reserved1!=3) return false; // is always 11
    if (pmt->reserved2!=3) return false; // is always 11
    if (pmt->reserved3!=7) return false; // is always 111
    if (pmt->reserved4!=15) return false; // is always 1111

    if ((pmt->program_number_H!=pat->program_number_H) || (pmt->program_number_L!=pat->program_number_L)) return false;

    int desc_len=(pmt->program_info_length_H<<8)+pmt->program_info_length_L;
    if (desc_len>166) return false; // beyond patpmt buffer

    int section_end = 196+(pmt->section_length_H<<8)+pmt->section_length_L;
    section_end-=4; // we don't care about the CRC32
    if (section_end>376) return false; //beyond patpmt buffer

    int i=205+desc_len;

    while (i<section_end) {
        struct ES_DESCRIPTOR *es=NULL;
        struct STREAMINFO *si = (struct STREAMINFO *) &patpmt[i];
        int esinfo_len=(si->ES_info_length_H<<8)+si->ES_info_length_L;
        if (esinfo_len) {
            es = (struct ES_DESCRIPTOR *) &patpmt[i+sizeof(struct STREAMINFO)];
        }

        // oh no -> more checks!
        if (si->reserved1!=7) return false;
        if (si->reserved2!=15) return false;

        int pid=(si->PID_H<<8)+si->PID_L;

        switch (si->stream_type) {
            case 0x1:
            case 0x2:
                macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
                // just use the first pid
                if (!macontext.Info.VPid.Num) macontext.Info.VPid.Num=pid;
                break;
            case 0x3:
            case 0x4: // just use the first pid
                if (!macontext.Info.APid.Num) macontext.Info.APid.Num=pid;
                break;
            case 0x6:
                if (es) {
                    if (es->Descriptor_Tag==0x6A) macontext.Info.DPid.Num=pid;
                }
                break;
            case 0x1b:
                macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
                // just use the first pid
                if (!macontext.Info.VPid.Num) macontext.Info.VPid.Num=pid;
                break;
        }
        i+=(sizeof(struct STREAMINFO)+esinfo_len);
    }
    return true;
}
#endif


#if defined CLASSIC_DECODER
bool cMarkAdStandalone::RegenerateIndex() {
    if (!directory) return false;
    // rename index[.vdr].generated -> index[.vdr]
    char *oldpath,*newpath;
    if (asprintf(&oldpath,"%s/index%s.generated",directory,
                 isTS ? "" : ".vdr")==-1) return false;
    ALLOC(strlen(oldpath)+1, "oldpath");
    if (asprintf(&newpath,"%s/index%s",directory,isTS ? "" : ".vdr")==-1) {
        free(oldpath);
        return false;
    }
    ALLOC(strlen(newpath)+1, "newpath");

    if (rename(oldpath,newpath)!=0) {
        if (errno!=ENOENT) {
            free(oldpath);
            free(newpath);
            return false;
        }
    }
    free(oldpath);
    free(newpath);
    return true;
}
#endif


bool cMarkAdStandalone::CreatePidfile() {
    char *buf=NULL;
    if (asprintf(&buf,"%s/markad.pid",directory)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    // check for other running markad process
    FILE *oldpid=fopen(buf,"r");
    if (oldpid) {
        // found old pidfile, check if it's still running
        int pid;
        if (fscanf(oldpid,"%10i\n",&pid)==1) {
            char procname[256]="";
            snprintf(procname,sizeof(procname),"/proc/%i",pid);
            struct stat statbuf;
            if (stat(procname,&statbuf)!=-1) {
                // found another, running markad
                fprintf(stderr,"another instance is running on %s",directory);
                abortNow=duplicate=true;
            }
        }
        fclose(oldpid);
    }
    else { // fopen above sets the error to 2, reset it here!
        errno=0;
    }
    if (duplicate) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        return false;
    }

    FILE *pidfile=fopen(buf,"w+");

    SetFileUID(buf);

    FREE(strlen(buf)+1, "buf");
    free(buf);
    if (!pidfile) return false;
    fprintf(pidfile,"%i\n",(int) getpid());
    fflush(pidfile);
    fclose(pidfile);
    return true;
}


void cMarkAdStandalone::RemovePidfile() {
    if (!directory) return;
    if (duplicate) return;

    char *buf;
    if (asprintf(&buf,"%s/markad.pid",directory)!=-1) {
        ALLOC(strlen(buf)+1, "buf");
        unlink(buf);
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
}


// const char cMarkAdStandalone::frametypes[8]={'?','I','P','B','D','S','s','b'};


cMarkAdStandalone::cMarkAdStandalone(const char *Directory, const MarkAdConfig *config) {
    setlocale(LC_MESSAGES, "");
    directory=Directory;
    gotendmark=false;
    inBroadCast=false;
    iStopinBroadCast=false;
    isREEL=false;

    indexFile=NULL;
#if defined CLASSIC_DECODER
    streaminfo=NULL;
    demux=NULL;
    decoder=NULL;
    skipped=0;
#endif
    video=NULL;
    audio=NULL;
    osd=NULL;

    memset(&pkt,0,sizeof(pkt));

    noticeVDR_VID=false;
    noticeVDR_AC3=false;
    noticeHEADER=false;
    noticeFILLER=false;

    length=0;

    sleepcnt=0;
    waittime=iwaittime=0;
    duplicate=false;
    title[0]=0;

    macontext={};
    macontext.Config=config;

    bDecodeVideo=config->DecodeVideo;
    bDecodeAudio=config->DecodeAudio;

    tStart=iStart=iStop=iStopA=0;

    if ((config->ignoreInfo & IGNORE_TIMERINFO)==IGNORE_TIMERINFO) {
        bIgnoreTimerInfo=true;
    }
    else {
        bIgnoreTimerInfo=false;
    }

#if defined CLASSIC_DECODER
    macontext.Info.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;
#endif
    macontext.Info.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    if (!config->NoPid) {
        CreatePidfile();
        if (abortNow) return;
    }

    if (LOG2REC) {
        char *fbuf;
        if (asprintf(&fbuf,"%s/markad.log",directory)!=-1) {
            ALLOC(strlen(fbuf)+1, "fbuf");
            if (freopen(fbuf,"w+",stdout)) {};
            SetFileUID(fbuf);
            FREE(strlen(fbuf)+1, "fbuf");
            free(fbuf);
        }
    }

    long lb;
    errno=0;
    lb=sysconf(_SC_LONG_BIT);
    if (errno==0) {
        isyslog("starting v%s (%libit)",VERSION,lb);
    }
    else {
        isyslog("starting v%s",VERSION);
    }

    int ver = avcodec_version();
    char *libver = NULL;
    if (asprintf(&libver,"%i.%i.%i",ver >> 16 & 0xFF,ver >> 8 & 0xFF,ver & 0xFF) != -1) {
        ALLOC(strlen(libver)+1, "libver");
        isyslog("using libavcodec.so.%s with %i threads",libver,config->threads);
        if (ver!=LIBAVCODEC_VERSION_INT) {
            esyslog("libavcodec header version %s",AV_STRINGIFY(LIBAVCODEC_VERSION));
            esyslog("header and library mismatch, do not report decoder bugs");
        }
        if (config->use_cDecoder && ((ver >> 16) < MINLIBAVCODECVERSION)) esyslog("update libavcodec to at least version %d, do not report decoder bugs", MINLIBAVCODECVERSION);
        FREE(strlen(libver)+1, "libver");
        free(libver);
    }

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(41<<8)+0)
    tsyslog("libavcodec config: %s",avcodec_configuration());
#endif
    if (((ver >> 16)<52)) {
        dsyslog("dont report bugs about H264, use libavcodec >= 52 instead!");
    }

    isyslog("on %s",Directory);

    if (!bDecodeAudio) {
        isyslog("audio decoding disabled by user");
    }
    if (!bDecodeVideo) {
        isyslog("video decoding disabled by user");
    }
    if (bIgnoreTimerInfo) {
        isyslog("timer info usage disabled by user");
    }
    if (config->logoExtraction!=-1) {
        // just to be sure extraction works
        bDecodeVideo=true;
    }
    if (config->Before) sleep(10);

    char *tmpDir = strdup(directory);
#ifdef DEBUGMEM
    ALLOC(strlen(tmpDir)+1, "tmpDir");
    int memsize_tmpDir = strlen(directory)+1;
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
    if (strstr(recName,"/@")) {
        isyslog("live-recording, disabling pre-/post timer");
        bIgnoreTimerInfo=true;
        bLiveRecording=true;
    }
    else {
        bLiveRecording=false;
    }
#ifdef DEBUGMEM
    FREE(memsize_tmpDir, "tmpDir");
#endif
    free(tmpDir);

    if (!CheckTS()) {
        esyslog("no files found");
        abortNow=true;
        return;
    }

    if (isTS) {
#if defined CLASSIC_DECODER
        off_t pos;
        int sc=0;
        do {
            pos=SeekPATPMT();
            if (pos==(off_t) -2) {
                sleep(10);
                sc++;
                if (sc>6) break;
            }
        } while (pos==(off_t) -2);

        if (!CheckPATPMT(pos)) {
            esyslog("no PAT/PMT found (%i) -> cannot process",(int) pos);
            abortNow=true;
            return;
        }
#endif
        if (asprintf(&indexFile,"%s/index",Directory)==-1) indexFile=NULL;
        ALLOC(strlen(indexFile)+1, "indexFile");
    }
    else {
        macontext.Info.APid.Num=-1;
        macontext.Info.VPid.Num=-1;
#if defined CLASSIC_DECODER
        macontext.Info.DPid.Num=-1;
        if (CheckVDRHD()) {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
        }
        else {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
        }
#endif
        if (asprintf(&indexFile,"%s/index.vdr",Directory)==-1) indexFile=NULL;
        ALLOC(strlen(indexFile)+1, "indexFile");
    }
    macontext.Info.APid.Num=0; // till now we do just nothing with stereo-sound

    if (!LoadInfo()) {
        if (bDecodeVideo) {
            esyslog("failed loading info - logo %s%sdisabled",
                    (config->logoExtraction!=-1) ? "extraction" : "detection",
                    bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
            tStart=iStart=iStop=iStopA=0;
            macontext.Video.Options.IgnoreLogoDetection=true;
        }
    }
    else {
        if (!CheckLogo() && (config->logoExtraction==-1)) {
            isyslog("no logo found, logo detection disabled");
            macontext.Video.Options.IgnoreLogoDetection=true;
        }
    }

    if (tStart>1) {
        if (tStart<60) tStart=60;
        isyslog("pre-timer %is",tStart);
    }
    if (length) isyslog("broadcast length %im",length/60);

    if (title[0]) {
        ptitle=title;
    }
    else {
        ptitle=(char *) Directory;
    }

    if (config->OSD) {
        osd= new cOSDMessage(config->svdrphost,config->svdrpport);
        if (osd) osd->Send("%s '%s'",tr("starting markad for"),ptitle);
    }
    else {
        osd=NULL;
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (macontext.Info.VPid.Num) {
        if (isTS) {
            isyslog("found %s-video (0x%04x)", macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262", macontext.Info.VPid.Num);
        }
#if defined CLASSIC_DECODER
        demux=new cDemux(macontext.Info.VPid.Num,macontext.Info.DPid.Num,macontext.Info.APid.Num, macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,true);
        ALLOC(sizeof(*demux), "demux");
    }
    else {
        demux=NULL;
#endif
    }

    if (macontext.Info.APid.Num) {
        if (macontext.Info.APid.Num!=-1)
            isyslog("found MP2 (0x%04x)",macontext.Info.APid.Num);
    }

    if (!abortNow) {
#if defined CLASSIC_DECODER
        if (macontext.Info.DPid.Num) {
            if (macontext.Info.DPid.Num!=-1)
                isyslog("found AC3 (0x%04x)",macontext.Info.DPid.Num);
        }
        decoder = new cMarkAdDecoder(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,config->threads);
        ALLOC(sizeof(*decoder), "decoder");
        streaminfo = new cMarkAdStreamInfo;
        ALLOC(sizeof(*streaminfo), "streaminfo");
#endif
        video = new cMarkAdVideo(&macontext);
        ALLOC(sizeof(*video), "video");
        audio = new cMarkAdAudio(&macontext);
        ALLOC(sizeof(*audio), "audio");
        if (macontext.Info.ChannelName)
            isyslog("channel %s",macontext.Info.ChannelName);
        if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
            macontext.Video.Options.IgnoreAspectRatio=true;
    }

    framecnt=0;
    framecnt2=0;
    framecnt3=0;
    lastiframe=0;
    iframe=0;
    chkSTART=chkSTOP=INT_MAX;
    gettimeofday(&tv1,&tz);
}


cMarkAdStandalone::~cMarkAdStandalone() {
    if ((!abortNow) && (!duplicate)) {
        gettimeofday(&tv2, &tz);
        time_t sec;
        suseconds_t usec;
        sec = tv2.tv_sec - tv1.tv_sec;
        usec = tv2.tv_usec - tv1.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        double etime = 0;
        double ftime = 0;
        etime = sec + ((double) usec / 1000000) - waittime;
        if (etime > 0) ftime = (framecnt + framecnt2 + framecnt3) / etime;
        isyslog("processed time %.2fs, %i/%i/%i frames, %.1f fps", etime, framecnt, framecnt2, framecnt3, ftime);
    }

    if ((osd) && (!duplicate)) {
        if (abortNow) {
            osd->Send("%s '%s'",tr("markad aborted for"), ptitle);
        }
        else {
            osd->Send("%s '%s'",tr("markad finished for"), ptitle);
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

#if defined CLASSIC_DECODER
    if (skipped) {
            isyslog("skipped %i bytes",skipped);
        }
    if (demux) {
        FREE(sizeof(*demux), "demux");
        delete demux;
    }
    if (decoder) {
        FREE(sizeof(*decoder), "decoder");
        delete decoder;
    }
    if (streaminfo) {
        FREE(sizeof(*streaminfo), "streaminfo");
        delete streaminfo;
    }
#endif
    if (video) {
        FREE(sizeof(*video), "video");
        delete video;
    }
    if (audio) {
        FREE(sizeof(*audio), "audio");
        delete audio;
    }
    if (osd) {
        FREE(sizeof(*osd), "osd");
        delete osd;
    }
    if (ptr_cDecoder) {
        FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
        delete ptr_cDecoder;
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
           "-v              --verbose\n"
           "                  increments loglevel by one, can be given multiple times\n"
           "-B              --backupmarks\n"
           "                  make a backup of existing marks\n"
#if defined CLASSIC_DECODER
           "-G              --genindex\n"
           "                  regenerate index file\n"
           "                  this functions is depreciated and will be removed in a future version, use vdr --genindex instead\n"
#endif
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
#if defined CLASSIC_DECODER
           "                --cDecoder\n"
           "                  use alternative cDecoder class for decoding\n"
#endif
           "                --vps\n"
           "                  use markad.vps from recording directory for stat and stop mark\n"
           "                  requires --cDecoder\n"
           "                --cut\n"
           "                  cut vidio based on marks and write it in the recording directory\n"
#if defined CLASSIC_DECODER
           "                  requires --cDecoder\n"
#endif
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
           "after                        markad starts to analyze the recording\n"
           "before                       markad exits immediately if called with \"before\"\n"
           "edited                       markad exits immediately if called with \"edited\"\n"
           "nice                         runs markad with nice(19)\n"
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
            kill(getpid(),SIGSTOP);
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
            for (i=0; i<trace_size; ++i) {
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


char *recDir=NULL;


void freedir(void) {
    if (recDir) free(recDir);
}


int main(int argc, char *argv[]) {
    bool bAfter=false,bEdited=false;
    bool bFork=false,bNice=false,bImmediateCall=false;
    int niceLevel = 19;
    int ioprio_class=3;
    int ioprio=7;
    char *tok,*str;
    int ntok;
    int online=0;
    bool bPass2Only=false;
    bool bPass1Only=false;
    struct config config={};

    // set defaults
    config.DecodeVideo=true;
    config.DecodeAudio=true;
    config.SaveInfo=false;
    config.logoExtraction=-1;
    config.logoWidth=-1;
    config.logoHeight=-1;
    config.threads=-1;
    config.astopoffs=0;
    config.posttimer=600;
    strcpy(config.svdrphost,"127.0.0.1");
    strcpy(config.logoDirectory,"/var/lib/markad");

    struct servent *serv=getservbyname("svdrp","tcp");
    if (serv) {
        config.svdrpport=htons(serv->s_port);
    }
    else {
        config.svdrpport=2001;
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
#if defined CLASSIC_DECODER
            {"genindex",0, 0, 'G'},
#endif
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
                        config.DecodeVideo=false;
                        break;
                    case 2:
                        config.DecodeAudio=false;
                        break;
                    case 3:
                        config.DecodeVideo=false;
                        config.DecodeAudio=false;
                        break;
                    default:
                        fprintf(stderr, "markad: invalid disable option: %s\n", optarg);
                         return 2;
                         break;
                }
                break;
            case 'i':
                // --ignoreinfo
                config.ignoreInfo=atoi(optarg);
                if ((config.ignoreInfo<1) || (config.ignoreInfo>255)) {
                    fprintf(stderr, "markad: invalid ignoreinfo option: %s\n", optarg);
                    return 2;
                }
                break;
            case 'l':
                strncpy(config.logoDirectory,optarg,sizeof(config.logoDirectory));
                config.logoDirectory[sizeof(config.logoDirectory)-1]=0;
                break;
            case 'p':
                // --priority
                if (isnumber(optarg) || *optarg=='-') niceLevel = atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                    return 2;
                }
                bNice = true;
                break;
            case 'r':
                // --ioprio
                str=strchr(optarg,',');
                if (str) {
                    *str=0;
                    ioprio=atoi(str+1);
                    *str=',';
                }
                ioprio_class=atoi(optarg);
                if ((ioprio_class<1) || (ioprio_class>3)) {
                    fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                    return 2;
                }
                if ((ioprio<0) || (ioprio>7)) {
                    fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                    return 2;
                }
                if (ioprio_class==3) ioprio=7;
                bNice = true;
                break;
            case 'v':
                // --verbose
                SysLogLevel++;
                if (SysLogLevel>10) SysLogLevel=10;
                break;
            case 'B':
                // --backupmarks
                config.BackupMarks=true;
                break;
#if defined CLASSIC_DECODER
            case 'G':
                config.GenIndex=true;
                fprintf(stderr, "markad: --genindex is depreciated and will be removed in a future version, use vdr --genindex instead\n");
                break;
#endif
            case 'I':
                config.SaveInfo=true;
                break;
            case 'L':
                // --extractlogo
                str=optarg;
                ntok=0;
                while (true) {
                    tok=strtok(str,",");
                    if (!tok) break;
                    switch (ntok) {
                        case 0:
                            config.logoExtraction=atoi(tok);
                            if ((config.logoExtraction<0) || (config.logoExtraction>3)) {
                                fprintf(stderr, "markad: invalid extractlogo value: %s\n", tok);
                                return 2;
                            }
                            break;
                        case 1:
                            config.logoWidth=atoi(tok);
                            if ((config.logoWidth<50) || (config.logoWidth>LOGO_MAXWIDTH)) {
                                fprintf(stderr, "markad: invalid width value: %s\n", tok);
                                return 2;
                            }
                            break;
                        case 2:
                            config.logoHeight=atoi(tok);
                            if ((config.logoHeight<20) || (config.logoHeight>LOGO_MAXHEIGHT)) {
                                fprintf(stderr, "markad: invalid height value: %s\n", tok);
                                return 2;
                            }
                            break;
                         default:
                            break;
                    }
                    str=NULL;
                    ntok++;
                }
                break;
            case 'O':
                // --OSD
                config.OSD=true;
                break;
            case 'R':
                // --log2rec
                LOG2REC=true;
                break;
            case 'T':
                // --threads
                config.threads=atoi(optarg);
                if (config.threads<1) config.threads=1;
                if (config.threads>16) config.threads=16;
                break;
            case 'V':
                printf("markad %s - marks advertisements in VDR recordings\n",VERSION);
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
                strncpy(config.markFileName,optarg,sizeof(config.markFileName));
                config.markFileName[sizeof(config.markFileName)-1]=0;
                break;
            case 2: // --loglevel
                SysLogLevel=atoi(optarg);
                if (SysLogLevel>10) SysLogLevel=10;
                if (SysLogLevel<0) SysLogLevel=2;
                break;
            case 3: // --online
                if (optarg) {
                    online=atoi(optarg);
                }
                else {
                    online=1;
                }
                if ((online!=1) && (online!=2)) {
                    fprintf(stderr, "markad: invalid online value: %s\n", optarg);
                    return 2;
                }
                break;
            case 4: // --nopid
                config.NoPid=true;
                break;
            case 5: // --svdrphost
                strncpy(config.svdrphost,optarg,sizeof(config.svdrphost));
                config.svdrphost[sizeof(config.svdrphost)-1]=0;
                break;
            case 6: // --svdrpport
                if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536) {
                    config.svdrpport=atoi(optarg);
                }
                else {
                    fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                    return 2;
                }
                break;
            case 7: // --pass2only
                bPass2Only=true;
                if (bPass1Only) {
                    fprintf(stderr, "markad: you cannot use --pass2only with --pass1only\n");
                    return 2;
                }
                break;
            case 8: // --pass1only
                bPass1Only=true;
                if (bPass2Only) {
                    fprintf(stderr, "markad: you cannot use --pass1only with --pass2only\n");
                    return 2;
                }
                break;
            case 9: // --astopoffs
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 240) {
                    config.astopoffs=atoi(optarg);
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
                config.use_cDecoder=true;
                break;
            case 12: // --cut
                config.MarkadCut=true;
                break;
            case 13: // --ac3reencode
                config.ac3ReEncode=true;
                break;
            case 14: // --autoLogo
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 2) config.autoLogo=atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid autologo value: %s\n", optarg);
                    return 2;
                }
                break;
            case 15: // --vps
                config.useVPS=true;
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
                if (!online) online=1;
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
                if ( strstr(argv[optind],".rec") != NULL ) {
                    recDir=realpath(argv[optind],NULL);
                    config.recDir=recDir;
                }
            }
            optind++;
        }
    }

    // do nothing if called from vdr before/after the video is cutted
    if (bEdited) return 0;
    if ((bAfter) && (online)) return 0;
    if ((config.Before) && (online==1) && (!strchr(recDir,'@'))) return 0;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || config.Before || bAfter || bNice) && recDir ) {
        // if bFork is given go in background
        if ( bFork ) {
            //close_files();
            pid_t pid = fork();
            if (pid < 0) {
                char *err=strerror(errno);
                fprintf(stderr, "%s\n",err);
                return 2;
            }
            if (pid != 0) {
                return 0; // initial program immediately returns
            }
            if (chdir("/")==-1) {
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
        if ( bNice ) {
            if (setpriority(PRIO_PROCESS,0,niceLevel)==-1) {
                fprintf(stderr,"failed to set nice to %d\n",niceLevel);
            }
            if (ioprio_set(1,getpid(),ioprio | ioprio_class << 13)==-1) {
                fprintf(stderr,"failed to set ioprio to %i,%i\n",ioprio_class,ioprio);
            }
        }
        // store the real values, maybe set by calling nice
        errno = 0;
        int PrioProcess = getpriority(PRIO_PROCESS,0);
        if ( errno ) {  // use errno because -1 is a valid return value
            fprintf(stderr,"failed to get nice value\n");
        }
        int IOPrio = ioprio_get(1,getpid());
        if (! IOPrio) {
            fprintf(stderr,"failed to get ioprio\n");
        }
        IOPrio = IOPrio >> 13;

        // now do the work...
        struct stat statbuf;
        if (stat(recDir,&statbuf)==-1) {
            fprintf(stderr,"%s not found\n",recDir);
            return -1;
        }

        if (!S_ISDIR(statbuf.st_mode)) {
            fprintf(stderr,"%s is not a directory\n",recDir);
            return -1;
        }

        if (access(recDir,W_OK|R_OK)==-1) {
            fprintf(stderr,"cannot access %s\n",recDir);
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

        cmasta = new cMarkAdStandalone(recDir,&config);
        ALLOC(sizeof(*cmasta), "cmasta");
        if (!cmasta) return -1;

        isyslog("parameter --loglevel is set to %i", SysLogLevel);

        if (niceLevel != 19) {
            isyslog("parameter --priority %i",niceLevel);
            isyslog("warning: increasing priority may affect other applications");
        }
        if (ioprio_class != 3) {
            isyslog("parameter --ioprio %i",ioprio_class);
            isyslog("warning: increasing priority may affect other applications");
        }
        dsyslog("markad process nice level %i",PrioProcess);
        dsyslog("makrad IO priority class %i",IOPrio);

        dsyslog("parameter --logocachedir is set to %s",config.logoDirectory);
        dsyslog("parameter --threads is set to %i", config.threads);
        dsyslog("parameter --astopoffs is set to %i",config.astopoffs);
        if (LOG2REC) dsyslog("parameter --log2rec is set");

#if defined CLASSIC_DECODER
        if (config.use_cDecoder) dsyslog("parameter --cDecoder is set");
#else
        config.use_cDecoder = true;
        dsyslog("force parameter --cDecoder to set because this markad is compiled without classic decoder");
#endif
        if (config.useVPS) {
            dsyslog("parameter --vps is set");
            if (!config.use_cDecoder) {
                esyslog("--cDecoder is not set, ignoring --vps");
                config.useVPS = false;
            }
        }
        if (config.MarkadCut) {
            dsyslog("parameter --cut is set");
            if (!config.use_cDecoder) {
                esyslog("--cDecoder is not set, ignoring --cut");
                config.MarkadCut = false;
            }
        }
        if (config.ac3ReEncode) {
            dsyslog("parameter --ac3reencode is set");
            if (!config.MarkadCut) {
                esyslog("--cut is not set, ignoring --ac3reencode");
                config.ac3ReEncode = false;
            }
        }
        dsyslog("parameter --autologo is set to %i",config.autoLogo);
        if (config.Before) dsyslog("parameter Before is set");

        if (!bPass2Only)
            if (config.use_cDecoder) cmasta->Process_cDecoder();
#if defined CLASSIC_DECODER
            else {
                cmasta->Process();
            }
#endif
        if (!bPass1Only) cmasta->Process2ndPass();
        if (config.MarkadCut) cmasta->MarkadCut();
        if (cmasta) {
            FREE(sizeof(*cmasta), "cmasta");
            delete cmasta;
        }
#ifdef DEBUGMEM
        memList();
#endif
        return 0;
    }
    return usage(config.svdrpport);
}
