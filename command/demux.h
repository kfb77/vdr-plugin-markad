/*
 * demux.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __demux_h_
#define __demux_h_

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#define PESHDRSIZE 6

#include "global.h"
#include "queue.h"
#include "ts2pkt.h"
#include "pes2es.h"

class cMarkAdDemux
{
private:
    cMarkAdTS2Pkt *ts2pkt;
    cMarkAdPES2ES *pes2audioes;
    cMarkAdPES2ES *pes2videoes;
    cMarkAdPaketQueue *queue;

    bool pause;
    int pause_retval;
    int min_needed;
    int skip;

    int GetMinNeeded(MarkAdPid Pid, uchar *Data, int Count, bool *Offcnt);
    void ProcessTS(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
    void ProcessVDR(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
public:
    cMarkAdDemux();
    ~cMarkAdDemux();
    void Clear();
    int Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen, bool *Offcnt);
};

#endif
