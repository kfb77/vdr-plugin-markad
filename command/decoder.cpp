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
#include "decoder.h"
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


void AVlog(__attribute__((unused)) void *ptr, int level, const char* fmt, va_list vl) {
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


cDecoder::cDecoder(const char *recDir, int threadsParam, const bool fullDecodeParam, char *hwaccel, cIndex *indexParam) {
    dsyslog("cDecoder::cDecoder(): create new decoder object");
    // recording directory
    if (!recordingDir) {
        if (asprintf(&recordingDir,"%s", recDir) == -1) {
            esyslog("cDecoder::cDecoder(): failed to allocate string, out of memory");
            return;
        }
        ALLOC(strlen(recordingDir), "recordingDir");
    }
    // libav log settings
    av_log_set_level(AVLOGLEVEL);
    av_log_set_callback(AVlog);
    // FFmpeg threads
    if (threads <  1) threads =  1;
    if (threads > 16) threads = 16;
    threads    = threadsParam;
    fullDecode = fullDecodeParam;
    dsyslog("cDecoder::cDecoder(): init with %d threads, full decode %d, hwaccel %s", threads, fullDecode, hwaccel);
    index      = indexParam;     // index can be nullptr if we use no index (used in logo search)

    av_frame_unref(&avFrame);    // reset all fields to default

// init hwaccel device
    if (hwaccel[0] != 0) {
        hwDeviceType = av_hwdevice_find_type_by_name(hwaccel);  // markad only support vaapi
        if (hwDeviceType == AV_HWDEVICE_TYPE_NONE) {
            esyslog("hardware acceleration device type %s is not supported by hardware", hwaccel);
            isyslog("available hardware acceleration device types:");
            while((hwDeviceType = av_hwdevice_iterate_types(hwDeviceType)) != AV_HWDEVICE_TYPE_NONE) isyslog(" %s", av_hwdevice_get_type_name(hwDeviceType));
            useHWaccel = false;
        }
        else {
            dsyslog("cDecoder::cDecoder(): hardware acceleration device type %s found", av_hwdevice_get_type_name(hwDeviceType));
            useHWaccel = true;
        }
    }
}


cDecoder::~cDecoder() {
    dsyslog("cDecoder::cDecoder(): delete decoder object");
    Reset();
    if (recordingDir) {
        FREE(strlen(recordingDir), "recordingDir");
        free(recordingDir);
    }
}


bool cDecoder::ReadNextFile() {
    if (!recordingDir)      return false;
    if (fileNumber >= 1000) return false;  // limit for max ts files per recording
    if (eof)                return false;

    char *filename;
    fileNumber++;
    if (asprintf(&filename, "%s/%05i.ts", recordingDir, fileNumber) == -1) {
        esyslog("cDecoder:::ReadNextFile(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename), "filename");

    // check if file exists
    dsyslog("cDecoder:::ReadNextFile(): search next file %s", filename);
    bool ret = false;
    struct stat buffer;
    int fileExists = stat(filename, &buffer);
    if (fileExists == 0 ) {
        dsyslog("cDecoder:::ReadNextFile(): next file %s found", filename);
        ret = InitDecoder(filename); // decode file
    }
    else {
        dsyslog("cDecoder:::ReadNextFile(): next file %s does not exists", filename);
        eof = true;
    }
    FREE(strlen(filename), "filename");
    free(filename);
    return ret;
}



int cDecoder::GetErrorCount() const {
    return decodeErrorCount;
}


int cDecoder::GetFileNumber() const {
    return fileNumber;
}


AVFormatContext *cDecoder::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoder::GetAVCodecContext() {
    return codecCtxArray;
}


bool cDecoder::InitDecoder(const char *filename) {
    if (!filename) return false;

    dsyslog("cDecoder::InitDecoder(): filename: %s", filename);
    AVFormatContext *avctxNextFile = nullptr;
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    // free codec context before alloc for new file
    if (codecCtxArray && avctx) {
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
        dsyslog("cDecoder::InitDecoder(): opened file %s", filename);
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
        dsyslog("cDecoder::InitDecoder(): Could not get stream infos %s", filename);
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
                dsyslog("cDecoder::InitDecoder(): ignore unsupported subtitle codec for stream %i codec id %d", streamIndex, codec_id);
                continue;
            }
            else {
                esyslog("cDecoder::InitDecoder(): could not find decoder for stream %i codec id %i", streamIndex, codec_id);
                return false;
            }
        }

        dsyslog("cDecoder::InitDecoder(): using decoder for stream %i: codec id %5i -> %s", streamIndex, codec_id, codec->long_name);
        if ((firstMP2Index < 0) && (codec_id == AV_CODEC_ID_MP2)) {
            firstMP2Index = streamIndex;
        }

        // check if hardware acceleration device type supports codec
        if (useHWaccel && IsVideoStream(streamIndex)) {
            for (int i = 0; ; i++) {
                const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
                if (!config) {
                    dsyslog("cDecoder::InitDecoder(): codec %s does not support hardware acceleration device type %s", codec->name, av_hwdevice_get_type_name(hwDeviceType));
                    useHWaccel= false;
                    break;
                }
                dsyslog("cDecoder::InitDecoder(): hw config: pix_fmt %d, method %d, device_type %d", config->pix_fmt, config->methods, config->device_type);
                if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && (config->device_type == hwDeviceType)) {
                    hw_pix_fmt = config->pix_fmt;
                    hwPixelFormat = config->pix_fmt;
                    dsyslog("cDecoder::InitDecoder(): codec %s supported by hardware acceleration device type %s with pixel format %d %s", codec->name, av_hwdevice_get_type_name(hwDeviceType), hw_pix_fmt, av_get_pix_fmt_name(hw_pix_fmt));
                    break;
                }
            }
        }

        dsyslog("cDecoder::InitDecoder(): create codec context");
        codecCtxArray[streamIndex] = avcodec_alloc_context3(codec);
        if (!codecCtxArray[streamIndex]) {
            dsyslog("cDecoder::InitDecoder(): avcodec_alloc_context3 failed");
            return false;
        }
        ALLOC(sizeof(*codecCtxArray[streamIndex]), "codecCtxArray[streamIndex]");

        // link hardware acceleration to codec context
        if (useHWaccel && (hw_pix_fmt != AV_PIX_FMT_NONE) && IsVideoStream(streamIndex)) {
            dsyslog("cDecoder::InitDecoder(): create hardware device context for %s", av_hwdevice_get_type_name(hwDeviceType));
            int ret = av_hwdevice_ctx_create(&hw_device_ctx, hwDeviceType, NULL, NULL, 0);
            if (ret >= 0) {
                dsyslog("cDecoder::InitDecoder(): linked hardware acceleration device %s successful for stream %d", av_hwdevice_get_type_name(hwDeviceType), streamIndex);
                codecCtxArray[streamIndex]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                if (codecCtxArray[streamIndex]->hw_device_ctx) {
                    dsyslog("cDecoder::InitDecoder(): hardware device reference created successful for stream %d", streamIndex);
                    codecCtxArray[streamIndex]->get_format = get_hw_format;
                    isyslog("use hardware acceleration for stream %d", streamIndex);
                }
                else {
                    dsyslog("cDecoder::InitDecoder(): hardware device reference create failed for stream %d, fall back to software decoder", streamIndex);
                    hwPixelFormat = AV_PIX_FMT_NONE;
                    useHWaccel = false;
                }
            }
            else {
                dsyslog("cDecoder::InitDecoder(): failed to link hardware acceleration device %s. Error code: %s", av_hwdevice_get_type_name(hwDeviceType), av_err2str(ret));
                useHWaccel = false;
            }
        }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avcodec_parameters_to_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codecpar) < 0)
