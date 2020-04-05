/*
 * decoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __decoder_h_
#define __decoder_h_

#ifndef uchar
typedef unsigned char uchar;
#endif

extern "C"
{
#ifdef USE_OLD_FFMPEG_HEADERS
#include <avcodec.h>
#else
#include <libavcodec/avcodec.h>
#endif

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(0<<8)+0)
#warning H264 parsing may be broken, better use libavcodec52
#endif

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(23<<8)+0)
#ifdef USE_OLD_FFMPEG_HEADERS
#include <avformat.h>
#else
#include <libavformat/avformat.h>
#endif
#endif
#include "debug.h"
}

#include "global.h"

class cMarkAdDecoder
{
private:
    bool skipframes;

    AVCodec *video_codec;
    AVCodecContext *video_context;
    AVFrame *video_frame;

    int threadcount;
    int8_t *last_qscale_table;

    bool SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context,
                       AVFrame *Video_Frame);
    bool noticeERRVID;
    bool addPkt;
public:
    bool DecodeVideo(MarkAdContext *maContext, uchar *pkt, int plen);
    bool Clear();
    cMarkAdDecoder(bool useH264, int Threads);
    ~cMarkAdDecoder();
};

#endif
