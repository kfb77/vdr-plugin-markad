/*
 * decoder.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "decoder.h"

cMarkAdDecoder::cMarkAdDecoder(int RecvNumber, bool useH264, bool hasAC3)
{
    recvnumber=RecvNumber;
    avcodec_init();
    avcodec_register_all();

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

    isyslog("markad [%i]: using libavcodec.so.%s with %i threads",recvnumber,
            AV_STRINGIFY(LIBAVCODEC_VERSION),cpucount);

    CodecID mp2_codecid=CODEC_ID_MP2;
    AVCodec *mp2_codec= avcodec_find_decoder(mp2_codecid);
    if (mp2_codec)
    {
        mp2_context = avcodec_alloc_context();
        if (mp2_context)
        {
            mp2_context->thread_count=cpucount;
            if (avcodec_open(mp2_context, mp2_codec) < 0)
            {
                esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,mp2_codecid);
                av_free(mp2_context);
                mp2_context=NULL;
            }
        }
        else
        {
            esyslog("markad [%i]: could not allocate mp2 context",recvnumber);
        }
    }
    else
    {
        esyslog("markad [%i]: codec 0x%05x not found",recvnumber,mp2_codecid);
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
                if (avcodec_open(ac3_context, ac3_codec) < 0)
                {
                    esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,ac3_codecid);
                    av_free(ac3_context);
                    ac3_context=NULL;
                }
            }
            else
            {
                esyslog("markad [%i]: could not allocate ac3 context",recvnumber);
            }
        }
        else
        {
            esyslog("markad [%i]: codec 0x%05x not found",recvnumber,ac3_codecid);
            ac3_context=NULL;
        }
    }
    else
    {
        ac3_context=NULL;
    }

    AVCodec *video_codec=NULL;
    CodecID video_codecid;

    if (useH264)
    {
        video_codecid=CODEC_ID_H264;
    }
    else
    {
        video_codecid=CODEC_ID_MPEG2VIDEO;
    }

    video_codec = avcodec_find_decoder(video_codecid);
    if (video_codec)
    {
        video_context = avcodec_alloc_context();
        if (video_context)
        {
            video_context->thread_count=cpucount;
            if (video_codec->capabilities & CODEC_CAP_TRUNCATED)
                video_context->flags|=CODEC_FLAG_TRUNCATED; // we do not send complete frames
            video_context->flags|=CODEC_FLAG_EMU_EDGE; // now linesize should be the same as width
            video_context->flags2|=CODEC_FLAG2_CHUNKS; // needed for H264!
            video_context->flags2|=CODEC_FLAG2_FAST; // really?

            if (avcodec_open(video_context, video_codec) < 0)
            {
                esyslog("markad [%i]: could not open codec 0x%05x",recvnumber,video_codecid);
                av_free(video_context);
                video_context=NULL;
            }
            else
            {
                video_frame = avcodec_alloc_frame();
                if (!video_frame)
                {
                    esyslog("markad [%i]: could not allocate frame",recvnumber);
                    avcodec_close(video_context);
                    av_free(video_context);
                    video_context=NULL;
                }
            }
        }
        else
        {
            esyslog("markad [%i]: could not allocate video context",recvnumber);
        }
    }
    else
    {
        esyslog("markad [%i]: codec 0x%05x not found",recvnumber,video_codecid);
        video_context=NULL;
    }
}

cMarkAdDecoder::~cMarkAdDecoder()
{
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
    SetVideoInfos(NULL,NULL,NULL,NULL);
}

bool cMarkAdDecoder::DecodeMP2(MarkAdContext *maContext, uchar *espkt, int eslen)
{
    if (!mp2_context) return false;
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

    DECLARE_ALIGNED(16,char,outbuf[AVCODEC_MAX_AUDIO_FRAME_SIZE]);
    int outbuf_size=AVCODEC_MAX_AUDIO_FRAME_SIZE;
    int ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(mp2_context,(short *) &outbuf,&outbuf_size,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(mp2_context,(short *) &outbuf,&outbuf_size,&avpkt);
#endif
        if (len<0)
        {
            esyslog("markad [%i]: error decoding mp2",recvnumber);
            break;
        }
        else
        {
            SetAudioInfos(maContext,ac3_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
    }
    return ret;
}

bool cMarkAdDecoder::SetAudioInfos(MarkAdContext *maContext, AVCodecContext *Audio_Context)
{
    if ((!maContext) || (!Audio_Context)) return false;

    maContext->Audio.Info.Channels = Audio_Context->channels;
    return true;
}

bool cMarkAdDecoder::DecodeAC3(MarkAdContext *maContext, uchar *espkt, int eslen)
{
    if (!ac3_context) return false;
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

    DECLARE_ALIGNED(16,char,outbuf[AVCODEC_MAX_AUDIO_FRAME_SIZE]);
    int outbuf_size=AVCODEC_MAX_AUDIO_FRAME_SIZE;
    int ret=false;
    while (avpkt.size>0)
    {
#if LIBAVCODEC_VERSION_INT < ((52<<16)+(25<<8)+0)
        int len=avcodec_decode_audio2(ac3_context,(short *) &outbuf,&outbuf_size,
                                      avpkt.data,avpkt.size);
#else
        int len=avcodec_decode_audio3(ac3_context,(short *) &outbuf,&outbuf_size,&avpkt);
#endif
        if (len<0)
        {
            esyslog("markad [%i]: error decoding ac3",recvnumber);
            break;
        }
        else
        {
            SetAudioInfos(maContext,ac3_context);
            ret=true;
            avpkt.size-=len;
            avpkt.data+=len;
        }
    }
    return ret;
}

void cMarkAdDecoder::PAR2DAR(AVRational a, AVRational *erg)
{
    av_reduce(&erg->num,&erg->den,video_context->width*a.num,
              video_context->height*a.den,1024*1024);
}

bool cMarkAdDecoder::SetVideoInfos(MarkAdContext *maContext,AVCodecContext *Video_Context, AVFrame *Video_Frame, AVRational *DAR)
{
    if ((!maContext) || (!Video_Context) || (!Video_Frame)) return false;
    maContext->Video.Data.Valid=false;
    for (int i=0; i<4; i++)
    {
        if (Video_Frame->data[i])
        {
            maContext->Video.Data.Plane[i]=Video_Frame->data[i];
            maContext->Video.Data.PlaneLinesize[i]=Video_Frame->linesize[i];
        }
    }
    maContext->Video.Info.Height=Video_Context->height;
    maContext->Video.Info.Width=Video_Context->width;

    switch (Video_Frame->pict_type)
    {
    case FF_I_TYPE:
        maContext->Video.Info.Pict_Type=MA_I_TYPE;
        break;
    case FF_P_TYPE:
        maContext->Video.Info.Pict_Type=MA_P_TYPE;
        break;
    case FF_B_TYPE:
        maContext->Video.Info.Pict_Type=MA_B_TYPE;
        break;
    case FF_S_TYPE:
        maContext->Video.Info.Pict_Type=MA_S_TYPE;
        break;
    case FF_SI_TYPE:
        maContext->Video.Info.Pict_Type=MA_SI_TYPE;
        break;
    case FF_SP_TYPE:
        maContext->Video.Info.Pict_Type=MA_SP_TYPE;
        break;
    case FF_BI_TYPE:
        maContext->Video.Info.Pict_Type=MA_BI_TYPE;
        break;
    default:
        maContext->Video.Info.Pict_Type=0;
    }

    if (DAR)
    {
        maContext->Video.Info.AspectRatio.Num=DAR->num;
        maContext->Video.Info.AspectRatio.Den=DAR->den;
    }

    maContext->Video.Data.Valid=true;
    return true;
}

bool cMarkAdDecoder::DecodeVideo(MarkAdContext *maContext,uchar *pkt, int plen)
{
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
            esyslog("markad [%i]: error decoding video",recvnumber);
            break;
        }
        else
        {
            avpkt.size-=len;
            avpkt.data+=len;
        }
        if (video_frame_ready)
        {
            AVRational dar;
            PAR2DAR(video_context->sample_aspect_ratio,&dar);
            if (SetVideoInfos(maContext,video_context,video_frame,&dar)) ret=true;
        }
    }
    return ret;
}
