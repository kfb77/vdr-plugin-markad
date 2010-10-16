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
#include <errno.h>

#include "markad-standalone.h"
#include "version.h"

extern int sys_ioprio_set(int which, int who, int ioprio);
bool SYSLOG=false;
cMarkAdStandalone *cmasta=NULL;
int SysLogLevel=2;

static inline int ioprio_set(int which, int who, int ioprio)
{
#if defined(__i386__)
#define __NR_ioprio_set		289
#define __NR_ioprio_get		290
#elif defined(__ppc__)
#define __NR_ioprio_set		273
#define __NR_ioprio_get		274
#elif defined(__x86_64__)
#define __NR_ioprio_set		251
#define __NR_ioprio_get		252
#elif defined(__ia64__)
#define __NR_ioprio_set		1274
#define __NR_ioprio_get		1275
#else
#error "Unsupported arch"
#endif
    return syscall(__NR_ioprio_set, which, who, ioprio);
}

void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
    if (SYSLOG)
    {
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
        va_start(ap, format);
        vsyslog(priority, fmt, ap);
        va_end(ap);
    }
    else
    {
        char fmt[255];
        snprintf(fmt, sizeof(fmt), "markad: [%d] %s", getpid(), format);
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
}

cOSDMessage::~cOSDMessage()
{
    if (tid) pthread_join(tid,NULL);
    if (msg) free(msg);
    if (host) free((void*) host);
}

bool cOSDMessage::readreply(int fd)
{
    usleep(400000);
    char c=' ';
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
    }
    while (c!='\n');
    return true;
}

void *cOSDMessage::send(void *posd)
{
    cOSDMessage *osd=(cOSDMessage *) posd;

    struct hostent *host=gethostbyname(osd->host);
    if (!host)
    {
        osd->tid=0;
        return NULL;
    }

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(osd->port);
    memcpy(&name.sin_addr.s_addr,host->h_addr,sizeof(host->h_addr));
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock<0) return NULL;

    if (connect(sock, (struct sockaddr *)&name,size)!=0)
    {
        close(sock);
        return NULL;
    }

    if (!osd->readreply(sock))
    {
        close(sock);
        return NULL;
    }

    ssize_t ret;
    ret=write(sock,"MESG ",5);
    ret=write(sock,osd->msg,strlen(osd->msg));
    ret=write(sock,"\r\n",2);

    if (!osd->readreply(sock))
    {
        close(sock);
        return NULL;
    }

    ret=write(sock,"QUIT\r\n",6);

    osd->readreply(sock);
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

void cMarkAdStandalone::CalculateStopPosition(int startframe, int delta)
{
    chkLEFT=0xFFFFFFF;
    chkRIGHT=0xFFFFFFF;
    if (!macontext.Info.Length) return;
    if (!macontext.Video.Info.FramesPerSecond) return;

    if (startframe)
    {
        int len_in_frames=macontext.Info.Length*macontext.Video.Info.FramesPerSecond;
        iStop=-(startframe+len_in_frames);
        chkLEFT=startframe+delta+macontext.Video.Info.FramesPerSecond;
        chkRIGHT=startframe+(len_in_frames*2/3);
    }
    else
    {
        chkLEFT=(int) ((macontext.Video.Info.FramesPerSecond*macontext.Info.Length)/3);
        chkRIGHT=(int) ((macontext.Video.Info.FramesPerSecond*macontext.Info.Length)*2/3);
    }
}

void cMarkAdStandalone::AddStartMark()
{
    char *buf;
    if (asprintf(&buf,"start of recording (0)")!=-1)
    {
        marks.Add(MT_COMMONSTART,0,buf);
        isyslog("%s",buf);
        free(buf);
    }

    if (tStart)
    {
        iStart=-(tStart*macontext.Video.Info.FramesPerSecond);
    }
    CalculateStopPosition(-iStart,macontext.Video.Info.FramesPerSecond*MAXRANGE);
}

void cMarkAdStandalone::CheckStartStop(int frame, bool checkend)
{
    MarkAdMark mark;
    char *buf;

    if ((iStartCheck>0) && (frame>=iStartCheck))
    {
        // check if we have a start mark which is near iStart
        clMark *before_iStart=marks.GetPrev(iStart,MT_START,0xF);
        clMark *after_iStart=marks.GetNext(iStart,MT_START,0xF);

        int MAXMARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*MAXRANGE);
        int newpos=0;
        int delta_before=MAXMARKDIFF;
        int delta_after=MAXMARKDIFF;

        if (before_iStart)
        {
            delta_before=abs(iStart-before_iStart->position);
            if (delta_before>MAXMARKDIFF) delta_before=MAXMARKDIFF;
        }
        if (after_iStart)
        {
            delta_after=abs(after_iStart->position-iStart);
            if (delta_after>MAXMARKDIFF) delta_after=MAXMARKDIFF;
        }

        if (delta_before>delta_after)
        {
            // use after_iStart
            newpos=after_iStart->position;
        }

        if (delta_before<delta_after)
        {
            // use before_iStart
            newpos=before_iStart->position;
        }

        if (newpos)
        {
            isyslog("using this mark instead of assumed start mark (%i->%i)",
                    newpos,iStart);
            marks.Del(iStart);
            marks.DelTill(newpos);
            CalculateStopPosition(newpos,frame-newpos);
        }
        iStart=0;
        iStartCheck=0;
    }

    if ((iStopCheck>0) && ((frame>=iStopCheck) || (checkend)))
    {
        // check if we have a stop mark which is near iStop
        clMark *before_iStop=marks.GetPrev(iStop,MT_STOP,0xF);
        clMark *after_iStop=marks.GetNext(iStop,MT_STOP,0xF);

        int MAXMARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*MAXRANGE);
        int newpos=0;
        int delta_before=MAXMARKDIFF;
        int delta_after=MAXMARKDIFF;

        if (before_iStop)
        {
            delta_before=abs(iStop-before_iStop->position);
            if (delta_before>MAXMARKDIFF) delta_before=MAXMARKDIFF;
        }
        if (after_iStop)
        {
            delta_after=abs(after_iStop->position-iStop);
            if (delta_after>MAXMARKDIFF) delta_after=MAXMARKDIFF;
        }

        if (delta_before>delta_after)
        {
            // use after_iStop
            newpos=after_iStop->position;
        }

        if (delta_before<delta_after)
        {
            // use before_iStop
            newpos=before_iStop->position;
        }

        if (newpos)
        {
            isyslog("using this mark instead of assumed stop mark (%i->%i)",
                    newpos,iStop);
            marks.Del(iStop);
            marks.DelTill(newpos,false);
        }
        else
        {
            marks.DelTill(iStop,false);
        }
        bDecodeVideo=false;
        gotendmark=true;
        iStop=0;
        iStopCheck=0;
    }

    if ((iStart<0) && (frame>-iStart))
    {
        iStart=frame;
        if (asprintf(&buf,"assumed start of broadcast (%i)",iStart)!=-1)
        {
            mark.Type=MT_ASSUMEDSTART;
            mark.Position=iStart;
            mark.Comment=buf;
            AddMark(&mark);
            free(buf);
        }
        int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*MAXRANGE);
        clMark *before_iStart=marks.GetPrev(iStart,MT_START,0xF);
        if (before_iStart)
        {
            int tmpdiff=abs(iStart-before_iStart->position);
            if (tmpdiff<MARKDIFF) MARKDIFF=tmpdiff;
        }
        iStartCheck=iStart+MARKDIFF;
        CalculateStopPosition(iStart,MARKDIFF);
    }
    if ((iStop<0) && (lastiframe>-iStop))
    {
        iStop=frame;
        if (asprintf(&buf,"assumed stop of broadcast (%i)",iStop)!=-1)
        {
            mark.Type=MT_ASSUMEDSTOP;
            mark.Position=iStop;
            mark.Comment=buf;
            AddMark(&mark);
            free(buf);
        }

        int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*MAXRANGE);
        clMark *before_iStop=marks.GetPrev(iStart,MT_STOP,0xF);
        if (before_iStop)
        {
            int tmpdiff=abs(iStop-before_iStop->position);
            if (tmpdiff<MARKDIFF) MARKDIFF=tmpdiff;
        }
        iStopCheck=iStop+MARKDIFF;
    }
}

