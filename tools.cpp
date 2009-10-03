/*
 * tools.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "tools.h"

cMarkAdPaketQueue::cMarkAdPaketQueue(int RecvNumber, const char *Name, int Size)
{
    recvnumber=RecvNumber;
    inptr=0;
    outptr=0;
    pktinfo.pkthdr=-1;
    maxqueue=Size;
    name=strdup(Name);
    buffer=(uchar *) malloc(Size+1);
    if (!buffer) maxqueue=0;
}

cMarkAdPaketQueue::~cMarkAdPaketQueue()
{
    if (name) free(name);
    if (buffer) free(buffer);
}

bool cMarkAdPaketQueue::Put(uchar *Data, int Size)
{
    if (!buffer) return false;
    if ((inptr) && (inptr==outptr)) inptr=outptr=0;

    if (((inptr+Size)>maxqueue) && (name))
    {
        esyslog("markad [%i]: buffer %s full",recvnumber,name);
        inptr=outptr=0;
    }

    memcpy(&buffer[inptr],Data,Size);
    inptr+=Size;

    if ((inptr>(0.9*maxqueue)) && (name))
    {
        dsyslog("markad [%i]: buffer %s usage: %3.f%%",recvnumber,
                name,((double) inptr/(double) maxqueue)*100);
    }

    return true;
}

uchar *cMarkAdPaketQueue::Get(int *Size)
{
    if (!buffer) return NULL;
    if (!Size) return NULL;
    if (Length()<*Size)
    {
        *Size=0;
        return NULL;
    }
    uchar *ret=&buffer[outptr];
    outptr+=*Size;
    return ret;
}

int cMarkAdPaketQueue::FindPktHeader(int Start, int *StreamSize,int *HeaderSize)
{
    if ((!StreamSize) || (!HeaderSize)) return -1;
    if (!Start) Start=outptr;
    if (Start>inptr) return -1;
    *StreamSize=0;
    *HeaderSize=4; // 0x0 0x0 0x1 0xNN
    unsigned long scanner=0xFFFFFFFF;
    int i;

    for (i=Start; i<inptr; i++)
    {
        scanner<<=8;
        if (scanner==0x00000100L)
        {
            break;
        }
        scanner|=buffer[i];
    }

    if (i==inptr) return -1;

    if (buffer[i]>=0xBC)// do we have a PES packet?
    {
#define PESHDRSIZE 6
        if ((i+PESHDRSIZE)>inptr)
        {
            return -1; // we need more data (for streamsize and headersize)
        }

        *StreamSize=(buffer[i+1]<<8)+buffer[i+2];
        if (*StreamSize) (*StreamSize)+=PESHDRSIZE; // 6 Byte PES-Header

        struct PESHDROPT *peshdropt=(struct PESHDROPT *) &buffer[i+3];
        if (peshdropt->MarkerBits==0x2)
        {
            *HeaderSize=PESHDRSIZE+sizeof(struct PESHDROPT)+
                        peshdropt->Length;
        }
        else
        {
            *HeaderSize=PESHDRSIZE;
        }
    }
    return i-3;
}

int cMarkAdPaketQueue::FindAudioHeader(int Start, int *FrameSize, int *HeaderSize, bool AC3)
{
    if (!Start) Start=outptr;
    if (Start>inptr) return -1;
    if (FrameSize) (*FrameSize)=0;
    if (HeaderSize) (*HeaderSize)=4;
    unsigned short scanner=0x0;
    int i;
    for (i=Start; i<inptr; i++)
    {
        scanner|=buffer[i];
        if (AC3)
        {
            if (scanner==0x0B77) break;
        }
        else
        {
            if ((scanner & 0xFFE0)==0xFFE0) break;
        }
        scanner<<=8;
    }
    if (i==inptr) return -1;
    i--;

    if (AC3)
    {
        struct AC3HDR *ac3hdr = (struct AC3HDR *) &buffer[i];

        if (ac3hdr->SampleRateIndex==3) return -1; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return -1; // reserved
        if (HeaderSize) (*HeaderSize)=sizeof(struct AC3HDR);

        if (FrameSize)
        {
            int bitRatesAC3[3][38] =   // all values are specified as kbits/s
            {
                { 64, 64, 80, 80, 96, 96, 112, 112, 128, 128, 160, 160, 192, 192,
                    224, 224, 256, 256, 320, 320, 384, 384, 448, 448, 512, 512,
                    640, 640, 768, 768, 896, 896, 1024, 1024, 1152, 1152, 1280, 1280 },  // 48kHz

                { 69, 70, 87, 88, 104, 105, 121, 122, 139, 140, 174, 175, 208, 209,
                  243, 244, 278, 279, 348, 349, 417, 418, 487, 488, 557, 558,
                  696, 697, 835, 836, 975, 976, 1114, 1115, 1253, 1254, 1393, 1394 },  // 44.1kHz

                { 96, 96, 120, 120, 144, 144, 168, 168, 192, 192, 240, 240, 288,
                  288, 336, 336, 384, 384, 480, 480, 576, 576, 672, 672, 768,
                  768, 960, 960, 1152, 1152, 1344, 1344, 1536, 1536, 1728, 1728, 1920,1920 }  // 32kHz
            };

            *FrameSize=2*bitRatesAC3[ac3hdr->SampleRateIndex][ac3hdr->FrameSizeIndex];
        }
        return i;
    }
    else
    {
        struct MP2HDR *mp2hdr = (struct MP2HDR *) &buffer[i];
        if (mp2hdr->MpegID==1) return -1; // reserved
        if (mp2hdr->Layer==0) return -1; // reserved
        if (mp2hdr->BitRateIndex==0xF) return -1; // forbidden
        if (mp2hdr->SampleRateIndex==3) return -1; //reserved
        if (mp2hdr->Emphasis==2) return -1; // reserved

        if (HeaderSize) (*HeaderSize)=sizeof(struct MP2HDR);

        if (FrameSize)
        {
            int bitRates[3][3][16] =   // all values are specified as kbits/s
            {
                {
                    { 0,  32,  64,  96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1 }, // M1, L1
                    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, 384, -1 }, // M1, L2
                    { 0,  32,  40,  48,  56,  64,  80,  96, 112, 128, 160, 192, 224, 256, 320, -1 }  // M1, L3
                },
                {
                    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, -1 }, // M2, L1
                    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1 }, // M2, L2
                    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1 }  // M2, L3
                },
                {
                    { 0,  32,  48,  56,  64,  80,  96, 112, 128, 144, 160, 176, 192, 224, 256, -1 }, // M2.5, L1
                    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1 }, // M2.5, L2
                    { 0,   8,  16,  24,  32,  40,  48,  56,  64,  80,  96, 112, 128, 144, 160, -1 }  // M2.5, L3
                }
            };

            int samplingFrequencies[3][4] =   // all values are specified in Hz
            {
                { 44100, 48000, 32000, -1 }, // MPEG 1
                { 22050, 24000, 16000, -1 }, // MPEG 2
                { 32000, 16000,  8000, -1 }  // MPEG 2.5
            };


            int slots_per_frame[3][3] =
            {
                { 12, 144, 144 }, // MPEG 1, Layer I, II, III
                { 12, 144,  72 },  // MPEG 2, Layer I, II, III
                { 12, 144,  72 }  // MPEG 2.5, Layer I, II, III
            };

            int mpegIndex;
            switch (mp2hdr->MpegID)
            {
            case 0:
                mpegIndex=2;
                break;
            case 2:
                mpegIndex=1;
                break;
            case 3:
                mpegIndex=0;
                break;
            default:
                mpegIndex=0; // just to get rid of compiler warnings ;)
            }
            int layerIndex = 3 - mp2hdr->Layer;

            // Layer I (i. e., layerIndex == 0) has a larger slot size
            int slotSize = (layerIndex == 0) ? 4 : 1; // bytes
            int sf = samplingFrequencies[mpegIndex][mp2hdr->SampleRateIndex];

            if (mp2hdr->BitRateIndex == 0)
                *FrameSize = 0; // "free" Bitrate -> we don't support this!
            else
            {
                int br = 1000 * bitRates[mpegIndex][layerIndex][mp2hdr->BitRateIndex]; // bits/s
                int N = slots_per_frame[mpegIndex][layerIndex] * br / sf; // slots

                *FrameSize = (N + mp2hdr->Padding) * slotSize; // bytes
            }
        }
        return i;
    }
}

uchar *cMarkAdPaketQueue::GetPacket(int *Size, int Type)
{
    if (!Size) return NULL;
    *Size=0;
    if (!Length()) return NULL;

    if (pktinfo.pkthdr==-1)
    {

        switch (Type)
        {
        case MA_PACKET_AC3:
            pktinfo.pkthdr=FindAudioHeader(0,&pktinfo.streamsize,&pktinfo.pkthdrsize, true);
            break;
        case MA_PACKET_MP2:
            pktinfo.pkthdr=FindAudioHeader(0,&pktinfo.streamsize,&pktinfo.pkthdrsize, false);
            break;
        default:
            pktinfo.pkthdr=FindPktHeader(0,&pktinfo.streamsize,&pktinfo.pkthdrsize);
        }

        if (pktinfo.pkthdr==-1)
        {
            return NULL;
        }
    }

    int start,streamsize,pkthdrsize,pkthdr=-1;

    if (pktinfo.streamsize)
    {
        if ((pktinfo.pkthdr+pktinfo.streamsize)>inptr)
        {
            return NULL; // need more data
        }
        else
        {
            start=pktinfo.pkthdr+pktinfo.streamsize;
        }
    }
    else
    {
        start=pktinfo.pkthdr+pktinfo.pkthdrsize;
    }

    switch (Type)
    {
    case MA_PACKET_AC3:
        pkthdr=FindAudioHeader(start,&streamsize,&pkthdrsize, true);
        break;
    case MA_PACKET_MP2:
        pkthdr=FindAudioHeader(start,&streamsize,&pkthdrsize, false);
        break;
    default:
        pkthdr=FindPktHeader(start,&streamsize,&pkthdrsize);
    }

    if (pkthdr==-1) return NULL;

    uchar *ptr=&buffer[pktinfo.pkthdr];

    if (pktinfo.streamsize)
    {
        *Size=pktinfo.streamsize;
    }
    else
    {
        *Size=pkthdr-pktinfo.pkthdr;
    }
    outptr=pkthdr;

    int bytesleft=inptr-outptr;
    if (pktinfo.pkthdr>(4096+bytesleft))
    {
        memcpy(buffer,&buffer[pkthdr],bytesleft);
        inptr=bytesleft;
        outptr=0;
        pkthdr=0;
    }

    pktinfo.pkthdr=pkthdr;
    pktinfo.streamsize=streamsize;
    pktinfo.pkthdrsize=pkthdrsize;

    return ptr;
}
