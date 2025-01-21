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
#define LOGO_IMARK 0.19        //!< percentage of pixels for invisible changed from 0,18 to 0,19 to detect short logo interuption
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
    BLACKSCREEN_ERROR         = -4,
    BLACKSCREEN_UNINITIALIZED = -3,
    LOWER_BORDER_INVISIBLE    = -2,
    BLACKSCREEN_INVISIBLE     = -1,
    BLACKSCREEN_NOCHANGE      =  0,
    BLACKSCREEN_VISIBLE       =  1,
    LOWER_BORDER_VISIBLE      =  2
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
 * class for basic video tools
 */
class cVideoTools {
public:
    cVideoTools() {};
    ~cVideoTools() {};

    /**
     * get brightness of center of picture
     * @param picture video picture
     * @return avg pixel value of picture center
     */
    int GetPictureCenterBrightness(const sVideoPicture *picture);

private:
    /**
      * picture brightness info
      */
    struct sPictureBrightness {
        int packetNumber = -1 ;  //!< cached picture packet number
        //!<
        int brightness   = 0;    //!< avg pixel value of plane 0
        //!<
    } pictureBrightness;         //!< picture brightness info
    //!<
};


/**
 * class to detect logo in recording
 */
class cLogoDetect : private cTools {
public:

    /**
     * class to detect logo
     * @param decoderParam          decoder
     * @param indexParam            index
     * @param criteriaParam         detection critaria
     * @param autoLogoParam         mode of logo source
     * @param logoCacheDirParam     logo cache directory
     */
    explicit cLogoDetect(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam, const int autoLogoParam, const char *logoCacheDirParam);

    ~cLogoDetect();

    /**
     * copy constructor
     */
    cLogoDetect(const cLogoDetect &origin) {
        memcpy(aCorner, origin.aCorner, sizeof(origin.aCorner));
        logo_xstart        = origin.logo_xstart;
        logo_xend          = origin.logo_xend;
        logo_ystart        = origin.logo_ystart;
        logo_yend          = origin.logo_yend;
        isInitColourChange = origin.isInitColourChange;
        sobel              = nullptr;
        recDir             = origin.recDir;
        logoCorner         = origin.logoCorner;
        autoLogo           = origin.autoLogo;
        logoCacheDir       = origin.logoCacheDir;
        criteria           = origin.criteria;
        index              = origin.index;
        decoder            = origin.decoder;
        packetNumberBefore = origin.packetNumberBefore;
        framePTSBefore     = origin.framePTSBefore;
    }

    /**
     * operator=
     */
    cLogoDetect &operator =(const cLogoDetect *origin) {
        memcpy(aCorner, origin->aCorner, sizeof(origin->aCorner));
        logo_xstart        = origin->logo_xstart;
        logo_xend          = origin->logo_xend;
        logo_ystart        = origin->logo_ystart;
        logo_yend          = origin->logo_yend;
        isInitColourChange = origin->isInitColourChange;
        sobel              = origin->sobel;
        recDir             = origin->recDir;
        logoCorner         = origin->logoCorner;
        autoLogo           = origin->autoLogo;
        logoCacheDir       = origin->logoCacheDir;
        criteria           = origin->criteria;
        index              = origin->index;
        decoder            = origin->decoder;
        packetNumberBefore = origin->packetNumberBefore;
        framePTSBefore     = origin->framePTSBefore;
        return *this;
    }

    /**
     * get logo corner
     * @return index of logo corner
     */
    int GetLogoCorner() const;

    /**
     * detect logo status
     * @param[out] logoPacketNumber packet number of logo change
     * @param[out] logoFramePTS     frame PTS of logo change
     * @return 1 = logo, 0 = unknown, -1 = no logo
     */
    int Detect(int *logoPacketNumber, int64_t *logoFramePTS);

    /**
     * reduce used logo planes to plane 0
     */
    void ReducePlanes();

    /**
     * process logo detection of current frame
     * @param[out] logoPacketNumber packet number of detected logo state change
     * @param[out] logoFramePTS     frame PTS^ of detected logo state change
     * @return #eLogoStatus
     */
    int Process(int *logoPacketNumber, int64_t *logoFramePTS);

    /**
     * clear status and free memory
     * @param isRestart   true if called from full video detection (blackscreen, logo, border) restart at pass 1, false otherwise
     */
    void Clear(const bool isRestart = false);

private:
    /**
     * reduce brightness of logo corner
     * @param  logo_vmark   count of pixel matches to accept logo visible
     * @param  logo_imark   count of pixel matches to accept logo invisible
     * @return true if we got a valid result, fasle otherwise
     */
    bool ReduceBrightness(const int logo_vmark, int *logo_imark);

