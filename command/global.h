/*
 * global.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __global_h_
#define __global_h_

#include <time.h>

#ifndef uchar
    typedef unsigned char uchar;
#endif

#define MAXSTREAMS 10
#define PLANES 3

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


typedef struct config {
    char logFile[20] = {};
    char logoDirectory[1024];
    char LogoDir[1024];
    char markFileName[1024];
    char svdrphost[1024];
    int logoExtraction;
    int logoWidth;
    int logoHeight;
    int ignoreInfo;
    int svdrpport;
    int threads;
    int astopoffs;
    int posttimer;
    bool useVPS = false;
    bool MarkadCut = false;
    bool ac3ReEncode = false;
    int autoLogo = 2;   // 0 = off, 1 = on, use less memory but a lot of cpu, 2 use a lot of memory but runs faster
    const char *recDir;
    bool DecodeVideo;
    bool DecodeAudio;
    bool BackupMarks;
    bool NoPid;
    bool OSD;
    int online = 0;
    bool Before;
    bool SaveInfo;
    int decodingLevel = 0; // 0 = decode only iFrames
                           // 1 = decode all video frames and set marks to all frame types
                           // 2 = cut video on all frame types

} MarkAdConfig;


typedef struct MarkAdPos {
    int FrameNumberBefore;
    int FrameNumberAfter;
} MarkAdPos;


typedef struct MarkAdAspectRatio {
    int Num=0;
    int Den=0;
} MarkAdAspectRatio;


typedef struct MarkAdMark {
    int Type = 0;
    int Position = 0;
    int ChannelsBefore = 0;
    int ChannelsAfter = 0;
    MarkAdAspectRatio AspectRatioBefore;
    MarkAdAspectRatio AspectRatioAfter;
} MarkAdMark;


typedef struct MarkAdMarks {
    static const int maxCount = 4;
    MarkAdMark Number[maxCount];
    int Count;
} MarkAdMarks;


#define MARKAD_PIDTYPE_VIDEO_H262 0x10
#define MARKAD_PIDTYPE_VIDEO_H264 0x11
#define MARKAD_PIDTYPE_VIDEO_H265 0x12

#define MARKAD_PIDTYPE_AUDIO_AC3  0x20
#define MARKAD_PIDTYPE_AUDIO_MP2  0x21


typedef struct MarkAdPid {
    int Num = 0;
    int Type = 0;
} MarkAdPid;


typedef struct MarkAdContext {
    const MarkAdConfig *Config;

    struct Info {
        bool isRunningRecording = false;           // true if markad is running during recording
        bool isStartMarkSaved = false;             // true if dummy start mark is set to end of pre timer and saved
        int tStart = 0;                            // offset of timer start to recording start (pre timer)
        MarkAdAspectRatio AspectRatio;   // set from info file and checked after chkSTART, valid for the recording
        bool checkedAspectRatio = false;
        short int Channels[MAXSTREAMS] = {0};
        char *ChannelName;
        bool timerVPS = false;  // true it was a VPS controlled timer
        MarkAdPid VPid;
        MarkAdPid APid;
    } Info;

    struct Video {
        struct Options {
            bool IgnoreAspectRatio;
            bool IgnoreBlackScreenDetection = false;
            bool IgnoreLogoDetection;
            bool ignoreHborder = false; // ignore horizontal borders detection if there is none at the start of the recording
                                        // later horizontal borders could be part of an ad
            bool ignoreVborder = false; // ignore vertical borders detection if there is none at the start of the recording
                                        // later vertical borders could be closing credits
        } Options;

        struct Info {
            int Width;  // width of pic
            int Height; // height of pic
            int Pict_Type; // picture type (I,P,B,S,SI,SP,BI)
            int Pix_Fmt; // Pixel format (see libavutil/pixfmt.h)
            MarkAdAspectRatio AspectRatio;  // set by decoder for the current frame
            double FramesPerSecond;
            bool Interlaced = false;
            bool hasBorder = false;
        } Info;

        struct Logo {
            int width = 0;  // width of logo
            int height = 0;  // height of logo
            int corner = -1;  // corner of logo
            bool isRotating = false;  // logo is rotating, e.g. SAT.1
        } Logo;

        struct Data {
            bool Valid; // flag, if true data is valid
            uchar *Plane[PLANES];  // picture planes (YUV420)
            int PlaneLinesize[PLANES]; // size int bytes of each picture plane line
        } Data;
    } Video;

    struct Audio {
        struct Options {
            bool IgnoreDolbyDetection;
        } Options;
        struct Info {
            short int Channels[MAXSTREAMS] = {0}; // number of audio channels from AC3 streams
            int SampleRate;
            bool channelChange = false;
            int frameChannelChange;
        } Info;
        struct Data {
            bool Valid;
            short *SampleBuf;
            int SampleBufLen;
        } Data;
    } Audio;
} MarkAdContext;
#endif