#else
        if (avcodec_copy_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codec) < 0)
#endif
        {
            dsyslog("cDecoder::InitDecoder(): avcodec_parameters_to_context failed");
            return false;
        }
        codecCtxArray[streamIndex]->thread_count = threads;
        if (avcodec_open2(codecCtxArray[streamIndex], codec, nullptr) < 0) {
            dsyslog("cDecoder::InitDecoder(): avcodec_open2 failed");
            return false;
        }
        if (IsVideoStream(streamIndex)) {
            dsyslog("cDecoder::InitDecoder(): average framerate %d/%d", avctx->streams[streamIndex]->avg_frame_rate.num, avctx->streams[streamIndex]->avg_frame_rate.den);
            dsyslog("cDecoder::InitDecoder(): real    framerate %d/%d", avctx->streams[streamIndex]->r_frame_rate.num, avctx->streams[streamIndex]->r_frame_rate.den);
        }
    }
    dsyslog("cDecoder::InitDecoder(): first MP2 audio stream index: %d", firstMP2Index);
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


int cDecoder::GetVideoFrameRate() {
    if (frameRate > 0) return frameRate;

    // always read frame rate from first file
    // found some Finnish H.264 interlaced recordings who changed real bite rate in second TS file header
    // frame rate can not change, ignore this and keep frame rate from first TS file
    if ((GetFileNumber() == 1) && IsInterlacedVideo() && (GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264) &&
            (GetVideoAvgFrameRate() == 25) && (GetVideoRealFrameRate() == 50)) {
        frameRate = GetVideoRealFrameRate();
        dsyslog("cDecoder::GetVideoFrameRate() use real frame rate %d", frameRate);
    }
    else {
        frameRate = GetVideoAvgFrameRate();
        dsyslog("cDecoder::GetVideoFrameRate() use avg frame rate %d", frameRate);
    }
    return frameRate;

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
            return av_q2d(av_stream_get_r_frame_rate(avctx->streams[i]));
#endif
        }
    }
    dsyslog("cDecoder::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoder::ReadPacket() {
    if (!avctx) return false;
    if (eof)    return false;

    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
        // check packet DTS and PTS
        if ((avpkt.dts == AV_NOPTS_VALUE) || (avpkt.pts == AV_NOPTS_VALUE)) {
            dsyslog("cDecoder::ReadPacket(): framenumber %5d: invalid packet, DTS or PTS not set", packetNumber);
            return true;   // false only on EOF
        }
        // analyse video packet
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
#else
        if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
#endif
        {
            packetNumber++;
            if (packetNumber > 0) sumDuration += avpkt.duration;   // first packet is offset 0

#ifdef DEBUG_FRAME_PTS
            dsyslog("cDecoder::ReadPacket():  fileNumber %d, framenumber %5d, DTS %ld, PTS %ld, duration %ld, flags %d, dtsBefore %ld, time_base.num %d, time_base.den %d",  fileNumber, packetNumber, avpkt.dts, avpkt.pts, avpkt.duration, avpkt.flags, dtsBefore, avctx->streams[avpkt.stream_index]->time_base.num, avctx->streams[avpkt.stream_index]->time_base.den);
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
                        dsyslog("cDecoder::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, ignore frame, decoding errors %d", packetNumber, dtsDiff, dtsStep, decodeErrorCount);
                        dtsBefore = avpkt.dts;  // store even wrong DTS to continue after error
                        return true;  // false only on EOF
                    }
                    else dsyslog("cDecoder::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, decoding errors %d", packetNumber,dtsDiff, dtsStep, decodeErrorCount);
                }
            }
            dtsBefore = avpkt.dts;

        }
        // analyse AC3 audio packet for channel count, we do not need to decode
        if (IsAudioAC3Packet()) {
            if (avpkt.stream_index > MAXSTREAMS) {
                esyslog("cDecoder::ReadPacket(): to much streams %i", avpkt.stream_index);
                return false;
            }
            int channelCount = 0;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            channelCount = avctx->streams[avpkt.stream_index]->codecpar->ch_layout.nb_channels;
#else
            channelCount = avctx->streams[avpkt.stream_index]->codecpar->channels;
#endif
#else
            channelCount = avctx->streams[avpkt.stream_index]->codec->channels;
#endif
            if ((channelCount != 2) && (channelCount != 6)) { // only accept valid channel counts
                dsyslog("cDecoder::ReadPacket(): packet (%d), stream %d: ignore invalid channel count %d", packetNumber, avpkt.stream_index, channelCount);
            }
            else {
                if (channelCount != audioAC3[avpkt.stream_index].channelCount) {
                    dsyslog("cDecoder::ReadPacket(): packet (%d), stream %d: audio channels changed from %d to %d at PTS %" PRId64, packetNumber, avpkt.stream_index, audioAC3[avpkt.stream_index].channelCount, channelCount, avpkt.pts);
                    if ((channelCount != 2) || (audioAC3[avpkt.stream_index].channelCount != 0)) audioAC3[avpkt.stream_index].processed = false;  // ignore 0 -> 2 at recording start
                    audioAC3[avpkt.stream_index].channelCount     = channelCount;
                    audioAC3[avpkt.stream_index].pts              = avpkt.pts;
                    audioAC3[avpkt.stream_index].videoFrameNumber = -1;
                }
            }
        }
        return true;
    }
