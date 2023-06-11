/**
 * @file video.h
 * A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __video_h_
#define __video_h_

#include "global.h"
#include "index.h"
#include "criteria.h"

#define LOGO_VMAXCOUNT 3       //!< count of IFrames for detection of "logo visible"
                               //!<
#define LOGO_IMAXCOUNT 4       //!< count of IFrames for detection of "logo invisible", reduced from 5 to 4
                               //!<
#define LOGO_VMARK 0.5         //!< percantage of pixels for visible
                               //!<
#define LOGO_IMARK 0.18        //!< percentage of pixels for invisible changed from 0,15 to 0,18
                               //!<

#define MIN_H_BORDER_SECS  79  //!< minimum lenght of horizontal border, changed from 97 to 79, shortest full hborder, after this logo in border results in lower values
                               //!<
#define MIN_V_BORDER_SECS 140  //!< minimum lenght of vertical border before it is accepted <br>
                               //!< changed from 128 to 140 <br>
                               //!< need a long sequence to prevent long darks scenes detected as border <br>
                               //!< keep it greater than MIN_H_BORDER_SECS for detecting long black screens


/**
 * logo detection status
 */
enum eLogoStatus {
    LOGO_RESTART       = -4,
    LOGO_ERROR         = -3,
    LOGO_UNINITIALIZED = -2,
    LOGO_INVISIBLE     = -1,
    LOGO_NOCHANGE      =  0,
    LOGO_VISIBLE       =  1
};

enum {
    SCENE_UNINITALISIZED = -2,
    SCENE_NOCHANGE       =  0,
    SCENE_CHANGED        =  1
};

enum {
    BLACKSCREEN_UNINITIALIZED = -2,
    BLACKSCREEN_INVISIBLE     = -1,
    BLACKSCREEN_VISIBLE       =  1
};

enum {
    HBORDER_RESTART       = -3,
    HBORDER_UNINITIALIZED = -2,
    HBORDER_INVISIBLE     = -1,
    HBORDER_ERROR         =  0,
    HBORDER_VISIBLE       =  1
};

enum {
    VBORDER_RESTART       = -3,
    VBORDER_UNINITIALIZED = -2,
    VBORDER_INVISIBLE     = -1,
    VBORDER_ERROR         =  0,
    VBORDER_VISIBLE       =  1
};

enum {
    OV_BEFORE = 0,
    OV_AFTER  = 1
};



/**
 * corner area after sobel transformation
 */
typedef struct sAreaT {
    uchar **sobel      = NULL;       //!< monochrome picture from edge after sobel transformation, memory will be alocated after we know video resolution
                                     //!<

    uchar **mask       = NULL;       //!< monochrome mask of logo, memory will be alocated after we know video resolution
                                     //!<

    uchar **result     = NULL;       //!< result of sobel + mask, memory will be alocated after we know video resolution
                                     //!<

    int rPixel[PLANES];              //!< black pixel in result
                                     //!<

    int mPixel[PLANES]    = {0};     //!< black pixel in mask
                                     //!<

    int status;                      //!< logo status: on, off, uninitialized
                                     //!<

    int frameNumber       = -1;      //!< start/stop frame number
                                     //!<

    int counter;                     //!< how many logo on, offs detected
                                     //!<

    int corner;                      //!< corner of logo
                                     //!<

    int intensity;                   //!< area intensity (higher -> brighter)
                                     //!<

    sAspectRatio AspectRatio;        //!< aspect ratio of the video
                                     //!<

    bool valid[PLANES];              //!< <b>true:</b> logo mask data (logo) are valid <br>
                                     //!< <b>false:</b> logo mask (logo) is not valid
                                     //!<
} sAreaT;


/**
 * calculate logo default and maximum size dependent on video resolution
 */
class cLogoSize {
    public:
        cLogoSize();
        ~cLogoSize();

/**
 * calculate default logo size dependent on video resolution, used to extract logo from recording
 * @param width video width in pixel
 * @return default logo width and heigth in pixel
 */
        sLogoSize GetDefaultLogoSize(const int width);

/**
 * calculatate maximum logo size dependent on video resolution
 * @param width video width in pixel
 * @return maximum valid logo width and heigth in pixel
 */
        sLogoSize GetMaxLogoSize(const int width);

/**
 * get maximum pixel count of a logo dependent on video resolution
 * @param width video width
 * @return maximum pixel count of the logo
 */
        int GetMaxLogoPixel(const int width);

