/*
 * decoder_new.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "decoder_new.h"
extern "C" {
#include "debug.h"
}


void AVlog(void *ptr, int level, const char* fmt, va_list vl){
    if (level <= AVLOGLEVEL) {
        char *logMsg = NULL;
        int rc = 0;
        rc = vasprintf(&logMsg, fmt, vl);
        if (rc == -1) {
            dsyslog("AVlog(): error in vasprintf");
            return;
        }
        ALLOC(strlen(logMsg)+1, "logMsg");

        if (logMsg[strlen(logMsg)-1] == '\n') {
            FREE(strlen(logMsg)+1, "logMsg");
            logMsg[strlen(logMsg)-1] = 0;
            logMsg = (char *) realloc(logMsg, strlen(logMsg)+1);
            if (!logMsg) {
                dsyslog("AVlog(): error in realloc");
                return;
            }
            ALLOC(strlen(logMsg)+1, "logMsg");
        }

        if ((strcmp(logMsg, "co located POCs unavailable") == 0) || // this will happen with h.264 coding because of partitial decoding
            (strcmp(logMsg, "mmco: unref short failure") == 0) ||
            (strcmp(logMsg, "number of reference frames (0+5) exceeds max (4; probably corrupt input), discarding one") == 0)) {
                tsyslog("AVlog(): %s",logMsg);
        }
        else dsyslog("AVlog(): %s",logMsg);

        FREE(strlen(logMsg)+1, "logMsg");
        free(logMsg);
    }
    return;
}


cDecoder::cDecoder(int threads) {
    av_log_set_level(AVLOGLEVEL);
    av_log_set_callback(AVlog);
    av_init_packet(&avpkt);
    codec = NULL;
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    dsyslog("cDecoder::cDecoder(): init with %i threads", threads);
    threadCount = threads;
}


cDecoder::~cDecoder() {
    av_packet_unref(&avpkt);
    if (avctx) {
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            if (codecCtxArray[streamIndex]) {
                FREE(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");
                avcodec_free_context(&codecCtxArray[streamIndex]);
            }
        }
        if (codecCtxArray) {
            FREE(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
            free(codecCtxArray);
        }
        dsyslog("cDecoder::~cDecoder(): close avformat context");
        avformat_close_input(&avctx);
    }
    if (recordingDir) {
        FREE(strlen(recordingDir), "recordingDir");
        free(recordingDir);
    }
    iFrameInfoVector.clear();
}


bool cDecoder::DecodeDir(const char * recDir) {
    if (!recDir) return false;
    char *filename;
    if ( ! recordingDir ) {
        if (asprintf(&recordingDir,"%s",recDir)==-1) {
            dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
            return false;
        }
        ALLOC(strlen(recordingDir), "recordingDir");
    }
    fileNumber++;
    if (asprintf(&filename,"%s/%05i.ts",recDir,fileNumber)==-1) {
        dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename), "filename");
    bool ret = DecodeFile(filename);
    FREE(strlen(filename), "filename");
    free(filename);
    return ret;
}


void cDecoder::Reset(){
    fileNumber=0;
    framenumber=0;
    msgGetFrameInfo=false;
}


AVFormatContext *cDecoder::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoder::GetAVCodecContext() {
    return codecCtxArray;
}


bool cDecoder::DecodeFile(const char * filename) {
    if (!filename) return false;
    AVFormatContext *avctxNextFile = NULL;
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    if (avformat_open_input(&avctxNextFile, filename, NULL, NULL) == 0) {
        dsyslog("cDecoder::DecodeFile(): start decode file %s",filename);
        if (avctx) avformat_close_input(&avctx);
        avctx = avctxNextFile;
    }
    else {
        if (fileNumber <= 1) dsyslog("cDecoder::DecodeFile(): Could not open source file %s", filename);
        return false;
    }
    if (avformat_find_stream_info(avctx, NULL) <0) {
        dsyslog("cDecoder::DecodeFile(): Could not get stream infos %s", filename);
        return false;
    }

    if (codecCtxArray) {   // free before aloc for new file
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            if (codecCtxArray[streamIndex]) {
                FREE(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");
                avcodec_free_context(&codecCtxArray[streamIndex]);
            }
        }
        FREE(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
        free(codecCtxArray);
        codecCtxArray = NULL;
    }

    codecCtxArray = (AVCodecContext **) malloc(sizeof(AVCodecContext *) * avctx->nb_streams);
    ALLOC(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
    memset(codecCtxArray, 0, sizeof(AVCodecContext *) * avctx->nb_streams);

    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        AVCodecID codec_id = avctx->streams[streamIndex]->codecpar->codec_id;
        codec=avcodec_find_decoder(codec_id);
#else
        AVCodecID codec_id = avctx->streams[streamIndex]->codec->codec_id;
        codec=avcodec_find_decoder(codec_id);
#endif
        if (!codec) {
            if (codec_id == 100359) {  // not supported by libavcodec
                dsyslog("cDecoder::DecodeFile(): ignore unsupported subtitle codec for stream %i codec id %d", streamIndex, codec_id);
                continue;
            }
            else {
                dsyslog("cDecoder::DecodeFile(): could not find decoder for stream %i codec id %i", streamIndex, codec_id);
                return false;
            }
        }

        if (msgDecodeFile) dsyslog("cDecoder::DecodeFile(): using decoder for stream %i: %s", streamIndex, codec->long_name);
        codecCtxArray[streamIndex]=avcodec_alloc_context3(codec);
        if (!codecCtxArray[streamIndex]) {
            dsyslog("cDecoder::DecodeFile(): avcodec_alloc_context3 failed");
            return false;
        }
        ALLOC(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avcodec_parameters_to_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codecpar) < 0)
#else
        if (avcodec_copy_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codec) < 0)
#endif
        {
            dsyslog("cDecoder::DecodeFile(): avcodec_parameters_to_context failed");
            return false;
        }
        codecCtxArray[streamIndex]->thread_count = threadCount;
        if (avcodec_open2(codecCtxArray[streamIndex], codec, NULL) < 0) {
            dsyslog("cDecoder::DecodeFile(): avcodec_open2 failed");
            return false;
        }
    }
    msgDecodeFile=false;
    return true;
}


int cDecoder::GetVideoType() {
    if (!avctx) return 0;
    for (unsigned int i = 0; i < avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            switch (avctx->streams[i]->codecpar->codec_id) {
                case AV_CODEC_ID_MPEG2VIDEO:
                    dsyslog("cDecoder::GetVideoType(): found H.262 Video");
                    return MARKAD_PIDTYPE_VIDEO_H262;
                    break;
                case AV_CODEC_ID_H264:
                    dsyslog("cDecoder::GetVideoType(): found H.264 Video");
                    return MARKAD_PIDTYPE_VIDEO_H264;
                    break;
                case AV_CODEC_ID_H265:
                    dsyslog("cDecoder::GetVideoType(): found H.265 Video");
                    return MARKAD_PIDTYPE_VIDEO_H265;
                    break;
                default:
                    dsyslog("cDecoder::GetVideoType(): unknown coded id %i", avctx->streams[i]->codecpar->codec_id);
                    return 0;
            }
        }
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (avctx->streams[i]->codec->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                dsyslog("cDecoder::GetVideoType(): found H.262 Video");
                return MARKAD_PIDTYPE_VIDEO_H262;
            }
            if (avctx->streams[i]->codec->codec_id == AV_CODEC_ID_H264) {
                dsyslog("cDecoder::GetVideoType(): found H.264 Video");
                return MARKAD_PIDTYPE_VIDEO_H264;
            }
           dsyslog("cDecoder::GetVideoType(): unknown coded id %i", avctx->streams[i]->codec->codec_id);
           return 0;
        }
#endif
    }
    dsyslog("cDecoder::GetVideoType(): failed");
    return 0;
}


int cDecoder::GetVideoHeight() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return avctx->streams[i]->codecpar->height;
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            return avctx->streams[i]->codec->height;
#endif
        }
    }
    dsyslog("cDecoder::GetVideoHeight(): failed");
    return 0;
}


int cDecoder::GetVideoWidth() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return avctx->streams[i]->codecpar->width;
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            return avctx->streams[i]->codec->width;
#endif
        }
    }
    dsyslog("cDecoder::GetVideoWidth(): failed");
    return 0;
}


int cDecoder::GetVideoFramesPerSecond() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
            return av_q2d(avctx->streams[i]->avg_frame_rate);
        }
    }
    dsyslog("cDecoder::GetVideoFramesPerSecond(): could not find average frame rate");
    return 0;
}


int cDecoder::GetVideoRealFrameRate() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            return av_q2d(avctx->streams[i]->r_frame_rate);
#else
    #if LIBAVCODEC_VERSION_INT <= ((56<<16)+(1<<8)+0)    // Rasbian Jessie
            AVStream *st = avctx->streams[i];
            AVRational r_frame_rate;
            if ( st->codec->time_base.den * (int64_t) st->time_base.num <= st->codec->time_base.num * st->codec->ticks_per_frame * (int64_t) st->time_base.den) {
                r_frame_rate.num = st->codec->time_base.den;
                r_frame_rate.den = st->codec->time_base.num * st->codec->ticks_per_frame;
            }
            else {
                r_frame_rate.num = st->time_base.den;
                r_frame_rate.den = st->time_base.num;
            }
            return av_q2d(r_frame_rate);
    #else
            return av_q2d(av_stream_get_r_frame_rate(avctx->streams[i]));
    #endif
#endif
        }
    }
    dsyslog("cDecoder::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoder::GetNextFrame() {
    if (!avctx) return false;
    iFrameData.Valid=false;
    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
       if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
       if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
             framenumber++;
             if (isVideoIFrame()) {
                 iFrameCount++;
                 if ((iFrameInfoVector.empty()) || (framenumber > iFrameInfoVector.back().iFrameNumber)) {
                     if (avpkt.pts != AV_NOPTS_VALUE) {   // store a iframe number pts index
                         int64_t tmp_pts = avpkt.pts - avctx->streams[avpkt.stream_index]->start_time;
                         if ( tmp_pts < 0 ) { tmp_pts += 0x200000000; }   // libavodec restart at 0 if pts greater than 0x200000000
                         int64_t pts_time_ms=tmp_pts*av_q2d(avctx->streams[avpkt.stream_index]->time_base)*100;
                         iFrameInfo newFrameInfo;
                         newFrameInfo.fileNumber=fileNumber;
                         newFrameInfo.iFrameNumber=framenumber;
                         newFrameInfo.pts_time_ms=pts_time_ms_LastFile+pts_time_ms;
                         iFrameInfoVector.push_back(newFrameInfo);
                     }
                     else dsyslog("cDecoder::GetNextFrame(): failed to get pts for frame %d", framenumber);
                 }
             }
        }
        return true;
    }
    pts_time_ms_LastFile += iFrameInfoVector.back().pts_time_ms;
    dsyslog("cDecoder::GetNextFrame(): start time next file %" PRId64, pts_time_ms_LastFile);
    return false;
}

AVPacket *cDecoder::GetPacket() {
    return &avpkt;
}


bool cDecoder::SeekToFrame(int iFrame) {
    if (!avctx) return false;
    dsyslog("cDecoder::SeekToFrame(): (%d)", iFrame);
    if (framenumber > iFrame) {
        dsyslog("cDecoder::SeekToFrame(): could not seek backward");
        return false;
    }
    while (framenumber < iFrame) {
        if (!this->GetNextFrame())
            if (!this->DecodeDir(recordingDir)) {
                dsyslog("cDecoder::SeekFrame(): failed for frame (%d) at frame (%d)", iFrame, framenumber);
                return false;
        }
    }
    return true;
}


AVFrame *cDecoder::DecodePacket(AVFormatContext *avctx, AVPacket *avpkt) {
    if (!avctx) return NULL;
    if (!avpkt) return NULL;
    AVFrame *avFrame = NULL;
//    tsyslog("cDecoder::DecodePacket(): framenumber %li pts %ld dts %ld",framenumber, avpkt->pts, avpkt->dts);
    avFrame = av_frame_alloc();
    if (!avFrame) {
        dsyslog("cDecoder::DecodePacket(): av_frame_alloc failed");
        return NULL;
    }
    ALLOC(sizeof(*avFrame), "avFrame");
    if (isVideoPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        avFrame->height=avctx->streams[avpkt->stream_index]->codecpar->height;
        avFrame->width=avctx->streams[avpkt->stream_index]->codecpar->width;
        avFrame->format=codecCtxArray[avpkt->stream_index]->pix_fmt;
#else
        avFrame->height=avctx->streams[avpkt->stream_index]->codec->height;
        avFrame->width=avctx->streams[avpkt->stream_index]->codec->width;
        avFrame->format=codecCtxArray[avpkt->stream_index]->pix_fmt;
#endif
    }
    else if (isAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        avFrame->nb_samples=av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codecpar->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codecpar->channel_layout;
        avFrame->format=avctx->streams[avpkt->stream_index]->codecpar->format;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codecpar->sample_rate;
#elif LIBAVCODEC_VERSION_INT >= ((56<<16)+(26<<8)+100)
        avFrame->nb_samples=av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format=codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codec->sample_rate;
#else  // Raspbian Jessie
        avFrame->nb_samples=av_popcount64(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format=codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codec->sample_rate;
#endif
    }
    else {
        dsyslog("cDecoder::DecodePacket(): stream type not supported");
        FREE(sizeof(*avFrame), "avFrame");   // test if avFrame not NULL above
        av_frame_free(&avFrame);
        return NULL;
    }
    int rc=av_frame_get_buffer(avFrame,32);
    if (rc != 0) {
        dsyslog("cDecoder::DecodePacket(): av_frame_get_buffer failed rc=%i", rc);
        if (avFrame) {
            FREE(sizeof(*avFrame), "avFrame");
            av_frame_free(&avFrame);
        }
        return NULL;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    rc=avcodec_send_packet(codecCtxArray[avpkt->stream_index],avpkt);
    if (rc  < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error EAGAIN at frame %d", framenumber);
                break;
            case AVERROR(ENOMEM):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error ENOMEM at frame %d", framenumber);
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket():GetFrameInfo(): avcodec_send_packet error EINVAL at frame %d", framenumber);
                break;
            case AVERROR_INVALIDDATA:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AVERROR_INVALIDDATA at frame %d", framenumber);
                break;
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            case AAC_AC3_PARSE_ERROR_SYNC:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC at frame %d", framenumber);
                break;
#endif
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet failed with rc=%d at frame %d",rc,framenumber);
                break;
            }
        if (avFrame) {
            FREE(sizeof(*avFrame), "avFrame");
            av_frame_free(&avFrame);
        }
        return NULL;
    }
    rc = avcodec_receive_frame(codecCtxArray[avpkt->stream_index],avFrame);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                tsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EAGAIN at frame %d", framenumber);
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EINVAL at frame %d", framenumber);
                break;
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame: decode of frame (%d) failed with return code %i", framenumber, rc);
                break;
        }
        if (avFrame) {
            FREE(sizeof(*avFrame), "avFrame");
            av_frame_free(&avFrame);
        }
        return NULL;
    }
#else
    int frame_ready=0;
    if (isVideoPacket()) {
        rc=avcodec_decode_video2(codecCtxArray[avpkt->stream_index],avFrame,&frame_ready,avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_video2 decode of frame (%li) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
            if (avFrame) {
                FREE(sizeof(*avFrame), "avFrame");
                av_frame_free(&avFrame);
            }
            return NULL;
        }
    }
    else if (isAudioPacket()) {
        rc=avcodec_decode_audio4(codecCtxArray[avpkt->stream_index],avFrame,&frame_ready,avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_audio4 of frame (%li) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
            if (avFrame) {
                FREE(sizeof(*avFrame), "avFrame");
                av_frame_free(&avFrame);
            }
            return NULL;
        }
    }

    else {
       dsyslog("cDecoder::DecodePacket(): packet type of stream %i not supported", avpkt->stream_index);
       if (avFrame) {
            FREE(sizeof(*avFrame), "avFrame");
            av_frame_free(&avFrame);
        }
       return NULL;
    }

    if ( !frame_ready ) {
        stateEAGAIN=true;
        if (avFrame) {
            FREE(sizeof(*avFrame), "avFrame");
            av_frame_free(&avFrame);
        }
        return NULL;
    }
#endif
    return avFrame;
}


bool cDecoder::GetFrameInfo(MarkAdContext *maContext) {
    if (!avctx) return false;
    AVFrame *avFrame = NULL;
    iFrameData.Valid=false;
    if (isVideoPacket()) {
        if (isVideoIFrame() || stateEAGAIN) {
            avFrame=this->DecodePacket(avctx, &avpkt);
            if (avFrame) {
                stateEAGAIN=false;
                if (avFrame->interlaced_frame != interlaced_frame) {
                    dsyslog("cDecoder::GetFrameInfo(): found %s video format",(avFrame->interlaced_frame) ? "interlaced" : "non interlaced");
                    interlaced_frame=avFrame->interlaced_frame;
                }
                for (int i = 0; i < PLANES; i++) {
                    if (avFrame->data[i]) {
                        maContext->Video.Data.Plane[i]=avFrame->data[i];
                        maContext->Video.Data.PlaneLinesize[i]=avFrame->linesize[i];
                        maContext->Video.Data.Valid=true;
                    }
                }

                int sample_aspect_ratio_num = avFrame->sample_aspect_ratio.num;
                int sample_aspect_ratio_den = avFrame->sample_aspect_ratio.den;
                if ((sample_aspect_ratio_num == 0) || (sample_aspect_ratio_den == 0)) {
                    dsyslog("cDecoder::GetFrameInfo(): invalid aspect ratio (%d:%d) at frame (%d)", sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
                    FREE(sizeof(*avFrame), "avFrame");
                    av_frame_free(&avFrame);
                    return false;
                }
                if ((sample_aspect_ratio_num == 1) && (sample_aspect_ratio_den == 1)) {
                    if ((avFrame->width == 1280) && (avFrame->height  ==  720) ||   // HD ready
                        (avFrame->width == 1920) && (avFrame->height  == 1080) ||   // full HD
                        (avFrame->width == 3840) && (avFrame->height  == 2160)) {   // UHD
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den = 9;
                    }
                    else {
                        dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio to video width %d hight %d at frame %d)",avFrame->width,avFrame->height,framenumber);
                        FREE(sizeof(*avFrame), "avFrame");
                        av_frame_free(&avFrame);
                        return false;
                    }
                }
                else {
                    if ((sample_aspect_ratio_num==64) && (sample_aspect_ratio_den==45)){  // // generic PAR MPEG-2 for PAL
                        sample_aspect_ratio_num =16;
                        sample_aspect_ratio_den = 9;
                    }
                    else if ((sample_aspect_ratio_num==16) && (sample_aspect_ratio_den==11)){  // // generic PAR MPEG-4 for PAL
                        sample_aspect_ratio_num =16;
                        sample_aspect_ratio_den = 9;
                    }
                    else if ((sample_aspect_ratio_num==32) && (sample_aspect_ratio_den==17)){
                         sample_aspect_ratio_num =16;
                         sample_aspect_ratio_den = 9;
                    }
                    else if ((sample_aspect_ratio_num==16) && (sample_aspect_ratio_den==15)){  // generic PAR MPEG-2 for PAL
                        sample_aspect_ratio_num =4;
                        sample_aspect_ratio_den =3;
                    }
                    else if ((sample_aspect_ratio_num==12) && (sample_aspect_ratio_den==11)){  // generic PAR MPEG-4 for PAL
                        sample_aspect_ratio_num =4;
                        sample_aspect_ratio_den =3;
                    }
                    else if ((sample_aspect_ratio_num==4) && (sample_aspect_ratio_den==3)){
//                      sample_aspect_ratio_num =4;
//                      sample_aspect_ratio_den =3;
                    }
                    else dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio (%d:%d) at frame (%d)",sample_aspect_ratio_num,sample_aspect_ratio_den,framenumber);
                }
                if ((maContext->Video.Info.AspectRatio.Num != sample_aspect_ratio_num) ||
                   ( maContext->Video.Info.AspectRatio.Den != sample_aspect_ratio_den)) {
                    if (msgGetFrameInfo) dsyslog("cDecoder::GetFrameInfo(): aspect ratio changed from (%d:%d) to (%d:%d) at frame %d",
                                                                            maContext->Video.Info.AspectRatio.Num, maContext->Video.Info.AspectRatio.Den,
                                                                            sample_aspect_ratio_num, sample_aspect_ratio_den,
                                                                            framenumber);
                    maContext->Video.Info.AspectRatio.Num=sample_aspect_ratio_num;
                    maContext->Video.Info.AspectRatio.Den=sample_aspect_ratio_den;
                }
                if (avFrame) {
                    FREE(sizeof(*avFrame), "avFrame");
                    av_frame_free(&avFrame);
                }
                return true;
            }
            if (avFrame) {
                FREE(sizeof(*avFrame), "avFrame");
                av_frame_free(&avFrame);
            }
            return false;
        }
    }

    if (isAudioPacket()) {
        if (isAudioAC3Packet()) {
            if (avpkt.stream_index > MAXSTREAMS) {
                dsyslog("cDecoder::GetFrameInfo(): to much streams %i", avpkt.stream_index);
                return false;
            }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codecpar->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %d changed from %d to %d at frame (%d)", avpkt.stream_index,
                                                                                                        maContext->Audio.Info.Channels[avpkt.stream_index],
                                                                                                        avctx->streams[avpkt.stream_index]->codecpar->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codecpar->channels;
#else
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codec->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %d changed from %d to %d at frame (%d)", avpkt.stream_index,
                                                                                                        maContext->Audio.Info.Channels[avpkt.stream_index],
                                                                                                        avctx->streams[avpkt.stream_index]->codec->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codec->channels;
#endif
            }
        }
        return true;
    }
    return false;
}


bool cDecoder::isVideoStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::isVideoStream(): streamindex %d out of range", streamIndex);
        return false;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::isVideoPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::isAudioStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::isAudioStream(): streamindex %d out of range", streamIndex);
        return false;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoder::isAudioPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}

bool cDecoder::isAudioAC3Stream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::isAudioAC3Stream(): streamindex %d out of range", streamIndex);
        return false;
    }
#define AUDIOFORMATAC3 8
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
    if (avctx->streams[streamIndex]->codecpar->codec_id == AV_CODEC_ID_AC3 ) return true;
#elif LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->format == AUDIOFORMATAC3) return true;
#else
    if (avctx->streams[streamIndex]->codec->sample_fmt == AUDIOFORMATAC3) return true;
#endif
    return false;
}


bool cDecoder::isAudioAC3Packet() {
    if (!avctx) return false;
#define AUDIOFORMATAC3 8
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_id == AV_CODEC_ID_AC3 ) return true;
#elif LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->format == AUDIOFORMATAC3) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->sample_fmt == AUDIOFORMATAC3) return true;
#endif
    return false;
}


bool cDecoder::isVideoIFrame() {
    if (!avctx) return false;
    if (!isVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0)  return true;
    return false;
}


int cDecoder::GetFrameNumber(){
    return framenumber;
}


int cDecoder::GetIFrameCount(){
    return iFrameCount;
}


bool cDecoder::isInterlacedVideo(){
    if (interlaced_frame > 0) return true;
    return false;
}


int cDecoder::GetIFrameRangeCount(int beginFrame, int endFrame) {
    if (iFrameInfoVector.empty()) {
        dsyslog("cDecoder::GetIFrameRangeCount(): iFrame Index not initialized");
        return 0;
    }
    int counter=0;

    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iInfo->iFrameNumber >= beginFrame) {
            counter++;
            if (iInfo->iFrameNumber >= endFrame) return counter;
        }
    }
    dsyslog("cDecoder::GetIFrameRangeCount(): failed beginFrame (%d) endFrame (%d) last frame in index list (%d)", beginFrame, endFrame, iFrameInfoVector.back().iFrameNumber);
    return 0;
}


int cDecoder::GetIFrameBefore(int iFrame) {
    if (iFrameInfoVector.empty()) {
        dsyslog("cDecoder::GetIFrameBefore(): iFrame Index not initialized");
        return 0;
    }
    int before_iFrame=0;

    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iInfo->iFrameNumber >= iFrame) {
            return before_iFrame;
        }
        else before_iFrame=iInfo->iFrameNumber;
    }
    dsyslog("cDecoder::GetIFrameBefore(): failed for frame (%d)", iFrame);
    return 0;
}


int64_t cDecoder::GetTimeFromIFrame(int iFrame) {
    if (iFrameInfoVector.empty()) {
        dsyslog("cDecoder::GetTimeFromIFrame(): iFrame Index not initialized");
        return 0;
    }
    int64_t before_pts=0;
    int before_iFrame=0;

    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iFrame == iInfo->iFrameNumber) {
            tsyslog("cDecoder::GetTimeFromIFrame(): iFrame (%d) time is %" PRId64" ms", iFrame, iInfo->pts_time_ms);
            return iInfo->pts_time_ms;
        }
        if (iInfo->iFrameNumber > iFrame) {
            if (abs(iFrame - before_iFrame) < abs(iFrame - iInfo->iFrameNumber)) {
//                tsyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame before (%li) %" PRId64 "ms",iFrame,before_iFrame,before_pts);
                return before_pts;
            }
            else {
//                tsyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame after (%li) %" PRId64 "ms",iFrame,iInfo->iFrameNumber,iInfo->pts_time_ms);
                return iInfo->pts_time_ms;
            }
        }
        else {
            before_iFrame=iInfo->iFrameNumber;
            before_pts=iInfo->pts_time_ms;
        }
    }
    dsyslog("cDecoder::GetTimeFromIFrame(): could not find time for frame %d",iFrame);
    return 0;
}


int cDecoder::GetIFrameFromOffset(int offset_ms) {
    if (iFrameInfoVector.empty()) dsyslog(")cDecoder::GetIFrameFromOffset: iFrame Index not initialized");
    int iFrameBefore = 0;
    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iInfo->pts_time_ms > offset_ms) return iFrameBefore;
        iFrameBefore = iInfo->iFrameNumber;
    }
    return 0;
}
