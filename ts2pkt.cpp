/*
 * ts2pkt.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
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
    switch (ErrIndex)
    {
    case MA_ERR_TOSMALL:
        dsyslog("markad [%i]: input to small",recvnumber);
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
        dsyslog("markad [%i]: buflen > 188",recvnumber);
        break;
    case MA_ERR_NEG:
        dsyslog("markad [%i]: buflen < 0",recvnumber);
        break;
    }
    counter=-1;
    if (queue) queue->Clear();
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
            Reset(MA_ERR_TOSMALL);
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

        if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
        {
            if (counter==tshdr->Counter)
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
    *PktData=queue->GetPacket(PktSize,MA_PACKET_PKT);
    return;
}
