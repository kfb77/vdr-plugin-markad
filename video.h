/*
 * video.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __video_h_
#define __video_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog

#include <time.h>
#include <stdio.h>

#include "global.h"

class cMarkAdBlackBordersHoriz
{
private:
    int borderstatus;
    int borderiframe;
    time_t borderstarttime;
    void SaveFrame(int LastIFrame);
    MarkAdContext *macontext;
public:
    cMarkAdBlackBordersHoriz(int RecvNumber, MarkAdContext *maContext);
    int Process(int LastIFrame,int *BorderIFrame);
};

class cMarkAdVideo
{
private:
    int recvnumber;
    MarkAdContext *macontext;
    MarkAdMark mark;

    MarkAdAspectRatio aspectratio;
    cMarkAdBlackBordersHoriz *hborder;

    void ResetMark();
    bool AddMark(int Position, const char *Comment);
    bool AspectRatioChange(MarkAdAspectRatio *a, MarkAdAspectRatio *b);
    void SetTimerMarks(int LastIFrame);

public:
    cMarkAdVideo(int RecvNumber,MarkAdContext *maContext);
    ~cMarkAdVideo();
    MarkAdMark *Process(int LastIFrame);
};

#endif