void cMarkAdStandalone::CheckLogoMarks(clMark *last)
{
    clMark *mark=marks.GetFirst();
    while (mark)
    {
        if ((mark->type==MT_LOGOSTOP) && mark->Next() && mark->Next()->type==MT_LOGOSTART)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*55);
            if (abs(mark->Next()->position-mark->position)<=MARKDIFF)
            {
                double distance=(mark->Next()->position-mark->position)/
                                macontext.Video.Info.FramesPerSecond;
                isyslog("logo distance too short (%.1fs), deleting (%i,%i)",distance,
                        mark->position,mark->Next()->position);
                clMark *tmp=mark;
                mark=mark->Next()->Next();
                marks.Del(tmp->Next());
                marks.Del(tmp);
                continue;
            }
        }
        mark=mark->Next();
        if ((last) && (mark==last)) return;
    }
}

void cMarkAdStandalone::CheckLastMark()
{
    if (marks.Count()<=2) return; // just two marks -> do nothing
    clMark *last=marks.GetLast();
    if (!last) return;

    clMark *nexttolast=last->Prev();
    if (!nexttolast) return;

    if ((last->type & 0xF)==(nexttolast->type & 0xF))
    {
        isyslog("removing double stop mark (%i)",last->position);
        marks.Del(last);
        return;
    }
}

void cMarkAdStandalone::CheckFirstMark()
{
    if (marksAligned) return;
    clMark *first=marks.GetFirst();
    if (!first) return;

    // Check the second mark
    clMark *second=first->Next();
    if (!second) return;

    if ((marks.Count(MT_BORDERCHANGE,0xF0)>0) && (marks.Count(MT_ASPECTCHANGE,0xF0)>0))
    {
        // wait till its clear, if we use ASPECT or BORDER
        return;
    }

    if ((second->type & 0xF)==MT_START)
    {
        bool delsec=false;
        clMark *third=second->Next();
        if (third)
        {
            if ((third->type & 0xF)==MT_START) delsec=true;
        }
        if (delsec)
        {
            isyslog("removing double start marks (%i,%i)",first->position,second->position);
            marks.Del(second);
        }
        else
        {
            isyslog("removing double start mark (%i)",first->position);
        }
        marks.Del(first);
        marksAligned=true;
        return;
    }

    if ((second->type & 0xF)==MT_STOP)
    {
        marksAligned=true;
        return;
    }

    // If we have an aspectchange, check the next aspectchange mark
    // and the difference between
    if ((second->type==MT_ASPECTCHANGE) && (macontext.Info.Length))
    {
        clMark *next=marks.GetNext(second->position,MT_ASPECTCHANGE);
        if (next)
        {
            int maxlen=macontext.Info.Length*(13*60)/(90*60); // max 13 minutes ads on 90 minutes program
            if (maxlen>(13*60)) maxlen=(13*60); // maximum ad block = 13 minutes
            int MAXPOSDIFF=(int) (macontext.Video.Info.FramesPerSecond*maxlen);
            if ((next->position-second->position)>MAXPOSDIFF)
            {
                clMark *first=marks.GetFirst();
                if (first)
                {
                    isyslog("removing unaligned start mark (%i)",first->position);
                    marks.Del(first);
                    marksAligned=true;
                    return;
                }
            }
        }
    }
    return;
}

void cMarkAdStandalone::CheckInfoAspectRatio()
{
    if (aspectChecked) return;

    if (bIgnoreVideoInfo)
    {
        aspectChecked=true;
        return;
    }

    dsyslog("checking aspectratio");

    bool aSet=false;
    if (!macontext.Info.AspectRatio.Num)
    {
        isyslog("assuming aspectratio of %i:%i",
                macontext.Video.Info.AspectRatio.Num,macontext.Video.Info.AspectRatio.Den);
        aSet=true;
    }
    else
    {
        if ((macontext.Info.AspectRatio.Num!=macontext.Video.Info.AspectRatio.Num) &&
                (macontext.Info.AspectRatio.Den!=macontext.Video.Info.AspectRatio.Den))
        {
            isyslog("aspectratio in info wrong %i:%i instead of %i:%i",
                    macontext.Video.Info.AspectRatio.Num,macontext.Video.Info.AspectRatio.Den,
                    macontext.Info.AspectRatio.Num,macontext.Info.AspectRatio.Den);
            aSet=true;
        }
    }

    if (aSet)
    {
        macontext.Info.AspectRatio.Num=macontext.Video.Info.AspectRatio.Num;
        macontext.Info.AspectRatio.Den=macontext.Video.Info.AspectRatio.Den;

        if ((macontext.Info.AspectRatio.Num==16) &&
                (macontext.Info.AspectRatio.Den==9))
        {
            macontext.Video.Options.IgnoreAspectRatio=true;
            setVideo169=true;
            setVideo43=false;
        }

        if ((macontext.Info.AspectRatio.Num==4) &&
                (macontext.Info.AspectRatio.Den==3))
        {
            setVideo43=true;
            setVideo169=false;
        }
        reprocess=true;
    }

    aspectChecked=true;
    return;
}