// end of file reached
    offsetTime_ms_LastFile = offsetTime_ms_LastRead;
    dsyslog("cDecoder::ReadPacket(): last frame of filenumber %d is (%d), end time %" PRId64 "ms (%3d:%02dmin)", fileNumber, packetNumber, offsetTime_ms_LastFile, static_cast<int> (offsetTime_ms_LastFile / 1000 / 60), static_cast<int> (offsetTime_ms_LastFile / 1000) % 60);
    if (decodeErrorFrame == packetNumber) decodeErrorCount--; // ignore malformed last frame of a file
    av_packet_unref(&avpkt);
    return false;
}


sAudioAC3 *cDecoder::GetChannelChange() {
    for (unsigned int streamIndex = 0; streamIndex < MAXSTREAMS; streamIndex++) {
        if (!audioAC3[streamIndex].processed) {
            dsyslog("cDecoder::GetChannelChange(): stream %d: unprocessed channel change", streamIndex);
            if (audioAC3[streamIndex].videoFrameNumber < 0) {  // we have no video frame number, try to get from index it now
                dsyslog("cDecoder::GetChannelChange(): stream %d: calculate video from number for PTS %ld", streamIndex, audioAC3[streamIndex].pts);
                if (fullDecode) {   // use PTS ring buffer to get exact video frame number
                    if (audioAC3[streamIndex].channelCount == 2) {  // 6 -> 2 channel, this will result in stop  mark, use nearest video i-frame with PTS before
                    }
                    else {                                                 // 2 -> 6 channel, this will result in start mark, use nearest video i-frame with PTS after
                    }
                }
                else {              // use i-frame index
                    // 6 -> 2 channel, this will result in stop  mark, use nearest video i-frame with PTS before
                    if (audioAC3[streamIndex].channelCount == 2) audioAC3[streamIndex].videoFrameNumber = index->GetIFrameBeforePTS(audioAC3[streamIndex].pts);
                    // 2 -> 6 channel, this will result in start mark, use nearest video i-frame with PTS after
                    else audioAC3[streamIndex].videoFrameNumber = index->GetIFrameAfterPTS(audioAC3[streamIndex].pts);
                }
            }
            if (audioAC3[streamIndex].videoFrameNumber < 0) {   // PTS not yet in index
                dsyslog("cDecoder::GetChannelChange(): stream %d: PTS %ld not found in index", streamIndex, audioAC3[streamIndex].pts);
                return nullptr;
            }
            return &audioAC3[streamIndex];                                      // valid channel change, no need to check all streams
        }
    }
    return nullptr;
}


