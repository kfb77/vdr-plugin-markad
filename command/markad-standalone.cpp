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

bool SYSLOG=false;
bool LOG2REC=false;
cMarkAdStandalone *cmasta=NULL;
int SysLogLevel=2;

static inline int ioprio_set(int which, int who, int ioprio)
{
#if defined(__i386__)
#define __NR_ioprio_set		289
#elif defined(__ppc__)
#define __NR_ioprio_set		273
#elif defined(__x86_64__)
#define __NR_ioprio_set		251
#elif defined(__ia64__)
#define __NR_ioprio_set		1274
#else
#define __NR_ioprio_set		0
#endif
    if (__NR_ioprio_set)
    {
        return syscall(__NR_ioprio_set, which, who, ioprio);
    } else {
        return 0; // just do nothing
    }
}

void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
    if ((SYSLOG) && (!LOG2REC))
    {
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
        va_start(ap, format);
        vsyslog(priority, fmt, ap);
        va_end(ap);
    }
    else
    {
        char buf[255]={0};
        const time_t now=time(NULL);
        if (ctime_r(&now,buf)) {
            buf[strlen(buf)-6]=0;
        }
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "%s%s [%d] %s", LOG2REC ? "":"markad: ",buf, getpid(), format);
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

void cMarkAdStandalone::CalculateCheckPositions(int startframe)
{
    if (!length) return;
    if (!startframe) return;
    if (!macontext.Video.Info.FramesPerSecond) return;

    int delta=macontext.Video.Info.FramesPerSecond*MAXRANGE;
    int len_in_frames=macontext.Video.Info.FramesPerSecond*length;
    int len_in_framesA=macontext.Video.Info.FramesPerSecond*(length+macontext.Config->astopoffs);

    iStart=-startframe;
    iStop=-(startframe+len_in_frames);
    iStopA=-(startframe+len_in_framesA);
    //chkSTART=-iStart+(1.1*delta);
    chkSTART=-iStart+delta;
    dsyslog("chkSTART set to %i",chkSTART);
    chkSTOP=-iStop+(3*delta);
}

void cMarkAdStandalone::CheckStop()
{
    dsyslog("checking stop");
    int delta=macontext.Video.Info.FramesPerSecond*MAXRANGE;
    clMark *end=marks.GetAround(delta,iStop,MT_STOP,0x0F);

    if (end)
    {
        marks.DelTill(end->position,false);
        isyslog("using mark on position %i as stop mark",end->position);
    }
    else
    {
        //fallback
        if (iStopinBroadCast)
        {
            MarkAdMark mark;
            memset(&mark,0,sizeof(mark));
            mark.Position=iStopA;
            mark.Type=MT_ASSUMEDSTOP;
            AddMark(&mark);
            marks.DelTill(iStopA,false);
        }
        else
        {
            isyslog("removing marks from position %i, if any",iStop);
            marks.DelTill(iStop,false);
        }
    }
    iStop=iStopA=0;
    gotendmark=true;
}

