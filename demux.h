/*
 * demux.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __demux_h_
#define __demux_h_

#include "global.h"
#include "ts2pes.h"
#include "pes2audioes.h"

class cMarkAdDemux
{
private:
    cMarkAdTS2PES *ts2pes;
    cMarkAdPES2AudioES *pes2audioes;
    uchar *pespkt;
    uchar *pesptr; // pointer into pespkt
    int peslen;
public:
    cMarkAdDemux();
    ~cMarkAdDemux();
    int Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen);
};

#endif