    private:
        int videoWidth = 0;  //!< video width
                             //!<
};


/**
 * class to detect overlaping scenes before and after advertising
 */
class cMarkAdOverlap {
    public:

/**
 * constructor of overlap detection
 * @param maContextParam markad context
 */
        explicit cMarkAdOverlap(sMarkAdContext *maContextParam);

        ~cMarkAdOverlap();

/// process overlap detection
/**
 * if beforeAd == true preload frames before stop mark in histogram buffer array, otherwise preload frames after start mark <br>
 * if we got frameCount, start compare
 * @param[in,out] ptr_OverlapPos new stop and start mark pair after overlap detection, -1 if no overlap was found
 * @param[in]     frameNumber    current frame number
 * @param[in]     frameCount     number of frames to process
 * @param[in]     beforeAd       true if called with a frame before advertising, false otherwise
 * @param[in]     h264           true if HD video, false otherwise
 */
        void Process(sOverlapPos *ptr_OverlapPos, const int frameNumber, const int frameCount, const bool beforeAd, const bool h264);

    private:

        typedef int simpleHistogram[256];     //!< histogram array
                                              //!<

/**
 * check if two histogram are similar
 * @param hist1 histogram 1
 * @param hist2 histogram 2
 * @return different pixels if similar, <0 otherwise
 */
        int AreSimilar(const simpleHistogram &hist1, const simpleHistogram &hist2);

/**
 * get a simple histogram of current frame
 * @param[in,out] dest histogram
 */
        void GetHistogram(simpleHistogram &dest);

/**
 * detect overlaps before and after advertising
 * @param[in,out] ptr_OverlapPos new stop and start mark pair after overlap detection, -1 if no overlap was found
 */
        void Detect(sOverlapPos *ptr_OverlapPos);

/**
 * reset state of overlap detection
 */
        void Clear();

/**
 * histogram buffer for overlap detection
 */
        typedef struct sHistBuffer {
            int frameNumber = -1;      //!< frame number
                                       //!<
            bool valid      = false;   //!< true if buffer is valid
                                       //!<
            simpleHistogram histogram; //!< simple frame histogram
                                       //!<
        } sHistBuffer;

        sMarkAdContext *maContext = NULL;  //!< markad context

        sHistBuffer *histbuf[2];           //!< simple frame histogram with frame number
                                           //!<
        int histcnt[2];                    //!< count of prcessed frame histograms
                                           //!<
        int histframes[2];                 //!< frame number of histogram buffer content
                                           //!<
        int lastFrameNumber;               //!< last processed frame number
                                           //!<
        int similarCutOff;                 //!< maximum different pixel to treat picture as similar, depends on resulution
                                           //!<
        int similarMinLength;              //!< minimum similar frames for a overlap
                                           //!<
};


/**
 * class to detect logo in recording
 */
class cMarkAdLogo : cLogoSize {
    public:

/**
 * class to detect logo
 * @param maContext      markad context
 * @param recordingIndex recording index
 */
        explicit cMarkAdLogo(sMarkAdContext *maContext, cMarkCriteria *markCriteriaParam, cIndex *recordingIndex);

        ~cMarkAdLogo();

/**
 * detect logo status
 * @param[in]  frameBefore     frame number before
 * @param[in]  frameCurrent    current frame number
 * @param[out] logoFrameNumber frame number of logo change
 * @return 1 = logo, 0 = unknown, -1 = no logo
 */
        int Detect(const int frameBefore, const int frameCurrent, int *logoFrameNumber); // return: 1 = logo, 0 = unknown, -1 = no logo

/**
 * process logo detection of current frame
 * @param[in]  iFrameBefore    i-frame before last i-frame
 * @param[in]  iFrameCurrent   last i-frame
 * @param[in]  frameCurrent    current frame number
 * @param[out] logoFrameNumber frame number of detected logo state change
 * @return #eLogoStatus
 */
        int Process(const int iFrameBefore, const int iFrameCurrent, const int frameCurrent, int *logoFrameNumber);

/**
 * get logo detection status of area
 * @return #eLogoStatus
 */
        int Status() {
            return area.status;
        }

/**
 * set logo dection status of area to LOGO_INVISIBLE
 */
        void SetStatusLogoInvisible() {
            if (area.status == LOGO_VISIBLE) area.status = LOGO_INVISIBLE;
        }

/**
 * set logo dection status of area to LOGO_UNINITIALIZED
 */
        void SetStatusUninitialized() {
            area.status = LOGO_UNINITIALIZED;
        }

/**
 * clear status and free memory
 * @param isRestart   true if called from full video detection (blackscreen, logo, border) restart at pass 1, false otherwise
 */
        void Clear(const bool isRestart = false);


/**
 * get pointer to logo area
 * @return pointer to logo area
 */
        sAreaT *GetArea();