void cMarkAdStandalone::AddMark(MarkAdMark *Mark)
{
    if (gotendmark) return;
    if (!Mark) return;
    if (!Mark->Type) return;
    if (!macontext.Video.Info.FramesPerSecond) return;

    bool loggedAlready=false;

    clMark *old=marks.Get(Mark->Position);
    if ((old) && (((old->type & 0xF0)==MT_ASPECTCHANGE) || ((old->type & 0xF0)==MT_CHANNELCHANGE)))
    {
        // Aspect- / Channelchange wins over Logo/Border
        return;
    }

    if (Mark->Type==MT_ASPECTSTOP)
    {
        // check if last mark is an stop mark in short distance
        clMark *prev=marks.GetLast();
        if (prev)
        {
            if ((prev->type & 0xF)==MT_STOP)
            {
                int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*15);
                if ((Mark->Position-prev->position)<MARKDIFF)
                {
                    if (Mark->Comment) isyslog("%s",Mark->Comment);
                    isyslog("double stop mark in short distance, deleting this mark (%i)",prev->position);
                    marks.Del(prev);
                    loggedAlready=true;
                }
            }
            if (prev->type==MT_ASPECTSTART)
            {
                int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*240);
                if ((Mark->Position-prev->position)<MARKDIFF)
                {
                    if (Mark->Comment) isyslog("%s",Mark->Comment);
                    double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                    isyslog("logo distance too short (%.1fs), deleting (%i,%i)",distance,
                            prev->position,Mark->Position);
                    marks.Del(prev);
                    return;
                }
            }
        }
    }

    if (Mark->Type==MT_LOGOSTOP)
    {
        // check if last mark is an audiochannel stop
        clMark *prev=marks.GetLast();
        if ((prev) && (prev->type==MT_CHANNELSTOP))
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*15);
            if ((Mark->Position-prev->position)<MARKDIFF)
            {
                if (Mark->Comment) isyslog("%s",Mark->Comment);
                isyslog("audiochannel change in short distance, using this mark (%i->%i)",Mark->Position,prev->position);
                return;
            }
        }

        prev=marks.GetPrev(Mark->Position,MT_LOGOSTART);
        if (prev)
        {
            int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*240);
            if ((Mark->Position-prev->position)<MARKDIFF)
            {
                if (Mark->Comment) isyslog("%s",Mark->Comment);
                double distance=(Mark->Position-prev->position)/macontext.Video.Info.FramesPerSecond;
                isyslog("logo distance too short (%.1fs), deleting (%i,%i)",distance,
                        prev->position,Mark->Position);
                marks.Del(prev);
                return;
            }
        }
    }

    if (Mark->Type==MT_LOGOSTART)
    {
        // check if last mark is an aspectratio change
        clMark *prev=marks.GetLast();
        if (prev)
        {
            if ((prev->type & 0xF0)==MT_ASPECTCHANGE)
            {
                int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*5);
                if ((Mark->Position-prev->position)<MARKDIFF)
                {
                    if (Mark->Comment) isyslog("%s",Mark->Comment);
                    isyslog("aspectratio change in short distance, deleting this mark (%i)",
                            Mark->Position);
                    return;
                }
            }

            if (prev->type==MT_CHANNELSTART)
            {
                int MARKDIFF=(int) (macontext.Video.Info.FramesPerSecond*5);
                if ((Mark->Position-prev->position)<MARKDIFF)
                {
                    if (Mark->Comment) isyslog("%s",Mark->Comment);
                    isyslog("audiochannel change in short distance, deleting this mark (%i)",
                            Mark->Position);
                    return;
                }
            }
        }
    }

    if (macontext.Info.Length>0)
    {
        if ((marks.Count(MT_BORDERCHANGE,0xF0)>=3) && (Mark->Position>chkLEFT) &&
                (Mark->Position<chkRIGHT) && (!macontext.Video.Options.IgnoreLogoDetection))
        {
            if (Mark->Comment)
            {
                isyslog("%s",Mark->Comment);
                loggedAlready=true;
            }
            isyslog("border changes detected. logo detection disabled");
            macontext.Video.Options.IgnoreLogoDetection=true;
            marks.Del((uchar) MT_LOGOSTART);
            marks.Del((uchar) MT_LOGOSTOP);
        }

        bool deleteLogoBorder=false;
        if ((marks.Count(MT_CHANNELCHANGE,0xF0)>=3) && (Mark->Position>chkLEFT) &&
                (Mark->Position<chkRIGHT) && (!macontext.Video.Options.IgnoreLogoDetection))
        {
            if (!loggedAlready)
            {
                if (Mark->Comment)
                {
                    isyslog("%s",Mark->Comment);
                    loggedAlready=true;
                }
            }
            isyslog("audio channel changes detected. logo/border detection disabled");
            if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H262)
            {
                if (!macontext.Info.AspectRatio.Num)
                {
                    isyslog("assuming broadcast aspectratio is 16:9");
                    macontext.Info.AspectRatio.Num=16;
                    macontext.Info.AspectRatio.Den=9;
                    macontext.Video.Options.IgnoreAspectRatio=true;
                }
            }
            macontext.Audio.Info.DolbyDigital51=true;
            setAudio51=true;
            setAudio20=false;
            reprocess=true;
            deleteLogoBorder=true;
        }

        if ((marks.Count(MT_ASPECTCHANGE,0xF0)>=3) && (Mark->Position>chkLEFT) &&
                (Mark->Position<chkRIGHT) && (!macontext.Video.Options.IgnoreLogoDetection))
        {
            if (!loggedAlready)
            {
                if (Mark->Comment)
                {
                    isyslog("%s",Mark->Comment);
                    loggedAlready=true;
                }
            }
            isyslog("aspectratio changes detected. logo/border detection disabled");

            if ((!macontext.Info.AspectRatio.Num) && ((Mark->Type & 0xF0)==MT_ASPECTCHANGE))
            {
                isyslog("assuming broadcast aspectratio is 4:3");
                macontext.Info.AspectRatio.Num=4;
                macontext.Info.AspectRatio.Den=3;
                reprocess=true;
                setVideo43=true;
            }
            deleteLogoBorder=true;
        }

        if (deleteLogoBorder)
        {
            bDecodeVideo=false;
            macontext.Video.Options.IgnoreLogoDetection=true;
            macontext.Video.Data.Valid=false;
            marks.Del((uchar) MT_LOGOSTART);
            marks.Del((uchar) MT_LOGOSTOP);
            marks.Del((uchar) MT_BORDERSTART);
            marks.Del((uchar) MT_BORDERSTOP);
        }
    }
    if (Mark->Position>chkLEFT) CheckFirstMark();
    if (marksAligned) CheckLogoMarks(marks.GetLast());

    if ((Mark->Comment) && (!loggedAlready)) isyslog("%s",Mark->Comment);
    marks.Add(Mark->Type,Mark->Position,Mark->Comment);
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

