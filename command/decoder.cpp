/*
 * decoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <string>
#include <sys/time.h>
#include "global.h"
#ifdef WINDOWS
   #include "win32/mingw64.h"
#endif

#include "decoder.h"
#include "debug.h"

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
                tsyslog("AVlog(): %s", logMsg);
        }
        else dsyslog("AVlog(): %s", logMsg);
#ifdef DEBUG_MEM
        FREE(length, "logMsg");
#endif
        free(logMsg);
    }
    return;
}


cDecoder::cDecoder(int threads, cIndex *recordingIndex) {
    dsyslog("cDecoder::cDecoder(): create new decoder instance");
    av_log_set_level(AVLOGLEVEL);
    av_log_set_callback(AVlog);
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
    av_init_packet(&avpkt);
#endif
    codec = NULL;
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    dsyslog("cDecoder::cDecoder(): init with %i threads", threads);
    threadCount = threads;
    recordingIndexDecoder = recordingIndex;
}


cDecoder::~cDecoder() {
    av_packet_unref(&avpkt);
    if (avctx && codecCtxArray) {
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            if (codecCtxArray[streamIndex]) {
                FREE(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");
                avcodec_free_context(&codecCtxArray[streamIndex]);
            }
        }
        FREE(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
        free(codecCtxArray);
    }
    if (avctx) {
        dsyslog("cDecoder::~cDecoder(): call avformat_close_input");
        FREE(sizeof(avctx), "avctx");
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
    dsyslog("cDecoder::~cDecoder(): decoder instance deleted");
}


int cDecoder::GetErrorCount() {
    return decodeErrorCount;
}


bool cDecoder::DecodeDir(const char *recDir) {
    if (!recDir) return false;
    char *filename;
    if (!recordingDir) {
        if (asprintf(&recordingDir,"%s", recDir) == -1) {
            dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
            return false;
        }
        ALLOC(strlen(recordingDir), "recordingDir");
    }
    fileNumber++;
    if (asprintf(&filename, "%s/%05i.ts", recDir, fileNumber) == -1) {
        dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename), "filename");
    bool ret = DecodeFile(filename);
    FREE(strlen(filename), "filename");
    free(filename);
    return ret;
}


int cDecoder::GetFileNumber() {
    return fileNumber;
}


void cDecoder::Reset(){
    fileNumber = 0;
    currFrameNumber = -1;
    msgGetFrameInfo = false;
    dtsBefore = -1;
}


AVFormatContext *cDecoder::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoder::GetAVCodecContext() {
    return codecCtxArray;
}


bool cDecoder::DecodeFile(const char *filename) {
    if (!filename) return false;
    dsyslog("cDecoder::DecodeFile(): filename: %s", filename);
    AVFormatContext *avctxNextFile = NULL;
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    // free codec context before alloc for new file
    if (codecCtxArray) {
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

    // open first/next file
    if (avformat_open_input(&avctxNextFile, filename, NULL, NULL) == 0) {
        ALLOC(sizeof(avctxNextFile), "avctx");
        dsyslog("cDecoder::DecodeFile(): opened file %s", filename);
        if (avctx) {
            FREE(sizeof(avctx), "avctx");
            avformat_close_input(&avctx);
        }
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

    codecCtxArray = static_cast<AVCodecContext **>(malloc(sizeof(AVCodecContext *) * avctx->nb_streams));
    ALLOC(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
    memset(codecCtxArray, 0, sizeof(AVCodecContext *) * avctx->nb_streams);

    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        AVCodecID codec_id = avctx->streams[streamIndex]->codecpar->codec_id;
        codec = avcodec_find_decoder(codec_id);
#else
        AVCodecID codec_id = avctx->streams[streamIndex]->codec->codec_id;
        codec = avcodec_find_decoder(codec_id);
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

        dsyslog("cDecoder::DecodeFile(): using decoder for stream %i: codec id %5i -> %s", streamIndex, codec_id, codec->long_name);
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
        if (IsVideoStream(streamIndex)) {
            dsyslog("cDecoder::DecodeFile(): average framerate %d/%d", avctx->streams[streamIndex]->avg_frame_rate.num, avctx->streams[streamIndex]->avg_frame_rate.den);
            dsyslog("cDecoder::DecodeFile(): real    framerate %d/%d", avctx->streams[streamIndex]->r_frame_rate.num, avctx->streams[streamIndex]->r_frame_rate.den);
        }
    }
    if (fileNumber <= 1) offsetTime_ms_LastFile = 0;
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


int cDecoder::GetVideoAvgFrameRate() {
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
    dsyslog("cDecoder::GetVideoAvgFrameRate(): could not find average frame rate");
    return 0;
}


int cDecoder::GetVideoRealFrameRate() {
    if (videoRealFrameRate > 0) return videoRealFrameRate;
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            videoRealFrameRate = av_q2d(avctx->streams[i]->r_frame_rate);
            return videoRealFrameRate;
#else
            videoRealFrameRate = av_q2d(av_stream_get_r_frame_rate(avctx->streams[i]));
            return videoRealFrameRate;
#endif
        }
    }
    dsyslog("cDecoder::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoder::GetNextPacket(bool ignorePTS_Ringbuffer) {
    if (!avctx) return false;
    FrameData.Valid = false;
    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
        if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
        {
            currFrameNumber++;

            // check packet DTS and PTS
            if ((avpkt.dts == AV_NOPTS_VALUE) || (avpkt.pts == AV_NOPTS_VALUE)) {
                dsyslog("cDecoder::GetNextPacket(): framenumber %5d: invalid packet, DTS or PTS not set", currFrameNumber);
                return true;   // false only on EOF
            }

            currOffset += avpkt.duration;
#ifdef DEBUG_FRAME_PTS
            dsyslog("cDecoder::GetNextPacket(): framenumber %5d, DTS %ld, PTS %ld, duration %ld, flags %d, dtsBefore %ld, time_base.num %d, time_base.den %d", currFrameNumber, avpkt.dts, avpkt.pts, avpkt.duration, avpkt.flags, dtsBefore, avctx->streams[avpkt.stream_index]->time_base.num, avctx->streams[avpkt.stream_index]->time_base.den);
#endif

            // check DTS continuity
            if (dtsBefore != -1) {
                int dtsDiff = 1000 * (avpkt.dts - dtsBefore) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;
                int dtsStep = 1000 / GetVideoRealFrameRate();
                if (dtsDiff > dtsStep) {  // some interlaced H.264 streams have some frames with half DTS
                    if (currFrameNumber > decodeErrorFrame) {  // only count new frames
                        decodeErrorCount++;
                        decodeErrorFrame = currFrameNumber;
                    }
                    if (dtsDiff <= 0) { // ignore frames with negativ DTS difference
                        dsyslog("cDecoder::GetNextPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, ignore frame, decoding errors %d",
                                                                                                                                   currFrameNumber, dtsDiff, dtsStep, decodeErrorCount);
                        dtsBefore = avpkt.dts;  // store even wrong DTS to continue after error
                        return true;  // false only on EOF
                    }
                    else dsyslog("cDecoder::GetNextPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, decoding errors %d",
                                                                                                                                    currFrameNumber,dtsDiff, dtsStep, decodeErrorCount);
                }
            }
            dtsBefore = avpkt.dts;

            // store frame number and pts in a ring buffer
            if (!ignorePTS_Ringbuffer) recordingIndexDecoder->AddPTS(currFrameNumber, avpkt.pts);
            int64_t offsetTime_ms = -1;
            if (avpkt.pts != AV_NOPTS_VALUE) {
                int64_t tmp_pts = avpkt.pts - avctx->streams[avpkt.stream_index]->start_time;
                if ( tmp_pts < 0 ) { tmp_pts += 0x200000000; }   // libavodec restart at 0 if pts greater than 0x200000000
                offsetTime_ms = 1000 * tmp_pts * av_q2d(avctx->streams[avpkt.stream_index]->time_base);
                offsetTime_ms_LastRead = offsetTime_ms_LastFile + offsetTime_ms;
            }
            if (IsVideoIFrame()) {
                iFrameCount++;
                // store iframe number and pts offset, sum frame duration in index
                int64_t frameTimeOffset_ms = 1000 * static_cast<int64_t>(currOffset) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;  // need more space to calculate value
                if (offsetTime_ms >= 0) recordingIndexDecoder->Add(fileNumber, currFrameNumber, offsetTime_ms_LastFile + offsetTime_ms, frameTimeOffset_ms);
                else dsyslog("cDecoder::GetNextPacket(): failed to get pts for frame %d", currFrameNumber);
            }
        }
        return true;
    }
    // end of file reached
    offsetTime_ms_LastFile = offsetTime_ms_LastRead;
    dsyslog("cDecoder::GetNextPacket(): last frame of filenumber %d is (%d), end time %" PRId64 "ms (%3d:%02dmin)", fileNumber, currFrameNumber, offsetTime_ms_LastFile, static_cast<int> (offsetTime_ms_LastFile / 1000 / 60), static_cast<int> (offsetTime_ms_LastFile / 1000) % 60);
    if (decodeErrorFrame == currFrameNumber) decodeErrorCount--; // ignore malformed last frame of a file
    return false;
}


AVPacket *cDecoder::GetPacket() {
    return &avpkt;
}


bool cDecoder::SeekToFrame(sMarkAdContext *maContext, int frameNumber) {
    dsyslog("cDecoder::SeekToFrame(): (%d)", frameNumber);
    if (!avctx) return false;
    if (!maContext) return false;
    if (currFrameNumber > frameNumber) {
        dsyslog("cDecoder::SeekToFrame(): current frame position (%d), could not seek backward to frame (%d)", currFrameNumber, frameNumber);
        return false;
    }

    int iFrameBefore = recordingIndexDecoder->GetIFrameBefore(frameNumber);
    if (iFrameBefore == -1) {
        dsyslog("cDecoder::SeekFrame(): failed to get iFrame before frame (%d)", frameNumber);
        return false;
    }
    if (iFrameBefore == -2) {
        iFrameBefore = 0;
        dsyslog("cDecoder::SeekFrame(): index does not yet contain frame (%5d), decode from current frame (%d) to build index", frameNumber, currFrameNumber);
    }

    // flush decoder buffer
    if (codecCtxArray) {
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            if (codecCtxArray[streamIndex]) {
                avcodec_flush_buffers(codecCtxArray[streamIndex]);
            }
        }
    }
    else {
        dsyslog("cDecoder::SeekToFrame(): codec context not valid");
        return false;
    }

    while (currFrameNumber < frameNumber) {
        if (!this->GetNextPacket()) {
            if (!this->DecodeDir(recordingDir)) {
                dsyslog("cDecoder::SeekFrame(): failed for frame (%d) at frame (%d)", frameNumber, currFrameNumber);
                return false;
            }
            continue;
        }
        if (currFrameNumber >= iFrameBefore) GetFrameInfo(maContext, false);  // preload decoder buffer
    }
    dsyslog("cDecoder::SeekToFrame(): successful");
    return true;
}


AVFrame *cDecoder::DecodePacket(AVPacket *avpkt) {
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

    if (IsVideoPacket()) {
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
    else {
        if (IsAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    #if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        avFrame->nb_samples     = codecCtxArray[avpkt->stream_index]->ch_layout.nb_channels;
        avFrame->format         = codecCtxArray[avpkt->stream_index]->sample_fmt;
        int ret                 = av_channel_layout_copy(&avFrame->ch_layout, &codecCtxArray[avpkt->stream_index]->ch_layout);
        if (ret < 0) {
            dsyslog("cDecoder::DecodePacket(): av_channel_layout_copy failed, rc = %d", ret);
            return NULL;
        }
    #else
        avFrame->nb_samples     = av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codecpar->channel_layout);
        avFrame->channel_layout = avctx->streams[avpkt->stream_index]->codecpar->channel_layout;
    #endif
        avFrame->format         = avctx->streams[avpkt->stream_index]->codecpar->format;
        avFrame->sample_rate    = avctx->streams[avpkt->stream_index]->codecpar->sample_rate;
#else
        avFrame->nb_samples     = av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout = avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format         = codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate    = avctx->streams[avpkt->stream_index]->codec->sample_rate;
#endif
        }
        else {
            if (IsSubtitlePacket()) { // do not decode subtitle, even on fullencode use it without reencoding
                FREE(sizeof(*avFrame), "avFrame");   // test if avFrame not NULL above
                av_frame_free(&avFrame);
                return NULL;
            }
            else {
                dsyslog("cDecoder::DecodePacket(): stream %d type not supported", avpkt->stream_index);
                FREE(sizeof(*avFrame), "avFrame");   // test if avFrame not NULL above
                av_frame_free(&avFrame);
                return NULL;
            }
        }
    }

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    int rc = av_frame_get_buffer(avFrame, 0);
#else
    int rc = av_frame_get_buffer(avFrame, 32);
#endif

    if (rc != 0) {
        char errTXT[64] = {0};
        av_strerror(rc, errTXT, sizeof(errTXT));
        dsyslog("cDecoder::DecodePacket(): stream index %d: av_frame_get_buffer failed: %s", avpkt->stream_index, errTXT);
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
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error EAGAIN at frame %d", currFrameNumber);
                break;
            case AVERROR(ENOMEM):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error ENOMEM at frame %d", currFrameNumber);
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error EINVAL at frame %d", currFrameNumber);
                break;
            case AVERROR_INVALIDDATA:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AVERROR_INVALIDDATA at frame %d", currFrameNumber);
                break;
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            case AAC_AC3_PARSE_ERROR_SYNC:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC at frame %d", currFrameNumber);
                break;
#endif
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet failed with rc=%d at frame %d",rc,currFrameNumber);
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
            case AVERROR(EAGAIN):  // no error
//                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EAGAIN at frame %d", currFrameNumber);
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EINVAL at frame %d", currFrameNumber);
                break;
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame: decode of frame (%d) failed with return code %i", currFrameNumber, rc);
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
    if (IsVideoPacket()) {
        rc = avcodec_decode_video2(codecCtxArray[avpkt->stream_index], avFrame, &frame_ready, avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_video2 decode of frame (%d) from stream %i failed with return code %i", currFrameNumber, avpkt->stream_index, rc);
            if (avFrame) {
                FREE(sizeof(*avFrame), "avFrame");
                av_frame_free(&avFrame);
            }
            return NULL;
        }
    }
    else if (IsAudioPacket()) {
        rc = avcodec_decode_audio4(codecCtxArray[avpkt->stream_index], avFrame, &frame_ready, avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_audio4 of frame (%d) from stream %i failed with return code %i", currFrameNumber, avpkt->stream_index, rc);
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
    // check decoding error
    if (avFrame && (avFrame->decode_error_flags != 0)) {
        if (currFrameNumber > decodeErrorFrame) {  // only count new frames
            decodeErrorFrame = currFrameNumber;
            decodeErrorCount++;
        }
        dsyslog("cDecoder::DecodePacket(): decoding of frame (%d) from stream %i failed: decode_error_flags %d, decoding errors %d", currFrameNumber, avpkt->stream_index, avFrame->decode_error_flags, decodeErrorCount);
        FREE(sizeof(*avFrame), "avFrame");
        av_frame_free(&avFrame);
        avFrame = NULL;
        avcodec_flush_buffers(codecCtxArray[avpkt->stream_index]);
    }

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


bool cDecoder::GetFrameInfo(sMarkAdContext *maContext, const bool full) {
    if (!maContext) return false;
    if (!avctx) return false;

    AVFrame *avFrameRef = NULL;

    FrameData.Valid = false;
    if (IsVideoPacket()) {
        if (full || IsVideoIFrame() || stateEAGAIN) {
            avFrameRef = DecodePacket(&avpkt);  // free in DecodePacket
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
                        maContext->Video.Data.valid = true;
                    }
                }
                sAspectRatio DAR;
                DAR.num = avFrameRef->sample_aspect_ratio.num;
                DAR.den = avFrameRef->sample_aspect_ratio.den;
                if ((DAR.num == 0) || (DAR.den == 0)) {
                    dsyslog("cDecoder::GetFrameInfo(): invalid aspect ratio (%d:%d) at frame (%d)", DAR.num, DAR.den, currFrameNumber);
                    maContext->Video.Data.valid = false;
                    return false;
                }
                if ((DAR.num == 1) && (DAR.den == 1)) {
                    if ((avFrameRef->width == 1280) && (avFrameRef->height  ==  720) ||   // HD ready
                        (avFrameRef->width == 1920) && (avFrameRef->height  == 1080) ||   // full HD
                        (avFrameRef->width == 3840) && (avFrameRef->height  == 2160)) {   // UHD
                        DAR.num = 16;
                        DAR.den = 9;
                    }
                    else {
                        dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio to video width %d hight %d at frame %d)",avFrameRef->width,avFrameRef->height,currFrameNumber);
                        maContext->Video.Data.valid = false;
                        return false;
                    }
                }
                else {
                    if ((DAR.num == 64) && (DAR.den == 45)) {  // // generic PAR MPEG-2 for PAL
                        DAR.num = 16;
                        DAR.den =  9;
                    }
                    else if ((DAR.num == 16) && (DAR.den == 11)) {  // // generic PAR MPEG-4 for PAL
                        DAR.num = 16;
                        DAR.den =  9;
                    }
                    else if ((DAR.num == 32) && (DAR.den == 17)) {
                         DAR.num = 16;
                         DAR.den =  9;
                    }
                    else if ((DAR.num == 16) && (DAR.den == 15)) {  // generic PAR MPEG-2 for PAL
                        DAR.num = 4;
                        DAR.den = 3;
                    }
                    else if ((DAR.num == 12) && (DAR.den == 11)) {  // generic PAR MPEG-4 for PAL
                        DAR.num = 4;
                        DAR.den = 3;
                    }
                    else if ((DAR.num == 4) && (DAR.den == 3)) {
                            if ((avFrameRef->width == 1440) && (avFrameRef->height  == 1080)) { // H.264 1440x1080 PAR 4:3 -> DAR 16:9
                                DAR.num = 16;
                                DAR.den =  9;
                            }
                    }
                    else if ((DAR.num == 3) && (DAR.den == 2)) {  // H.264 1280x1080
                        DAR.num = 16;
                        DAR.den =  9;
                    }
                    else {
                        dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio (%d:%d) at frame (%d)",DAR.num, DAR.den, currFrameNumber);
                        maContext->Video.Data.valid = false;
                        return false;
                    }
                }
                if ((maContext->Video.Info.AspectRatio.num != DAR.num) ||
                   ( maContext->Video.Info.AspectRatio.den != DAR.den)) {
                    if (msgGetFrameInfo) dsyslog("cDecoder::GetFrameInfo(): aspect ratio changed from (%d:%d) to (%d:%d) at frame %d",
                                                                            maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den,
                                                                            DAR.num, DAR.den,
                                                                            currFrameNumber);
                    maContext->Video.Info.AspectRatio.num = DAR.num;
                    maContext->Video.Info.AspectRatio.den = DAR.den;
                }
                return true;
            }
            maContext->Video.Data.valid = false;
            return false;
        }
    }

    if (IsAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (maContext->Audio.Info.codec_id[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codecpar->codec_id) {
            if (maContext->Audio.Info.codec_id[avpkt.stream_index] != 0) dsyslog("cDecoder::GetFrameInfo(): frame (%d) stream index %d codec_id changed from %d to %d", currFrameNumber, avpkt.stream_index, maContext->Audio.Info.codec_id[avpkt.stream_index], avctx->streams[avpkt.stream_index]->codecpar->codec_id);
            maContext->Audio.Info.codec_id[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codecpar->codec_id;
        }
#else
        if (maContext->Audio.Info.codec_id[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codec->codec_id) {
            if (maContext->Audio.Info.codec_id[avpkt.stream_index] != 0) dsyslog("cDecoder::GetFrameInfo(): frame (%d) stream index %d codec_id changed from %d to %d", currFrameNumber, avpkt.stream_index, maContext->Audio.Info.codec_id[avpkt.stream_index], avctx->streams[avpkt.stream_index]->codec->codec_id);
            maContext->Audio.Info.codec_id[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codec->codec_id;
        }
#endif
        if (IsAudioAC3Packet()) {
            if (avpkt.stream_index > MAXSTREAMS) {
                dsyslog("cDecoder::GetFrameInfo(): to much streams %i", avpkt.stream_index);
                return false;
            }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    #if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codecpar->ch_layout.nb_channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %d changed from %d to %d at frame (%d) PTS %" PRId64, avpkt.stream_index, maContext->Audio.Info.Channels[avpkt.stream_index], avctx->streams[avpkt.stream_index]->codecpar->ch_layout.nb_channels, currFrameNumber, avpkt.pts);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codecpar->ch_layout.nb_channels;
    #else
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codecpar->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %d changed from %d to %d at frame (%d) PTS %" PRId64, avpkt.stream_index, maContext->Audio.Info.Channels[avpkt.stream_index], avctx->streams[avpkt.stream_index]->codecpar->channels, currFrameNumber, avpkt.pts);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codecpar->channels;
    #endif
#else
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codec->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %d changed from %d to %d at frame (%d) PTS %" PRId64, avpkt.stream_index,
                                                                                                        maContext->Audio.Info.Channels[avpkt.stream_index],
                                                                                                        avctx->streams[avpkt.stream_index]->codec->channels,
                                                                                                        currFrameNumber, avpkt.pts);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codec->channels;
#endif
                maContext->Audio.Info.channelChangeFrame = currFrameNumber;
                maContext->Audio.Info.channelChangePTS   = avpkt.pts;
            }
        }
        return true;
    }
    return false;
}


bool cDecoder::IsVideoStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsVideoStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::IsVideoPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::IsVideoIFrame() {
    if (!avctx) return false;
    if (!IsVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0)  return true;
    return false;
}


bool cDecoder::IsAudioStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsAudioStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoder::IsAudioPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoder::IsAudioAC3Stream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsAudioAC3Stream(): streamindex %d out of range", streamIndex);
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


bool cDecoder::IsAudioAC3Packet() {
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


bool cDecoder::IsSubtitleStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsSubtitleStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#endif
    return false;
}


bool cDecoder::IsSubtitlePacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#endif
    return false;
}


int cDecoder::GetFrameNumber(){
    return currFrameNumber;
}


int cDecoder::GetIFrameCount(){
    return iFrameCount;
}


bool cDecoder::IsInterlacedVideo(){
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
// if <before> we are called for a range before mark and return next i-frame after last silence part frame
// if not <before> we are called direct after mark position and return i-frame before first silence part
// -1 if no silence part were found or we got no valid video frame number to silecnce PTS
//
int cDecoder::GetNextSilence(sMarkAdContext *maContext, const int stopFrame, const bool isBeforeMark, const bool isStartMark) {
#define SILENCE_LEVEL 17  // changed from 25 to 17
#define SILENCE_COUNT 5   // low level counts twice
    struct silenceType {
        int     startTmp    = -1;
        int64_t startTmpPTS = -1;
        int     endTmp      = -1;
        int64_t endTmpPTS   = -1;
        int     countTmp    =  0;
        int     startFrame  = -1;
        int64_t startPTS    = -1;
        int     endFrame    = -1;
        int64_t endPTS      = -1;
        int     count       =  0;
    } silence;

    int streamIndex = GetFirstMP2AudioStream();
    if (streamIndex < 0) {
        dsyslog("cDecoder::GetNextSilence(): could not get stream index of MP2 audio stream");
        return -1;
    }
    struct sVideoFrame {
        int     frameNumber = -1;
        int64_t pts         = -1;
    } videoFrame;
    std::vector<sVideoFrame> videoFrameVector;
    int startFrame = GetFrameNumber();

    dsyslog("cDecoder::GetNextSilence(): using stream index %i from frame (%d) to frame (%d)", streamIndex, GetFrameNumber(), stopFrame);
    while (GetFrameNumber() < stopFrame) {
        if (!GetNextPacket()) {
            if (!DecodeDir(recordingDir)) break;
        }
        if (IsVideoPacket()) {  // store video PTS of each video frame
#ifdef DEBUG_SILENCE
            dsyslog("cDecoder::GetNextSilence(): stream index %i video frame (%d) pts %" PRId64, streamIndex, GetFrameNumber(), avpkt.pts);
#endif
            videoFrame.frameNumber = GetFrameNumber();
            videoFrame.pts = avpkt.pts;
            videoFrameVector.push_back(videoFrame);
            ALLOC(sizeof(videoFrame), "videoFrameVector");
            continue;
        }
        if (avpkt.stream_index != streamIndex) continue;
        if (IsAudioPacket()) {
            AVFrame *audioFrame = DecodePacket(&avpkt);
            if (audioFrame) {
                if (audioFrame->format == AV_SAMPLE_FMT_S16P) {
                    int level = 0;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
                    for (int channel = 0; channel < audioFrame->ch_layout.nb_channels; channel++) {
#else
                    for (int channel = 0; channel < audioFrame->channels; channel++) {
#endif
                        const int16_t *samples = reinterpret_cast<int16_t*>(audioFrame->data[channel]);
                        for (int sample = 0; sample < audioFrame->nb_samples; sample++) {
                            level += abs(samples[sample]);
                        }
                    }
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
                    int normLevel =  level / audioFrame->nb_samples / audioFrame->ch_layout.nb_channels;
#else
                    int normLevel =  level / audioFrame->nb_samples / audioFrame->channels;
#endif
#ifdef DEBUG_SILENCE
                    dsyslog("cDecoder::GetNextSilence(): frame (%5d) level %d", GetFrameNumber(), normLevel);
#endif
                    if (normLevel <= SILENCE_LEVEL) {
                        silence.countTmp++;
                        if (normLevel <= 7) silence.countTmp++;
                        int frameNumber = GetFrameNumber();
                        if (silence.startTmp == -1) {
                            silence.startTmp = frameNumber;
                            silence.startTmpPTS = avpkt.pts;
                        }
                        silence.endTmp = frameNumber;
                        silence.endTmpPTS = avpkt.pts;
                        dsyslog("cDecoder::GetNextSilence(): stream %d frame (%5d) level %2d silenceCount %2d, pts %" PRId64, avpkt.stream_index, frameNumber, normLevel, silence.countTmp, silence.endTmpPTS);

                    }
                    else {
                        if (silence.countTmp >= SILENCE_COUNT) { // min count reached, this part is valid
                            if (silence.startTmp >= 0) {  // end of silence part reached
                                if (!isBeforeMark) break;  // we are called after the mark, take first valid silence part
                                if (silence.startFrame <= silence.startTmp) { // later result found
                                    silence.count      = silence.countTmp;
                                    silence.startFrame = silence.startTmp;
                                    silence.startPTS   = silence.startTmpPTS;
                                    silence.endFrame   = silence.endTmp;
                                    silence.endPTS     = silence.endTmpPTS;
                                }
                            }
                        }
                        silence.countTmp    =  0;
                        silence.startTmp    = -1;
                        silence.startTmpPTS = -1;
                        silence.endTmp      = -1;
                        silence.endTmpPTS   = -1;
                    }
                }
                else {
                    dsyslog("cDecoder::GetNextSilence(): stream %i frame %i sample format not supported %s", avpkt.stream_index, GetFrameNumber(), av_get_sample_fmt_name((enum AVSampleFormat) audioFrame->format));
                    return -1;
                }
            }
        }

    }
    if (silence.startFrame <= silence.startTmp) { // later result found
        silence.count      = silence.countTmp;
        silence.startFrame = silence.startTmp;
        silence.startPTS   = silence.startTmpPTS;
        silence.endFrame   = silence.endTmp;
        silence.endPTS     = silence.endTmpPTS;
    }
    videoFrame.frameNumber = -1;
    videoFrame.pts         = -1;
    int silenceFrame       = -1;

    if (silence.count >= SILENCE_COUNT) {
        struct sAudioFrame {
            int frameNumber = -1;
            int64_t pts = -1;
        } audioFrame;
        if (isStartMark) {
            audioFrame.frameNumber = silence.endFrame; // for start marks we use end of silence part
            audioFrame.pts         = silence.endPTS;

            videoFrame.pts = INT64_MAX;
            while (!videoFrameVector.empty()) {  // search video frame with pts after audio frame
                if ((videoFrameVector.back().pts > audioFrame.pts) && (videoFrameVector.back().pts < videoFrame.pts)) {
                    videoFrame.frameNumber =  videoFrameVector.back().frameNumber;
                    videoFrame.pts =  videoFrameVector.back().pts;
                }
                videoFrameVector.pop_back();
                FREE(sizeof(videoFrame), "videoFrameVector");
            }
            if (videoFrame.frameNumber == -1) {
                dsyslog("cDecoder::GetNextSilence(): video frame with pts after not in range, set to audio frame");
                videoFrame.frameNumber = silence.endFrame;
            }
        }
        else {
            audioFrame.frameNumber = silence.startFrame;  // for stop mark we use start of silence part
            audioFrame.pts         = silence.startPTS;

            // search video frame with pts before audio frame
            while (!videoFrameVector.empty()) {
                if ((videoFrameVector.back().pts < audioFrame.pts) && (videoFrameVector.back().pts > videoFrame.pts)) {
                    videoFrame.frameNumber =  videoFrameVector.back().frameNumber;
                    videoFrame.pts =  videoFrameVector.back().pts;
                }
                videoFrameVector.pop_back();
                FREE(sizeof(videoFrame), "videoFrameVector");
            }
            if (videoFrame.frameNumber == -1) {
                if (isBeforeMark) {
                    dsyslog("cDecoder::GetNextSilence(): video frame with pts before not in range, set to audio frame");
                    videoFrame.frameNumber = silence.startFrame;
                }
                else { // we check after a logo stop mark, do not go back before this mark
                    dsyslog("cDecoder::GetNextSilence(): video frame with pts of silence is before stop mark during check after stop mark, use stop mark position");
                    return startFrame;
                }
            }
        }
        if (!maContext->Config->fullDecode) {
            if (isBeforeMark) silenceFrame = recordingIndexDecoder->GetIFrameBefore(videoFrame.frameNumber);
            else              silenceFrame = recordingIndexDecoder->GetIFrameAfter(videoFrame.frameNumber);
        }
        else silenceFrame = videoFrame.frameNumber;
        dsyslog("cDecoder::GetNextSilence(): found silence part in stream %d between audio frame (%d) and (%d)", streamIndex, silence.startFrame, silence.endFrame);
        dsyslog("cDecoder::GetNextSilence(): use audio frame (%d) PTS %" PRId64 ", video frame (%d) PTS %" PRId64 ", return frame (%d)", audioFrame.frameNumber, audioFrame.pts, videoFrame.frameNumber, videoFrame.pts, silenceFrame);
    }
    else {
#ifdef DEBUG_MEM
        int size = videoFrameVector.size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(videoFrame), "videoFrameVector");
        }
#endif
        videoFrameVector.clear();
    }
    return silenceFrame;
}