    private:

/**
 * enumeration for ReduceBrightness function return codes
 */
        enum eBrightness {
            BRIGHTNESS_CHANGED       =  1,
            BRIGHTNESS_VALID         =  0,           // picture is valid, no reduction necessary
            BRIGHTNESS_SEPARATOR     = -1,
            BRIGHTNESS_ERROR         = -2,
            BRIGHTNESS_UNINITIALIZED = -3
        };

/**
 * calculate coordinates of logo position (values for array index, from 0 to (Video.Info.width - 1) or (Video.Info.height)
 * @param[out] xstart x position of logo start
 * @param[out] xend   x position of logo end
 * @param[out] ystart y position of logo start
 * @param[out] yend   y position of logo end
 * @param[in]  plane  plane number
 * @return true if sucessful, false otherwise
 */
        bool SetCoordinates(int *xstart, int *xend, int *ystart, int *yend, const int plane);

/**
 * reduce brightness of logo corner
 * @param[in]  frameNumber     frame number, only used to debug
 * @param[out] contrastReduced logo area brightness after reduction if sucessful, otherwise unchanged
 * @return return code #eBrightness value
 */
        int ReduceBrightness(const int frameNumber, int *contrastReduced);

/**
 * sobel transform one plane of the picture
 * @param plane plane number
 * @param boundary count pixel of outer frame to ignore in sobel transformation, need for logo extraction to avoid corner lines
 * @return true if successful, false otherwise
 */
        bool SobelPlane(const int plane, int boundary);

/**
 * load logo from file in directory
 * @param directory source directory
 * @param file file name
 * @param plane plane number
 * @return 0 on success, -1 file not found, -2 format error in logo file
 */
        int Load(const char *directory, const char *file, const int plane);

/**
 * save the area.corner picture after sobel transformation to /tmp
 * @param frameNumber frame number
 * @param picture picture to save
 * @param plane number
 * @param debug = NULL: save was called by --extract function, != NULL: save was called by debug statements, add debug identifier to filename
 * return: true if successful, false otherwise
 */
        bool Save(const int frameNumber, uchar **picture, const short int plane, const char *debug);

/**
 * save the original corner picture /tmp and add debug identifier to filename
 * @param frameNumber frame number
 * @param debug debug identifier
 * @return: true if successful, false otherwise
 */
        void SaveFrameCorner(const int frameNumber, const int debug);

/**
 * copy all black pixels from logo pane 0 into plan 1 and plane 2,
 * we need this for channels with usually grey logos, but at start and end they can be red (DMAX)
 */
        void LogoGreyToColour();

        enum {
            TOP_LEFT,
            TOP_RIGHT,
            BOTTOM_LEFT,
            BOTTOM_RIGHT
        };

        cIndex *recordingIndexMarkAdLogo = NULL;  //!< recording index
                                                  //!<
        int logoHeight                   = 0;     //!< logo height
                                                  //!<
        int logoWidth                    = 0;     //!< logo width
                                                  //!<
        sAreaT area;                              //!< pixels of logo area
                                                  //!<
        int GX[3][3];                             //!< GX Sobel mask
                                                  //!<
        int GY[3][3];                             //!< GY Sobel mask
                                                  //!<
        sMarkAdContext *maContext        = NULL;  //!< markad context
                                                  //!<
        cMarkCriteria *markCriteria      = NULL;  //!< pointer to class with decoding states
                                                  //!<
        bool pixfmt_info                 = false; //!< true if unknown pixel error message was logged, false otherwise
                                                  //!<
        bool isInitColourChange          = false; //!< true if trnasformation of grey logo to coloured logo is done
                                                  //!<
        int logo_xstart                  = 0;     //!< x start coordinate of the visible part of the logo
                                                  //!<
        int logo_xend                    = 0;     //!< x end coordinate of the visible part of the logo
                                                  //!<
        int logo_ystart                  = 0;     //!< y start coordinate of the visible part of the logo
                                                  //!<
        int logo_yend                    = 0;     //!< y end coordinate of the visible part of the logo
                                                  //!<
};

