/*
 * decoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdint.h>
#include <stdio.h>
#include <sched.h>
#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <cstdlib>

#include "decoder.h"

#ifndef DECLARE_ALIGNED
#define DECLARE_ALIGNED(n,t,v) t v __attribute__ ((aligned (n)))
#endif

#ifndef CPU_COUNT
#define CPU_COUNT(i) 1 // very crude ;)
#endif

#if LIBAVUTIL_VERSION_INT < ((50<<16)+(14<<8)+0)
#define AVMEDIA_TYPE_AUDIO CODEC_TYPE_AUDIO
#define AVMEDIA_TYPE_VIDEO CODEC_TYPE_VIDEO
#define AVMEDIA_TYPE_UNKNOWN CODEC_TYPE_UNKNOWN
#endif

#if LIBAVCODEC_VERSION_INT < ((52<<16)+(65<<8)+0)
int avcodec_copy_context(AVCodecContext *dest, const AVCodecContext *src)
{
    if (dest->codec)   // check that the dest context is uninitialized
    {
        av_log(dest, AV_LOG_ERROR,
               "Tried to copy AVCodecContext %p into already-initialized %p\n",
               src, dest);
        return AVERROR(EINVAL);
    }
    memcpy(dest, src, sizeof(*dest));

    /* set values specific to opened codecs back to their default state */
    dest->priv_data       = NULL;
    dest->codec           = NULL;
    dest->palctrl         = NULL;
    dest->slice_offset    = NULL;
    dest->internal_buffer = NULL;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(18<<8)+0)
    dest->hwaccel         = NULL;
#endif
    dest->execute         = NULL;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(37<<8)+0)
    dest->execute2        = NULL;
#endif
    dest->reget_buffer    = NULL;
    dest->thread_opaque   = NULL;

    /* reallocate values that should be allocated separately */

    dest->rc_eq           = NULL;
    dest->extradata       = NULL;
    dest->intra_matrix    = NULL;
    dest->inter_matrix    = NULL;
    dest->rc_override     = NULL;

    if (src->rc_eq)
    {
        dest->rc_eq = av_strdup(src->rc_eq);
        if (!dest->rc_eq)
            return AVERROR(ENOMEM);
    }

#define alloc_and_copy_or_fail(obj, type, size, pad) \
     if (src->obj && size > 0) { \
        dest->obj =  (type *) av_malloc(size + pad); \
        if (!dest->obj) \
            goto fail; \
        memcpy(dest->obj, src->obj, size); \
        if (pad) \
            memset(((uint8_t *) dest->obj) + size, 0, pad); \
    }

    alloc_and_copy_or_fail(extradata, uint8_t, src->extradata_size,
                           FF_INPUT_BUFFER_PADDING_SIZE);
    alloc_and_copy_or_fail(intra_matrix, uint16_t, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(inter_matrix, uint16_t, 64 * sizeof(int16_t), 0);
    alloc_and_copy_or_fail(rc_override,  RcOverride, src->rc_override_count * sizeof(*src->rc_override), 0);

#undef alloc_and_copy_or_fail

    return 0;

fail:
    av_freep(&dest->rc_override);
    av_freep(&dest->intra_matrix);
    av_freep(&dest->inter_matrix);
    av_freep(&dest->extradata);
    av_freep(&dest->rc_eq);
    return AVERROR(ENOMEM);
}
#endif

#if LIBAVCODEC_VERSION_INT < ((55<<16)+(18<<8)+102)
#ifndef AV_CODEC_ID_H264
#define AV_CODEC_ID_H264 CODEC_ID_H264
#endif
#ifndef AV_CODEC_ID_MPEG2VIDEO
#define AV_CODEC_ID_MPEG2VIDEO CODEC_ID_MPEG2VIDEO
#endif
#ifndef AV_CODEC_ID_MPEG2VIDEO_XVMC
#define AV_CODEC_ID_MPEG2VIDEO_XVMC CODEC_ID_MPEG2VIDEO_XVMC
#endif
#ifndef AV_CODEC_ID_NONE
#define AV_CODEC_ID_NONE CODEC_ID_NONE
#endif
#endif

cMarkAdDecoder::cMarkAdDecoder(bool useH264, int Threads)
{
#if LIBAVCODEC_VERSION_INT < ((53<<16)+(7<<8)+1)
    avcodec_init();
#endif
    avcodec_register_all();

    last_qscale_table=NULL;
    skipframes=true;

    addPkt=false;
    noticeERRVID=false;

    cpu_set_t cpumask;
    uint len = sizeof(cpumask);
    int cpucount=1;
    if (sched_getaffinity(0,len,&cpumask)>=0)
    {
        cpucount=CPU_COUNT(&cpumask);
    }

    if (Threads==-1)
    {
        threadcount=cpucount;
    }
    else
    {
        threadcount=Threads;
    }

    int ver = avcodec_version();
    char libver[256];
    snprintf(libver,sizeof(libver),"%i.%i.%i",ver >> 16 & 0xFF,ver >> 8 & 0xFF,ver & 0xFF);
    isyslog("using libavcodec.so.%s with %i threads",libver,threadcount);

    if (ver!=LIBAVCODEC_VERSION_INT)
    {
        esyslog("libavcodec header version %s",AV_STRINGIFY(LIBAVCODEC_VERSION));
        esyslog("header and library mismatch, dont report decoder-bugs!");
    }

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(41<<8)+0)
    tsyslog("libavcodec config: %s",avcodec_configuration());
