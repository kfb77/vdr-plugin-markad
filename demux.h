/*
 * demux.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __demux_h_
#define __demux_h_

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#ifndef VDR_SIZE
#define VDR_SIZE 2048
#endif

#include "global.h"
#include "queue.h"
#include "vdr2pkt.h"
#include "ts2pkt.h"
#include "pes2es.h"

#if 1
#include <unistd.h>
#endif

class cMarkAdDemux
{
private:
    int recvnumber;
    cMarkAdVDR2Pkt *vdr2pkt;
    cMarkAdTS2Pkt *ts2pkt;
    cMarkAdPES2ES *pes2audioes;
    cMarkAdPES2ES *pes2videoes;
    cMarkAdPaketQueue *queue;

    bool pause;
    int pause_retval;

    void ProcessTS(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
    void ProcessVDR(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
public:
    cMarkAdDemux(int RecvNumber);
    ~cMarkAdDemux();
    int Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
};

#endif
