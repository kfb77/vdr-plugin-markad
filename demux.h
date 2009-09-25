/*
 * demux.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __demux_h_
#define __demux_h_

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#include "global.h"
#include "ts2pkt.h"
#include "pes2audioes.h"

class cMarkAdDemux
{
private:
    cMarkAdTS2Pkt *ts2pkt;
    cMarkAdPES2AudioES *pes2audioes;
    uchar *pkt;
    uchar *pesptr; // pointer into pkt

    uchar *tsdata;
    int tssize;
    uchar *tsptr;

    int pktlen;
public:
    cMarkAdDemux(int RecvNumber);
    ~cMarkAdDemux();
    int Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
};

#endif
