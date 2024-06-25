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
#include "debug.h"
#include "decoder.h"
#include "criteria.h"
#include "tools.h"
#include "sobel.h"

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
 * class to detect logo in recording
 */
class cLogoDetect : private cTools {
public:

    /**
     * class to detect logo
     * @param criteriaParam         detection critaria
     * @param recordingIndex        recording index
     */
    explicit cLogoDetect(cDecoder *decoderParam, cCriteria *criteriaParam, const char *recDirParam, const char *logoCacheDirParam);

    ~cLogoDetect();

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
    int Process(int *logoFrameNumber);


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
     * reduce brightness of logo corner
     * @param  frameNumber  frame number, only used to debug
     * @param  logo_vmark   count of pixel matches to accept logo visible
     * @param  logo_imark   count of pixel matches to accept logo invisible
     * @return true if we got a valid result, fasle otherwise
     */
    bool ReduceBrightness(const int logo_vmark, const int logo_imark);

    bool LoadLogo();

    /**
     * load logo from file in directory
     * @param directory source directory
     * @param file name of file
     * @param plane number of plane
     * @return 0 on success, -1 file not found, -2 format error in logo file
     */
    bool LoadLogoPlane(const char *path, const char *logoName, const int plane);

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

    cDecoder *decoder              = nullptr;  //!< decoder
    //!<
    cCriteria *criteria               = nullptr;  //!< decoding states and criteria
    //!<
    const char *logoCacheDir          = nullptr;  //!< logo cache directory
    //!<
    const char *recDir                = nullptr;  //!< recording directory
    //!<
    cSobel *sobel                     = nullptr;  //!< sobel transformation
    //!<
    sAreaT area                       = {};       //!< pixels of logo area
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
    const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" }; //!< array to transform enum corner to text
    //!<
};

/**
 * class to detect scene change
 */
class cSceneChangeDetect {
public:
    /**
     * class to detect scene change
     */
    cSceneChangeDetect(cDecoder *decoderParam, cCriteria *criteriaParam);
    ~cSceneChangeDetect();

    /**
     * process scene change detection
     * @param[in]   currentFrameNumber current frame number
     * @param[out]  changeFrameNumber  frame number of scene change
     * @return      scene change status: <br>
     *         -1 scene stop <br>
     *          0 no status change <br>
     *          1 scene start
     */
    int Process(int *changeFrameNumber);

private:
    cDecoder *decoder = nullptr;               //!< pointer to decoder
    //!<
    cCriteria *criteria  = nullptr;               //!< analyse criteria
    //!<
    int prevFrameNumber  = -1;                    //!< previous frame number
    //!<
    int *prevHistogram   = nullptr;               //!< histogram of previous frame
    //!<
    int sceneStatus      = SCENE_UNINITIALIZED;   //!< status of scene change
    //!<
    int blendFrame       = -1;                    //!< frames number of first frame over blend limit
    //!<
    int blendCount       = 0;                     //!< number of frames over blend limit
    //!<
};


/**
 * class to detect black screen
 */
class cBlackScreenDetect {
public:

    /**
     * class to detect black screen
     */
    explicit cBlackScreenDetect(cDecoder *decoderParam, cCriteria *criteriaParam);

    /**
     * process black screen detection
     * @param frameCurrent current frame number
     * @return black screen status: <br>
     *         -1 blackscreen start (notice: this is a STOP mark) <br>
     *          0 no status change <br>
     *          1 blackscreen end (notice: this is a START mark)
     */
    int Process();

    /**
     * clear blackscreen detection status
     */
    void Clear();

private:
    cDecoder *decoder  = nullptr;                   //!< pointer to decoder
    //!<
    cCriteria *criteria   = nullptr;                   //!< pointer to class for marks and decoding criteria
    //!<
    int blackScreenStatus = BLACKSCREEN_UNINITIALIZED; //!< status of black screen detection
    //!<
    int lowerBorderStatus = BLACKSCREEN_UNINITIALIZED; //!< status of lower part black screen detection
    //!<
};


/**
 * cladd to detect horizental border
 */
class cHorizBorderDetect {
public:

    /**
     * constructor of class to detect horizental border
     * @param channelNameParam  channel name
     * @param frameRateParam    frame rate
     * @param criteriaParam     detection criteria
     */
    explicit cHorizBorderDetect(cDecoder *decoderParam, cCriteria *criteriaParam);
    ~cHorizBorderDetect();

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * process horizontal border detection of current frame
     * @param  picture     video picture to analyse
     * @param  borderFrame frame number of detected border
     * @return             border detection status
     */
    int Process(int *borderFrame);

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
    cDecoder *decoder      = nullptr;   //!< pointer to decoder
    //!<
    cCriteria *criteria       = nullptr;   //!< pointer to class with decoding states and criteria
    //!<
    bool logoInBorder         = false;     //!< true if channel has logo in border
    //!<
    bool infoInBorder         = false;     //!< true if channel has info banner in border
    //!<
    int frameRate             = 0;         //!< frame rate
    //!<
    int borderstatus;                      //!< status of horizontal border detection
    //!<
    int borderframenumber;                 //!< frame number of detected horizontal border
    //!<
};