bool cDecoder::ReadNextPacket() {
    if (eof)             return false;
    if (ReadPacket())    return true;     // read from current input ts file successfully
    if (!ReadNextFile()) return false;    // use next file
    return ReadPacket();                  // first read from next ts file
}


bool cDecoder::DecodeNextFrame(const bool audioDecode) {
    while (true) {
        if (abortNow) return false;
        switch (decoderSendState) {
        case 0:  // we had successful send last packet and we can try to send next packet
            // send next packets
            if(ReadNextPacket()) {        // got a packet
                if (!fullDecode  && !IsVideoIPacket()) continue;   // decode only iFrames, no audio decode without full decode
                if (!audioDecode && !IsVideoPacket())  continue;   // decode only video frames
                decoderSendState = SendPacketToDecoder(false);     // flash flag
#ifdef DEBUG_DECODER
                dsyslog("cDecoder::DecodeNextFrame(): packet (%d): send to decoder", packetNumber);
#endif
            }
            else {
                dsyslog("cDecoder::DecodeNextFrame(): packet (%d): end of all ts files reached", packetNumber);
                decoderSendState = SendPacketToDecoder(true);  // flush flag
            }
            break;
        case -EAGAIN: // full queue at last send, send now again without read from input
            decoderSendState = SendPacketToDecoder(false);
#ifdef DEBUG_DECODER
            dsyslog("cDecoder::DecodeNextFrame(): packet (%5d): repeat send to decoder", packetNumber);
#endif
            break;
        case AVERROR_EOF:
            dsyslog("cDecoder::DecodeNextFrame(): packet (%5d): end of file (AVERROR_EOF)", packetNumber);
            return false;
            break;
        default:
            esyslog("cDecoder::DecodeNextFrame(): packet (%5d): unexpected state of decoder send queue rc = %d: %s", packetNumber, decoderSendState, av_err2str(decoderSendState));
            return false;
            break;
        }
// now queue is filled, receive a frame from decoder
        int rc = ReceiveFrameFromDecoder();    // receive packet from decoder
        switch (rc) {
        case 0:
#ifdef DEBUG_DECODER
            dsyslog("cDecoder::DecodeNextFrame(): frame  (%5d): receive from decoder", frameNumber);
#endif
            return true;
            break;
        case -EAGAIN:        // frame not ready
#ifdef DEBUG_DECODER
            dsyslog("cDecoder::DecodeNextFrame(): frame  (%5d): no frame available in decoder (EAGAIN)", frameNumber);
#endif
            break;
        case -EIO:        // end of file
            dsyslog("cDecoder::DecodeNextFrame(): frame  (%5d): I/O error (EIO)", frameNumber);
            break;
        case AVERROR_EOF:        // end of file
            dsyslog("cDecoder::DecodeNextFrame(): frame  (%5d): end of file (AVERROR_EOF)", frameNumber);
            break;
        default:
            esyslog("cDecoder::DecodeNextFrame(): frame  (%5d): unexpected return code from decoder rc = %d: %s", frameNumber, rc, av_err2str(rc));
            return false;
            break;
        }
    }
}


