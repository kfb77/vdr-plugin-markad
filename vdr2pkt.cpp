/*
 * vdr2pkt.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "vdr2pkt.h"

cMarkAdVDR2Pkt::cMarkAdVDR2Pkt(const char *QueueName, int QueueSize)
{
    queue = new cMarkAdPaketQueue(QueueName,QueueSize);
}

cMarkAdVDR2Pkt::~cMarkAdVDR2Pkt()
{
    if (queue) delete queue;
}

void cMarkAdVDR2Pkt::Process(MarkAdPid Pid, uchar *VDRData, int VDRSize, uchar **PktData, int *PktSize)
{
    if ((!PktData) || (!PktSize) || (!queue)) return;
    if ((Pid.Type!=MARKAD_PIDTYPE_VIDEO_H262) &&
            (Pid.Type!=MARKAD_PIDTYPE_VIDEO_H264)) return;

    *PktData=NULL;
    *PktSize=0;

    if (VDRData) queue->Put(VDRData,VDRSize);
    *PktData=queue->GetPacket(PktSize,MA_PACKET_PKT);
    return;
}
