/*
 * decoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <string>
#include <sys/time.h>
#include <sys/stat.h>

#include "global.h"
#ifdef WINDOWS
#include "win32/mingw64.h"
#endif
#include "decoderNEW.h"
#include "debug.h"


// global variables
extern long int decodeTime_us;
extern bool abortNow;


// make av_err2str usable in c++
#ifdef av_err2str
#undef av_err2str
#include <string>
av_always_inline std::string av_err2string(int errnum) {
    char str[AV_ERROR_MAX_STRING_SIZE];
    return av_make_error_string(str, AV_ERROR_MAX_STRING_SIZE, errnum);
}
#define av_err2str(err) av_err2string(err).c_str()
#endif  // av_err2str


void AVlogNEW(__attribute__((unused)) void *ptr, int level, const char* fmt, va_list vl) {
    if (level <= AVLOGLEVEL) {
        char *logMsg = nullptr;
        int rc = 0;
        rc = vasprintf(&logMsg, fmt, vl);
        if (rc == -1) {
            dsyslog("AVlog(): error in vasprintf");
            return;
        }
        int length = strlen(logMsg);
        ALLOC(length + 1, "logMsg");
        if (logMsg[length - 1] == '\n') logMsg[length - 1] = 0;

        if ((strcmp(logMsg, "co located POCs unavailable") == 0) || // this will happen with h.264 coding because of partial decoding
                (strcmp(logMsg, "mmco: unref short failure") == 0) ||
                (strcmp(logMsg, "number of reference frames (0+5) exceeds max (4; probably corrupt input), discarding one") == 0)) {
            tsyslog("AVlog(): %s", logMsg);
        }
        else dsyslog("AVlog(): %s", logMsg);
        FREE(length + 1, "logMsg");
        free(logMsg);
    }
    return;
}


AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;         // supported pixel format by hardware, need globol scope

static enum AVPixelFormat get_hw_format(__attribute__((unused)) AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    dsyslog("get_hw_format(): called with %d %s", *pix_fmts, av_get_pix_fmt_name(*pix_fmts));
    const enum AVPixelFormat *p;
    for (p = pix_fmts; *p != AV_PIX_FMT_NONE; p++) {
        if (*p == hw_pix_fmt) {
            dsyslog("get_hw_format(): return %d %s", *p, av_get_pix_fmt_name(*p));
            return *p;
        }
    }
    dsyslog("get_hw_format(): Failed to get HW surface format for %d %s", *pix_fmts, av_get_pix_fmt_name(*pix_fmts));
    return AV_PIX_FMT_NONE;
}


cDecoderNEW::cDecoderNEW(const char *recDir, int threadsParam, const bool fullDecodeParam, const bool vaapiParam, cIndex *indexParam) {
    dsyslog("cDecoderNEW::cDecoder(): create new decoder instance");
    // recording directory
    if (!recordingDir) {
        if (asprintf(&recordingDir,"%s", recDir) == -1) {
            esyslog("cDecoderNEW::cDecoderNEW(): failed to allocate string, out of memory");
            return;
        }
        ALLOC(strlen(recordingDir), "recordingDir");
    }
    // libav log settings
    av_log_set_level(AVLOGLEVEL);
    av_log_set_callback(AVlogNEW);
    // FFmpeg threads
    if (threads <  1) threads =  1;
    if (threads > 16) threads = 16;
    dsyslog("cDecoderNEW::cDecoder(): init with %d threads", threads);
    threads    = threadsParam;
    fullDecode = fullDecodeParam;
    vaapi      = vaapiParam;
    index      = indexParam;     // index can be nullptr if we use no index (used in logo search)

    av_frame_unref(&avFrame);    // reset all fields to default

// init vaapi device
    if (vaapi) {
        hwDeviceType = av_hwdevice_find_type_by_name("vaapi");
        if (hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
            esyslog("hardware acceleration device type vaapi is not supported");
            isyslog("available hardware acceleration device types:");
            while((hwDeviceType = av_hwdevice_iterate_types(hwDeviceType)) != AV_HWDEVICE_TYPE_NONE) isyslog(" %s", av_hwdevice_get_type_name(hwDeviceType));
            vaapi = false;
        }
        else {
            dsyslog("cDecoder::cDecoder(): hardware acceleration device type %s found", av_hwdevice_get_type_name(hwDeviceType));
        }
    }
}


cDecoderNEW::~cDecoderNEW() {
    av_packet_unref(&avpkt);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
    if (nv12_to_yuv_ctx) {
        FREE(sizeof(nv12_to_yuv_ctx), "nv12_to_yuv_ctx");  // pointer size, real size not possible because of extern declaration, only as reminder
        sws_freeContext(nv12_to_yuv_ctx);
    }
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
        dsyslog("cDecoderNEW::~cDecoder(): call avformat_close_input");
        FREE(sizeof(avctx), "avctx");
        avformat_close_input(&avctx);
    }
    if (recordingDir) {
        FREE(strlen(recordingDir), "recordingDir");
        free(recordingDir);
    }
    av_frame_unref(&avFrame);
    dsyslog("cDecoderNEW::~cDecoder(): decoder instance deleted");
}


bool cDecoderNEW::ReadNextFile() {
    if (!recordingDir) return false;

    char *filename;
    fileNumber++;
    if (asprintf(&filename, "%s/%05i.ts", recordingDir, fileNumber) == -1) {
        esyslog("cDecoderNEW:::ReadNextFile(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename), "filename");

    // check if file exists
    bool ret = false;
    struct stat buffer;
    int fileExists = stat(filename, &buffer);
    if (fileExists == 0 ) {
        dsyslog("cDecoderNEW:::ReadNextFile(): next file %s found", filename);
        ret = InitDecoder(filename); // decode file
    }
    else {
        dsyslog("cDecoderNEW:::ReadNextFile(): next file %s does not exists", filename);
    }

    FREE(strlen(filename), "filename");
    free(filename);
    return ret;
}



int cDecoderNEW::GetErrorCount() const {
    return decodeErrorCount;
}


int cDecoderNEW::GetFileNumber() const {
    return fileNumber;
}


void cDecoderNEW::Reset() {
    fileNumber   = 0;
    packetNumber = -1;
    frameNumber  = -1;
    dtsBefore    = -1;
}


AVFormatContext *cDecoderNEW::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoderNEW::GetAVCodecContext() {
    return codecCtxArray;
}


bool cDecoderNEW::InitDecoder(const char *filename) {
    if (!filename) return false;

    dsyslog("cDecoderNEW::InitDecoder(): filename: %s", filename);
    AVFormatContext *avctxNextFile = nullptr;
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
        codecCtxArray = nullptr;
    }

    // open first/next file
    if (avformat_open_input(&avctxNextFile, filename, nullptr, nullptr) == 0) {
        ALLOC(sizeof(avctxNextFile), "avctx");
        dsyslog("cDecoderNEW::InitDecoder(): opened file %s", filename);
        if (avctx) {
            FREE(sizeof(avctx), "avctx");
            avformat_close_input(&avctx);
        }
        avctx = avctxNextFile;
    }
    else {
        if (fileNumber <= 1) {
            esyslog("could not open source file %s", filename);
            exit(EXIT_FAILURE);
        }
        return false;
    }
    if (avformat_find_stream_info(avctx, nullptr) < 0) {
        dsyslog("cDecoderNEW::InitDecoder(): Could not get stream infos %s", filename);
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
        if (!codec) {  // ignore not supported DVB subtitle by libavcodec
#if LIBAVCODEC_VERSION_INT < ((59<<16)+(18<<8)+100)
            if (codec_id == 100359)
#else
            if (codec_id ==  98314)
#endif
            {   // not supported by libavcodec
                dsyslog("cDecoderNEW::InitDecoder(): ignore unsupported subtitle codec for stream %i codec id %d", streamIndex, codec_id);
                continue;
            }
            else {
                esyslog("cDecoderNEW::InitDecoder(): could not find decoder for stream %i codec id %i", streamIndex, codec_id);
                return false;
            }
        }

        dsyslog("cDecoderNEW::InitDecoder(): using decoder for stream %i: codec id %5i -> %s", streamIndex, codec_id, codec->long_name);
        if ((firstMP2Index < 0) && (codec_id == AV_CODEC_ID_MP2)) {
            firstMP2Index = streamIndex;
        }

        // check if hardware acceleration device type supports codec
        if (vaapi && IsVideoStream(streamIndex)) {
            for (int i = 0; ; i++) {
                const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
                if (!config) {
                    dsyslog("cDecoder::InitDecoder(): codec %s does not support hardware acceleration device type %s", codec->name, av_hwdevice_get_type_name(hwDeviceType));
                    break;
                }
                dsyslog("cDecoderNEW::InitDecoder(): hw config: pix_fmt %d, method %d, device_type %d", config->pix_fmt, config->methods, config->device_type);
                if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && (config->device_type == hwDeviceType)) {
                    hw_pix_fmt = config->pix_fmt;
                    hwPixelFormat = config->pix_fmt;
                    dsyslog("cDecoder::InitDecoder(): codec %s supported by hardware acceleration device type %s with pixel format %d %s", codec->name, av_hwdevice_get_type_name(hwDeviceType), hw_pix_fmt, av_get_pix_fmt_name(hw_pix_fmt));
                    break;
                }
            }
        }

        codecCtxArray[streamIndex] = avcodec_alloc_context3(codec);
        if (!codecCtxArray[streamIndex]) {
            dsyslog("cDecoderNEW::InitDecoder(): avcodec_alloc_context3 failed");
            return false;
        }
        ALLOC(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");

        // link hardware acceleration to codec context
        if (vaapi && (hw_pix_fmt != AV_PIX_FMT_NONE) && IsVideoStream(streamIndex)) {
            bool linked = true;
            int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_VAAPI, NULL, NULL, 0);
            if (ret < 0) {
                dsyslog("cDecoder::InitDecoder(): failed to link hardware acceleration device. Error code: %s", av_err2str(ret));
                linked = false;
            }
            else if (!hw_device_ctx) {
                dsyslog("cDecoder::InitDecoder(): failed to link hardware acceleration device, got no error code (FFmpeg bug ?)");
                linked = false;
            }
            if (linked) {
                dsyslog("cDecoder::InitDecoder(): linked hardware acceleration device successful for stream %d", streamIndex);
                codecCtxArray[streamIndex]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                if (!codecCtxArray[streamIndex]->hw_device_ctx) {
                    dsyslog("cDecoder::InitDecoder(): hardware device reference create failed for stream %d", streamIndex);
                    linked = false;
                }
                else dsyslog("cDecoder::InitDecoder(): hardware device reference created successful for stream %d", streamIndex);
                codecCtxArray[streamIndex]->get_format = get_hw_format;
                isyslog("use hardware acceleration for stream %d", streamIndex);
            }
            else {
                isyslog("could not initialize hardware acceleration device, fall back to software decoder");
                hwPixelFormat = AV_PIX_FMT_NONE;
                vaapi = false;
            }
        }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avcodec_parameters_to_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codecpar) < 0)
#else
        if (avcodec_copy_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codec) < 0)
#endif
        {
            dsyslog("cDecoderNEW::InitDecoder(): avcodec_parameters_to_context failed");
            return false;
        }
        codecCtxArray[streamIndex]->thread_count = threads;
        if (avcodec_open2(codecCtxArray[streamIndex], codec, nullptr) < 0) {
            dsyslog("cDecoderNEW::InitDecoder(): avcodec_open2 failed");
            return false;
        }
        if (IsVideoStream(streamIndex)) {
            dsyslog("cDecoderNEW::InitDecoder(): average framerate %d/%d", avctx->streams[streamIndex]->avg_frame_rate.num, avctx->streams[streamIndex]->avg_frame_rate.den);
            dsyslog("cDecoderNEW::InitDecoder(): real    framerate %d/%d", avctx->streams[streamIndex]->r_frame_rate.num, avctx->streams[streamIndex]->r_frame_rate.den);
        }
    }
    dsyslog("cDecoderNEW::InitDecoder(): first MP2 audio stream index: %d", firstMP2Index);
    if (fileNumber <= 1) offsetTime_ms_LastFile = 0;
    return true;
}


int cDecoderNEW::GetVideoType() {
    if (!avctx) return 0;
    for (unsigned int i = 0; i < avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            switch (avctx->streams[i]->codecpar->codec_id) {
            case AV_CODEC_ID_MPEG2VIDEO:
                dsyslog("cDecoderNEW::GetVideoType(): video coding format: H.262");
                return MARKAD_PIDTYPE_VIDEO_H262;
                break;
            case AV_CODEC_ID_H264:
                dsyslog("cDecoderNEW::GetVideoType(): video coding format: H.264");
                return MARKAD_PIDTYPE_VIDEO_H264;
                break;
            case AV_CODEC_ID_H265:
                dsyslog("cDecoderNEW::GetVideoType(): video coding format: H.265");
                return MARKAD_PIDTYPE_VIDEO_H265;
                break;
            default:
                dsyslog("cDecoderNEW::GetVideoType(): video coding format unknown, coded id: %i", avctx->streams[i]->codecpar->codec_id);
                return 0;
            }
        }
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
            if (avctx->streams[i]->codec->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
                dsyslog("cDecoderNEW::GetVideoType(): found H.262 Video");
                return MARKAD_PIDTYPE_VIDEO_H262;
            }
            if (avctx->streams[i]->codec->codec_id == AV_CODEC_ID_H264) {
                dsyslog("cDecoderNEW::GetVideoType(): found H.264 Video");
                return MARKAD_PIDTYPE_VIDEO_H264;
            }
            dsyslog("cDecoderNEW::GetVideoType(): unknown coded id %i", avctx->streams[i]->codec->codec_id);
            return 0;
        }
#endif
    }
    dsyslog("cDecoderNEW::GetVideoType(): failed");
    return 0;
}


int cDecoderNEW::GetVideoHeight() {
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
    dsyslog("cDecoderNEW::GetVideoHeight(): failed");
    return 0;
}


int cDecoderNEW::GetVideoWidth() {
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
    dsyslog("cDecoderNEW::GetVideoWidth(): failed");
    return 0;
}


int cDecoderNEW::GetVideoAvgFrameRate() {
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
    dsyslog("cDecoderNEW::GetVideoAvgFrameRate(): could not find average frame rate");
    return 0;
}


int cDecoderNEW::GetVideoRealFrameRate() {
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
    dsyslog("cDecoderNEW::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoderNEW::ReadPacket() {
    if (!avctx) return false;

    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
        if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
        {
            packetNumber++;

            // check packet DTS and PTS
            if ((avpkt.dts == AV_NOPTS_VALUE) || (avpkt.pts == AV_NOPTS_VALUE)) {
                dsyslog("cDecoderNEW::ReadPacket(): framenumber %5d: invalid packet, DTS or PTS not set", packetNumber);
                return true;   // false only on EOF
            }

            currOffset += avpkt.duration;
#ifdef DEBUG_FRAME_PTS
            dsyslog("cDecoderNEW::ReadPacket():  fileNumber %d, framenumber %5d, DTS %ld, PTS %ld, duration %ld, flags %d, dtsBefore %ld, time_base.num %d, time_base.den %d",  fileNumber, packetNumber, avpkt.dts, avpkt.pts, avpkt.duration, avpkt.flags, dtsBefore, avctx->streams[avpkt.stream_index]->time_base.num, avctx->streams[avpkt.stream_index]->time_base.den);
#endif

            // check DTS continuity
            if (dtsBefore != -1) {
                int dtsDiff = 1000 * (avpkt.dts - dtsBefore) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;
                int dtsStep = 1000 / GetVideoRealFrameRate();
                if (dtsDiff > dtsStep) {  // some interlaced H.264 streams have some frames with half DTS
                    if (packetNumber > decodeErrorFrame) {  // only count new frames
                        decodeErrorCount++;
                        decodeErrorFrame = packetNumber;
                    }
                    if (dtsDiff <= 0) { // ignore frames with negativ DTS difference
                        dsyslog("cDecoderNEW::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, ignore frame, decoding errors %d", packetNumber, dtsDiff, dtsStep, decodeErrorCount);
                        dtsBefore = avpkt.dts;  // store even wrong DTS to continue after error
                        return true;  // false only on EOF
                    }
                    else dsyslog("cDecoderNEW::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, decoding errors %d", packetNumber,dtsDiff, dtsStep, decodeErrorCount);
                }
            }
            dtsBefore = avpkt.dts;

            // TODO: move to decoding part
            if (index) {
                // store frame number and pts in a ring buffer
                index->AddPTS(packetNumber, avpkt.pts);
                int64_t offsetTime_ms = -1;
                if (avpkt.pts != AV_NOPTS_VALUE) {
                    int64_t tmp_pts = avpkt.pts - avctx->streams[avpkt.stream_index]->start_time;
                    if ( tmp_pts < 0 ) {
                        tmp_pts += 0x200000000;    // libavodec restart at 0 if pts greater than 0x200000000
                    }
                    offsetTime_ms = 1000 * tmp_pts * av_q2d(avctx->streams[avpkt.stream_index]->time_base);
                    offsetTime_ms_LastRead = offsetTime_ms_LastFile + offsetTime_ms;
                }
                if (IsVideoIPacket()) {
                    iFrameCount++;
                    // store iframe number and pts offset, sum frame duration in index
                    int64_t frameTimeOffset_ms = 1000 * static_cast<int64_t>(currOffset) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;  // need more space to calculate value
                    if (offsetTime_ms >= 0) index->Add(fileNumber, packetNumber, offsetTime_ms_LastFile + offsetTime_ms, frameTimeOffset_ms);
                    else dsyslog("cDecoderNEW::ReadPacket(): failed to get pts for frame %d", packetNumber);
                }
            }
        }
        return true;
    }
    // end of file reached
    offsetTime_ms_LastFile = offsetTime_ms_LastRead;
    dsyslog("cDecoderNEW::ReadPacket(): last frame of filenumber %d is (%d), end time %" PRId64 "ms (%3d:%02dmin)", fileNumber, packetNumber, offsetTime_ms_LastFile, static_cast<int> (offsetTime_ms_LastFile / 1000 / 60), static_cast<int> (offsetTime_ms_LastFile / 1000) % 60);
    if (decodeErrorFrame == packetNumber) decodeErrorCount--; // ignore malformed last frame of a file
    av_packet_unref(&avpkt);
    return false;
}


bool cDecoderNEW::ReadNextPacket() {
    if (ReadPacket()) return true;         // read from current input ts file successfully
    if (!ReadNextFile()) return false;    // use next file
    return ReadPacket();                   // first read from next ts file
}


bool cDecoderNEW::DecodeNextFrame() {
    while (true) {
        if (abortNow) return false;
        switch (decoderSendState) {
        case 0: { // we had successful send last packet and we can try to send next packet
            // send next packets
            if(ReadNextPacket()) {        // got a packet
                if (!fullDecode && !IsVideoIPacket()) continue;   // decode only iFrames
                if (!IsVideoPacket()) continue;                   // decode only video frames
                decoderSendState = SendPacketToDecoder();
#ifdef DEBUG_DECODER
                dsyslog("cDecoderNEW::DecodeNextFrame(): packet (%d): send to decoder", packetNumber);
#endif
            }
            break;
        }
        case -EAGAIN: // full queue at last send, send now again without read from input
            decoderSendState = SendPacketToDecoder();
#ifdef DEBUG_DECODER
            dsyslog("cDecoderNEW::DecodeNextFrame(): packet (%5d): repeat send to decoder", packetNumber);
#endif
            break;
        default:
            esyslog("cDecoderNEW::DecodeNextFrame(): packet (%5d): unexpected state of decoder send queue %d", packetNumber, decoderSendState);
            return false;
            break;
        }
// now queue is filled, receive a frame from decoder
        int rc = ReceiveFrameFromDecoder();    // receive packet from decoder
        switch (rc) {
        case 0:
#ifdef DEBUG_DECODER
            dsyslog("cDecoderNEW::DecodeNextFrame(): frame  (%5d): receive from decoder", frameNumber);
#endif
            return true;
            break;
        case -EAGAIN:        // frame not ready
#ifdef DEBUG_DECODER
            dsyslog("cDecoderNEW::DecodeNextFrame(): frame  (%5d): no frame available in decoder (EAGAIN)", frameNumber);
#endif
            break;
        default:
            esyslog("cDecoderNEW::DecodeNextFrame(): frame  (%5d): unexpected return code from decoder rc = %d", frameNumber, rc);
            return false;
            break;
        }
    }
}


sVideoPicture *cDecoderNEW::GetVideoPicture() {
    bool valid = true;
    for (int i = 0; i < PLANES; i++) {
        if (avFrame.data[i]) {
            videoPicture.Plane[i]         = avFrame.data[i];
            videoPicture.PlaneLinesize[i] = avFrame.linesize[i];
        }
        else valid = false;
    }
    if (valid) return &videoPicture;
    else return nullptr;
}


AVPacket *cDecoderNEW::GetPacket() {
    return &avpkt;
}


bool cDecoderNEW::SeekToFrame(sMarkAdContext *maContext, int frameNumber) {
    dsyslog("cDecoderNEW::SeekToFrame(): current frame position (%d), seek to frame (%d)", packetNumber, frameNumber);
    if (!avctx) return false;
    if (!maContext) return false;
    if (packetNumber > frameNumber) {
        dsyslog("cDecoderNEW::SeekToFrame(): current frame position (%d), could not seek backward to frame (%d)", packetNumber, frameNumber);
        return false;
    }
    bool logDecode = true;

    int iFrameBefore = index->GetIFrameBefore(frameNumber - 1);  // start decoding from iFrame before to fill decoder buffer
    if (iFrameBefore == -1) {
        iFrameBefore = 0;
        dsyslog("cDecoderNEW::SeekFrame(): index does not yet contain frame (%5d), decode from current frame (%d) to build index", frameNumber, packetNumber);
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
        dsyslog("cDecoderNEW::SeekToFrame(): codec context not valid");
        return false;
    }

    while (packetNumber < frameNumber) {
        if (!this->ReadPacket()) {  // build frame index but no pts index
            if (!this->ReadNextFile()) {
                dsyslog("cDecoderNEW::SeekFrame(): failed for frame (%d) at frame (%d)", frameNumber, packetNumber);
                return false;
            }
            continue;
        }
        if (packetNumber >= iFrameBefore) {
            if (logDecode) {
                dsyslog("cDecoderNEW::SeekToFrame(): start decode at frame (%d)", packetNumber);
                logDecode = false;
            }
//            GetFrameInfo(maContext, true, maContext->Config->fullDecode, false, false);  // preload decoder bufferA
        }
    }
    dsyslog("cDecoderNEW::SeekToFrame(): (%d) successful", packetNumber);
    return true;
}


int cDecoderNEW::SendPacketToDecoder() {
    if (!avctx) return AVERROR_EXIT;

    // push current packet number to ring buffer
    if (!fullDecode) frameNumberMap.push_back(packetNumber);

    int rc = avcodec_send_packet(codecCtxArray[avpkt.stream_index], &avpkt);
    if (rc  < 0) {
        switch (rc) {
        case AVERROR(EAGAIN):
            tsyslog("cDecoderNEW::SendPacketToDecoder(): avcodec_send_packet error EAGAIN at frame %d", packetNumber);
            break;
        case AVERROR(ENOMEM):
            dsyslog("cDecoderNEW::SendPacketToDecoder(): avcodec_send_packet error ENOMEM at frame %d", packetNumber);
            break;
        case AVERROR(EINVAL):
            dsyslog("cDecoderNEW::SendPacketToDecoder(): avcodec_send_packet error EINVAL at frame %d", packetNumber);
            break;
        case AVERROR_INVALIDDATA:
            dsyslog("cDecoderNEW::SendPacketToDecoder:(): avcodec_send_packet error AVERROR_INVALIDDATA at frame %d", packetNumber);
            break;
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
        case AAC_AC3_PARSE_ERROR_SYNC:
            dsyslog("cDecoderNEW::SendPacketToDecoder:(): avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC at frame %d", packetNumber);
            break;
#endif
        default:
            dsyslog("cDecoderNEW::SendPacketToDecoder(): avcodec_send_packet failed with rc=%d at frame %d",rc,packetNumber);
            break;
        }
    }
    return rc;
}


int cDecoderNEW::ReceiveFrameFromDecoder() {
    if (!avctx) return AVERROR_EXIT;

    // init avFrame for new frame
    av_frame_unref(&avFrame);

    if (IsVideoPacket()) {
        avFrame.height = avctx->streams[avpkt.stream_index]->codecpar->height;
        avFrame.width  = avctx->streams[avpkt.stream_index]->codecpar->width;
        avFrame.format = codecCtxArray[avpkt.stream_index]->pix_fmt;
    }
    else {
        if (IsAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            avFrame.nb_samples     = codecCtxArray[avpkt.stream_index]->ch_layout.nb_channels;
            avFrame.format         = codecCtxArray[avpkt.stream_index]->sample_fmt;
            int ret                = av_channel_layout_copy(&avFrame.ch_layout, &codecCtxArray[avpkt.stream_index]->ch_layout);
            if (ret < 0) {
                dsyslog("cDecoder::ReceiveFrameFromDecoder(): av_channel_layout_copy failed, rc = %d", ret);
                av_frame_unref(&avFrame);
                return AVERROR_EXIT;
            }
#else
            avFrame.nb_samples     = av_get_channel_layout_nb_channels(avctx->streams[avpkt.stream_index]->codecpar->channel_layout);
            avFrame.channel_layout = avctx->streams[avpkt.stream_index]->codecpar->channel_layout;
#endif
        }
        else {
            if (IsSubtitlePacket()) { // do not decode subtitle, even on fullencode use it without reencoding
                av_frame_unref(&avFrame);
                return AVERROR_EXIT;
            }
            else {
                dsyslog("cDecoder::ReceiveFrameFromDecoder(): stream %d type not supported", avpkt.stream_index);
                av_frame_unref(&avFrame);
                return AVERROR_EXIT;
            }
        }
    }

    if (!vaapi) {  // we do not need a buffer for avcodec_receive_frame() if we use hwaccel
        int rc = av_frame_get_buffer(&avFrame, 0);
        if (rc != 0) {
            char errTXT[64] = {0};
            av_strerror(rc, errTXT, sizeof(errTXT));
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): stream index %d: av_frame_get_buffer failed: %s", avpkt.stream_index, errTXT);
            av_frame_unref(&avFrame);
            return AVERROR_EXIT;
        }
    }
    int rc = avcodec_receive_frame(codecCtxArray[avpkt.stream_index], &avFrame);
    if (rc < 0) {
        switch (rc) {
        case AVERROR(EAGAIN):   // frame not ready
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): avcodec_receive_frame error EAGAIN", frameNumber);
            break;
        case AVERROR(EINVAL):
            esyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): avcodec_receive_frame error EINVAL", frameNumber);
            break;
        default:
            esyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): avcodec_receive_frame decode failed with return code %d", frameNumber, rc);
            break;
        }
        av_frame_unref(&avFrame);
        return rc;
    }

    // we got a frame and have used hardware acceleration
    if (vaapi) {
        // retrieve data from GPU to CPU
        AVFrame swFrame = {};
        av_frame_unref(&swFrame);    // need to reset fields, other coredump, maybe bug in FFmepeg

        rc = av_hwframe_transfer_data(&swFrame, &avFrame, 0);
        if (rc < 0 ) {
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): error transferring the data to system memory: %s", frameNumber, av_err2str(rc));
            av_frame_unref(&swFrame);
            av_frame_unref(&avFrame);
            return rc;
        }
        swFrame.sample_aspect_ratio = avFrame.sample_aspect_ratio;   // some filds from avFrame filled by receive, but no video data

        av_frame_unref(&avFrame);   // reset avFrame
        avFrame.height = avctx->streams[avpkt.stream_index]->codecpar->height;
        avFrame.width  = avctx->streams[avpkt.stream_index]->codecpar->width;
        avFrame.format = 0;
        rc = av_frame_get_buffer(&avFrame, 0);
        if (rc != 0) {
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): av_frame_get_buffer for stream %d failed rc=%d: %s", frameNumber, avpkt.stream_index, rc, av_err2str(rc));
            av_frame_unref(&swFrame);
            av_frame_unref(&avFrame);
            return rc;
        }

        if (!nv12_to_yuv_ctx) {
            nv12_to_yuv_ctx = sws_getContext(swFrame.width, swFrame.height, AV_PIX_FMT_NV12, swFrame.width, swFrame.height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,NULL,NULL);
            ALLOC(sizeof(nv12_to_yuv_ctx), "nv12_to_yuv_ctx");  // pointer size, real size not possible because of extern declaration, only as reminder
        }

        sws_scale(nv12_to_yuv_ctx, swFrame.data, swFrame.linesize, 0, swFrame.height, avFrame.data, avFrame.linesize);
        avFrame.sample_aspect_ratio = swFrame.sample_aspect_ratio;
        av_frame_unref(&swFrame);
    }

    // check decoding error
    if (avFrame.decode_error_flags != 0) {
        if (frameNumber > decodeErrorFrame) {  // only count new frames
            decodeErrorFrame = frameNumber;
            decodeErrorCount++;
        }
        dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): decoding from stream %d failed: decode_error_flags %d, decoding errors %d", frameNumber, avpkt.stream_index, avFrame.decode_error_flags, decodeErrorCount);
        av_frame_unref(&avFrame);
        avcodec_flush_buffers(codecCtxArray[avpkt.stream_index]);
    }
    if (!fullDecode && !frameNumberMap.empty()) {
        frameNumber = frameNumberMap.front();
        frameNumberMap.pop_front();
    }
    else frameNumber++;

    return rc;
}


bool cDecoderNEW::IsVideoStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoderNEW::IsVideoStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoderNEW::IsVideoPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoderNEW::IsVideoIPacket() {
    if (!avctx) return false;
    if (!IsVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0) return true;
    return false;
}


bool cDecoderNEW::IsAudioStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoderNEW::IsAudioStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoderNEW::IsAudioPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoderNEW::IsAudioAC3Stream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoderNEW::IsAudioAC3Stream(): streamindex %d out of range", streamIndex);
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


bool cDecoderNEW::IsAudioAC3Packet() {
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


bool cDecoderNEW::IsSubtitleStream(const unsigned int streamIndex) {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoderNEW::IsSubtitleStream(): stream index %d out of range", streamIndex);
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#endif
    return false;
}


bool cDecoderNEW::IsSubtitlePacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
#endif
    return false;
}


int cDecoderNEW::GetPacketNumber() const {
    return packetNumber;
}

int cDecoderNEW::GetFrameNumber() const {
    if (fullDecode) return frameNumber;
    else return packetNumber;             // if we do not decode all videos frames, we will have no valid video frame number
    // not exact because some packets are still in queue and are waitiing for decoding
}


int cDecoderNEW::GetIFrameCount() const {
    return iFrameCount;
}


bool cDecoderNEW::IsInterlacedVideo() const {
    if (interlaced_frame > 0) return true;
    return false;
}
