/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "status.h"

cStatusMarkAd::cStatusMarkAd()
{
    memset(recv,0,sizeof(recv));
}

int cStatusMarkAd::GetFreeReceiver()
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (recv[i]==NULL) return i;
    }
    return -1;
}

int cStatusMarkAd::FindReceiver(const char *FileName)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (recv[i])
        {
            if (!strcmp(recv[i]->FileName(),FileName)) return i;
        }
    }
    return -1;
}

void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    if (!Device) return; // just to be safe
    if (!FileName) return; // we cannot operate without a filename

    int recvnumber;

    if (On)
    {
        if (!Name) return; // we cannot operate without name ;)

        cTimer *timer=NULL;

        for (cTimer *Timer = Timers.First(); Timer; Timer=Timers.Next(Timer))
        {
            if (Timer->Recording() && (!strcmp(Timer->File(),Name)))
            {
                timer=Timer;
                break;
            }
        }

        if (!timer) return;

        recvnumber=GetFreeReceiver();
        if  (recvnumber<0) return;

        recv[recvnumber] = new cMarkAdReceiver(recvnumber,FileName,timer);
        dsyslog("markad [%i]: start recording %s ",recvnumber,FileName);
        ((cDevice *) Device)->AttachReceiver(recv[recvnumber]);
    }
    else
    {
        recvnumber=FindReceiver(FileName);
        if (recvnumber<0) return;

        dsyslog("markad [%i]: stop recording %s ",recvnumber,FileName);
        ((cDevice *) Device)->Detach(recv[recvnumber]);
        delete recv[recvnumber];
        recv[recvnumber]=NULL;
    }

}
