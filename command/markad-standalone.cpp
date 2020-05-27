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

bool SYSLOG=false;
bool LOG2REC=false;
cDecoder* ptr_cDecoder = NULL;
cExtractLogo* ptr_cExtractLogo = NULL;
cMarkAdStandalone *cmasta=NULL;
bool restartLogoDetectionDone=false;
int SysLogLevel=2;
bool abortNow = false;

static inline int ioprio_set(int which, int who, int ioprio)
{
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
    if (__NR_ioprio_set)
    {
        return syscall(__NR_ioprio_set, which, who, ioprio);
    } else {
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

void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
    if ((SYSLOG) && (!LOG2REC))
    {
        priority=LOG_ERR;
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
        va_start(ap, format);
        vsyslog(priority, fmt, ap);
        va_end(ap);
    }
    else
    {
        char buf[27]={0};
        const time_t now=time(NULL);
        if (ctime_r(&now,buf)) {
            buf[strlen(buf)-6]=0;
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
        vprintf(fmt,ap);
        va_end(ap);
        printf("\n");
        fflush(stdout);
    }
}

cOSDMessage::cOSDMessage(const char *Host, int Port)
{
    tid=0;
    msg=NULL;
    host=strdup(Host);
    port=Port;
    send(this);
}

cOSDMessage::~cOSDMessage()
{
    if (tid) pthread_join(tid,NULL);
    if (msg) free(msg);
    if (host) free((void*) host);
}

bool cOSDMessage::readreply(int fd, char **reply)
{
    usleep(400000);
    char c=' ';
    int repsize=0;
    int msgsize=0;
    bool skip=false;
    if (reply) *reply=NULL;
    do
    {
        struct pollfd fds;
        fds.fd=fd;
        fds.events=POLLIN;
        fds.revents=0;
        int ret=poll(&fds,1,600);

        if (ret<=0) return false;
        if (fds.revents!=POLLIN) return false;
        if (read(fd,&c,1)<0) return false;
        if ((reply) && (!skip) && (c!=10) && (c!=13)) {
            msgsize++;
            while ((msgsize+5)>repsize) {
                repsize+=80;
                char *tmp=(char *) realloc(*reply,repsize);
                if (!tmp) {
                    free(*reply);
                    *reply=NULL;
                    skip=true;
                } else {
                    *reply=tmp;
                }
            }
            (*reply)[msgsize-1]=c;
            (*reply)[msgsize]=0;
        }
    }
    while (c!='\n');
    return true;
}

void *cOSDMessage::send(void *posd)
{
    cOSDMessage *osd=static_cast<cOSDMessage *>(posd);

    struct hostent *host=gethostbyname(osd->host);
    if (!host)
    {
        osd->tid=0;
        return NULL;
    }

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(osd->port);
    memcpy(&name.sin_addr.s_addr,host->h_addr,host->h_length);
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock<0) return NULL;

    if (connect(sock, (struct sockaddr *)&name,size)!=0)
    {
        close(sock);
        return NULL;
    }

    char *reply=NULL;
    if (!osd->readreply(sock,&reply))
    {
        if (reply) free(reply);
        close(sock);
        return NULL;
    }

    ssize_t ret;
    if (osd->msg) {
        if (reply) free(reply);
        ret=write(sock,"MESG ",5);
        if (ret!=(ssize_t)-1) ret=write(sock,osd->msg,strlen(osd->msg));
        if (ret!=(ssize_t)-1) ret=write(sock,"\r\n",2);

        if (!osd->readreply(sock) || (ret==(ssize_t)-1))
        {
            close(sock);
            return NULL;
        }
    } else {
        if (reply) {
            char *cs=strrchr(reply,';');
            if (cs) {
                cs+=2;
                trcs(cs);
            } else {
                trcs("UTF-8"); // just a guess
            }
            free(reply);
        } else {
            trcs("UTF-8"); // just a guess
        }
    }

    ret=write(sock,"QUIT\r\n",6);

    if (ret!=(ssize_t)-1) osd->readreply(sock);
    close(sock);
    return NULL;
}

int cOSDMessage::Send(const char *format, ...)
{
    if (tid) pthread_join(tid,NULL);
    if (msg) free(msg);
    va_list ap;
    va_start(ap, format);
    if (vasprintf(&msg,format,ap)==-1) return -1;
    va_end(ap);

    if (pthread_create(&tid,NULL,(void *(*) (void *))&send, (void *) this)!=0) return -1;
    return 0;
}


void cMarkAdStandalone::CalculateCheckPositions(int startframe) {
    dsyslog("-------------------------------------------------------");
    dsyslog("CalculateCheckPositions with startframe %i",startframe);

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
        MarkAdMark mark={};
        mark.Position=0;
        mark.Type=MT_RECORDINGSTART;
        AddMark(&mark);
        startframe=macontext.Video.Info.FramesPerSecond * 6 * 60;  // give 6 minutes to get best mark type for this recording
    }
    else dsyslog("startframe %i", startframe);

    dsyslog("use frame rate %f", macontext.Video.Info.FramesPerSecond);

    iStart=-startframe;
    iStop = -(startframe + macontext.Video.Info.FramesPerSecond * length) ;   // iStop change from - to + when frames reached iStop

    iStartA=abs(iStart);
    iStopA =startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->astopoffs - 30);
    chkSTART=iStartA + macontext.Video.Info.FramesPerSecond * 3*MAXRANGE; // fit for later broadcast start
    chkSTOP=startframe + macontext.Video.Info.FramesPerSecond * (length + macontext.Config->posttimer);

    dsyslog("length of recording %is", length);
    dsyslog("assumed start frame %i", iStartA);
    dsyslog("assumed stop frame %i", iStopA);
    dsyslog("chkSTART set to %i",chkSTART);
    dsyslog("chkSTOP set to %i", chkSTOP);
    dsyslog("-------------------------------------------------------");
}


