/*
 * common.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "common.h"

cMarkAdCommon::cMarkAdCommon(int RecvNumber,MarkAdContext *maContext)
{
    macontext=maContext;
    recvnumber=RecvNumber;
    mark.Comment=NULL;
    mark.Position=0;
}

cMarkAdCommon::~cMarkAdCommon()
{
    ResetMark();
}

void cMarkAdCommon::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
}

bool cMarkAdCommon::AddMark(int Position, const char *Comment)
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

void cMarkAdCommon::SetTimerMarks(int LastIFrame)
{
    if (!macontext) return;
    if (!LastIFrame) return;

    if ((time(NULL)>macontext->General.StartTime) && (!macontext->State.ContentStarted))
    {
        char *buf=NULL;
        if (asprintf(&buf,"start of %s content (%i)",
                 macontext->General.ManualRecording ? "user" : "event",LastIFrame)!=-1)
        {
            isyslog("markad [%i]: %s",recvnumber,buf);
            AddMark(LastIFrame,buf);
            free(buf);
        }
        macontext->State.ContentStarted=LastIFrame;
    }

    if ((time(NULL)>macontext->General.EndTime) &&
            (macontext->State.ContentStarted) && (!macontext->State.ContentStopped))
    {
        char *buf=NULL;
        if (asprintf(&buf,"stop of %s content (%i)",
                 macontext->General.ManualRecording ? "user" : "event",LastIFrame)!=-1)
        {
            isyslog("markad [%i]: %s",recvnumber,buf);
            AddMark(LastIFrame,buf);
            free(buf);
        }
        macontext->State.ContentStopped=LastIFrame;
    }
}

MarkAdMark *cMarkAdCommon::Process(int LastIFrame)
{
    ResetMark();
    if (!LastIFrame) return NULL;
    SetTimerMarks(LastIFrame);
    return &mark;
}
