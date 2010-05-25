/*
 * ts2pkt.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __ts2pkt_h_
#define __ts2pkt_h_

extern "C"
{
#include "debug.h"
}

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "queue.h"

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

    struct PESHDR
    {
        uchar Sync1;
        uchar Sync2;
        uchar Sync3;
        uchar StreamID;
        uchar LenH;
        uchar LenL;
    };

#pragma pack(1)
    struct PESHDROPT
    {
unsigned OOC:
        1;
unsigned CY:
        1;
unsigned DAI:
        1;
unsigned PESP:
        1;
unsigned PESSC:
        2;
unsigned MarkerBits:
        2;
unsigned EXT:
        1;
unsigned CRC:
        1;
unsigned ACI:
        1;
unsigned TM:
        1;
unsigned RATE:
        1;
unsigned ESCR:
        1;
unsigned TSF:
        2;
unsigned Length:
        8;
    };
#pragma pack()

    int counter;
    bool sync;

    cMarkAdPaketQueue *queue;

#define MA_ERR_STARTUP 0
#define MA_ERR_TSSIZE 1
#define MA_ERR_NOSYNC 2
#define MA_ERR_SEQ 3
#define MA_ERR_AFC 4
#define MA_ERR_TOBIG 5
#define MA_ERR_NEG 6
    void Reset(int ErrIndex=MA_ERR_STARTUP);
public:
    cMarkAdTS2Pkt(const char *QueueName="TS2Pkt", int QueueSize=32768);
    ~cMarkAdTS2Pkt();
    void Clear();
    void Process(MarkAdPid Pid,uchar *TSData, int TSSize, uchar **PktData, int *PktSize);
    bool InjectVideoPES(uchar *PESData, int PESSize);
};

#endif