sVideoPicture *cDecoder::GetVideoPicture() {
    bool valid = true;
    for (int i = 0; i < PLANES; i++) {
        if (avFrame.data[i]) {
            videoPicture.plane[i]         = avFrame.data[i];
            videoPicture.planeLineSize[i] = avFrame.linesize[i];
        }
        else valid = false;
    }
    if (valid) {
        videoPicture.frameNumber = frameNumber;
        videoPicture.width       = GetVideoWidth();
        videoPicture.height      = GetVideoHeight();
        return &videoPicture;
    }
    else return nullptr;
}


/*  for future use
AVPacket *cDecoder::GetPacket() {
    return &avpkt;
}
*/


// if we have no index, we do not preload decoder
// if we do no full decode, we seek to next i-frame
// only used by logo search, we don't care exact position in this case
bool cDecoder::SeekToFrame(int seekNumber) {
    if (!avctx) return false;
    if (!codecCtxArray) return false;
    dsyslog("cDecoder::SeekToFrame(): packet (%d), frame (%d): seek to frame (%d)", packetNumber, frameNumber, seekNumber);
    if (packetNumber > frameNumber) {
        dsyslog("cDecoder::SeekToFrame(): packet position (%d): could not seek backward to frame (%d)", packetNumber, frameNumber);
        return false;
    }

    int iFrameBefore = 0;
    if (index) iFrameBefore = index->GetIFrameBefore(seekNumber - 1);  // start decoding from iFrame before to fill decoder buffer
    else iFrameBefore = seekNumber;                                    // if we have no index, we do not take care of fill decoder
    if (iFrameBefore == -1) {
        dsyslog("cDecoder::SeekFrame(): index does not yet contain frame (%5d), decode from current frame (%d) to build index", frameNumber, packetNumber);
        iFrameBefore = 0;
    }

    // flush decoder buffer
    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        if (codecCtxArray[streamIndex]) {
            avcodec_flush_buffers(codecCtxArray[streamIndex]);
        }
    }

    // read packets from current position to iFrameBefore
    while (frameNumber < seekNumber) {    // we stop one frame before, next DecodePacket will return requestet frame
        if (abortNow) return false;
        if (!ReadPacket()) {
            dsyslog("cDecoder::SeekToFrame(): packet (%d), frame (%d): seek to frame (%d) failed to read next packet", packetNumber, frameNumber, seekNumber);
            return false;
        }
        if (packetNumber >= iFrameBefore) {  // start decoding to fill decoder queue
            if (!DecodeNextFrame(false)) {   // no audio decode
                dsyslog("cDecoder::SeekToFrame(): packet (%d), frame (%d): seek to frame (%d) failed to decode next frame", packetNumber, frameNumber, seekNumber);
                return false;
            }
        }
    }
    if (fullDecode) dsyslog("cDecoder::SeekToFrame(): packet (%d), frame (%d): seek to frame (%d) successful", packetNumber, frameNumber, seekNumber);
    else dsyslog("cDecoder::SeekToFrame(): packet (%d), frame (%d): seek to i-frame after (%d) successful", packetNumber, frameNumber, seekNumber);
    return true;
}