#endif

    if (((ver >> 16)<52) && (useH264))
    {
        esyslog("dont report bugs about H264, use libavcodec >= 52 instead!");
    }

    video_codec=NULL;
#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(51<<8)+100)
    AVCodecID video_codecid;
#else
    CodecID video_codecid;
#endif

    if (useH264)
    {
        video_codecid=AV_CODEC_ID_H264;
    }
    else
    {
        video_codecid=AV_CODEC_ID_MPEG2VIDEO_XVMC;
    }

    video_codec = avcodec_find_decoder(video_codecid);
    if ((!video_codec) && (video_codecid==AV_CODEC_ID_MPEG2VIDEO_XVMC))
    {
        // fallback to MPEG2VIDEO
        video_codecid=AV_CODEC_ID_MPEG2VIDEO;
        video_codec=avcodec_find_decoder(video_codecid);
    }

    if (video_codec)
    {
#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(51<<8)+100)
        video_context = avcodec_alloc_context3(NULL);
#else
        video_context = avcodec_alloc_context();
#endif
        if (video_context)
        {
            if (video_codec->capabilities & CODEC_CAP_TRUNCATED)
                video_context->flags|=CODEC_FLAG_TRUNCATED; // we do not send complete frames
            video_context->flags|=CODEC_FLAG_LOW_DELAY;
            video_context->flags2|=CODEC_FLAG2_FAST; // really?
            video_context->skip_idct=AVDISCARD_ALL;

            if (video_codecid!=AV_CODEC_ID_H264)
            {
                video_context->skip_frame=AVDISCARD_NONKEY; // just I-frames
            } else {
                video_context->flags2|=CODEC_FLAG2_CHUNKS;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(47<<8)+0)
                av_log_set_level(AV_LOG_FATAL); // silence decoder output
#else
                av_log_set_level(AV_LOG_QUIET);
#endif
            }
            video_context->codec_id = video_codecid;
            video_context->codec_type = AVMEDIA_TYPE_VIDEO;
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(5<<8)+0)
            int ret=avcodec_open2(video_context, video_codec, NULL);
#else
            int ret=avcodec_open(video_context, video_codec);
#endif
            if ((ret < 0) && (video_codecid==AV_CODEC_ID_MPEG2VIDEO_XVMC))
            {
                // fallback to MPEG2VIDEO
                video_codecid=AV_CODEC_ID_MPEG2VIDEO;
                video_codec=avcodec_find_decoder(video_codecid);
                if (video_codec)
                {
                    video_context->codec_type=AVMEDIA_TYPE_UNKNOWN;
                    video_context->codec_id=AV_CODEC_ID_NONE;
                    video_context->codec_tag=0;
#if (LIBAVCODEC_VERSION_MAJOR < 57)
                    memset(video_context->codec_name,0,sizeof(video_context->codec_name));
#endif
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(5<<8)+0)
                    video_context->thread_count=threadcount;
                    ret=avcodec_open2(video_context, video_codec, NULL);
#else
                    ret=avcodec_open(video_context, video_codec);
#endif
                }
                else
                {
                    ret=-1;
                }
            }
            if (ret < 0)
            {
                switch (video_codecid)
                {
                case AV_CODEC_ID_H264:
                    esyslog("could not open codec for H264");
                    break;
                case AV_CODEC_ID_MPEG2VIDEO_XVMC:
                    esyslog("could not open codec MPEG2 (XVMC)");
                    break;
                case AV_CODEC_ID_MPEG2VIDEO:
                    esyslog("could not open codec MPEG2");
                    break;
                default:
                    esyslog("could not open video codec");
                    break;
                }
                av_free(video_context);
                video_context=NULL;
            }
            else
            {
#if LIBAVCODEC_VERSION_INT < ((51<<16)+(55<<8)+0)
                isyslog("using codec %s",video_codec->name);
#else
                isyslog("using codec %s",video_codec->long_name);
#endif

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(22<<8)+2)
                if (video_context->hwaccel)
                {
                    isyslog("using hwaccel %s",video_context->hwaccel->name);
                }
#endif

#if ((LIBAVCODEC_VERSION_MICRO >  100) && (LIBAVCODEC_VERSION_INT < ((55<<16)+(45<<8)+101))) || \
    ((LIBAVCODEC_VERSION_MICRO <= 100) && (LIBAVCODEC_VERSION_INT < ((55<<16)+(28<<8)+1)))
                video_frame = avcodec_alloc_frame();
#else
                video_frame = av_frame_alloc();
