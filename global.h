/*
 * global.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
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
#define MA_S_TYPE 4
#define MA_SI_TYPE 5
#define MA_SP_TYPE 6
#define MA_BI_TYPE 7
#define MA_D_TYPE 80

typedef struct MarkAdMark
{
    int Position;
    char *Comment;
} MarkAdMark;

typedef struct MarkAdAspectRatio
{
    int Num;
    int Den;
} MarkAdAspectRatio;

typedef struct MarkAdContext
{
    struct General
    {
        time_t StartTime;
        time_t EndTime;
        bool ManualRecording;
        bool H264;
        int VPid;
        int APid;
        int DPid;
    } General;

    struct State
    {
        int ContentStarted;
        int ContentStopped;
    } State;

    struct Video
    {
        struct Info
        {
            int Width;  // width of pic
            int Height; // height of pic
            int Pict_Type; // picture type (I,P,B,S,SI,SP,BI)
            MarkAdAspectRatio AspectRatio;
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
        struct Info
        {
            int Channels; // number of audio channels
        } Info;
        struct Data
        {
            bool Valid;
            uchar *SampleBufAC3;
            int SampleBufLenAC3;
            uchar *SampleBufMP2;
            int SampleBufLenMP2;
        } Data;
    } Audio;

} MarkAdContext;

#endif