void cDecoder::Reset() {
    dsyslog("cDecoder::Reset(): reset decoder");
    av_packet_unref(&avpkt);
    av_frame_unref(&avFrame);
    if (hw_device_ctx) av_buffer_unref(&hw_device_ctx);
    if (nv12_to_yuv_ctx) {
        FREE(sizeof(nv12_to_yuv_ctx), "nv12_to_yuv_ctx");  // pointer size, real size not possible because of extern declaration, only as reminder
        sws_freeContext(nv12_to_yuv_ctx);
        nv12_to_yuv_ctx = nullptr;
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
        codecCtxArray = nullptr;
    }
    if (avctx) {
        FREE(sizeof(avctx), "avctx");
        avformat_close_input(&avctx);
        avctx = nullptr;
    }
    fileNumber       = 0;
    packetNumber     = -1;
    frameNumber      = -1;
    dtsBefore        = -1;
    decoderSendState = 0;
    eof              = false;
    packetFrameMap.clear();
    sumDuration      = 0;
}


void cDecoder::Restart() {
    dsyslog("cDecoder::Restart(): restart decoder");
    Reset();
    ReadNextFile();  // re-init decoder
}


int cDecoder::ResetToSW() {
    // hardware decoding faild at first packet, something is wrong maybe not supported codec
    // during init we got no error code
    // cleanup current decoder
    // init decoder without hwaccel
    dsyslog("cDecoder::ResetToSW(): reset hardware decoder und init software decoder");
    Reset();
    useHWaccel   = false;
    ReadNextFile();  // re-init decoder without hwaccel
    ReadPacket();
    return(SendPacketToDecoder(false));
}


int cDecoder::SendPacketToDecoder(const bool flush) {
    if (!avctx) return AVERROR_EXIT;

    int rc = 0;
    if (flush) {
        rc = avcodec_send_packet(codecCtxArray[avpkt.stream_index], nullptr);
        dsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: flush queue send", packetNumber, avpkt.stream_index);
    }
    else       rc = avcodec_send_packet(codecCtxArray[avpkt.stream_index], &avpkt);
    if (rc  < 0) {
        switch (rc) {
        case AVERROR(EAGAIN):
            tsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet error EAGAIN", packetNumber, avpkt.stream_index);
            break;
        case AVERROR(ENOMEM):
            dsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet error ENOMEM", packetNumber, avpkt.stream_index);
            break;
        case AVERROR(EINVAL):
            dsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet error EINVAL", packetNumber, avpkt.stream_index);
            break;
        case AVERROR_INVALIDDATA:
            dsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet error AVERROR_INVALIDDATA", packetNumber, avpkt.stream_index);
            break;
        case -EIO:
            dsyslog("cDecoderWER::SendPacketToDecoder(): packet (%5d): I/O error (EIO)", frameNumber);
            break;
        case AVERROR_EOF:
            dsyslog("cDecoderWER::SendPacketToDecoder(): packet (%5d): end of file (AVERROR_EOF)", frameNumber);
            break;
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
        case AAC_AC3_PARSE_ERROR_SYNC:
            dsyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC", packetNumber, avpkt.stream_index);
            break;
#endif
        default:
            esyslog("cDecoder::SendPacketToDecoder(): packet (%d), stream %d: avcodec_send_packet failed with rc=%d: %s", packetNumber, avpkt.stream_index, rc, av_err2str(rc));
            break;
        }
        if ((frameNumber < 0)                                     &&  // we have no frame successful decoded
                useHWaccel                                        &&  // we want to use hwaccel
                IsVideoStream(avpkt.stream_index)                 &&  // is video stream
                !codecCtxArray[avpkt.stream_index]->hw_frames_ctx &&  // faild to get hardware frame context
                codecCtxArray[avpkt.stream_index]->hw_device_ctx) {   // harware device is linked
            dsyslog("cDecoder::SendPacketToDecoder(): packet (%d): stream %d: hardware decoding failed, fallback to software decoding", packetNumber, avpkt.stream_index);
            rc = ResetToSW();
        }
    }

    // push current packet number and sum duration to ring buffer
    if (!fullDecode && (rc == 0)) {
        sPacketFrameMap nextPacketFrameMap;
        nextPacketFrameMap.frameNumber = packetNumber;
        nextPacketFrameMap.sumDuration = sumDuration;
        packetFrameMap.push_back(nextPacketFrameMap);
    }
    return rc;
}