void cMarkAdStandalone::CheckStart()
{
    dsyslog("checking start");
    clMark *begin=NULL;

    if ((macontext.Info.Channels) && (macontext.Audio.Info.Channels) &&
            (macontext.Info.Channels!=macontext.Audio.Info.Channels))
    {
        char as[20];
        switch (macontext.Info.Channels)
        {
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
        switch (macontext.Audio.Info.Channels)
        {
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

    macontext.Info.Channels=macontext.Audio.Info.Channels;
    if (macontext.Config->DecodeAudio) {
        if ((macontext.Info.Channels==6) && (macontext.Audio.Options.IgnoreDolbyDetection==false))
        {
            isyslog("DolbyDigital5.1 audio detected. logo/border/aspect detection disabled");
            bDecodeVideo=false;
            macontext.Video.Options.IgnoreAspectRatio=true;
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_ASPECTSTART);
            marks.Del(MT_ASPECTSTOP);
            // start mark must be around istart
            begin=marks.GetAround(INT_MAX,iStart,MT_CHANNELSTART);
        }
        else
        {
            if (macontext.Info.DPid.Num)
            {
                if ((macontext.Info.Channels) && (macontext.Audio.Options.IgnoreDolbyDetection==false))
                    isyslog("broadcast with %i audio channels, disabling AC3 decoding",macontext.Info.Channels);
                if (macontext.Audio.Options.IgnoreDolbyDetection==true)
                    isyslog("disabling AC3 decoding (from logo)");
                macontext.Info.DPid.Num=0;
                demux->DisableDPid();
            }
        }
    }
    if ((macontext.Info.AspectRatio.Num) && ((macontext.Info.AspectRatio.Num!=
            macontext.Video.Info.AspectRatio.Num) || (macontext.Info.AspectRatio.Den!=
                    macontext.Video.Info.AspectRatio.Den)))
    {
        isyslog("video aspect description in info (%i:%i) wrong",
                macontext.Info.AspectRatio.Num,
                macontext.Info.AspectRatio.Den);
    }

    macontext.Info.AspectRatio.Num=macontext.Video.Info.AspectRatio.Num;
    macontext.Info.AspectRatio.Den=macontext.Video.Info.AspectRatio.Den;

    if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) {
        isyslog("aspectratio of %i:%i detected",
                macontext.Video.Info.AspectRatio.Num,
                macontext.Video.Info.AspectRatio.Den);
    } else {
        isyslog("aspectratio of %i:%i detected%s",
                macontext.Video.Info.AspectRatio.Num,
                macontext.Video.Info.AspectRatio.Den,
                ((macontext.Video.Info.AspectRatio.Num==4) &&
                 (macontext.Video.Info.AspectRatio.Den==3)) ?
                ". logo/border detection disabled" : "");

        if ((macontext.Video.Info.AspectRatio.Num==4) &&
                (macontext.Video.Info.AspectRatio.Den==3))
        {
            bDecodeVideo=false;
            if (macontext.Info.Channels==6) {
                macontext.Video.Options.IgnoreAspectRatio=false;
                macontext.Info.DPid.Num=0;
                demux->DisableDPid();
            }
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_CHANNELSTART);
            marks.Del(MT_CHANNELSTOP);
            // start mark must be around iStart
            begin=marks.GetAround(macontext.Video.Info.FramesPerSecond*(MAXRANGE*4),iStart,MT_ASPECTSTART);
        }
    }

    if (!bDecodeVideo)
    {
        macontext.Video.Data.Valid=false;
        marks.Del(MT_LOGOSTART);
        marks.Del(MT_LOGOSTOP);
        marks.Del(MT_HBORDERSTART);
        marks.Del(MT_HBORDERSTOP);
        marks.Del(MT_VBORDERSTART);
        marks.Del(MT_VBORDERSTOP);
    }

    if (!begin)
    {
        begin=marks.GetAround(macontext.Video.Info.FramesPerSecond*(MAXRANGE*2),iStart,MT_START,0x0F);
        if (begin) {
            clMark *begin2=marks.GetAround(macontext.Video.Info.FramesPerSecond*MAXRANGE,begin->position,MT_START,0x0F);
            if (begin2) {
                if (begin2->type>begin->type) {
                    if (begin2->type==MT_ASPECTSTART) {
                        // special case, only take this mark if aspectratio is 4:3
                        if ((macontext.Video.Info.AspectRatio.Num==4) &&
                                (macontext.Video.Info.AspectRatio.Den==3)) {
                            isyslog("mark on position %i stronger than mark on position %i as start mark",begin2->position,begin->position);
                            begin=begin2;
                        }
                    } else {
                        isyslog("mark on position %i stronger than mark on position %i as start mark",begin2->position,begin->position);
                        begin=begin2;
                    }
                }
            }
        }
    }
    if (begin)
    {
        marks.DelTill(begin->position);
        CalculateCheckPositions(begin->position);
        isyslog("using mark on position %i as start mark",begin->position);

        if ((begin->type==MT_VBORDERSTART) || (begin->type==MT_HBORDERSTART))
        {
            isyslog("%s borders, logo detection disabled",(begin->type==MT_HBORDERSTART) ? "horizontal" : "vertical");
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del(MT_LOGOSTART);
            marks.Del(MT_LOGOSTOP);
        }

    }
    else
    {
        //fallback
        marks.DelTill(chkSTART);
        MarkAdMark mark;
        memset(&mark,0,sizeof(mark));
        mark.Position=iStart;
        mark.Type=MT_ASSUMEDSTART;
        AddMark(&mark);
        CalculateCheckPositions(iStart);
    }
    iStart=0;
    return;
}

