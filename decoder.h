/*
 * decoder.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __decoder_h_
#define __decoder_h_

#define __STDC_CONSTANT_MACROS

#include <vdr/tools.h> // needed for (d/e/i)syslog
#include <stdint.h>
#include <sched.h>

#ifndef DECLARE_ALIGNED
#define DECLARE_ALIGNED(n,t,v) t v __attribute__ ((aligned (n)))
#endif

#ifndef CPU_COUNT
#define CPU_COUNT(i) 1 // very crude ;)
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#ifdef HAVE_AVCODEC
extern "C"
{
#include <libavcodec/avcodec.h>

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(23<<8)+0)
#include <libavformat/avformat.h>
#endif
}
#endif

#include "global.h"

class cMarkAdDecoder
{
private:
    int recvnumber;
#ifdef HAVE_AVCODEC
    AVCodecContext *ac3_context;
    AVCodecContext *mp2_context;
    AVCodecContext *video_context;
    AVFrame *video_frame;
    uchar *temp_pictureplane[4];

    bool SetAudioInfos(MarkAdContext *maContext, AVCodecContext *Audio_Context);

    void PAR2DAR(AVRational a, AVRational *erg);
    bool SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context,
                       AVFrame *Video_Frame, AVRational *DAR);
#endif
public:
    void FindAC3AudioInfos(MarkAdContext *maContext, uchar *espkt, int eslen);
    void FindH264VideoInfos(MarkAdContext *maContext, uchar *pespkt, int peslen);
    void FindH262VideoInfos(MarkAdContext *maContext, uchar *pespkt, int peslen);
    bool DecodeVideo(MarkAdContext *maContext, uchar *pespkt, int peslen);
    bool DecodeMP2(MarkAdContext *maContext, uchar *espkt, int eslen);
    bool DecodeAC3(MarkAdContext *maContext, uchar *espkt, int eslen);
    cMarkAdDecoder(int recvnumber, bool useH264, bool hasAC3);
    ~cMarkAdDecoder();
};

#endif
