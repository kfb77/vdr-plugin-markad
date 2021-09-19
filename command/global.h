/*
 * global.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __global_h_
#define __global_h_

#include <time.h>
#include <stdint.h>
#include <climits>

#ifndef uchar
    typedef unsigned char uchar;
#endif

#define MAXSTREAMS 10
#define PLANES 3
#define CORNERS 4

#define MA_I_TYPE 1
#define MA_P_TYPE 2
#define MA_B_TYPE 3
#define MA_D_TYPE 4
#define MA_SI_TYPE 5
#define MA_SP_TYPE 6
#define MA_BI_TYPE 7

#define MT_START          (unsigned char) 1
#define MT_STOP           (unsigned char) 2

#define MT_ASSUMED        (unsigned char) 0x10
#define MT_ASSUMEDSTART   (unsigned char) 0x11
#define MT_ASSUMEDSTOP    (unsigned char) 0x12

#define MT_BLACKCHANGE    (unsigned char) 0x20
#define MT_NOBLACKSTART   (unsigned char) 0x21
#define MT_NOBLACKSTOP    (unsigned char) 0x22

#define MT_LOGOCHANGE     (unsigned char) 0x30
#define MT_LOGOSTART      (unsigned char) 0x31
#define MT_LOGOSTOP       (unsigned char) 0x32

#define MT_VBORDERCHANGE  (unsigned char) 0x40
#define MT_VBORDERSTART   (unsigned char) 0x41
#define MT_VBORDERSTOP    (unsigned char) 0x42

#define MT_HBORDERCHANGE  (unsigned char) 0x50
#define MT_HBORDERSTART   (unsigned char) 0x51
#define MT_HBORDERSTOP    (unsigned char) 0x52

#define MT_ASPECTCHANGE   (unsigned char) 0x60
#define MT_ASPECTSTART    (unsigned char) 0x61
#define MT_ASPECTSTOP     (unsigned char) 0x62

#define MT_CHANNELCHANGE  (unsigned char) 0x70
#define MT_CHANNELSTART   (unsigned char) 0x71
#define MT_CHANNELSTOP    (unsigned char) 0x72

#define MT_VPSCHANGE      (unsigned char) 0xC0
#define MT_VPSSTART       (unsigned char) 0xC1
#define MT_VPSSTOP        (unsigned char) 0xC2

#define MT_RECORDINGSTART (unsigned char) 0xD1
#define MT_RECORDINGSTOP  (unsigned char) 0xD2

#define MT_MOVEDCHANGE    (unsigned char) 0xE0
#define MT_MOVEDSTART     (unsigned char) 0xE1
#define MT_MOVEDSTOP      (unsigned char) 0xE2

#define MT_ALL            (unsigned char) 0xFF


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
 * markad configuration structure
 */
