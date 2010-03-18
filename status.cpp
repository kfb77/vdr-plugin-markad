/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "status.h"

void cStatusMarkAd::Recording(const cDevice *Device, const char *Name, const char *FileName, bool On)
{
    if (!Device) return; // just to be safe
    if (!FileName) return; // we cannot operate without a filename

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

        // TODO: Start the standalone version ;)
    }
    else
    {
        // TODO: Start second pass?
    }

}
