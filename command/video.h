/*
 * video.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __video_h_
#define __video_h_

#include "global.h"

#define LOGO_MAXHEIGHT   250
#define LOGO_MAXWIDTH    480

#define LOGO_DEFHEIGHT   100
#define LOGO_DEFWIDTH    192
#define LOGO_DEFHDWIDTH  288

#define LOGO_VMAXCOUNT 3  // count of IFrames for detection of "logo visible"
#define LOGO_IMAXCOUNT 5  // count of IFrames for detection of "logo invisible"
#define LOGO_VMARK 0.5    // percantage of pixels for visible
#define LOGO_IMARK 0.15   // percentage of pixels for invisible

class cMarkAdOverlap
{
private:
#define BEFORE 0
#define AFTER 1
    MarkAdContext *macontext;
    typedef int simpleHistogram[256];

    typedef struct
    {
        int framenumber;
        simpleHistogram histogram;
    } histbuffer;
    histbuffer *histbuf[2];
    int histcnt[2];
    int histframes[2];

    int lastframenumber;

    MarkAdPos result;

    int similarCutOff;
    int similarMaxCnt;
    bool areSimilar(simpleHistogram &hist1, simpleHistogram &hist2);
    void getHistogram(simpleHistogram &dest);
    MarkAdPos *Detect();
    void Clear();
public:
    cMarkAdOverlap(MarkAdContext *maContext);
    ~cMarkAdOverlap();
    MarkAdPos *Process(int FrameNumber, int Frames, bool BeforeAd);
};

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
#ifdef VDRDEBUG
        uchar source[4][MAXPIXEL]; // original picture
#endif
        uchar sobel[4][MAXPIXEL];  // monochrome picture with edges (after sobel)
        uchar mask[4][MAXPIXEL];   // monochrome mask of logo
        uchar result[4][MAXPIXEL]; // result of sobel + mask
        int rpixel[4];             // black pixel in result
        int mpixel[4];             // black pixel in mask
        int status;                // status = LOGO on, off, uninitialized
        int framenumber;           // start/stop frame
        int counter;               // how many logo on, offs detected?
        int corner;                // which corner
        int intensity;             // intensity (higher -> brighter)
        MarkAdAspectRatio aspectratio; // aspectratio
        bool valid[4];             // logo mask valid?
    } area;

    int G[5][5];
    int GX[3][3];
    int GY[3][3];

    MarkAdContext *macontext;
    bool pixfmt_info;
    int SobelPlane(int plane); // do sobel operation on plane
    int Detect(int framenumber, int *logoframenumber); // ret 1 = logo, 0 = unknown, -1 = no logo
    int Load(const char *directory, char *file, int plane);
    void Save(int framenumber, uchar picture[4][MAXPIXEL], int plane);
public:
    cMarkAdLogo(MarkAdContext *maContext);
    int Process(int FrameNumber, int *LogoFrameNumber);
    void Clear();
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
    int borderframenumber;
    MarkAdContext *macontext;
public:
    cMarkAdBlackBordersHoriz(MarkAdContext *maContext);
    int Process(int FrameNumber,int *BorderFrameNumber);
    void Clear();
};

class cMarkAdVideo
{
private:
    MarkAdContext *macontext;
    MarkAdMarks marks;

    MarkAdAspectRatio aspectratio;
    cMarkAdBlackBordersHoriz *hborder;
    cMarkAdLogo *logo;
    cMarkAdOverlap *overlap;

    void ResetMarks();
    bool AddMark(int Type, int Position, const char *Comment);
    bool AspectRatioChange(MarkAdAspectRatio *a, MarkAdAspectRatio *b);

    int framelast;

public:
    cMarkAdVideo(MarkAdContext *maContext);
    ~cMarkAdVideo();
    MarkAdPos *Process2ndPass(int FrameNumber, int Frames, bool BeforeAd);
    MarkAdMarks *Process(int FrameNumber, int FrameNumberNext);
    void Clear();
};

#endif