#endif
                if (!video_frame)
                {
                    esyslog("could not allocate frame");
                    avcodec_close(video_context);
                    av_free(video_context);
                    video_context=NULL;
                }

#if LIBAVCODEC_VERSION_INT < ((53<<16)+(5<<8)+0)
                if ((threadcount>1) && (video_context))
                {
                    if (avcodec_thread_init(video_context,threadcount)==-1)
                    {
                        esyslog("cannot use %i threads, falling back to single thread operation",threadcount);
                        threadcount=1;
                    }
                }
#endif
            }
        }
        else
        {
            esyslog("could not allocate video context");
        }
    }
    else
    {
        switch (video_codecid)
        {
        case AV_CODEC_ID_H264:
            esyslog("codec for H264 not found");
            break;
        case AV_CODEC_ID_MPEG2VIDEO_XVMC:
            esyslog("codec for MPEG2 (XVMC) not found");
            break;
        case AV_CODEC_ID_MPEG2VIDEO:
            esyslog("codec for MPEG2 not found");
            break;
        default:
            esyslog("video codec not found");
            break;
        }
        video_context=NULL;
    }
}

cMarkAdDecoder::~cMarkAdDecoder()
{
    Clear();
    if (video_context)
    {
        avcodec_close(video_context);
        av_free(video_context);
        av_free(video_frame);
    }
}

bool cMarkAdDecoder::Clear()
{
    bool ret=true;
    if (video_context)
    {
        avcodec_flush_buffers(video_context);
        AVCodecContext *dest;
#if LIBAVCODEC_VERSION_INT >= ((54<<16)+(51<<8)+100)
        dest=avcodec_alloc_context3(NULL);
#else
        dest=avcodec_alloc_context();
#endif
        if (dest)
        {
            if (avcodec_copy_context(dest,video_context)!=0) ret=false;
        }
        else
        {
            ret=false;
        }
        avcodec_close(video_context);
        av_free(video_context);
        if (ret)
        {
            video_context=dest;
#if LIBAVCODEC_VERSION_INT >= ((53<<16)+(5<<8)+0)
            video_context->thread_count=threadcount;
            if (avcodec_open2(video_context,video_codec,NULL)<0) ret=false;
#else
            if (avcodec_open(video_context,video_codec)<0) ret=false;
#endif
        }
#if LIBAVCODEC_VERSION_INT < ((53<<16)+(5<<8)+0)
        if (threadcount>1)
        {
            if (avcodec_thread_init(video_context,threadcount)==-1)
            {
                video_context->execute=avcodec_default_execute;
                threadcount=1;
            }
        }
        else
        {
            video_context->execute=avcodec_default_execute;
        }
#endif
    }
    return ret;
}

bool cMarkAdDecoder::SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context, AVFrame *Video_Frame)
{
    if ((!maContext) || (!Video_Context) || (!Video_Frame)) return false;
    for (int i=0; i<4; i++)
    {
        if (Video_Frame->data[i])
        {
            maContext->Video.Data.Plane[i]=Video_Frame->data[i];
            maContext->Video.Data.PlaneLinesize[i]=Video_Frame->linesize[i];
            maContext->Video.Data.Valid=true;
        }
    }
    maContext->Video.Info.Height=Video_Context->height;
    maContext->Video.Info.Width=Video_Context->width;
    maContext->Video.Info.Pix_Fmt=Video_Context->pix_fmt;
    return true;
}

bool cMarkAdDecoder::DecodeVideo(MarkAdContext *maContext,uchar *pkt, int plen)
{
    if (!video_context) return false;
    if (!video_frame) return false;
    maContext->Video.Data.Valid=false;

    if (video_context->codec_id==AV_CODEC_ID_H264) {
        if (plen>=5) {
            if (((pkt[4] & 0x1F)==9) && (pkt[5]==0x10)) addPkt=true;
        }
        if (!addPkt) return false;
    }

    if (video_context->codec_id==AV_CODEC_ID_MPEG2VIDEO) {
        if (plen>=5) {
            if (!pkt[0] && !pkt[1] && (pkt[2]==1) && !pkt[3]  && ((pkt[5] & 8)==8)) addPkt=true;
        }
        if (!addPkt) return false;
    }

    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=pkt;
    avpkt.size=plen;

    // decode video
    int video_frame_ready=0;
    int len,ret=false;

    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        len=avcodec_decode_video(video_context,video_frame,&video_frame_ready,
                                 avpkt.data,avpkt.size);
#else
        len=avcodec_decode_video2(video_context,video_frame,&video_frame_ready,
                                  &avpkt);
#endif
        if (len<0)
        {
            if (!noticeERRVID)
            {
                esyslog("error decoding video");
                noticeERRVID=true;
                addPkt=false;
            }
            break;
        }
        else
        {
            avpkt.size-=len;
            avpkt.data+=len;
        }
        if (video_frame_ready)
        {
            if (SetVideoInfos(maContext,video_context,video_frame)) ret=true;
        }
        if (!len) break;
    }
    if (ret) addPkt=false;
    return ret;
}