typedef struct sMarkAdConfig {
    char logFile[20] = {};  //!< name of the markad log file
                            //!<

    char logoDirectory[1024]; //!< logo cache directory (defaut /var/lib/markad)
                              //!<

    char markFileName[1024];  //!< name of the marks file (default marks)
                              //!<

    char svdrphost[1024];  //!< ip or name of vdr server (default localhost)
                           //!<

    int svdrpport;  //!< vdr svdrp port number
                    //!<

    int logoExtraction = false; //!< <b>true:</b> extract logo and store to /tmp <br>
                                //!< <b>false:</b> normal markad operation
                                //!<

    int logoWidth;  //!< width for logo extractions
                    //!

    int logoHeight; //!< height for logo extraction
                    //!<

    int ignoreInfo; //!< <b>true:</b> ignore vdr info file <br>
                    //!< <b>false:</b> use data from vdr info file
                    //!<

    int threads;  //!< number of threads for decoder and encoder
                  //!<

    int astopoffs; //!< assumed stop offset in seconds
                   //!<

    int posttimer; //!< time in second in the recording after assumed end of broadcast
                   //!<

    bool useVPS = false; //!< <b>true:</b> use informations from vps file to optimize marks
                         //!< <b>false:</b> do not use informations from vps file to optimize marks

    bool MarkadCut = false;   //!< cut video after mark detection
                              //!<

    bool ac3ReEncode = false; //!< re-encode AC3 stream and adapt audio volume
                              //!<

    int autoLogo = 2;   //!< 0 = off, 1 = deprecated, 2 = on
                        //!<

    const char *recDir; //!< name of the recording directory
                        //!<

    bool decodeVideo; //!< <b>true:</b> use video stream to detect marks <br>
                      //!< <b>false:</b> do not use video stream to detect marks
                      //!<

    bool decodeAudio; //!< <b>true:</b> use audio streams to detect marks <br>
                      //!< <b>false:</b> do not use audio streams to detect marks
                      //!<

    bool backupMarks; //!< <b>true:</b> backup marks file before override <br>
                      //!< <b>false:</b> do not backup marks file
                      //!<

    bool noPid;  //!< <b>true:</b> do not write a PID file <br>
                 //!< <b>false:</b> write a PID file
                 //!<

    bool osd; //!< <b>true:</b> send screen messages to vdr <br>
              //!< <b>false:</b> do not send screen messages to vdr
              //!<

    int online = 0;  //!< start markad immediately when called with "before" as cmd
                     //!< if online is 1, markad starts online for live-recordings
                     //!< only, online=2 starts markad online for every recording
                     //!< live-recordings are identified by having a '@' in the
                     //!< filename so the entry 'Mark instant recording' in the menu
                     //!< Setup - Recording of the vdr should be set to 'yes'
                     //!< ( default is 1 )
                     //!<

    bool before;     //!< <b>true:</b> markad started by vdr before the recording is complete, only valid together with --online <br>
                     //!<

    bool saveInfo;  //!< <b>true:</b> override vdr info file <br>
                    //!< <b>false:</b> do not save info file
                    //!<


    bool fullDecode = false; //!< <b>true:</b> decode all video frames <br>
                             //!< <b>false:</b> decode only iFrames
                             //!<

    bool fullEncode = false;   //!< <b>true:</b> full re-encode all frames, cut on all frame types <br>
                               //!< <b>false:</b> copy frames without re-encode, cut on iframe position
                               //!<

    bool bestEncode = true;  //!< <b>true:</b> encode all video and audio streams <br>
                             //!< <b>false:</b> encode all video and audio streams
                             //!<
    bool pts        = false; //!< <b>true:</b> add pts based timestanp to marks<br>
                             //!< <b>false:</b> otherwise
} sMarkAdConfig;


/**
 * frame overlap start and stop positions
 */
typedef struct sOverlapPos {
    int frameNumberBefore; //!< frame number of overlaps start before mark position
                           //!<

    int frameNumberAfter; //!< frame number of overlaps stop after mark position
                          //!<
} sOverlapPos;


/**
 * video aspect ratio (DAR or PAR)
 */
typedef struct sAspectRatio {
    int num = 0;  //!< video aspectio ratio numerator
                  //!<

    int den = 0;  //!< video aspectio ratio denominator
                  //!<

} sAspectRatio;


/**
 * new mark to add
 */
typedef struct sMarkAdMark {
    int type = 0; //!< type of the new mark, see global.h
                  //!<

    int position = 0; //!< frame position
                      //!<

    int channelsBefore = 0; //!< audio channel count before mark (set if channel changed at this mark)
                            //!<

    int channelsAfter = 0; //!< audio channel count after mark (set if channel changed at this mark)
                           //!<

    sAspectRatio AspectRatioBefore; //!< video aspect ratio before mark (set if video aspect ratio changed at this mark)
                                          //!<

    sAspectRatio AspectRatioAfter; //!< video aspect ratio after mark (set if video aspect ratio changed at this mark)
                                         //!<
} sMarkAdMark;


/**
 * array of new marks to add
 */
