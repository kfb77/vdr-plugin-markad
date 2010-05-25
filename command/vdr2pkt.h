/*
 * vdr2pkt.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __vdr2pkt_h_
#define __vdr2pkt_h_

#ifndef uchar
typedef unsigned char uchar;
#endif

#include "global.h"
#include "queue.h"

class cMarkAdVDR2Pkt
{
private:
    cMarkAdPaketQueue *queue;
public:
    cMarkAdVDR2Pkt(const char *QueueName="VDR2PKT", int QueueSize=32768);
    ~cMarkAdVDR2Pkt();
    void Clear();
    void Process(MarkAdPid Pid,uchar *VDRData, int VDRSize, uchar **PktData, int *PktSize);
};

#endif
