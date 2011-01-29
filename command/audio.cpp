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
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
    result.CommentBefore=NULL;
    result.CommentAfter=NULL;
    Clear();
}

cMarkAdAudio::~cMarkAdAudio()
{
    ResetMark();
    Clear();
}

void cMarkAdAudio::Clear()
{
    channels=0;
    if (result.CommentBefore) free(result.CommentBefore);
    if (result.CommentAfter) free(result.CommentAfter);
    memset(&result,0,sizeof(result));
}

void cMarkAdAudio::ResetMark()
{
    if (!mark.Type) return;
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

bool cMarkAdAudio::ChannelChange(int a, int b)
{
    if ((a==0) || (b==0)) return false;
    if (a!=b) return true;
    return false;
}

MarkAdMark *cMarkAdAudio::Process(int FrameNumber, int FrameNumberNext)
{
    if ((!FrameNumber) || (!FrameNumberNext)) return NULL;
    ResetMark();

    if (ChannelChange(macontext->Audio.Info.Channels,channels))
    {
        char *buf=(char *) calloc(1,256);
        if (!buf) return NULL;

        snprintf(buf,255,"audio channel change from %i to %i (", channels,
                 macontext->Audio.Info.Channels);

        if (macontext->Audio.Info.Channels>2)
        {
            char nbuf[20];
            snprintf(nbuf,sizeof(nbuf),"%i)*",FrameNumberNext);
            nbuf[19]=0;
            strcat(buf,nbuf);
            AddMark(MT_CHANNELSTART,FrameNumberNext,buf);
        }
        else
        {
            char nbuf[20];
            snprintf(nbuf,sizeof(nbuf),"%i)",framelast);
            nbuf[19]=0;
            strcat(buf,nbuf);
            AddMark(MT_CHANNELSTOP,framelast,buf);
        }
        free(buf);
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
