/*
 * video.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __video_h_
#define __video_h_

#include "global.h"
#include "index.h"

#define LOGO_MAXHEIGHT   250
#define LOGO_MAXWIDTH    480

#define LOGO_DEFHEIGHT   130  // increased from 100 to 130
#define LOGO_DEFWIDTH    230

#define LOGO_DEFHDHEIGHT 220  // changed from 190 to 220 for 13th_Street_HD
#define LOGO_DEFHDWIDTH  340

#define LOGO_VMAXCOUNT 3  // count of IFrames for detection of "logo visible"
#define LOGO_IMAXCOUNT 4  // count of IFrames for detection of "logo invisible", reduced from 5 to 4
#define LOGO_VMARK 0.5    // percantage of pixels for visible
#define LOGO_IMARK 0.15   // percentage of pixels for invisible

#define MIN_H_BORDER_SECS 60
#define MIN_V_BORDER_SECS 70  // keep it greater than MIN_H_BORDER_SECS fot detecting long black screens

enum {
    LOGO_ERROR = -3,
    LOGO_UNINITIALIZED = -2,
    LOGO_INVISIBLE = -1,
    LOGO_NOCHANGE = 0,
    LOGO_VISIBLE = 1
};

enum {
    BLACKSCREEN_UNINITIALIZED = -2,
    BLACKSCREEN_INVISIBLE = -1,
    BLACKSCREEN_VISIBLE = 1
};

enum {
    HBORDER_UNINITIALIZED = -2,
    HBORDER_INVISIBLE     = -1,
    HBORDER_ERROR         =  0,
    HBORDER_VISIBLE       =  1
};

enum {
    VBORDER_UNINITIALIZED = -2,
    VBORDER_INVISIBLE     = -1,
    VBORDER_ERROR         =  0,
    VBORDER_VISIBLE       =  1
};

enum {
    OV_BEFORE = 0,
    OV_AFTER = 1
};

enum {
    BRIGHTNESS_VALID = 0,
    BRIGHTNESS_SEPARATOR = -1,
    BRIGHTNESS_ERROR = -2,
    BRIGHTNESS_UNINITIALIZED = -3
};


#define MAXPIXEL LOGO_MAXWIDTH * LOGO_MAXHEIGHT

/**
 * corner area after sobel transformation
 */
typedef struct sAreaT {
#ifdef VDRDEBUG
    uchar source[PLANES][MAXPIXEL];  //!< original picture
                                     //!<

#endif
    uchar sobel[PLANES][MAXPIXEL];   //!< monochrome picture with edges (after sobel)
                                     //!<

    uchar mask[PLANES][MAXPIXEL];    //!< monochrome mask of logo
                                     //!<

    uchar result[PLANES][MAXPIXEL];  //!< result of sobel + mask
                                     //!<

    int rPixel[PLANES];              //!< black pixel in result
                                     //!<

    int mPixel[PLANES];              //!< black pixel in mask
                                     //!<

    int status;                      //!< logo status: on, off, uninitialized
                                     //!<

    int frameNumber;                 //!< start/stop frame number
                                     //!<

    int counter;                     //!< since how many logo on, offs detected?
                                     //!<

    int corner;                      //!< corner of logo
                                     //!<

    int intensity;                   //!< area intensity (higher -> brighter)
                                     //!<

    sAspectRatio AspectRatio;        //!< aspect ratio of the video
                                     //!<

    bool valid[PLANES];              //!< <b>true:</b> logo mask data are valid <br>
                                     //!< <b>false:</b> logo mask is not valid
                                     //!<

} sAreaT;


class cMarkAdOverlap {
    private:
        sMarkAdContext *maContext = NULL;
        typedef int simpleHistogram[256];

/**
 * histogram buffer for overlap detection
 */
        typedef struct sHistBuffer {
            int frameNumber;           //!< frame number
                                       //!<

            simpleHistogram histogram; //!< simple frame histogram
                                       //!<

        } sHistBuffer;

        sHistBuffer *histbuf[2];
        int histcnt[2];
        int histframes[2];

        int lastframenumber;
        sOverlapPos result;
        int similarCutOff;
        int similarMaxCnt;
        int areSimilar(simpleHistogram &hist1, simpleHistogram &hist2);
        void getHistogram(simpleHistogram &dest);
        sOverlapPos *Detect();
        void Clear();
    public:
        explicit cMarkAdOverlap(sMarkAdContext *maContextParam);
        ~cMarkAdOverlap();
        sOverlapPos *Process(const int FrameNumber, const int Frames, const bool BeforeAd, const bool H264);
};


class cMarkAdLogo {
    private:
        cIndex *recordingIndexMarkAdLogo = NULL;
        enum {
            TOP_LEFT,
            TOP_RIGHT,
            BOTTOM_LEFT,
            BOTTOM_RIGHT
        };
        int LOGOHEIGHT = 0; // max. 140
        int LOGOWIDTH = 0; // 192-288
        sAreaT area;
        int GX[3][3];
        int GY[3][3];
        sMarkAdContext *maContext = NULL;
        bool pixfmt_info;
        bool isInitColourChange = false;

