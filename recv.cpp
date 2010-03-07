/*
 * recv.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "recv.h"

#if APIVERSNUM > 10711
cMarkAdReceiver::cMarkAdReceiver(int RecvNumber, const char *Filename, cTimer *Timer)
        :
        cReceiver(NULL, -1),
        cThread("markad"),
        buffer(MEGATS(3)), running(false) // 3MB Buffer
#else
cMarkAdReceiver::cMarkAdReceiver(int RecvNumber, const char *Filename, cTimer *Timer)
        :
        cReceiver(Timer->Channel()->GetChannelID(), -1,
                  Timer->Channel()->Vpid(),Timer->Channel()->Dpids()),
        cThread("markad"),
        buffer(MEGATS(3)), running(false) // 3MB Buffer
#endif
{
    if ((!Filename) || (!Timer)) return;

#if APIVERSNUM > 10711
    AddPid(Timer->Channel()->VPid());
    AddPid(Timer->Channel()->Dpid(0));
#endif

    recvnumber=RecvNumber;
    filename=strdup(Filename);

    // 10 ms timeout on getting TS frames
    buffer.SetTimeouts(0, 10);

    memset(&macontext,0,sizeof(macontext));
    macontext.General.VPid.Num=Timer->Channel()->Vpid();

#if APIVERSNUM > 10700
    switch Timer->Channel()->Vtype()
    {
    case 0x2:
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
        break;
    case 0x1b:
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
        break;
    default:
        macontext.General.VPid.Num=0;
        macontext.General.VPid.Type=0;
        break;
    }
#else
#if APIVERSNUM < 10700
    macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
#else
#error "VDR-1.7.0 is not supported"
#endif
#endif

    macontext.General.DPid.Num=Timer->Channel()->Dpid(0); // ... better solution?
    macontext.General.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;

    if (macontext.General.VPid.Num)
    {
        dsyslog("markad [%i]: using %s-video",recvnumber,
                (macontext.General.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) ? "H264": "H262");
        video=new cMarkAdVideo(RecvNumber,&macontext);
        video_demux = new cMarkAdDemux(RecvNumber);
    }
    else
    {
        video=NULL;
        video_demux=NULL;
    }

    if (macontext.General.DPid.Num)
    {
        dsyslog("markad [%i]: using AC3",recvnumber);
        ac3_demux = new cMarkAdDemux(RecvNumber);
    }
    else
    {
        ac3_demux=NULL;
    }

    if ((macontext.General.APid.Num) || (macontext.General.DPid.Num))
    {
        audio=new cMarkAdAudio(RecvNumber,&macontext);
    }
    else
    {
        audio=NULL;
    }

    streaminfo=new cMarkAdStreamInfo;

    marks.Load(Filename);

    Index=NULL;
    lastiframe=0;
    framecnt=-1;
    marksfound=false;

    if (!marks.Count())
    {
        MarkAdMark tempmark;
        char *buf;
        tempmark.Position=0;
        if (asprintf(&buf,"start of recording (0)")!=-1)
        {
            tempmark.Comment=buf;
            AddMark(&tempmark,0);
            isyslog("markad [%i]: %s",recvnumber,buf);
            free(buf);
        }
    }
    else
    {
        marksfound=true;
    }
}

cMarkAdReceiver::~cMarkAdReceiver()
{
    cReceiver::Detach();
    if (running)
    {
        running=false;
        buffer.Signal();
        Cancel(2);
    }
    buffer.Clear();

    if (lastiframe)
    {
        MarkAdMark tempmark;
        tempmark.Position=lastiframe;
        char *buf;

        if (asprintf(&buf,"stop of recording (%i)",lastiframe)!=-1)
        {
            tempmark.Comment=buf;
            AddMark(&tempmark,0);
            isyslog("markad [%i]: %s",recvnumber,buf);
            free(buf);
        }
    }

    if (Index) delete Index;
    if (video_demux) delete video_demux;
    if (ac3_demux) delete ac3_demux;
    if (streaminfo) delete streaminfo;
    if (video) delete video;
    if (audio) delete audio;
    if (filename) free(filename);
}

char *cMarkAdReceiver::strcatrealloc(char *dest, const char *src)
{
    if (!src || !*src)
        return dest;

    size_t l = (dest ? strlen(dest) : 0) + strlen(src) + 1;
    if (dest)
    {
        dest = (char *)realloc(dest, l);
        strcat(dest, src);
    }
    else
    {
        dest = (char*)malloc(l);
        strcpy(dest, src);
    }
    return dest;
}

int cMarkAdReceiver::LastIFrame()
{
    if (!Index)
    {
        if (!filename) return 0;
        Index = new cIndexFile(filename,false);
        if (!Index)
        {
            esyslog("markad [%i]: ERROR can't allocate index",recvnumber);
            return 0;
        }
        else if (!Index->Ok())
        {
            // index file is not ready till now, try it later
            delete Index;
            Index=NULL;
            return 0;
        }
    }
    int iframe=Index->GetNextIFrame(Index->Last(),false,NULL,NULL,NULL,true);
    if (iframe>0)
    {
        return iframe;
    }
    else
    {
        return 0;
    }
}

void cMarkAdReceiver::Activate(bool On)
{
    if (On)
    {
        if (!running)
        {
            running=true;
            Start();
        }
    }
    else if (running)
    {
        running = false;
        buffer.Signal();
        Cancel(2);
    }
}

void cMarkAdReceiver::Receive(uchar *Data, int Length)
{
    int len = Length;

    if (!buffer.Check(len))
    {
        // Buffer overrun
        esyslog("markad [%i]: buffer overrun (Check)",recvnumber);
        buffer.Signal();
        return;
    }

    cFrame *frame=new cFrame(Data, len);
    if (frame && !buffer.Put(frame))
    {
        // Buffer overrun
        esyslog("markad [%i]: buffer overrun (Put)",recvnumber);
        delete frame;
        buffer.Signal();
    }
}

void cMarkAdReceiver::AddMark(MarkAdMark *mark, int Priority)
{
    if (!mark) return;
    if (!mark->Comment) return;
    if (mark->Position<0) return;

    cMark *newmark=marks.Add(mark->Position);
    if (newmark)
    {
        char *buf;
        if (asprintf(&buf,"P%i %s",Priority,mark->Comment)!=-1)
        {
            if (newmark->comment) free(newmark->comment);
            newmark->comment=buf;
        }
    }
    marks.Save();

    if (!marksfound)
    {
        cMark *prevmark=marks.GetPrev(mark->Position);
        if (!prevmark) return;
        if (!prevmark->comment) return;
        if (prevmark->position==0) return;
#define MAXPOSDIFF (25*60*13) // = 13 min
        if (abs(mark->Position-prevmark->position)>MAXPOSDIFF)
        {
            cMark *firstmark=marks.Get(0);
            if (firstmark)
            {
                marks.Del(firstmark,true);
                marks.Save();
                marksfound=true;
            }
        }
        else
        {
            marksfound=true;
        }
    }
}

void cMarkAdReceiver::Action()
{
    while (running)
    {
        cFrame *frame=buffer.Get();
        if (frame)
        {
            lastiframe=LastIFrame();
            MarkAdMark *mark;

            if ((video_demux) && (streaminfo) && (video))
            {
                cTimeMs t;
                uchar *pkt;
                int pktlen;

                uchar *tspkt = frame->Data();
                int tslen = frame->Count();

                while (tslen>0)
                {
                    int len=video_demux->Process(macontext.General.VPid,tspkt,tslen,&pkt,&pktlen);
                    if (len<0)
                    {
                        break;
                    }
                    else
                    {
                        if (pkt)
                        {
                            if (streaminfo->FindVideoInfos(&macontext,pkt,pktlen))
                            {
                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                                {
                                    if (framecnt==-1)
                                    {
                                        framecnt=0;
                                    }
                                    else
                                    {
                                        mark=video->Process(lastiframe);
                                        AddMark(mark,3);
                                    }
                                }
                                if (framecnt!=-1) framecnt++;
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
                if (t.Elapsed()>100)
                {
                    isyslog("markad [%i]: 100ms exceeded -> %Lims",
                            recvnumber,t.Elapsed());
                }
            }

            if ((ac3_demux) && (streaminfo) && (audio))
            {
                uchar *pkt;
                int pktlen;

                uchar *tspkt = frame->Data();
                int tslen = frame->Count();

                while (tslen>0)
                {
                    int len=ac3_demux->Process(macontext.General.DPid,tspkt,tslen,&pkt,&pktlen);
                    if (len<0)
                    {
                        break;
                    }
                    else
                    {
                        if (pkt)
                        {
                            if (streaminfo->FindAC3AudioInfos(&macontext,pkt,pktlen))
                            {
                                mark=audio->Process(lastiframe);
                                AddMark(mark,2);
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
            }

            buffer.Drop(frame);
        }
        else
            buffer.Wait();
    }
    buffer.Clear();
    running=false;
}


