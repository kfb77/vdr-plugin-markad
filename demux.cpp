/*
 * demux.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "demux.h"

cMarkAdDemux::cMarkAdDemux(int RecvNumber)
{
    recvnumber=RecvNumber;
    ts2pkt=NULL;
    vdr2pkt=NULL;
    pes2audioes=NULL;
    pes2videoes=NULL;
    queue = new cMarkAdPaketQueue(RecvNumber,"Demux");
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

    if (!vdr2pkt) vdr2pkt= new cMarkAdVDR2Pkt(recvnumber);
    if (!vdr2pkt) return;

    vdr2pkt->Process(Pid,Data,Count,&pkt,&pktlen);

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES(recvnumber,"PES2ES audio");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,pkt,pktlen,Pkt,PktLen);
        return;
    }

    if ((Pid.Type==MARKAD_PIDTYPE_VIDEO_H262) || (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264))
    {
        if (!pes2videoes) pes2videoes=new cMarkAdPES2ES(recvnumber,"PES2ES video",262144);
        if (!pes2videoes) return;
        pes2videoes->Process(Pid,pkt,pktlen,Pkt,PktLen);
        return;
    }
    return;
}

void cMarkAdDemux::ProcessTS(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Pkt) || (!PktLen) || (!Data)) return;
    *Pkt=NULL;
    *PktLen=0;

    uchar *pkt;
    int pktlen;

    if (!ts2pkt) ts2pkt=new cMarkAdTS2Pkt(recvnumber);
    if (!ts2pkt) return;

    ts2pkt->Process(Pid,Data,Count,&pkt,&pktlen);

    if ((Pid.Type==MARKAD_PIDTYPE_AUDIO_AC3) || (Pid.Type==MARKAD_PIDTYPE_AUDIO_MP2))
    {
        if (!pes2audioes) pes2audioes=new cMarkAdPES2ES(recvnumber,"PES2ES audio");
        if (!pes2audioes) return;
        pes2audioes->Process(Pid,pkt,pktlen,Pkt,PktLen);
        return;
    }

    if (pkt)
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

    int retval;
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

    uchar *in;
    int inlen=TS_SIZE;
    in=queue->Get(&inlen);

    if (Pid.Num>=0)
    {
        ProcessTS(Pid, in, inlen, Pkt, PktLen);
    }
    else
    {
        ProcessVDR(Pid, in, inlen, Pkt, PktLen);
    }
    return retval;
}
