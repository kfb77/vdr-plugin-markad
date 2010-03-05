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

#if 1
#include <stdio.h>
#endif

#include <math.h>

#include "global.h"

class cMarkAdLogo
{
private:
#define MAXFRAMES 25

    enum
    {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    int LOGOHEIGHT; // max. 100
    int LOGOWIDTH; // max. 288

    struct area
    {
        uchar plane[28800];
        bool init;
        int blackpixel;
//        int cntfound;
    } area[4];

    int savedlastiframe;
    int framecnt;

    int logostart;

    int GX[3][3];
    int GY[3][3];

    int counter;

    int logostate;
    MarkAdContext *macontext;
    void CheckCorner(int corner);
    void CheckCorners(int lastiframe);
    void RestartLogoDetection();
    bool LogoVisible();

    /*
        void ResetLogoDetection();
        bool LogoFound();
    */
    void SaveLogo(int corner, int lastiframe);
public:
    cMarkAdLogo(int RecvNumber, MarkAdContext *maContext);
    ~cMarkAdLogo();
    int Process(int LastIFrame);
};

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
    cMarkAdLogo *logo;

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
