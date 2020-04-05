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

#define MT_LOGOCHANGE     (unsigned char) 0x20
#define MT_LOGOSTART      (unsigned char) 0x21
#define MT_LOGOSTOP       (unsigned char) 0x22

#define MT_HBORDERCHANGE  (unsigned char) 0x30
#define MT_HBORDERSTART   (unsigned char) 0x31
#define MT_HBORDERSTOP    (unsigned char) 0x32

#define MT_VBORDERCHANGE  (unsigned char) 0x40
#define MT_VBORDERSTART   (unsigned char) 0x41
#define MT_VBORDERSTOP    (unsigned char) 0x42

#define MT_ASPECTCHANGE   (unsigned char) 0x50
#define MT_ASPECTSTART    (unsigned char) 0x51
#define MT_ASPECTSTOP     (unsigned char) 0x52

#define MT_CHANNELCHANGE  (unsigned char) 0x60
#define MT_CHANNELSTART   (unsigned char) 0x61
#define MT_CHANNELSTOP    (unsigned char) 0x62

#define MT_RECORDINGSTART (unsigned char) 0xD1
#define MT_RECORDINGSTOP  (unsigned char) 0xD2
#define MT_MOVED          (unsigned char) 0xE0
#define MT_ALL            (unsigned char) 0xFF

typedef struct config
{
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

    bool DecodeVideo;
    bool DecodeAudio;
    bool BackupMarks;

    bool NoPid;
    bool OSD;
    bool Before;
    bool GenIndex;
    bool SaveInfo;
} MarkAdConfig;

typedef struct MarkAdPos
{
    int FrameNumberBefore;
    int FrameNumberAfter;
} MarkAdPos;

typedef struct MarkAdAspectRatio
{
    int Num;
    int Den;
} MarkAdAspectRatio;

typedef struct MarkAdMark
{
    int Type;
    int Position;
    int ChannelsBefore;
    int ChannelsAfter;
    MarkAdAspectRatio AspectRatioBefore;
    MarkAdAspectRatio AspectRatioAfter;
} MarkAdMark;

typedef struct MarkAdMarks
{
    static const int maxCount=4;
    MarkAdMark Number[maxCount];
    int Count;
} MarkAdMarks;

#define MARKAD_PIDTYPE_VIDEO_H262 0x10
#define MARKAD_PIDTYPE_VIDEO_H264 0x11
#define MARKAD_PIDTYPE_AUDIO_AC3  0x20
#define MARKAD_PIDTYPE_AUDIO_MP2  0x21

typedef struct MarkAdPid
{
    int Num;
    int Type;
} MarkAdPid;

typedef struct MarkAdContext
{
    const MarkAdConfig *Config;

    struct Info
    {

        MarkAdAspectRatio AspectRatio;
        int Channels;

        char *ChannelName;
        MarkAdPid VPid;
        MarkAdPid APid;
        MarkAdPid DPid;
    } Info;

    struct Video
    {
        struct Options
        {
            bool IgnoreAspectRatio;
            bool IgnoreLogoDetection;
            bool WeakMarksOk;
        } Options;

        struct Info
        {
            int Width;  // width of pic
            int Height; // height of pic
            int Pict_Type; // picture type (I,P,B,S,SI,SP,BI)
            int Pix_Fmt; // Pixel format (see libavutil/pixfmt.h)
            MarkAdAspectRatio AspectRatio;
            double FramesPerSecond;
            bool Interlaced;
        } Info;

        struct Data
        {
            bool Valid; // flag, if true data is valid
            uchar *Plane[4];  // picture planes (YUV420)
            int PlaneLinesize[4]; // size int bytes of each picture plane line
        } Data;
    } Video;

    struct Audio
    {
        struct Options
        {
            bool IgnoreDolbyDetection;
        } Options;
        struct Info
        {
            int Channels; // number of audio channels
            int SampleRate;
        } Info;
        struct Data
        {
            bool Valid;
            short *SampleBuf;
            int SampleBufLen;
        } Data;
    } Audio;

} MarkAdContext;

#endif
