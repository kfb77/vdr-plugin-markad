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

#define MAX_SILENCE_VOLUME 9   // max volume to detect as silence part
#define MAXSTREAMS 10
#define PLANES 3
#define CORNERS 4

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
    int frameNumber           = -1;         //!< frame number of picture, -1 for invalid
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
    int channelCount     = 0;     //!< channel count
    //!<
    int64_t pts          = 0;     //!< packet pts of last change
    //!<
    int videoFrameNumber = -1;    //!< associated video frame number from PTS ring buffer
    //!<
    bool processed       = true;  //!< true if channel change is processed by audio channel mark detection
    //!<
} sAudioAC3Channels;


/**
 * corner area after sobel transformation
 */
typedef struct sAreaT {
    uchar **sobel        = nullptr;              //!< monochrome picture from edge after sobel transformation, memory will be allocated after we know video resolution
    //!<
    uchar **logo         = nullptr;              //!< monochrome mask of logo, memory will be allocated after we know video resolution
    //!<
    uchar **result       = nullptr;              //!< result of sobel + mask, memory will be allocated after we know video resolution
    //!<
    uchar **inverse      = nullptr;              //!< inverse result of sobel + mask, memory will be allocated after we know video resolution
    //!<
    bool valid[PLANES]   = {false};              //!< true if plane is valid
    //!<
    int mPixel[PLANES]   = {0};                  //!< black pixel in mask
    //!<
    int rPixel[PLANES]   = {0};                  //!< black pixel in result
    //!<
    int iPixel[PLANES]   = {0};                  //!< black pixel in inverse result
    //!<
    int status           = LOGO_UNINITIALIZED;   //!< logo status: on, off, uninitialized
    //!<
    int stateFrameNumber = -1;                   //!< last detected logo start/stop state change frame number
    //!<
    int counter          = 0;                    //!< how many logo on, offs detected
    //!<
    int intensity        = 0;                    //!< area intensity (higher -> brighter)
    //!<
    sLogoSize logoSize   = {0};                  //!< logo size
    //!<
    int logoCorner       = -1;                   //!< corner of logo
    //!<
    sAspectRatio logoAspectRatio;                //!< logo for video with this aspect ratio
    //!<
} sAreaT;


/**
 * markad configuration structure
 */
typedef struct sMarkAdConfig {
    char logFile[20]               = {};       //!< name of the markad log file
    //!<
    char logoCacheDirectory[1024]  = {};       //!< logo cache directory (default /var/lib/markad)
    //!<
    char markFileName[255]         = {};       //!< name of the marks file (default marks)
    //!<
    char svdrphost[1024]           = {};       //!< ip or name of vdr server (default localhost)
    //!<
    int svdrpport                  = 0;        //!< vdr svdrp port number
    //!<
    int logoExtraction             = false;    //!< <b>true:</b> extract logo and store to /tmp <br>
    //!< <b>false:</b> normal markad operation
    //!<
    int logoWidth                  = 0;        //!< width for logo extractions
    //!
    int logoHeight                 = 0;        //!< height for logo extraction
    //!<
    int threads                    = 0;        //!< number of threads for decoder and encoder
    //!<
    bool useVPS                    = false;    //!< <b>true:</b> use information from vps file to optimize marks
    //!< <b>false:</b> do not use information from vps file to optimize marks
    bool MarkadCut                 = false;    //!< cut video after mark detection
    //!<
    bool ac3ReEncode               = false;    //!< re-encode AC3 stream and adapt audio volume
    //!<
    int autoLogo                   = 2;        //!< 0 = off, 1 = deprecated, 2 = on
    //!<
    const char *cmd                = nullptr;  //!< cmd parameter
    //!<
    const char *recDir             = nullptr;  //!< name of the recording directory
    //!<
    bool backupMarks               = false;    //!< <b>true:</b> backup marks file before override <br>
    //!< <b>false:</b> do not backup marks file
    //!<
    bool noPid                     = false;    //!< <b>true:</b> do not write a PID file <br>
    //!< <b>false:</b> write a PID file
    //!<
    bool osd                       = false;    //!< <b>true:</b> send screen messages to vdr <br>
    //!< <b>false:</b> do not send screen messages to vdr
    //!<
    int online                     = 0;        //!< start markad immediately when called with "before" as cmd
    //!< if online is 1, markad starts online for live-recordings
    //!< only, online=2 starts markad online for every recording
    //!< live-recordings are identified by having a '@' in the
    //!< filename so the entry 'Mark instant recording' in the menu
    //!< Setup - Recording of the vdr should be set to 'yes'
    //!< ( default is 1 )
    //!<
    bool before                    = false;    //!< <b>true:</b> markad started by vdr before the recording is complete, only valid together with --online <br>
    //!<
    bool fullDecode                = false;    //!< <b>true:</b> decode all video frames <br>
    //!< <b>false:</b> decode only iFrames
    //!<
    bool fullEncode                = false;    //!< <b>true:</b> full re-encode all frames, cut on all frame types <br>
    //!< <b>false:</b> copy frames without re-encode, cut on iframe position
    //!<
    bool bestEncode                = true;     //!< <b>true:</b> encode all video and audio streams <br>
    //!< <b>false:</b> encode all video and audio streams
    //!<
    bool pts                       = false;    //!< <b>true:</b> add pts based timestanp to marks<br>
    //!< <b>false:</b> otherwise
    char hwaccel[16]               = {0};      //!< hardware acceleration methode
    //!<
    bool forceHW                   = false;    //!< force hwaccel for MPEG2
    //!<
    bool perftest                  = false;    //!< <b>true:</b>  run decoder performance test before detect marks<br>
    //!< <b>false:</b> otherwise
} sMarkAdConfig;