typedef struct sMarkAdMarks {
    static const int maxCount = 4; //!< maximum elements of the array
                                   //!<
    int Count; //!< current count of elements in the array
               //!<

    sMarkAdMark Number[maxCount]; //!< array of new marks to add
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
        bool isRunningRecording = false;  //!< <b>true:</b> markad is running during recording <br>
                                          //!< <b>false:</b>  markad is running after recording
                                          //!<

        bool isStartMarkSaved = false;    //!< <b>true:</b> dummy start mark is set to end of pre timer and saved
                                          //!< <b>false:</b> dummy start mark is not jet set
                                          //!<

        int tStart = 0;                   //!< offset of timer start to recording start (pre timer)
                                          //!<

        sAspectRatio AspectRatio;   //!< set from info file and checked after chkSTART, valid for the recording
                                          //!<

        bool checkedAspectRatio = false;  //!< <b>true:</b> current video aspect ratio is verified <br>
                                          //!< <b>false:</b> current video aspect ratio is not jet verified
                                          //!<

        short int Channels[MAXSTREAMS] = {0};  //!< count of audio channel of each audio stream
                                               //!<


        char *ChannelName = NULL;  //!< name of the channel
                                   //!<

        bool timerVPS = false;  //!< <b>true:</b> recording is from a VPS controlled timer <br>
                                //!< <b>false:</b> recording is not from a VPS controlled timer
                                //!<

        int vPidType = 0;  //!< video packet identifier type
                           //!<

    } Info; //!< global markad state infos
            //!<

/**
 * video structure
 */
    struct sVideo {
/**
 * video detection options structure
 */
        struct sOptions {

            bool ignoreAspectRatio = false; //!< <b>true:</b>  ignore video aspect ratio detection, set if we found audio channel changes or H.264 video <br>
                                            //!< <b>false:</b> detect video aspect ratio
                                            //!<

            bool ignoreBlackScreenDetection = false; //!< <b>true:</b>  ignore black screen detection, set if there is a better criteria than logo detection found <br>
                                                     //!< <b>false:</b> detect black screen
                                                     //!<

            bool ignoreLogoDetection = false; //!< <b>true:</b>  ignore logo detection, set if there is any other advertising criteria found <br>
                                              //!< <b>false:</b> detect logo
                                              //!<

            bool ignoreHborder = false; //!< <b>true:</b>  ignore horizontal borders detection, set if there is no horizontal border found at the start of the recording
                                        //!< later horizontal borders could be part of an advertising <br>
                                        //!< <b>false:</b> detect horizontal borders
                                        //!<

            bool ignoreVborder = false; //!< <b>true:</b>  ignore vertical borders detection, set if there is no vertical border found at the start of the recording
                                        //!< later vertical borders could be part of an advertising <br>
                                        //!< <b>false:</b> detect vertical borders
                                        //!<

        } Options; //!< video detection options
                   //!<

/**
 * video stream infos
 */
        struct sInfo {
            int width;  //!< width of the video in pixel
                        //!<

            int height; //!< height of the video in pixel
                        //!<

            int pixFmt; //!< pixel format (see libavutil/pixfmt.h)
                        //!<

            sAspectRatio AspectRatio;  //!< current video aspect ratio, set by decoder for each frame
                                             //!<

            double framesPerSecond; //!< frames per second of the recording
                                    //!<

            bool interlaced = false;  //!< <b>true:</b> video is interlaced <br>
                                      //!< <b>false:</b> video is progressive
                                      //!<

            bool hasBorder = false;  //!< <b>true:</b> video has horizontal or vertical borders <br>
                                     //!< <b>false:</b> video has no horizontal or vertical borders
                                     //!<


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
            int pixelRatio     = INT_MAX; //!< ratio of pixel in logo area, calculated: 1000 * logo pixel / (width * height)
                                          //!<
            bool isRotating    = false;   //!< <b>true:</b>  logo is rotating <br>
                                          //!< <b>false:</b> logo is not rotating
                                          //!<
            bool isTransparent = false;   //!< <b>true:</b>  logo is transparent, expect bad detection <br>
                                          //!< <b>false:</b> logo is not transparent
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
 * audio detection options structure
 */
        struct sOptions {
            bool ignoreDolbyDetection; //!< <b>true:</b> ignore audio channel count detection <br>
                                       //!< <b>false:</b> detect audio channel count changes
                                       //!<

        } Options; //!< audio detection options
                   //!<

/**
 * audio stream info structure
 */
        struct sInfo {
            short int Channels[MAXSTREAMS] = {0}; //!< number of audio channels from each AC3 streams
                                                  //!<

            int SampleRate;  //!< audio sample rate
                             //!<

            bool channelChange = false; //!< a valid channel change is detected in this recording
                                        //!<

            int channelChangeFrame; //!< frame number of last channel change
                                    //!<

            int64_t channelChangePTS; //!< presentation timestamp of last audio channel change
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
