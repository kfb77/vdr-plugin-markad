/*
 * audio.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"

cMarkAdAudio::cMarkAdAudio(MarkAdContext *maContext)
{
    macontext=maContext;
    mark.Position=0;
    mark.Type=0;
    Clear();
}

cMarkAdAudio::~cMarkAdAudio()
{
    resetmark();
    Clear();
}

void cMarkAdAudio::Clear()
{
    channels=0;
}

void cMarkAdAudio::resetmark()
{
    if (!mark.Type) return;
    memset(&mark,0,sizeof(mark));
}

void cMarkAdAudio::setmark(int type, int position, int channelsbefore, int channelsafter)
{
    mark.ChannelsBefore=channelsbefore;
    mark.ChannelsAfter=channelsafter;
    mark.Position=position;
    mark.Type=type;
}

bool cMarkAdAudio::channelchange(int a, int b)
{
    if ((a==0) || (b==0)) return false;
    if (a!=b) return true;
    return false;
}

MarkAdMark *cMarkAdAudio::Process(int FrameNumber, int FrameNumberNext)
{
    if ((!FrameNumber) || (!FrameNumberNext)) return NULL;
    resetmark();

    if (channelchange(macontext->Audio.Info.Channels,channels))
    {
        if (macontext->Audio.Info.Channels>2)
        {
            setmark(MT_CHANNELSTART,FrameNumberNext,macontext->Audio.Info.Channels,channels);
        }
        else
        {
            setmark(MT_CHANNELSTOP,framelast,macontext->Audio.Info.Channels,channels);
        }
    }

    channels=macontext->Audio.Info.Channels;
    framelast=FrameNumber;
    if (mark.Position)
    {
        return &mark;
    }
    else
    {
        return NULL;
    }
}
