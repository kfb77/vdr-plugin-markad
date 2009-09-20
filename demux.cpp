/*
 * demux.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "demux.h"

cMarkAdDemux::cMarkAdDemux()
{
    ts2pes=new cMarkAdTS2PES();
    pes2audioes=NULL;
    pespkt=NULL;
    pesptr=NULL;
    peslen=0;
}

cMarkAdDemux::~cMarkAdDemux()
{
    if (ts2pes) delete ts2pes;
    if (pes2audioes) delete pes2audioes;
}

int cMarkAdDemux::Process(int Pid, uchar *Data, int Count, uchar **Pkt, int *PktLen)
{
    if ((!Data) && (!Count) && (!ts2pes) && (!pes2audioes) ||
            (!Pkt) || (!PktLen) || (!Pid)) return -1;
    *Pkt=NULL;
    *PktLen=0;

    int len=-1; // we don't want loops

    if (!peslen)
    {
        len=ts2pes->Process(Pid,Data,Count,&pespkt,&peslen);
    }
    if (pespkt)
    {

        if ((((pespkt[3]>=0xc0) && (pespkt[3]<=0xDF)) || (pespkt[3]==0xBD))
                && (!pesptr))
        {
            if (!pes2audioes)
            {
                pes2audioes=new cMarkAdPES2AudioES();
            }
            if (pes2audioes)
            {
                pesptr=pespkt;
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
            while (peslen>0)
            {
                int len2=pes2audioes->Process(pesptr,peslen,&esdata,&essize);
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
                    peslen-=len2;
                    if (!peslen) pesptr=NULL;
                    break;
                }
            }

        }
        else
        {
            *Pkt=pespkt;
            *PktLen=peslen;
            pespkt=pesptr=NULL;
            peslen=0;
        }
    }
    return len;
}

