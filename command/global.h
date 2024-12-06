/*
 * global.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __global_h_
#define __global_h_

#if defined (_WIN32) || defined (__MINGW32__)
/* mingw64 build on Windows 7 or higher */
#undef POSIX
#define WINDOWS
#else
/* safe to include unistd.h and use the
 * _POSIX_VERSION macro */
#undef WINDOWS
#define POSIX
#endif

#include <time.h>
#include <stdint.h>
#include <climits>

#ifndef uchar
typedef unsigned char uchar;
#endif

#define MIN_NONSILENCE_VOLUME 3   // min volume to detect as no silence part
#define MAXSTREAMS 10
#define PLANES 3
#define CORNERS 4
// ignore this number of frames at the start for start marks, they are initial marks from recording before
#define IGNORE_AT_START 12


// global types
#define MT_UNDEFINED          (unsigned char) 0x00
#define MT_START              (unsigned char) 0x01
#define MT_STOP               (unsigned char) 0x02

// mark types
#define MT_ASSUMED            (unsigned char) 0x10
#define MT_ASSUMEDSTART       (unsigned char) 0x11
#define MT_ASSUMEDSTOP        (unsigned char) 0x12

#define MT_SCENECHANGE        (unsigned char) 0x20
#define MT_SCENESTART         (unsigned char) 0x21
#define MT_SCENESTOP          (unsigned char) 0x22

#define MT_SOUNDCHANGE        (unsigned char) 0x30
#define MT_SOUNDSTART         (unsigned char) 0x31
#define MT_SOUNDSTOP          (unsigned char) 0x32

#define MT_LOWERBORDERCHANGE  (unsigned char) 0x40
#define MT_NOLOWERBORDERSTART (unsigned char) 0x41
#define MT_NOLOWERBORDERSTOP  (unsigned char) 0x42

#define MT_BLACKCHANGE        (unsigned char) 0x50
#define MT_NOBLACKSTART       (unsigned char) 0x51
#define MT_NOBLACKSTOP        (unsigned char) 0x52

#define MT_LOGOCHANGE         (unsigned char) 0x60
#define MT_LOGOSTART          (unsigned char) 0x61
#define MT_LOGOSTOP           (unsigned char) 0x62

#define MT_VBORDERCHANGE      (unsigned char) 0x70
#define MT_VBORDERSTART       (unsigned char) 0x71
#define MT_VBORDERSTOP        (unsigned char) 0x72

#define MT_HBORDERCHANGE      (unsigned char) 0x80
#define MT_HBORDERSTART       (unsigned char) 0x81
#define MT_HBORDERSTOP        (unsigned char) 0x82

#define MT_ASPECTCHANGE       (unsigned char) 0x90
#define MT_ASPECTSTART        (unsigned char) 0x91
#define MT_ASPECTSTOP         (unsigned char) 0x92

#define MT_CHANNELCHANGE      (unsigned char) 0xA0
#define MT_CHANNELSTART       (unsigned char) 0xA1
#define MT_CHANNELSTOP        (unsigned char) 0xA2

#define MT_TYPECHANGE         (unsigned char) 0xB0
#define MT_TYPECHANGESTART    (unsigned char) 0xB1
#define MT_TYPECHANGESTOP     (unsigned char) 0xB2

#define MT_VPSCHANGE          (unsigned char) 0xC0
#define MT_VPSSTART           (unsigned char) 0xC1
#define MT_VPSSTOP            (unsigned char) 0xC2

#define MT_RECORDINGCHANGE    (unsigned char) 0xD0
#define MT_RECORDINGSTART     (unsigned char) 0xD1
#define MT_RECORDINGSTOP      (unsigned char) 0xD2

#define MT_MOVED              (unsigned char) 0xE0
#define MT_MOVEDSTART         (unsigned char) 0xE1
#define MT_MOVEDSTOP          (unsigned char) 0xE2

// global types
#define MT_VIDEO              (unsigned char) 0xFD  // dummy type for video decoding state
#define MT_AUDIO              (unsigned char) 0xFE  // dummy type for audio decoding state
#define MT_ALL                (unsigned char) 0xFF

// subtypes for moved marks
#define MT_OVERLAPCHANGE                     0xD10
#define MT_OVERLAPSTART                      0xD11
#define MT_OVERLAPSTOP                       0xD12

#define MT_INTRODUCTIONCHANGE                0xD20
#define MT_INTRODUCTIONSTART                 0xD21