int cDecoder::ReceiveFrameFromDecoder() {
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

    if (!useHWaccel) {  // we do not need a buffer for avcodec_receive_frame() if we use hwaccel
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
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): not ready, retry later", frameNumber);
            break;
        case AVERROR(EINVAL):
            esyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): avcodec_receive_frame error EINVAL", frameNumber);
            break;
        case -EIO:        // end of file
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%5d): I/O error (EIO)", frameNumber);
            break;
        case AVERROR_EOF:        // end of file
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%5d): end of file (AVERROR_EOF)", frameNumber);
            break;
        default:
            esyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): avcodec_receive_frame decode failed with return code %d", frameNumber, rc);
            break;
        }
        av_frame_unref(&avFrame);
        return rc;
    }

    // we got a frame and have used hardware acceleration
    if (useHWaccel) {
        // retrieve data from GPU to CPU
        AVFrame swFrame = {};
        av_frame_unref(&swFrame);    // need to reset fields, other coredump, maybe bug in FFmepeg

        rc = av_hwframe_transfer_data(&swFrame, &avFrame, 0);
        if (rc < 0 ) {
            switch (rc) {
            case -EIO:        // end of file
                dsyslog("cDecoder::ReceiveFrameFromDecoder(): frame  (%5d): I/O error (EIO)", frameNumber);
                break;
            default:
                esyslog("cDecoder::ReceiveFrameFromDecoder(): frame (%d): av_hwframe_transfer_data rc = %d: %s", frameNumber, rc, av_err2str(rc));
                break;
            }
            av_frame_unref(&swFrame);
            av_frame_unref(&avFrame);
            return rc;
        }
        swFrame.sample_aspect_ratio = avFrame.sample_aspect_ratio;  // need to set before pixel format transformation
        // some filds in avFrame filled by receive, but no video data, copy to swFrame to keep it after pixel format transformation
        AVPictureType pict_type     = avFrame.pict_type;
        int64_t pts                 = avFrame.pts;
        int64_t duration            = avFrame.duration;

        // transform pixel format
        av_frame_unref(&avFrame);
        avFrame.height = avctx->streams[avpkt.stream_index]->codecpar->height;
        avFrame.width  = avctx->streams[avpkt.stream_index]->codecpar->width;
        avFrame.format = 0;
        rc = av_frame_get_buffer(&avFrame, 0);
        if (rc != 0) {
            av_frame_unref(&swFrame);
            av_frame_unref(&avFrame);
            return rc;
        }

        if (!nv12_to_yuv_ctx) {
            nv12_to_yuv_ctx = sws_getContext(swFrame.width, swFrame.height, AV_PIX_FMT_NV12, swFrame.width, swFrame.height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL,NULL,NULL);
            ALLOC(sizeof(nv12_to_yuv_ctx), "nv12_to_yuv_ctx");  // pointer size, real size not possible because of extern declaration, only as reminder
        }

        sws_scale(nv12_to_yuv_ctx, swFrame.data, swFrame.linesize, 0, swFrame.height, avFrame.data, avFrame.linesize);

        // restore values we lost during pixel transformation
        avFrame.pict_type           = pict_type;
        avFrame.pts                 = pts;
        avFrame.duration            = duration;
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
        return AVERROR_EXIT;
    }

    // we got a frame, set new frame number
    int frameSumDuration = 0;
    if (!fullDecode && !packetFrameMap.empty()) {
        sPacketFrameMap nextPacketFrameMap = packetFrameMap.front();
        frameNumber      = nextPacketFrameMap.frameNumber;
        frameSumDuration = nextPacketFrameMap.sumDuration;
        packetFrameMap.pop_front();
    }
    else {
        frameNumber++;
        sumDuration += avFrame.duration;
        frameSumDuration = sumDuration;
    }

    // build index
    if (index) {
        // store each frame number and pts in a PTS ring buffer
        index->AddPTS(frameNumber, avFrame.pts);
        int64_t offsetTime_ms = -1;
        if (avFrame.pts != AV_NOPTS_VALUE) {
            int64_t tmp_pts = avFrame.pts - avctx->streams[0]->start_time;   // TODO remove hard coded index
            if ( tmp_pts < 0 ) {
                tmp_pts += 0x200000000;    // libavodec restart at 0 if pts greater than 0x200000000
            }
            offsetTime_ms = 1000 * tmp_pts * av_q2d(avctx->streams[0]->time_base);
            offsetTime_ms_LastRead = offsetTime_ms_LastFile + offsetTime_ms;
        }

        // store PTS of all i-frames
        if (IsVideoIFrame()) {
            iFrameCount++;
            // store iframe number and pts offset, sum frame duration in index
            int64_t frameTimeOffset_ms = 1000 * static_cast<int64_t>(frameSumDuration) * avctx->streams[0]->time_base.num / avctx->streams[0]->time_base.den;  // need more space to calculate value
            if (offsetTime_ms >= 0) index->Add(fileNumber, frameNumber, avFrame.pts, offsetTime_ms_LastFile + offsetTime_ms, frameTimeOffset_ms);
            else dsyslog("cDecoder::ReceiveFrameFromDecoder():: failed to get pts for frame %d", packetNumber);
        }
    }

    return rc;
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


