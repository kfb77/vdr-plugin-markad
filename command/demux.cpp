/*
 * demux.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "demux.h"

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

void cMarkAdDemux::ProcessVDR(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Pkt) || (!PktLen)) return;
    *Pkt=NULL;
    *PktLen=0;

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES("PES2ES audio");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,Data,Count,Pkt,PktLen);
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        if (!pes2videoes)
        {
            if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
            {
                pes2videoes=new cMarkAdPES2ES("PES2H264ES video",425984);
            }
            else
            {
                pes2videoes=new cMarkAdPES2ES("PES2ES video",65536);
            }
        }
        if (!pes2videoes) return;
        pes2videoes->Process(Pid,Data,Count,Pkt,PktLen);
    }

    return;
}

void cMarkAdDemux::ProcessTS(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Pkt) || (!PktLen)) return;
    *Pkt=NULL;
    *PktLen=0;

    uchar *pkt;
    int pktlen;

    if (!ts2pkt)
    {
        if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
        {
            ts2pkt=new cMarkAdTS2Pkt("TS2H264",819200);
        }
        else
        {
            ts2pkt=new cMarkAdTS2Pkt("TS2PKT",262144);
        }
    }
    if (!ts2pkt) return;

    ts2pkt->Process(Pid,Data,Count,&pkt,&pktlen);

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES("PES2ES audio");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,pkt,pktlen,Pkt,PktLen);
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        if ((pkt) && ((pkt[3] & 0xF0)==0xE0) && (pkt[4]!=0) && (pkt[5]!=0))
        {
            ts2pkt->InjectVideoPES(pkt,pktlen);
            pkt=NULL;
            pktlen=0;
        }
    }

    if ((pkt) && (!*Pkt))
    {
        *Pkt=pkt;
        *PktLen=pktlen;
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

int cMarkAdDemux::Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen, bool *Offcnt)
{
    if ((!Data) && (!Count) && (!Pkt) || (!PktLen)) return -1;

    *Pkt=NULL;
    *PktLen=0;

    uchar *in=NULL;
    int inlen=0;
    int retval=0;

    if (Offcnt) *Offcnt=false;

    if (!pause)
    {
        if (!min_needed)
        {
            if (skip)
            {
                int t_skip=skip;
                skip=0;
                if (Offcnt) *Offcnt=true;
                return t_skip;
            }

            int t_min_needed=GetMinNeeded(Pid,Data,Count,Offcnt);
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
        ProcessTS(Pid, in, inlen, Pkt, PktLen);
    }
    else
    {
        ProcessVDR(Pid, in, inlen, Pkt, PktLen);
    }

    if (*Pkt)
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

    if (Offcnt) *Offcnt=true;
    return retval;
}
