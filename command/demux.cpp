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
    vdr2pkt=NULL;
    pes2audioes=NULL;
    pes2videoes=NULL;
    pause=false;
    pause_retval=0;
    queue = new cMarkAdPaketQueue(NULL,376);
}

cMarkAdDemux::~cMarkAdDemux()
{
    if (ts2pkt) delete ts2pkt;
    if (vdr2pkt) delete vdr2pkt;
    if (pes2audioes) delete pes2audioes;
    if (pes2videoes) delete pes2videoes;
    if (queue) delete queue;
}

void cMarkAdDemux::ProcessVDR(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Pkt) || (!PktLen)) return;
    *Pkt=NULL;
    *PktLen=0;

    uchar *pkt;
    int pktlen;

    if (!vdr2pkt)
    {
        if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
        {
            vdr2pkt= new cMarkAdVDR2Pkt("VDR2PKT audio");
        }
        else
        {
            vdr2pkt= new cMarkAdVDR2Pkt("VDR2PKT video");
        }
    }
    if (!vdr2pkt) return;

    vdr2pkt->Process(Pid,Data,Count,&pkt,&pktlen);

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES("PES2ES audio");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,pkt,pktlen,Pkt,PktLen);
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        if (!pes2videoes)
        {
            if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
            {
                pes2videoes=new cMarkAdPES2ES("PES2H264ES video",393216);
            }
            else
            {
                pes2videoes=new cMarkAdPES2ES("PES2ES video",65536);
            }
        }
        if (!pes2videoes) return;
        pes2videoes->Process(Pid,pkt,pktlen,Pkt,PktLen);
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

int cMarkAdDemux::Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Data) && (!Count) && (!Pkt) || (!PktLen)) return -1;

    *Pkt=NULL;
    *PktLen=0;

    uchar *in=NULL;
    int inlen=0;
    int retval=0;

    if (!pause)
    {
        int min_needed=TS_SIZE;

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
        if (queue->Length()<min_needed) return Count;

        inlen=TS_SIZE;
        in=queue->Get(&inlen);
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

    return retval;
}
