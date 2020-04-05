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
#define LOGO_DEFHDHEIGHT 180

#define LOGO_VMAXCOUNT 3  // count of IFrames for detection of "logo visible"
#define LOGO_IMAXCOUNT 5  // count of IFrames for detection of "logo invisible"
#define LOGO_VMARK 0.5    // percantage of pixels for visible
#define LOGO_IMARK 0.15   // percentage of pixels for invisible

enum
{
    LOGO_ERROR=-3,
    LOGO_UNINITIALIZED=-2,
    LOGO_INVISIBLE=-1,
    LOGO_NOCHANGE=0,
    LOGO_VISIBLE=1
};

enum
{
    HBORDER_UNINITIALIZED=-2,
    HBORDER_INVISIBLE=-1,
    HBORDER_VISIBLE=1
};

enum
{
    VBORDER_UNINITIALIZED=-2,
    VBORDER_INVISIBLE=-1,
    VBORDER_VISIBLE=1
};

#define MINBORDERSECS 60

enum
{
    OV_BEFORE=0,
    OV_AFTER=1
};

class cMarkAdOverlap
{
private:
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
    MarkAdPos *Process(int FrameNumber, int Frames, bool BeforeAd, bool H264);
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
    int Status()
    {
        return area.status;
    }
    void SetStatusLogoInvisible()
    {
        if (area.status==LOGO_VISIBLE)
            area.status=LOGO_INVISIBLE;
    }
    void SetStatusUninitialized()
    {
        if (area.status!=LOGO_UNINITIALIZED)
            area.status=LOGO_UNINITIALIZED;
    }
    void Clear();
};

class cMarkAdBlackBordersHoriz
{
private:
    int borderstatus;
    int borderframenumber;
    MarkAdContext *macontext;
public:
    cMarkAdBlackBordersHoriz(MarkAdContext *maContext);
    int Process(int FrameNumber,int *BorderFrameNumber);
    int Status()
    {
        return borderstatus;
    }
    void SetStatusBorderInvisible() {
        borderstatus=HBORDER_INVISIBLE;
        borderframenumber=-1;
    }
    void Clear();
};

class cMarkAdBlackBordersVert
{
private:
    int borderstatus;
    int borderframenumber;
    MarkAdContext *macontext;
public:
    cMarkAdBlackBordersVert(MarkAdContext *maContext);
    int Process(int FrameNumber,int *BorderFrameNumber);
    int Status()
    {
        return borderstatus;
    }
    void SetStatusBorderInvisible() {
        borderstatus=VBORDER_INVISIBLE;
        borderframenumber=-1;
    }
    void Clear();
};

class cMarkAdVideo
{
private:
    MarkAdContext *macontext;
    MarkAdMarks marks;

    MarkAdAspectRatio aspectratio;
    cMarkAdBlackBordersHoriz *hborder;
    cMarkAdBlackBordersVert *vborder;
    cMarkAdLogo *logo;
    cMarkAdOverlap *overlap;

    void resetmarks();
    bool addmark(int type, int position, MarkAdAspectRatio *before=NULL,
                 MarkAdAspectRatio *after=NULL);
    bool aspectratiochange(MarkAdAspectRatio &a, MarkAdAspectRatio &b, bool &start);

    int framelast;
    int framebeforelast;

public:
    cMarkAdVideo(MarkAdContext *maContext);
    ~cMarkAdVideo();
    MarkAdPos *ProcessOverlap(int FrameNumber, int Frames, bool BeforeAd, bool H264);
    MarkAdMarks *Process(int FrameNumber, int FrameNumberNext);
    void Clear();
};

#endif
