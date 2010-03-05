/*
 * audio.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "audio.h"

cMarkAdAudio::cMarkAdAudio(int RecvNumber,MarkAdContext *maContext)
{
    macontext=maContext;
    recvnumber=RecvNumber;
    mark.Comment=NULL;
    mark.Position=0;
    channels=0;
}

cMarkAdAudio::~cMarkAdAudio()
{
    ResetMark();
}

void cMarkAdAudio::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
}

bool cMarkAdAudio::AddMark(int Position, const char *Comment)
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
    return true;
}

bool cMarkAdAudio::ChannelChange(int a, int b)
{
    if ((a==0) || (b==0)) return false;
    if (a!=b) return true;
    return false;
}

MarkAdMark *cMarkAdAudio::Process(int LastIFrame)
{
    ResetMark();
    if (!LastIFrame) return NULL;

    if (ChannelChange(macontext->Audio.Info.Channels,channels))
    {
        char *buf=NULL;
        if (asprintf(&buf,"audio channel change from %i to %i (%i)", channels,
                     macontext->Audio.Info.Channels,LastIFrame)!=-1)
        {
            isyslog("markad [%i]: %s",recvnumber, buf);
            AddMark(LastIFrame,buf);
            free(buf);
        }
    }

    channels=macontext->Audio.Info.Channels;
    return &mark;
}

