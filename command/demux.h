/*
 * demux.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __demux_h_
#define __demux_h_

#include <stdint.h>

#ifndef uchar
typedef unsigned char uchar;
#endif

#define PACKET_MASK  0xF0
#define PACKET_VIDEO 0x10
#define PACKET_H262  0x10 // 0x00 0x00 0x01 (PES / H262)
#define PACKET_H264  0x11 // 0x00 0x00 0x00 0x01 (H264)
#define PACKET_AUDIO 0x20
#define PACKET_AC3   0x20
#define PACKET_MP2   0x21
#define PACKET_TS    0x30
#define PACKET_PES   0x40

typedef struct AvPacket
{
    uchar *Data;
    int Length;
    int Type;
    int Stream;
} AvPacket;

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
#pragma pack()

// ----------------------------------------------------------------------------

class cPaketQueue
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

private:
    const char *name;
    struct pktinfo
    {
        int pkthdr;
        int pktsyncsize;
        int streamsize;
        bool ispes;
    } pktinfo;

    int percent;
    int mpercent; // max percentage use

    uchar *buffer;
    int maxqueue;
    int inptr;
    int outptr;

    int skipped;

    uint32_t scanner;
    int scannerstart;

    int findaudioheader(int start, int *framesize, int *headersize, bool ac3);
    int findpktheader(int start, int *streamsize,int *headersize, bool longstartcode, bool pesonly);
public:
    cPaketQueue(const char *Name, int Size=32768);
    ~cPaketQueue();
    int Length()
    {
        return inptr-outptr;
    }
    const char *Name()
    {
        return name;
    }
    void Clear();
    int Skipped()
    {
        int temp=skipped;
        skipped=0;
        return temp;
    }
    bool Put(uchar *Data, int Size);
    uchar *Get(int *Size);
    uchar *Peek(int Size);
    uchar *GetPacket(int *Size, int Type);
    void Resize(int NewSize, const char *NewName=NULL);
    bool NeedMoreData()
    {
        if (scannerstart==-1) return true;
        if (pktinfo.streamsize)
        {
            return (((inptr-pktinfo.pkthdr)-outptr)<=(pktinfo.streamsize+6));
        }
        else
        {
            return ((scannerstart==inptr) || (scannerstart==outptr));
        }
    }
    int FindPESHeader(int Start);
    int FindTSHeader(int Start);
};

// ----------------------------------------------------------------------------

class cTS2Pkt
{
    enum
    {
        ERR_INIT=0,
        ERR_SEQUENCE,
        ERR_PAYLOAD,
        ERR_HDRBIT,
        ERR_AFCLEN,
        ERR_DUPLICATE,
        ERR_SYNC
    };

private:
    cPaketQueue *queue;
    int counter;
    bool sync;
    bool firstsync;
    int skipped;
    int pid;
    int lasterror;
    bool h264;
    bool noticeFILLER;
public:
    cTS2Pkt(int Pid, const char *QueueName="TS2Pkt", int QueueSize=32768, bool H264=false);
    ~cTS2Pkt();
    void Clear(AvPacket *Pkt=NULL);
    int Skipped()
    {
        return skipped;
    }
    bool Process(uchar *TSData, int TSSize, AvPacket *Pkt);
    void Resize(int NewQueueSize, const char *NewQueueName)
    {
        queue->Resize(NewQueueSize, NewQueueName);
    }
    bool NeedMoreData()
    {
        if (queue)
        {
            return queue->NeedMoreData();
        }
        else
        {
            return false;
        }
    }
};

// ----------------------------------------------------------------------------

class cPES2ES
{
    enum
    {
        ERR_INIT,
        ERR_LENGTH,
        ERR_PADDING
    };

private:
    cPaketQueue *queue;
    int skipped;
    int ptype;
    int stream;
    int lasterror;
    bool h264;
public:
    cPES2ES(int PacketType, const char *QueueName="PES2ES", int QueueSize=32768);
    ~cPES2ES();
    void Clear();
    bool Process(uchar *PESData, int PESSize, AvPacket *ESPkt);
    int Skipped()
    {
        return skipped;
    }
    bool NeedMoreData()
    {
        if (queue)
        {
            return queue->NeedMoreData();
        }
        else
        {
            return false;
        }
    }
};

// ----------------------------------------------------------------------------

class cDemux
{
    enum
    {
        ERR_INIT=0,
        ERR_JUNK,
        ERR_BROKEN
    };

private:
    int vpid,dpid,apid;
    int stream_or_pid;
    int skipped;
    int lasterror;
    bool h264;
    bool TS;
    uint64_t offset;
    uint64_t rawoffset;
    int from_oldfile;
    int last_bplen;

    bool raw;

    bool vdrcount;
    int vdroffset;
    bool vdraddpatpmt(uchar *data, int count);

    void addoffset();

    cPaketQueue *queue;
    cPES2ES *pes2videoes;
    cPES2ES *pes2audioes_mp2;
    cPES2ES *pes2audioes_ac3;
    cTS2Pkt *ts2pkt_vpid;
    cTS2Pkt *ts2pkt_dpid;
    cTS2Pkt *ts2pkt_apid;
    bool needmoredata();
    int checkts(uchar *data, int count, int &pid);
    bool isvideopes(uchar *data, int count);
    int fillqueue(uchar *data, int count, int &stream_or_pid, int &packetsize, int &readout);
public:
    cDemux(int VPid, int DPid, int APid, bool H264=false, bool VDRCount=false, bool RAW=false);
    ~cDemux();
    void DisableDPid();
    void Clear();
    bool Empty()
    {
        return queue ? (queue->Length()==0) : true;
    }
    int Skipped();
    void NewFile();
    uint64_t Offset()
    {
        return offset;
    }
    int Process(uchar *Data, int Count, AvPacket *pkt);
};

#endif
