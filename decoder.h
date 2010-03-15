/*
 * decoder.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
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

extern "C"
{
#include <libavcodec/avcodec.h>

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(23<<8)+0)
#include <libavformat/avformat.h>
#endif
}

#include "global.h"

class cMarkAdDecoder
{
private:
    int recvnumber;
    int16_t *audiobuf;
    int audiobufsize;

    AVCodecContext *ac3_context;
    AVCodecContext *mp2_context;
    AVCodecContext *video_context;
    AVFrame *video_frame;

    int8_t *last_qscale_table;

    bool SetAudioInfos(MarkAdContext *maContext, AVCodecContext *Audio_Context);

    void PAR2DAR(AVRational a, AVRational *erg);
    bool SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context,
                       AVFrame *Video_Frame);
public:
    bool DecodeVideo(MarkAdContext *maContext, uchar *pkt, int plen);
    bool DecodeMP2(MarkAdContext *maContext, uchar *espkt, int eslen);
    bool DecodeAC3(MarkAdContext *maContext, uchar *espkt, int eslen);
    cMarkAdDecoder(int recvnumber, bool useH264, bool useMP2, bool hasAC3);
    ~cMarkAdDecoder();
};



#endif
