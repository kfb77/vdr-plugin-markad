/*
 * pes2es.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __pes2es_h_
#define __pes2es_h_

#ifndef uchar
typedef unsigned char uchar;
#endif

#include "global.h"
#include "queue.h"

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
unsigned PTSDTS:
    2;
unsigned Length:
    8;
};

struct PESHDROPTPTS
{
unsigned Marker1:
    1;
unsigned PTS32_30:
    3;
unsigned Fixed:
    4;
unsigned PTS29_15_H:
    8;
unsigned Marker2:
    1;
unsigned PTS29_15_L:
    7;
unsigned PTS14_0_H:
    8;
unsigned Marker3:
    1;
unsigned PTS14_0_L:
    7;
};
#pragma pack()

class cMarkAdPES2ES
{
private:
    cMarkAdPaketQueue *queue;
    int type;
public:
    cMarkAdPES2ES(const char *QueueName="PES2ES", int QueueSize=32768);
    ~cMarkAdPES2ES();
    void Clear();
    void Process(MarkAdPid Pid, uchar *PESData, int PESSize, MarkAdPacket *ESPkt);
};

#endif
