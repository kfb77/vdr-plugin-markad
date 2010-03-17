/*
 * video.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __video_h_
#define __video_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog

#include <time.h>
#include <math.h>
#include "global.h"

#define LOGO_MAXHEIGHT 170
#define LOGO_MAXWIDTH  480

#define LOGO_DEFHEIGHT 100
#define LOGO_DEFWIDTH  192

#define LOGO_MAXCOUNT 3

class cMarkAdLogo
{
private:
    int recvnumber;

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
        int rpixel;  // black pixel in result
        int mpixel;  // black pixel in mask
        int status;
        int lastiframe;
        int counter;
        int corner;
        MarkAdAspectRatio aspectratio;
        bool valid;
    } area;

    int G[5][5];
    int GX[3][3];
    int GY[3][3];

    MarkAdContext *macontext;
    int Detect(int lastiframe, int *logoiframe); // ret 1 = logo, 0 = unknown, -1 = no logo
    int Load(char *file);
    void Save(int lastiframe, uchar *picture);
public:
    cMarkAdLogo(int RecvNumber, MarkAdContext *maContext);
    ~cMarkAdLogo();
    int Process(int LastIFrame, int *LogoIFrame);
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