/**
 * frame overlap start and stop positions
 */
typedef struct sOverlapPos {
    int similarBeforeStart = -1; //!< start of similar part before stop mark
    //!<
    int similarBeforeEnd   = -1; //!< stop of similar part before stop mark, this will be the new stop position
    //!<
    int similarAfterStart  = -1; //!< start of similar part after start mark
    //!<
    int similarAfterEnd    = -1; //!< stop of similar part after start mark, this will be the new start position
    //!<
    int similarMax         =  0; //!< maximum similar value from the overlap (lowest match)
    //!<
    int similarEnd         =  0; //!< similar value from the end of the overlap
    //!<
} sOverlapPos;


/**
 * new mark to add
 */
typedef struct sMarkAdMark {
    int type                       = 0;   //!< type of the new mark, see global.h
    //!<
    int position                   = 0;   //!< frame position
    //!<
    int channelsBefore             = 0;   //!< audio channel count before mark (set if channel changed at this mark)
    //!<
    int channelsAfter              = 0;   //!< audio channel count after mark (set if channel changed at this mark)
    //!<
    sAspectRatio AspectRatioBefore;       //!< video aspect ratio before mark (set if video aspect ratio changed at this mark)
    //!<
    sAspectRatio AspectRatioAfter;        //!< video aspect ratio after mark (set if video aspect ratio changed at this mark)
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


/**
 * markad context structure
 */
typedef struct sMarkAdContext {
    sMarkAdConfig *Config; //!< markad configuration
    //!<

    /**
     * global markad state structure
     */
    struct sInfo {
        bool isRunningRecording        = false;    //!< <b>true:</b> markad is running during recording <br>
        //!< <b>false:</b>  markad is running after recording
        //!<
        int tStart                     = -1;       //!< offset of timer start to recording start (pre timer)
        //!<
        sAspectRatio AspectRatio       = {0};      //!< set from info file and checked after chkSTART, valid for the recording
        //!<
        short int Channels[MAXSTREAMS] = {0};      //!< count of audio channel of each audio stream
        //!<
        char *ChannelName              = nullptr;  //!< name of the channel
        //!<
        bool timerVPS                  = false;    //!< <b>true:</b> recording is from a VPS controlled timer <br>
        //!< <b>false:</b> recording is not from a VPS controlled timer
        //!<
        int vPidType                   = 0;        //!< video packet identifier type
        //!<
    } Info; //!< global markad state infos
    //!<

    /**
     * video structure
     */
    struct sVideo {
        /**
         * video stream infos
         */
        struct sInfo {
            int width                   = 0;      //!< width of the video in pixel
            //!<
            int height                  = 0;      //!< height of the video in pixel
            //!<
            int pixFmt                  = 0;      //!< pixel format (see libavutil/pixfmt.h)
            //!<
            double framesPerSecond      = 0;      //!< frames per second of the recording
            //!<
            bool interlaced             = false;  //!< <b>true:</b>  video is interlaced <br>
            //!< <b>false:</b> video is progressive
        } Info; //!< video stream infos
        //!<

        /**
         * logo info structure
         */
        struct sLogo {
            int width          = 0;       //!< width of logo
            //!<
            int height         = 0;       //!< height of logo
            //!<
            int corner         = -1;      //!< corner of logo, -1 for undefined
            //!<
        } Logo;                           //!< logo infos
        //!<

        /**
         * video picture data
         */
        struct sData {
            bool valid = false; //!< <b>true:</b>  video data planes are valid <br>
            //!< <b>false:</b> video data planes are not valid
            //!<

            uchar *Plane[PLANES];  //!< array of picture planes (YUV420)
            //!<

            int PlaneLinesize[PLANES]; //!< size int bytes of each picture plane line
            //!<

        } Data;  //!< video picture data
        //!<

    } Video; //!< video stream infos
    //!<

    /**
     * audio structure
     */
    struct sAudio {
        /**
         * audio stream info structure
         */
        struct sInfo {
            short int Channels[MAXSTREAMS] = {0};    //!< number of audio channels from each AC3 streams
            //!<
            int volume                     = -1;     //!< current volume of first MP2 audio stream
            //!<
            int64_t PTS                    = -1;     //!< current PTS of packet from first MP2 audio stream
            //!<
            int codec_id[MAXSTREAMS]       = {0};    //!< codec id of the audio stream
            //!<
            int SampleRate                 = 0;      //!< audio sample rate
            //!<
            bool channelChange             = false;  //!< a valid channel change is detected in this recording
            //!<
            int channelChangeFrame         = -1;     //!< frame number of last channel change
            //!<
            int64_t channelChangePTS       = -1;     //!< presentation timestamp of last audio channel change
            //!<
        } Info; //!< audio stream infos
        //!<

        /**
         * audio data structure
         */
        struct sData {
            bool Valid;  //!< <b>true:</b> audio sample buffer contains valid data <br>
            //!< <b>false:</b> audio sample buffer is not valid
            //!<

            short *SampleBuf; //!< audio sample buffer
            //!<

            int SampleBufLen; //!< length of audio sample buffer
            //!<

        } Data;  //!< audio data
        //!<
    } Audio;  //!< audio stream infos, options and data
    //!<

} sMarkAdContext;
#endif