void cMarkAdStandalone::CheckStop()
{
    dsyslog("-------------------------------------------------------");
    dsyslog("checking stop (%i)", lastiframe);
    dsyslog("assumed stop frame %i", iStopA);

//  only for debugging
    clMark *mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    int delta=macontext.Video.Info.FramesPerSecond*MAXRANGE;

    clMark *end=marks.GetAround(3*delta,iStopA,MT_CHANNELSTOP);      // try if we can get a good stop mark, start with MT_ASPECTSTOP
    if (!end) {
        dsyslog("no MT_CHANNELSTOP mark found");
        end=marks.GetAround(3*delta,iStopA,MT_ASPECTSTOP);      // try MT_ASPECTSTOP
        if (!end) {
            dsyslog("no MT_ASPECTSTOP mark found");
            end=marks.GetAround(3*delta,iStopA,MT_HBORDERSTOP);         // try MT_HBORDERSTOP
            if (!end) {
                dsyslog("no MT_HBORDERSTOP mark found");
                end=marks.GetAround(3*delta,iStopA,MT_VBORDERSTOP);         // try MT_VBORDERSTOP
                if (!end) {
                    dsyslog("no MT_VBORDERSTOP mark found");
                    end=marks.GetAround(3*delta,iStopA,MT_LOGOSTOP);        // try MT_LOGOSTOP
                    if (!end) {
                        dsyslog("no MT_LOGOSTOP mark found");
                        end=marks.GetAround(3*delta,iStopA,MT_STOP,0x0F);    // try any type of stop mark
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

    clMark *lastStart=marks.GetAround(INT_MAX,lastiframe,MT_START,0x0F);
    if (end)
    {
        dsyslog("found end mark at (%i)", end->position);
        clMark *mark=marks.GetFirst();
        while (mark) {
            if ((mark->position >= iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) && (mark->position < end->position) && ((mark->type & 0xF0) < (end->type & 0xF0))) { // delete all weak marks
                dsyslog("found stronger end mark delete mark (%i)", mark->position);
                clMark *tmp=mark;
                mark=mark->Next();
                marks.Del(tmp);
                continue;
            }
            mark=mark->Next();
        }

        if ((end->type == MT_NOBLACKSTOP) && (end->position < iStopA)) {        // if stop mark is MT_NOBLACKSTOP and it is not after iStopA try next, better save than sorry
           clMark *end2=marks.GetAround(delta,end->position+2*delta,MT_STOP,0x0F);
           if (end2) {
               dsyslog("stop mark is week, use next stop mark at (%i)", end2->position);
               end=end2;
           }
        }

        char *timeText = marks.IndexToHMSF(end->position,&macontext, ptr_cDecoder);
        if (timeText) {
            isyslog("using mark on position (%i) type 0x%X at %s as stop mark",end->position,  end->type, timeText);
            free(timeText);
        }
        marks.DelTill(end->position,false);

        if ( end->position < iStopA - 3*delta ) {    // last found stop mark too early, adding STOP mark at the end
                                                     // this can happen by audio channel change too if the next broadcast has also 6 channels
            if ( ( lastStart) && ( lastStart->position > end->position ) ) {
                isyslog("last STOP mark results in to short recording, set STOP at the end of the recording (%i)", lastiframe);
                MarkAdMark mark={};
                mark.Position=lastiframe;
                mark.Type=MT_ASSUMEDSTOP;
                AddMark(&mark);
            }
        }
    }
    else {
        dsyslog("no stop mark found, add stop mark at the last frame (%i)",lastiframe);
        MarkAdMark mark={};
        mark.Position=lastiframe;  // we are lost, add a end mark at the last iframe
        mark.Type=MT_ASSUMEDSTOP;
        AddMark(&mark);
    }
    iStop=iStopA=0;
    gotendmark=true;

//  only for debugging
    mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    dsyslog("-------------------------------------------------------");
}

void cMarkAdStandalone::CheckStart()
{
    dsyslog("-------------------------------------------------------");
    dsyslog("checking start (%i)", lastiframe);
    dsyslog("assumed start frame %i", iStartA);

//  only for debugging
    clMark *mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }

    clMark *begin=NULL;
    int delta=macontext.Video.Info.FramesPerSecond*MAXRANGE;

    macontext.Video.Options.IgnoreBlackScreenDetection=true;   // use black sceen setection only to find start mark

    for (short int stream=0; stream < MAXSTREAMS; stream++) {
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
        macontext.Info.Channels[stream]=macontext.Audio.Info.Channels[stream];

        if ((macontext.Config->DecodeAudio) && (macontext.Info.Channels[stream])) {
            if ((macontext.Info.Channels[stream]==6) && (macontext.Audio.Options.IgnoreDolbyDetection==false)) {
                isyslog("DolbyDigital5.1 audio whith 6 Channels in stream %i detected. logo/border/aspect detection disabled", stream);
                bDecodeVideo=false;
                macontext.Video.Options.IgnoreAspectRatio=true;
                macontext.Video.Options.IgnoreLogoDetection=true;
                marks.Del(MT_ASPECTSTART);
                marks.Del(MT_ASPECTSTOP);
                // start mark must be around iStartA
                begin=marks.GetAround(delta*4,iStartA,MT_CHANNELSTART);
                if (!begin) {          // previous recording had also 6 channels, try other marks
                    dsyslog("no audio channel start mark found");
                }
                else {
                    dsyslog("audio channel start mark found at %d", begin->position);
                    marks.Del(MT_LOGOSTART);   // we do not need the weaker marks if we found a MT_CHANNELSTART
                    marks.Del(MT_LOGOSTOP);
                    marks.Del(MT_HBORDERSTART);
                    marks.Del(MT_HBORDERSTOP);
                }
            }
            else {
                if (macontext.Info.DPid.Num) {
                    if ((macontext.Info.Channels[stream]) && (macontext.Audio.Options.IgnoreDolbyDetection==false))
                        isyslog("broadcast with %i audio channels of stream %i, disabling AC3 decoding",macontext.Info.Channels[stream], stream);
                    if (macontext.Audio.Options.IgnoreDolbyDetection==true)
                        isyslog("disabling AC3 decoding (from logo)");
                    macontext.Info.DPid.Num=0;
                    demux->DisableDPid();
                }
            }
        }
    }

    if (!begin) {    // try ascpect ratio mark
        clMark *aStart=marks.GetAround(chkSTART,chkSTART,MT_ASPECTSTART);   // check if ascpect ratio changed in start part
        clMark *aStop=marks.GetAround(chkSTART,chkSTART,MT_ASPECTSTOP);
        bool earlyAspectChange=false;
        if (aStart && aStop && (aStop->position > aStart->position)) {
            dsyslog("found very early aspect ratio change at (%i) and (%i)", aStart->position,  aStop->position);
            earlyAspectChange=true;
        }

        if ((macontext.Info.AspectRatio.Num) && (! earlyAspectChange) &&
           ((macontext.Info.AspectRatio.Num!= macontext.Video.Info.AspectRatio.Num) || (macontext.Info.AspectRatio.Den!= macontext.Video.Info.AspectRatio.Den)))
        {
            isyslog("video aspect description in info (%i:%i) wrong", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
            macontext.Info.AspectRatio.Num=macontext.Video.Info.AspectRatio.Num;
            macontext.Info.AspectRatio.Den=macontext.Video.Info.AspectRatio.Den;
        }
        if ((macontext.Info.AspectRatio.Num == 0) || (macontext.Video.Info.AspectRatio.Den == 0)) {
            isyslog("no video aspect ratio found in info file");
            macontext.Info.AspectRatio.Num=macontext.Video.Info.AspectRatio.Num;
            macontext.Info.AspectRatio.Den=macontext.Video.Info.AspectRatio.Den;
        }

        if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) {
            isyslog("HD Video with aspectratio of %i:%i detected", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
        }
        else {
            isyslog("SD Video with aspectratio of %i:%i detected", macontext.Info.AspectRatio.Num, macontext.Info.AspectRatio.Den);
            if (((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3))) {
                isyslog("logo/border detection disabled");
                bDecodeVideo=false;
                macontext.Video.Options.IgnoreAspectRatio=false;
                for (short int stream=0;stream<MAXSTREAMS; stream++) {
                    if (macontext.Info.Channels[stream]==6) {
                        macontext.Info.DPid.Num=0;
                        demux->DisableDPid();
                    }
                }
                macontext.Video.Options.IgnoreLogoDetection=true;
                marks.Del(MT_CHANNELSTART);
                marks.Del(MT_CHANNELSTOP);
                // start mark must be around iStartA
                begin=marks.GetAround(delta*4,iStartA,MT_ASPECTSTART);
                if (begin) {
                    dsyslog("MT_ASPECTSTART found at (%i)",begin->position);
                    if (begin->position > abs(iStartA)/4) {    // this is a valid start
                        marks.Del(MT_LOGOSTART);  // we found MT_ASPECTSTART, we do not need LOGOSTART
                        marks.Del(MT_LOGOSTOP);
                   }
                   else { // if there is a MT_ASPECTSTOP, delete all marks after this position
                       clMark *aStop = marks.GetNext(begin->position, MT_ASPECTSTOP);
                       if (aStop) {
                           dsyslog("found MT_ASPECTSTOP (%i), delete all weaker marks after", aStop->position);
                           marks.DelWeakFrom(aStop->position, aStop->type);
                       }
                   }

                }
                else {
                    dsyslog("no MT_ASPECTSTART found");   // previous is 4:3 too, try another start mark
                    clMark *begin2=marks.GetAround(iStartA,iStartA+delta,MT_START,0x0F);
                    if (begin2) {
                        dsyslog("using mark at position (%i) as start mark", begin2->position);
                        begin=begin2;
                    }
                }
            }
            else { // recording is 16:9 but maybe we can get a MT_ASPECTSTART mark if previous recording was 4:3
                begin=marks.GetAround(delta*3,iStartA,MT_ASPECTSTART);
                if (begin) {
                    dsyslog("use MT_ASPECTSTART found at (%i) because previous recording was 4:3",begin->position);
                    clMark *begin2=marks.GetAround(delta*4,begin->position,MT_LOGOSTART);  // do not use this mark if there is a later logo start mark
                    if (begin2 && begin2->position >  begin->position) {
                        dsyslog("found later MT_LOGOSTART, do not use MT_ASPECTSTART");
                        begin=NULL;
                    }
                }
            }
        }
    }

    if (!begin) {    // try horizontal border
        clMark *hStart=marks.GetAround(iStartA+delta,iStartA+delta+1,MT_HBORDERSTART);
        if (!hStart) {
            dsyslog("no horizontal border at start found, ignore horizontal border detection");
            macontext.Video.Options.ignoreHborder=true;
            clMark *hStop=marks.GetAround(iStartA+delta,iStartA+delta,MT_HBORDERSTOP);
            if (hStop) {
                int pos = hStop->position;
                char *comment=NULL;
                dsyslog("horizontal border stop without start mark found (%i), assume as start mark of the following recording", pos);
                marks.Del(pos);
                if (!asprintf(&comment,"assumed start from horizontal border stop (%d)", pos)) comment=NULL;
                begin=marks.Add(MT_ASSUMEDSTART, pos, comment);
                free(comment);
            }
        }
        else {
            dsyslog("horizontal border start found at (%i)", hStart->position);
            clMark *hStop=marks.GetAround(delta,hStart->position,MT_HBORDERSTOP);  // if there is a MT_HBORDERSTOP short after the MT_HBORDERSTART, MT_HBORDERSTART is not valid
            if ( (hStop) && (hStop->position > hStart->position)) {
                isyslog("horizontal border STOP (%i) short after horizontal border START (%i) found, this is not valid, delete marks",hStop->position,hStart->position);
                marks.Del(hStart);
                marks.Del(hStop);

            }
            else {
                if (hStart->position != 0) {  // position 0 is a hborder previous recording
                    dsyslog("delete VBORDER marks if any");
                    marks.Del(MT_VBORDERSTART);
                    marks.Del(MT_VBORDERSTOP);
                    begin = hStart;   // found valid horizontal border start mark
                }
            }
        }
    }

    if (!begin) {    // try vertical border
        clMark *vStart=marks.GetAround(iStartA+delta,iStartA+delta+1,MT_VBORDERSTART);
        if (!vStart) {
            dsyslog("no vertical border at start found, ignore vertical border detection");
            macontext.Video.Options.ignoreVborder=true;
            clMark *vStop=marks.GetAround(iStartA+delta,iStartA+delta,MT_VBORDERSTOP);
            if (vStop) {
                int pos = vStop->position;
                char *comment=NULL;
                dsyslog("vertical border stop without start mark found (%i), assume as start mark of the following recording", pos);
                marks.Del(pos);
                if (!asprintf(&comment,"assumed start from vertical border stop (%d)", pos)) comment=NULL;
                begin=marks.Add(MT_ASSUMEDSTART, pos, comment);
                free(comment);
            }
        }
        else {
            dsyslog("vertical border start found at (%i)", vStart->position);
            clMark *vStop=marks.GetAround(delta,vStart->position,MT_VBORDERSTOP);  // if there is a MT_VBORDERSTOP short after the MT_VBORDERSTART, MT_VBORDERSTART is not valid
            if ( (vStop) && (vStop->position > vStart->position)) {
                isyslog("vertical border STOP (%i) short after vertical border START (%i) found, this is not valid, delete marks",vStop->position,vStart->position);
                marks.Del(vStart);
                marks.Del(vStop);

            }
            else {
                if (vStart->position != 0) {  // position 0 is a vborder previous recording
                    dsyslog("delete HBORDER marks if any");
                    marks.Del(MT_HBORDERSTART);
                    marks.Del(MT_HBORDERSTOP);
                    begin = vStart;   // found valid horizontal border start mark
                }
            }

            begin = vStart;
        }
    }

    if (!begin) {   // try logo start mark
        clMark *lStart=marks.GetAround(iStartA+delta,iStartA,MT_LOGOSTART);
        if (!lStart) {
            dsyslog("no logo start mark found");
        }
        else {
            char *timeText = marks.IndexToHMSF(lStart->position,&macontext, ptr_cDecoder);
            if (timeText) {
                dsyslog("logo start mark found on position (%i) at %s", lStart->position, timeText);
                free(timeText);
            }
            begin=lStart;   // found valid logo start mark
        }
    }

    if (begin && ((begin->position == 0) || ((begin->type == MT_LOGOSTART) && (begin->position  < iStart/8)))) { // we found the correct type but the mark is too early because the previous recording has same type
        dsyslog("start mark (%i) dropped because it is too early", begin->position);
        begin = NULL;
    }

    if (!begin) {    // try anything
        begin=marks.GetAround(iStartA+delta,iStartA,MT_START,0x0F);
        if (begin) {
            dsyslog("found start mark at (%i)", begin->position);
        }
    }

    clMark *beginRec=marks.GetAround(delta,1,MT_RECORDINGSTART);  // do we have an incomplete recording ?
    if (beginRec) {
        dsyslog("found MT_RECORDINGSTART at %i, replace start mark", beginRec->position);
        begin = beginRec;
    }

    if (begin) {
        marks.DelTill(begin->position);    // delete all marks till start mark
        CalculateCheckPositions(begin->position);
        char *timeText = marks.IndexToHMSF(begin->position,&macontext, ptr_cDecoder);
        if (timeText) {
            isyslog("using mark on position %i type 0x%X at %s as start mark", begin->position, begin->type, timeText);
            free(timeText);
        }


        if ((begin->type==MT_VBORDERSTART) || (begin->type==MT_HBORDERSTART)) {
            isyslog("found %s borders, logo detection disabled",(begin->type==MT_HBORDERSTART) ? "horizontal" : "vertical");
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

        clMark *mark=marks.GetFirst();   // delete all black screen marks because they are weak, execpt the start mark
        while (mark)
        {
            if (( (mark->type == MT_NOBLACKSTART) || (mark->type == MT_NOBLACKSTOP) ) && (mark->position > begin->position) ) {
                dsyslog("delete black screen mark at position (%i)", mark->position);
                clMark *tmp=mark;
                mark=mark->Next();
                marks.Del(tmp);
                continue;
            }
            mark=mark->Next();
        }
        if (begin->type == MT_LOGOSTART) {
            clMark *mark=marks.GetFirst();
            while (mark)
            {
                if ( (mark->type == MT_LOGOSTART) && (mark->position > begin->position) && (mark->position <= chkSTART)) {
                    if ( mark->Next() && (mark->Next()->type == MT_LOGOSTOP)) {
                        dsyslog("delete logo mark at position (%i),(%i) between STARTLOGO (%i) and chkSTART (%i)", mark->position, mark->Next()->position, begin->position, chkSTART);
                        clMark *tmp=mark;
                        mark=mark->Next()->Next();
                        marks.Del(tmp->Next());
                        marks.Del(tmp);
                        continue;
                    }
                }
                mark=mark->Next();
            }
        }
    }
    else
    {
        //fallback
        dsyslog("no valid start mark found, assume start time at pre recording time");
        marks.DelTill(chkSTART);
        MarkAdMark mark={};
        mark.Position=iStart;
        mark.Type=MT_ASSUMEDSTART;
        AddMark(&mark);
        CalculateCheckPositions(iStart);
    }

    //  only for debugging
    mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    dsyslog("-------------------------------------------------------");
    iStart=0;
    return;
}

void cMarkAdStandalone::CheckMarks()            // cleanup marks that make no sense
{
    dsyslog("-------------------------------------------------------");
    isyslog("cleanup marks");
    //  only for debugging
    clMark *mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    mark=marks.GetFirst();
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

        if ((mark->type==MT_LOGOSTART) && mark->Next() && mark->Next()->type==MT_LOGOSTOP)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*60);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF)
            {
                double distance=(mark->Next()->position-mark->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp=mark;
                mark=mark->Next()->Next();
                marks.Del(tmp->Next());
                if (marks.GetFirst()->position == tmp->position) mark=marks.GetFirst(); // do not delete start mark, restart check from first mark
                else marks.Del(tmp);
                continue;
            }
        }

        if ((mark->type==MT_LOGOSTOP) && mark->Next() && mark->Next()->type==MT_LOGOSTART)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*50);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF)
            {
                double distance=(mark->Next()->position-mark->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between logo STOP and START too short (%.1fs), deleting %i,%i", distance, mark->position, mark->Next()->position);
                clMark *tmp=mark;
                mark=marks.GetFirst();    // restart check from start
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
    mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    dsyslog("-------------------------------------------------------");
}

void cMarkAdStandalone::AddMark(MarkAdMark *Mark)
{
    if (!Mark) return;
    if (!Mark->Type) return;
    if ((macontext.Config) && (macontext.Config->logoExtraction!=-1)) return;
    if (gotendmark) return;

    char *comment=NULL;
    switch (Mark->Type)
    {
    case MT_ASSUMEDSTART:
        if (asprintf(&comment,"assuming start (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_ASSUMEDSTOP:
        if (asprintf(&comment,"assuming stop (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_NOBLACKSTART:
        if (asprintf(&comment,"detected end of black screen (%i)*",Mark->Position)==-1) comment=NULL;
        break;
    case MT_NOBLACKSTOP:
        if (asprintf(&comment,"detected start of black screen (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_LOGOSTART:
        if (asprintf(&comment,"detected logo start (%i)*",Mark->Position)==-1) comment=NULL;
        break;
    case MT_LOGOSTOP:
        if (asprintf(&comment,"detected logo stop (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_HBORDERSTART:
        if (asprintf(&comment,"detected start of horiz. borders (%i)*",Mark->Position)==-1) comment=NULL;
        break;
    case MT_HBORDERSTOP:
        if (asprintf(&comment,"detected stop of horiz. borders (%i)", Mark->Position)==-1) comment=NULL;
        break;
    case MT_VBORDERSTART:
        if (asprintf(&comment,"detected start of vert. borders (%i)*", Mark->Position)==-1) comment=NULL;
        break;
    case MT_VBORDERSTOP:
        if (asprintf(&comment,"detected stop of vert. borders (%i)", Mark->Position)==-1) comment=NULL;
        break;
    case MT_ASPECTSTART:
        if (!Mark->AspectRatioBefore.Num) {
            if (asprintf(&comment,"aspectratio start with %i:%i (%i)*", Mark->AspectRatioAfter.Num,
                                                                        Mark->AspectRatioAfter.Den,
                                                                        Mark->Position)==-1) comment=NULL;
        }
        else {
            if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)*",
                         Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                         Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den,
                         Mark->Position)==-1) comment=NULL;
            if ((macontext.Config->autoLogo > 0) &&( Mark->Position > 0) && bDecodeVideo) {
                isyslog("logo detection reenabled, trying to find a logo from this position");
                macontext.Video.Options.IgnoreLogoDetection=false;
                macontext.Video.Options.WeakMarksOk=false;
            }
        }
        break;
    case MT_ASPECTSTOP:
        if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)",
                     Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                     Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den,
                     Mark->Position)==-1) comment=NULL;
        if ((macontext.Config->autoLogo > 0) && (Mark->Position > 0) && bDecodeVideo) {
            isyslog("logo detection reenabled, trying to find a logo from this position");
            macontext.Video.Options.IgnoreLogoDetection=false;
            macontext.Video.Options.WeakMarksOk=false;
        }
        break;
    case MT_CHANNELSTART:
        if (asprintf(&comment,"audio channel change from %i to %i (%i)*",
                     Mark->ChannelsBefore,Mark->ChannelsAfter,
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_CHANNELSTOP:
        if (asprintf(&comment,"audio channel change from %i to %i (%i)",
                     Mark->ChannelsBefore,Mark->ChannelsAfter,
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_RECORDINGSTART:
        if (asprintf(&comment,"start of recording (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_RECORDINGSTOP:
        if (asprintf(&comment,"stop of recording (%i)",Mark->Position)==-1) comment=NULL;
        break;
    }

    char *timeText = marks.IndexToHMSF(Mark->Position,&macontext, ptr_cDecoder);
    if (comment) isyslog("%s at %s",comment, timeText);
    free(timeText);

    clMark *prev=marks.GetLast();
    if (prev) {
        if (prev->position==Mark->Position) {
            if (prev->type>Mark->Type)
            {
                isyslog("previous mark (%i) type 0x%X stronger than actual mark on same position, deleting (%i) type 0x%X", prev->position, prev->type, Mark->Position, Mark->Type);
                if (comment) free(comment);
                return;
            }
            else
            {
                isyslog("actual mark (%d) type 0x%X stronger then previous mark on same position, deleting (%i) type 0x%X",Mark->Position, Mark->Type, prev->position, prev->type);
                marks.Del(prev);
            }
        }
    }

    if (((Mark->Type & 0x0F)==MT_START) && (!iStart) && (Mark->Position < (abs(iStopA) - 2*macontext.Video.Info.FramesPerSecond*MAXRANGE )))
    {
        clMark *prev=marks.GetPrev(Mark->Position,(Mark->Type & 0xF0)|MT_STOP);
        if (prev)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*10);    // maybe this is only ia short logo detection failure
            if ( (Mark->Position - prev->position) < MARKDIFF )
            {
                double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between STOP and START too short (%.1fs), deleting %i,%i",distance, prev->position,Mark->Position);
                if (!macontext.Video.Options.WeakMarksOk) inBroadCast=true;
                marks.Del(prev);
                if (comment) free(comment);
                return;
            }
        }
    }

    if (Mark->Type==MT_LOGOSTOP)
    {
        clMark *prev=marks.GetPrev(Mark->Position,MT_LOGOSTART);
        if (prev)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*30);    // maybe this is only ia short logo detection failure
            if ( (Mark->Position - prev->position) < MARKDIFF )
            {
                double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i",distance, prev->position,Mark->Position);
                if (!macontext.Video.Options.WeakMarksOk) inBroadCast=false;
                marks.Del(prev);
                if (comment) free(comment);
                return;
            }
        }
    }

    if (((Mark->Type & 0x0F)==MT_STOP) && (!iStart) && (Mark->Position < abs(iStopA) - macontext.Video.Info.FramesPerSecond*MAXRANGE ))
    {
        clMark *prev=marks.GetPrev(Mark->Position,(Mark->Type & 0xF0)|MT_START);
        if (prev)
        {
            int MARKDIFF;
            if ((Mark->Type & 0xF0)==MT_LOGOCHANGE)
            {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*120);
            }
            else
            {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*90);
            }
            if ((Mark->Position - prev->position) < MARKDIFF)
            {
                double distance=(Mark->Position - prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance between START and STOP too short (%.1fs), deleting %i,%i",distance,prev->position,Mark->Position);
                if (!macontext.Video.Options.WeakMarksOk) inBroadCast=false;
                marks.Del(prev);
                if (comment) free(comment);
                return;
            }
        }
    }

    prev=marks.GetLast();
    if (prev) {
        if ((prev->type & 0x0F)==(Mark->Type & 0x0F)) {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*30);
            int diff=abs(Mark->Position-prev->position);
            if (diff<MARKDIFF) {
                if (prev->type>Mark->Type) {
                    isyslog("previous mark (%i) type 0x%X stronger than actual mark, deleting (%i) type 0x%X", prev->position, prev->type, Mark->Position, Mark->Type);
                    if (comment) free(comment);
                    return;
                }
                else {
                    isyslog("actual mark (%i) type 0x%X stronger then previous mark, deleting %i type 0x%X",Mark->Position, Mark->Type, prev->position, prev->type);
                    marks.Del(prev);
                }
            }
        }
    }

    if (!macontext.Video.Options.WeakMarksOk) {
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
    }
    marks.Add(Mark->Type,Mark->Position,comment);
    if (comment) free(comment);
    tsyslog("cMarkAdStandalone::AddMark(): status inBroadCast: %i", inBroadCast);
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
    do
    {
        struct stat statbuf;
        if (stat(indexFile,&statbuf)==-1) {
            if (!macontext.Config->GenIndex) {
                esyslog("failed to stat %s",indexFile);
            }
            return;
        }

        int maxframes=statbuf.st_size/8;
        if (maxframes<(framecnt+200))
        {
            if ((difftime(time(NULL),statbuf.st_mtime))>=WAITTIME)
            {
                if (length && startTime)
                {
                    if (time(NULL)>(startTime+(time_t) length))
                    {
                        // "old" recording
//                        tsyslog("assuming old recording, now>startTime+length");
                        return;
                    }
                    else
                    {
                        sleepcnt=0;
                        if (!iwaittime) esyslog("recording interrupted, waiting for continuation...");
                        iwaittime+=WAITTIME;
                    }
                }
                else
                {
                    // "old" recording
                    dsyslog("assuming old recording - no length and startTime");
                    return;
                }
            }
            marks.Save(directory,&macontext, ptr_cDecoder, isTS);
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
            if (sleepcnt>=2)
            {
                esyslog("no new data after %is, skipping wait!",
                        waittime);
                notenough=false; // something went wrong?
            }
        }
        else
        {
            if (iwaittime)
            {
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

void cMarkAdStandalone::ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos)
{
    if (!NewPos) return;
    if (!Mark1) return;
    if (!*Mark1) return;
    bool save=false;

    if ((*Mark1)->position!=NewPos->FrameNumberBefore)
    {
        char *buf=NULL;
        char *timeTextBefore = marks.IndexToHMSF((*Mark1)->position,&macontext, ptr_cDecoder);
        char *timeTextNewPos = marks.IndexToHMSF(NewPos->FrameNumberBefore,&macontext, ptr_cDecoder);
        if (asprintf(&buf,"overlap before %i at %s, moved to %i at %s",(*Mark1)->position, timeTextBefore,
                     NewPos->FrameNumberBefore, timeTextNewPos)==-1) return;
        isyslog("%s",buf);
        marks.Del(*Mark1);
        *Mark1=marks.Add(MT_MOVED,NewPos->FrameNumberBefore,buf);
        free(buf);
        free(timeTextBefore);
        free(timeTextNewPos);
        save=true;
    }

    if (Mark2 && (*Mark2) && (*Mark2)->position!=NewPos->FrameNumberAfter)
    {
        char *buf=NULL;
        char *timeTextBefore = marks.IndexToHMSF((*Mark2)->position,&macontext, ptr_cDecoder);
        char *timeTextNewPos = marks.IndexToHMSF(NewPos->FrameNumberAfter,&macontext, ptr_cDecoder);
        if (asprintf(&buf,"overlap after %i at %s, moved to %i at %s",(*Mark2)->position, timeTextBefore,
                     NewPos->FrameNumberAfter, timeTextNewPos)==-1) return;
        isyslog("%s",buf);
        marks.Del(*Mark2);
        *Mark2=marks.Add(MT_MOVED,NewPos->FrameNumberAfter,buf);
        free(buf);
        free(timeTextBefore);
        free(timeTextNewPos);
        save=true;
    }
    if (save) marks.Save(directory,&macontext,ptr_cDecoder,isTS,true);
}

bool cMarkAdStandalone::ProcessFile2ndPass(clMark **Mark1, clMark **Mark2,int Number, off_t Offset,
        int Frame, int Frames)
{
    if (!directory) return false;
    if (!Number) return false;
    if (!Frames) return false;
    if (!decoder) return false;
    if (!Mark1) return false;
    if (!*Mark1) return false;

    int pn; // process number 1=start mark, 2=before mark, 3=after mark
    if (Mark1 && Mark2)
    {
        if (!(*Mark1) || !(*Mark2)) return false;
        if (*Mark1==*Mark2) pn=mSTART;
        if (*Mark1!=*Mark2) pn=mAFTER;
    }
    else
    {
        pn=mBEFORE;
    }

    if (!Reset(false))
    {
        // reset all, but marks
        esyslog("failed resetting state");
        return false;
    }
    iframe=Frame;
    int actframe=Frame;
    int framecounter=0;
    int pframe=-1;
    MarkAdPos *pos=NULL;

    while (framecounter<Frames)
    {
        if (abortNow) return false;

        const int datalen=319976;
        uchar data[datalen];

        char *fbuf;
        if (isTS)
        {
            if (asprintf(&fbuf,"%s/%05i.ts",directory,Number)==-1) return false;
        }
        else
        {
            if (asprintf(&fbuf,"%s/%03i.vdr",directory,Number)==-1) return false;
        }

        int f=open(fbuf,O_RDONLY);
        free(fbuf);
        if (f==-1) return false;

        int dataread;
        if (pn==mSTART)
        {
            dsyslog("processing file %05i (start mark)",Number);
        }
        else
        {
            if (pn==mBEFORE)
            {
                dsyslog("processing file %05i (before mark %i)",Number,(*Mark1)->position);
            }
            else
            {
                dsyslog("processing file %05i (after mark %i)",Number,(*Mark2)->position);
            }
        }

        if (lseek(f,Offset,SEEK_SET)!=Offset)
        {
            close(f);
            return false;
        }

        while ((dataread=read(f,data,datalen))>0)
        {
            if (abortNow) break;

            if ((demux) && (video) && (decoder) && (streaminfo))
            {
                uchar *tspkt = data;
                int tslen = dataread;

                while (tslen>0)
                {
                    int len=demux->Process(tspkt,tslen,&pkt);
                    if (len<0)
                    {
                        esyslog("error demuxing file");
                        abortNow=true;
                        break;
                    }
                    else
                    {
                        if ((pkt.Data) && ((pkt.Type & PACKET_MASK)==PACKET_VIDEO))
                        {
                            bool dRes=false;
                            if (streaminfo->FindVideoInfos(&macontext,pkt.Data,pkt.Length))
                            {
                                actframe++;
                                framecnt2++;

                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                                {
                                    lastiframe=iframe;
                                    iframe=actframe-1;
                                    dRes=true;
                                }
                            }
                            if (pn>mSTART) dRes=decoder->DecodeVideo(&macontext,pkt.Data,pkt.Length);
                            if (dRes)
                            {
                                if (pframe!=lastiframe)
                                {
                                    if (pn>mSTART) pos=video->ProcessOverlap(lastiframe,Frames,(pn==mBEFORE),
                                                       (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
                                    framecounter++;
                                }
                                if ((pos) && (pn==mAFTER))
                                {
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

            if (abortNow)
            {
                close(f);
                return false;
            }

            if (framecounter>Frames)
            {
                break;
            }

        }
        close(f);
        Number++;
        Offset=0;
    }
    return true;
}

bool cMarkAdStandalone::ProcessMark2ndPass(clMark **mark1, clMark **mark2) {

    if (!mark1) return false;
    if (!*mark1) return false;
    if (!mark2) return false;
    if (!*mark2) return false;

    long int iFrameCount=0;
    int fRange=0;
    MarkAdPos *ptr_MarkAdPos=NULL;

    if (!Reset(false))
    {
        // reset all, but marks
        esyslog("failed resetting state");
        return false;
    }

    fRange=macontext.Video.Info.FramesPerSecond*120;     // 40s + 80s
    int fRangeBegin=(*mark1)->position-fRange;           // 120 seconds before first mark
    if (fRangeBegin<0) fRangeBegin=0;                    // but not before beginning of broadcast
    fRangeBegin=ptr_cDecoder->GetIFrameBefore(fRangeBegin);
    if (!fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameBefore failed for frame (%i)", fRangeBegin);
        return false;
    }
    if (!ptr_cDecoder->SeekToFrame(fRangeBegin)) {
        esyslog("cDecoder: could not seek to frame (%i)", fRangeBegin);
        return false;
    }
    iFrameCount=ptr_cDecoder->GetIFrameRangeCount(fRangeBegin, (*mark1)->position);
    if (iFrameCount<=0) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameRangeCount failed at range (%i,%i))", fRangeBegin, (*mark1)->position);
            return false;
    }
    while (ptr_cDecoder->GetFrameNumber() <= (*mark1)->position ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetNextFrame failed at frame (%li)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())  // if we have interlaced video this is expected, we have to read the next half picture
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() before mark GetFrameInfo failed at frame (%li)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos=video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(),iFrameCount,true,(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
   }

    fRange=macontext.Video.Info.FramesPerSecond*320; // 160s + 160s
    fRangeBegin=ptr_cDecoder->GetIFrameBefore((*mark2)->position);
    if (!fRangeBegin) {
        dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameBefore failed for frame (%i)", fRangeBegin);
        return false;
    }
    int fRangeEnd=(*mark2)->position+fRange;         // 320 seconds after second mark
    if (!ptr_cDecoder->SeekToFrame((*mark2)->position)) {
        esyslog("cDecoder: could not seek to frame (%i)", fRangeBegin);
        return false;
    }
    iFrameCount=ptr_cDecoder->GetIFrameRangeCount(fRangeBegin, fRangeEnd)-2;
    if (iFrameCount<=0) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetIFrameRangeCount failed at range (%i,%i))", fRangeBegin, (*mark1)->position);
            return false;
    }
    while (ptr_cDecoder->GetFrameNumber() <= fRangeEnd ) {
        if (abortNow) return false;
        if (!ptr_cDecoder->GetNextFrame()) {
            dsyslog("cMarkAdStandalone::ProcessMark2ndPass() GetNextFrame failed at frame (%li)", ptr_cDecoder->GetFrameNumber());
            return false;
        }
        if (!ptr_cDecoder->isVideoPacket()) continue;
        if (!ptr_cDecoder->GetFrameInfo(&macontext)) {
            if (ptr_cDecoder->isVideoIFrame())
                tsyslog("cMarkAdStandalone::ProcessMark2ndPass() after mark GetFrameInfo failed at frame (%li)", ptr_cDecoder->GetFrameNumber());
            continue;
        }
        if (ptr_cDecoder->isVideoIFrame()) {
            ptr_MarkAdPos=video->ProcessOverlap(ptr_cDecoder->GetFrameNumber(),iFrameCount,false,(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264));
        }
        if (ptr_MarkAdPos) dsyslog("cMarkAdStandalone::ProcessMark2ndPass found overlap in frames (%i,%i)", ptr_MarkAdPos->FrameNumberBefore, ptr_MarkAdPos->FrameNumberAfter);
        if (ptr_MarkAdPos) {
            // found overlap
            ChangeMarks(mark1,mark2,ptr_MarkAdPos);
            return true;
        }
    }
    return false;
}

void cMarkAdStandalone::MarkadCut() {
    cEncoder* ptr_cEncoder = NULL;
    if (abortNow) return;
    if (!ptr_cDecoder) {
        isyslog("video cut function only supported with --cDecoder");
        return;
    }
    dsyslog("-------------------------------------------------------");
    dsyslog("start MarkadCut()");
    if (marks.Count()<2) {
        isyslog("need at least 2 marks to cut Video");
        return; // we cannot do much without marks
    }
    dsyslog("final marks are:");
    //  only for debugging
    clMark *mark=marks.GetFirst();
    while (mark) {
        char *timeText = marks.IndexToHMSF(mark->position,&macontext, ptr_cDecoder);
        if (timeText) {
            dsyslog("mark at position %6i type 0x%X at %s", mark->position, mark->type, timeText);
            free(timeText);
        }
        mark=mark->Next();
    }
    clMark *StartMark= marks.GetFirst();
    if (((StartMark->type & 0x0F) != 1) && (StartMark->type != MT_MOVED)) {
        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
        return;
    }
    clMark *StopMark = StartMark->Next();
    if (((StopMark->type & 0x0F) != 2) && (StopMark->type != MT_MOVED)) {
        esyslog("got invalid stop mark at (%i) type 0x%X", StopMark->position, StopMark->type);
        return;
    }
    ptr_cEncoder = new cEncoder(macontext.Config->threads, macontext.Config->ac3ReEncode);
    if ( ! ptr_cEncoder->OpenFile(directory,ptr_cDecoder)) {
        esyslog("failed to open output file");
        return;
    }
    while(ptr_cDecoder->DecodeDir(directory)) {
        while(ptr_cDecoder->GetNextFrame()) {
            if  (ptr_cDecoder->GetFrameNumber() < StartMark->position) ptr_cDecoder->SeekToFrame(StartMark->position);
            if  (ptr_cDecoder->GetFrameNumber() > StopMark->position) {
                if (StopMark->Next() && StopMark->Next()->Next()) {
                    StartMark = StopMark->Next();
                    if (((StartMark->type & 0x0F) != 1) && (StartMark->type != MT_MOVED)) {
                        esyslog("got invalid start mark at (%i) type 0x%X", StartMark->position, StartMark->type);
                        return;
                    }
                    StopMark = StartMark->Next();
                    if (((StopMark->type & 0x0F) != 2) && (StopMark->type != MT_MOVED)) {
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
//            dsyslog("--- Framenumber %ld", ptr_cDecoder->GetFrameNumber());
            if ( ! ptr_cEncoder->WritePacket(pkt, ptr_cDecoder) ) {
                isyslog("failed to write frame %ld to output stream", ptr_cDecoder->GetFrameNumber());
            }
            if (abortNow) {
                if (ptr_cDecoder) delete ptr_cDecoder;
                if (ptr_cEncoder) {
                    ptr_cEncoder->CloseFile();
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
    dsyslog("end MarkadCut() at frame %ld", ptr_cDecoder->GetFrameNumber());
    if (ptr_cEncoder) {
        delete ptr_cEncoder;
        ptr_cEncoder = NULL;
    }
}


void cMarkAdStandalone::Process2ndPass()
{
    if (abortNow) return;
    if (duplicate) return;
    if (!decoder) return;
    if (!length) return;
    if (!startTime) return;
    if (time(NULL)<(startTime+(time_t) length)) return;

    dsyslog("-------------------------------------------------------");
    dsyslog("start 2ndPass");

    if (!macontext.Video.Info.FramesPerSecond)
    {
        isyslog("WARNING: assuming fps of 25");
        macontext.Video.Info.FramesPerSecond=25;
    }

    if (!marks.Count())
    {
        marks.Load(directory,macontext.Video.Info.FramesPerSecond,isTS);
    }

    bool infoheader=false;
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

    while ((p1) && (p2))
    {
        if (!infoheader)
        {
            isyslog("2nd pass");
            infoheader=true;
        }
        off_t offset;
        int number,frame,iframes;
        int frange=macontext.Video.Info.FramesPerSecond*120; // 40s + 80s
        int frange_begin=p1->position-frange; // 120 seconds before first mark
        if (frange_begin<0) frange_begin=0; // but not before beginning of broadcast

        if (ptr_cDecoder) {
            if (!ProcessMark2ndPass(&p1,&p2)) {
                dsyslog("cDecoder: ProcessMark2ndPass no overlap found for marks at frames (%i) and (%i)", p1->position, p2->position);
            }
        }
        else {
            if (marks.ReadIndex(directory,isTS,frange_begin,frange,&number,&offset,&frame,&iframes))
            {
                if (!ProcessFile2ndPass(&p1,NULL,number,offset,frame,iframes)) break;

                frange=macontext.Video.Info.FramesPerSecond*320; // 160s + 160s
                if (marks.ReadIndex(directory,isTS,p2->position,frange,&number,&offset,&frame,&iframes))
                {
                    if (!ProcessFile2ndPass(&p1,&p2,number,offset,frame,iframes)) break;
                }
            }
            else
            {
                esyslog("error reading index");
                return;
            }
        }
        p1=p2->Next();
        if (p1)
        {
            p2=p1->Next();
        }
        else
        {
            p2=NULL;
        }
    }
    dsyslog("end 2ndPass");
}


bool cMarkAdStandalone::ProcessFile(int Number)
{
    if (!directory) return false;
    if (!Number) return false;

    CheckIndexGrowing();

    if (abortNow) return false;

    const int datalen=319976;
    uchar data[datalen];

    char *fbuf;
    if (isTS)
    {
        if (asprintf(&fbuf,"%s/%05i.ts",directory,Number)==-1) {
            esyslog("failed to allocate string, out of memory?");
            return false;
        }
    }
    else
    {
        if (asprintf(&fbuf,"%s/%03i.vdr",directory,Number)==-1) {
            esyslog("failed to allocate string, out of memory?");
            return false;
        }
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
    free(fbuf);

    int dataread;
    dsyslog("processing file %05i",Number);

    int pframe=-1;
    demux->NewFile();

again:
    while ((dataread=read(f,data,datalen))>0)
    {
        if (abortNow) break;
        if ((demux) && (video) && (streaminfo))
        {
            uchar *tspkt = data;
            int tslen = dataread;
            while (tslen>0)
            {
                int len=demux->Process(tspkt,tslen,&pkt);
                if (len<0)
                {
                    esyslog("error demuxing");
                    abortNow=true;
                    break;
                }
                else
                {
                    if (pkt.Data)
                    {
                        if ((pkt.Type & PACKET_MASK)==PACKET_VIDEO)
                        {
                            bool dRes=false;
                            if (streaminfo->FindVideoInfos(&macontext,pkt.Data,pkt.Length))
                            {
                                if ((macontext.Video.Info.Height) && (!noticeHEADER))
                                {
                                    if ((!isTS) && (!noticeVDR_VID))
                                    {
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

                                if (!framecnt)
                                {
                                    CalculateCheckPositions(tStart*macontext.Video.Info.FramesPerSecond);
                                }
                                if (macontext.Config->GenIndex)
                                {
                                    marks.WriteIndex(directory,isTS,demux->Offset(),macontext.Video.Info.Pict_Type,Number);
                                }
                                framecnt++;
                                if ((macontext.Config->logoExtraction!=-1) && (framecnt>=256))
                                {
                                    isyslog("finished logo extraction, please check /tmp for pgm files");
                                    abortNow=true;
                                    if (f!=-1) close(f);
                                    return true;
                                }

                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                                {
                                    lastiframe=iframe;
                                    if ((iStart<0) && (lastiframe>-iStart)) iStart=lastiframe;
                                    if ((iStop<0) && (lastiframe>-iStop))
                                    {
                                        iStop=lastiframe;
                                        iStopinBroadCast=inBroadCast;
                                    }
                                    if ((iStopA<0) && (lastiframe>-iStopA))
                                    {
                                        iStopA=lastiframe;
                                    }
                                    iframe=framecnt-1;
                                    dRes=true;
                                }
                            }
                            if ((decoder) && (bDecodeVideo))
                                dRes=decoder->DecodeVideo(&macontext,pkt.Data,pkt.Length);
                            if (dRes)
                            {
                                if (pframe!=lastiframe)
                                {
                                    MarkAdMarks *vmarks=video->Process(lastiframe,iframe);
                                    if (vmarks)
                                    {
                                        for (int i=0; i<vmarks->Count; i++)
                                        {
                                            AddMark(&vmarks->Number[i]);
                                        }
                                    }
//                                    if (lastiframe == 14716) SaveFrame(lastiframe);  // TODO: JUST FOR DEBUGGING!
                                    if (iStart>0)
                                    {
                                        if ((inBroadCast) && (lastiframe>chkSTART)) CheckStart();
                                    }
                                    if ((lastiframe>iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE) &&
                                                                (macontext.Video.Options.IgnoreBlackScreenDetection)) {
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

                        if ((pkt.Type & PACKET_MASK)==PACKET_AC3)
                        {
                            if (streaminfo->FindAC3AudioInfos(&macontext, pkt.Data, pkt.Length))
                            {
                                if ((!isTS) && (!noticeVDR_AC3))
                                {
                                    isyslog("found AC3 (0x%02X)",pkt.Stream);
                                    noticeVDR_AC3=true;
                                }
                                if ((framecnt-iframe)<=3)
                                {
                                    MarkAdMark *amark=audio->Process(lastiframe,iframe);
                                    if (amark)
                                    {
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
        if ((gotendmark) && (!macontext.Config->GenIndex))
        {
            if (f!=-1) close(f);
            return true;
        }

        CheckIndexGrowing();
        if (abortNow)
        {
            if (f!=-1) close(f);
            return false;
        }
    }
    if ((dataread==-1) && (errno==EINTR)) goto again; // i know this is ugly ;)

    close(f);
    return true;
}

bool cMarkAdStandalone::Reset(bool FirstPass)
{
    bool ret=true;
    if (FirstPass) framecnt=0;
    lastiframe=0;
    iframe=0;

    gotendmark=false;

    memset(&pkt,0,sizeof(pkt));

    chkSTART=chkSTOP=INT_MAX;

    if (FirstPass)
    {
        marks.DelAll();
        marks.CloseIndex(directory,isTS);
    }

    macontext.Video.Info.Pict_Type=0;
    macontext.Video.Info.AspectRatio.Den=0;
    macontext.Video.Info.AspectRatio.Num=0;
    memset(macontext.Audio.Info.Channels, 0, sizeof(macontext.Audio.Info.Channels));

    if (decoder)
    {
        ret=decoder->Clear();
    }
    if (streaminfo) streaminfo->Clear();
    if (demux) demux->Clear();
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
                return(false);
            }
            if (!macontext.Video.Data.Valid) {
                isyslog("cMarkAdStandalone::ProcessFrame faild to get video data of frame (%li)", ptr_cDecoder->GetFrameNumber());
                return(false);
            }

            if ( !restartLogoDetectionDone && (lastiframe > (iStopA-macontext.Video.Info.FramesPerSecond*MAXRANGE)) &&
                                     ((macontext.Video.Options.IgnoreBlackScreenDetection) || (macontext.Video.Options.IgnoreLogoDetection))) {
                    isyslog("restart logo and black screen detection at frame (%li)",ptr_cDecoder->GetFrameNumber());
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
            MarkAdMarks *vmarks=video->Process(lastiframe,iframe);
            if (vmarks) {
                for (int i=0; i<vmarks->Count; i++) {
                    AddMark(&vmarks->Number[i]);
                }
            }

            if (iStart>0) {
                if ((inBroadCast) && (lastiframe>chkSTART)) CheckStart();
            }
            if ((iStop>0) && (iStopA>0)) {
                if (lastiframe>chkSTOP) {
                    if (iStart != 0) {
                        dsyslog("still no chkStart called, doing it now");
                        CheckStart();
                    }
                    CheckStop();
                    return(false);
                }
            }
        }
        if(ptr_cDecoder->isAudioAC3Packet()) {
             MarkAdMark *amark=audio->Process(lastiframe,iframe);
            if (amark) AddMark(amark);
        }
    }
    return(true);
}

void cMarkAdStandalone::ProcessFile()
{
    dsyslog("start processing files");
    if (macontext.Config->use_cDecoder) {
        ptr_cDecoder = new cDecoder(macontext.Config->threads);
        CheckIndexGrowing();
        while(ptr_cDecoder && ptr_cDecoder->DecodeDir(directory)) {
            if (abortNow) {
                if (ptr_cDecoder) {
                    delete ptr_cDecoder;
                    ptr_cDecoder = NULL;
                }
                break;
            }
            if(ptr_cDecoder->GetFrameNumber() < 0) {
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
                        delete ptr_cDecoder;
                        ptr_cDecoder = NULL;
                    }
                    break;
                }
                if (! cMarkAdStandalone::ProcessFrame(ptr_cDecoder)) break;
                CheckIndexGrowing();
            }
        }
    }
    else {
        for (int i=1; i<=MaxFiles; i++)
        {
            if (abortNow) break;
            if (!ProcessFile(i)) break;
            if ((gotendmark) && (!macontext.Config->GenIndex)) break;
        }
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
    if ( !macontext.Config->use_cDecoder && demux) skipped=demux->Skipped();
    dsyslog("end processing files");
}

void cMarkAdStandalone::Process()
{
    if (abortNow) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);

    ProcessFile();

    marks.CloseIndex(directory,isTS);
    if (!abortNow)
    {
        if (marks.Save(directory,&macontext,ptr_cDecoder,isTS))
        {
            if (length && startTime)
            {
                if (!ptr_cDecoder ){  // new decoder class does not use the vdr index file
                                      // and does not support to create an new index file
                                      // use vdr to create a new index
                    if (((time(NULL)>(startTime+(time_t) length)) || (gotendmark)) && !ptr_cDecoder )
                    {
                        int iIndexError=false;
                        int tframecnt=macontext.Config->GenIndex ? framecnt : 0;
                        if (marks.CheckIndex(directory,isTS,&tframecnt,&iIndexError))
                        {
                            if (iIndexError)
                            {
                                if (macontext.Config->GenIndex)
                                {
                                    switch (iIndexError)
                                    {
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
                                    if (RegenerateIndex())
                                    {
                                        isyslog("recreated index");
                                    }
                                    else
                                    {
                                        esyslog("failed to recreate index");
                                    }
                                }
                                else
                                {
                                    esyslog("index doesn't match marks%s",
                                            ((isTS) || ((macontext.Info.VPid.Type==
                                                     MARKAD_PIDTYPE_VIDEO_H264) && (!isTS))) ?
                                            ", sorry you're lost" :
                                            ", please run genindex");
                                }
                            }
                        }
                        if (macontext.Config->SaveInfo) SaveInfo();
                    }
                    else
                    {
                        // this shouldn't be reached
                        if (macontext.Config->logoExtraction==-1)
                            esyslog("ALERT: stopping before end of broadcast");
                    }
                }
                else { // ptr_cDecoder
                    if (macontext.Config->SaveInfo) SaveInfo();
                }
            }
        }
    }
    if (macontext.Config->GenIndex) marks.RemoveGeneratedIndex(directory,isTS);
}

bool cMarkAdStandalone::SetFileUID(char *File)
{
    if (!File) return false;
    struct stat statbuf;
    if (!stat(directory,&statbuf))
    {
        if (chown(File,statbuf.st_uid, statbuf.st_gid)==-1) return false;
    }
    return true;
}

bool cMarkAdStandalone::SaveInfo()
{
    isyslog("writing info file");
    char *src,*dst;
    if (isREEL) {
        if (asprintf(&src,"%s/info.txt",directory)==-1) return false;
    } else {
        if (asprintf(&src,"%s/info%s",directory,isTS ? "" : ".vdr")==-1) return false;
    }

    if (asprintf(&dst,"%s/info.bak",directory)==-1)
    {
        free(src);
        return false;
    }

    FILE *r,*w;
    r=fopen(src,"r");
    if (!r)
    {
        free(src);
        free(dst);
        return false;
    }

    w=fopen(dst,"w+");
    if (!w)
    {
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
    if ((macontext.Video.Info.FramesPerSecond==25) || (macontext.Video.Info.FramesPerSecond==50))
    {
        component_type_43=1;
        component_type_169=3;
    }
    else
    {
        component_type_43=5;
        component_type_169=7;
    }

    bool err=false;
    for (int i=0; i<MAXSTREAMS; i++) {
        dsyslog("stream %i has %i channels", i, macontext.Info.Channels[i]);
    }
    int stream_index=0;
    if (ptr_cDecoder) stream_index++;
    while (getline(&line,&len,r)!=-1) {
        dsyslog("info file line: %s", line);
        if (line[0]=='X') {
            int stream=0,type=0;
            char descr[256]="";

            int result=sscanf(line,"%*c %3i %3X %3c %250c",&stream,&type,(char *) &lang, (char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                switch (stream)
                {
                case 1:
                case 5:
                    if (stream==stream_content) {
                        if ((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3)) {
                            if (fprintf(w,"X %i %02i %s 4:3\n",stream_content,
                                        component_type_43+component_type_add,lang)<=0) err=true;
                            macontext.Info.AspectRatio.Num=0;
                            macontext.Info.AspectRatio.Den=0;
                        }
                        else if ((macontext.Info.AspectRatio.Num==16) && (macontext.Info.AspectRatio.Den==9))
                        {
                            if (fprintf(w,"X %i %02X %s 16:9\n",stream_content,
                                        component_type_169+component_type_add,lang)<=0) err=true;
                            macontext.Info.AspectRatio.Num=0;
                            macontext.Info.AspectRatio.Den=0;
                        }
                        else {
                            if (fprintf(w,"%s",line)<=0) err=true;
                        }
                    }
                    else
                    {
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
            }
        }
        if (err) break;
    }
    if (line) free(line);
    line=lline;

    if (lang[0]==0) strcpy(lang,"und");

    if (stream_content)
    {
        if ((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3) && (!err))
        {
            if (fprintf(w,"X %i %02i %s 4:3\n",stream_content,
                        component_type_43+component_type_add,lang)<=0) err=true;
        }
        if ((macontext.Info.AspectRatio.Num==16) && (macontext.Info.AspectRatio.Den==9) && (!err))
        {
            if (fprintf(w,"X %i %02i %s 16:9\n",stream_content,
                        component_type_169+component_type_add,lang)<=0) err=true;
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
    if (line)
    {
        if (fprintf(w,"%s",line)<=0) err=true;
        free(line);
    }
    fclose(w);
    struct stat statbuf_r;
    if (fstat(fileno(r),&statbuf_r)==-1) err=true;

    fclose(r);
    if (err)
    {
        unlink(dst);
    }
    else
    {
        if (rename(dst,src)==-1)
        {
            err=true;
        }
        else
        {
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

time_t cMarkAdStandalone::GetBroadcastStart(time_t start, int fd)
{
    // get recording start from atime of directory (if the volume is mounted with noatime)
    struct mntent *ent;
    struct stat statbuf;
    FILE *mounts=setmntent(_PATH_MOUNTED,"r");
    int mlen=0;
    int oldmlen=0;
    bool useatime=false;
    while ((ent=getmntent(mounts))!=NULL)
    {
        if (strstr(directory,ent->mnt_dir))
        {
            mlen=strlen(ent->mnt_dir);
            if (mlen>oldmlen)
            {
                if (strstr(ent->mnt_opts,"noatime"))
                {
                    useatime=true;
                }
                else
                {
                    useatime=false;
                }
            }
            oldmlen=mlen;
        }
    }
    endmntent(mounts);

    if ((useatime) && (stat(directory,&statbuf)!=-1))
    {
        if (fabs(difftime(start,statbuf.st_atime))<7200)
        {
            isyslog("getting recording start from directory atime");
            return statbuf.st_atime;
        }
    }

    // try to get from mtime
    // (and hope info.vdr has not changed after the start of the recording)
    if (fstat(fd,&statbuf)!=-1)
    {
        if (fabs(difftime(start,statbuf.st_mtime))<7200)
        {
            isyslog("getting recording start from VDR info file mtime");
            return (time_t) statbuf.st_mtime;
        }
    }

    // fallback to the directory
    const char *timestr=strrchr(directory,'/');
    if (timestr)
    {
        timestr++;
        if (isdigit(*timestr))
        {
            time_t now = time(NULL);
            struct tm tm_r;
            struct tm t = *localtime_r(&now, &tm_r); // init timezone
            if (sscanf(timestr, "%4d-%02d-%02d.%02d%*c%02d", &t.tm_year, &t.tm_mon, &t.tm_mday,
                       &t.tm_hour, & t.tm_min)==5)
            {
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

bool cMarkAdStandalone::CheckLogo()
{
    if (!macontext.Config) return false;
    if (!macontext.Config->logoDirectory) return false;
    if (!macontext.Info.ChannelName) return false;
    int len=strlen(macontext.Info.ChannelName);
    if (!len) return false;

    dsyslog("using logo directory %s",macontext.Config->logoDirectory);
    dsyslog("searching logo for %s",macontext.Info.ChannelName);
    DIR *dir=opendir(macontext.Config->logoDirectory);
    if (!dir) return false;

    struct dirent *dirent;
    while ((dirent=readdir(dir)))
    {
        if (!strncmp(dirent->d_name,macontext.Info.ChannelName,len))
        {
            closedir(dir);
            return true;
        }
    }
    closedir(dir);

    if (macontext.Config->autoLogo > 0) {
        isyslog("no logo found in logo directory, trying to find logo in recording directory");
        DIR *dir=opendir(macontext.Config->recDir);
        if (dir) {
            struct dirent *dirent;
            while ((dirent=readdir(dir))) {
                if (!strncmp(dirent->d_name,macontext.Info.ChannelName,len)) {
                    closedir(dir);
                    isyslog("logo found in recording directory");
                    return(true);
                }
            }
            closedir(dir);
        }
        isyslog("no logo found in recording directory, trying to extract logo from recording");
        ptr_cExtractLogo = new cExtractLogo();
        if (!ptr_cExtractLogo->SearchLogo(&macontext, 0)) {  // search logo from start
            if (ptr_cExtractLogo) {
                delete ptr_cExtractLogo;
                ptr_cExtractLogo = NULL;
            }
            isyslog("no logo found in recording");
        }
        else {
            dsyslog("found logo in recording");
            if (ptr_cExtractLogo) {
                delete ptr_cExtractLogo;
                ptr_cExtractLogo = NULL;
            }
            return(true);
        }
    }
    return false;
}

bool cMarkAdStandalone::LoadInfo()
{
    char *buf;
    if (asprintf(&buf,"%s/info%s",directory,isTS ? "" : ".vdr")==-1) return false;

    FILE *f;
    f=fopen(buf,"r");
    free(buf);
    buf=NULL;
    if (!f) {
        // second try for reel vdr
        if (asprintf(&buf,"%s/info.txt",directory)==-1) return false;
        f=fopen(buf,"r");
        free(buf);
        if (!f) return false;
        isREEL=true;
    }

    char *line=NULL;
    size_t linelen;
    while (getline(&line,&linelen,f)!=-1)
    {
        if (line[0]=='C')
        {
            char channelname[256]="";
            int result=sscanf(line,"%*c %*80s %250c",(char *) &channelname);
            if (result==1)
            {
                macontext.Info.ChannelName=strdup(channelname);
                char *lf=strchr(macontext.Info.ChannelName,10);
                if (lf) *lf=0;
                char *cr=strchr(macontext.Info.ChannelName,13);
                if (cr) *cr=0;
                for (int i=0; i<(int) strlen(macontext.Info.ChannelName); i++)
                {
                    if (macontext.Info.ChannelName[i]==' ') macontext.Info.ChannelName[i]='_';
                    if (macontext.Info.ChannelName[i]=='.') macontext.Info.ChannelName[i]='_';
                    if (macontext.Info.ChannelName[i]=='/') macontext.Info.ChannelName[i]='_';
                }
            }
        }
        if ((line[0]=='E') && (!bLiveRecording))
        {
            int result=sscanf(line,"%*c %*10i %20li %6i %*2x %*2x",&startTime,&length);
            if (result!=2)
            {
                startTime=0;
                length=0;
            }
        }
        if (line[0]=='T')
        {
            int result=sscanf(line,"%*c %79c",title);
            if ((result==0) || (result==EOF))
            {
                title[0]=0;
            }
            else
            {
                char *lf=strchr(title,10);
                if (lf) *lf=0;
                char *cr=strchr(title,13);
                if (cr) *cr=0;
            }
        }
        if (line[0]=='F')
        {
            int fps;
            int result=sscanf(line,"%*c %3i",&fps);
            if ((result==0) || (result==EOF))
            {
                macontext.Video.Info.FramesPerSecond=0;
            }
            else
            {
                macontext.Video.Info.FramesPerSecond=fps;
            }
        }
        if ((line[0]=='X') && (!bLiveRecording))
        {
            int stream=0,type=0;
            char descr[256]="";
            int result=sscanf(line,"%*c %3i %3i %250c",&stream,&type,(char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                if ((stream==1) || (stream==5))
                {
                    if ((type!=1) && (type!=5) && (type!=9) && (type!=13))
                    {
                        isyslog("broadcast aspectratio 16:9 (from info)");
                        macontext.Info.AspectRatio.Num=16;
                        macontext.Info.AspectRatio.Den=9;
                    }
                    else
                    {
                        isyslog("broadcast aspectratio 4:3 (from info)");
                        macontext.Info.AspectRatio.Num=4;
                        macontext.Info.AspectRatio.Den=3;
                    }
                }

                if (stream==2)
                {
                    if (type==5)
                    {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr,'2'))
                        {
                            isyslog("broadcast with DolbyDigital2.0 (from info)");
                            macontext.Info.Channels[stream]=2;
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr,'5'))
                        {
                            isyslog("broadcast with DolbyDigital5.1 (from info)");
                            macontext.Info.Channels[stream]=6;
                        }
                    }
                }
            }
        }
    }
    if ((macontext.Info.AspectRatio.Num==0) && (macontext.Info.AspectRatio.Den==0)) isyslog("no broadcast aspectratio found in info");
    if (line) free(line);

    if ((length) && (!bIgnoreTimerInfo) && (startTime))
    {
        time_t rStart=GetBroadcastStart(startTime, fileno(f));
        if (rStart) {
            dsyslog("recording start at %s", strtok(ctime(&rStart), "\n"));
            dsyslog("broadcast start at %s from VDR info file", strtok(ctime(&startTime), "\n"));
            tStart=(int) (startTime-rStart);
            if (tStart > 60*60) {   // more than 1h pre-timer make no sense, there must be a wrong directory time
                isyslog("pre-time %is not valid, possible wrong directory time, set pre-timer to vdr default (2min)", tStart);
                tStart = 120;
            }
            if (tStart<0) {
                if (length+tStart>0) {
                    isyslog("broadcast start truncated by %im, length will be corrected",-tStart/60);
                    startTime=rStart;
                    length+=tStart;
                    tStart=-1;
                }
                else {
                    esyslog("cannot determine broadcast start, assume VDR default pre timer of 120s");
                    tStart=120;
                }
            }
        }
        else {
            tStart=0;
        }
    }
    else {
        tStart=0;
    }
    fclose(f);

    if ((!length) && (!bLiveRecording))
    {
        esyslog("cannot read broadcast length from info, marks can be wrong!");
        macontext.Info.AspectRatio.Num=0;
        macontext.Info.AspectRatio.Den=0;
        bDecodeVideo=macontext.Config->DecodeVideo;
        macontext.Video.Options.IgnoreAspectRatio=false;
    }

    if (!macontext.Info.ChannelName)
    {
        return false;
    }
    else
    {
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
    struct stat statbuf;
    if (stat(buf,&statbuf)==-1)
    {
        if (errno!=ENOENT)
        {
            free(buf);
            return false;
        }
        free(buf);
        buf=NULL;
        if (asprintf(&buf,"%s/001.vdr",directory)==-1) return false;
        if (stat(buf,&statbuf)==-1)
        {
            free(buf);
            return false;
        }
        free(buf);
        // .VDR detected
        isTS=false;
        MaxFiles=999;
        return true;
    }
    free(buf);
    // .TS detected
    isTS=true;
    MaxFiles=65535;
    return true;
}

bool cMarkAdStandalone::CheckVDRHD()
{
    char *buf;
    if (asprintf(&buf,"%s/001.vdr",directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

    uchar pes_buf[32];
    if (read(fd,pes_buf,sizeof(pes_buf))!=sizeof(pes_buf))
    {
        close(fd);
        return false;
    }
    close(fd);

    if ((pes_buf[0]==0) && (pes_buf[1]==0) && (pes_buf[2]==1) && ((pes_buf[3] & 0xF0)==0xE0))
    {
        int payloadstart=9+pes_buf[8];
        if (payloadstart>23) return false;
        uchar *start=&pes_buf[payloadstart];
        if ((start[0]==0) && (start[1]==0) && (start[2]==1) && (start[5]==0) && (start[6]==0)
                && (start[7]==0) && (start[8]==1))
        {
            return true;
        }
    }
    return false;
}

off_t cMarkAdStandalone::SeekPATPMT()
{
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return (off_t) -1;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return (off_t) -1;
    uchar peek_buf[188];
    for (int i=0; i<5000; i++)
    {
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

bool cMarkAdStandalone::CheckPATPMT(off_t Offset)
{
    if (Offset<(off_t) 0) return false;
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

    if (lseek(fd,Offset,SEEK_SET)==(off_t)-1)
    {
        close(fd);
        return false;
    }

    uchar patpmt_buf[564];
    uchar *patpmt;

    if (read(fd,patpmt_buf,sizeof(patpmt_buf))!=sizeof(patpmt_buf))
    {
        close(fd);
        return false;
    }
    close(fd);
    patpmt=patpmt_buf;

    if ((patpmt[0]==0x47) && ((patpmt[1] & 0x5F)==0x40) && (patpmt[2]==0x11) &&
            ((patpmt[3] & 0x10)==0x10)) patpmt+=188; // skip SDT

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

    if ((pmt->program_number_H!=pat->program_number_H) ||
            (pmt->program_number_L!=pat->program_number_L)) return false;

    int desc_len=(pmt->program_info_length_H<<8)+pmt->program_info_length_L;
    if (desc_len>166) return false; // beyond patpmt buffer

    int section_end = 196+(pmt->section_length_H<<8)+pmt->section_length_L;
    section_end-=4; // we don't care about the CRC32
    if (section_end>376) return false; //beyond patpmt buffer

    int i=205+desc_len;

    while (i<section_end)
    {
        struct ES_DESCRIPTOR *es=NULL;
        struct STREAMINFO *si = (struct STREAMINFO *) &patpmt[i];
        int esinfo_len=(si->ES_info_length_H<<8)+si->ES_info_length_L;
        if (esinfo_len)
        {
            es = (struct ES_DESCRIPTOR *) &patpmt[i+sizeof(struct STREAMINFO)];
        }

        // oh no -> more checks!
        if (si->reserved1!=7) return false;
        if (si->reserved2!=15) return false;

        int pid=(si->PID_H<<8)+si->PID_L;

        switch (si->stream_type)
        {
        case 0x1:
        case 0x2:
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
            // just use the first pid
            if (!macontext.Info.VPid.Num) macontext.Info.VPid.Num=pid;
            break;

        case 0x3:
        case 0x4:
            // just use the first pid
            if (!macontext.Info.APid.Num) macontext.Info.APid.Num=pid;
            break;

        case 0x6:
            if (es)
            {
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

bool cMarkAdStandalone::RegenerateIndex()
{
    if (!directory) return false;
    // rename index[.vdr].generated -> index[.vdr]
    char *oldpath,*newpath;
    if (asprintf(&oldpath,"%s/index%s.generated",directory,
                 isTS ? "" : ".vdr")==-1) return false;
    if (asprintf(&newpath,"%s/index%s",directory,isTS ? "" : ".vdr")==-1)
    {
        free(oldpath);
        return false;
    }

    if (rename(oldpath,newpath)!=0)
    {
        if (errno!=ENOENT)
        {
            free(oldpath);
            free(newpath);
            return false;
        }
    }
    free(oldpath);
    free(newpath);
    return true;
}

bool cMarkAdStandalone::CreatePidfile()
{
    char *buf=NULL;
    if (asprintf(&buf,"%s/markad.pid",directory)==-1) return false;

    // check for other running markad process
    FILE *oldpid=fopen(buf,"r");
    if (oldpid)
    {
        // found old pidfile, check if it's still running
        int pid;
        if (fscanf(oldpid,"%10i\n",&pid)==1)
        {
            char procname[256]="";
            snprintf(procname,sizeof(procname),"/proc/%i",pid);
            struct stat statbuf;
            if (stat(procname,&statbuf)!=-1)
            {
                // found another, running markad
                fprintf(stderr,"another instance is running on %s",directory);
                abortNow=duplicate=true;
            }
        }
        fclose(oldpid);
    }
    else
    {
        // fopen above sets the error to 2, reset it here!
        errno=0;
    }
    if (duplicate)
    {
        free(buf);
        return false;
    }

    FILE *pidfile=fopen(buf,"w+");

    SetFileUID(buf);

    free(buf);
    if (!pidfile) return false;
    fprintf(pidfile,"%i\n",(int) getpid());
    fflush(pidfile);
    fclose(pidfile);
    return true;
}

void cMarkAdStandalone::RemovePidfile()
{
    if (!directory) return;
    if (duplicate) return;

    char *buf;
    if (asprintf(&buf,"%s/markad.pid",directory)!=-1)
    {
        unlink(buf);
        free(buf);
    }
}

const char cMarkAdStandalone::frametypes[8]={'?','I','P','B','D','S','s','b'};

cMarkAdStandalone::cMarkAdStandalone(const char *Directory, const MarkAdConfig *config)
{
    setlocale(LC_MESSAGES, "");
    directory=Directory;
    gotendmark=false;
    inBroadCast=false;
    iStopinBroadCast=false;
    isREEL=false;

    indexFile=NULL;
    streaminfo=NULL;
    demux=NULL;
    decoder=NULL;
    video=NULL;
    audio=NULL;
    osd=NULL;

    memset(&pkt,0,sizeof(pkt));

    noticeVDR_VID=false;
    noticeVDR_AC3=false;
    noticeHEADER=false;
    noticeFILLER=false;

    skipped=0;
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

    if ((config->ignoreInfo & IGNORE_TIMERINFO)==IGNORE_TIMERINFO)
    {
        bIgnoreTimerInfo=true;
    }
    else
    {
        bIgnoreTimerInfo=false;
    }

    macontext.Info.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;
    macontext.Info.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    if (!config->NoPid)
    {
        CreatePidfile();
        if (abortNow) return;
    }

    if (LOG2REC)
    {
        char *fbuf;
        if (asprintf(&fbuf,"%s/markad.log",directory)!=-1)
        {
            if (freopen(fbuf,"w+",stdout)) {};
            SetFileUID(fbuf);
            free(fbuf);
        }
    }

    long lb;
    errno=0;
    lb=sysconf(_SC_LONG_BIT);
    if (errno==0)
    {
        isyslog("starting v%s (%libit)",VERSION,lb);
    }
    else
    {
        isyslog("starting v%s",VERSION);
    }

    int ver = avcodec_version();
    char *libver = NULL;
    if (asprintf(&libver,"%i.%i.%i",ver >> 16 & 0xFF,ver >> 8 & 0xFF,ver & 0xFF)) {
        isyslog("using libavcodec.so.%s with %i threads",libver,config->threads);
        if (ver!=LIBAVCODEC_VERSION_INT) {
            esyslog("libavcodec header version %s",AV_STRINGIFY(LIBAVCODEC_VERSION));
            esyslog("header and library mismatch, do not report decoder bugs");
        }
        if (config->use_cDecoder && ((ver >> 16) < MINLIBAVCODECVERSION)) esyslog("update libavcodec to at least version %d, do not report decoder bugs", MINLIBAVCODECVERSION);
        free(libver);
    }

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(41<<8)+0)
    tsyslog("libavcodec config: %s",avcodec_configuration());
#endif
    if (((ver >> 16)<52)) {
        dsyslog("dont report bugs about H264, use libavcodec >= 52 instead!");
    }

    isyslog("on %s",Directory);

    if (!bDecodeAudio)
    {
        isyslog("audio decoding disabled by user");
    }
    if (!bDecodeVideo)
    {
        isyslog("video decoding disabled by user");
    }
    if (bIgnoreTimerInfo)
    {
        isyslog("timer info usage disabled by user");
    }
    if (config->logoExtraction!=-1)
    {
        // just to be sure extraction works
        bDecodeVideo=true;
    }

    if (config->Before) sleep(10);

    if (strstr(directory,"/@"))
    {
        isyslog("live-recording, disabling pre-/post timer");
        bIgnoreTimerInfo=true;
        bLiveRecording=true;
    } else {
        bLiveRecording=false;
    }

    if (!CheckTS()) {
        esyslog("no files found");
        abortNow=true;
        return;
    }

    if (isTS)
    {
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

        if (!CheckPATPMT(pos))
        {
            esyslog("no PAT/PMT found (%i) -> cannot process",(int) pos);
            abortNow=true;
            return;
        }
        if (asprintf(&indexFile,"%s/index",Directory)==-1) indexFile=NULL;
    }
    else
    {
        macontext.Info.APid.Num=-1;
        macontext.Info.DPid.Num=-1;
        macontext.Info.VPid.Num=-1;

        if (CheckVDRHD())
        {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
        }
        else
        {
            macontext.Info.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
        }
        if (asprintf(&indexFile,"%s/index.vdr",Directory)==-1) indexFile=NULL;
    }
    macontext.Info.APid.Num=0; // till now we do just nothing with stereo-sound

    if (!LoadInfo())
    {
        if (bDecodeVideo)
        {
            esyslog("failed loading info - logo %s%sdisabled",
                    (config->logoExtraction!=-1) ? "extraction" : "detection",
                    bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
            tStart=iStart=iStop=iStopA=0;
            macontext.Video.Options.IgnoreLogoDetection=true;
            macontext.Video.Options.WeakMarksOk=true;
        }
    }
    else
    {
        if (!CheckLogo() && (config->logoExtraction==-1))
        {
            isyslog("no logo found, logo detection disabled");
            macontext.Video.Options.IgnoreLogoDetection=true;
            macontext.Video.Options.WeakMarksOk=true;
        }
    }

    if (macontext.Video.Options.WeakMarksOk)
    {
        isyslog("marks can/will be weak!");
        inBroadCast=true;
    }

    if (tStart>1) {
        if (tStart<60) tStart=60;
        isyslog("pre-timer %is",tStart);
    }
    if (length) isyslog("broadcast length %im",length/60);

    if (title[0])
    {
        ptitle=title;
    }
    else
    {
        ptitle=(char *) Directory;
    }

    if (config->OSD)
    {
        osd= new cOSDMessage(config->svdrphost,config->svdrpport);
        if (osd) osd->Send("%s '%s'",tr("starting markad for"),ptitle);
    }
    else
    {
        osd=NULL;
    }

    if (config->markFileName[0]) marks.SetFileName(config->markFileName);

    if (macontext.Info.VPid.Num)
    {
        if (isTS)
        {
            isyslog("found %s-video (0x%04x)",
                    macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262",
                    macontext.Info.VPid.Num);
        }
        demux=new cDemux(macontext.Info.VPid.Num,macontext.Info.DPid.Num,macontext.Info.APid.Num,
                         macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,true);
    }
    else
    {
        demux=NULL;
    }

    if (macontext.Info.APid.Num)
    {
        if (macontext.Info.APid.Num!=-1)
            isyslog("found MP2 (0x%04x)",macontext.Info.APid.Num);
    }

    if (macontext.Info.DPid.Num)
    {
        if (macontext.Info.DPid.Num!=-1)
            isyslog("found AC3 (0x%04x)",macontext.Info.DPid.Num);
    }
    if (!abortNow)
    {
        decoder = new cMarkAdDecoder(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,config->threads);
        video = new cMarkAdVideo(&macontext);
        audio = new cMarkAdAudio(&macontext);
        streaminfo = new cMarkAdStreamInfo;
        if (macontext.Info.ChannelName)
            isyslog("channel %s",macontext.Info.ChannelName);
        if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
            macontext.Video.Options.IgnoreAspectRatio=true;
    }

    framecnt=0;
    framecnt2=0;
    lastiframe=0;
    iframe=0;
    chkSTART=chkSTOP=INT_MAX;
    gettimeofday(&tv1,&tz);
}

cMarkAdStandalone::~cMarkAdStandalone()
{
    if ((!abortNow) && (!duplicate))
    {
        if (skipped)
        {
            isyslog("skipped %i bytes",skipped);
        }

        gettimeofday(&tv2,&tz);
        time_t sec;
        suseconds_t usec;
        sec=tv2.tv_sec-tv1.tv_sec;
        usec=tv2.tv_usec-tv1.tv_usec;
        if (usec<0)
        {
            usec+=1000000;
            sec--;
        }
        double etime,ftime=0,ptime=0;
        etime=sec+((double) usec/1000000)-waittime;
        if (etime>0) ftime=(framecnt+framecnt2)/etime;
        if (macontext.Video.Info.FramesPerSecond>0)
            ptime=ftime/macontext.Video.Info.FramesPerSecond;
        isyslog("processed time %.2fs, %i/%i frames, %.1f fps, %.1f pps",
                etime,framecnt,framecnt2,ftime,ptime);
    }

    if ((osd) && (!duplicate))
    {
        if (abortNow)
        {
            osd->Send("%s '%s'",tr("markad aborted for"),ptitle);
        }
        else
        {
            osd->Send("%s '%s'",tr("markad finished for"),ptitle);
        }
    }

    if (macontext.Info.ChannelName) free(macontext.Info.ChannelName);
    if (indexFile) free(indexFile);

    if (demux) delete demux;
    if (decoder) delete decoder;
    if (video) delete video;
    if (audio) delete audio;
    if (streaminfo) delete streaminfo;
    if (osd) delete osd;

    RemovePidfile();
}

bool isnumber(const char *s)
{
    while (*s)
    {
        if (!isdigit(*s))
            return false;
        s++;
    }
    return true;
}

int usage(int svdrpport)
{
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
           "-G              --genindex\n"
           "                  regenerate index file\n"
           "                  this functions is depreciated and will be removed in a future version, use vdr --genindex instead\n"
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
           "                --astopoffs=<value> (default is 100)\n"
           "                  assumed stop offset in seconds range from 0 to 240\n"
           "                --posttimer=<value> (default is 600)\n"
           "                  additional recording after timer end in seconds range from 0 to 1200\n"
           "                --cDecoder\n"
           "                  use alternative cDecoder class for decoding\n"
           "                --cut\n"
           "                  cut vidio based on marks and write it in the recording directory\n"
           "                  requires --cDecoder\n"
           "                --ac3reencode\n"
           "                  re-encode AC3 stream to fix low audio level of cutted video on same devices\n"
           "                  requires --cDecoder and --cut\n"
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

static void signal_handler(int sig)
{
    void *trace[32];
    char **messages = (char **)NULL;
    int i, trace_size = 0;

    switch (sig)
    {
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
        for (i=0; i<trace_size; ++i)
        {
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

void freedir(void)
{
    if (recDir) free(recDir);
}

int main(int argc, char *argv[])
{
    int c;
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
    config.astopoffs=100;
    config.posttimer=600;
    strcpy(config.svdrphost,"127.0.0.1");
    strcpy(config.logoDirectory,"/var/lib/markad");

    struct servent *serv=getservbyname("svdrp","tcp");
    if (serv)
    {
        config.svdrpport=htons(serv->s_port);
    }
    else
    {
        config.svdrpport=2001;
    }

    atexit(freedir);

    while (1)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"background", 0, 0, 'b'},
            {"disable", 1, 0, 'd'},
            {"ignoreinfo", 1, 0, 'i' },
            {"logocachedir", 1, 0, 'l'},
            {"priority",1,0,'p'},
            {"ioprio",1,0,'r'},
            {"statisticfile",1,0,'s'},
            {"verbose", 0, 0, 'v'},

            {"asd",0,0,6},
            {"astopoffs",1,0,12},
            {"posttimer",1,0,13},
            {"cDecoder",0,0,14},
            {"cut",0,0,15},
            {"ac3reencode",0,0,16},
            {"autologo",1,0,17},
            {"loglevel",1,0,2},
            {"markfile",1,0,1},
            {"nopid",0,0,5},
            {"online",2,0,4},
            {"pass1only",0,0,11},
            {"pass2only",0,0,10},
            {"pass3only",0,0,7},
            {"svdrphost",1,0,8},
            {"svdrpport",1,0,9},
            {"testmode",0,0,3},

            {"backupmarks", 0, 0, 'B'},
            {"scenechangedetection", 0, 0, 'C'},
            {"genindex",0, 0, 'G'},
            {"saveinfo",0, 0, 'I'},
            {"extractlogo", 1, 0, 'L'},
            {"OSD",0,0,'O' },
            {"log2rec",0,0,'R'},
            {"savelogo", 0, 0, 'S'},
            {"threads", 1, 0, 'T'},
            {"version", 0, 0, 'V'},

            {0, 0, 0, 0}
        };

        c = getopt_long  (argc, argv, "bd:i:l:p:r:s:vBCGIL:ORST:V", long_options, &option_index);
        if (c == -1) break;

        switch (c) {
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
            case 's':
                // --statisticfile
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
            case 'C':
                // --scenechangedetection
                break;
            case 'G':
                config.GenIndex=true;
                fprintf(stderr, "markad: --genindex is depreciated and will be removed in a future version, use vdr --genindex instead\n");
                break;
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
            case 'S':
                // --savelogo
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
            case 3: // --testmode
                break;
            case 4: // --online
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
            case 5: // --nopid
                config.NoPid=true;
                break;
            case 6: // --asd
                break;
            case 7: // --pass3only
                break;
            case 8: // --svdrphost
                strncpy(config.svdrphost,optarg,sizeof(config.svdrphost));
                config.svdrphost[sizeof(config.svdrphost)-1]=0;
                break;
            case 9: // --svdrpport
                if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536) {
                    config.svdrpport=atoi(optarg);
                }
                else {
                    fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                    return 2;
                }
                break;
            case 10: // --pass2only
                bPass2Only=true;
                if (bPass1Only) {
                    fprintf(stderr, "markad: you cannot use --pass2only with --pass1only\n");
                    return 2;
                }
                break;
            case 11: // --pass1only
                bPass1Only=true;
                if (bPass2Only) {
                    fprintf(stderr, "markad: you cannot use --pass1only with --pass2only\n");
                    return 2;
                }
                break;
            case 12: // --astopoffs
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 240) {
                    config.astopoffs=atoi(optarg);
                }
                else {
                    fprintf(stderr, "markad: invalid astopoffs value: %s\n", optarg);
                    return 2;
                }
                break;
            case 13: // --posttimer
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 1200) config.posttimer=atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid posttimer value: %s\n", optarg);
                    return 2;
                }
                break;
            case 14: // --cDecoder
                config.use_cDecoder=true;
                break;
            case 15: // --cut
                config.MarkadCut=true;
                break;
            case 16: // --ac3reencode
                config.ac3ReEncode=true;
                break;
            case 17: // --autoLogo
                if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 2) config.autoLogo=atoi(optarg);
                else {
                    fprintf(stderr, "markad: invalid autologo value: %s\n", optarg);
                    return 2;
                }
                break;
            default:
                printf ("? getopt returned character code 0%o ? (option_index %d)\n", c,option_index);
        }
    }

    if (optind < argc) {
        while (optind < argc)
        {
            if (strcmp(argv[optind], "after" ) == 0 )
            {
                bAfter = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "before" ) == 0 )
            {
                if (!online) online=1;
                config.Before = bFork = bNice = SYSLOG = true;
            }
            else if (strcmp(argv[optind], "edited" ) == 0 )
            {
                bEdited = true;
            }
            else if (strcmp(argv[optind], "nice" ) == 0 )
            {
                bNice = true;
            }
            else if (strcmp(argv[optind], "-" ) == 0 )
            {
                bImmediateCall = true;
            }
            else
            {
                if ( strstr(argv[optind],".rec") != NULL )
                {
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
    if ( (bImmediateCall || config.Before || bAfter || bNice) && recDir )
    {
        // if bFork is given go in background
        if ( bFork )
        {
            //close_files();
            pid_t pid = fork();
            if (pid < 0)
            {
                char *err=strerror(errno);
                fprintf(stderr, "%s\n",err);
                return 2;
            }
            if (pid != 0)
            {
                return 0; // initial program immediately returns
            }
        }
        if ( bFork )
        {
            if (chdir("/")==-1)
            {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            if (setsid() == (pid_t)(-1))
            {
                perror("setsid");
                exit(EXIT_FAILURE);
            }
            if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
            {
                perror("signal(SIGHUP, SIG_IGN)");
                errno = 0;
            }
            int f;

            f = open("/dev/null", O_RDONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdin)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }

            f = open("/dev/null", O_WRONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdout)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                if (dup2(f, fileno(stderr)) == -1)
                {
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
        if ( bNice )
        {
            if (setpriority(PRIO_PROCESS,0,niceLevel)==-1)
            {
                fprintf(stderr,"failed to set nice to %d\n",niceLevel);
            }
            if (ioprio_set(1,getpid(),ioprio | ioprio_class << 13)==-1)
            {
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
        if (stat(recDir,&statbuf)==-1)
        {
            fprintf(stderr,"%s not found\n",recDir);
            return -1;
        }

        if (!S_ISDIR(statbuf.st_mode))
        {
            fprintf(stderr,"%s is not a directory\n",recDir);
            return -1;
        }

        if (access(recDir,W_OK|R_OK)==-1)
        {
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
        if (config.use_cDecoder) dsyslog("parameter --cDecoder is set");
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

        if (!bPass2Only) cmasta->Process();
        if (!bPass1Only) cmasta->Process2ndPass();
        if (config.MarkadCut) cmasta->MarkadCut();
        delete cmasta;
        return 0;
    }
    return usage(config.svdrpport);
}
