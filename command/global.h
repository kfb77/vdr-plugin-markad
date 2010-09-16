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

#define MT_UNCERTAIN	 0
#define MT_START         1
#define MT_STOP          2

#define MT_COMMON        0x10
#define MT_COMMONSTART   0x11
#define MT_COMMONSTOP    0x12

#define MT_ASPECTCHANGE  0x20
#define MT_ASPECTSTART   0x21
#define MT_ASPECTSTOP    0x22

#define MT_CHANNELCHANGE 0x30
#define MT_CHANNELSTART  0x31
#define MT_CHANNELSTOP   0x32

#define MT_LOGOCHANGE    0x40
#define MT_LOGOSTART     0x41
#define MT_LOGOSTOP      0x42

#define MT_BORDERCHANGE  0x50
#define MT_BORDERSTART   0x51
#define MT_BORDERSTOP    0x52

#define MT_SILENCECHANGE 0x60

#define MT_MOVED         0xE0
#define MT_ALL           0xFF

typedef struct MarkAdPos
{
    int FrameNumberBefore;
    int FrameNumberAfter;
    char *CommentBefore;
    char *CommentAfter;
} MarkAdPos;

typedef struct MarkAdMark
{
    char Type;
    int Position;
    char *Comment;
} MarkAdMark;

typedef struct MarkAdAspectRatio
{
    int Num;
    int Den;
} MarkAdAspectRatio;

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
    char *LogoDir; // Logo Directory, default /var/lib/markad

    struct Options
    {
        int LogoExtraction;
        int LogoWidth;
        int LogoHeight;
        bool ASD;
    } Options;

    struct Info
    {
        int Length; // in seconds
        char *ChannelID;
        MarkAdAspectRatio AspectRatio;
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
        } Options;

        struct Info
        {
            int Width;  // width of pic
            int Height; // height of pic
            int Pict_Type; // picture type (I,P,B,S,SI,SP,BI)
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
            bool AudioSilenceDetection;
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