/**
 * class to detect scene change
 */
class cMarkAdSceneChange {
    public:

/**
 * class to detect scene change
 * @param maContextParam markad context
 */
        explicit cMarkAdSceneChange(sMarkAdContext *maContextParam);
        ~cMarkAdSceneChange();

/**
 * process scene change detection
 * @param frameCurrent current frame number
 * @return scene change status: <br>
 *         -1 scene stop <br>
 *          0 no status change <br>
 *          1 scene start
 */
        int Process(const int currentFrameNumber);

    private:
        sMarkAdContext *maContext = NULL;  //!< markad context
                                           //!<
        int prevFrameNumber       = 0;     //!< previous frame number
                                           //!<
        int *prevHistogram        = NULL;  //!< histogram of previos frame
                                           //!<
};


/**
 * class to detect black screen
 */
class cMarkAdBlackScreen {
    public:

/**
 * class to detect black screen
 * @param maContextParam markad context
 */
        explicit cMarkAdBlackScreen(sMarkAdContext *maContextParam);

/**
 * process black screen detection
 * @param frameCurrent current frame number
 * @return black screen status: <br>
 *         -1 blackscreen start (notice: this is a STOP mark) <br>
 *          0 no status change <br>
 *          1 blackscreen end (notice: this is a START mark)
 */
        int Process(const int frameCurrent);

/**
 * clear blackscreen detection status
 */
        void Clear();

    private:
        int blackScreenstatus;             //!< status of black screen detection
                                           //!<
        sMarkAdContext *maContext = NULL;  //!< markad context
                                           //!<
};


/**
 * cladd to detect horizental border
 */
class cMarkAdBlackBordersHoriz {
    public:

/**
 * constructor of class to detect horizental border
 * @param maContextParam markad context
 */
        explicit cMarkAdBlackBordersHoriz(sMarkAdContext *maContextParam);

/**
 * get first frame number with border
 * @return first frame number with border
 */
        int GetFirstBorderFrame();

/**
 * process horizontal border detection of current frame
 * @param frameNumber current frame number
 * @param borderFrame frame number of detected border
 * @return border detection status
 */
        int Process(const int frameNumber, int *borderFrame);

/**
 * set horizontal border status to VBORDER_INVISIBLE
 */
        void SetStatusBorderInvisible() {
            borderstatus = HBORDER_INVISIBLE;
            borderframenumber = -1;
        }

/**
 * clear horizontal border detection status
 */
        void Clear(const bool isRestart = false);

    private:
        int borderstatus;                 //!< status of horizontal border detection
                                          //!<
        int borderframenumber;            //!< frame number of detected horizontal border
                                          //!<
        sMarkAdContext *maContext = NULL; //!< markad context
                                          //!<
};


/**
 * class to detect vertical border
 */
class cMarkAdBlackBordersVert {
    public:

/**
 * constructor of class to detect vertical border
 * @param maContextParam markad context
 */
        explicit cMarkAdBlackBordersVert(sMarkAdContext *maContextParam);

/**
 * get first frame number with border
 * @return first frame number with border
 */
        int GetFirstBorderFrame();

/**
 * process vertical border detection of current frame
 * @param frameNumber current frame number
 * @param borderFrame frame number of detected border
 * @return border detection status
 */
        int Process(int frameNumber, int *borderFrame);

/**
 * set vertical border status to VBORDER_INVISIBLE
 */
        void SetStatusBorderInvisible() {
            borderstatus = VBORDER_INVISIBLE;
            borderframenumber = -1;
        }

/**
 * clear vertical border detection status
 */
        void Clear(const bool isRestart = false);

    private:
        int borderstatus;                    //!< status of vertical border detection
                                             //!<
        int borderframenumber     = -1;      //!< frame number of detected vertical border
                                             //!<
        int darkFrameNumber       = INT_MAX; //!< first vborder frame, but need to check, because of dark picture
                                             //!<
        sMarkAdContext *maContext = NULL;    //!< markad context
                                             //!<
#ifdef DEBUG_VBORDER
        int minBrightness         = INT_MAX;
        int maxBrightness         = 0;
#endif
};