void cMarkAdStandalone::CheckBroadcastLength()
{
    if (macontext.Info.Length) return;
    if (!macontext.Video.Info.FramesPerSecond) return;
    /* get broadcastlength from length of indexFile */
    int tframecnt=1;
    int iIndexError;
    marks.CheckIndex(directory,isTS,&tframecnt,&iIndexError);
    macontext.Info.Length=tframecnt/macontext.Video.Info.FramesPerSecond;
    isyslog("got broadcast length of %im from index",macontext.Info.Length/60);
    reprocess=true;
}

bool cMarkAdStandalone::CheckIndexGrowing()
{
    // Here we check if the index is more
    // advanced than our framecounter.
    // If not we wait. If we wait too much,
    // we discard this check...

#define WAITTIME 10

    if (!indexFile) return false;
    if (sleepcnt>=2) return false; // we already slept too much

    bool notenough=true;
    do
    {
        struct stat statbuf;
        if (stat(indexFile,&statbuf)==-1) return false;

        int maxframes=statbuf.st_size/8;
        if (maxframes<(framecnt+200))
        {
            if ((difftime(time(NULL),statbuf.st_mtime))>=10) return false; // "old" file
            marks.Save(directory,macontext.Video.Info.FramesPerSecond,isTS);
            sleep(WAITTIME); // now we sleep and hopefully the index will grow
            waittime+=WAITTIME;
            if (errno==EINTR) return false;
            sleepcnt++;
            if (sleepcnt>=2)
            {
                esyslog("no new data after %i seconds, skipping wait!",
                        sleepcnt*WAITTIME);
                notenough=false; // something went wrong?
            }
        }
        else
        {
            sleepcnt=0;
            notenough=false;
        }
    }
    while (notenough);
    if (!sleepcnt) return true;
    else return false;
}