bool cDecoder::IsVideoIPacket() {
    if (!avctx) return false;
    if (!IsVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0) return true;
    return false;
}

bool cDecoder::IsVideoFrame() {
    if (!avctx) return false;
    if (avFrame.pict_type == AV_PICTURE_TYPE_NONE) return false;
    return true;
}

bool cDecoder::IsVideoIFrame() {
    if (!avctx) return false;
    if (avFrame.pict_type == AV_PICTURE_TYPE_I) return true;
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


int cDecoder::GetAC3ChannelCount() {
    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        if (!IsAudioAC3Stream(streamIndex)) continue;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        return avctx->streams[streamIndex]->codecpar->ch_layout.nb_channels;
#else
        return avctx->streams[streamIndex]->codecpar->channels;
#endif
#else
        return avctx->streams[streamIndex]->codec->channels;
#endif
    }
    return 0;
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


/* for future use
int cDecoder::GetVideoPacketNumber() const {
    return packetNumber;
}
*/


int cDecoder::GetVideoFrameNumber() const {
    return frameNumber;
}


int cDecoder::GetIFrameCount() const {
    return iFrameCount;
}


bool cDecoder::IsInterlacedVideo() const {
    if (interlaced_frame > 0) return true;
    return false;
}


sAspectRatio *cDecoder::GetFrameAspectRatio() {
    DAR.num = avFrame.sample_aspect_ratio.num;
    DAR.den = avFrame.sample_aspect_ratio.den;
    if ((DAR.num == 0) || (DAR.den == 0)) {
        esyslog("cDecoder::GetAspectRatio(): invalid aspect ratio (%d:%d) at frame (%d)", DAR.num, DAR.den, frameNumber);
        return nullptr;
    }
    if ((DAR.num == 1) && (DAR.den == 1)) {
        if ((avFrame.width == 1280) && (avFrame.height  ==  720) ||   // HD ready
                (avFrame.width == 1920) && (avFrame.height  == 1080) ||   // full HD
                (avFrame.width == 3840) && (avFrame.height  == 2160)) {   // UHD
            DAR.num = 16;
            DAR.den = 9;
        }
        else {
            esyslog("cDecoder::GetAspectRatio(): unknown aspect ratio to video width %d hight %d at frame %d)", avFrame.width, avFrame.height, frameNumber);
            return nullptr;
        }
    }
    else {
        if ((DAR.num == 64) && (DAR.den == 45)) {        // generic PAR MPEG-2 for PAL
            DAR.num = 16;
            DAR.den =  9;
        }
        else if ((DAR.num == 16) && (DAR.den == 11)) {   // generic PAR MPEG-4 for PAL
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
            if ((avFrame.width == 1440) && (avFrame.height  == 1080)) { // H.264 1440x1080 PAR 4:3 -> DAR 16:9
                DAR.num = 16;
                DAR.den =  9;
            }
        }
        else if ((DAR.num == 3) && (DAR.den == 2)) {  // H.264 1280x1080
            DAR.num = 16;
            DAR.den =  9;
        }
        else {
            esyslog("cDecoder::GetAspectRatio(): unknown aspect ratio (%d:%d) at frame (%d)", DAR.num, DAR.den, frameNumber);
            return nullptr;
        }
    }
    return &DAR;
}