void cMarkAdStandalone::CheckLogoMarks()
{
    clMark *mark=marks.GetFirst();
    while (mark)
    {
        if ((mark->type==MT_LOGOSTOP) && mark->Next() && mark->Next()->type==MT_LOGOSTART)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*30);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF)
            {
                double distance=(mark->Next()->position-mark->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance too short (%.1fs), deleting %i,%i",distance,
                        mark->position,mark->Next()->position);
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
    case MT_LOGOSTART:
        if (asprintf(&comment,"detected logo start (%i)*",Mark->Position)==-1) comment=NULL;
        break;
    case MT_LOGOSTOP:
        if (asprintf(&comment,"detected logo stop (%i)",Mark->Position)==-1) comment=NULL;
        break;
    case MT_HBORDERSTART:
        if (asprintf(&comment,"detected start of horiz. borders (%i)*",
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_HBORDERSTOP:
        if (asprintf(&comment,"detected stop of horiz. borders (%i)",
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_VBORDERSTART:
        if (asprintf(&comment,"detected start of vert. borders (%i)*",
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_VBORDERSTOP:
        if (asprintf(&comment,"detected stop of vert. borders (%i)",
                     Mark->Position)==-1) comment=NULL;
        break;
    case MT_ASPECTSTART:
        if (!Mark->AspectRatioBefore.Num)
        {
            if (asprintf(&comment,"aspectratio start with %i:%i (%i)*",
                         Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den,
                         Mark->Position)==-1) comment=NULL;
        }
        else
        {
            if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)*",
                         Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                         Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den,
                         Mark->Position)==-1) comment=NULL;
        }
        break;
    case MT_ASPECTSTOP:
        if (asprintf(&comment,"aspectratio change from %i:%i to %i:%i (%i)",
                     Mark->AspectRatioBefore.Num,Mark->AspectRatioBefore.Den,
                     Mark->AspectRatioAfter.Num,Mark->AspectRatioAfter.Den,
                     Mark->Position)==-1) comment=NULL;
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

    if (comment) isyslog("%s",comment);

    clMark *prev=marks.GetLast();
    if (prev) {
        if (prev->position==Mark->Position) {
            if (prev->type>Mark->Type)
            {
                isyslog("previous mark (%i) stronger than actual mark on same position, deleting %i",
                        prev->position, Mark->Position);
                if (comment) free(comment);
                return;
            }
            else
            {
                isyslog("actual mark stronger then previous mark on same position, deleting %i",prev->position);
                marks.Del(prev);
            }
        }
    }

    /*
    if ((Mark->Type==MT_LOGOSTART) && (!iStart) && (Mark->Position<abs(iStop)))
    {
        clMark *prev=marks.GetPrev(Mark->Position,MT_LOGOSTOP);
        if (prev)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*10);
            if ((Mark->Position-prev->position)<MARKDIFF)
            {
                double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance too short (%.1fs), deleting %i,%i",distance,
                        prev->position,Mark->Position);
                if (!macontext.Video.Options.WeakMarksOk) inBroadCast=false;
                marks.Del(prev);
                if (comment) free(comment);
                return;
            }
        }
    }
    */

    if (((Mark->Type & 0x0F)==MT_STOP) && (!iStart) && (Mark->Position<abs(iStop)))
    {
        clMark *prev=marks.GetPrev(Mark->Position,(Mark->Type & 0xF0)|MT_START);
        if (prev)
        {
            int MARKDIFF;
            if ((Mark->Type & 0xF0)==MT_LOGOCHANGE)
            {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*180);
            }
            else
            {
                MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*10);
            }
            if ((Mark->Position-prev->position)<MARKDIFF)
            {
                double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("mark distance too short (%.1fs), deleting %i,%i",distance,
                        prev->position,Mark->Position);
                if (!macontext.Video.Options.WeakMarksOk) inBroadCast=false;
                marks.Del(prev);
                if (comment) free(comment);
                return;
            }
        }
    }

    prev=marks.GetLast();
    if (prev)
    {
        if ((prev->type & 0x0F)==(Mark->Type & 0x0F))
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*30);
            int diff=abs(Mark->Position-prev->position);
            if (diff<MARKDIFF)
            {
                if (prev->type>Mark->Type)
                {
                    isyslog("previous mark (%i) stronger than actual mark, deleting %i",
                            prev->position, Mark->Position);
                    if (comment) free(comment);
                    return;
                }
                else
                {
                    isyslog("actual mark stronger then previous mark, deleting %i",prev->position);
                    marks.Del(prev);
                }
            }
        }
    }

    if (!macontext.Video.Options.WeakMarksOk)
    {
        if ((Mark->Type & 0x0F)==MT_START)
        {
            inBroadCast=true;
        }
        else
        {
            inBroadCast=false;
        }
    }
    marks.Add(Mark->Type,Mark->Position,comment);
    if (comment) free(comment);
}

