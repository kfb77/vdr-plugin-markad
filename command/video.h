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


enum {
    LOGO_ERROR=-3,
    LOGO_UNINITIALIZED=-2,
    LOGO_INVISIBLE=-1,
    LOGO_NOCHANGE=0,
    LOGO_VISIBLE=1
};

enum {
    BLACKSCREEN_UNINITIALIZED=-2,
    BLACKSCREEN_INVISIBLE=-1,
    BLACKSCREEN_VISIBLE=1
};

enum {
    HBORDER_UNINITIALIZED=-2,
    HBORDER_INVISIBLE=-1,
    HBORDER_VISIBLE=1
};

enum {
    VBORDER_UNINITIALIZED=-2,
    VBORDER_INVISIBLE=-1,
    VBORDER_VISIBLE=1
};

#define MINBORDERSECS 60

enum {
    OV_BEFORE=0,
    OV_AFTER=1
};


#define MAXPIXEL LOGO_MAXWIDTH*LOGO_MAXHEIGHT

typedef struct {
#ifdef VDRDEBUG
    uchar source[PLANES][MAXPIXEL]; // original picture
#endif
    uchar sobel[PLANES][MAXPIXEL];  // monochrome picture with edges (after sobel)
    uchar mask[PLANES][MAXPIXEL];   // monochrome mask of logo
    uchar result[PLANES][MAXPIXEL]; // result of sobel + mask
    int rpixel[PLANES];             // black pixel in result
    int mpixel[PLANES];             // black pixel in mask
    int status;                // status = LOGO on, off, uninitialized
    int framenumber;           // start/stop frame
    int counter;               // how many logo on, offs detected?
    int corner;                // which corner
    int intensity;             // intensity (higher -> brighter)
    MarkAdAspectRatio aspectratio; // aspectratio
    bool valid[PLANES];             // logo mask valid?
} areaT;


class cMarkAdOverlap {
    private:
        MarkAdContext *macontext;
        typedef int simpleHistogram[256];
        typedef struct {
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
        explicit cMarkAdOverlap(MarkAdContext *maContext);
        ~cMarkAdOverlap();
        MarkAdPos *Process(int FrameNumber, int Frames, bool BeforeAd, bool H264);
};


class cMarkAdLogo {
    private:
        enum {
            TOP_LEFT,
            TOP_RIGHT,
            BOTTOM_LEFT,
            BOTTOM_RIGHT
        };
        int LOGOHEIGHT = 0; // max. 140
        int LOGOWIDTH = 0; // 192-288
        areaT area;
        int GX[3][3];
        int GY[3][3];
        MarkAdContext *macontext;
        bool pixfmt_info;
        int SobelPlane(const int plane); // do sobel operation on plane
        int Load(const char *directory, const char *file, const int plane);
        bool Save(const int framenumber, uchar picture[PLANES][MAXPIXEL], const short int plane);
    public:
        explicit cMarkAdLogo(MarkAdContext *maContext);
        int Detect(int framenumber, int *logoframenumber); // ret 1 = logo, 0 = unknown, -1 = no logo
        int Process(int FrameNumber, int *LogoFrameNumber);
        int Status() {
            return area.status;
        }
        void SetStatusLogoInvisible() {
            if (area.status==LOGO_VISIBLE) area.status=LOGO_INVISIBLE;
        }
        void SetStatusUninitialized() {
            area.status=LOGO_UNINITIALIZED;
        }
        void Clear();
        areaT *GetArea();
};


class cMarkAdBlackScreen {
    private:
        int blackScreenstatus;
        MarkAdContext *macontext;
    public:
        explicit cMarkAdBlackScreen(MarkAdContext *maContext);
        int Process(int FrameNumber,int *BlackFrameNumber);
        void Clear();
};


class cMarkAdBlackBordersHoriz {
    private:
        int borderstatus;
        int borderframenumber;
        MarkAdContext *macontext;
    public:
        explicit cMarkAdBlackBordersHoriz(MarkAdContext *maContext);
        int Process(int FrameNumber,int *BorderFrameNumber);
        int Status() {
            return borderstatus;
        }
        void SetStatusBorderInvisible() {
            borderstatus=HBORDER_INVISIBLE;
            borderframenumber=-1;
        }
        void Clear();
};


class cMarkAdBlackBordersVert {
    private:
        int borderstatus;
        int borderframenumber;
        MarkAdContext *macontext;
    public:
        explicit cMarkAdBlackBordersVert(MarkAdContext *maContext);
        int Process(int FrameNumber,int *BorderFrameNumber);
        int Status() {
            return borderstatus;
        }
        void SetStatusBorderInvisible() {
            borderstatus=VBORDER_INVISIBLE;
            borderframenumber=-1;
        }
        void Clear();
};


class cMarkAdVideo {
    private:
        MarkAdContext *macontext;
        MarkAdMarks marks = {0};
        MarkAdAspectRatio aspectratio;
        cMarkAdBlackScreen *blackScreen;
        cMarkAdBlackBordersHoriz *hborder;
        cMarkAdBlackBordersVert *vborder;
        cMarkAdLogo *logo;
        cMarkAdOverlap *overlap;
        void resetmarks();
        bool addmark(int type, int position, MarkAdAspectRatio *before=NULL, MarkAdAspectRatio *after=NULL);
        bool aspectratiochange(MarkAdAspectRatio &a, MarkAdAspectRatio &b, bool &start);
        int framelast;
        int framebeforelast;
    public:
        explicit cMarkAdVideo(MarkAdContext *maContext);
        ~cMarkAdVideo();
        cMarkAdVideo(const cMarkAdVideo &origin) {   //  copy constructor, not used, only for formal reason
            macontext = NULL;
            blackScreen = NULL;
            hborder = NULL;
            vborder = NULL;
            logo = NULL;
            overlap = NULL;
            framelast = 0;
            framebeforelast = 0;
        };
        cMarkAdVideo &operator =(const cMarkAdVideo *origin) {  // operator=, not used, only for formal reason
            macontext = NULL;
            blackScreen = NULL;
            hborder = NULL;
            vborder = NULL;
            logo = NULL;
            overlap = NULL;
            framelast = 0;
            framebeforelast = 0;
            return *this;
        }
        MarkAdPos *ProcessOverlap(int FrameNumber, int Frames, bool BeforeAd, bool H264);
        MarkAdMarks *Process(int FrameNumber, int FrameNumberNext);
        void Clear(bool isRestart);
};
#endif
