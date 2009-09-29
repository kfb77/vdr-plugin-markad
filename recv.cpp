/*
 * recv.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "recv.h"

cMarkAdReceiver::cMarkAdReceiver(int RecvNumber, const char *Filename, cTimer *Timer)
        :
        cReceiver(Timer->Channel()->GetChannelID(), -1,
                  Timer->Channel()->Vpid(),Timer->Channel()->Apids(),
                  Timer->Channel()->Dpids()),cThread("markad"),
        buffer(MEGATS(3)), running(false) // 3MB Buffer
{
    if ((!Filename) || (!Timer)) return;

    recvnumber=RecvNumber;
    filename=strdup(Filename);

    // 10 ms timeout on getting TS frames
    buffer.SetTimeouts(0, 10);

    bool useH264=false;
#if APIVERSNUM >= 10700
#ifdef DVBFE_DELSYS_DVBS2
    if (Timer->Channel()->System()==DVBFE_DELSYS_DVBS2) useH264=true;
#else
    if (Timer->Channel()->System()==SYS_DVBS2) useH264=true;
#endif
#endif
    memset(&macontext,0,sizeof(macontext));
    if (Timer->Event())
    {
        macontext.General.StartTime=Timer->Event()->StartTime();
        macontext.General.EndTime=Timer->Event()->EndTime();
    }
    else
    {
        macontext.General.StartTime=Timer->StartTime();
        macontext.General.EndTime=Timer->StopTime();
        macontext.General.ManualRecording=true;
    }

    macontext.General.VPid.Num=Timer->Channel()->Vpid();
    if (useH264)
    {
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
    }
    else
    {
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
    }

//    macontext.General.APid.Pid=Timer->Channel()->Apid(0); // TODO ... better solution?
//    macontext.General.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    macontext.General.DPid.Num=Timer->Channel()->Dpid(0); // TODO ... better solution?
    macontext.General.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;

    macontext.General.H264=useH264;

    if (macontext.General.VPid.Num)
    {
        dsyslog("markad [%i]: using %s-video",recvnumber,useH264 ? "H264": "H262");
        video=new cMarkAdVideo(RecvNumber,&macontext);
        video_demux = new cMarkAdDemux(RecvNumber);
    }
    else
    {
        video=NULL;
        video_demux=NULL;
    }

    if (macontext.General.APid.Num)
    {
        dsyslog("markad [%i]: using mp2",recvnumber);
        mp2_demux = new cMarkAdDemux(RecvNumber);
    }
    else
    {
        mp2_demux = NULL;
    }

    if (macontext.General.DPid.Num)
    {
        dsyslog("markad [%i]: using ac3",recvnumber);
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

    decoder=new cMarkAdDecoder(RecvNumber,useH264,macontext.General.DPid.Num!=0);
    common=new cMarkAdCommon(RecvNumber,&macontext);

    marks.Load(Filename);
    Index=NULL;
    lastiframe=0;
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

    if ((!macontext.State.ContentStopped) && (lastiframe))
    {
        MarkAdMark tempmark;
        tempmark.Position=lastiframe;

        char *buf;
        if (asprintf(&buf,"stop of user content (%i)",lastiframe)!=-1)
        {
            tempmark.Comment=buf;
            AddMark(&tempmark,0);
            isyslog("markad [%i]: %s",recvnumber,buf);
            free(buf);
        }
    }

    if (video_demux) delete video_demux;
    if (mp2_demux) delete mp2_demux;
    if (ac3_demux) delete ac3_demux;
    if (decoder) delete decoder;
    if (video) delete video;
    if (audio) delete audio;
    if (common) delete common;
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
        marks.Save();
        if (Index) delete Index;
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
    if (!mark->Position) return;
    if (!mark->Comment) return;
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

#define MAXPOSDIFF 10500 // = 7 min

    cMark *prevmark=marks.GetPrev(mark->Position);
    if (!prevmark) return;
    if (!prevmark->comment) return;
    if (abs(mark->Position-prevmark->position)>MAXPOSDIFF) return;

    int prevPriority=atoi(prevmark->comment+1);
    if (prevPriority==Priority) return;

    if (prevPriority>Priority)
    {
        // add text from mark to prevmark
        prevmark->comment=strcatrealloc(prevmark->comment," ");
        prevmark->comment=strcatrealloc(prevmark->comment,mark->Comment);

        dsyslog("markad [%i]: delete mark %i",recvnumber,newmark->position);
        marks.Del(newmark,true);
    }
    else
    {
        // add text from prevmark to mark
        mark->Comment=strcatrealloc(mark->Comment," ");
        mark->Comment=strcatrealloc(mark->Comment,prevmark->comment);

        dsyslog("markad [%i]: delete previous mark %i",recvnumber,prevmark->position);
        marks.Del(prevmark,true);
    }
    marks.Save();
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

            if (common)
            {
                mark=common->Process(lastiframe);
                AddMark(mark,0);
            }

            if ((video_demux) && (decoder) && (video))
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
                            decoder->FindVideoInfos(&macontext,pkt,pktlen);
                            if (decoder->DecodeVideo(&macontext,pkt,pktlen))
                            {
                                if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                                {
                                    mark=video->Process(lastiframe);
                                    AddMark(mark,3);
                                }
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

            if ((mp2_demux) && (decoder) && (audio))
            {
                uchar *pkt;
                int pktlen;

                uchar *tspkt = frame->Data();
                int tslen = frame->Count();

                while (tslen>0)
                {
                    int len=mp2_demux->Process(macontext.General.APid,tspkt,tslen,&pkt,&pktlen);
                    if (len<0)
                    {
                        break;
                    }
                    else
                    {
                        if (pkt)
                        {
                            if (decoder->DecodeMP2(&macontext,pkt,pktlen))
                            {
                                mark=audio->Process(lastiframe);
                                AddMark(mark,1);
                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
            }

            if ((ac3_demux) && (decoder) && (audio))
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
                            decoder->FindAC3AudioInfos(&macontext,pkt,pktlen);
                            if (decoder->DecodeAC3(&macontext,pkt,pktlen))
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