        bool SetCoorginates(int *xstart, int *xend, int *ystart, int *yend, const int plane);
        int ReduceBrightness(const int framenumber);
        bool SobelPlane(const int plane); // do sobel operation on plane
        int Load(const char *directory, const char *file, const int plane);
        bool Save(const int framenumber, uchar picture[PLANES][MAXPIXEL], const short int plane, const int debug);
        void SaveFrameCorner(const int framenumber, const int debug);
        void LogoGreyToColour();
    public:
        explicit cMarkAdLogo(sMarkAdContext *maContext, cIndex *recordingIndex);
        int Detect(const int frameBefore, const int frameCurrent, int *logoFrameNumber); // return: 1 = logo, 0 = unknown, -1 = no logo
        int Process(const int iFrameBefore, const int iFrameCurrent, const int frameCurrent, int *logoFrameNumber);
        int Status() {
            return area.status;
        }
        void SetStatusLogoInvisible() {
            if (area.status == LOGO_VISIBLE) area.status = LOGO_INVISIBLE;
        }
        void SetStatusUninitialized() {
            area.status = LOGO_UNINITIALIZED;
        }
        void Clear(const bool isRestart = false, const bool inBroadCast = false);
        void SetLogoSize(const int width, const int height);
        sAreaT *GetArea();
};


class cMarkAdBlackScreen {
    private:
        int blackScreenstatus;
        sMarkAdContext *maContext = NULL;
    public:
        explicit cMarkAdBlackScreen(sMarkAdContext *maContextParam);
        int Process(const int frameCurrent);
        void Clear();
};


class cMarkAdBlackBordersHoriz {
    private:
        int borderstatus;
        int borderframenumber;
        sMarkAdContext *maContext = NULL;
    public:
        explicit cMarkAdBlackBordersHoriz(sMarkAdContext *maContextParam);
        int GetFirstBorderFrame();
        int Process(const int frameNumber, int *BorderFrameNumber);
        void SetStatusBorderInvisible() {
            borderstatus = HBORDER_INVISIBLE;
            borderframenumber = -1;
        }
        void Clear();
};


class cMarkAdBlackBordersVert {
    private:
        int borderstatus;
        int borderframenumber;
        sMarkAdContext *maContext = NULL;
    public:
        explicit cMarkAdBlackBordersVert(sMarkAdContext *maContextParam);
        int GetFirstBorderFrame();
        int Process(int FrameNumber, int *BorderFrameNumber);
        void SetStatusBorderInvisible() {
            borderstatus = VBORDER_INVISIBLE;
            borderframenumber = -1;
        }
        void Clear();
};


class cMarkAdVideo {
    public:
        explicit cMarkAdVideo(sMarkAdContext *maContext, cIndex *recordingIndex);
        ~cMarkAdVideo();
        cMarkAdVideo(const cMarkAdVideo &origin) {   //  copy constructor, not used, only for formal reason
            maContext = origin.maContext;
            blackScreen = NULL;
            hborder = NULL;
            vborder = NULL;
            logo = NULL;
            overlap = NULL;
        };
        cMarkAdVideo &operator =(const cMarkAdVideo *origin) {  // operator=, not used, only for formal reason
            maContext = origin->maContext;
            blackScreen = NULL;
            hborder = NULL;
            vborder = NULL;
            logo = NULL;
            overlap = NULL;
            aspectRatio = {};
            return *this;
        }
        sOverlapPos *ProcessOverlap(const int FrameNumber, const int Frames, const bool BeforeAd, const bool H264);
        sMarkAdMarks *Process(int iFrameBefore, const int iFrameCurrent, const int frameCurrent);
        bool ReducePlanes(void);
        void Clear(bool isRestart, bool inBroadCast = false);

    private:
        void resetmarks();

        bool addmark(int type, int position, sAspectRatio *before = NULL, sAspectRatio *after = NULL);

/**
 * check if video aspect ratio changes between the two aspect ratios
 * @param[in]  AspectRatioA first video aspact ratio
 * @param[in]  AspectRatioB second video aspect ratio
 * @param[out] start true if aspect ratio change to 4:3 at video decoding start, from 0:0 (unknown) to 4:3
 * @return true if aspect ratio changed, false otherwise
 */
        bool AspectRatioChange(const sAspectRatio &AspectRatioA, const sAspectRatio &AspectRatioB, bool &start);

        cIndex *recordingIndexMarkAdVideo = NULL; //!< recording index
                                                  //!<
        sMarkAdContext *maContext = NULL;         //!< markad context
                                                  //!<
        sMarkAdMarks marks = {};                  //!< array of marks to add to list
                                                  //!<
        sAspectRatio aspectRatio = {};            //!< video display aspect ratio (DAR)
                                                  //!<
        cMarkAdBlackScreen *blackScreen;          //!< pointer to class cMarkAdBlackScreen
                                                  //!<
        cMarkAdBlackBordersHoriz *hborder;        //!< pointer to class cMarkAdBlackBordersHoriz
                                                  //!<
        cMarkAdBlackBordersVert *vborder;         //!< pointer to class cMarkAdBlackBordersVert
                                                  //!<
        cMarkAdLogo *logo;                        //!< pointer to class cMarkAdLogo
                                                  //!<
        cMarkAdOverlap *overlap;                  //!< pointer to class cMarkAdOverlap
                                                  //!<
};
#endif