#define MT_CLOSINGCREDITSCHANGE              0xD30
#define MT_CLOSINGCREDITSSTOP                0xD32

#define MT_ADINFRAMECHANGE                   0xD40
#define MT_NOADINFRAMESTART                  0xD41  // used to replace start mark, frame after ad in frame ends
#define MT_NOADINFRAMESTOP                   0xD42  // used to replace stop mark, frame before ad in frame starts


// corner index
enum {
    TOP_LEFT,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT
};


/**
 * evaluate stop/start pair status
 */
enum eEvaluateStatus {
    STATUS_ERROR    = -3,
    STATUS_DISABLED = -2,
    STATUS_NO       = -1,
    STATUS_UNKNOWN  =  0,
    STATUS_YES      =  1
};


/**
 * mark position
 */
typedef struct sMarkPos {
    int position = -1;  //!< packet number decoded of mark position
    //!<
    int64_t pts  = -1;  //!< pts of decoded frame
    //!<
} sMarkPos;


/**
 * logo stop / start pair
 */
typedef struct sLogoStopStartPair {
    int stopPosition                 = -1;              //!< frame number of logo stop mark
    //!<
    int startPosition                = -1;              //!< frame number of logo start mark
    //!<
    bool hasBorder                   = false;           //!< true if there is vborder or hborder between stopPosition and startPosition
    //!<
    int isLogoChange                 = STATUS_UNKNOWN;  //!< status of logo change, value #eEvaluateStatus
    //!<
    int isAdInFrameAfterStart        = STATUS_UNKNOWN;  //!< status of advertising in frame with logo after logo start mark, value #eEvaluateStatus
    //!<
    int isAdInFrameBeforeStop        = STATUS_UNKNOWN;  //!< status of advertising in frame with logo before logo stop mark, value #eEvaluateStatus
    //!<
    int isStartMarkInBroadcast       = STATUS_UNKNOWN;  //!< status of in broadacst, value #eEvaluateStatus
    //!<
    int isInfoLogo                   = STATUS_UNKNOWN;  //!< status of info logo, value #eEvaluateStatus
    //!<
    eEvaluateStatus isClosingCredits = STATUS_UNKNOWN;  //!< status of closing credits, value #eEvaluateStatus
    //!<
    sMarkPos endClosingCredits       = {-1};            //!< mark position of end of closing credits
    //!<
} sLogoStopStartPair;


/**
 * logo size structure
 */
typedef struct sLogoSize {
    int width = 0;  //!< width for logo extractions
    //!

    int height = 0; //!< height for logo extraction
    //!<
} sLogoSize;


/**
 * video picture structure
 */
typedef struct sVideoPicture {
    uchar *plane[PLANES]      = {nullptr};  //!< array of picture planes (YUV420)
    //!<
    int planeLineSize[PLANES] = {0};        //!< size int bytes of each picture plane line
    //!<
    int packetNumber          = -1;         //!< packet number of picture, -1 for invalid
    //!<
    int64_t pts               = -1;         //!< pts of picture
    //!<
    int width                 = 0;          //!< video width
    //!<
    int height                = 0;          //!< viedeo height
    //!<
} sVideoPicture;  //!< video picture data structure


/**
 * video aspect ratio (DAR or PAR)
 */
typedef struct sAspectRatio {
    int num = 0;  //!< video aspectio ratio numerator
    //!<
    int den = 0;  //!< video aspectio ratio denominator
    //!<

    /**
     *  operator !=
     */
    bool operator != (const sAspectRatio& other) const {
        if ((this->num != other.num) || (this->den != other.den)) return true;
        return false;

    }
    /**
     *  operator ==
     */
    bool operator == (const sAspectRatio& other) const {
        if ((this->num == other.num) && (this->den == other.den)) return true;
        return false;
    }

    /**
     *  operator =
     */
    sAspectRatio& operator = (const sAspectRatio& other) {
        num = other.num;
        den = other.den;
        return *this;
    }
} sAspectRatio;


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


/**
 * AC3 audio channel change
 */
typedef struct sAudioAC3Channels {
    int channelCountAfter  = 0;     //!< channel count after channel change
    //!<
    int channelCountBefore = 0;     //!< channel count before channel change
    //!<
    int videoPacketNumber  = -1;    //!< associated video packet number from PTS ring buffer
    //!<
    int64_t videoFramePTS  = -1;    //!< decode frame PTS of last change
    //!<
    bool processed         = true;  //!< true if channel change is processed by audio channel mark detection
    //!<
} sAudioAC3Channels;


