/*
 * ts2pkt.cpp: A program for the Video Disk Recorder
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

#include "ts2pkt.h"

cMarkAdTS2Pkt::cMarkAdTS2Pkt(const char *QueueName, int QueueSize)
{
    queue=new cMarkAdPaketQueue(QueueName,QueueSize);
    Reset();
}

cMarkAdTS2Pkt::~cMarkAdTS2Pkt()
{
    if (queue) delete queue;
}

void cMarkAdTS2Pkt::Clear()
{
    Reset();
}

bool cMarkAdTS2Pkt::Reset(int ErrIndex)
{
    sync=false;
    switch (ErrIndex)
    {
    case MA_ERR_TSSIZE:
        dsyslog("inbuf not 188 bytes");
        break;
    case MA_ERR_NOSYNC:
        dsyslog("found no sync");
        break;
    case MA_ERR_SEQ:
        dsyslog("sequence error");
        break;
    case MA_ERR_AFC:
        dsyslog("wrong AFC value");
        break;
    case MA_ERR_TOBIG:
        dsyslog("buflen > 188 bytes");
        break;
    case MA_ERR_NEG:
        dsyslog("buflen negative");
        break;
    }
    counter=-1;
    if (queue) queue->Clear();
    return false;
}

bool cMarkAdTS2Pkt::InjectVideoPES(uchar *PESData, int PESSize)
{
    if ((!PESData) || (!PESSize)) return false;

    struct PESHDR *peshdr=(struct PESHDR *) PESData;

    // first check some simple things
    if ((peshdr->Sync1!=0) && (peshdr->Sync2!=0) && (peshdr->Sync3!=1)) return false;
    if ((peshdr->StreamID & 0xF0)!=0xE0) return false;

    int Length=(peshdr->LenH<<8)+peshdr->LenL;
    if (Length) Length+=sizeof(PESHDR);
    if (Length!=PESSize) return false;

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
    queue->Inject(buf,buflen);
    return true;
}

bool cMarkAdTS2Pkt::Process(MarkAdPid Pid, uchar *TSData, int TSSize, MarkAdPacket *Pkt)
{
    if ((!Pkt) || (!queue)) return false;

    bool ret=true;

    if (TSData)
    {
        if (TSSize!=TS_SIZE)
        {
            return Reset(MA_ERR_TSSIZE);  // we need a full packet
        }

        // check TS packet sync
        if (TSData[0]!=0x47)
        {
            return Reset(MA_ERR_NOSYNC); // no sync
        }

        struct TSHDR *tshdr = (struct TSHDR *) TSData;

        int pid = (tshdr->PidH << 8) | tshdr->PidL;
        if (Pid.Num!=pid)
        {
            return true; // not for us, but this is ok
        }

        if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
        {
            if (counter==(int) tshdr->Counter)
            {
                return true; // duplicate paket -> just ignore
            }
            // sequence error
            ret=Reset(MA_ERR_SEQ);
            if (!tshdr->PayloadStart) return ret;
        }
        counter=tshdr->Counter;

        if (tshdr->PayloadStart) sync=true;
        if (!sync)
        {
            return false; // not synced
        }

        if ((tshdr->AFC<=0) || (tshdr->AFC>3))
        {
            return Reset(MA_ERR_AFC);
        }

        // we just ignore the infos in the adaption field (e.g. OPCR/PCR)
        if ((tshdr->AFC!=1) && (tshdr->AFC!=3))
        {
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
            struct TSADAPT *tsadapt = (struct TSADAPT *) &TSData[4];
            int alen=tsadapt->Len+1;
            buflen=TS_SIZE-(sizeof(struct TSHDR)+alen);
            buf=&TSData[sizeof(struct TSHDR)+alen];
        }

        if (buflen>TS_SIZE)
        {
            // size to large
            return Reset(MA_ERR_TOBIG);
        }
        if (buflen<0)
        {
            // error in size
            return Reset(MA_ERR_NEG);
        }
        if (buflen==0)
        {
            // no data?
            return false;
        }

        queue->Put(buf,buflen);
    }
    if (!ret) return ret;
    if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
    {
        Pkt->Data=queue->GetPacket(&Pkt->Length,MA_PACKET_H264);
    }
    else
    {
        Pkt->Data=queue->GetPacket(&Pkt->Length,MA_PACKET_PKT);
    }
    return ret;
}
