/*
 * pes2audioes.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __pes2audioes_h_
#define __pes2audioes_h_

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

class cMarkAdPES2AudioES
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

    struct MP2HDR
    {
unsigned Sync1:
        8;
unsigned Protection:
        1;
unsigned Layer:
        2;
unsigned MpegID:
        2;
unsigned Sync2:
        3;
unsigned Private:
        1;
unsigned Padding:
        1;
unsigned SampleRateIndex:
        2;
unsigned BitRateIndex:
        4;
unsigned Emphasis:
        2;
unsigned Original:
        1;
unsigned Copyright:
        1;
unsigned ModeExt:
        2;
unsigned Mode:
        2;
    };

#pragma pack(1)
    struct AC3HDR
    {
unsigned Sync1:
        8;
unsigned Sync2:
        8;
unsigned CRC1:
        8;
unsigned CRC2:
        8;
unsigned FrameSizeIndex:
        6;
unsigned SampleRateIndex:
        2;
    };
#pragma pack()

    uchar *esdatalast;
    uchar *esdata;
    int essize;
    bool data_left;

    int FindAudioHeader(uchar *PESData, int PESSize);
    bool IsValidAudioHeader(uchar *Data, int *FrameSize);
    void Reset();
public:
    cMarkAdPES2AudioES();
    ~cMarkAdPES2AudioES();
    int Process(uchar *PESData, int PESSize, uchar **ESData, int *ESSize);
};


#endif