void cMarkAdStandalone::SaveFrame(int frame)
{
    if (!macontext.Video.Info.Width) return;
    if (!macontext.Video.Data.Valid) return;

    FILE *pFile;
    char szFilename[256];

    // Open file
    sprintf(szFilename, "/tmp/frame%06d.pgm", frame);
    pFile=fopen(szFilename, "wb");
    if (pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", macontext.Video.Data.PlaneLinesize[0],
            macontext.Video.Info.Height);

    // Write pixel data
    if (fwrite(macontext.Video.Data.Plane[0],1,
               macontext.Video.Data.PlaneLinesize[0]*macontext.Video.Info.Height,pFile)) {};
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

    if (!indexFile) return;
    if (macontext.Config->logoExtraction!=-1) return;
    if (sleepcnt>=2) {
        dsyslog("slept too much");
        return; // we already slept too much
    }

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
                        tsyslog("assuming old recording, now>startTime+length");
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
            marks.Save(directory,macontext.Video.Info.FramesPerSecond,isTS);
            unsigned int sleeptime=WAITTIME;
            time_t sleepstart=time(NULL);
            double slepttime=0;
            while ((unsigned int)slepttime<sleeptime) {
                while (sleeptime>0) {
                    unsigned int ret=sleep(sleeptime); // now we sleep and hopefully the index will grow
                    if ((errno) && (ret)) {
                        if (abort) return;
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
        if (asprintf(&buf,"overlap before %i, moved to %i",(*Mark1)->position,
                     NewPos->FrameNumberBefore)==-1) return;
        isyslog("%s",buf);
        marks.Del(*Mark1);
        *Mark1=marks.Add(MT_MOVED,NewPos->FrameNumberBefore,buf);
        free(buf);
        save=true;
    }

    if (Mark2 && (*Mark2) && (*Mark2)->position!=NewPos->FrameNumberAfter)
    {
        char *buf=NULL;
        if (asprintf(&buf,"overlap after %i, moved to %i",(*Mark2)->position,
                     NewPos->FrameNumberAfter)==-1) return;
        isyslog("%s",buf);
        marks.Del(*Mark2);
        *Mark2=marks.Add(MT_MOVED,NewPos->FrameNumberAfter,buf);
        free(buf);
        save=true;
    }
    if (save) marks.Save(directory,macontext.Video.Info.FramesPerSecond,isTS,true);
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
        if (abort) return false;

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
            if (abort) break;

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
                        abort=true;
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

            if (abort)
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

void cMarkAdStandalone::Process2ndPass()
{
    if (abort) return;
    if (duplicate) return;
    if (!decoder) return;
    if (!length) return;
    if (!startTime) return;
    if (time(NULL)<(startTime+(time_t) length)) return;

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

    if (marks.Count()<4) return; // we cannot do much without marks

    p1=marks.GetFirst();
    if (!p1) return;

    p1=p1->Next();
    if (p1) p2=p1->Next();

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
}

bool cMarkAdStandalone::ProcessFile(int Number)
{
    if (!directory) return false;
    if (!Number) return false;

    CheckIndexGrowing();

    if (abort) return false;

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
    free(fbuf);
    if (f==-1) {
        if (isTS) {
            dsyslog("failed to open %05i.ts",Number);
        } else {
            dsyslog("failed to open %03i.vdr",Number);
        }
        return false;
    }

    int dataread;
    dsyslog("processing file %05i",Number);

    int pframe=-1;

    demux->NewFile();
again:
    while ((dataread=read(f,data,datalen))>0)
    {
        if (abort) break;
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
                    abort=true;
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
                                    abort=true;
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
                                    //SaveFrame(lastiframe);  // TODO: JUST FOR DEBUGGING!
                                    if (iStart>0)
                                    {
                                        if ((inBroadCast) && (lastiframe>chkSTART)) CheckStart();
                                    }
                                    if ((iStop>0) && (iStopA>0))
                                    {
                                        if (lastiframe>chkSTOP) CheckStop();
                                    }
                                    pframe=lastiframe;
                                }
                            }
                        }

                        if ((pkt.Type & PACKET_MASK)==PACKET_AC3)
                        {
                            if (streaminfo->FindAC3AudioInfos(&macontext,pkt.Data,pkt.Length))
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
        if (abort)
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
    macontext.Audio.Info.Channels=0;

    if (decoder)
    {
        ret=decoder->Clear();
    }
    if (streaminfo) streaminfo->Clear();
    if (demux) demux->Clear();
    if (video) video->Clear();
    if (audio) audio->Clear();
    return ret;
}

void cMarkAdStandalone::ProcessFile()
{
    for (int i=1; i<=MaxFiles; i++)
    {
        if (abort) break;
        if (!ProcessFile(i)) break;
        if ((gotendmark) && (!macontext.Config->GenIndex)) break;
    }

    if (!abort)
    {
        CheckLogoMarks();
        if ((iStop>0) && (iStopA>0)) CheckStop(); // no stopmark till now?
        if ((inBroadCast) && (!gotendmark) && (lastiframe))
        {
            MarkAdMark tempmark;
            tempmark.Type=MT_RECORDINGSTOP;
            tempmark.Position=lastiframe;
            AddMark(&tempmark);
        }
    }
    if (demux) skipped=demux->Skipped();
}

void cMarkAdStandalone::Process()
{
    if (abort) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);

    ProcessFile();

    marks.CloseIndex(directory,isTS);
    if (!abort)
    {
        if (marks.Save(directory,macontext.Video.Info.FramesPerSecond,isTS))
        {
            if (length && startTime)
            {
                if ((time(NULL)>(startTime+(time_t) length)) || (gotendmark))
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
    while (getline(&line,&len,r)!=-1)
    {
        if (line[0]=='X')
        {
            int stream=0,type=0;
            char descr[256]="";

            int result=sscanf(line,"%*c %3i %3i %3c %250c",&stream,&type,(char *) &lang, (char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                switch (stream)
                {
                case 1:
                case 5:
                    if (stream==stream_content)
                    {
                        if ((macontext.Info.AspectRatio.Num==4) && (macontext.Info.AspectRatio.Den==3))
                        {
                            if (fprintf(w,"X %i %02i %s 4:3\n",stream_content,
                                        component_type_43+component_type_add,lang)<=0) err=true;
                            macontext.Info.AspectRatio.Num=0;
                            macontext.Info.AspectRatio.Den=0;
                        }
                        else if ((macontext.Info.AspectRatio.Num==16) && (macontext.Info.AspectRatio.Den==9))
                        {
                            if (fprintf(w,"X %i %02i %s 16:9\n",stream_content,
                                        component_type_169+component_type_add,lang)<=0) err=true;
                            macontext.Info.AspectRatio.Num=0;
                            macontext.Info.AspectRatio.Den=0;
                        }
                        else
                        {
                            if (fprintf(w,"%s",line)<=0) err=true;
                        }
                    }
                    else
                    {
                        if (fprintf(w,"%s",line)<=0) err=true;
                    }
                    break;
                case 2:
                    if (type==5)
                    {
                        if (macontext.Info.Channels==6)
                        {
                            if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
                            macontext.Info.Channels=0;
                        }
                        else if (macontext.Info.Channels==2)
                        {
                            if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
                            macontext.Info.Channels=0;
                        }
                        else
                        {
                            if (fprintf(w,"%s",line)<=0) err=true;
                        }
                    }
                    else
                    {
                        if (fprintf(w,"%s",line)<=0) err=true;
                    }
                    break;
                default:
                    if (fprintf(w,"%s",line)<=0) err=true;
                    break;
                }
            }
        }
        else
        {
            if (line[0]!='@')
            {
                if (fprintf(w,"%s",line)<=0) err=true;
            }
            else
            {
                if (lline)
                {
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

    if ((macontext.Info.Channels==2) && (!err))
    {
        if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
    }
    if ((macontext.Info.Channels==6) && (!err))
    {
        if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
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
    // get broadcast start from atime of directory (if the volume is mounted with noatime)
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
            isyslog("getting broadcast start from directory atime");
            return statbuf.st_atime;
        }
    }

    // try to get from mtime
    // (and hope info.vdr has not changed after the start of the recording)
    if (fstat(fd,&statbuf)!=-1)
    {
        if (fabs(difftime(start,statbuf.st_mtime))<7200)
        {
            isyslog("getting broadcast start from info mtime");
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
                isyslog("getting broadcast start from directory (can be wrong!)");
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
                            macontext.Info.Channels=2;
                        }
                        // if we have DolbyDigital 5.1 disable video decoding
                        if (strchr(descr,'5'))
                        {
                            isyslog("broadcast with DolbyDigital5.1 (from info)");
                            macontext.Info.Channels=6;
                        }
                    }
                }
            }
        }
    }
    if (line) free(line);

    if ((length) && (!bIgnoreTimerInfo) && (startTime))
    {
        time_t rStart=GetBroadcastStart(startTime, fileno(f));
        if (rStart)
        {
            tStart=(int) (startTime-rStart);
            if (tStart<0)
            {
                if (length+tStart>0)
                {
                    isyslog("broadcast start truncated by %im, length will be corrected",-tStart/60);
                    startTime=rStart;
                    length+=tStart;
                    tStart=1;
                }
                else
                {
                    esyslog("cannot determine broadcast start, disabling start/stop detection");
                    tStart=0;
                }
            }
        }
        else
        {
            tStart=0;
        }
    }
    else
    {
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
                abort=duplicate=true;
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
    abort=false;
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

    memset(&macontext,0,sizeof(macontext));
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
        if (abort) return;
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
        abort=true;
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
            abort=true;
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
        isyslog("pre-timer %im",tStart/60);
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

    if (!abort)
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
    if ((!abort) && (!duplicate))
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
        if (abort)
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
        if (cmasta) cmasta->SetAbort();
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
        if (cmasta) cmasta->SetAbort();
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

    struct config config;
    memset(&config,0,sizeof(config));

    // set defaults
    config.DecodeVideo=true;
    config.DecodeAudio=true;
    config.SaveInfo=false;
    config.logoExtraction=-1;
    config.logoWidth=-1;
    config.logoHeight=-1;
    config.threads=-1;
    config.astopoffs=100;
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
            {"ac3",0,0,'a'
            },
            {"background", 0, 0, 'b'},
            {"comments", 0, 0, 'c'},
            {"disable", 1, 0, 'd'},
            {"ignoreinfo", 1, 0, 'i' },
            {"jumplogo",0,0,'j'},
            {"logocachedir", 1, 0, 'l'},
            {"nelonen",0,0,'n'},
            {"overlap",0,0,'o' },
            {"priority",1,0,'p'},
            {"ioprio",1,0,'r'},
            {"statisticfile",1,0,'s'},
            {"verbose", 0, 0, 'v'},

            {"asd",0,0,6},
            {"astopoffs",1,0,12},
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

        c = getopt_long  (argc, argv, "abcd:i:jl:nop:r:s:vBCGIL:ORST:V",
                          long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {

        case 'a':
            // --ac3
            break;

        case 'b':
            // --background
            bFork = SYSLOG = true;
            break;

        case 'c':
            // --comments
            break;

        case 'd':
            // --disable
            switch (atoi(optarg))
            {
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
            if ((config.ignoreInfo<1) || (config.ignoreInfo>255))
            {
                fprintf(stderr, "markad: invalid ignoreinfo option: %s\n", optarg);
                return 2;
            }
            break;

        case 'j':
            // --jumplogo
            break;

        case 'l':
            strncpy(config.logoDirectory,optarg,sizeof(config.logoDirectory));
            config.logoDirectory[sizeof(config.logoDirectory)-1]=0;
            break;

        case 'n':
            // --nelonen
            break;

        case 'o':
            // --overlap
            break;

        case 'p':
            // --priority
            if (isnumber(optarg) || *optarg=='-')
                niceLevel = atoi(optarg);
            else
            {
                fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                return 2;
            }
            bNice = true;
            break;

        case 'r':
            // --ioprio
            str=strchr(optarg,',');
            if (str)
            {
                *str=0;
                ioprio=atoi(str+1);
                *str=',';
            }
            ioprio_class=atoi(optarg);
            if ((ioprio_class<1) || (ioprio_class>3))
            {
                fprintf(stderr, "markad: invalid io-priority: %s\n", optarg);
                return 2;
            }
            if ((ioprio<0) || (ioprio>7))
            {
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
            break;

        case 'I':
            config.SaveInfo=true;
            break;

        case 'L':
            // --extractlogo
            str=optarg;
            ntok=0;
            while (tok=strtok(str,","))
            {
                switch (ntok)
                {
                case 0:
                    config.logoExtraction=atoi(tok);
                    if ((config.logoExtraction<0) || (config.logoExtraction>3))
                    {
                        fprintf(stderr, "markad: invalid extractlogo value: %s\n", tok);
                        return 2;
                    }
                    break;

                case 1:
                    config.logoWidth=atoi(tok);
                    if ((config.logoWidth<50) || (config.logoWidth>LOGO_MAXWIDTH))
                    {
                        fprintf(stderr, "markad: invalid width value: %s\n", tok);
                        return 2;
                    }
                    break;

                case 2:
                    config.logoHeight=atoi(tok);
                    if ((config.logoHeight<20) || (config.logoHeight>LOGO_MAXHEIGHT))
                    {
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
            printf("unknow option ?\n");
            break;

        case 0:
            printf ("option %s", long_options[option_index].name);
            if (optarg)
                printf (" with arg %s", optarg);
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
            if (optarg)
            {
                online=atoi(optarg);
            }
            else
            {
                online=1;
            }
            if ((online!=1) && (online!=2))
            {
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
            if (isnumber(optarg) && atoi(optarg) > 0 && atoi(optarg) < 65536)
            {
                config.svdrpport=atoi(optarg);
            }
            else
            {
                fprintf(stderr, "markad: invalid svdrpport value: %s\n", optarg);
                return 2;
            }
            break;

        case 10: // --pass2only
            bPass2Only=true;
            if (bPass1Only)
            {
                fprintf(stderr, "markad: you cannot use --pass2only with --pass1only\n");
                return 2;
            }
            break;

        case 11: // --pass1only
            bPass1Only=true;
            if (bPass2Only)
            {
                fprintf(stderr, "markad: you cannot use --pass1only with --pass2only\n");
                return 2;
            }
            break;

        case 12: // --astopoffs
            if (isnumber(optarg) && atoi(optarg) >= 0 && atoi(optarg) <= 240)
            {
                config.astopoffs=atoi(optarg);
            }
            else
            {
                fprintf(stderr, "markad: invalid astopoffs value: %s\n", optarg);
                return 2;
            }
            break;

        default:
            printf ("? getopt returned character code 0%o ? (option_index %d)\n", c,option_index);
        }
    }

    if (optind < argc)
    {
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
                fprintf(stderr,"failed to set nice to %d",niceLevel);
            }
            if (ioprio_set(1,getpid(),ioprio | ioprio_class << 13)==-1)
            {
                fprintf(stderr,"failed to set ioprio to %i,%i",ioprio_class,ioprio);
            }
        }

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

        if (!bPass2Only) cmasta->Process();
        if (!bPass1Only) cmasta->Process2ndPass();
        delete cmasta;
        return 0;
    }
    return usage(config.svdrpport);
}
