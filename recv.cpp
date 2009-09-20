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
        buffer(MEGABYTE(3)), running(false) // 3MB Buffer
{
    if ((!Filename) || (!Timer)) return;

    recvnumber=RecvNumber;
    filename=strdup(Filename);

    // 10 ms timeout on getting TS frames
    buffer.SetTimeouts(0, 10);

    bool useH264=false;
#if APIVERSNUM >= 10700 && APIVERSNUM < 10702
    if (Timer->Channel()->System()==DVBFE_DELSYS_DVBS2) useH264=true;
#endif

#if APIVERSNUM >= 10702
    if (Timer->Channel()->System()==SYS_DVBS2) useH264=true;
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

    macontext.General.VPid=Timer->Channel()->Vpid();
//    macontext.General.APid=Timer->Channel()->Apid(0); // TODO ... better solution?
    macontext.General.DPid=Timer->Channel()->Dpid(0); // TODO ... better solution?

    macontext.General.H264=useH264;

    if (macontext.General.VPid)
    {
        video=new cMarkAdVideo(RecvNumber,&macontext);
        video_demux = new cMarkAdDemux();
    }
    else
    {
        video=NULL;
        video_demux=NULL;
    }

    if (macontext.General.APid)
    {
        mp2_demux = new cMarkAdDemux();
    }
    else
    {
        mp2_demux = NULL;
    }

    if (macontext.General.DPid)
    {
        ac3_demux = new cMarkAdDemux();
    }
    else
    {
        ac3_demux=NULL;
    }

    if ((macontext.General.APid) || (macontext.General.DPid))
    {
        audio=new cMarkAdAudio(RecvNumber,&macontext);
    }
    else
    {
        audio=NULL;
    }

    decoder=new cMarkAdDecoder(RecvNumber,useH264,macontext.General.DPid!=0);
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
        tempmark.Comment=strdup("stop of content (user)");
        AddMark(&tempmark);
        isyslog("markad [%i]: stop of content (user)",recvnumber);
        free(tempmark.Comment);
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

int cMarkAdReceiver::LastIFrame()
{
    if (!Index)
    {
        Index = new cIndexFile(filename,false);
        if (!Index)
        {
            esyslog("markad [%i]: ERROR can't allocate index",recvnumber);
            return -1;
        }
        else if (!Index->Ok())
        {
            // index file is not ready till now, try it later
            delete Index;
            Index=NULL;
            return -1;
        }
    }
    return Index->GetNextIFrame(Index->Last(),false,NULL,NULL,NULL,true);
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
void cMarkAdReceiver::AddMark(MarkAdMark *mark)
{
    if (!mark) return;
    if (!mark->Position) return;
    if (!mark->Comment) return;
    cMark *newmark=marks.Add(mark->Position);
    if (newmark)
    {
        if (newmark->comment) free(newmark->comment);
        newmark->comment=strdup(mark->Comment);
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
                AddMark(mark);
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
                            decoder->FindH262VideoInfos(&macontext,pkt,pktlen);
                            if (decoder->DecodeVideo(&macontext,pkt,pktlen))
                            {
                                mark=video->Process(lastiframe);
                                AddMark(mark);

                            }
                        }
                        tspkt+=len;
                        tslen-=len;
                    }
                }
                if (t.Elapsed()>100)
                {
                    isyslog("markad [%i]: 100ms exceeded -> %Li",
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
                                AddMark(mark);
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
                    int len=ac3_demux->Process(macontext.General.APid,tspkt,tslen,&pkt,&pktlen);
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


