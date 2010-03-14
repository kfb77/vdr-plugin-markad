/*
 * pes2es.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "pes2es.h"

cMarkAdPES2ES::cMarkAdPES2ES(int RecvNumber, const char *QueueName, int QueueSize)
{
    queue = new cMarkAdPaketQueue(RecvNumber,QueueName,QueueSize);
    type=0;
}

cMarkAdPES2ES::~cMarkAdPES2ES()
{
    if (queue) delete queue;
}

void cMarkAdPES2ES::Reset()
{
    queue->Clear();
}

void cMarkAdPES2ES::Process(MarkAdPid Pid, uchar *PESData, int PESSize, uchar **ESData, int *ESSize)
{
    if ((!ESData) || (!ESSize) || (!queue)) return;
    *ESData=NULL;
    *ESSize=0;

    if (PESData)
    {
        struct PESHDR *peshdr=(struct PESHDR *) PESData;

        // first check some simple things
        if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1))
        {
            Reset();
            return;
        }

        if (peshdr->StreamID<=0xBC) return;

        int Length=(peshdr->LenH<<8)+peshdr->LenL;
        if (Length) Length+=sizeof(PESHDR);
        if (Length!=PESSize)
        {
            if ((peshdr->StreamID & 0xF0)==0xE0) return;
            Reset();
            return;
        }

        switch (Pid.Type)
        {
        case MARKAD_PIDTYPE_VIDEO_H262:
            if ((peshdr->StreamID & 0xF0)!=0xE0) return;
            type=MA_PACKET_PKT;
            break;
        case MARKAD_PIDTYPE_VIDEO_H264:
            if ((peshdr->StreamID & 0xF0)!=0xE0) return;
            type=MA_PACKET_H264;
            break;
        case MARKAD_PIDTYPE_AUDIO_AC3:
            if (peshdr->StreamID!=0xBD) return;
            type=MA_PACKET_AC3;
            break;
        case MARKAD_PIDTYPE_AUDIO_MP2:
            if ((peshdr->StreamID<0xC0) || (peshdr->StreamID>0xDF)) return;
            type=MA_PACKET_MP2;
            break;
        default:
            Reset();
            return;
        }

        struct PESHDROPT *peshdropt=(struct PESHDROPT *) &PESData[sizeof(struct PESHDR)];

        uchar *buf;
        int buflen;

        if (peshdropt->MarkerBits==0x2)
        {
            // we have an optional PES header
            int bpos=sizeof(struct PESHDR)+sizeof(struct PESHDROPT)+
                     peshdropt->Length;
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
        }
        else
        {
            int bpos=sizeof(struct PESHDR);
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
        }
        queue->Put(buf,buflen);
    }
    if (type) *ESData=queue->GetPacket(ESSize,type);
    return;
}
