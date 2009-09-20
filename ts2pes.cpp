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
    streamsize=0;
    counter=-1;
    sync=false;
}

int cMarkAdTS2PES::FindPESHeader(uchar *TSData, int TSSize, int *StreamSize)
{
    unsigned long scanner=0xFFFFFFFF;
    int i;
    for (i=0; i<TSSize; i++)
    {
        scanner<<=8;
        if (scanner==0x00000100)
        {
            break;
        }
        scanner|=TSData[i];
    }
    if (i!=TSSize)
    {
        if (StreamSize)
        {
            if (TSData[i]>=0xBC)
            {
                *StreamSize=(TSData[i+1]<<8)+TSData[i+2];
                if (*StreamSize) (*StreamSize)+=6; // 6 Byte PES-Header
            }
        }
        return i-3;
    }
    return -1;
}

int cMarkAdTS2PES::Process(int Pid, uchar *TSData, int TSSize, uchar **PESData, int *PESSize)
{
    if ((!PESData) || (!PESSize)) return -1;
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
        if (Pid!=pid)
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

    int peshdr=FindPESHeader(pesdata, pessize, &streamsize);
    if (peshdr==0)
    {
        if (!streamsize)
        {
            peshdr=FindPESHeader(pesdata+3,pessize-3,NULL);
            if (peshdr>0) peshdr+=3;
        }
        else
        {
            if (pessize>streamsize)
            {
                int size=pessize-streamsize;

                *PESData=pesdata;
                *PESSize=streamsize;
                if (pesdatalast) free(pesdatalast);
                pesdatalast=pesdata;
                pesdata=NULL;
                pessize=0;

                void *ptr=malloc(size);
                if (!ptr) return -1;
                memcpy(ptr,(*PESData)+streamsize,size);
                bytes_processed-=size;
                pessize=size;
                pesdata=(uchar *) ptr;
                data_left=true;
                streamsize=0;
            }
        }
    }
    if (peshdr>0)
    {
        // start of next PES paket found
        if (pesdata)
        {
            // return old data
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
                memcpy(ptr,(*PESData)+peshdr,size);
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
    return bytes_processed;
}
