/*
 * video.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __video_h_
#define __video_h_

#include <time.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "global.h"

extern "C"
{
#include "debug.h"
}

#define LOGO_MAXHEIGHT   170
#define LOGO_MAXWIDTH    480

#define LOGO_DEFHEIGHT   100
#define LOGO_DEFWIDTH    192
#define LOGO_DEFHDWIDTH  288

#define LOGO_VMAXCOUNT 3  // count of IFrames for detection of "logo invisible"
#define LOGO_IMAXCOUNT 5  // count of IFrames for detection of "logo invisible"
#define LOGO_VMARK 0.8    // percantage of pixels for visible
#define LOGO_IMARK 0.15   // percentage of pixels for invisible

class cMarkAdLogo
{
private:

    enum
    {
        TOP_LEFT,
        TOP_RIGHT,
        BOTTOM_LEFT,
        BOTTOM_RIGHT
    };

    enum
    {
        ERROR=-3,
        UNINITIALIZED=-2,
        NOLOGO=-1,
        NOCHANGE=0,
        LOGO=1
    };

    int LOGOHEIGHT; // max. 140
    int LOGOWIDTH; // 192-288

#define MAXPIXEL LOGO_MAXWIDTH*LOGO_MAXHEIGHT

    struct areaT
    {
        uchar source[MAXPIXEL]; // original grayscale picture
        uchar sobel[MAXPIXEL];  // monochrome picture with edges (after sobel)
        uchar mask[MAXPIXEL];   // monochrome mask of logo
        uchar result[MAXPIXEL]; // result of sobel + mask
        int rpixel;             // black pixel in result
        int mpixel;             // black pixel in mask
        int status;             // status = LOGO on, off, uninitialized
        int lastiframe;         // start/stop frame
        int counter;            // how many logo on, offs detected?
        int corner;             // which corner
        MarkAdAspectRatio aspectratio; // aspectratio
        bool valid;             // logo mask valid?
    } area;

    int G[5][5];
    int GX[3][3];
    int GY[3][3];

    MarkAdContext *macontext;
    int Detect(int lastiframe, int *logoiframe); // ret 1 = logo, 0 = unknown, -1 = no logo
    int Load(char *file);
    void Save(int lastiframe, uchar *picture);
public:
    cMarkAdLogo(MarkAdContext *maContext);
    ~cMarkAdLogo();
    int Process(int LastIFrame, int *LogoIFrame);
};

class cMarkAdBlackBordersHoriz
{
private:
    enum
    {
        ERROR=-3,
        UNINITIALIZED=-2,
        NOBORDER=-1,
        NOCHANGE=0,
        BORDER=1
    };

    int borderstatus;
    int borderiframe;
    MarkAdContext *macontext;
public:
    cMarkAdBlackBordersHoriz(MarkAdContext *maContext);
    int Process(int LastIFrame,int *BorderIFrame);
};

class cMarkAdVideo
{
private:
    MarkAdContext *macontext;
    MarkAdMark mark;

    MarkAdAspectRatio aspectratio;
    cMarkAdBlackBordersHoriz *hborder;
    cMarkAdLogo *logo;

    void ResetMark();
    bool AddMark(int Type, int Position, const char *Comment);
    bool AspectRatioChange(MarkAdAspectRatio *a, MarkAdAspectRatio *b);
    void SetTimerMarks(int LastIFrame);

public:
    cMarkAdVideo(MarkAdContext *maContext);
    ~cMarkAdVideo();
    MarkAdMark *Process(int LastIFrame);
};

#endif
