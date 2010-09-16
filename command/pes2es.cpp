/*
 * pes2es.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "pes2es.h"
#include <stdio.h>
cMarkAdPES2ES::cMarkAdPES2ES(const char *QueueName, int QueueSize)
{
    queue = new cMarkAdPaketQueue(QueueName,QueueSize);
    type=0;
}

cMarkAdPES2ES::~cMarkAdPES2ES()
{
    if (queue) delete queue;
}

void cMarkAdPES2ES::Clear()
{
    if (queue) queue->Clear();
}

void cMarkAdPES2ES::Process(MarkAdPid Pid, uchar *PESData, int PESSize, MarkAdPacket *ESPkt)
{
    if (!ESPkt) return;

    if (PESData)
    {
        struct PESHDR *peshdr=(struct PESHDR *) PESData;

        // first check some simple things
        if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1))
        {
            Clear();
            return;
        }

        if (peshdr->StreamID<=0xBC) return;

        int Length=(peshdr->LenH<<8)+peshdr->LenL;
        if (Length) Length+=sizeof(PESHDR);
        if (Length!=PESSize)
        {
            if ((peshdr->StreamID & 0xF0)==0xE0) return;
            Clear();
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
            Clear();
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
            if (peshdropt->PTSDTS>1)
            {
                struct PESHDROPTPTS *peshdroptpts=(struct PESHDROPTPTS *) &PESData[sizeof(struct PESHDR)+
                                                              sizeof(struct PESHDROPT)];

                if (peshdroptpts->Marker1 && peshdroptpts->Marker2 && peshdroptpts->Marker3)
        {
                    unsigned int pts=0;
                    pts|=((peshdroptpts->PTS29_15_H<<7|peshdroptpts->PTS29_15_L)<<15);
                    pts|=(peshdroptpts->PTS14_0_H<<7|peshdroptpts->PTS14_0_L);
                    pts|=(peshdroptpts->PTS32_30<<30);
                    ESPkt->Timestamp=pts;
                }
            }
        }
        else
        {
            int bpos=sizeof(struct PESHDR);
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
        }
        queue->Put(buf,buflen);
    }
    if (type) ESPkt->Data=queue->GetPacket(&ESPkt->Length,type);
    return;
}