    /**
     * load logo from file in directory
     * @return true on success, false otherwise
     */
    bool LoadLogo();

    /**
     * load logo with new aspect ratio, try to extract from recording if not found
     * @return true on success, false otherwise
     */
    bool ChangeLogoAspectRatio(const sAspectRatio *aspectRatio);

    /**
     * load logo from file in directory
     * @param path source directory
     * @param logoName name of logo
     * @param plane number of plane
     * @return true on success, false otherwise
     */
    bool LoadLogoPlane(const char *path, const char *logoName, const int plane);

    /**
     * check if logo is visible in coloured plane if changel changed logo colour
     * return true if we found a visible logo in coloured plane, false otherwise
     */
    bool LogoColourChange(int *rPixel, const int logo_vmark);

    /**
     * copy all black pixels from logo pane 0 into plan 1 and plane 2,
     * we need this for channels with usually grey logos, but at start and end they can be red (e.g. DMAX)
     */
    void LogoGreyToColour();

    cDecoder *decoder                 = nullptr;  //!< decoder
    //!<
    cIndex *index                     = nullptr;  //!< index
    //!<
    cCriteria *criteria               = nullptr;  //!< decoding states and criteria
    //!<
    int autoLogo                      = -1;       //!< mode of logo source
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
    int logoCorner                    = -1;       //!< cached logo corner
    //!<
    int packetNumberBefore            = -1;       //!< packet number before
    //!<
    int64_t framePTSBefore            = -1;       //!< frame PTS before
    //!<
    const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" }; //!< array to transform enum corner to text
    //!<
};

/**
 * class to detect scene change
 */
class cSceneChangeDetect : private cTools {
public:
    /**
     * class to detect scene change
     */
    cSceneChangeDetect(cDecoder *decoderParam, cCriteria *criteriaParam);
    ~cSceneChangeDetect();

    /**
     * process scene change detection
     * @param[out]  changePacketNumber  packet number of scene change
     * @param[out]  changeFramePTS      decoded frame PTS of scene change
     * @return      scene change status: <br>
     *              -1 scene stop <br>
     *              0 no status change <br>
     *              1 scene start
     */
    int Process(int *changePacketNumber, int64_t *changeFramePTS);

private:
    cDecoder *decoder     = nullptr;               //!< pointer to decoder
    //!<
    cCriteria *criteria   = nullptr;               //!< analyse criteria
    //!<
    int prevPacketNumber  = -1;                    //!< previous packet number
    //!<
    int64_t prevFramePTS  = -1;                    //!< previous frame number
    //!<
    int *prevHistogram    = nullptr;               //!< histogram of previous frame
    //!<
    int sceneStatus       = SCENE_UNINITIALIZED;   //!< status of scene change
    //!<
    int blendPacketNumber = -1;                    //!< packet number of first frame over blend limit
    //!<
    int64_t blendFramePTS = -1;                    //!< frames PTS of first frame over blend limit
    //!<
    int blendCount        = 0;                     //!< number of frames over blend limit
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
class cHorizBorderDetect : private cVideoTools {
public:

    /**
     * constructor of class to detect horizental border
     * @param decoderParam      pointer to decoder
     * @param indexParam        pointer to index
     * @param criteriaParam     detection criteria
     */
    explicit cHorizBorderDetect(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam);
    ~cHorizBorderDetect();

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * @param  hBorderPacketNumber frame number of detected border
     * @param  hBorderFramePTS     frame PTS of detected border
     * @return                     border detection status
     */
    int Process(int *hBorderPacketNumber, int64_t *hBorderFramePTS);

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
    cDecoder *decoder            = nullptr;               //!< pointer to decoder
    //!<
    cIndex *index                = nullptr;               //!< pointer to index
    //!<
    cCriteria *criteria          = nullptr;               //!< pointer to class with decoding states and criteria
    //!<
    int brightnessSure           = INT_MAX;               //!< lower limit for hborder
    //!<
    int brightnessMaybe          = INT_MAX;               //!< upper limit for hborder
    //!<
    int frameRate                = 0;                     //!< frame rate
    //!<
    int borderstatus             = HBORDER_UNINITIALIZED; //!< status of horizontal border detection
    //!<
    int prevPacketNumber         = -1;                    //!< packet number of previous packet
    //!<
    int64_t prevFramePTS         = -1;                    //!< frame PTS of previous packet
    //!<
    int hBorderStartPacketNumber = -1;                    //!< packet number of detected horizontal border
    //!<
    int64_t hBorderStartFramePTS = -1;                    //!< frame PTS of detected horizontal border
    //!<
    bool valid                   = false;                 //!< true if we found hborder in bright picture
};


/**
 * class to detect vertical border
 */
class cVertBorderDetect : private cVideoTools {
public:

    /**
     * constructor of class to detect vertical border
     * @param decoderParam     pointer to decoder
     * @param indexParam       pointer to index
     * @param criteriaParam    detection criteria
     */
    explicit cVertBorderDetect(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam);

    /**
     * get first frame number with border
     * @return first frame number with border
     */
    int GetFirstBorderFrame() const;

    /**
     * process vertical border detection of current frame
     * @param vBorderPacketNumber packet number of detected border
     * @param vBorderFramePTS     frame PTS of detected border
     * @return                    border detection status
     */
    int Process(int *vBorderPacketNumber, int64_t *vBorderFramePTS);


    /**
     * clear vertical border detection status
     */
    void Clear(const bool isRestart = false);

private:
    cDecoder *decoder            = nullptr;                //!< pointer to decoder
    //!<
    cIndex *index                = nullptr;                //!< pointer to index
    //!<
    cCriteria *criteria          = nullptr;                //!< pointer to class with decoding states and criteria
    //!<
    bool logoInBorder            = false;                  //!< true if channel has logo in border
    //!<
    bool infoInBorder            = false;                  //!< true if channel has info banner in border
    //!<
    int frameRate                = 0;                      //!< frame rate of video
    //!<
    int borderstatus             = VBORDER_UNINITIALIZED;  //!< status of vertical border detection
    //!<
    int vBorderStartPacketNumber = -1;                     //!< packet number from start of detected vertical border
    //!<
    int64_t vBorderStartFramePTS = -1;                     //!< frame number from start of detected vertical border
    //!<
    bool valid                   = false;                  //!< first vborder frame, but need to check, because of dark picture
    //!<
};


/**
 * check packet for video based marks
 */
class cVideo {
public:

    /**
     * constructor of class to check packet for video based marks
     * @param decoderParam        decoder
     * @param indexParam          recording index
     * @param criteriaParam       detection criteria
     * @param recDirParam         recording directory
     * @param autoLogo            source of logo
     * @param logoCacheDirParam   logo cache directory
     */
    explicit cVideo(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam, const char *recDirParam, const int autoLogo, const char *logoCacheDirParam);
    ~cVideo();

    /**
     * copy constructor, not used, only for formal reason
     */
    cVideo(const cVideo &origin) {
        decoder                 = nullptr;
        index                   = nullptr;
        criteria                = nullptr;
        recDir                  = nullptr;
        logoCacheDir            = nullptr;
        sceneChangeDetect       = nullptr;
        blackScreenDetect       = nullptr;
        hBorderDetect           = nullptr;
        vBorderDetect           = nullptr;
        logoDetect              = nullptr;
        aspectRatioBeforeFrame  = 0;
        packetNumberBefore      = origin.packetNumberBefore;
        framePTSBefore          = origin.framePTSBefore;
    };

    /**
     * operator=, not used, only for formal reason
     */
    cVideo &operator =(const cVideo *origin) {
        decoder                 = nullptr;
        index                   = nullptr;
        criteria                = origin->criteria;
        recDir                  = nullptr;
        logoCacheDir            = nullptr;
        sceneChangeDetect       = nullptr;
        blackScreenDetect       = nullptr;
        hBorderDetect           = nullptr;
        vBorderDetect           = nullptr;
        logoDetect              = nullptr;
        aspectRatioBeforeFrame  = 0;
        videoMarks              = {};
        packetNumberBefore      = origin->packetNumberBefore;
        framePTSBefore          = origin->framePTSBefore;
        return *this;
    }

    /**
     * get corner index of loaded logo
     * @return corner index of loaded logo, -1 if no valid logo found
     */
    int GetLogoCorner() const;

    /**
     * detect video packet based marks
     * @return array of detected marks from this video packet
     */
    sMarkAdMarks *Process();

    /**
     * reduce logo detection to plane 0
     */
    void ReducePlanes(void);

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
     * @param type         mark type
     * @param packetNumber packet number
     * @param framePTS     TPS of the decoded frame
     * @param before   video aspect ratio before mark position
     * @param after    video aspect ratio after mark position
     * @return true if free position in new mark array found, false otherwise
     */
    bool AddMark(int type, int packetNumber, int64_t framePTS, const sAspectRatio *before = nullptr, const sAspectRatio *after = nullptr);

    cDecoder *decoder                     = nullptr;  //!< pointer to decoder
    //!<
    cIndex *index                         = nullptr;  //!< pointer to index
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
    int packetNumberBefore                = -1;       //!< number of packet before
    //!<
    int64_t framePTSBefore                = -1;       //!< PTS of packet before
    //!<
    int aspectRatioBeforeFrame            = 0;        //!< last frame number before aspect ratio change, needed for stop mark
    //!<
};
#endif