/**
 * class to detect vertical border
 */
class cVertBorderDetect {
public:

    /**
     * constructor of class to detect vertical border
     * @param channelNameParam channel name
     * @param frameRateParam   video frame rate
     * @param criteriaParam    detection criteria
     */
    explicit cVertBorderDetect(cDecoder *decoderParam, cCriteria *criteriaParam);

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * process vertical border detection of current frame
     * @param borderFrame frame number of detected border
     * @return border detection status
     */
    int Process(int *borderFrame);


    /**
     * clear vertical border detection status
     */
    void Clear(const bool isRestart = false);

private:
    cDecoder *decoder      = nullptr;   //!< pointer to decoder
    //!<
    cCriteria *criteria       = nullptr;   //!< pointer to class with decoding states and criteria
    //!<
    bool logoInBorder         = false;     //!< true if channel has logo in border
    //!<
    bool infoInBorder         = false;     //!< true if channel has info banner in border
    //!<
    int frameRate             = 0;         //!< frame rate of video
    //!<
    int borderstatus;                      //!< status of vertical border detection
    //!<
    int borderframenumber     = -1;        //!< frame number of detected vertical border
    //!<
    int darkFrameNumber       = INT_MAX;   //!< first vborder frame, but need to check, because of dark picture
    //!<
#ifdef DEBUG_VBORDER
    int minBrightness         = INT_MAX;
    int maxBrightness         = 0;
#endif
};


/**
 * check packet for video based marks
 */
class cVideo {
public:

    /**
     * constructor of class to check packet for video based marks
     * @param criteriaParam        detection criteria
     * @param recordingIndex       recording index
     */
    explicit cVideo(cDecoder *decoderParam, cCriteria *criteriaParam, const char *recDirParam, const char *logoCacheDirParam);
    ~cVideo();

    /**
     * copy constructor, not used, only for formal reason
     */
    cVideo(const cVideo &origin) {
        sceneChangeDetect         = nullptr;
        blackScreenDetect         = nullptr;
        hBorderDetect             = nullptr;
        vBorderDetect             = nullptr;
        logoDetect                = nullptr;
        criteria                  = nullptr;
        aspectRatioBeforeFrame    = 0;
    };

    /**
     * operator=, not used, only for formal reason
     */
    cVideo &operator =(const cVideo *origin) {
        criteria                  = origin->criteria;
        sceneChangeDetect         = nullptr;
        blackScreenDetect         = nullptr;
        hBorderDetect             = nullptr;
        vBorderDetect             = nullptr;
        logoDetect                = nullptr;
        aspectRatioBeforeFrame    = 0;
        videoMarks                = {};
        return *this;
    }

    /**
     * detect video packet based marks
     * @return array of detected marks from this video packet
     */
    sMarkAdMarks *Process();

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

    /**
     * set ascpect ratio of broadcast
     */
    void SetAspectRatioBroadcast(sAspectRatio aspectRatio);

private:

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
    static bool AspectRatioChange(const sAspectRatio AspectRatioOld, const sAspectRatio *AspectRatioNew);

    cDecoder *decoder                  = nullptr;  //!< pointer to decoder
    //!<
    cCriteria *criteria                   = nullptr;  //!< pointer to class for marks and decoding criteria
    //!<
    const char *recDir                    = nullptr;  //!< recording directory
    //!<
    const char *logoCacheDir              = nullptr;  //!< logo cache directory
    //!<
    sMarkAdMarks videoMarks               = {};       //!< array of marks to add to list
    //!<
    sAspectRatio aspectRatioFrameBefore   = {0};      //!< video display aspect ratio (DAR) of frame before
    //!<
    sAspectRatio aspectRatioBroadcast     = {0};      //!< broadcast display aspect ratio (DAR)
    //!<
    cSceneChangeDetect *sceneChangeDetect = nullptr;  //!< pointer to class cMarkAdsceneChange
    //!<
    cBlackScreenDetect *blackScreenDetect = nullptr;  //!< pointer to class cBlackScreenDetect
    //!<
    cHorizBorderDetect *hBorderDetect     = nullptr;  //!< pointer to class cHorizBorderDetect
    //!<
    cVertBorderDetect *vBorderDetect      = nullptr;  //!< pointer to class cVertBorderDetect
    //!<
    cLogoDetect *logoDetect               = nullptr;  //!< pointer to class cLogoDetect
    //!<
    int aspectRatioBeforeFrame            = 0;        //!< last frame number before aspect ratio change, needed for stop mark
    //!<
};
#endif
