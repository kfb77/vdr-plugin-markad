/*
 * demux.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "demux.h"

#include <string.h>

cMarkAdDemux::cMarkAdDemux()
{
    ts2pkt=NULL;
    pes2audioes=NULL;
    pes2videoes=NULL;
    pause=false;
    pause_retval=0;
    min_needed=0;
    skip=0;
    queue = new cMarkAdPaketQueue(NULL,2176);
}

cMarkAdDemux::~cMarkAdDemux()
{
    if (ts2pkt) delete ts2pkt;
    if (pes2audioes) delete pes2audioes;
    if (pes2videoes) delete pes2videoes;
    if (queue) delete queue;
}

void cMarkAdDemux::Clear()
{
    if (ts2pkt) ts2pkt->Clear();
    if (pes2audioes) pes2audioes->Clear();
    if (pes2videoes) pes2videoes->Clear();
    if (queue) queue->Clear();
    pause=false;
    pause_retval=0;
    min_needed=0;
    skip=0;
}

void cMarkAdDemux::GetVideoPTS(uchar *Data, int Count, unsigned int *Timestamp)
{
    if (!Data) return;
    if (Count<=0) return;
    if (!Timestamp) return;
    struct PESHDR *peshdr=(struct PESHDR *) Data;

    if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1)) return;
    if ((peshdr->StreamID & 0xF0)!=0xE0) return;

    struct PESHDROPT *peshdropt=(struct PESHDROPT *) &Data[sizeof(struct PESHDR)];
    if (peshdropt->MarkerBits!=0x2) return;

    if (peshdropt->PTSDTS<2) return;

    struct PESHDROPTPTS *peshdroptpts=(struct PESHDROPTPTS *) &Data[sizeof(struct PESHDR)+
                                                  sizeof(struct PESHDROPT)];

    if (peshdroptpts->Marker1 && peshdroptpts->Marker2 &&
            peshdroptpts->Marker3)
{
        unsigned int pts=0;
        pts|=((peshdroptpts->PTS29_15_H<<7|peshdroptpts->PTS29_15_L)<<15);
        pts|=(peshdroptpts->PTS14_0_H<<7|peshdroptpts->PTS14_0_L);
        pts|=(peshdroptpts->PTS32_30<<30);
        *Timestamp=pts;
    }
    return;
}

void cMarkAdDemux::ProcessVDR(MarkAdPid Pid, uchar *Data, int Count, MarkAdPacket *Pkt)
{
    if (!Pkt) return;

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) ?
                    "PES2ES AC3" : "PES2ES MP2");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,Data,Count,Pkt);
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        if (!pes2videoes)
        {
            if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
            {
                pes2videoes=new cMarkAdPES2ES("PES2H264ES",425984);
            }
            else
            {
                pes2videoes=new cMarkAdPES2ES("PES2H262ES",65536);
            }
        }
        if (!pes2videoes) return;
        pes2videoes->Process(Pid,Data,Count,Pkt);
    }

    return;
}

void cMarkAdDemux::ProcessTS(MarkAdPid Pid, uchar *Data, int Count, MarkAdPacket *Pkt)
{
    if (!Pkt) return;

    MarkAdPacket pkt;
    memset(&pkt,0,sizeof(pkt));

    if (!ts2pkt)
    {
        switch (Pid.Type)
        {
        case MARKAD_PIDTYPE_VIDEO_H264:
            ts2pkt=new cMarkAdTS2Pkt("TS2H264",819200);
            break;

        case MARKAD_PIDTYPE_VIDEO_H262:
            ts2pkt=new cMarkAdTS2Pkt("TS2H262",262144);
            break;

        case MARKAD_PIDTYPE_AUDIO_AC3:
            ts2pkt=new cMarkAdTS2Pkt("TS2PES AC3",32768);
            break;

        case MARKAD_PIDTYPE_AUDIO_MP2:
            ts2pkt=new cMarkAdTS2Pkt("TS2PES MP2",16384);
            break;
        }
    }
    if (!ts2pkt) return;

    if (!ts2pkt->Process(Pid,Data,Count,&pkt))
    {
        if (pes2audioes) pes2audioes->Clear();
        return;
    }

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) ?
                    "PES2ES AC3" : "PES2ES MP2");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,pkt.Data,pkt.Length,Pkt);
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        GetVideoPTS(pkt.Data,pkt.Length,&Pkt->Timestamp);
        if ((pkt.Data) && ((pkt.Data[3] & 0xF0)==0xE0) && (pkt.Data[4]!=0) && (pkt.Data[5]!=0))
        {
            ts2pkt->InjectVideoPES(pkt.Data,pkt.Length);
            pkt.Data=NULL;
            pkt.Length=0;
        }
        Pkt->Data=pkt.Data;
        Pkt->Length=pkt.Length;
    }
    return;
}

int cMarkAdDemux::GetMinNeeded(MarkAdPid Pid, uchar *Data, int Count, bool *Offcnt)
{
    if (Pid.Num>=0) return TS_SIZE;

    uchar *qData=queue->Peek(PESHDRSIZE);
    if (!qData)
    {
        int len=PESHDRSIZE-queue->Length();
        int cnt=(Count>len) ? len : Count;
        queue->Put(Data,cnt);
        return -cnt;
    }

    int stream=qData[3];

    if ((qData[0]==0) && (qData[1]==0) && (qData[2]==1) && (stream>0xBC))
    {
        int needed=qData[4]*256+qData[5];

        if (((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) ||
                (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)) && ((stream & 0xF0)!=0xE0))
        {
            // ignore 6 header bytes from queue->Put above
            queue->Clear();
            if (Offcnt) *Offcnt=true;
            return -needed;
        }
        if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2) && ((stream & 0xF0)!=0xC0))
        {
            // ignore 6 header bytes from queue->Put above
            queue->Clear();
            if (Offcnt) *Offcnt=true;
            return -needed;
        }
        if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) && (stream!=0xBD))
        {
            // ignore 6 header bytes from queue->Put above
            queue->Clear();
            if (Offcnt) *Offcnt=true;
            return -needed;
        }
        return needed+PESHDRSIZE;
    }
    else
    {
        return 0;
    }
}

int cMarkAdDemux::Process(MarkAdPid Pid, uchar *Data, int Count, MarkAdPacket *Pkt)
{
    if ((!Data) && (!Count) && (!Pkt)) return -1;

    uchar *in=NULL;
    int inlen=0;
    int retval=0;

    if (!pause)
    {
        Pkt->Data=NULL;
        Pkt->Length=0;
        Pkt->Offcnt=false;

        if (!min_needed)
        {
            if (skip)
            {
                int t_skip=skip;
                skip=0;
                Pkt->Offcnt=true;
                return t_skip;
            }

            int t_min_needed=GetMinNeeded(Pid,Data,Count,&Pkt->Offcnt);
            if (t_min_needed==0)
            {
                return -1;
            }
            if (t_min_needed<0)
            {
                if (-t_min_needed>Count)
                {
                    skip=-t_min_needed-Count;
                    return Count;
                }
                return -t_min_needed;
            }
            min_needed=t_min_needed;
        }

        int needed=min_needed-queue->Length();

        if (Count>needed)
        {
            queue->Put(Data,needed);
            retval=needed;
        }
        else
        {
            queue->Put(Data,Count);
            retval=Count;
        }
        if (queue->Length()<min_needed)
        {
            return Count;
        }
        inlen=min_needed;
        in=queue->Get(&inlen);
        min_needed=0;
    }

    if (Pid.Num>=0)
    {
        ProcessTS(Pid, in, inlen, Pkt);
    }
    else
    {
        ProcessVDR(Pid, in, inlen, Pkt);
    }

    if (Pkt->Data)
    {
        if (!pause_retval) pause_retval=retval;
        pause=true;
        return 0;
    }

    if (pause)
    {
        if (pause_retval)
        {
            retval=pause_retval;
            pause_retval=0;
        }
        pause=false;
    }

    Pkt->Offcnt=true;
    return retval;
}
