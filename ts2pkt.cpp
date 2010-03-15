/*
 * ts2pkt.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "ts2pkt.h"

cMarkAdTS2Pkt::cMarkAdTS2Pkt(int RecvNumber, const char *QueueName, int QueueSize)
{
    recvnumber=RecvNumber;
    queue=new cMarkAdPaketQueue(RecvNumber,QueueName,QueueSize);
    Reset();
}

cMarkAdTS2Pkt::~cMarkAdTS2Pkt()
{
    if (queue) delete queue;
}

void cMarkAdTS2Pkt::Reset(int ErrIndex)
{
    sync=false;
    switch (ErrIndex)
    {
    case MA_ERR_TSSIZE:
        dsyslog("markad [%i]: inbuf not 188 bytes",recvnumber);
        break;
    case MA_ERR_NOSYNC:
        dsyslog("markad [%i]: found no sync",recvnumber);
        break;
    case MA_ERR_SEQ:
        dsyslog("markad [%i]: sequence error",recvnumber);
        break;
    case MA_ERR_AFC:
        dsyslog("markad [%i]: wrong AFC value",recvnumber);
        break;
    case MA_ERR_TOBIG:
        dsyslog("markad [%i]: buflen > 188 bytes",recvnumber);
        break;
    case MA_ERR_NEG:
        dsyslog("markad [%i]: buflen negative",recvnumber);
        break;
    }
    counter=-1;
    if (queue) queue->Clear();
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

void cMarkAdTS2Pkt::Process(MarkAdPid Pid, uchar *TSData, int TSSize, uchar **PktData, int *PktSize)
{
    if ((!PktData) || (!PktSize) || (!queue)) return;
    *PktData=NULL;
    *PktSize=0;

    if (TSData)
    {
        if (TSSize!=TS_SIZE)
        {
            Reset(MA_ERR_TSSIZE);
            return; // we need a full packet
        }

        // check TS packet sync
        if (TSData[0]!=0x47)
        {
            Reset(MA_ERR_NOSYNC);
            return;
        }

        struct TSHDR *tshdr = (struct TSHDR *) TSData;

        int pid = (tshdr->PidH << 8) | tshdr->PidL;
        if (Pid.Num!=pid)
        {
            return; // not for us
        }

        if (tshdr->PayloadStart) sync=true;
        if (!sync)
        {
            return; // not synced
        }

        if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
        {
            if (counter==(int) tshdr->Counter)
            {
                // duplicate paket -> just ignore
                return;
            }
            // sequence error
            Reset(MA_ERR_SEQ);
            return;
        }
        counter=tshdr->Counter;

        if ((tshdr->AFC<=0) || (tshdr->AFC>3))
        {
            Reset(MA_ERR_AFC);
            return;
        }

        // we just ignore the infos in the adaption field (e.g. OPCR/PCR)
        if ((tshdr->AFC!=1) && (tshdr->AFC!=3))
        {
            return;
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
            Reset(MA_ERR_TOBIG);
            return;
        }
        if (buflen<0)
        {
            // error in size
            Reset(MA_ERR_NEG);
            return;
        }
        if (buflen==0)
        {
            // no data?
            return;
        }

        queue->Put(buf,buflen);
    }
    if (Pid.Type==MARKAD_PIDTYPE_VIDEO_H264)
    {
        *PktData=queue->GetPacket(PktSize,MA_PACKET_H264);
    }
    else
    {
        *PktData=queue->GetPacket(PktSize,MA_PACKET_PKT);
    }
    return;
}
