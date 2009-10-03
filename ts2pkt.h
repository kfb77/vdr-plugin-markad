/*
 * ts2pkt.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __ts2pkt_h_
#define __ts2pkt_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "tools.h"

class cMarkAdTS2Pkt
{
private:
    struct TSHDR
    {
unsigned Sync:
        8;
unsigned PidH:
        5;
unsigned Priority:
        1;
unsigned PayloadStart:
        1;
unsigned TError:
        1;
unsigned PidL:
        8;
unsigned Counter:
        4;
unsigned AFC:
        2;
unsigned TSC:
        2;
    };

    struct TSADAPT
    {
unsigned Len:
        8;
unsigned Flags:
        8;
    };

    int recvnumber;
    int counter;

    cMarkAdPaketQueue *queue;

#define MA_ERR_STARTUP 0
#define MA_ERR_TOSMALL 1
#define MA_ERR_NOSYNC 2
#define MA_ERR_SEQ 3
#define MA_ERR_AFC 4
#define MA_ERR_TOBIG 5
#define MA_ERR_NEG 6
    void Reset(int ErrIndex=MA_ERR_STARTUP);
public:
    cMarkAdTS2Pkt(int RecvNumber, const char *QueueName="TS2Pkt", int QueueSize=32768);
    ~cMarkAdTS2Pkt();
    void Process(MarkAdPid Pid,uchar *TSData, int TSSize, uchar **PktData, int *PktSize);
};

#endif
