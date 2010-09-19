/*
 * decoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdint.h>
#include <sched.h>
#include <sys/types.h>
#include <string.h>
#include <cstdlib>

#ifndef DECLARE_ALIGNED
#define DECLARE_ALIGNED(n,t,v) t v __attribute__ ((aligned (n)))
#endif

#ifndef CPU_COUNT
#define CPU_COUNT(i) 1 // very crude ;)
#endif

#include "decoder.h"

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
    dest->hwaccel         = NULL;
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

cMarkAdDecoder::cMarkAdDecoder(bool useH264, bool useMP2, bool hasAC3)
{
    avcodec_init();
    avcodec_register_all();

    last_qscale_table=NULL;
    skipframes=true;

    cpu_set_t cpumask;
    uint len = sizeof(cpumask);
    int cpucount;
    if (sched_getaffinity(0,len,&cpumask)<0)
    {
        cpucount=1;
    }
    else
    {
        cpucount=CPU_COUNT(&cpumask);
    }

    int ver = avcodec_version();
    char libver[256];
    snprintf(libver,sizeof(libver),"%i.%i.%i",ver >> 16 & 0xFF,ver >> 8 & 0xFF,ver & 0xFF);
    isyslog("using libavcodec.so.%s with %i threads",libver,cpucount);

    if (ver!=LIBAVCODEC_VERSION_INT)
    {
        esyslog("libavcodec header version %s",AV_STRINGIFY(LIBAVCODEC_VERSION));
        esyslog("header and library mismatch, decoding disabled");
        video_context=NULL;
        ac3_context=NULL;
        mp2_context=NULL;
        audiobuf=NULL;
        return;
    }

    if (((ver >> 16)<52) && (useH264))
    {
        esyslog("dont report bugs about H264, use libavcodec >= 52 instead!");
    }

    if (useMP2)
    {
        CodecID mp2_codecid=CODEC_ID_MP2;
        AVCodec *mp2_codec= avcodec_find_decoder(mp2_codecid);
        if (mp2_codec)
        {
            mp2_context = avcodec_alloc_context();
            if (mp2_context)
            {
                mp2_context->thread_count=cpucount;
                mp2_context->codec_id = mp2_codecid;
                mp2_context->codec_type = CODEC_TYPE_AUDIO;
                if (avcodec_open(mp2_context, mp2_codec) < 0)
                {
                    esyslog("could not open codec for MP2");
                    av_free(mp2_context);
                    mp2_context=NULL;
                }
            }
            else
            {
                esyslog("could not allocate mp2 context");
            }
        }
        else
        {
            esyslog("codec for MP2 not found");
            mp2_context=NULL;
        }
    }
    else
    {
        mp2_context=NULL;
    }

    if (hasAC3)
    {
        CodecID ac3_codecid=CODEC_ID_AC3;
        AVCodec *ac3_codec= avcodec_find_decoder(ac3_codecid);
        if (ac3_codec)
        {
            ac3_context = avcodec_alloc_context();
            if (ac3_context)
            {
                ac3_context->thread_count=cpucount;
                ac3_context->codec_id = ac3_codecid;
                ac3_context->codec_type = CODEC_TYPE_AUDIO;
                if (avcodec_open(ac3_context, ac3_codec) < 0)
                {
                    esyslog("could not open codec for AC3");
                    av_free(ac3_context);
                    ac3_context=NULL;
                }
            }
            else
            {
                esyslog("could not allocate ac3 context");
            }
        }
        else
        {
            esyslog("codec for AC3 not found");
            ac3_context=NULL;
        }
    }
    else
    {
        ac3_context=NULL;
    }

    video_codec=NULL;
    CodecID video_codecid;

    if (useH264)
    {
        video_codecid=CODEC_ID_H264;
    }
    else
    {
        video_codecid=CODEC_ID_MPEG2VIDEO_XVMC;
    }

    video_codec = avcodec_find_decoder(video_codecid);
    if ((!video_codec) && (video_codecid==CODEC_ID_MPEG2VIDEO_XVMC))
    {
        // fallback to MPEG2VIDEO
        video_codecid=CODEC_ID_MPEG2VIDEO;
        video_codec=avcodec_find_decoder(video_codecid);
    }

    if (video_codec)
    {
        video_context = avcodec_alloc_context();
        if (video_context)
        {
            video_context->thread_count=cpucount;
            if (video_codec->capabilities & CODEC_CAP_TRUNCATED)
                video_context->flags|=CODEC_FLAG_TRUNCATED; // we do not send complete frames

            video_context->flags2|=CODEC_FLAG2_FAST; // really?

            video_context->skip_idct=AVDISCARD_ALL;

            if (video_codecid==CODEC_ID_H264)
            {
                video_context->flags2|=CODEC_FLAG2_CHUNKS; // needed for H264!
                av_log_set_level(AV_LOG_FATAL); // H264 decoder is very chatty
            }
            else
            {
                video_context->skip_frame=AVDISCARD_NONKEY; // just I-frames
            }
            video_context->codec_id = video_codecid;
            video_context->codec_type = CODEC_TYPE_VIDEO;
            int ret=avcodec_open(video_context, video_codec);
            if ((ret < 0) && (video_codecid==CODEC_ID_MPEG2VIDEO_XVMC))
            {
                // fallback to MPEG2VIDEO
                video_codecid=CODEC_ID_MPEG2VIDEO;
                video_codec=avcodec_find_decoder(video_codecid);
                if (video_codec)
                {
                    video_context->codec_type=CODEC_TYPE_UNKNOWN;
                    video_context->codec_id=CODEC_ID_NONE;
                    video_context->codec_tag=0;
                    memset(video_context->codec_name,0,sizeof(video_context->codec_name));
                    ret=avcodec_open(video_context, video_codec);
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
                case CODEC_ID_H264:
                    esyslog("could not open codec for H264");
                    break;
                case CODEC_ID_MPEG2VIDEO_XVMC:
                    esyslog("could not open codec MPEG2 (XVMC)");
                    break;
                case CODEC_ID_MPEG2VIDEO:
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
                isyslog("using codec %s",video_codec->long_name);

#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(22<<8)+2)
                if (video_context->hwaccel)
                {
                    isyslog("using hwaccel %s",video_context->hwaccel->name);
                }
#endif

                video_frame = avcodec_alloc_frame();
                if (!video_frame)
                {
                    esyslog("could not allocate frame");
                    avcodec_close(video_context);
                    av_free(video_context);
                    video_context=NULL;
                }
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
        case CODEC_ID_H264:
            esyslog("codec for H264 not found");
            break;
        case CODEC_ID_MPEG2VIDEO_XVMC:
            esyslog("codec for MPEG2 (XVMC) not found");
            break;
        case CODEC_ID_MPEG2VIDEO:
            esyslog("codec for MPEG2 not found");
            break;
        default:
            esyslog("video codec not found");
            break;
        }
        video_context=NULL;
    }

    if ((ac3_context) || (mp2_context))
    {
        audiobuf=(int16_t*) malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE);
    }
    else
    {
        audiobuf=NULL;
    }
    initial_coded_picture=-1;
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

    if (ac3_context)
    {
        avcodec_close(ac3_context);
        av_free(ac3_context);
    }

    if (mp2_context)
    {
        avcodec_close(mp2_context);
        av_free(mp2_context);
    }
    if (audiobuf) free(audiobuf);
}

bool cMarkAdDecoder::Clear()
{
    bool ret=true;
    if (video_context)
    {
        avcodec_flush_buffers(video_context);
        AVCodecContext *dest;
        dest=avcodec_alloc_context();
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
            if (avcodec_open(video_context,video_codec)<0) ret=false;
        }
    }
    if (ac3_context) avcodec_flush_buffers(ac3_context);
    if (mp2_context) avcodec_flush_buffers(mp2_context);
    return ret;
}

bool cMarkAdDecoder::SetAudioInfos(MarkAdContext *maContext, AVCodecContext *Audio_Context)
{
    if ((!maContext) || (!Audio_Context)) return false;

    maContext->Audio.Info.SampleRate = Audio_Context->sample_rate;
    maContext->Audio.Info.Channels = Audio_Context->channels;
    maContext->Audio.Data.SampleBuf=audiobuf;
    maContext->Audio.Data.SampleBufLen=audiobufsize;
    maContext->Audio.Data.Valid=true;
    return true;
}

bool cMarkAdDecoder::DecodeMP2(MarkAdContext *maContext, uchar *espkt, int eslen)
{
    if (!mp2_context) return false;
    maContext->Audio.Data.Valid=false;
    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=espkt;
    avpkt.size=eslen;

    audiobufsize=AVCODEC_MAX_AUDIO_FRAME_SIZE;
    int ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(mp2_context,audiobuf,&audiobufsize,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(mp2_context,audiobuf,&audiobufsize,&avpkt);
#endif
        if (len<0)
        {
            esyslog("error decoding mp2");
            break;
        }
        if (audiobufsize>0)
        {
            SetAudioInfos(maContext,mp2_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
    }
    return ret;
}

bool cMarkAdDecoder::DecodeAC3(MarkAdContext *maContext, uchar *espkt, int eslen)
{
    if (!ac3_context) return false;
    maContext->Audio.Data.Valid=false;
    AVPacket avpkt;
#if LIBAVCODEC_VERSION_INT >= ((52<<16)+(25<<8)+0)
    av_init_packet(&avpkt);
#else
    memset(&avpkt,0,sizeof(avpkt));
    avpkt.pts = avpkt.dts = AV_NOPTS_VALUE;
    avpkt.pos = -1;
#endif
    avpkt.data=espkt;
    avpkt.size=eslen;

    int ret=false;
    while (avpkt.size>0)
    {
        audiobufsize=AVCODEC_MAX_AUDIO_FRAME_SIZE;
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(ac3_context,audiobuf,&audiobufsize,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(ac3_context,audiobuf,&audiobufsize,&avpkt);
#endif
        if (len<0)
        {
            esyslog("error decoding ac3");
            break;
        }
        if (audiobufsize>0)
        {
            SetAudioInfos(maContext,ac3_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
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
    return true;
}

bool cMarkAdDecoder::DecodeVideo(MarkAdContext *maContext,uchar *pkt, int plen)
{
    if (!video_context) return false;
    if (!video_frame) return false;
    maContext->Video.Data.Valid=false;

    if ((video_context->codec_id==CODEC_ID_H264) && (!video_context->skip_frame))
    {
        // with H264 we cannot set skip_frame just to NONKEY, is depends on Interlaced...
        if (maContext->Video.Info.Height)
        {
            if (maContext->Video.Info.Interlaced)
            {
                video_context->skip_frame=AVDISCARD_BIDIR; // just P/I-frames
                video_context->skip_loop_filter=AVDISCARD_BIDIR;
            }
            else
            {
                video_context->skip_frame=AVDISCARD_NONKEY; // just I-frames
                video_context->skip_loop_filter=AVDISCARD_NONKEY;
            }
        }
        else
        {
            return false;
        }
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
            esyslog("error decoding video");
            break;
        }
        else
        {
            avpkt.size-=len;
            avpkt.data+=len;
        }
        if (video_frame_ready)
        {
            if (video_context->skip_frame!=AVDISCARD_DEFAULT)
            {
                if (last_qscale_table!=video_frame->qscale_table)
                {
                    if (SetVideoInfos(maContext,video_context,video_frame)) ret=true;
                    last_qscale_table=video_frame->qscale_table;
                }
            }
            else
            {
                if (SetVideoInfos(maContext,video_context,video_frame)) ret=true;
            }
        }
    }
    return ret;
}
