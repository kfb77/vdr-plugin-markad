/*
 * demux.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "demux.h"

cMarkAdDemux::cMarkAdDemux(int RecvNumber)
{
    ts2pkt=new cMarkAdTS2Pkt(RecvNumber);
    pes2audioes=NULL;
    pkt=NULL;
    pesptr=NULL;
    pktlen=0;
    tsdata=tsptr=NULL;
    tssize=0;
}

cMarkAdDemux::~cMarkAdDemux()
{
    if (ts2pkt) delete ts2pkt;
    if (pes2audioes) delete pes2audioes;
    if (tsdata) free(tsdata);
}

int cMarkAdDemux::Process(MarkAdPid Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Data) && (!Count) && (!ts2pkt) && (!pes2audioes) ||
            (!Pkt) || (!PktLen) || (!Pid.Num)) return -1;
    *Pkt=NULL;
    *PktLen=0;

    int len=-1; // we don't want loops

    if (!pktlen)
    {
        if (tssize<TS_SIZE)
        {
            tsdata=(uchar *) realloc(tsdata,tssize+Count);
            if (!tsdata) return -1;
            memcpy(tsdata+tssize,Data,Count);
            tssize+=Count;
            tsptr=tsdata;
            if (tssize<TS_SIZE) return Count;
        }
        len=ts2pkt->Process(Pid,tsdata,tssize,&pkt,&pktlen);

        int bufleftsize=tssize-len;
        uchar *ptr=(uchar *) malloc(bufleftsize);
        if (!ptr) return -1;
        memcpy(ptr,tsdata+len,bufleftsize);

        free(tsdata);
        tsdata=ptr;
        tssize=bufleftsize;
        if (tssize<TS_SIZE)
        {
            len=Count;
        }
        else
        {
            len=0;
        }
    }
    if (pkt)
    {

        if ((((pkt[3]>=0xc0) && (pkt[3]<=0xDF)) || (pkt[3]==0xBD))
                && (!pesptr))
        {
            if (!pes2audioes)
            {
                pes2audioes=new cMarkAdPES2AudioES();
            }
            if (pes2audioes)
            {
                pesptr=pkt;
            }
            else
            {
                pesptr=NULL;
            }
        }
        if (pesptr)
        {

            if (len==-1) len=0;
            uchar *esdata;
            int essize;
            while (pktlen>0)
            {
                int len2=pes2audioes->Process(pesptr,pktlen,&esdata,&essize);
                if (len2<0)
                {
                    break;
                }
                else
                {
                    if (esdata)
                    {
                        *Pkt=esdata;
                        *PktLen=essize;
                    }
                    pesptr+=len2;
                    pktlen-=len2;
                    if (!pktlen) pesptr=NULL;
                    break;
                }
            }

        }
        else
        {
            *Pkt=pkt;
            *PktLen=pktlen;
            pkt=pesptr=NULL;
            pktlen=0;
        }
    }
    return len;
}

