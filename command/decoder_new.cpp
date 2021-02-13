/*
 * decoder_new.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <string>
#include <sys/time.h>

#include "decoder_new.h"
extern "C" {
    #include "debug.h"
}

extern int decodeTime_us;


void AVlog(__attribute__((unused)) void *ptr, int level, const char* fmt, va_list vl){
    if (level <= AVLOGLEVEL) {
        char *logMsg = NULL;
        int rc = 0;
        rc = vasprintf(&logMsg, fmt, vl);
        if (rc == -1) {
            dsyslog("AVlog(): error in vasprintf");
            return;
        }
#ifdef DEBUG_MEM
        int length = strlen(logMsg) + 1;
        ALLOC(length, "logMsg");
#endif

        if (logMsg[strlen(logMsg) - 1] == '\n') logMsg[strlen(logMsg) - 1] = 0;

        if ((strcmp(logMsg, "co located POCs unavailable") == 0) || // this will happen with h.264 coding because of partitial decoding
            (strcmp(logMsg, "mmco: unref short failure") == 0) ||
            (strcmp(logMsg, "number of reference frames (0+5) exceeds max (4; probably corrupt input), discarding one") == 0)) {
                tsyslog("AVlog(): %s",logMsg);
        }
        else dsyslog("AVlog(): %s",logMsg);

#ifdef DEBUG_MEM
        FREE(length, "logMsg");
#endif
        free(logMsg);
    }
    return;
}


cDecoder::cDecoder(int threads, cIndex *recordingIndex) {
    av_log_set_level(AVLOGLEVEL);
    av_log_set_callback(AVlog);
    av_init_packet(&avpkt);
    codec = NULL;
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    dsyslog("cDecoder::cDecoder(): init with %i threads", threads);
    threadCount = threads;
    recordingIndexDecoder = recordingIndex;
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
        avformat_close_input(&avctx);
    }
    if (recordingDir) {
        FREE(strlen(recordingDir), "recordingDir");
        free(recordingDir);
    }
    if (avFrame) {
        FREE(sizeof(*avFrame), "avFrame");
        av_frame_free(&avFrame);
    }
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
    framenumber=-1;
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
    if (avformat_find_stream_info(avctx, NULL) < 0) {
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

        if (msgDecodeFile) dsyslog("cDecoder::DecodeFile(): using decoder for stream %i: codec id %5i -> %s", streamIndex, codec_id, codec->long_name);
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
    if (fileNumber <= 1) pts_time_ms_LastFile = 0;
    return true;
}


int cDecoder::GetVideoType() {
    if (!avctx) return 0;
    for (unsigned int i = 0; i < avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            switch (avctx->streams[i]->codecpar->codec_id) {
                case AV_CODEC_ID_MPEG2VIDEO:
                    dsyslog("cDecoder::GetVideoType(): video coding format: H.262");
                    return MARKAD_PIDTYPE_VIDEO_H262;
                    break;
                case AV_CODEC_ID_H264:
                    dsyslog("cDecoder::GetVideoType(): video coding format: H.264");
                    return MARKAD_PIDTYPE_VIDEO_H264;
                    break;
                case AV_CODEC_ID_H265:
                    dsyslog("cDecoder::GetVideoType(): video coding format: H.265");
                    return MARKAD_PIDTYPE_VIDEO_H265;
                    break;
                default:
                    dsyslog("cDecoder::GetVideoType(): video coding format unknown, coded id: %i", avctx->streams[i]->codecpar->codec_id);
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
    iFrameData.Valid = false;
    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
        if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
        {
            framenumber++;
            int64_t pts_time_ms = -1;
            if (avpkt.pts != AV_NOPTS_VALUE) {
                int64_t tmp_pts = avpkt.pts - avctx->streams[avpkt.stream_index]->start_time;
                if ( tmp_pts < 0 ) { tmp_pts += 0x200000000; }   // libavodec restart at 0 if pts greater than 0x200000000
                pts_time_ms = tmp_pts*av_q2d(avctx->streams[avpkt.stream_index]->time_base) * 1000;
                pts_time_ms_LastRead = pts_time_ms_LastFile + pts_time_ms;
            }
            if (isVideoIFrame()) {
                iFrameCount++;
                // store a iframe number pts index
                if (pts_time_ms >= 0) {
                    recordingIndexDecoder->Add(fileNumber, framenumber, pts_time_ms_LastFile + pts_time_ms);
                }
                else dsyslog("cDecoder::GetNextFrame(): failed to get pts for frame %d", framenumber);
            }
        }
        return true;
    }
    pts_time_ms_LastFile = pts_time_ms_LastRead;
    dsyslog("cDecoder::GetNextFrame(): last frame of filenumber %d is (%d), end time %" PRId64 "ms (%3d:%02dmin)", fileNumber, framenumber, pts_time_ms_LastFile, static_cast<int> (pts_time_ms_LastFile / 1000 / 60), static_cast<int> (pts_time_ms_LastFile / 1000) % 60);
    return false;
}


AVPacket *cDecoder::GetPacket() {
    return &avpkt;
}


bool cDecoder::SeekToFrame(MarkAdContext *maContext, int frame) {
    dsyslog("cDecoder::SeekToFrame(): (%d)", frame);
    if (!avctx) return false;
    if (!maContext) return false;
    if (framenumber > frame) {
        dsyslog("cDecoder::SeekToFrame(): current frame position (%d), could not seek backward to frame (%d)", framenumber, frame);
        return false;
    }

    int iFrameBefore = recordingIndexDecoder->GetIFrameBefore(frame);
    if (iFrameBefore == -1) {
        dsyslog("cDecoder::SeekFrame(): failed to get iFrame before frame (%d)", frame);
        return false;
    }
    if (iFrameBefore == -2) {
        iFrameBefore = 0;
        dsyslog("cDecoder::SeekFrame(): index does not yet contain frame (%5d), decode from current frame (%d) to build index", frame, framenumber);
    }

    // flush decoder buffer
    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        if (codecCtxArray[streamIndex]) {
            avcodec_flush_buffers(codecCtxArray[streamIndex]);
        }
    }
    while (framenumber < frame) {
        if (!this->GetNextFrame()) {
            if (!this->DecodeDir(recordingDir)) {
                dsyslog("cDecoder::SeekFrame(): failed for frame (%d) at frame (%d)", frame, framenumber);
                return false;
            }
        }
        if (framenumber >= iFrameBefore) GetFrameInfo(maContext);  // preload decoder buffer for interleaved codec
    }
    dsyslog("cDecoder::SeekToFrame(): successful");
    return true;
}


AVFrame *cDecoder::DecodePacket(AVFormatContext *avctx, AVPacket *avpkt) {
    if (!avctx) return NULL;
    if (!avpkt) return NULL;

    struct timeval startDecode = {};
    gettimeofday(&startDecode, NULL);

    if (avFrame) {  // reset avFrame structure
        FREE(sizeof(*avFrame), "avFrame");
        av_frame_free(&avFrame);
    }
    avFrame = av_frame_alloc();
    if (!avFrame) {
        dsyslog("cDecoder::DecodePacket(): av_frame_alloc failed");
        return NULL;
    }
    ALLOC(sizeof(*avFrame), "avFrame");

    if (isVideoPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        avFrame->height = avctx->streams[avpkt->stream_index]->codecpar->height;
        avFrame->width = avctx->streams[avpkt->stream_index]->codecpar->width;
        avFrame->format = codecCtxArray[avpkt->stream_index]->pix_fmt;
#else
        avFrame->height = avctx->streams[avpkt->stream_index]->codec->height;
        avFrame->width = avctx->streams[avpkt->stream_index]->codec->width;
        avFrame->format = codecCtxArray[avpkt->stream_index]->pix_fmt;
#endif
    }
    else if (isAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        avFrame->nb_samples = av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codecpar->channel_layout);
        avFrame->channel_layout = avctx->streams[avpkt->stream_index]->codecpar->channel_layout;
        avFrame->format = avctx->streams[avpkt->stream_index]->codecpar->format;
        avFrame->sample_rate = avctx->streams[avpkt->stream_index]->codecpar->sample_rate;
#elif LIBAVCODEC_VERSION_INT >= ((56<<16)+(26<<8)+100)
        avFrame->nb_samples = av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout = avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format = codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate = avctx->streams[avpkt->stream_index]->codec->sample_rate;
#else  // Raspbian Jessie
        avFrame->nb_samples = av_popcount64(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout = avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format = codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate = avctx->streams[avpkt->stream_index]->codec->sample_rate;
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
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error EINVAL at frame %d", framenumber);
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
            avFrame = NULL;
        }
    }
#else
    int frame_ready = 0;
    if (isVideoPacket()) {
        rc = avcodec_decode_video2(codecCtxArray[avpkt->stream_index], avFrame, &frame_ready, avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_video2 decode of frame (%d) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
            if (avFrame) {
                FREE(sizeof(*avFrame), "avFrame");
                av_frame_free(&avFrame);
            }
            return NULL;
        }
    }
    else if (isAudioPacket()) {
        rc = avcodec_decode_audio4(codecCtxArray[avpkt->stream_index], avFrame, &frame_ready, avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_audio4 of frame (%d) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
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
    struct timeval endDecode = {};
    gettimeofday(&endDecode, NULL);
    time_t sec = endDecode.tv_sec - startDecode.tv_sec;
    suseconds_t usec = endDecode.tv_usec - startDecode.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        sec--;
    }
    decodeTime_us += sec * 1000000 + usec;

    return avFrame;
}


bool cDecoder::GetFrameInfo(MarkAdContext *maContext) {
    if (!maContext) return false;
    if (!avctx) return false;

    AVFrame *avFrameRef = NULL;

    iFrameData.Valid = false;
    if (isVideoPacket()) {
        if (isVideoIFrame() || stateEAGAIN) {
            avFrameRef = DecodePacket(avctx, &avpkt);  // free in DecodePacket
            if (avFrameRef) {
                stateEAGAIN=false;
                if (avFrameRef->interlaced_frame != interlaced_frame) {
                    dsyslog("cDecoder::GetFrameInfo(): %s video format",(avFrameRef->interlaced_frame) ? "interlaced" : "non interlaced");
                    interlaced_frame = avFrameRef->interlaced_frame;
                }
                for (int i = 0; i < PLANES; i++) {
                    if (avFrameRef->data[i]) {
                        maContext->Video.Data.Plane[i] = avFrameRef->data[i];
                        maContext->Video.Data.PlaneLinesize[i] = avFrameRef->linesize[i];
                        maContext->Video.Data.Valid = true;
                    }
                }

                int sample_aspect_ratio_num = avFrameRef->sample_aspect_ratio.num;
                int sample_aspect_ratio_den = avFrameRef->sample_aspect_ratio.den;
                if ((sample_aspect_ratio_num == 0) || (sample_aspect_ratio_den == 0)) {
                    dsyslog("cDecoder::GetFrameInfo(): invalid aspect ratio (%d:%d) at frame (%d)", sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
                    maContext->Video.Data.Valid = false;
                    return false;
                }
                if ((sample_aspect_ratio_num == 1) && (sample_aspect_ratio_den == 1)) {
                    if ((avFrameRef->width == 1280) && (avFrameRef->height  ==  720) ||   // HD ready
                        (avFrameRef->width == 1920) && (avFrameRef->height  == 1080) ||   // full HD
                        (avFrameRef->width == 3840) && (avFrameRef->height  == 2160)) {   // UHD
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den = 9;
                    }
                    else {
                        dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio to video width %d hight %d at frame %d)",avFrameRef->width,avFrameRef->height,framenumber);
                        maContext->Video.Data.Valid = false;
                        return false;
                    }
                }
                else {
                    if ((sample_aspect_ratio_num == 64) && (sample_aspect_ratio_den == 45)) {  // // generic PAR MPEG-2 for PAL
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den =  9;
                    }
                    else if ((sample_aspect_ratio_num == 16) && (sample_aspect_ratio_den == 11)) {  // // generic PAR MPEG-4 for PAL
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den =  9;
                    }
                    else if ((sample_aspect_ratio_num == 32) && (sample_aspect_ratio_den == 17)) {
                         sample_aspect_ratio_num = 16;
                         sample_aspect_ratio_den =  9;
                    }
                    else if ((sample_aspect_ratio_num == 16) && (sample_aspect_ratio_den == 15)) {  // generic PAR MPEG-2 for PAL
                        sample_aspect_ratio_num = 4;
                        sample_aspect_ratio_den = 3;
                    }
                    else if ((sample_aspect_ratio_num == 12) && (sample_aspect_ratio_den == 11)) {  // generic PAR MPEG-4 for PAL
                        sample_aspect_ratio_num = 4;
                        sample_aspect_ratio_den = 3;
                    }
                    else if ((sample_aspect_ratio_num == 4) && (sample_aspect_ratio_den == 3)) {
//                      sample_aspect_ratio_num =4;
//                      sample_aspect_ratio_den =3;
                    }
                    else if ((sample_aspect_ratio_num == 3) && (sample_aspect_ratio_den == 2)) {  // H.264 1280x1080
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den =  9;
                    }
                    else dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio (%d:%d) at frame (%d)",sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
                }
                if ((maContext->Video.Info.AspectRatio.Num != sample_aspect_ratio_num) ||
                   ( maContext->Video.Info.AspectRatio.Den != sample_aspect_ratio_den)) {
                    if (msgGetFrameInfo) dsyslog("cDecoder::GetFrameInfo(): aspect ratio changed from (%d:%d) to (%d:%d) at frame %d",
                                                                            maContext->Video.Info.AspectRatio.Num, maContext->Video.Info.AspectRatio.Den,
                                                                            sample_aspect_ratio_num, sample_aspect_ratio_den,
                                                                            framenumber);
                    maContext->Video.Info.AspectRatio.Num = sample_aspect_ratio_num;
                    maContext->Video.Info.AspectRatio.Den = sample_aspect_ratio_den;
                }
                return true;
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
                maContext->Audio.Info.frameChannelChange = framenumber;
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


int cDecoder::GetFirstMP2AudioStream() {
    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        AVCodecID codec_id = avctx->streams[streamIndex]->codecpar->codec_id;
#else
        AVCodecID codec_id = avctx->streams[streamIndex]->codec->codec_id;
#endif
        if (codec_id == AV_CODEC_ID_MP2) return streamIndex;
    }
    return -1;
}


// get next silence part
// return:
// if <before> we are called at range before mark and return next iFrame after last silence part frame
// if not <before> we are called direct after mark position and return iFrame before first silence part
// -1 if no silence part were found
//
int cDecoder::GetNextSilence(const int stopFrame, const bool before) {
#define SILENCE_LEVEL 25  // changed from 10 to 27 to 25
#define SILENCE_COUNT 5   // low level counts twice
    int silenceCount = 0;
    int nextSilenceFrame = -1;
    int firstSilenceFrame = -1;
    int lastSilenceFrame = -1;
    int streamIndex = GetFirstMP2AudioStream();
    if (streamIndex < 0) {
        dsyslog("cDecoder::GetNextSilence(): could not get stream index of MP2 audio stream");
        return -1;
    }
    dsyslog("cDecoder::GetNextSilence(): using stream index %i from frame (%d) to frame (%d)", streamIndex, GetFrameNumber(), stopFrame);
    while (GetFrameNumber() < stopFrame) {
        if (!GetNextFrame()) {
            if (!DecodeDir(recordingDir)) break;
        }
        if (avpkt.stream_index != streamIndex) continue;
        if (isAudioPacket()) {
            AVFrame *audioFrame = DecodePacket(avctx, &avpkt);
            if (audioFrame) {
                if (audioFrame->format == AV_SAMPLE_FMT_S16P) {
                    int level = 0;
                    for (int channel = 0; channel < audioFrame->channels; channel++) {
                        int16_t *samples = reinterpret_cast<int16_t*>(audioFrame->data[channel]);
                        for (int sample = 0; sample < audioFrame->nb_samples; sample++) {
                            level += abs(samples[sample]);
                        }
                    }
                    int normLevel =  level / audioFrame->nb_samples / audioFrame->channels;
#ifdef DEBUG_SILENCE
                    dsyslog("cDecoder::GetNextSilence(): frame (%5d) level %d", GetFrameNumber(), normLevel);
#endif
                    if (normLevel <= SILENCE_LEVEL) {
                        silenceCount++;
                        if (normLevel <= 7) silenceCount++;  // changed from 0 to 7
                        if (nextSilenceFrame == -1) nextSilenceFrame = GetFrameNumber();
                        if (before) nextSilenceFrame = GetFrameNumber();
                        dsyslog("cDecoder::GetNextSilence(): stream %d frame (%d) level %d silenceCount %d", avpkt.stream_index, GetFrameNumber(), normLevel, silenceCount);
                        if (silenceCount >= SILENCE_COUNT) {
                            if (before) {
                                dsyslog("cDecoder::GetNextSilence(): found silence part in stream %d before mark, %d silence frames end at frame (%d)", avpkt.stream_index, SILENCE_COUNT, nextSilenceFrame);
                                lastSilenceFrame = recordingIndexDecoder->GetIFrameAfter(nextSilenceFrame);
                                silenceCount--;
                            }
                            else {
                                dsyslog("cDecoder::GetNextSilence(): found silence part in stream %d after mark, %d silence frames start at frame (%d)", avpkt.stream_index, SILENCE_COUNT, nextSilenceFrame);
                                firstSilenceFrame = recordingIndexDecoder->GetIFrameBefore(nextSilenceFrame);
                                break;
                            }
                        }
                    }
                    else {
                        silenceCount = 0;
                        nextSilenceFrame = -1;
                        firstSilenceFrame = -1;
                    }
                }
                else {
                    dsyslog("cDecoder::GetNextSilence(): stream %i frame %i sample format not supported %s", avpkt.stream_index, GetFrameNumber(), av_get_sample_fmt_name((enum AVSampleFormat) audioFrame->format));
                    return -1;
                }
            }
        }
    }
    if (before) return lastSilenceFrame;
    else return firstSilenceFrame;
}
