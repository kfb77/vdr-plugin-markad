/*
 * queue.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdlib.h>
#include <string.h>

extern "C"
{
#include "debug.h"
}

#include "queue.h"

cMarkAdPaketQueue::cMarkAdPaketQueue(const char *Name, int Size)
{
    inptr=0;
    outptr=0;
    memset(&pktinfo,0,sizeof(pktinfo));
    pktinfo.pkthdr=-1;
    maxqueue=Size;
    if (Name)
    {
        name=strdup(Name);
    }
    else
    {
        name=NULL;
    }
    buffer=(uchar *) malloc(Size+1);
    if (!buffer) maxqueue=0;
    scanner=0xFFFFFFFF;
    scannerstart=-1;
    percent=-1;
    mpercent=0;
}

cMarkAdPaketQueue::~cMarkAdPaketQueue()
{
    if (name)
    {
        tsyslog("buffer usage: %-15s %3i%%",name,mpercent);
        free(name);
    }
    if (buffer) free(buffer);
}

bool cMarkAdPaketQueue::Inject(uchar *Data, int Size)
{
    if (!buffer) return false;
    isyslog("inject was called, please report this");

    if (outptr>Size)
    {
        uchar *temp=(uchar *) alloca(Size+1);
        if (!temp) return false;
        memcpy(temp,Data,Size);
        outptr-=Size;
        memcpy(&buffer[outptr],temp,Size);
        pktinfo.pkthdr=-1;
    }
    else
    {
        int oldSize=Length();
        uchar *tempold=(uchar *) alloca(oldSize+1);
        if (!tempold) return false;
        uchar *temp=(uchar *) alloca(Size+1);
        if (!temp) return false;

        memcpy(tempold,&buffer[outptr],oldSize);
        memcpy(temp,Data,Size);
        memcpy(buffer,temp,Size);
        memcpy(buffer+Size,tempold,oldSize);

        inptr=Size+oldSize;
        outptr=0;
        pktinfo.pkthdr=-1;
    }
    return true;
}

bool cMarkAdPaketQueue::Put(uchar *Data, int Size)
{
    if (!buffer) return false;
    if ((inptr) && (inptr==outptr)) inptr=outptr=0;

    if ((inptr+Size)>maxqueue)
    {
        if (name)
        {
            esyslog("buffer %s full",name);
        }
        else
        {
            esyslog("buffer full");
        }
        Clear();
        return false;
    }

    memcpy(&buffer[inptr],Data,Size);
    inptr+=Size;

    int npercent=(int) ((inptr*100)/maxqueue);
    if (npercent>mpercent) mpercent=npercent;

    if ((npercent>90) && (name) && (npercent!=percent))
    {
        dsyslog("buffer %s usage: %3i%%",
                name,npercent);
        percent=npercent;
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

uchar *cMarkAdPaketQueue::Peek(int Size)
{
    if (!buffer) return NULL;
    if (!Size) return NULL;
    if (Length()<Size) return NULL;
    uchar *ret=&buffer[outptr];
    return ret;
}

int cMarkAdPaketQueue::FindPktHeader(int Start, int *StreamSize,int *HeaderSize, bool LongStartCode)
{
    if ((!StreamSize) || (!HeaderSize)) return -1;
    if (!Start) Start=outptr;
    if (Start>=inptr) return -1;
    *StreamSize=0;
    if (LongStartCode)
    {
        *HeaderSize=4; // 0x0 0x0 0x0 0x1
    }
    else
    {
        *HeaderSize=3; // 0x0 0x0 0x1
    }
    int i;

    if (scanner!=0xFFFFFFFF)
    {
        scanner<<=8;
        scanner|=buffer[Start++];
    }

    for (i=Start; i<inptr; i++)
    {
        if (LongStartCode)
        {
            if (scanner==1L) break;
            if ((scanner & 0xFFFFFFF0)==0x1E0L) break;
        }
        else
        {
            if ((scanner & 0x00FFFFFF)==1L) break;
        }
        scanner<<=8;
        scanner|=buffer[i];
    }

    if (i==inptr) return -1;
    if (LongStartCode) i--;
    if (buffer[i]>=0xBC)// do we have a PES packet?
    {
#define PESHDRSIZE 6
        if ((i+PESHDRSIZE)>inptr)
        {
            return -1; // we need more data (for streamsize and headersize)
        }

        *StreamSize=(buffer[i+1]<<8)+buffer[i+2];
        if (*StreamSize) (*StreamSize)+=PESHDRSIZE; // 6 Byte PES-Header
        if (LongStartCode)
        {
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
    }

    return i-3;
}

int cMarkAdPaketQueue::FindAudioHeader(int Start, int *FrameSize, int *HeaderSize, bool AC3)
{
    if ((!FrameSize) || (!HeaderSize)) return -1;
    if (!Start) Start=outptr;
    if (Start>=inptr) return -1;
    (*FrameSize)=0;
    if (AC3)
    {
        (*HeaderSize)=2;
    }
    else
    {
        (*HeaderSize)=3;
    }
    int i;

    if (scanner!=0xFFFFFFFF)
    {
        scanner<<=8;
        scanner|=buffer[Start++];
    }

    for (i=Start; i<inptr; i++)
    {

        if (AC3)
        {
            if ((scanner & 0x0000FFFF)==0xB77L) break;
        }
        else
        {
            if ((scanner & 0x00000FFE)==0xFFEL) break;
        }

        scanner<<=8;
        scanner|=buffer[i];
    }
    if (i==inptr) return -1;
    if (AC3) i-=2;

    if (AC3)
    {
        struct AC3HDR *ac3hdr = (struct AC3HDR *) &buffer[i];

        if (ac3hdr->SampleRateIndex==3) return -1; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return -1; // reserved

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
    if (Length()<4) return NULL;

    if ((Type==MA_PACKET_H264) && (pktinfo.pktsyncsize>5) && (pktinfo.pkthdr!=-1))
    {
        // ignore PES paket
        pktinfo.pkthdr=-1;
        outptr+=pktinfo.pktsyncsize;
    }

    if (pktinfo.pkthdr==-1)
    {
        scanner=0xFFFFFFFF;
        switch (Type)
        {
        case MA_PACKET_AC3:
            pktinfo.pkthdr=FindAudioHeader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize, true);
            break;
        case MA_PACKET_MP2:
            pktinfo.pkthdr=FindAudioHeader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize, false);
            break;
        case MA_PACKET_H264:
            pktinfo.pkthdr=FindPktHeader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,true);
            if (pktinfo.pktsyncsize>5)
            {
                // ignore PES paket
                pktinfo.pkthdr=-1;
                outptr+=pktinfo.pktsyncsize;
            }
            break;
        default:
            pktinfo.pkthdr=FindPktHeader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,false);
            break;
        }

        if (pktinfo.pkthdr==-1)
        {
            return NULL;
        }
        scannerstart=pktinfo.pkthdr+pktinfo.pktsyncsize;
    }

    int streamsize,pktsyncsize,pkthdr=-1;

    if (pktinfo.streamsize)
    {
        if ((pktinfo.pkthdr+pktinfo.streamsize)>inptr)
        {
            return NULL; // need more data
        }
        else
        {
            scannerstart=pktinfo.pkthdr+pktinfo.streamsize;
            scanner=0xFFFFFFFF;
        }
    }

    switch (Type)
    {
    case MA_PACKET_AC3:
        pkthdr=FindAudioHeader(scannerstart,&streamsize,&pktsyncsize, true);
        break;
    case MA_PACKET_MP2:
        pkthdr=FindAudioHeader(scannerstart,&streamsize,&pktsyncsize, false);
        break;
    case MA_PACKET_H264:
        pkthdr=FindPktHeader(scannerstart,&streamsize,&pktsyncsize, true);
        break;
    default:
        pkthdr=FindPktHeader(scannerstart,&streamsize,&pktsyncsize, false);
        break;
    }

    if (pkthdr==-1)
    {
        scannerstart=inptr;
        return NULL;
    }
    scannerstart=pkthdr+pktsyncsize;

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
        scannerstart-=outptr;
        inptr=bytesleft;
        outptr=0;
        pkthdr=0;
    }

    pktinfo.pkthdr=pkthdr;
    pktinfo.streamsize=streamsize;
    pktinfo.pktsyncsize=pktsyncsize;

    return ptr;
}
