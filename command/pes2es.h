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

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "queue.h"

class cMarkAdPES2ES
{
private:
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

    cMarkAdPaketQueue *queue;
    int type;
    void Reset();
public:
    cMarkAdPES2ES(const char *QueueName="PES2ES", int QueueSize=32768);
    ~cMarkAdPES2ES();
    void Process(MarkAdPid Pid, uchar *PESData, int PESSize, uchar **ESData, int *ESSize);
};

#endif