void cMarkAdStandalone::ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos)
{
    if (!NewPos) return;
    if (!Mark1) return;
    if (!*Mark1) return;
    bool save=false;

    if ((*Mark1)->position!=NewPos->FrameNumberBefore)
    {
        marks.Del(*Mark1);
        *Mark1=marks.Add(MT_MOVED,NewPos->FrameNumberBefore,NewPos->CommentBefore);
        save=true;
    }
    if (NewPos->CommentBefore) isyslog("%s",NewPos->CommentBefore);

    if (Mark2 && (*Mark2) && (*Mark2)->position!=NewPos->FrameNumberAfter)
    {
        marks.Del(*Mark2);
        *Mark2=marks.Add(MT_MOVED,NewPos->FrameNumberAfter,NewPos->CommentAfter);
        if (NewPos->CommentAfter) isyslog("%s",NewPos->CommentAfter);
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
        if (*Mark1==*Mark2) pn=1;
        if (*Mark1!=*Mark2) pn=3;
    }
    else
    {
        pn=2;
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
        if (pn==1)
        {
            dsyslog("processing file %05i (start mark)",Number);
        }
        else
        {
            dsyslog("processing file %05i %s",Number,(pn==3) ? "(after mark)" : "(before mark)");
        }

        if (lseek(f,Offset,SEEK_SET)!=Offset)
        {
            close(f);
            return false;
        }

        uint64_t offset=Offset;
        int offset_add=0;
        while ((dataread=read(f,data,datalen))>0)
        {
            if (abort) break;

            if ((video_demux) && (video) && (decoder) && (streaminfo))
            {
                uchar *tspkt = data;
                int tslen = dataread;

                while (tslen>0)
                {
                    int len=video_demux->Process(macontext.Info.VPid,tspkt,tslen,&vpkt);
                    if (len<0)
                    {
                        esyslog("error demuxing video");
                        abort=true;
                        break;
                    }
                    else
                    {
                        if (vpkt.Data)
                        {
                            bool dRes=false;
                            if (streaminfo->FindVideoInfos(&macontext,vpkt.Data,vpkt.Length))
                            {
                                actframe++;
                                framecnt2++;

                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                                {
                                    lastiframe=iframe;
                                    lastiframetime=iframetime;
                                    if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
                                    {
                                        iframe=actframe;
                                    }
                                    else
                                    {
                                        iframe=actframe-1;
                                    }
                                    iframetime=vpkt.Timestamp;
                                    dRes=true;
                                }
                            }
                            if (pn>1) dRes=decoder->DecodeVideo(&macontext,vpkt.Data,vpkt.Length);
                            if (dRes)
                            {
                                if ((actframe-iframe)<=3)
                                {
                                    if (pn>1) pos=video->Process2ndPass(lastiframe,Frames,(pn==2));
                                    //SaveFrame(lastiframe);
                                    framecounter++;
                                }
                                if ((pos) && (pn==3))
                                {
                                    // found overlap
                                    ChangeMarks(Mark1,Mark2,pos);
                                    close(f);
                                    return true;
                                }
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                        if (!vpkt.Offcnt)
                        {
                            offset_add+=len;
                        }
                        else
                        {
                            offset+=offset_add;
                            offset+=len;
                            offset_add=0;
                        }
                    }
                }
            }
#if 0
            if ((mp2_demux) && (audio) && (pn!=3))
            {
                uchar *tspkt = data;
                int tslen = dataread;

                while (tslen>0)
                {
                    int len=mp2_demux->Process(macontext.Info.APid,tspkt,tslen,&apkt);
                    if (len<0)
                    {
                        esyslog("error demuxing mp2-audio");
                        break;
                    }
                    else
                    {
                        if (apkt.Data)
                        {
                            if (apkt.Timestamp) audiotime=apkt.Timestamp;

                            if (abs(audiotime-lastiframetime)<DELTATIME)
                            {
                                if (decoder->DecodeMP2(&macontext,apkt.Data,apkt.Length))
                                {
                                    pos=audio->Process2ndPass(iframe);
                                    if (pos) ChangeMarks(Mark1,NULL,pos);
                                }
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
            }
#endif
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

    if (!macontext.Video.Info.FramesPerSecond)
        macontext.Video.Info.FramesPerSecond=25;

    if (!marks.Count())
    {
        marks.Load(directory,macontext.Video.Info.FramesPerSecond,isTS);
    }

    bool infoheader=false;
    clMark *p1=NULL,*p2=NULL;

#if 0
    p1=marks.GetFirst();
    if (!p1) return;
    if (p1->position>0)
    {
        isyslog("2nd pass");
        infoheader=true;
        off_t offset;
        int number,frame,iframes;
        int frange=macontext.Video.Info.FramesPerSecond*120;

        if (marks.ReadIndex(directory,isTS,p1->position-(frange/10),frange,&number,&offset,&frame,&iframes))
        {
            ProcessFile2ndPass(&p1,&p1,number,offset,frame,iframes);
        }
    }
#endif
    if (marks.Count()<4) return; // here we cannot do much

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

        if (marks.ReadIndex(directory,isTS,p1->position-frange,frange,&number,&offset,&frame,&iframes))
        {
            if (!ProcessFile2ndPass(&p1,NULL,number,offset,frame,iframes)) break;

            frange=macontext.Video.Info.FramesPerSecond*320; // 160s + 160s
            if (marks.ReadIndex(directory,isTS,p2->position,frange,&number,&offset,&frame,&iframes))
            {
                if (!ProcessFile2ndPass(&p1,&p2,number,offset,frame,iframes)) break;
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
}

char *cMarkAdStandalone::Timestamp2HMS(unsigned int Timestamp)
{
    double val=(double) Timestamp/90000;
    int h,m;
    double s_ns;
    h=(int) val/3600;
    m=(int) (val-(3600*h))/60;
    s_ns=val-(h*3600+m*60);
    static char buf[20];
    sprintf(buf,"%02d:%02d:%06.4f",h,m,s_ns);
    return (char *) &buf;
}

bool cMarkAdStandalone::ProcessFile(int Number)
{
    if (!directory) return false;
    if (!Number) return false;

    if (!CheckIndexGrowing()) CheckBroadcastLength();

    if (abort) return false;

    //const int datalen=8272;
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
    dsyslog("processing file %05i",Number);

    uint64_t offset=0;
    int offset_add=0;
    while ((dataread=read(f,data,datalen))>0)
    {
        if (abort) break;
        MarkAdMark *mark=NULL;

        if ((video_demux) && (video) && (streaminfo))
        {
            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=video_demux->Process(macontext.Info.VPid,tspkt,tslen,&vpkt);
                if (len<0)
                {
                    esyslog("error demuxing video");
                    abort=true;
                    break;
                }
                else
                {
                    if (vpkt.Data)
                    {
                        bool dRes=false;
                        if (streaminfo->FindVideoInfos(&macontext,vpkt.Data,vpkt.Length))
                        {
                            if ((macontext.Video.Info.Height) && (!noticeHEADER))
                            {
                                isyslog("%s %i%c",(macontext.Video.Info.Height>576) ? "HDTV" : "SDTV",
                                        macontext.Video.Info.Height,
                                        macontext.Video.Info.Interlaced ? 'i' : 'p');
                                noticeHEADER=true;
                            }

                            if (!framecnt)
                            {
                                AddStartMark();
                                nextPictType=MA_I_TYPE;
                            }

                            if (macontext.Config->GenIndex)
                            {
                                if ((macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H262) ||
                                        ((macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) &&
                                         (!macontext.Video.Info.Interlaced)))
                                {
                                    nextPictType=macontext.Video.Info.Pict_Type;
                                }
                                marks.WriteIndex(directory,isTS,offset,nextPictType,Number);
                            }
                            nextPictType=macontext.Video.Info.Pict_Type;
                            framecnt++;

                            if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                            {
                                lastiframe=iframe;
                                lastiframetime=iframetime;
                                CheckStartStop(lastiframe);
                                if (lastiframe>chkLEFT) CheckInfoAspectRatio();

                                if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
                                {
                                    iframe=framecnt;
                                }
                                else
                                {
                                    iframe=framecnt-1;
                                }
                                iframetime=vpkt.Timestamp;
                                dRes=true;
                            }
                        }

                        if ((decoder) && (bDecodeVideo))
                            dRes=decoder->DecodeVideo(&macontext,vpkt.Data,vpkt.Length);
                        if (dRes)
                        {
                            if ((framecnt-iframe)<=3)
                            {
                                //printf("VID %5i %s %x\n",lastiframe,Timestamp2HMS(lastiframetime),lastiframetime);

                                mark=video->Process(lastiframe,iframe);
                                if (mark) AddMark(mark);
                                //SaveFrame(lastiframe);  // TODO: JUST FOR DEBUGGING!
                            }
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                    if (!vpkt.Offcnt)
                    {
                        offset_add+=len;
                    }
                    else
                    {
                        offset+=offset_add;
                        offset+=len;
                        offset_add=0;
                    }
                }
            }
        }

        if ((ac3_demux) && (streaminfo) && (audio))
        {
            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=ac3_demux->Process(macontext.Info.DPid,tspkt,tslen,&apkt);
                if (len<0)
                {
                    esyslog("error demuxing ac3-audio");
                    break;
                }
                else
                {
                    if (apkt.Data)
                    {
                        if (streaminfo->FindAC3AudioInfos(&macontext,apkt.Data,apkt.Length))
                        {
                            if ((!isTS) && (!noticeVDR_AC3))
                            {
                                isyslog("found AC3%s",macontext.Config->AC3Always ? "*" : "");
                                noticeVDR_AC3=true;
                            }
                            if (apkt.Timestamp) audiotime=apkt.Timestamp;

                            if (abs(audiotime-lastiframetime)<DELTATIME)
                            {
                                //printf("AC3 %5i %s %x\n",lastiframe,Timestamp2HMS(audiotime),audiotime);
                                mark=audio->Process(lastiframe,iframe);
                                if (mark) AddMark(mark);
                            }
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if (((gotendmark) && (!macontext.Config->GenIndex)) || (reprocess))
        {
            if (f!=-1) close(f);
            return true;
        }

        if (!CheckIndexGrowing()) CheckBroadcastLength();
        if (abort)
        {
            if (f!=-1) close(f);
            return false;
        }
    }
    close(f);
    return true;
}

bool cMarkAdStandalone::Reset(bool FirstPass)
{
    bool ret=true;
    reprocess=false;
    if (FirstPass) framecnt=0;
    lastiframe=0;
    iframe=0;

    lastiframetime=0;
    iframetime=0;
    audiotime=0;

    gotendmark=false;

    memset(&vpkt,0,sizeof(vpkt));
    memset(&apkt,0,sizeof(apkt));

    iStart=iStartCheck=iStop=iStopCheck=0;
    chkLEFT=chkRIGHT=0;

    if (FirstPass)
    {
        marksAligned=false;
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
    if (video_demux) video_demux->Clear();
    if (ac3_demux) ac3_demux->Clear();
    if (mp2_demux) mp2_demux->Clear();
    if (video) video->Clear();
    if (audio) audio->Clear();
    return ret;
}

bool cMarkAdStandalone::CheckDolbyDigital51()
{
    if (!macontext.Audio.Info.DolbyDigital51) return false;
    if (abort) return false;

    // Assumption: last mark must be MT_CHANNELSTOP and the position must be
    // beyond the half of the broadcast length (or?)
    clMark *mark=marks.GetLast();
    if (!mark) return false; // no last mark? there is a problem!

    mark=marks.GetPrev(mark->position,MT_CHANNELSTART);
    if (mark)
    {
        if (mark->position>chkLEFT) return false;
    }

    reprocess=true;
    bDecodeVideo=macontext.Config->DecodeVideo;
    setAudio20=true;
    setAudio51=false;
    macontext.Audio.Info.DolbyDigital51=false;
    isyslog("%s DolbyDigital5.1 marks found", mark ? "not enough" : "no");
    isyslog("restarting from scratch");
    if ((ac3_demux) && (!macontext.Config->AC3Always))
    {
        delete ac3_demux;
        ac3_demux=NULL;
        macontext.Info.DPid.Num=0;
    }
    Reset();
    return true;
}

void cMarkAdStandalone::ProcessFile()
{
    for (int i=1; i<=MaxFiles; i++)
    {
        if (abort) break;
        if (!ProcessFile(i)) break;
        if ((gotendmark) && (!macontext.Config->GenIndex)) break;
        if (reprocess)
        {
            isyslog("restarting from scratch");
            i=0;
            Reset();
        }
    }

    if (!abort)
    {
        if (lastiframe)
        {
            CheckStartStop(lastiframe,true);

            if ((!gotendmark) && ((iStop<0) || (!tStart)))
            {
                char *buf;
                MarkAdMark tempmark;
                tempmark.Type=MT_COMMONSTOP;
                tempmark.Position=lastiframe;

                if (asprintf(&buf,"stop of recording (%i)",lastiframe)!=-1)
                {
                    tempmark.Comment=buf;
                    AddMark(&tempmark);
                    free(buf);
                }
            }
        }

        CheckLastMark();
        CheckLogoMarks();
    }
}

void cMarkAdStandalone::Process()
{
    if (abort) return;

    if (macontext.Config->BackupMarks) marks.Backup(directory,isTS);

    ProcessFile();
    if (CheckDolbyDigital51()) ProcessFile();

    if (!abort)
    {
        marks.CloseIndex(directory,isTS);
        if (marks.Save(directory,macontext.Video.Info.FramesPerSecond,isTS))
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
                            isyslog("index contains errors");
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
        }
        if (macontext.Config->GenIndex) marks.RemoveGeneratedIndex(directory,isTS);

        if ((!bIgnoreAudioInfo) && (!bIgnoreVideoInfo) && (macontext.Info.Length)) SaveInfo();
    }
}

bool cMarkAdStandalone::SaveInfo()
{
    if ((!setVideo43) && (!setVideo169) && (!setAudio20) && (!setAudio51)) return true;
    char *src,*dst;
    if (asprintf(&src,"%s/info%s",directory,isTS ? "" : ".vdr")==-1) return false;

    if (asprintf(&dst,"%s/info.bak",directory)==-1)
    {
        free(src);
        return false;
    }

    FILE *r,*w;
    r=fopen(src,"r");

    w=fopen(dst,"w+");

    if ((!r) || (!w))
    {
        free(src);
        free(dst);
        return false;
    }

    char *line=NULL;
    char *lline=NULL;
    size_t length=0;

    bool setVideo43_done=false;
    bool setVideo169_done=false;
    bool setAudio20_done=false;
    bool setAudio51_done=false;

    char lang[4]="";

    bool err=false;
    while (getline(&line,&length,r)!=-1)
    {
        if (line[0]=='X')
        {
            int stream=0,type=0;
            char descr[256]="";

            int result=sscanf(line,"%*c %i %i %3c %250c",&stream,&type,(char *) &lang, (char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                switch (stream)
                {
                case 1:
                    if ((((type==1) || (type==5)) && (setVideo169)) ||
                            (((type==2) || (type==3) || (type==6) || (type==7)) && (setVideo43)))
                    {
                        if (setVideo43)
                        {
                            if (fprintf(w,"X 1 01 %s 4:3\n",lang)<=0) err=true;
                            setVideo43_done=true;
                        }
                        if (setVideo169)
                        {
                            if (fprintf(w,"X 1 03 %s 16:9\n",lang)<=0) err=true;
                            setVideo169_done=true;
                        }
                    }
                    else
                    {
                        if (fprintf(w,"%s",line)<=0) err=true;
                    }
                    break;
                case 2:
                    if ((type==5) && ((setAudio51) || (setAudio20)))
                    {
                        if (setAudio51)
                        {
                            if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
                            setAudio51_done=true;
                        }
                        if (setAudio20)
                        {
                            if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
                            setAudio20_done=true;
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

    if ((setVideo43) && (!setVideo43_done) && (!err))
    {
        if (fprintf(w,"X 1 01 %s 4:3\n",lang)<=0) err=true;
    }
    if ((setVideo169) && (!setVideo169_done) && (!err))
    {
        if (fprintf(w,"X 1 03 %s 16:9\n",lang)<=0) err=true;
    }
    if ((setAudio20) && (!setAudio20_done) && (!err))
    {
        if (fprintf(w,"X 2 05 %s Dolby Digital 2.0\n",lang)<=0) err=true;
    }
    if ((setAudio51) && (!setAudio51_done) && (!err))
    {
        if (fprintf(w,"X 2 05 %s Dolby Digital 5.1\n",lang)<=0) err=true;
    }
    if (line)
    {
        if (fprintf(w,"%s",line)<=0) err=true;
        free(line);
    }
    fclose(w);
    fclose(r);
    if (err)
    {
        unlink(dst);
    }
    else
    {
        if (rename(dst,src)==-1) err=true;
    }

    if ((getuid()==0 || geteuid()!=0) && (!err))
    {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath=NULL;
        if (asprintf(&spath,"%s/%s",directory,isTS ? "00001.ts" : "001.vdr")!=-1)
        {
            struct stat statbuf;
            if (!stat(spath,&statbuf))
            {
                if (chown(src,statbuf.st_uid, statbuf.st_gid)) {};
            }
            free(spath);
        }
    }

    free(src);
    free(dst);
    return (err==false);
}

bool cMarkAdStandalone::LoadInfo()
{
    char *buf;
    if (asprintf(&buf,"%s/info%s",directory,isTS ? "" : ".vdr")==-1) return false;

    FILE *f;
    f=fopen(buf,"r");
    free(buf);
    if (!f) return false;

    const char *timestr=strrchr(directory,'/');
    if (timestr)
    {
        timestr++;
        if (!isdigit(*timestr)) timestr=directory;
    }
    else
    {
        timestr=directory;
    }

    time_t now = time(NULL);
    struct tm tm_r;
    struct tm t = *localtime_r(&now, &tm_r); // init timezone
    time_t rStart=0;
    if (sscanf(timestr, "%4d-%02d-%02d.%02d%*c%02d", &t.tm_year, &t.tm_mon, &t.tm_mday,
               &t.tm_hour, & t.tm_min)==5)
    {
        t.tm_year-=1900;
        t.tm_mon--;
        t.tm_sec=0;
        t.tm_isdst=-1;
        rStart=mktime(&t);
    }

    long start=0;
    char *line=NULL;
    size_t length;
    while (getline(&line,&length,f)!=-1)
    {
        if (line[0]=='C')
        {
            char channelname[256]="";
            int result=sscanf(line,"%*c %*s %250c",(char *) &channelname);
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
                }
            }
        }
        if (line[0]=='E')
        {
            int result=sscanf(line,"%*c %*i %li %i %*i %*x",&start,&macontext.Info.Length);
            if (result!=2)
            {
                start=0;
                macontext.Info.Length=0;
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
        if (line[0]=='X')
        {
            int stream=0,type=0;
            char descr[256]="";
            int result=sscanf(line,"%*c %i %i %250c",&stream,&type,(char *) &descr);
            if ((result!=0) && (result!=EOF))
            {
                if ((stream==1) && (!bIgnoreVideoInfo))
                {
                    if ((type!=1) && (type!=5))
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

                if ((stream==2) && (!bIgnoreAudioInfo))
                {
                    if (type==5)
                    {
                        // if we have DolbyDigital 2.0 disable AC3
                        if (strchr(descr,'2'))
                        {
                            isyslog("broadcast with DolbyDigital2.0%s",macontext.Config->AC3Always ?
                                    "" : ", disabling AC3 decoding");

                            if (!macontext.Config->AC3Always) macontext.Info.DPid.Num=0;
                        }
                        else
                            // if we have DolbyDigital 5.1 disable video decoding
                            if (strchr(descr,'5'))
                            {
                                bDecodeVideo=false;
                                macontext.Audio.Info.DolbyDigital51=true;
                                macontext.Video.Options.IgnoreAspectRatio=true;
                                isyslog("broadcast with DolbyDigital5.1, disabling video decoding");
                                if (macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H262)
                                {

                                    if ((macontext.Info.AspectRatio.Num==4) &&
                                            (macontext.Info.AspectRatio.Den==3))
                                    {
                                        isyslog("wrong aspectratio in info, changing to 16:9");
                                        macontext.Info.AspectRatio.Num=16;
                                        macontext.Info.AspectRatio.Den=9;
                                        setVideo169=true;
                                    }
                                }
                            }
                            else
                            {
                                setAudio20=true;
                            }
                    }
                }
            }
        }
    }
    if (line) free(line);
    fclose(f);

    if ((macontext.Info.Length) && (!bIgnoreTimerInfo) && (start) && (rStart))
    {
        tStart=start-rStart;
        if (tStart<0)
        {
            isyslog("broadcast start truncated by %im, length will be corrected",-tStart/60);
            macontext.Info.Length+=tStart;
            tStart=0;
        }
    }
    else
    {
        tStart=0;
    }

    if (!macontext.Info.Length)
    {
        esyslog("cannot read broadcast length from info, marks can be wrong!");
        macontext.Info.AspectRatio.Num=0;
        macontext.Info.AspectRatio.Den=0;
        bIgnoreAudioInfo=true;
        bIgnoreVideoInfo=true;
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

bool cMarkAdStandalone::CheckPATPMT()
{
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

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
        if (fscanf(oldpid,"%i\n",&pid)==1)
        {
            char procname[256]="";
            snprintf(procname,sizeof(procname),"/proc/%i",pid);
            struct stat statbuf;
            if (stat(procname,&statbuf)!=-1)
            {
                // found another, running markad
                isyslog("another instance is running on this recording");
                abort=duplicate=true;
            }
        }
        fclose(oldpid);
    }
    if (duplicate)
    {
        free(buf);
        return false;
    }

    FILE *pidfile=fopen(buf,"w+");

    if (getuid()==0 || geteuid()!=0)
    {
        // if we are root, set fileowner to owner of directory
        struct stat statbuf;
        if (!stat(directory,&statbuf))
        {
            if (chown(buf,statbuf.st_uid, statbuf.st_gid)) {};
        }
    }

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
    reprocess=false;

    indexFile=NULL;
    streaminfo=NULL;
    video_demux=NULL;
    ac3_demux=NULL;
    mp2_demux=NULL;
    decoder=NULL;
    video=NULL;
    audio=NULL;
    osd=NULL;

    memset(&vpkt,0,sizeof(vpkt));
    memset(&apkt,0,sizeof(apkt));

    setAudio51=false;
    setAudio20=false;
    setVideo43=false;
    setVideo169=false;
    aspectChecked=false;

    noticeVDR_MP2=false;
    noticeVDR_AC3=false;
    noticeHEADER=false;

    sleepcnt=0;
    waittime=0;
    duplicate=false;
    marksAligned=false;
    title[0]=0;

    memset(&macontext,0,sizeof(macontext));
    macontext.Config=config;

    bDecodeVideo=config->DecodeVideo;
    bDecodeAudio=config->DecodeAudio;

    tStart=iStart=iStop=iStartCheck=iStopCheck=0;

    if ((config->ignoreInfo & IGNORE_VIDEOINFO)==IGNORE_VIDEOINFO)
    {
        bIgnoreVideoInfo=true;
    }
    else
    {
        bIgnoreVideoInfo=false;
    }

    if ((config->ignoreInfo & IGNORE_AUDIOINFO)==IGNORE_AUDIOINFO)
    {
        bIgnoreAudioInfo=true;
    }
    else
    {
        bIgnoreAudioInfo=false;
    }

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

    isyslog("starting v%s",VERSION);
    isyslog("on %s",Directory);

    if (!bDecodeAudio)
    {
        isyslog("audio decoding disabled by user");
    }
    if (!bDecodeVideo)
    {
        isyslog("video decoding disabled by user");
    }
    if (bIgnoreAudioInfo)
    {
        isyslog("audio info usage disabled by user");
    }
    if (bIgnoreVideoInfo)
    {
        isyslog("video info usage disabled by user");
    }
    if (bIgnoreTimerInfo)
    {
        isyslog("timer info usage disabled by user");
    }

    if (config->logoExtraction!=-1)
    {
        // just to be sure extraction works
        bDecodeVideo=true;
        bIgnoreAudioInfo=true;
        bIgnoreVideoInfo=true;
    }

    if (!config->NoPid)
    {
        CreatePidfile();
        if (abort) return;
    }

    if (config->Before) sleep(10);

    if (!CheckTS()) return;

    if (isTS)
    {
        if (!CheckPATPMT())
        {
            esyslog("no PAT/PMT found -> cannot process");
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

    if (!LoadInfo())
    {
        if (bDecodeVideo)
        {
            esyslog("failed loading info - logo %s%sdisabled",
                    (config->logoExtraction!=-1) ? "extraction" : "detection",
                    bIgnoreTimerInfo ? " " : " and pre-/post-timer ");
            tStart=0;
        }
    }

    if (tStart) isyslog("pre-timer %im",tStart/60);
    if (macontext.Info.Length) isyslog("broadcast length %im",macontext.Info.Length/60);

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
        if (osd) osd->Send("%s %s",tr("starting markad for"),ptitle);
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
            dsyslog("using %s-video (0x%04x)",
                    macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262",
                    macontext.Info.VPid.Num);
        }
        else
        {
            dsyslog("using %s-video",
                    macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262");
        }
        video_demux = new cMarkAdDemux();
    }
    else
    {
        video_demux=NULL;
    }

    if (macontext.Info.APid.Num)
    {
        if (macontext.Info.APid.Num!=-1)
            dsyslog("using MP2 (0x%04x)",macontext.Info.APid.Num);
        mp2_demux = new cMarkAdDemux();
    }
    else
    {
        mp2_demux=NULL;
    }

    if (macontext.Info.DPid.Num)
    {
        if (macontext.Info.DPid.Num!=-1)
            dsyslog("using AC3 (0x%04x)%s",macontext.Info.DPid.Num,macontext.Config->AC3Always ? "*" : "");
        ac3_demux = new cMarkAdDemux();
    }
    else
    {
        ac3_demux=NULL;
    }

    if (!abort)
    {
        decoder = new cMarkAdDecoder(macontext.Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,
                                     macontext.Info.APid.Num!=0,macontext.Info.DPid.Num!=0,config->threads);
        video = new cMarkAdVideo(&macontext);
        audio = new cMarkAdAudio(&macontext);
        streaminfo = new cMarkAdStreamInfo;
        if (macontext.Info.ChannelName)
            dsyslog("channel %s",macontext.Info.ChannelName);
    }

    framecnt=0;
    framecnt2=0;
    lastiframe=0;
    iframe=0;
    lastiframetime=0;
    iframetime=0;
    audiotime=0;
    chkLEFT=chkRIGHT=0;
    gettimeofday(&tv1,&tz);
}

cMarkAdStandalone::~cMarkAdStandalone()
{
    if ((!abort) && (!duplicate))
    {
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

    if (osd)
    {
        if (abort)
        {
            osd->Send("%s %s",tr("markad aborted for"),ptitle);
        }
        else
        {
            osd->Send("%s %s",tr("markad finished for"),ptitle);
        }
    }

    if (macontext.Info.ChannelName) free(macontext.Info.ChannelName);
    if (indexFile) free(indexFile);

    if (video_demux) delete video_demux;
    if (ac3_demux) delete ac3_demux;
    if (mp2_demux) delete mp2_demux;
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
           "-a              --AC3\n"
           "                  always search in DolbyDigital channels, even if the\n"
           "                  broadcast isn't in DolbyDigital5.1\n"
           "-b              --background\n"
           "                  markad runs as a background-process\n"
           "                  this will be automatically set if called with \"after\"\n"
           "-d              --disable=<option>\n"
           "                  <option>   1 = disable video decoding, 2 = disable audio\n"
           "                             decoding, 3 = disable video and audio decoding\n"
           "-i              --ignoreinfo=<info>\n"
           "                  ignores hints from info(.vdr) file\n"
           "                  <info> 1 = ignore audio info, 2 = ignore video info,\n"
           "                         4 = ignore timer info\n"
           "                         (options can be added together, e.g. 5 = ignore\n"
           "                          audio and timer info)\n"
           "-l              --logocachedir\n"
           "                  directory where logos stored, default /var/lib/markad\n"
           "-p              --priority=<priority>\n"
           "                  software priority of markad when running in background\n"
           "                  <priority> from -20...19, default 19\n"
           "-r              --ioprio=<class>[,<level>]\n"
           "                  io priority of markad when running in background\n"
           "                  <class> 1 = realtime, <level> from 0..7, default 4\n"
           "                          2 = besteffort, <level> from 0..7, default 4\n"
           "                          3 = idle\n"
           "-v              --verbose\n"
           "                  increments loglevel by one, can be given multiple times\n"
           "-B              --backupmarks\n"
           "                  make a backup of existing marks\n"
           "-G              --genindex\n"
           "                  regenerate broken index file\n"
           "-L              --extractlogo=<direction>[,width[,height]]\n"
           "                  extracts logo to /tmp as pgm files (must be renamed)\n"
           "                  <direction>  0 = top left,    1 = top right\n"
           "                               2 = bottom left, 3 = bottom right\n"
           "                  [width]  range from 50 to %3i, default %3i (SD)\n"
           "                                                 default %3i (HD)\n"
           "                  [height] range from 20 to %3i, default %3i\n"
           "-O              --OSD\n"
           "                  markad sends an OSD-Message for start and end\n"
           "-T              --threads=<number>\n"
           "                  number of threads used for decoding, max. 16\n"
           "                  (default is the number of cpus)\n"
           "-V              --version\n"
           "                  print version-info and exit\n"
           "                --markfile=<markfilename>\n"
           "                  set a different markfile-name\n"
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
           "\ncmd: one of\n"
           "-                            dummy-parameter if called directly\n"
           "after                        markad starts to analyze the recording\n"
           "before                       markad exits immediately if called with \"before\"\n"
           "edited                       markad exits immediately if called with \"edited\"\n"
           "nice                         runs markad with nice(19)\n"
           "\n<record>                     is the name of the directory where the recording\n"
           "                             is stored\n\n",
           LOGO_MAXWIDTH,LOGO_DEFWIDTH,LOGO_DEFHDWIDTH,
           LOGO_MAXHEIGHT,LOGO_DEFHEIGHT,svdrpport
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
            esyslog("[bt] %s\n", messages[i]);
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
    int ioprio_class=2;
    int ioprio=4;
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
    config.logoExtraction=-1;
    config.logoWidth=-1;
    config.logoHeight=-1;
    config.threads=-1;
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
            {"extractlogo", 1, 0, 'L'},
            {"OSD",0,0,'O' },
            {"savelogo", 0, 0, 'S'},
            {"threads", 1, 0, 'T'},
            {"version", 0, 0, 'V'},

            {0, 0, 0, 0}
        };

        c = getopt_long  (argc, argv, "abcd:i:jl:nop:r:s:vBCGL:OST:V",
                          long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {

        case 'a':
            // --ac3
            config.AC3Always=true;
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
            if ((config.ignoreInfo<0) || (config.ignoreInfo>255))
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
            online=atoi(optarg);
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
    if ((bAfter) && (online)) return 0;
    if (config.Before)
    {
        if (!online) return 0;
        if ((online==1) && (!strchr(recDir,'@'))) return 0;
    }
    if (bEdited) return 0;

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
                esyslog("fork ERROR: %s",err);
                return 2;
            }
            if (pid != 0)
            {
                tsyslog("forked to pid %d",pid);
                return 0; // initial program immediately returns
            }
        }
        if ( bFork )
        {
            tsyslog("(forked) pid %d", getpid());
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
                esyslog("failed to set nice to %d",niceLevel);
            }
            if (ioprio_set(1,getpid(),ioprio | ioprio_class << 13)==-1)
            {
                esyslog("failed to set ioprio to %i,%i",ioprio_class,ioprio);
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
