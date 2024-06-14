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
#include "tools.h"

#define LOGO_VMAXCOUNT 3       //!< count of IFrames for detection of "logo visible"
//!<
#define LOGO_IMAXCOUNT 4       //!< count of IFrames for detection of "logo invisible", reduced from 5 to 4
//!<
#define LOGO_VMARK 0.5         //!< percantage of pixels for visible
//!<
#define LOGO_IMARK 0.18        //!< percentage of pixels for invisible changed from 0,15 to 0,18
//!<

#define MIN_H_BORDER_SECS 81  //!< minimum length of horizontal border
//!<
#define MIN_V_BORDER_SECS 82  //!< minimum length of vertical border before it is accepted <br>
//!< shortest valid vborder part in broadcast found with length 82s
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
    SCENE_ERROR          = -3,
    SCENE_UNINITIALIZED  = -2,
    SCENE_STOP           = -1,
    SCENE_NOCHANGE       =  0,
    SCENE_BLEND          =  1,    // scene blend is active
    SCENE_START          =  2
};

enum {
    BLACKSCREEN_UNINITIALIZED = -3,
    BLACKLOWER_INVISIBLE      = -2,
    BLACKSCREEN_INVISIBLE     = -1,
    BLACKSCREEN_NOCHANGE      =  0,
    BLACKSCREEN_VISIBLE       =  1,
    BLACKLOWER_VISIBLE        =  2
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
    uchar **sobel      = nullptr;              //!< monochrome picture from edge after sobel transformation, memory will be allocated after we know video resolution
    //!<

    uchar **mask       = nullptr;              //!< monochrome mask of logo, memory will be allocated after we know video resolution
    //!<

    uchar **result     = nullptr;              //!< result of sobel + mask, memory will be allocated after we know video resolution
    //!<

    uchar **inverse    = nullptr;              //!< inverse result of sobel + mask, memory will be allocated after we know video resolution
    //!<

    int mPixel[PLANES] = {0};                  //!< black pixel in mask
    //!<

    int rPixel[PLANES] = {0};                  //!< black pixel in result
    //!<

    int iPixel[PLANES] = {0};                  //!< black pixel in inverse result
    //!<

    int status         = LOGO_UNINITIALIZED;   //!< logo status: on, off, uninitialized
    //!<

    int frameNumber    = -1;                   //!< start/stop frame number
    //!<

    int counter;                               //!< how many logo on, offs detected
    //!<

    int corner;                                //!< corner of logo
    //!<

    int intensity;                             //!< area intensity (higher -> brighter)
    //!<

    sAspectRatio AspectRatio;                  //!< aspect ratio of the video
    //!<

    bool valid[PLANES];                        //!< <b>true:</b> logo mask data (logo) are valid <br>
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
     * @return default logo width and height in pixel
     */
    sLogoSize GetDefaultLogoSize(const int width);

    /**
     * calculatate maximum logo size dependent on video resolution
     * @param width video width in pixel
     * @return maximum valid logo width and height in pixel
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
 * class to detect logo in recording
 */
class cMarkAdLogo : private cLogoSize, cTools {
public:

    /**
     * class to detect logo
     * @param maContextParam        markad context
     * @param criteriaParam         detection critaria
     * @param recordingIndex        recording index
     */
    explicit cMarkAdLogo(sMarkAdContext *maContextParam, cCriteria *criteriaParam, cIndex *recordingIndex);

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
     * calculate coordinates of logo position (values for array index, from 0 to (Video.Info.width - 1) or (Video.Info.height)
     * @param[out] xstart x position of logo start
     * @param[out] xend   x position of logo end
     * @param[out] ystart y position of logo start
     * @param[out] yend   y position of logo end
     * @param[in]  plane  number of plane
     * @return true if successful, false otherwise
     */
    bool SetCoordinates(int *xstart, int *xend, int *ystart, int *yend, const int plane) const;

    /**
     * reduce brightness of logo corner
     * @param  frameNumber  frame number, only used to debug
     * @param  logo_vmark   count of pixel matches to accept logo visible
     * @param  logo_imark   count of pixel matches to accept logo invisible
     * @return true if we got a valid result, fasle otherwise
     */
    bool ReduceBrightness(const int frameNumber, const int logo_vmark, const int logo_imark);

    /**
     * sobel transform one plane of the picture
     * @param plane number of plane
     * @param boundary count pixel of outer frame to ignore in sobel transformation, need for logo extraction to avoid corner lines
     * @return true if successful, false otherwise
     */
    bool SobelPlane(const int plane, int boundary);

    /**
     * load logo from file in directory
     * @param directory source directory
     * @param file name of file
     * @param plane number of plane
     * @return 0 on success, -1 file not found, -2 format error in logo file
     */
    int Load(const char *directory, const char *file, const int plane);

    /**
     * save the area.corner picture after sobel transformation to /tmp
     * @param frameNumber frame number
     * @param picture save picture
     * @param plane number
     * @param debug = nullptr: save was called by --extract function, != nullptr: save was called by debug statements, add debug identifier to filename
     * return: true if successful, false otherwise
     */
    bool Save(const int frameNumber, uchar **picture, const short int plane, const char *debug);

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

    cIndex *recordingIndexMarkAdLogo  = nullptr;  //!< recording index
    //!<
    int logoHeight                    = 0;        //!< logo height
    //!<
    int logoWidth                     = 0;        //!< logo width
    //!<
    sAreaT area;                                  //!< pixels of logo area
    //!<
    int GX[3][3];                                 //!< GX Sobel mask
    //!<
    int GY[3][3];                                 //!< GY Sobel mask
    //!<
    sMarkAdContext *maContext         = nullptr;  //!< markad context
    //!<
    cCriteria *criteria               = nullptr;  //!< pointer to class with decoding states and criteria
    //!<
    bool pixfmt_info                  = false;    //!< true if unknown pixel error message was logged, false otherwise
    //!<
    bool isInitColourChange           = false;    //!< true if trnasformation of grey logo to coloured logo is done
    //!<
    int logo_xstart                   = -1;       //!< x start coordinate of the visible part of the logo
    //!<
    int logo_xend                     = -1;       //!< x end coordinate of the visible part of the logo
    //!<
    int logo_ystart                   = -1;       //!< y start coordinate of the visible part of the logo
    //!<
    int logo_yend                     = -1;       //!< y end coordinate of the visible part of the logo
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
     * @param[in]   currentFrameNumber current frame number
     * @param[out]  changeFrameNumber  frame number of scene change
     * @return      scene change status: <br>
     *         -1 scene stop <br>
     *          0 no status change <br>
     *          1 scene start
     */
    int Process(const int currentFrameNumber, int *changeFrameNumber);

private:
    sMarkAdContext *maContext = nullptr;                  //!< markad context
    //!<
    int prevFrameNumber       = -1;                    //!< previous frame number
    //!<
    int *prevHistogram        = nullptr;                  //!< histogram of previous frame
    //!<
    int sceneStatus           = SCENE_UNINITIALIZED;   //!< status of scene change
    //!<
    int blendFrame            = -1;                    //!< frames number of first frame over blend limit
    //!<
    int blendCount            = 0;                     //!< number of frames over blend limit
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
    int blackScreenStatus = BLACKSCREEN_UNINITIALIZED; //!< status of black screen detection
    //!<
    int lowerBorderStatus = BLACKSCREEN_UNINITIALIZED; //!< status of lower part black screen detection
    //!<
    sMarkAdContext *maContext = nullptr;                  //!< markad context
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
     * @param criteriaParam  detection criteria
     */
    explicit cMarkAdBlackBordersHoriz(sMarkAdContext *maContextParam, cCriteria *criteriaParam);

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * process horizontal border detection of current frame
     * @param  FrameNumber current frame number
     * @param  borderFrame frame number of detected border
     * @return border detection status
     */
    int Process(const int FrameNumber, int *borderFrame);

    /**
     * get horizontal border detection status
     * @return border detection status
     */
    int State() const;

    /**
     * clear horizontal border detection status
     */
    void Clear(const bool isRestart = false);

private:
    int borderstatus;                 //!< status of horizontal border detection
    //!<
    int borderframenumber;            //!< frame number of detected horizontal border
    //!<
    sMarkAdContext *maContext = nullptr; //!< markad context
    //!<
    cCriteria *criteria       = nullptr; //!< pointer to class with decoding states and criteria
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
     * @param criteriaParam  detection criteria
     */
    explicit cMarkAdBlackBordersVert(sMarkAdContext *maContextParam, cCriteria *criteriaParam);

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * process vertical border detection of current frame
     * @param frameNumber current frame number
     * @param borderFrame frame number of detected border
     * @return border detection status
     */
    int Process(int frameNumber, int *borderFrame);


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
    sMarkAdContext *maContext = nullptr;    //!< markad context
    //!<
    cCriteria *criteria       = nullptr;    //!< pointer to class with decoding states and criteria
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
     * @param maContextParam       markad context
     * @param criteriaParam        detection criteria
     * @param recordingIndex       recording index
     */
    explicit cMarkAdVideo(sMarkAdContext *maContextParam, cCriteria *criteriaParam, cIndex *recordingIndex);
    ~cMarkAdVideo();

    /**
     * copy constructor, not used, only for formal reason
     */
    cMarkAdVideo(const cMarkAdVideo &origin) {
        maContext                 = origin.maContext;
        blackScreen               = nullptr;
        hborder                   = nullptr;
        vborder                   = nullptr;
        logo                      = nullptr;
        criteria                  = nullptr;
        recordingIndexMarkAdVideo = nullptr;
        sceneChange               = nullptr;
        aspectRatioBeforeFrame    = 0;
    };

    /**
     * operator=, not used, only for formal reason
     */
    cMarkAdVideo &operator =(const cMarkAdVideo *origin) {
        maContext                 = origin->maContext;
        criteria                  = origin->criteria;
        sceneChange               = nullptr;
        blackScreen               = nullptr;
        hborder                   = nullptr;
        vborder                   = nullptr;
        logo                      = nullptr;
        aspectRatio               = {};
        aspectRatioBeforeFrame    = 0;
        recordingIndexMarkAdVideo = nullptr;
        videoMarks                = {};
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
     * @param type     mark type
     * @param position frame number
     * @param before   video aspect ratio before mark position
     * @param after    video aspect ratio after mark position
     * @return true if free position in new mark array found, false otherwise
     */
    bool AddMark(int type, int position, const sAspectRatio *before = nullptr, const sAspectRatio *after = nullptr);

    /**
     * check if video aspect ratio changes between the two aspect ratios
     * @param[in]  AspectRatioA first video aspact ratio
     * @param[in]  AspectRatioB second video aspect ratio
     * @return true if aspect ratio changed, false otherwise
     */
    static bool AspectRatioChange(const sAspectRatio &AspectRatioA, const sAspectRatio &AspectRatioB);

    sMarkAdContext *maContext         = nullptr; //!< markad context
    //!<
    cCriteria *criteria               = nullptr; //!< pointer to class for marks and decoding criteria
    //!<
    cIndex *recordingIndexMarkAdVideo = nullptr; //!< recording index
    //!<
    sMarkAdMarks videoMarks           = {};   //!< array of marks to add to list
    //!<
    sAspectRatio aspectRatio;                 //!< video display aspect ratio (DAR)
    //!<
    cMarkAdSceneChange *sceneChange   = nullptr; //!< pointer to class cMarkAdsceneChange
    //!<
    cMarkAdBlackScreen *blackScreen   = nullptr; //!< pointer to class cMarkAdBlackScreen
    //!<
    cMarkAdBlackBordersHoriz *hborder = nullptr; //!< pointer to class cMarkAdBlackBordersHoriz
    //!<
    cMarkAdBlackBordersVert *vborder  = nullptr; //!< pointer to class cMarkAdBlackBordersVert
    //!<
    cMarkAdLogo *logo                 = nullptr; //!< pointer to class cMarkAdLogo
    //!<
    int aspectRatioBeforeFrame        = 0;    //!< last frame number before aspect ratio change, needed for stop mark
    //!<
};
#endif
