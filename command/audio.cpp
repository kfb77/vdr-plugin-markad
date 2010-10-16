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

extern "C"
{
#include "debug.h"
}

#include "audio.h"

cMarkAdAudio::cMarkAdAudio(MarkAdContext *maContext)
{
    macontext=maContext;
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
    result.CommentBefore=NULL;
    result.CommentAfter=NULL;
    Clear();
}

cMarkAdAudio::~cMarkAdAudio()
{
    Clear();
    ResetMark();
}

void cMarkAdAudio::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
}

bool cMarkAdAudio::AddMark(int Type, int Position, const char *Comment)
{
    if (!Comment) return false;
    if (mark.Comment)
    {
        int oldlen=strlen(mark.Comment);
        mark.Comment=(char *) realloc(mark.Comment,oldlen+10+strlen(Comment));
        if (!mark.Comment)
        {
            mark.Position=0;
            return false;
        }
        strcat(mark.Comment," [");
        strcat(mark.Comment,Comment);
        strcat(mark.Comment,"]");
    }
    else
    {
        mark.Comment=strdup(Comment);
    }
    mark.Position=Position;
    mark.Type=Type;
    return true;
}

bool cMarkAdAudio::AnalyzeGain(int FrameNumber)
{
    if (!macontext->Audio.Data.Valid) return false;

    int samples=macontext->Audio.Data.SampleBufLen/
                sizeof(*macontext->Audio.Data.SampleBuf)/
                macontext->Audio.Info.Channels;

    double left[samples];
    double right[samples];

    for (int i=0; i<samples; i++)
    {
        left[i]=macontext->Audio.Data.SampleBuf[0+(i*2)];
        right[i]=macontext->Audio.Data.SampleBuf[1+(i*2)];
    }

    if (FrameNumber!=lastframe_gain)
    {
        if ((lastframe_gain>0) && (audiogain.AnalyzedSamples()>=(3*samples)))
        {
            double gain = audiogain.GetGain();
            printf("%05i %+.2f db\n",lastframe_gain,gain);
        }
        audiogain.Init(macontext->Audio.Info.SampleRate);
        lastframe_gain=-1;
    }

    if (audiogain.AnalyzeSamples(left,right,samples,2)!=GAIN_ANALYSIS_OK)
    {
        lastframe_gain=-1;
        return false;
    }
    else
    {
        lastframe_gain=FrameNumber;
    }
    return true;
}

void cMarkAdAudio::Clear()
{
    channels=0;
    lastframe_gain=-1;
    if (result.CommentBefore) free(result.CommentBefore);
    if (result.CommentAfter) free(result.CommentAfter);
    memset(&result,0,sizeof(result));
}

bool cMarkAdAudio::ChannelChange(int a, int b)
{
    if ((a==0) || (b==0)) return false;
    if (a!=b) return true;
    return false;
}

MarkAdPos *cMarkAdAudio::Process2ndPass(int FrameNumber)
{
    if (!FrameNumber) return NULL;
#if 0
    if (AnalyzeGain(FrameNumber))
    {
        if (result.CommentBefore) free(result.CommentBefore);
        if (asprintf(&result.CommentBefore,"audio silence detection (%i)",FrameNumber)==-1)
        {
            result.CommentBefore=NULL;
        }
        result.FrameNumberBefore=FrameNumber;
        return &result;
    }
#endif
    return NULL;
}

MarkAdMark *cMarkAdAudio::Process(int FrameNumber, int FrameNumberNext)
{
    if ((!FrameNumber) || (!FrameNumberNext)) return NULL;
    ResetMark();

    if (ChannelChange(macontext->Audio.Info.Channels,channels))
    {
        bool start=false;
        if (macontext->Audio.Info.DolbyDigital51)
        {
            if (macontext->Audio.Info.Channels>2) start=true;
            else start=false;
        }
        else
        {
            if (macontext->Audio.Info.Channels>2) start=false;
            else start=true;
        }

        char *buf=NULL;
        if (asprintf(&buf,"audio channel change from %i to %i (%i)%s", channels,
                     macontext->Audio.Info.Channels,
                     start ? FrameNumberNext :
                     framelast, start ? "*" : "")!=-1)
        {
            if (start)
            {
                AddMark(MT_CHANNELSTART,FrameNumberNext,buf);
            }
            else
            {
                AddMark(MT_CHANNELSTOP,framelast,buf);
            }
            free(buf);
        }
    }

    channels=macontext->Audio.Info.Channels;
    framelast=FrameNumberNext;
    return &mark;
}

