/*
 * ts2pes.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __ts2pes_h_
#define __ts2pes_h_

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <stdlib.h>
#include <string.h>

#include "global.h"

class cMarkAdTS2PES
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

    uchar *pesdatalast;
    uchar *pesdata;
    int pessize;
    bool data_left;
    int counter;
    bool sync;

    void Reset();
    int FindPESHeader(uchar *TSData, int TSSize, int *StreamSize, int *HeaderSize);
public:
    cMarkAdTS2PES();
    ~cMarkAdTS2PES();
    int Process(MarkAdPid Pid,uchar *TSData, int TSSize, uchar **PESData, int *PESSize);
};

#endif
