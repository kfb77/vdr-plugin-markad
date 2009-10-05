/*
 * tools.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __tools_h_
#define __tools_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <string.h>

class cMarkAdPaketQueue
{
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

private:
    char *name;
    int recvnumber;
    struct pktinfo
    {
        int pkthdr;
        int pkthdrsize;
        int streamsize;
    } pktinfo;

    uchar *buffer;
    int maxqueue;
    int inptr;
    int outptr;
    int FindPktHeader(int Start, int *StreamSize,int *HeaderSize);
    int FindAudioHeader(int Start, int *FrameSize, int *HeaderSize, bool AC3);
public:
    cMarkAdPaketQueue(int RecvNumber, const char *Name, int Size=32768);
    ~cMarkAdPaketQueue();
    int Length()
    {
        return inptr-outptr;
    }
    void Clear()
    {
        inptr=outptr=0;
        pktinfo.pkthdr=-1;
    }
    bool Inject(uchar *Data, int Size);
    bool Put(uchar *Data, int Size);
    uchar *Get(int *Size);

#define MA_PACKET_PKT 1
#define MA_PACKET_AC3 2
#define MA_PACKET_MP2 3

    uchar *GetPacket(int *Size, int Type);
};

#endif