/**
 * check packet for video based marks
 */
class cMarkAdVideo {
    public:

/**
 * constructor of class to check packet for video based marks
 * @param maContext      markad context
 * @param recordingIndex recording index
 */
        explicit cMarkAdVideo(sMarkAdContext *maContext, cMarkCriteria *markCriteriaParam, cIndex *recordingIndex);
        ~cMarkAdVideo();

/**
 * copy constructor, not used, only for formal reason
 */
        cMarkAdVideo(const cMarkAdVideo &origin) {
            maContext = origin.maContext;
            blackScreen = NULL;
            hborder = NULL;
            vborder = NULL;
            logo = NULL;
//            overlap = NULL;
        };

/**
 * operator=, not used, only for formal reason
 */
        cMarkAdVideo &operator =(const cMarkAdVideo *origin) {
            maContext                 = origin->maContext;
	    markCriteria              = origin->markCriteria;
            sceneChange               = NULL;
            blackScreen               = NULL;
            hborder                   = NULL;
            vborder                   = NULL;
            logo                      = NULL;
            aspectRatio               = {};
            recordingIndexMarkAdVideo = NULL;
            marks                     = {};
            return *this;
        }

/**
 * detect video packet based marks
 * @param iFrameBefore  number of i-frame before last i-frame frame
 * @param iFrameCurrent number of last i-frame
 * @param frameCurrent  current frame number
 * @return array of detected marks from this video packet
 */
        sMarkAdMarks *Process(int iFrameBefore, const int iFrameCurrent, const int frameCurrent);

/**
 * reduce logo detection to plane 0
 * @return true if we found a valid plane >= 1 to switch off
 */
        bool ReducePlanes(void);

/**
 * reset all video based detection of marks
 * @param isRestart   ture if called after restart of full video detection (blackscreen, logo, border) at start of the end part of the recording, false otherwise
 */
        void Clear(const bool isRestart = false);

/**
 * clear state of border detection
 */
        void ClearBorder();

    private:

/**
 * reset array of new marks
 */
        void ResetMarks();

/**
 * add a new mark to array of new marks
 * @param type     type of the mark
 * @param position frame number
 * @param before   video aspect ratio before mark position
 * @param after    video aspect ratio after mark position
 * @return true if free position in new mark array found, false otherwise
 */
        bool AddMark(int type, int position, const sAspectRatio *before = NULL, const sAspectRatio *after = NULL);

/**
 * check if video aspect ratio changes between the two aspect ratios
 * @param[in]  AspectRatioA first video aspact ratio
 * @param[in]  AspectRatioB second video aspect ratio
 * @param[out] start true if aspect ratio change to 4:3 at video decoding start, from 0:0 (unknown) to 4:3
 * @return true if aspect ratio changed, false otherwise
 */
        bool AspectRatioChange(const sAspectRatio &AspectRatioA, const sAspectRatio &AspectRatioB, bool &start);

        sMarkAdContext *maContext         = NULL; //!< markad context
                                                  //!<
        cMarkCriteria *markCriteria       = NULL; //!< poiter to class for marks and decoding criterias
                                                  //!<
        cIndex *recordingIndexMarkAdVideo = NULL; //!< recording index
                                                  //!<
        sMarkAdMarks marks                = {};   //!< array of marks to add to list
                                                  //!<
        sAspectRatio aspectRatio          = {};   //!< video display aspect ratio (DAR)
                                                  //!<
        cMarkAdSceneChange *sceneChange   = NULL; //!< pointer to class cMarkAdsceneChange
                                                  //!<
        cMarkAdBlackScreen *blackScreen   = NULL; //!< pointer to class cMarkAdBlackScreen
                                                  //!<
        cMarkAdBlackBordersHoriz *hborder = NULL; //!< pointer to class cMarkAdBlackBordersHoriz
                                                  //!<
        cMarkAdBlackBordersVert *vborder  = NULL; //!< pointer to class cMarkAdBlackBordersVert
                                                  //!<
        cMarkAdLogo *logo                 = NULL; //!< pointer to class cMarkAdLogo
                                                  //!<
};
#endif
