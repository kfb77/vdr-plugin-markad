/*
 * queue.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __queue_h_
#define __queue_h_

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
    struct pktinfo
    {
        int pkthdr;
        int pktsyncsize;
        int streamsize;
        bool ispes;
    } pktinfo;

    int percent;

    uchar *buffer;
    int maxqueue;
    int inptr;
    int outptr;

    uint32_t scanner;
    int scannerstart;

    int FindPktHeader(int Start, int *StreamSize,int *SyncSize, bool LongStartCode);
    int FindAudioHeader(int Start, int *FrameSize, int *SyncSize, bool AC3);
public:
    cMarkAdPaketQueue(const char *Name, int Size=32768);
    ~cMarkAdPaketQueue();
    int Length()
    {
        return inptr-outptr;
    }
    void Clear()
    {
        inptr=outptr=0;
        pktinfo.pkthdr=-1;
        scanner=0xFFFFFFFF;
        scannerstart=-1;
    }
    bool Inject(uchar *Data, int Size);
    bool Put(uchar *Data, int Size);
    uchar *Get(int *Size);

#define MA_PACKET_PKT		0x10 // 0x00 0x00 0x01 (PES / H262)
#define MA_PACKET_H264		0x11 // 0x00 0x00 0x00 0x01 (H264)
#define MA_PACKET_AC3		0x20
#define MA_PACKET_MP2		0x30

    uchar *GetPacket(int *Size, int Type);
};

#endif
