/*
 * ts2pkt.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "ts2pkt.h"

cMarkAdTS2Pkt::cMarkAdTS2Pkt(int RecvNumber)
{
    recvnumber=RecvNumber;
    pktdata=NULL;
    pktdatalast=NULL;
    Reset();
}

cMarkAdTS2Pkt::~cMarkAdTS2Pkt()
{
    if (pktdata) free(pktdata);
    if (pktdatalast) free(pktdatalast);
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
    case MA_ERR_MEM:
        dsyslog("markad [%i]: out of memory",recvnumber);
        break;
    }

    if (pktdata) free(pktdata);
    pktdata=NULL;
    pktsize=0;
    dataleft=false;
    counter=-1;
    pktinfo.pkthdr=-1;
    isPES=false;
}

bool cMarkAdTS2Pkt::CheckStreamID(MarkAdPid Pid, uchar *Data, int Size)
{
    if ((!Data) || (!Size)) return false;
    if (Size<3) return false;
    int streamid=Data[3];

    switch (Pid.Type)
    {
    case MARKAD_PIDTYPE_AUDIO_AC3:
        if (streamid!=0xBD) return false;
        return true;
        break;
    case MARKAD_PIDTYPE_AUDIO_MP2:
        if ((streamid & 0xF0)!=0xC0) return false;
        return true;
        break;
    case MARKAD_PIDTYPE_VIDEO_H262:
    case MARKAD_PIDTYPE_VIDEO_H264:
        if ((streamid>=0xBC) && (streamid<0xE0)) return false;
        if (streamid>0xEF) return false;
        return true;
        break;
    default:
        return false;
    }
    return false;
}

int cMarkAdTS2Pkt::FindPktHeader(uchar *Data, int Size, int *StreamSize,
                                 int *HeaderSize)
{
#define PESHDRSIZE 6
    if ((!Data) || (!Size)) return -1;
    if (StreamSize) (*StreamSize)=0;
    if (HeaderSize) (*HeaderSize)=3;

    unsigned long scanner=0xFFFFFFFF;
    int i;

    for (i=0; i<Size; i++)
    {
        scanner<<=8;
        if (scanner==0x00000100L)
        {
            break;
        }
        scanner|=Data[i];
    }
    if (i!=Size)
    {
        if ((StreamSize) && ((i+2)<Size))
        {
            if (Data[i]>=0xBC) // do we have a PES packet?
            {
                (*StreamSize)=(Data[i+1]<<8)+Data[i+2];
                if (*StreamSize) (*StreamSize)+=PESHDRSIZE; // 6 Byte PES-Header

                if ((HeaderSize) && ((i+PESHDRSIZE)<Size))
                {
                    struct PESHDROPT *peshdropt=(struct PESHDROPT *) &Data[i+3];
                    if (peshdropt->MarkerBits==0x2)
                    {
                        (*HeaderSize)=PESHDRSIZE+sizeof(struct PESHDROPT)+
                                      peshdropt->Length;
                    }
                    else
                    {
                        (*HeaderSize)=PESHDRSIZE;
                    }
                }
            }
        }
        return i-3;
    }
    return -1;
}

int cMarkAdTS2Pkt::Process(MarkAdPid Pid, uchar *TSData, int TSSize, uchar **PktData, int *PktSize)
{
    if ((!PktData) || (!PktSize) || (!TSData) || (!TSSize)) return -1;
    *PktData=NULL;
    *PktSize=0;

    int buflen=TS_SIZE+1;
    uchar *buf=NULL;

    int bytes_processed; // pointer in TSData

    if (!dataleft)
    {
        if (TSSize<TS_SIZE)
        {
            Reset(MA_ERR_TOSMALL);
            return TSSize; // we need a full packet
        }

        if ((TSData[0]==0) && (TSData[1]==0) && (TSData[2]==1))
        {
            isPES=true;
        }

        if (isPES)
        {
            buf=TSData;
            buflen=TSSize;
        }
        else
        {
            // search for TS packet sync
            int i;
            for (i=0; i<TSSize; i++)
            {
                if (TSData[i]==0x47) break;
            }
            if (i==TSSize)
            {
                Reset(MA_ERR_NOSYNC);
                return TSSize;
            }
            TSData+=i;
            TSSize-=i;

            struct TSHDR *tshdr = (struct TSHDR *) TSData;

            int pid = (tshdr->PidH << 8) | tshdr->PidL;
            if (Pid.Num!=pid)
            {
                return TS_SIZE; // not for us
            }

            if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
            {
                if (counter==tshdr->Counter)
                {
                    // duplicate paket -> just ignore
                    return TS_SIZE;
                }
                // sequence error
                Reset(MA_ERR_SEQ);
                return TS_SIZE;
            }
            counter=tshdr->Counter;

            if ((tshdr->AFC<=0) || (tshdr->AFC>3))
            {
                Reset(MA_ERR_AFC);
                return TS_SIZE;
            }

            // we just ignore the infos in the adaption field (e.g. OPCR/PCR)
            if ((tshdr->AFC!=1) && (tshdr->AFC!=3))
            {
                return TS_SIZE;
            }

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
                return TS_SIZE;
            }
            if (buflen<=0)
            {
                // error in size
                Reset(MA_ERR_NEG);
                return TS_SIZE;
            }

        }
        pktdata=(uchar *) realloc(pktdata,pktsize+buflen);
        if (!pktdata)
        {
            Reset(MA_ERR_MEM);
            return -1;
        }
        memcpy(pktdata+pktsize,buf,buflen);
        pktsize+=buflen;
        if (isPES)
        {
            bytes_processed=buflen;
        }
        else
        {
            bytes_processed=TS_SIZE;
        }
    }
    else
    {
        bytes_processed=pktsize;
        dataleft=false;
    }

    if (pktinfo.pkthdr==-1)
    {
        pktinfo.pkthdr=FindPktHeader(pktdata, pktsize, &pktinfo.streamsize, &pktinfo.pkthdrsize);
        if (pktinfo.pkthdr==-1)
        {
            return bytes_processed; // not found any header -> next paket
        }
    }

    int streamsize=0;
    int pkthdrsize;
    int pkthdr=FindPktHeader(pktdata+pktinfo.pkthdr+pktinfo.pkthdrsize,
                             pktsize-(pktinfo.pkthdr+pktinfo.pkthdrsize),&streamsize,&pkthdrsize);

    if (pkthdr==-1)
    {
        return bytes_processed; // no next header -> next packet
    }

    pkthdr+=(pktinfo.pkthdrsize+pktinfo.pkthdr);

    // found paket between pktinfo.pkthdr and pkthdr
    int size = pkthdr-pktinfo.pkthdr;
    if (size<pktinfo.streamsize) size=pktinfo.streamsize;

    if (pktsize<size) return bytes_processed; // we need more data!

    (*PktData)=pktdata+pktinfo.pkthdr;
    (*PktSize)=size;
    if (isPES)
    {
        if (!CheckStreamID(Pid,*PktData,*PktSize))
        {
            (*PktData)=NULL;
            (*PktSize)=0;
        }
    }
    if (pktdatalast) free(pktdatalast);
    pktdatalast=pktdata;

    int bufleftsize=pktsize-(pktinfo.pkthdr+size);
    if (bufleftsize<=0)
    {
        Reset(MA_ERR_NEG);
        return bytes_processed;
    }
    uchar *bufleft=(uchar *) malloc(bufleftsize);
    if (!bufleft)
    {
        Reset(MA_ERR_MEM);
        return bytes_processed;
    }
    memcpy(bufleft,pktdata+pktinfo.pkthdr+size,bufleftsize);
    pktinfo.pkthdr=-1;
    pktdata=bufleft;
    pktsize=bufleftsize;

    bytes_processed-=bufleftsize;
    if (bytes_processed<0) bytes_processed=0; // just to be safe

    dataleft=true;

    return bytes_processed;
}
