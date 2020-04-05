/*
 * demux.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <string.h>
#include "demux.h"
extern "C"
{
#include "debug.h"
}

#include <stdlib.h>

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

cPaketQueue::cPaketQueue(const char *Name, int Size)
{
    maxqueue=Size;
    if (Name)
    {
        name=strdup(Name);
    }
    else
    {
        name=NULL;
    }
    buffer=(uchar *) malloc(Size+8);
    if (!buffer) maxqueue=0;
    memset(&pktinfo,0,sizeof(pktinfo));
    percent=-1;
    mpercent=0;
    inptr=outptr=0;
    skipped=0;
    Clear();
}

void cPaketQueue::Clear()
{
    skipped+=(inptr-outptr);
    inptr=outptr=0;
    pktinfo.pkthdr=-1;
    scanner=0xFFFFFFFF;
    scannerstart=-1;
}

cPaketQueue::~cPaketQueue()
{
    if (name)
    {
        tsyslog("buffer usage  : %-15s %3i%%",name,mpercent);
        free((void *) name);
    }
    if (buffer) free(buffer);
}

void cPaketQueue::Resize(int NewSize, const char *NewName)
{
    if (NewName)
    {
        if (name) free((void *) name);
        name=strdup(NewName);
    }
    if (inptr<NewSize)
    {
        uchar *tmp=(uchar *) realloc(buffer,NewSize+8);
        if (tmp)
        {
            buffer=tmp;
            maxqueue=NewSize;
        }
        else
        {
            free(buffer);
            buffer=NULL;
            maxqueue=0;
            Clear();
        }
    }
}

int cPaketQueue::findpktheader(int start, int *streamsize,int *headersize, bool longstartcode, bool pesonly=false)
{
    if ((!streamsize) || (!headersize)) return -1;
    if (!start) start=outptr;
    if (start>=inptr) return -1;
    *streamsize=0;
    if (longstartcode)
    {
        *headersize=4; // 0x0 0x0 0x0 0x1
    }
    else
    {
        *headersize=3; // 0x0 0x0 0x1
    }
    int i;

    if (scanner!=0xFFFFFFFF)
    {
        scanner<<=8;
        scanner|=buffer[start++];
    }

    bool found=false;
    for (i=start; i<inptr; i++)
    {
        if (longstartcode)
        {
            if (scanner==1L)
            {
                if (buffer[i]==0xE0) longstartcode=false;
                found=true;
                break;
            }
            if ((scanner & 0xFFFFFFF0)==0x1E0L)
            {
                found=true;
                break;
            }
        }
        else
        {
            if ((scanner & 0x00FFFFFF)==1L)
            {
                if (pesonly)
                {
                    if (buffer[i]>=0xBC)
                    {
                        found=true;
                        break;
                    }
                }
                else
                {
                    found=true;
                    break;
                }
            }
        }
        scanner<<=8;
        scanner|=buffer[i];
    }
    if (!found)
    {
        if (longstartcode)
        {
            if (scanner==1L)
            {
                if (buffer[i]==0xE0) longstartcode=false;
                found=true;
            }
            if ((scanner & 0xFFFFFFF0)==0x1E0L)
            {
                found=true;
            }
        }
        else
        {
            if (((scanner & 0x00FFFFFF)==1L) && (!pesonly))
            {
                found=true;
            }
        }
    }
    if (i==inptr)
    {
        if (found)
        {
            scanner=0xFFFFFFFF;
        }
        return -1; // we need more bytes!
    }
    if (longstartcode) i--;
    if (buffer[i]>=0xBC) // do we have a PES packet?
    {
#define PESHDRSIZE 6
        if ((i+PESHDRSIZE)>inptr)
        {
            return -1; // we need more data (for streamsize and headersize)
        }

        *streamsize=(buffer[i+1]<<8)+buffer[i+2];
        if (*streamsize) (*streamsize)+=PESHDRSIZE; // 6 Byte PES-Header
        if (longstartcode)
        {
            struct PESHDROPT *peshdropt=(struct PESHDROPT *) &buffer[i+3];
            if (peshdropt->MarkerBits==0x2)
            {
                *headersize=PESHDRSIZE+sizeof(struct PESHDROPT)+
                            peshdropt->Length;
            }
            else
            {
                *headersize=PESHDRSIZE;
            }
        }
    }

    return i-3;
}

int cPaketQueue::findaudioheader(int start, int *framesize, int *headersize, bool ac3)
{
    if ((!framesize) || (!headersize)) return -1;
    if (!start) start=outptr;
    if (start>=inptr) return -1;
    (*framesize)=0;
    if (ac3)
    {
        (*headersize)=2;
    }
    else
    {
        (*headersize)=3;
    }
    int i;

    if (scanner!=0xFFFFFFFF)
    {
        scanner<<=8;
        scanner|=buffer[start++];
    }
    else
    {
        scanner<<=8;
        scanner|=buffer[start++];
        scanner<<=8;
        scanner|=buffer[start++];
    }

    for (i=start; i<inptr; i++)
    {

        if (ac3)
        {
            if ((scanner & 0x0000FFFF)==0xB77L) break;
        }
        else
        {
            if ((scanner & 0x0000FFE0)==0xFFE0L) break;
        }

        scanner<<=8;
        scanner|=buffer[i];
    }
    if (i==inptr) return -1;

    i-=2;

    if (ac3)
    {
        struct AC3HDR *ac3hdr = (struct AC3HDR *) &buffer[i];

        if (ac3hdr->SampleRateIndex==3) return -1; // reserved
        if (ac3hdr->FrameSizeIndex>=38) return -1; // reserved

        if (framesize)
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

            *framesize=2*bitRatesAC3[ac3hdr->SampleRateIndex][ac3hdr->FrameSizeIndex];
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

        if (framesize)
        {
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
                *framesize = 0; // "free" Bitrate -> we don't support this!
            else
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

                int br = 1000 * bitRates[mpegIndex][layerIndex][mp2hdr->BitRateIndex]; // bits/s
                int N = slots_per_frame[mpegIndex][layerIndex] * br / sf; // slots

                *framesize = (N + mp2hdr->Padding) * slotSize; // bytes
            }
        }
        return i;
    }
}

int cPaketQueue::FindTSHeader(int Start)
{
    int start=outptr+Start;
    int i=0;
    for (i=start; i<inptr; i++)
    {
        if (buffer[i]==0x47) break;
    }
    if (i==inptr) return -1;
    return i;
}

int cPaketQueue::FindPESHeader(int Start)
{
    int start=outptr+Start;
    int ssize,hsize;
    int pos=findpktheader(start,&ssize,&hsize,false,true);
    if (pos==-1) return -1;
    pos-=outptr;
    return pos;
}

bool cPaketQueue::Put(uchar *Data, int Size)
{
    if (!buffer) return false;
    if ((inptr) && (inptr==outptr)) inptr=outptr=0;

    if (outptr)
    {
        if (outptr>(inptr-outptr))
        {
            memcpy(buffer,&buffer[outptr],inptr-outptr);
            scannerstart-=outptr;
            inptr-=outptr;
            if (pktinfo.pkthdr>0) pktinfo.pkthdr-=outptr;
            outptr=0;
        }
    }

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
        mpercent=100;
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

uchar *cPaketQueue::Get(int *Size)
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

uchar *cPaketQueue::Peek(int Size)
{
    if (!buffer) return NULL;
    if (!Size) return NULL;
    if (Length()<Size) return NULL;
    uchar *ret=&buffer[outptr];
    return ret;
}

uchar *cPaketQueue::GetPacket(int *Size, int Type)
{
    if (!Size) return NULL;
    *Size=0;
    if (Length()<4) return NULL;

    if (pktinfo.pkthdr==-1)
    {
        scanner=0xFFFFFFFF;
        scannerstart=outptr;
        switch (Type)
        {
        case PACKET_AC3:
            pktinfo.pkthdr=findaudioheader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,true);
            break;
        case PACKET_MP2:
            pktinfo.pkthdr=findaudioheader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,false);
            break;
        case PACKET_H264:
            pktinfo.pkthdr=findpktheader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,true);
            break;
        default:
            pktinfo.pkthdr=findpktheader(0,&pktinfo.streamsize,&pktinfo.pktsyncsize,false);
            break;
        }

        if (pktinfo.pkthdr==-1)
        {
            return NULL;
        }
        else
        {
            if (pktinfo.pkthdr!=outptr)
            {
                skipped+=(pktinfo.pkthdr-outptr);
            }
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
    case PACKET_AC3:
        pkthdr=findaudioheader(scannerstart,&streamsize,&pktsyncsize,true);
        break;
    case PACKET_MP2:
        pkthdr=findaudioheader(scannerstart,&streamsize,&pktsyncsize,false);
        break;
    case PACKET_H264:
        pkthdr=findpktheader(scannerstart,&streamsize,&pktsyncsize,true);
        break;
    default:
        pkthdr=findpktheader(scannerstart,&streamsize,&pktsyncsize,false);
        break;
    }

    if (pkthdr==-1)
    {
        if ((pktinfo.streamsize) && ((inptr-outptr)>pktinfo.streamsize))
        {
            // no startcode right after streamsize?
            // output streamsize packet
            scannerstart=pktinfo.pkthdr+pktinfo.streamsize;
            streamsize=pktsyncsize=0;
            pkthdr=-1;
        }
        else
        {
            scannerstart=inptr;
            return NULL;
        }
    }
    else
    {
        scannerstart=pkthdr+pktsyncsize;
        if (pktsyncsize>4) scanner=0xFFFFFFFF;
    }

    uchar *ptr=&buffer[pktinfo.pkthdr];

    if (pktinfo.streamsize)
    {
        *Size=pktinfo.streamsize;
    }
    else
    {
        *Size=pkthdr-pktinfo.pkthdr;
    }
    if (pkthdr==-1)
    {
        outptr=pktinfo.pkthdr+pktinfo.streamsize;
    }
    else
    {
        outptr=pkthdr;
    }

    pktinfo.pkthdr=pkthdr;
    pktinfo.streamsize=streamsize;
    pktinfo.pktsyncsize=pktsyncsize;

    return ptr;
}

// ----------------------------------------------------------------------------

cTS2Pkt::cTS2Pkt(int Pid, const char *QueueName, int QueueSize, bool H264)
{
    queue=new cPaketQueue(QueueName,QueueSize);
    pid=Pid;
    h264=H264;
    firstsync=false;
    Clear();
}

cTS2Pkt::~cTS2Pkt()
{
    if (queue)
    {
        if (skipped) tsyslog("buffer skipped: %-15s %i bytes",queue->Name(),skipped);
        delete queue;
    }
}

void cTS2Pkt::Clear(AvPacket *Pkt)
{
    if (!Pkt)
    {
        skipped=0;
        noticeFILLER=false;
        lasterror=ERR_INIT;
    }
    else
    {
        Pkt->Length=0;
        Pkt->Type=0;
        Pkt->Stream=0;
    }
    firstsync=sync=false;
    counter=-1;
    if (queue) queue->Clear();
}

bool cTS2Pkt::Process(uchar *TSData, int TSSize, AvPacket *Pkt)
{
    if (!Pkt) return false;
    if (!queue) return false;
    if (TSData)
    {
        if (TSSize!=TS_SIZE) return false;

        struct TSHDR *tshdr = (struct TSHDR *) TSData;

        int ppid=(tshdr->PidH << 8) | tshdr->PidL;
        if (ppid!=pid)
        {
            return false;
        }

        if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
        {
            if (counter==(int) tshdr->Counter)
            {
                if (lasterror!=ERR_DUPLICATE)
                {
                    lasterror=ERR_DUPLICATE;
                    dsyslog("duplicate packet, skipping (0x%04x)",pid);
                }
                Pkt->Length=0;
                Pkt->Type=0;
                Pkt->Stream=0;
                skipped+=TS_SIZE;
                return true; // duplicate packet -> just ignore
            }
            // sequence error
            if (lasterror!=ERR_SEQUENCE)
            {
                lasterror=ERR_SEQUENCE;
                esyslog("sequence error %i->%i (0x%04x)",counter,tshdr->Counter,pid);
            }
            Clear(Pkt);
            skipped+=queue->Skipped();
            if (!tshdr->PayloadStart)
            {
                skipped+=TS_SIZE;
                return true;
            }
        }
        counter=tshdr->Counter;

        if (tshdr->PayloadStart)
        {
            firstsync=sync=true;
        }
        if (!sync)
        {
            Clear(Pkt);
            if (firstsync)
            {
                if (lasterror==ERR_INIT)
                {
                    lasterror=ERR_SYNC;
                    esyslog("out of sync (0x%04x)",pid);
                }
                skipped+=TS_SIZE; // only count skipped bytes after first sync
                skipped+=queue->Skipped();
            }
            return true; // not synced
        }

        // we just ignore the infos in the adaption field (e.g. OPCR/PCR)
        if ((tshdr->AFC!=1) && (tshdr->AFC!=3))
        {
            Pkt->Length=0;
            Pkt->Type=0;
            Pkt->Stream=0;
            return true;
        }

        if (tshdr->TError)
        {
            if (lasterror!=ERR_HDRBIT) {
                lasterror=ERR_HDRBIT;
                esyslog("stream error bit set (0x%04x)",pid);
            }
            Clear(Pkt);
            skipped+=queue->Skipped();
            skipped+=TS_SIZE;
            return true;
        }

        int buflen=TS_SIZE+1;
        uchar *buf=NULL;

        if (tshdr->AFC==1)
        {
            // payload only
            buflen=TS_SIZE-sizeof(struct TSHDR);
            buf=&TSData[sizeof(struct TSHDR)];
        }

        if (tshdr->AFC==3)
        {
            // adaption field + payload
            int alen=TSData[4]+1;
            if (alen>(TS_SIZE-(int) sizeof(struct TSHDR)))
            {
                if (lasterror!=ERR_AFCLEN)
                {
                    lasterror=ERR_AFCLEN;
                    esyslog("afc length error (0x%04x)",pid);
                }
                Clear(Pkt);
                skipped+=queue->Skipped();
                skipped+=TS_SIZE;
                return true;
            }
            buflen=TS_SIZE-(sizeof(struct TSHDR)+alen);
            buf=&TSData[sizeof(struct TSHDR)+alen];
        }

        if (!buflen)
        {
            // no data? -> impossible?
            return false;
        }

        if (tshdr->PayloadStart)
        {
            if ((buf[0]!=0) && (buf[1]!=0))
            {
                if (lasterror!=ERR_PAYLOAD)
                {
                    lasterror=ERR_PAYLOAD;
                    esyslog("payload start error (0x%04x)",pid);
                }
                Clear(Pkt);
                skipped+=queue->Skipped();
                skipped+=TS_SIZE;
                return true;
            }
        }
        queue->Put(buf,buflen);
    }

    Pkt->Data=queue->GetPacket(&Pkt->Length,h264 ? PACKET_H264 : PACKET_H262);
    if (Pkt->Data)
    {
        Pkt->Type=h264 ? PACKET_H264 : PACKET_H262;
        Pkt->Stream=pid;
        if ((h264) && ((Pkt->Data[4] & 0x1F)==0x0C))
        {
            if (!noticeFILLER)
            {
                isyslog("H264 video stream with filler nalu (0x%04x)",pid);
                noticeFILLER=true;
            }
            skipped+=Pkt->Length; // thats not accurate!
            Pkt->Data=NULL;
            Pkt->Length=0;
            Pkt->Type=0;
            Pkt->Stream=0;
        }
    }
    else
    {
        Pkt->Length=0;
        Pkt->Type=0;
        Pkt->Stream=0;
    }
    skipped+=queue->Skipped();
    return true;
}

// ----------------------------------------------------------------------------

cPES2ES::cPES2ES(int PacketType, const char *QueueName, int QueueSize)
{
    queue = new cPaketQueue(QueueName,QueueSize);
    ptype=PacketType;
    Clear();
}

cPES2ES::~cPES2ES()
{
    if (queue)
    {
        if (skipped) tsyslog("buffer skipped: %-15s %i bytes",queue->Name(),skipped);
        delete queue;
    }
}

void cPES2ES::Clear()
{
    stream=0;
    skipped=0;
    lasterror=0;
    if (queue) queue->Clear();
}

bool cPES2ES::Process(uchar *PESData, int PESSize, AvPacket *ESPkt)
{
    if (!ESPkt) return false;
    if (PESData)
    {
        struct PESHDR *peshdr=(struct PESHDR *) PESData;

        // first check some simple things
        if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1)) return false;
        if (peshdr->StreamID<=0xBC) return false;

        int Length=(peshdr->LenH<<8)+peshdr->LenL;
        if (Length)
        {
            Length+=sizeof(PESHDR);
            if (Length!=PESSize)
            {
                if (lasterror!=ERR_LENGTH)
                {
                    esyslog("length mismatch (0x%02X)",peshdr->StreamID);
                    lasterror=ERR_LENGTH;
                }
                skipped+=Length;
                return true;
            }
        }

        if (peshdr->StreamID==0xBE)
        {
            if (lasterror!=ERR_PADDING)
            {
                esyslog("found padding stream (0x%02X)",peshdr->StreamID);
                lasterror=ERR_PADDING;
            }
            queue->Clear();
            skipped+=queue->Skipped();
            return true;
        }

        switch (ptype)
        {
        case PACKET_H262:
            if (peshdr->StreamID!=0xE0) return true; // ignore packets not for us!
            break;
        case PACKET_H264:
            if (peshdr->StreamID!=0xE0) return true; // ignore packets not for us!
            break;
        case PACKET_AC3:
            if (peshdr->StreamID!=0xBD) return true; // ignore packets not for us!
            break;
        case PACKET_MP2:
            if (peshdr->StreamID!=0xC0) return true; // ignore packets not for us!
            break;
        default:
            break;
        }
        stream=peshdr->StreamID;
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
        }
        else
        {
            int bpos=sizeof(struct PESHDR);
            buf=&PESData[bpos];
            buflen=PESSize-bpos;
        }
        if ((ptype==PACKET_AC3) && (buflen>6))
        {
            if ((buf[4]==0x0B) && (buf[5]==0x77))
            {
                buf+=4;
                buflen-=4;
            }
        }
        queue->Put(buf,buflen);
    }
    ESPkt->Data=queue->GetPacket(&ESPkt->Length,ptype);
    if (ESPkt->Data)
    {
        ESPkt->Type=ptype;
        ESPkt->Stream=stream;
    }
    else
    {
        ESPkt->Type=0;
        ESPkt->Length=0;
        ESPkt->Stream=0;
    }
    skipped+=queue->Skipped();
    return true;
}

// ----------------------------------------------------------------------------

cDemux::cDemux(int VPid, int DPid, int APid, bool H264, bool VDRCount, bool RAW)
{
    raw=RAW;
    TS=false;
    if ((VPid>0) || (DPid>0) || (APid>0)) TS=true;

    vpid=VPid;
    dpid=DPid;
    apid=APid;
    if (TS)
    {
        if (!vpid) vpid=-1;
        if (!dpid) dpid=-1;
        if (!apid) apid=-1;
    }

    pes2videoes=NULL;
    pes2audioes_mp2=NULL;
    pes2audioes_ac3=NULL;
    ts2pkt_vpid=NULL;
    ts2pkt_dpid=NULL;
    ts2pkt_apid=NULL;
    h264=H264;
    vdrcount=VDRCount;
    queue = new cPaketQueue("DEMUX",5640);
    skipped=0;
    Clear();
}

cDemux::~cDemux()
{
    if (skipped) tsyslog("buffer skipped: %-15s %i bytes",queue->Name(),skipped);
    if (queue) delete queue;

    if (ts2pkt_vpid) delete ts2pkt_vpid;
    if (pes2videoes) delete pes2videoes;

    if (ts2pkt_dpid) delete ts2pkt_dpid;
    if (pes2audioes_ac3) delete pes2audioes_ac3;

    if (ts2pkt_apid) delete ts2pkt_apid;
    if (pes2audioes_mp2) delete pes2audioes_mp2;

}

int cDemux::Skipped()
{
    int val=skipped;
    if (pes2videoes) val+=pes2videoes->Skipped();
    if (pes2audioes_mp2) val+=pes2audioes_mp2->Skipped();
    if (pes2audioes_ac3) val+=pes2audioes_ac3->Skipped();
    if (ts2pkt_vpid) val+=ts2pkt_vpid->Skipped();
    if (ts2pkt_dpid) val+=ts2pkt_dpid->Skipped();
    if (ts2pkt_apid) val+=ts2pkt_apid->Skipped();
    return val;
}

void cDemux::Clear()
{
    if (pes2videoes) pes2videoes->Clear();
    if (pes2audioes_mp2) pes2audioes_mp2->Clear();
    if (pes2audioes_ac3) pes2audioes_ac3->Clear();
    if (ts2pkt_vpid) ts2pkt_vpid->Clear();
    if (ts2pkt_dpid) ts2pkt_dpid->Clear();
    if (ts2pkt_apid) ts2pkt_apid->Clear();

    if (queue) queue->Clear();
    offset=rawoffset=0;
    vdroffset=0;
    last_bplen=0;
    from_oldfile=0;
    stream_or_pid=0;
    lasterror=ERR_INIT;
}

bool cDemux::isvideopes(uchar *data, int count)
{
    if (!data) return false;
    if (count<6) return false;
    if ((data[0]==0) && (data[1]==0) && (data[2]==1) &&
            ((data[3] & 0xF0)==0xE0) &&
            ((data[4]!=0) || (data[5]!=0))) return true;
    return false;
}

int cDemux::checkts(uchar *data, int count, int &pid)
{
    pid=-1;
    if (count<(int) sizeof(struct TSHDR)) return -1;
    if (data[0]!=0x47) return 1;

    struct TSHDR *tshdr = (struct TSHDR *) data;
    pid = (tshdr->PidH << 8) | tshdr->PidL;
    if ((tshdr->AFC<=0) || (tshdr->AFC>3)) return 1;

    return 0;
}

int cDemux::fillqueue(uchar *data, int count, int &stream_or_pid, int &packetsize, int &readout)
{
#define PEEKBUF 6
    stream_or_pid=packetsize=readout=0;

    uchar *qData=NULL;

    while (!(qData=queue->Peek(PEEKBUF)))
    {
        int len=PEEKBUF-queue->Length();
        int cnt=(count>len) ? len : count;
        if (!queue->Put(data,cnt)) return -1;
        readout+=cnt;
        data+=cnt;
        count-=cnt;
        if (queue->Length()<PEEKBUF) return cnt; // we need more data!
    }

    if (!TS)
    {
        if ((qData[0]==0) && (qData[1]==0) && (qData[2]==1) && (qData[3]>=0xBC))
        {
            stream_or_pid=qData[3];
            packetsize=PEEKBUF+(qData[4]*256+qData[5]);
        }
        else
        {
            if (lasterror!=ERR_JUNK)
            {
                esyslog("unusable data, skipping");
                lasterror=ERR_JUNK;
            }
            skipped++;
            stream_or_pid=0;
            packetsize=1;
            return 0; // no useable data found, try next byte!
        }
    }
    else
    {
        int ret=checkts(qData,PEEKBUF,stream_or_pid);
        if (ret==-1) return -1;
        if (ret)
        {
            if (lasterror!=ERR_JUNK)
            {
                esyslog("unusable data, skipping");
                lasterror=ERR_JUNK;
            }
            skipped++;
            stream_or_pid=0;
            packetsize=1;
            return 0; // no useable data found, try next byte!
        }
        packetsize=TS_SIZE;
    }

    int needed=packetsize+PEEKBUF;
    while (!(qData=queue->Peek(needed)))
    {
        int len=needed-queue->Length();
        int cnt=(count>len) ? len : count;
        if (!queue->Put(data,cnt)) return -1;
        readout+=cnt;
        data+=cnt;
        count-=cnt;
        if (queue->Length()<needed) return cnt; // we need more data!
    }
    if (!TS)
    {
        // check length of PES-packet
        qData=queue->Peek(packetsize+PEEKBUF);
        if (qData)
        {
            int start=packetsize;
            if ((qData[start]!=0) || (qData[start+1]!=0) || (qData[start+2]!=1) || (qData[start+3]<0xBC))
            {
                int start=queue->FindPESHeader(1);
                if (start>0)
                {
                    // broken PES in queue, skip it
                    if (lasterror!=ERR_BROKEN)
                    {
                        esyslog("broken PES in queue, skipping");
                        lasterror=ERR_BROKEN;
                    }
                    packetsize=start;
                    skipped+=start;
                    stream_or_pid=0;
                    return 0;
                }
                else
                {
                    // try to use the first packet
                    return 0;
                }
            }
        }
    }
    else
    {
        qData=queue->Peek(packetsize+PEEKBUF);
        if (qData)
        {
            int start=packetsize;
            int pid;
            int ret=checkts(&qData[start],PEEKBUF,pid);
            if (ret==-1)
            {
                return -1;
            }
            if (ret)
            {
                if (pid!=-1) return 0; // next packet is broken!
                int start=queue->FindTSHeader(1);
                if (start>0)
                {
                    // broken TS in queue, skip it
                    if (lasterror!=ERR_BROKEN)
                    {
                        esyslog("broken TS in queue, skipping");
                        lasterror=ERR_BROKEN;
                    }
                    packetsize=start;
                    skipped+=start;
                    stream_or_pid=0;
                    return 0;
                }
                else
                {
                    // try to use the first packet
                    return 0;
                }
            }
        }
    }
    return 0;
}

bool cDemux::needmoredata()
{
    if (!stream_or_pid) return true;

    if (!TS)
    {
        switch (stream_or_pid)
        {
        case 0xE0:
            if (pes2videoes) return pes2videoes->NeedMoreData();
            break;
        case 0xC0:
            if ((pes2audioes_mp2) && (apid)) return pes2audioes_mp2->NeedMoreData();
            break;
        case 0xBD:
            if ((pes2audioes_ac3) && (dpid)) return pes2audioes_ac3->NeedMoreData();
            break;
        }
    }

    if (TS)
    {
        if ((stream_or_pid==vpid) && (ts2pkt_vpid))
        {
            if (pes2videoes) return pes2videoes->NeedMoreData();
            return ts2pkt_vpid->NeedMoreData();
        }
        if ((stream_or_pid==dpid) && (ts2pkt_dpid))
        {
            if (pes2audioes_ac3) return pes2audioes_ac3->NeedMoreData();
            return ts2pkt_dpid->NeedMoreData();
        }
        if ((stream_or_pid==apid) && (ts2pkt_apid))
        {
            if (pes2audioes_mp2) return pes2audioes_mp2->NeedMoreData();
            return ts2pkt_apid->NeedMoreData();
        }
    }
    return false;
}

bool cDemux::vdraddpatpmt(uchar *data, int count)
{
    // TS-VDR adds pat/pmt to the output, e.g. if
    // a picture starts @376, vdr outputs 0 (!)
    int pid;
    if (checkts(data,count,pid)!=0) return false;
    if ((!pid) || (pid==132)) // 0=PAT 132=PMT
    {
        last_bplen=0;
        vdroffset+=count;
    }
    else
    {
        last_bplen=vdroffset+count;
        vdroffset=0;
    }
    return true;
}

void cDemux::addoffset()
{
    offset+=last_bplen;
    if (from_oldfile)
    {
        from_oldfile-=last_bplen;
        if (!from_oldfile) offset=0;
    }
    last_bplen=0;
}

void cDemux::NewFile()
{
    from_oldfile=queue->Length();
}

void cDemux::DisableDPid()
{
    if (pes2audioes_ac3) delete pes2audioes_ac3;
    if (ts2pkt_dpid) delete ts2pkt_dpid;
    pes2audioes_ac3=NULL;
    ts2pkt_dpid=NULL;
    if (TS)
    {
        dpid=-1;
    }
    else
    {
        dpid=0;
    }
    stream_or_pid=0;
}

int cDemux::Process(uchar *Data, int Count, AvPacket *pkt)
{
    if (!pkt) return -1;
    pkt->Data=NULL;
    pkt->Length=0;

    bool add=needmoredata();
    if ((raw) && (!Data) && (!Count))
    {
        uchar Dummy[6];
        if (!TS)
        {
            Dummy[0]=0;
            Dummy[1]=0;
            Dummy[2]=1;
            Dummy[3]=0xbd;
            Dummy[4]=0;
            Dummy[5]=0;
        }
        Data=Dummy;
        Count=6;
        add=true; // last packet!
    }
    int bplen=0,readout=0;
    uchar *bpkt=NULL;
    if (add)
    {
        addoffset();
        int advance=fillqueue(Data,Count,stream_or_pid,bplen,readout);
        if (advance<0) return -1;
        if (advance) return advance;
        bpkt=queue->Get(&bplen);
        if (!bpkt) return -1;
        last_bplen=bplen;
        rawoffset+=bplen;
        if ((vdrcount) && (TS)) vdraddpatpmt(bpkt,bplen);
    }

    if (raw)
    {
        if (bpkt)
        {
            pkt->Data=bpkt;
            pkt->Length=bplen;
            pkt->Stream=stream_or_pid;
            if (TS)
            {
                pkt->Type=PACKET_TS;
            }
            else
            {
                pkt->Type=PACKET_PES;
            }
        }
        stream_or_pid=0;
        return add ? readout : 0;
    }

    if (!TS)
    {
        switch (stream_or_pid)
        {
        case 0xE0:
            if (!pes2videoes)
            {
                if (h264)
                {
                    pes2videoes=new cPES2ES(PACKET_H264,"PES2H264ES",524288);
                }
                else
                {
                    pes2videoes=new cPES2ES(PACKET_H262,"PES2H262ES",65536);
                }
            }
            if (!pes2videoes->Process(bpkt,bplen,pkt)) return -1;
            break;
        case 0xC0:
            if (apid)
            {
                if (!pes2audioes_mp2) pes2audioes_mp2=new cPES2ES(PACKET_MP2,"PES2MP2",16384);
                if (!pes2audioes_mp2->Process(bpkt,bplen,pkt)) return -1;
            }
            else
            {
                stream_or_pid=0;
            }
            break;
        case 0xBD:
            if (dpid)
            {
                if (!pes2audioes_ac3) pes2audioes_ac3=new cPES2ES(PACKET_AC3,"PES2AC3");
                if (!pes2audioes_ac3->Process(bpkt,bplen,pkt)) return -1;
            }
            else
            {
                stream_or_pid=0;
            }
            break;
        default:
            stream_or_pid=0;
            break;
        }
    }
    else
    {
        if (stream_or_pid==vpid)
        {
            if (!ts2pkt_vpid)
            {
                if (h264)
                {
                    ts2pkt_vpid=new cTS2Pkt(vpid,"TS2H264",819200,true);
                }
                else
                {
                    ts2pkt_vpid=new cTS2Pkt(vpid,"TS2H262",65536);
                }
            }
            if (!ts2pkt_vpid->Process(bpkt,bplen,pkt)) return -1;
            if (isvideopes(pkt->Data,pkt->Length) || pes2videoes)
            {
                AvPacket tpkt;
                memcpy(&tpkt,pkt,sizeof(AvPacket));
                memset(pkt,0,sizeof(AvPacket));
                if (!pes2videoes)
                {
                    ts2pkt_vpid->Resize(3*tpkt.Length,"TS2PES");
                    if (h264)
                    {
                        pes2videoes=new cPES2ES(PACKET_H264,"PES2H264ES",589824);
                    }
                    else
                    {
                        pes2videoes=new cPES2ES(PACKET_H262,"PES2H262ES",65536);
                    }
                }
                if (!pes2videoes->Process(tpkt.Data,tpkt.Length,pkt)) return -1;
            }
        }
        else if (stream_or_pid==dpid)
        {
            AvPacket tpkt={NULL,0,0,0};
            if (!ts2pkt_dpid) ts2pkt_dpid=new cTS2Pkt(dpid,"TS2PES AC3");
            if (!ts2pkt_dpid->Process(bpkt,bplen,&tpkt)) return -1;
            if (!pes2audioes_ac3) pes2audioes_ac3=new cPES2ES(PACKET_AC3,"PES2AC3");
            if (!pes2audioes_ac3->Process(tpkt.Data,tpkt.Length,pkt)) return -1;
        }
        else if (stream_or_pid==apid)
        {
            AvPacket tpkt={NULL,0,0,0};
            if (!ts2pkt_apid) ts2pkt_apid=new cTS2Pkt(apid,"TS2PES MP2",16384);
            if (!ts2pkt_apid->Process(bpkt,bplen,&tpkt)) return -1;
            if (!pes2audioes_mp2) pes2audioes_mp2=new cPES2ES(PACKET_MP2,"PES2MP2",16384);
            if (!pes2audioes_mp2->Process(tpkt.Data,tpkt.Length,pkt)) return -1;
        }
        else stream_or_pid=0;
    }
    return add ? readout : 0;
}
