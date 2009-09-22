/*
 * ts2pes.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "ts2pes.h"

#include <stdio.h>

cMarkAdTS2PES::cMarkAdTS2PES()
{
    pesdata=NULL;
    pesdatalast=NULL;
    Reset();
}

cMarkAdTS2PES::~cMarkAdTS2PES()
{
    if (pesdata) free(pesdata);
    if (pesdatalast) free(pesdatalast);
}

void cMarkAdTS2PES::Reset()
{
    if (pesdata) free(pesdata);
    pesdata=NULL;
    pessize=0;
    data_left=false;
    counter=-1;
    sync=false;
}

int cMarkAdTS2PES::FindPESHeader(uchar *TSData, int TSSize, int *StreamSize,
                                 int *HeaderSize)
{
    if ((!TSData) || (!TSSize)) return -1;
#define PESHDRSIZE 6
    if (StreamSize) (*StreamSize)=0;
    if (HeaderSize) (*HeaderSize)=3;
    //unsigned long scanner=0xFFFFFFFF;
    int i=0;
long scanner=-1;

    for (i=0; i<TSSize; i++)
    {
        scanner<<=8;
        if (scanner==(long) 0x00000100L)
        {
            break;
        }
        scanner|=TSData[i];
    }
    if (i!=TSSize)
    {
        if ((StreamSize) && ((i+2)<TSSize))
        {
            if (TSData[i]>=0xBC)
            {
                (*StreamSize)=(TSData[i+1]<<8)+TSData[i+2];
                if (*StreamSize) (*StreamSize)+=6; // 6 Byte PES-Header
            }
        }
        if ((HeaderSize) && ((i+6)<TSSize))
        {
            struct PESHDROPT *peshdropt=(struct PESHDROPT *) &TSData[i+3];
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
        return i-3;
    }
    return -1;
}

int cMarkAdTS2PES::Process(MarkAdPid Pid, uchar *TSData, int TSSize, uchar **PESData, int *PESSize)
{
    if ((!PESData) || (!PESSize) || (!TSData) || (!TSSize)) return -1;
    *PESData=NULL;
    *PESSize=0;

    int buflen=TS_SIZE+1;
    uchar *buf=NULL;

    int bytes_processed;

    if (!data_left)
    {
        // search for TS packet sync
        int i;
        for (i=0; i<TSSize; i++)
        {
            if (TSData[i]==0x47) break;
        }
        if (i==TSSize)
        {
            Reset();
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

        if ((tshdr->PayloadStart==0) && (!sync))
        {
            return TS_SIZE;
        }
        else
        {
            sync=true;
        }

        if ((counter!=-1) && (((counter+1) & 0xF)!=tshdr->Counter))
        {
            // sequence error
            Reset();
            return TS_SIZE;
        }
        counter=tshdr->Counter;

        if ((tshdr->AFC<=0) || (tshdr->AFC>3))
        {
            Reset();
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
            Reset();
            return TS_SIZE;
        }
        if (buflen<0)
        {
            // error in size
            Reset();
            return TS_SIZE;
        }

        bytes_processed=TS_SIZE-buflen;

        pesdata=(uchar *) realloc(pesdata,pessize+buflen);
        if (!pesdata)
        {
            pessize=0;
            return -1;
        }
        memcpy(pesdata+pessize,buf,buflen);
        pessize+=buflen;
        bytes_processed+=buflen;
    }
    else
    {
        bytes_processed=pessize;
        data_left=false;
    }

    int streamsize=0;
    int peshdrsize=3; // size of sync (just as start)
    int peshdr=FindPESHeader(pesdata, pessize, &streamsize, &peshdrsize);

    if (peshdr==0)
    {
        if (!streamsize)
        {
            peshdr=FindPESHeader(pesdata+peshdrsize,pessize-peshdrsize,NULL,NULL);
            if (peshdr>=0)
            {
                peshdr+=peshdrsize;
            }
        }
        else
        {
            if (pessize>streamsize)
            {
                int size=pessize-streamsize;
                uchar *pesptr=pesdata;
                *PESData=pesdata;
                *PESSize=streamsize;
                if (pesdatalast) free(pesdatalast);
                pesdatalast=pesdata;
                pesdata=NULL;
                pessize=0;

                void *ptr=malloc(size);
                if (!ptr) return -1;
                memcpy(ptr,pesptr+streamsize,size);
                bytes_processed-=size;
                pessize=size;
                pesdata=(uchar *) ptr;
                data_left=true;
            }
        }
    }

    if (peshdr>0)
    {
        // start of next PES paket found
        if (pesdata)
        {
            if ((pesdata[0]==0) && (pesdata[1]==0) && (pesdata[2]==1))
            {
                // return old data
                uchar *pesptr=pesdata;
                *PESData=pesdata;
                *PESSize=peshdr;
                if (pesdatalast) free(pesdatalast);
                pesdatalast=pesdata;
                int size=pessize-peshdr;
                pesdata=NULL;
                pessize=0;

                if (size>0)
                {
                    void *ptr=malloc(size);
                    if (!ptr) return -1;
                    memcpy(ptr,pesptr+peshdr,size);
                    bytes_processed-=size;
                    pessize=size;
                    pesdata=(uchar *) ptr;
                    data_left=true;
                }
                else
                {
                    // TODO: not sure if this is ok
                    bytes_processed-=size;
                }
            }
        }
    }
    return bytes_processed;
}