/**
 * corner area after sobel transformation
 */
typedef struct sAreaT {
    uchar **sobel         = nullptr;              //!< monochrome picture from edge after sobel transformation, memory will be allocated after we know video resolution
    //!<
    uchar **logo          = nullptr;              //!< monochrome mask of logo, memory will be allocated after we know video resolution
    //!<
    uchar **result        = nullptr;              //!< result of sobel + mask, memory will be allocated after we know video resolution
    //!<
    uchar **inverse       = nullptr;              //!< inverse result of sobel + mask, memory will be allocated after we know video resolution
    //!<
    bool valid[PLANES]    = {false};              //!< true if plane is valid
    //!<
    int mPixel[PLANES]    = {0};                  //!< black pixel in mask
    //!<
    int rPixel[PLANES]    = {0};                  //!< black pixel in result
    //!<
    int iPixel[PLANES]    = {0};                  //!< black pixel in inverse result
    //!<
    int status            = LOGO_UNINITIALIZED;   //!< logo status: on, off, uninitialized
    //!<
    int statePacketNumber = -1;                   //!< packet number last detected logo start/stop state change frame number
    //!<
    int64_t stateFramePTS = -1;                   //!< frame PTS last detected logo start/stop state change frame number
    //!<
    int counter           = 0;                    //!< how many logo on, offs detected
    //!<
    int intensity         = 0;                    //!< area intensity (higher -> brighter)
    //!<
    sLogoSize logoSize    = {0};                  //!< logo size
    //!<
    int logoCorner        = -1;                   //!< corner of logo
    //!<
    sAspectRatio logoAspectRatio = {};            //!< logo for video with this aspect ratio
    //!<
} sAreaT;



/**
 * frame overlap start and stop positions
 */
typedef struct sOverlapPos {
    int similarBeforeStartPacketNumber = -1; //!< start of similar part before stop mark
    //!<
    int64_t similarBeforeStartPTS      = -1; //!< PTS of start of similar part before stop mark
    //!<
    int similarBeforeEndPacketNumber   = -1; //!< stop of similar part before stop mark, this will be the new stop position
    //!<
    int64_t similarBeforeEndPTS        = -1; //!< PTS of stop of similar part before stop mark, this will be the new stop position
    //!<
    int similarAfterStartPacketNumber  = -1; //!< start of similar part after start mark
    //!<
    int64_t similarAfterStartPTS       = -1; //!< PTS of start of similar part after start mark
    //!<
    int similarAfterEndPacketNumber    = -1; //!< stop of similar part after start mark, this will be the new start position
    //!<
    int64_t similarAfterEndPTS         = -1; //!< PST of stop of similar part after start mark, this will be the new start position
    //!<
    int similarMax                     =  0; //!< maximum similar value from the overlap (lowest match)
    //!<
    int similarEnd                     =  0; //!< similar value from the end of the overlap
    //!<
} sOverlapPos;


/**
 * new mark to add
 */
typedef struct sMarkAdMark {
    int type                       = MT_UNDEFINED;   //!< type of the new mark, see global.h
    //!<
    int position                   = -1;             //!< internal packet read/decode position
    //!<
    int64_t framePTS               = -1;             //!< decoded frame PTS
    //!<
    int channelsBefore             = 0;              //!< audio channel count before mark (set if channel changed at this mark)
    //!<
    int channelsAfter              = 0;              //!< audio channel count after mark (set if channel changed at this mark)
    //!<
    sAspectRatio AspectRatioBefore = {};             //!< video aspect ratio before mark (set if video aspect ratio changed at this mark)
    //!<
    sAspectRatio AspectRatioAfter  = {};             //!< video aspect ratio after mark (set if video aspect ratio changed at this mark)
    //!<
} sMarkAdMark;


/**
 * array of new marks to add
 */
typedef struct sMarkAdMarks {
    static const int maxCount = 6; //!< maximum elements of the array
    //!<
    int Count                 = 0; //!< current count of elements in the array
    //!<
    sMarkAdMark Number[maxCount];  //!< array of new marks to add
    //!<
} sMarkAdMarks;


#define MARKAD_PIDTYPE_VIDEO_H262 0x10
#define MARKAD_PIDTYPE_VIDEO_H264 0x11
#define MARKAD_PIDTYPE_VIDEO_H265 0x12

#define MARKAD_PIDTYPE_AUDIO_AC3  0x20
#define MARKAD_PIDTYPE_AUDIO_MP2  0x21

#endif
