/*
 * pes2audioes.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "pes2audioes.h"

#include <stdio.h>

cMarkAdPES2AudioES::cMarkAdPES2AudioES()
{
    esdata=NULL;
    esdatalast=NULL;
    essize=0;
    data_left=false;
}

cMarkAdPES2AudioES::~cMarkAdPES2AudioES()
{
    if (esdata) free(esdata);
    if (esdatalast) free(esdatalast);
}

void cMarkAdPES2AudioES::Reset()
{
    if (esdata) free(esdata);
    esdata=NULL;
    essize=0;
    data_left=false;
}

int cMarkAdPES2AudioES::FindAudioHeader(uchar *ESData, int ESSize)
{
    unsigned short scanner=0x0;
    int i;
    for (i=0; i<ESSize; i++)
    {
        scanner|=ESData[i];
        if ((scanner==0x0B77) || ((scanner & 0xFFE0)==0xFFE0))
        {
            break;
        }
        scanner<<=8;

    }
    if (i!=ESSize)
    {
        return i-1;
    }
    return -1;
}


bool cMarkAdPES2AudioES::IsValidAudioHeader(uchar *Data,int *FrameSize)
{
    struct MP2HDR *mp2hdr = (struct MP2HDR *) Data;
    struct AC3HDR *ac3hdr = (struct AC3HDR *) Data;

    if ((ac3hdr->Sync1==0x0b) && (ac3hdr->Sync2==0x77))
    {
        if (ac3hdr->SampleRateIndex==3) return false; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return false; // reserved

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

    }
    else if ((mp2hdr->Sync1==0xFF) && (mp2hdr->Sync2==7))
    {
        if (mp2hdr->MpegID==1) return false; // reserved
        if (mp2hdr->Layer==0) return false; // reserved
        if (mp2hdr->BitRateIndex==0xF) return false; // forbidden
        if (mp2hdr->SampleRateIndex==3) return false; //reserved
        if (mp2hdr->Emphasis==2) return false; // reserved

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
    }
    else
    {
        return false;
    }
    return true;
}

int cMarkAdPES2AudioES::Process(uchar *PESData, int PESSize, uchar **ESData, int *ESSize)
{
    if ((!ESData) || (!ESSize)) return -1;
    *ESData=NULL;
    *ESSize=0;

    int bytes_processed;

    if (!data_left)
    {
        struct PESHDR *peshdr=(struct PESHDR *) PESData;

        // first check some simple things
        if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1))
        {
            Reset();
            return PESSize;
        }

        if ((peshdr->StreamID<0xC0) && (peshdr->StreamID>0xDF))
        {
            if (peshdr->StreamID!=0xBD) return PESSize; // no audio
        }

        int Length=(peshdr->LenH<<8)+peshdr->LenL;
        if (Length>PESSize)
        {
            Reset();
            return PESSize;
        }

        struct PESHDROPT *peshdropt=(struct PESHDROPT *) &PESData[sizeof(struct PESHDR)];

        uchar *buf;
        int buflen;

        if (peshdropt->MarkerBits==0x2)
        {
            // we have an optional PES header
            int bpos=sizeof(struct PESHDR)+sizeof(struct PESHDROPT)+
                     peshdropt->Length;
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
            bytes_processed=bpos;
        }
        else
        {
            int bpos=sizeof(struct PESHDR);
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
            bytes_processed=bpos;
        }
        esdata=(uchar *) realloc(esdata,essize+buflen);
        if (!esdata)
        {
            return -1;
            essize=0;
        }
        memcpy(esdata+essize,buf,buflen);
        essize+=buflen;
        bytes_processed+=buflen;
    }
    else
    {
        bytes_processed=essize;
        data_left=false;
    }

    int audiohdr=FindAudioHeader(esdata,essize);
    if (audiohdr!=-1)
    {
        int framesize;
        if (IsValidAudioHeader(&esdata[audiohdr],&framesize))
        {
            if ((essize-audiohdr)>=framesize)
            {
                *ESData=&esdata[audiohdr];
                *ESSize=framesize;
                if (esdatalast) free(esdatalast);
                esdatalast=esdata;

                int size=(essize-framesize-audiohdr);

                esdata=NULL;
                essize=0;

                if (size)
                {
                    void *ptr=malloc(size);
                    if (!ptr) return -1;

                    memcpy(ptr,(*ESData)+framesize,size);
                    bytes_processed-=size;

                    esdata=(uchar *) ptr;
                    essize=size;
                    data_left=true;
                }
            }
        }
    }
    return bytes_processed;
}
