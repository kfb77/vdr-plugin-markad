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
    tsyslog("get_hw_format(): called with %d %s", *pix_fmts, av_get_pix_fmt_name(*pix_fmts));
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


cDecoder::cDecoder(const char *recDir, int threadsParam, const bool fullDecodeParam, char *hwaccelParam, const bool forceHWparam, const bool forceInterlacedParam, cIndex *indexParam) {
    LogSeparator(true);
    dsyslog("cDecoder::cDecoder(): create decoder object");
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

    // FFmpeg thread count
    threads = threadsParam;
    if (threads <  1) threads =  1;
    if (threads > 16) threads = 16;

    fullDecode      = fullDecodeParam;
    index           = indexParam;     // index can be nullptr if we use no index (used in logo search)
    forceHWaccel    = forceHWparam;
    forceInterlaced = forceInterlacedParam;
    if (hwaccelParam && hwaccelParam[0] != 0) hwaccel = hwaccelParam;
    dsyslog("cDecoder::cDecoder(): init with %d thread(s), %s, hwaccel: %s %s %s", threads, (fullDecode) ? "full decode" : "i-frame decode", (hwaccel) ? hwaccel : "none", (forceHWaccel) ? "(force)" : "", (forceInterlaced) ? ", force interlaced" : "");

    av_frame_unref(&avFrame);    // reset all fields to default

// init hwaccel device
    if (hwaccel) {
        hwDeviceType = av_hwdevice_find_type_by_name(hwaccel);
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
    Reset();
    if (recordingDir) {
        FREE(strlen(recordingDir), "recordingDir");
        free(recordingDir);
    }
    dsyslog("cDecoder::cDecoder(): delete decoder object");
}


void cDecoder::Reset() {
    dsyslog("cDecoder::Reset(): reset decoder");
    av_packet_unref(&avpkt);
    av_frame_unref(&avFrame);
    av_frame_unref(&avFrameConvert);

    if (swsContext) {
        FREE(sizeof(swsContext), "swsContext");  // pointer size, real size not possible because of extern declaration, only as reminder
        sws_freeContext(swsContext);
        swsContext = nullptr;
    }

    if (avctx) {
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            audioAC3Channels[streamIndex].channelCountBefore = 0;
            audioAC3Channels[streamIndex].channelCountAfter  = 0;
            audioAC3Channels[streamIndex].pts                = -1;
            audioAC3Channels[streamIndex].videoFrameNumber   = -1;
            audioAC3Channels[streamIndex].processed          = true;
        }
    }
    FreeCodecContext();

    fileNumber       = 0;
    packetNumber     = -1;
    dtsBefore        = -1;
    decoderSendState = 0;
    eof              = false;
    sumDuration      = 0;
    decoderRestart   = true;
}


bool cDecoder::Restart() {
    dsyslog("cDecoder::Restart(): restart decoder");
    Reset();
    return(ReadNextFile());  // re-init decoder
}


int cDecoder::ResetToSW() {
    // hardware decoding failed at first packet, something is wrong maybe not supported codec
    // during init we got no error code
    // cleanup current decoder
    // init decoder without hwaccel
    dsyslog("cDecoder::ResetToSW(): reset hardware decoder und init software decoder");
    Reset();
    useHWaccel = false;
    ReadNextFile();  // re-init decoder without hwaccel
    ReadPacket();
    return(SendPacketToDecoder(false));
}


bool cDecoder::GetFullDecode() const {
    return fullDecode;
}


int cDecoder::GetThreads() const {
    return threads;
}


char *cDecoder::GetHWaccelName() {
    if (useHWaccel && hwaccel) return hwaccel;
    return nullptr;
}


bool cDecoder::GetForceHWaccel() const {
    return forceHWaccel;
}


const char* cDecoder::GetRecordingDir() const {
    return recordingDir;
}


int cDecoder::GetThreadCount() const {
    return threads;
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
        if (fileNumber > maxFileNumber) maxFileNumber = fileNumber;
        ret = InitDecoder(filename);
        if (!ret && useHWaccel) {
            esyslog("cDecoder:::ReadNextFile(): init decoder with hwaccel %s failed, try fallback to software decoder", hwaccel);
            useHWaccel = false;
            ret = InitDecoder(filename);
        }
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


AVFormatContext *cDecoder::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoder::GetAVCodecContext() {
    return codecCtxArray;
}


enum AVHWDeviceType cDecoder::GetHWDeviceType() const {
    return hwDeviceType;
}


void cDecoder::FreeCodecContext() {
    if (hw_device_ctx) {
        dsyslog("cDecoder::FreeCodecContext(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(hw_device_ctx));
        av_buffer_unref(&hw_device_ctx);  // have to unref both to reduce ref-counter
    }
    if (avctx && codecCtxArray) {
        for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
            if (codecCtxArray[streamIndex]) {
                if(codecCtxArray[streamIndex]->hw_device_ctx) {
                    dsyslog("cDecoder::FreeCodecContext(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(codecCtxArray[streamIndex]->hw_device_ctx));
                    FREE(sizeof(*hw_device_ctx), "hw_device_ctx");
                    av_buffer_unref(&codecCtxArray[streamIndex]->hw_device_ctx);
                }
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

}


// copied and adapted from https://github.com/rellla/vdr-plugin-softhddevice-drm-gles/blob/4c85964f56682c9d73a72731916f742340ed308e/video_drm.c#L250C1-L267C1
size_t cDecoder::ReadLineFromFile(char *buf, size_t size, char *file) {
    FILE *fd = NULL;
    size_t character;

    fd = fopen(file, "r");
    if (fd == NULL) {
        esyslog("cDecoder::ReadLineFromFile((): Can't open file %s", file);
        return 0;
    }

    character = getline(&buf, &size, fd);
    fclose(fd);
    return character;
}


// copied and adapted from https://github.com/rellla/vdr-plugin-softhddevice-drm-gles/blob/4c85964f56682c9d73a72731916f742340ed308e/video_drm.c#L268C1-L306C2
void cDecoder::ReadHWPlatform(sCodecInfo *codecInfo) {
    char *txt_buf;
    char *read_ptr;
    size_t bufsize = 128;
    size_t read_size;

    txt_buf = static_cast<char *>(calloc(bufsize, sizeof(char)));

    char fileName[] = "/sys/firmware/devicetree/base/compatible";
    read_size = ReadLineFromFile(txt_buf, bufsize, fileName);
    if (!read_size) {
        free(static_cast<void *>(txt_buf));
        esyslog("cDecoder::ReadHWPlatform(): failed to read /sys/firmware/devicetree/base/compatible");
        return;
    }
    read_ptr = txt_buf;

    while(read_size) {
        if (strstr(read_ptr, "bcm2711")) {
            dsyslog("cDecoder::ReadHWPlatform(): CPU Broadcom bcm2711 (e.g. RasPi 4) found, disable MPGEG2 hwaccel and disable use of hardware device");
            codecInfo->noHWaccelMPEG2 = true;
            codecInfo->hwaccelDevice  = false;
            break;
        }
        if (strstr(read_ptr, "amlogic")) {
            dsyslog("cDecoder::ReadHWPlatform(): CPU amlogic (e.g. Odroid N2+) found, enable MPGEG2 hwaccel and enable use of hardware device");
            codecInfo->hwaccelDevice = false;
            break;
        }
        read_size -= (strlen(read_ptr) + 1);
        read_ptr = static_cast<char *>(&read_ptr[(strlen(read_ptr) + 1)]);
    }
    free(static_cast<void *>(txt_buf));
    return;
}


void cDecoder::GetVideoCodec(AVCodecID codecID, sCodecInfo *codecInfo) const {
    // drm hwaccel on ARM hardware
    if (useHWaccel && (hwDeviceType == AV_HWDEVICE_TYPE_DRM)) {
        ReadHWPlatform(codecInfo);
        if (codecID == AV_CODEC_ID_H264 && !codecInfo->hwaccelDevice) codecInfo->codec = avcodec_find_decoder_by_name("h264_v4l2m2m");
    }
    if (!codecInfo->codec) codecInfo->codec = avcodec_find_decoder(codecID);
}



bool cDecoder::InitDecoder(const char *filename) {
    if (!filename) return false;

    LogSeparator(false);
    dsyslog("cDecoder::InitDecoder(): filename: %s", filename);
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    FreeCodecContext();

    // open first/next file
    if (avformat_open_input(&avctx, filename, nullptr, nullptr) == 0) {
        ALLOC(sizeof(avctx), "avctx");
        dsyslog("cDecoder::InitDecoder(): opened file %s", filename);
    }
    else {
        if (fileNumber <= 1) {
            esyslog("could not open source file %s", filename);
            exit(EXIT_FAILURE);
        }
        return false;
    }
    if (avformat_find_stream_info(avctx, nullptr) < 0) {
        dsyslog("cDecoder::InitDecoder(): could not get stream infos %s", filename);
        return false;
    }

    codecCtxArray = static_cast<AVCodecContext **>(malloc(sizeof(AVCodecContext *) * avctx->nb_streams));
    ALLOC(sizeof(AVCodecContext *) * avctx->nb_streams, "codecCtxArray");
    memset(codecCtxArray, 0, sizeof(AVCodecContext *) * avctx->nb_streams);

    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        AVCodecID codec_id = avctx->streams[streamIndex]->codecpar->codec_id;
        sCodecInfo codecInfo = {};
        if (IsVideoStream(streamIndex)) {
            GetVideoCodec(codec_id, &codecInfo);
            if (!codecInfo.codec) return false;   // we can not work without video codec
        }
        else codecInfo.codec= avcodec_find_decoder(codec_id);
        if (!codecInfo.codec) {  // ignore not supported DVB subtitle by libavcodec
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

        dsyslog("cDecoder::InitDecoder(): using decoder for stream %d: codec id %5d -> id %5d: %s", streamIndex, codec_id, codecInfo.codec->id, codecInfo.codec->long_name);
        if (useHWaccel && !forceHWaccel && (codec_id == AV_CODEC_ID_MPEG2VIDEO)) {
            dsyslog("cDecoder::InitDecoder(): hwaccel is slower than software decoding with this codec, disable hwaccel");
            useHWaccel = false;
        }
        if ((firstMP2Index < 0) && (codec_id == AV_CODEC_ID_MP2)) {
            firstMP2Index = streamIndex;
        }
        if (codec_id == AV_CODEC_ID_MPEG2VIDEO && codecInfo.noHWaccelMPEG2) useHWaccel = false;

        // check if hardware acceleration device type supports codec, only video supported by FFmpeg
        if (useHWaccel && codecInfo.hwaccelDevice && IsVideoStream(streamIndex)) {
            for (int i = 0; ; i++) {
                const AVCodecHWConfig *config = avcodec_get_hw_config(codecInfo.codec, i);
                if (!config) {
                    isyslog("cDecoder::InitDecoder(): codec %s not supported by hardware acceleration device type %s", codecInfo.codec->name, av_hwdevice_get_type_name(hwDeviceType));
                    useHWaccel= false;
                    break;
                }
                dsyslog("cDecoder::InitDecoder(): codec %s: available config->methods %2d, hwDeviceType %d %s, config->device_type %d %s, pixel format %3d %s", codecInfo.codec->name, config->methods, hwDeviceType, av_hwdevice_get_type_name(hwDeviceType), config->device_type, av_hwdevice_get_type_name(config->device_type), config->pix_fmt, av_get_pix_fmt_name(config->pix_fmt));
                if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) && (config->device_type == hwDeviceType)) {
                    hw_pix_fmt = config->pix_fmt;
                    hwPixelFormat = config->pix_fmt;
                    dsyslog("cDecoder::InitDecoder(): codec %s supported by hardware acceleration device type %s with pixel format %d %s", codecInfo.codec->name, av_hwdevice_get_type_name(hwDeviceType), hw_pix_fmt, av_get_pix_fmt_name(hw_pix_fmt));
                    break;
                }
            }
        }

        dsyslog("cDecoder::InitDecoder(): create codec context");
        codecCtxArray[streamIndex] = avcodec_alloc_context3(codecInfo.codec);
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
                ALLOC(sizeof(*hw_device_ctx), "hw_device_ctx");
                dsyslog("cDecoder::InitDecoder(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(hw_device_ctx));
                dsyslog("cDecoder::InitDecoder(): linked hardware acceleration device %s successful for stream %d", av_hwdevice_get_type_name(hwDeviceType), streamIndex);
                codecCtxArray[streamIndex]->hw_device_ctx = av_buffer_ref(hw_device_ctx);
                dsyslog("cDecoder::InitDecoder(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(codecCtxArray[streamIndex]->hw_device_ctx));
                if (codecCtxArray[streamIndex]->hw_device_ctx) {
                    dsyslog("cDecoder::InitDecoder(): hardware device reference created successful for stream %d", streamIndex);
                    codecCtxArray[streamIndex]->get_format = get_hw_format;
                    // fix problem with mpeg2:
                    // AVlog(): Failed to allocate a vaapi/nv12 frame from a fixed pool of hardware frames.
                    // AVlog(): Consider setting extra_hw_frames to a larger value (currently set to -1, giving a pool size of 6).
                    codecCtxArray[streamIndex]->extra_hw_frames = 8;
                    if (logHwaccel) {
                        isyslog("use hardware acceleration %s for stream %d", av_hwdevice_get_type_name(hwDeviceType), streamIndex);
                        logHwaccel = false;
                    }
                    else dsyslog("use hardware acceleration %s for stream %d", av_hwdevice_get_type_name(hwDeviceType), streamIndex);
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

        if (avcodec_parameters_to_context(codecCtxArray[streamIndex],avctx->streams[streamIndex]->codecpar) < 0) {
            dsyslog("cDecoder::InitDecoder(): avcodec_parameters_to_context failed");
            return false;
        }
        codecCtxArray[streamIndex]->thread_count = threads;
        if (avcodec_open2(codecCtxArray[streamIndex], codecInfo.codec, nullptr) < 0) {
            dsyslog("cDecoder::InitDecoder(): avcodec_open2 failed");
            return false;
        }
        if (IsVideoStream(streamIndex)) {
            dsyslog("cDecoder::InitDecoder(): average framerate %d/%d", avctx->streams[streamIndex]->avg_frame_rate.num, avctx->streams[streamIndex]->avg_frame_rate.den);
            dsyslog("cDecoder::InitDecoder(): real    framerate %d/%d", avctx->streams[streamIndex]->r_frame_rate.num, avctx->streams[streamIndex]->r_frame_rate.den);
            if (index && (fileNumber == 1)) index->SetStartPTS(avctx->streams[streamIndex]->start_time, avctx->streams[streamIndex]->time_base);  // register stream infos in index
        }
    }
    dsyslog("cDecoder::InitDecoder(): first MP2 audio stream index: %d", firstMP2Index);

    LogSeparator(false);
    return true;
}


enum AVPixelFormat cDecoder::GetVideoPixelFormat() const {
    if (!avctx) return AV_PIX_FMT_NONE;
    for (unsigned int i = 0; i < avctx->nb_streams; i++) {
        if (codecCtxArray[avpkt.stream_index]->codec_type == AVMEDIA_TYPE_VIDEO) return codecCtxArray[avpkt.stream_index]->pix_fmt;
    }
    dsyslog("cDecoder::cDecoder::GetVideoPixelFormat(): failed");
    return AV_PIX_FMT_NONE;
}


int cDecoder::GetVideoType() const {
    if (!avctx) return 0;
    for (unsigned int i = 0; i < avctx->nb_streams; i++) {
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            switch (avctx->streams[i]->codecpar->codec_id) {
            case AV_CODEC_ID_MPEG2VIDEO:
                return MARKAD_PIDTYPE_VIDEO_H262;
                break;
            case AV_CODEC_ID_H264:
                return MARKAD_PIDTYPE_VIDEO_H264;
                break;
            case AV_CODEC_ID_H265:
                return MARKAD_PIDTYPE_VIDEO_H265;
                break;
            default:
                dsyslog("cDecoder::GetVideoType(): video coding format unknown, coded id: %i", avctx->streams[i]->codecpar->codec_id);
                return 0;
            }
        }
    }
    dsyslog("cDecoder::GetVideoType(): failed");
    return 0;
}


int cDecoder::GetVideoWidth() {
    if ((videoWidth <= 0) && avctx) {
        for (unsigned int i=0; i < avctx->nb_streams; i++) {
            if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) videoWidth = avctx->streams[i]->codecpar->width;
        }
    }
    if (videoWidth <= 0) dsyslog("cDecoder::GetVideoWidth(): failed");
    return videoWidth;
}


int cDecoder::GetVideoHeight() {
    if ((videoHeight <= 0) && avctx) {
        for (unsigned int i=0; i < avctx->nb_streams; i++) {
            if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) videoHeight = avctx->streams[i]->codecpar->height;
        }
    }
    if (videoHeight <= 0) esyslog("cDecoder::GetVideoHeight(): failed");
    return videoHeight;
}


int cDecoder::GetVideoFrameRate() {
    // always read frame rate from first file
    // found some Finnish H.264 interlaced recordings who changed real bite rate in second TS file header
    // frame rate can not change, ignore this and use cacheid frame rate from first TS file
    if (frameRate > 0) return frameRate;
    frameRate = GetVideoRealFrameRate();
    dsyslog("cDecoder::GetVideoFrameRate(): cache frame rate %d", frameRate);
    return frameRate;
}


/*
int cDecoder::GetVideoAvgFrameRate() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            return av_q2d(avctx->streams[i]->avg_frame_rate);
        }
    }
    dsyslog("cDecoder::GetVideoAvgFrameRate(): could not find average frame rate");
    return 0;
}
*/


int cDecoder::GetVideoRealFrameRate() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
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

    frameValid = false;
    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
        // check packet DTS and PTS
        if (avpkt.pts == AV_NOPTS_VALUE) {
            dsyslog("cDecoder::ReadPacket(): packet (%5d), stream %d: PTS not set", packetNumber, avpkt.stream_index);
            return true;   // false only on EOF
        }
        if (avpkt.dts == AV_NOPTS_VALUE) {
            dsyslog("cDecoder::ReadPacket(): packet (%5d), stream %d: DTS not set", packetNumber, avpkt.stream_index);
            return true;   // false only on EOF
        }

        // analyse video packet
        if (IsVideoPacket()) {
            packetNumber++;
#ifdef DEBUG_FRAME_PTS
            dsyslog("cDecoder::ReadPacket():  fileNumber %d, framenumber %5d, DTS %ld, PTS %ld, duration %ld, flags %d, dtsBefore %ld, time_base.num %d, time_base.den %d",  fileNumber, packetNumber, avpkt.dts, avpkt.pts, avpkt.duration, avpkt.flags, dtsBefore, avctx->streams[avpkt.stream_index]->time_base.num, avctx->streams[avpkt.stream_index]->time_base.den);
#endif

            // check DTS continuity
            if (dtsBefore != -1) {
                int dtsDiff = 1000 * (avpkt.dts - dtsBefore) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;
                int dtsStep = 1000 / GetVideoFrameRate();
                if (dtsDiff > dtsStep) {  // some interlaced H.264 streams have some frames with half DTS
                    if (packetNumber > decodeErrorFrame) {  // only count new frames
                        decodeErrorCount++;
                        decodeErrorFrame = packetNumber;
                    }
                    if (dtsDiff <= 0) { // ignore frames with negativ DTS difference
                        esyslog("cDecoder::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, ignore frame, decoding errors %d", packetNumber, dtsDiff, dtsStep, decodeErrorCount);
                        dtsBefore = avpkt.dts;  // store even wrong DTS to continue after error
                        return true;  // false only on EOF
                    }
                    else dsyslog("cDecoder::ReadPacket(): DTS continuity error at frame (%d), difference %dms should be %dms, decoding errors %d", packetNumber,dtsDiff, dtsStep, decodeErrorCount);
                }
            }
            dtsBefore = avpkt.dts;

            // build index
            if (index) {
                if (packetNumber > 0) sumDuration += avpkt.duration;   // offset to packet start, first packet is offset 0
                // store each frame number and pts in a PTS ring buffer
                index->AddPTS(packetNumber, avpkt.pts);

                // store PTS and sum duration of all i-frames
                if (IsVideoKeyPacket()) {
                    int64_t frameTimeOffset_ms = 1000 * static_cast<int64_t>(sumDuration) * avctx->streams[avpkt.stream_index]->time_base.num / avctx->streams[avpkt.stream_index]->time_base.den;  // need more space to calculate value
                    index->Add(fileNumber, packetNumber, avpkt.pts, frameTimeOffset_ms);
                }
            }

        }
        // analyse AC3 audio packet for channel count, we do not need to decode
        if (IsAudioAC3Packet()) {
            if (avpkt.stream_index > MAXSTREAMS) {
                esyslog("cDecoder::ReadPacket(): to much streams %i", avpkt.stream_index);
                return false;
            }
            int channelCount = 0;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            channelCount = avctx->streams[avpkt.stream_index]->codecpar->ch_layout.nb_channels;
#else
            channelCount = avctx->streams[avpkt.stream_index]->codecpar->channels;
#endif
            if ((channelCount != 2) && (channelCount != 5) && (channelCount != 6)) { // only accept valid channel counts
                esyslog("cDecoder::ReadPacket(): packet (%d), stream %d: ignore invalid channel count %d", packetNumber, avpkt.stream_index, channelCount);
            }
            else {
                if ((channelCount != 0) && (audioAC3Channels[avpkt.stream_index].channelCountBefore == 0)) {  // init with channel start
                    dsyslog("cDecoder::ReadPacket(): packet (%2d), stream %d: audio channels start with %d channels", packetNumber, avpkt.stream_index, channelCount);
                    audioAC3Channels[avpkt.stream_index].channelCountBefore = channelCount;
                }
                if (audioAC3Channels[avpkt.stream_index].processed && (channelCount != audioAC3Channels[avpkt.stream_index].channelCountBefore)) {  // if we do not use channel mark detection ignore channel changes
                    dsyslog("cDecoder::ReadPacket(): packet (%d), stream %d: audio channels changed from %d to %d at PTS %" PRId64, packetNumber, avpkt.stream_index, audioAC3Channels[avpkt.stream_index].channelCountBefore, channelCount, avpkt.pts);
                    audioAC3Channels[avpkt.stream_index].processed          = false;
                    audioAC3Channels[avpkt.stream_index].channelCountAfter  = channelCount;
                    audioAC3Channels[avpkt.stream_index].pts                = avpkt.pts;
                    audioAC3Channels[avpkt.stream_index].videoFrameNumber   = -1;
                }
            }
        }
        return true;
    }
// end of file reached
    dsyslog("cDecoder::ReadPacket(): packet (%d): end of of filenumber %d ", packetNumber, fileNumber);
    if (decodeErrorFrame == packetNumber) decodeErrorCount--; // ignore malformed last frame of a file
    av_packet_unref(&avpkt);
    return false;
}

AVPacket *cDecoder::GetPacket() {
    return &avpkt;
}


AVFrame *cDecoder::GetFrame(enum AVPixelFormat pixelFormat) {
    if (!frameValid) return nullptr;
    if (!IsVideoFrame())                return &avFrame;        // never convert audio frames
    if (pixelFormat == AV_PIX_FMT_NONE) return &avFrame;        // no pixel format requested, data planes will not be used
    if (avFrame.format == pixelFormat)  return &avFrame;        // pixel format matches target pixel format

    // have to convert pixel format to AV_PIX_FMT_YUV420P, GetFrame() only called by encoder, no double conversion from GetVideoPicture()
    if (!ConvertVideoPixelFormat(pixelFormat)) return nullptr;
    // set encoding relevant values
    avFrameConvert.format              = avFrame.format;
    avFrameConvert.pict_type           = avFrame.pict_type;
    avFrameConvert.pts                 = avFrame.pts;
    avFrameConvert.pkt_dts             = avFrame.pkt_dts;
    avFrameConvert.sample_aspect_ratio = avFrame.sample_aspect_ratio;
#if LIBAVCODEC_VERSION_INT > ((59<<16)+(37<<8)+100)   // FFmpeg 5.1.4
    avFrameConvert.duration            = avFrame.duration;
#else
    avFrameConvert.pkt_duration        = avFrame.pkt_duration;
#endif
    return &avFrameConvert;

}


int64_t cDecoder::GetPacketPTS() const {
    return avpkt.pts;
}

int cDecoder::GetVolume() {
    if (!fullDecode) return -1;                           // no audio frames decoded
    if (avpkt.stream_index != firstMP2Index) return -1;   // only check first MP2 stream

    // get volume of MP2 frame
    if (avFrame.format == AV_SAMPLE_FMT_S16P) {
        int level = 0;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        for (int channel = 0; channel < avFrame.ch_layout.nb_channels; channel++)
#else
        for (int channel = 0; channel < avFrame.channels; channel++)
#endif
        {
            const int16_t *samples = reinterpret_cast<int16_t*>(avFrame.data[channel]);
            for (int sample = 0; sample < avFrame.nb_samples; sample++) {
                level += abs(samples[sample]);
#if !defined(DEBUG_VOLUME)
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
                if ((level / avFrame.nb_samples / avFrame.ch_layout.nb_channels) > MAX_SILENCE_VOLUME) break;  // non silence reached
#else
                if ((level / avFrame.nb_samples / avFrame.channels)              > MAX_SILENCE_VOLUME) break;  // non silence reached
#endif
#endif
            }
        }
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        int normVolume =  level / avFrame.nb_samples / avFrame.ch_layout.nb_channels;
#else
        int normVolume =  level / avFrame.nb_samples / avFrame.channels;
#endif
#ifdef DEBUG_VOLUME
        dsyslog("cDecoder::GetSilence(): packet (%d), stream %d: avpkt.pts %ld: normVolume %d", packetNumber, avpkt.stream_index, avpkt.pts, normVolume);
#endif
        return normVolume;
    }
    else  esyslog("cDecoder::GetSilence(): packet (%d), stream %d: invalid format %d", packetNumber, avpkt.stream_index, avFrame.format);
    return .1;
}


sAudioAC3Channels *cDecoder::GetChannelChange() {
    if (!avctx) return nullptr;
    if (!index) return nullptr;

    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        if (!audioAC3Channels[streamIndex].processed) {
            dsyslog("cDecoder::GetChannelChange(): stream %d: unprocessed channel change", streamIndex);
            if (audioAC3Channels[streamIndex].videoFrameNumber < 0) {  // we have no video frame number, try to get from index it now
                dsyslog("cDecoder::GetChannelChange(): stream %d: calculate video packet number for PTS %" PRId64, streamIndex, audioAC3Channels[streamIndex].pts);
                if (fullDecode) {   // use PTS ring buffer to get exact video frame number
                    // 6 -> 2 channel, this will result in stop  mark, use nearest video i-frame with PTS before
                    if (audioAC3Channels[streamIndex].channelCountAfter == 2) audioAC3Channels[streamIndex].videoFrameNumber = index->GetFrameBeforePTS(audioAC3Channels[streamIndex].pts);
                    // 2 -> 6 channel, this will result in start mark, use nearest video i-frame with PTS after
                    else audioAC3Channels[streamIndex].videoFrameNumber = index->GetFrameAfterPTS(audioAC3Channels[streamIndex].pts);
                }
                else {              // use i-frame index
                    // 6 -> 2 channel, this will result in stop  mark, use nearest video i-frame with PTS before
                    if (audioAC3Channels[streamIndex].channelCountAfter == 2) audioAC3Channels[streamIndex].videoFrameNumber = index->GetIFrameBeforePTS(audioAC3Channels[streamIndex].pts);
                    // 2 -> 6 channel, this will result in start mark, use nearest video i-frame with PTS after
                    else audioAC3Channels[streamIndex].videoFrameNumber = index->GetIFrameAfterPTS(audioAC3Channels[streamIndex].pts);
                }
            }
            if (audioAC3Channels[streamIndex].videoFrameNumber < 0) {   // PTS not yet in index
                dsyslog("cDecoder::GetChannelChange(): stream %d: PTS %" PRId64 " not found in index", streamIndex, audioAC3Channels[streamIndex].pts);
                return nullptr;
            }
            return &audioAC3Channels[streamIndex];                                      // valid channel change, no need to check all streams
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


bool cDecoder::DecodePacket() {
    decoderSendState = SendPacketToDecoder(false);      // send packet to decoder, no flash flag
    if (decoderSendState != 0) return false;
    decoderReceiveState = ReceiveFrameFromDecoder();  // retry receive
    if (decoderReceiveState != 0) return false;
    return true;
}


bool cDecoder::DecodeNextFrame(const bool audioDecode) {
    // receive a frame from decoder, one decoded packet can result in more than one frame
    if (decoderRestart) {   // send initial video key packet
        if(GetPacketNumber() < 0) ReadNextPacket();        // read first packet after decoder restart
        while (!IsVideoKeyPacket()) {
            if (abortNow) return false;
            if(!ReadNextPacket()) {        // got a packet
                esyslog("cDecoder::DecodeNextFrame(): packet (%d): unable to get first key packet after restart decoder", packetNumber);
                return false;
            }
#ifdef DEBUG_DECODE_NEXT_FRAME
            dsyslog("cDecoder::DecodeNextFrame(): packet        (%5d), stream %d: avpkt.flags %d send key packet to decoder after restart decoder", packetNumber, avpkt.stream_index, avpkt.flags);
#endif
        }
        decoderSendState = SendPacketToDecoder(false);      // send key packet to decoder, no flash flag
        dsyslog("cDecoder::DecodeNextFrame(): packet (%5d), stream %d: is video key packet, start decoding", packetNumber, avpkt.stream_index);
        decoderRestart = false;
    }
    // receive decoded frame from queue
    decoderReceiveState = ReceiveFrameFromDecoder();      // receive only if first send is donw
    while (decoderReceiveState == -EAGAIN) {              // no packet ready, send next packet
        if (abortNow) return false;

        // send next packet to decoder
        switch (decoderSendState) {
        case -EIO:
            dsyslog("cDecoder::DecodeNextFrame(): packet     (%5d), stream %d: avcodec_send_packet EIO", packetNumber, avpkt.stream_index);
        case AVERROR_INVALIDDATA:
            dsyslog("cDecoder::DecodeNextFrame(): packet     (%5d), stream %d: avcodec_send_packet AVERROR_INVALIDDATA", packetNumber, avpkt.stream_index);
        case AAC_AC3_PARSE_ERROR_SYNC:
            dsyslog("cDecoder::DecodeNextFrame(): packet     (%5d), stream %d: avcodec_send_packet AAC_AC3_PARSE_ERROR_SYNC", packetNumber, avpkt.stream_index);
        case 0:  // we had successful send last packet and we can send next packet
            // send next packets
            while (true) {   // read until we got a valid packet type
                if (abortNow) return false;
                if(ReadNextPacket()) {        // got a packet
                    if (!fullDecode      && !IsVideoKeyPacket()) continue; // decode only iFrames, no audio decode without full decode
                    if (!audioDecode     && !IsVideoPacket())    continue; // decode only video frames
                    if (!IsVideoPacket() && !IsAudioPacket())    continue; // ignore all other types (e.g. subtitle
                    decoderSendState = SendPacketToDecoder(false);      // send packet to decoder, no flash flag
#ifdef DEBUG_DECODE_NEXT_FRAME
                    dsyslog("cDecoder::DecodeNextFrame(): packet        (%5d), stream %d, fullDecode %d: avpkt.flags %d send to decoder", packetNumber, avpkt.stream_index, fullDecode, avpkt.flags);
#endif
                }
                else {
                    dsyslog("cDecoder::DecodeNextFrame(): packet     (%5d): end of all ts files reached", packetNumber);
                    decoderSendState = SendPacketToDecoder(true);  // flush flag
                }
                break;
            }
            break;
        case -EAGAIN: // full queue at last send, send now again without read from input, this should not happen
            decoderSendState = SendPacketToDecoder(false);
            esyslog("cDecoder::DecodeNextFrame(): packet     (%5d): repeat send to decoder", packetNumber);
            break;
        case AVERROR_EOF:
            dsyslog("cDecoder::DecodeNextFrame(): packet     (%5d): end of file (AVERROR_EOF)", packetNumber);
            break;
        default:
            esyslog("cDecoder::DecodeNextFrame(): packet     (%5d): unexpected state of decoder send queue rc = %d: %s", packetNumber, decoderSendState, av_err2str(decoderSendState));
            return false;
            break;
        }
        // retry receive
        decoderReceiveState = ReceiveFrameFromDecoder();  // retry receive
    }

    // now we have received a frame or an error
    switch (decoderReceiveState) {
    case 0:
#ifdef DEBUG_DECODE_NEXT_FRAME
        dsyslog("cDecoder::DecodeNextFrame(): packet         (%5d): pict_type %d, received from decoder", packetNumber, avFrame.pict_type);
#endif
        return true;
        break;
    case -EIO:         // I/O error
        dsyslog("cDecoder::DecodeNextFrame(): packet  (%5d): I/O error (EIO)", packetNumber);
        if (!eof) return true;   // could be a decoding error from defect packet, try to receive next frame
        break;
    case AVERROR_EOF:  // end of file
        dsyslog("cDecoder::DecodeNextFrame(): packet  (%5d): end of file (AVERROR_EOF)", packetNumber);
        break;
    default:
        esyslog("cDecoder::DecodeNextFrame(): packet  (%5d): unexpected return code ReceiveFrameFromDecoder(): rc = %d: %s", packetNumber, decoderReceiveState, av_err2str(decoderReceiveState));
        break;
    }
    return false;
}


void cDecoder::DropFrameFromGPU() {
    // use by logo extraction for skip frame
    if (avFrame.hw_frames_ctx) {
        AVFrame *avFrameHW = av_frame_alloc();
        ALLOC(sizeof(*avFrameHW), "avFrameHW");
        av_hwframe_transfer_data(avFrameHW, &avFrame, 0);
        FREE(sizeof(*avFrameHW), "avFrameHW");
        av_frame_free(&avFrameHW);
        frameValid = false;
    }
}


bool cDecoder::ConvertVideoPixelFormat(enum AVPixelFormat pixelFormat) {
    if (!frameValid) {
        dsyslog("cDecoder::ConvertVideoPixelFormat(): frame not valid");
        return false;
    }
    Time(true);
    av_frame_unref(&avFrameConvert);
    avFrameConvert.width  = GetVideoWidth();
    avFrameConvert.height = GetVideoHeight();
    avFrameConvert.format = 0;

    int rc = av_frame_get_buffer(&avFrameConvert, 0);
    if (rc != 0) {
        av_frame_unref(&avFrameConvert);
        Time(false);
        return false;
    }
    AVFrame *avFrameSource = &avFrame;
    AVFrame *avFrameHW     = nullptr;
    // hardware decoed, receive picture from GPU and convert pixel format
    if (avFrame.format == hwPixelFormat) {        // retrieve data from GPU to CPU
        avFrameHW = av_frame_alloc();
        if (!avFrameHW) return false;
        ALLOC(sizeof(*avFrameHW), "avFrameHW");
        avFrameSource = avFrameHW;
        rc = av_hwframe_transfer_data(avFrameHW, &avFrame, 0);
        if (rc < 0 ) {
            switch (rc) {
            case -EIO:        // end of file
                dsyslog("cDecoder::ConvertVideoPixelFormat(): packet  (%5d): I/O error (EIO)", packetNumber);
                break;
            default:
                esyslog("cDecoder::ConvertVideoPixelFormat(): packet  (%5d), pict_type %d: av_hwframe_transfer_data rc = %d: %s", packetNumber, avFrame.pict_type, rc, av_err2str(rc));
                break;
            }
            FREE(sizeof(*avFrameHW), "avFrameHW");
            av_frame_free(&avFrameHW);
            avFrameHW = nullptr;
            Time(false);
            return false;
        }
    }
    // init pixel conversion
    if (!swsContext) {
        dsyslog("cDecoder::ConvertVideoPixelFormat(): video pixel format: source %s, target %s", av_get_pix_fmt_name(static_cast<enum AVPixelFormat>(avFrameSource->format)), av_get_pix_fmt_name(pixelFormat));
        swsContext = sws_getContext(GetVideoWidth(), GetVideoHeight(), static_cast<enum AVPixelFormat>(avFrameSource->format), GetVideoWidth(), GetVideoHeight(), pixelFormat, SWS_BICUBIC, NULL,NULL,NULL);
        ALLOC(sizeof(swsContext), "swsContext");  // pointer size, real size not possible because of extern declaration, only as reminder
    }
    // convert pixel format
    sws_scale(swsContext, avFrameSource->data, avFrameSource->linesize, 0, GetVideoHeight(), avFrameConvert.data, avFrameConvert.linesize);  // software decoded, use avFrame
    if (avFrameHW) {
        FREE(sizeof(*avFrameHW), "avFrameHW");
        av_frame_free(&avFrameHW);
        avFrameHW = nullptr;
    }
    Time(false);
    return true;
}


sVideoPicture *cDecoder::GetVideoPicture() {
    if (!frameValid) {
        dsyslog("cDecoder::GetVideoPicture(): frame not valid");
        return nullptr;
    }
    if (!IsVideoFrame()) {
        dsyslog("cDecoder::GetVideoPicture(): no video frame");
        return nullptr;
    }
    Time(true);
    if (videoPicture.packetNumber == packetNumber) {
        Time(false);
        return &videoPicture;  // use cached picture
    }

    AVFrame *avFrameResult = &avFrame;
    // have to convert pixel format to AV_PIX_FMT_YUV420P
    if (avFrame.format != AV_PIX_FMT_YUV420P) {
        if (!ConvertVideoPixelFormat(AV_PIX_FMT_YUV420P)) {
            dsyslog("cDecoder::GetVideoPicture(): ConvertVideoPixelFormat() from %d to %d failed", avFrame.format, AV_PIX_FMT_YUV420P);
            Time(false);
            return nullptr;    // we always use AV_PIX_FMT_YUV420P for mark detection
        }
        avFrameResult = &avFrameConvert;
    }

    // check if picture planes are valid
    bool valid = true;
    for (int i = 0; i < PLANES; i++) {
        if (avFrameResult->data[i]) {
            videoPicture.plane[i]         = avFrameResult->data[i];
            videoPicture.planeLineSize[i] = avFrameResult->linesize[i];
        }
        else valid = false;
    }
    if (valid) {
        videoPicture.packetNumber = packetNumber;
        videoPicture.width        = GetVideoWidth();
        videoPicture.height       = GetVideoHeight();
        Time(false);
        return &videoPicture;
    }
    else {
        dsyslog("cDecoder::GetVideoPicture(): picture not valid");
        Time(false);
        return nullptr;
    }
}


// seek read position to video packet <seekPacketNumber>
// seek frame is read but not decoded
bool cDecoder::SeekToPacket(int seekPacketNumber) {
    dsyslog("cDecoder::SeekToPacket(): packet (%d): seek to packet (%d)", packetNumber, seekPacketNumber);
    if (!avctx) {  // seek without init decoder before, do it now
        dsyslog("cDecoder::SeekToPacket(): seek without decoder initialized, do it now");
        if (ReadNextFile()) {
            esyslog("cDecoder::SeekToPacket(): failed to nit decoder");
            return false;
        }
    }
    // seek backward is invalid
    if (packetNumber > seekPacketNumber) {
        esyslog("cDecoder::SeekToPacket(): packet (%d): can not seek backwards to (%d)", packetNumber, seekPacketNumber);
        return false;
    }

    // no seek necessary
    if (packetNumber == seekPacketNumber) {
        dsyslog("cDecoder::SeekToPacket(): seek packet number is identical to current position (%d)", packetNumber);
        return true;
    }

    // flush decoder buffer
    // we do no decoding but maybe calling function does
    for (unsigned int streamIndex = 0; streamIndex < avctx->nb_streams; streamIndex++) {
        if (codecCtxArray[streamIndex]) {
            avcodec_flush_buffers(codecCtxArray[streamIndex]);
        }
    }

    while (ReadNextPacket()) {
        if (abortNow) return false;
        if (packetNumber >= seekPacketNumber) break;
    }
    decoderRestart = true;
    dsyslog("cDecoder::SeekToPacket(): packet (%d): seek to packet (%d) successful", packetNumber, seekPacketNumber);
    return true;
}


void cDecoder::Time(bool start) {
    if (start) {
        gettimeofday(&startDecode, nullptr); // store start time of SendPacketToDecoder()
        timeStartCalled = true;
    }
    else {
        if (timeStartCalled) {   // sometimes we call receive without send to check for more frames, ignore this
            // store end time
            struct timeval endDecode = {};
            gettimeofday(&endDecode, nullptr);
            time_t sec = endDecode.tv_sec - startDecode.tv_sec;
            suseconds_t usec = endDecode.tv_usec - startDecode.tv_usec;
            if (usec < 0) {
                usec += 1000000;
                sec--;
            }
            // calculate elapsed time and add to global statistics variable
            decodeTime_us += sec * 1000000 + usec;
            timeStartCalled = false;
        }
    }
}


int cDecoder::SendPacketToDecoder(const bool flush) {
    if (!avctx) return AVERROR_EXIT;
    if (!IsVideoPacket() && !IsAudioPacket()) {
        esyslog("cDecoder::cDecoder::SendPacketToDecoder():     packet (%5d) stream %d: type not supported", packetNumber, avpkt.stream_index);
        return AVERROR_EXIT;
    }
    Time(true);

#ifdef DEBUG_DECODER
    LogSeparator();
    dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avpkt.flags %d, avcodec_send_packet", packetNumber, avpkt.stream_index, avpkt.flags);
#endif

    int rc = 0;
    if (flush) {
        rc = avcodec_send_packet(codecCtxArray[avpkt.stream_index], nullptr);
        dsyslog("cDecoder::SendPacketToDecoder(): packet (%5d), stream %d: flush queue send", packetNumber, avpkt.stream_index);
    }
    else rc = avcodec_send_packet(codecCtxArray[avpkt.stream_index], &avpkt);
    if (rc  < 0) {
        switch (rc) {
        case AVERROR(EAGAIN):
#ifdef DEBUG_DECODER
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet error EAGAIN", packetNumber, avpkt.stream_index);
#endif
            break;
        case AVERROR(ENOMEM):
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet error ENOMEM", packetNumber, avpkt.stream_index);
            break;
        case AVERROR(EINVAL):
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet error EINVAL", packetNumber, avpkt.stream_index);
            break;
        case AVERROR_INVALIDDATA:
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet error AVERROR_INVALIDDATA", packetNumber, avpkt.stream_index);
            avcodec_flush_buffers(codecCtxArray[avpkt.stream_index]);   // cleanup buffers after invalid packet
            break;
        case AVERROR(EIO):
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d): I/O error (EIO)", packetNumber);
            break;
        case AVERROR_EOF:
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d): end of file (AVERROR_EOF)", packetNumber);
            break;
        case AAC_AC3_PARSE_ERROR_SYNC:
            dsyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC", packetNumber, avpkt.stream_index);
            break;
        default:
            esyslog("cDecoder::SendPacketToDecoder():     packet (%5d), stream %d: avcodec_send_packet failed with rc=%d: %s", packetNumber, avpkt.stream_index, rc, av_err2str(rc));
            break;
        }
        // we should not get a send error before first frame was successful received, otherwise hwaccel decoder is not working
        if (!firstHWaccelReceivedOK                               &&  // we have no frame successful received from decoder
                useHWaccel                                        &&  // we want to use hwaccel
                IsVideoStream(avpkt.stream_index)                 &&  // is video stream
                codecCtxArray[avpkt.stream_index]->hw_device_ctx) {   // hardware device is linked
            esyslog("hardware decoding failed for codec or pixel format, fallback to software decoding");
            rc = ResetToSW();
        }
    }
    return rc;
}


int cDecoder::ReceiveFrameFromDecoder() {
    if (!avctx) return AVERROR_EXIT;

    // init avFrame for new frame
    av_frame_unref(&avFrame);

    if (IsVideoPacket()) {
        if (!useHWaccel) {  // we do not need a buffer for avcodec_receive_frame() if we use hwaccel
            avFrame.height = avctx->streams[avpkt.stream_index]->codecpar->height;
            avFrame.width  = avctx->streams[avpkt.stream_index]->codecpar->width;
            avFrame.format = codecCtxArray[avpkt.stream_index]->pix_fmt;
        }
    }
    else if (IsAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        avFrame.nb_samples     = codecCtxArray[avpkt.stream_index]->ch_layout.nb_channels;
        avFrame.format         = codecCtxArray[avpkt.stream_index]->sample_fmt;
        int ret                = av_channel_layout_copy(&avFrame.ch_layout, &codecCtxArray[avpkt.stream_index]->ch_layout);
        if (ret < 0) {
            esyslog("cDecoder::ReceiveFrameFromDecoder(): av_channel_layout_copy failed, rc = %d", ret);
            av_frame_unref(&avFrame);
            Time(false);
            return AVERROR_EXIT;
        }
#else
        avFrame.nb_samples     = av_get_channel_layout_nb_channels(avctx->streams[avpkt.stream_index]->codecpar->channel_layout);
        avFrame.channel_layout = avctx->streams[avpkt.stream_index]->codecpar->channel_layout;
        avFrame.format         = avctx->streams[avpkt.stream_index]->codecpar->format;
        avFrame.sample_rate    = avctx->streams[avpkt.stream_index]->codecpar->sample_rate;
#endif
    }
    else {
        esyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%d) stream %d: type not supported", packetNumber, avpkt.stream_index);
        Time(false);
        return AVERROR_EXIT;
    }

    if (!useHWaccel || IsAudioPacket()) {   // buffer for frames without hwaccel
        int rc = av_frame_get_buffer(&avFrame, 0);
        if (rc != 0) {
            char errTXT[64] = {0};
            av_strerror(rc, errTXT, sizeof(errTXT));
            esyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%d), stream index %d: av_frame_get_buffer failed: %s", packetNumber, avpkt.stream_index, errTXT);
            av_frame_unref(&avFrame);
            Time(false);
            return AVERROR_EXIT;
        }
    }
    int rc = avcodec_receive_frame(codecCtxArray[avpkt.stream_index], &avFrame);
    if (rc < 0) {
        switch (rc) {
        case AVERROR(EAGAIN):   // frame not ready, expected with interlaced video
#ifdef DEBUG_DECODER
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%5d): avcodec_receive_frame error EAGAIN", packetNumber);
#endif
            break;
        case AVERROR(EINVAL):
            esyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%5d): avcodec_receive_frame error EINVAL", packetNumber);
            break;
        case -EIO:              // I/O error
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%5d): I/O error (EIO)", packetNumber);
            break;
        case AVERROR_EOF:       // end of file
            dsyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%5d): end of file (AVERROR_EOF)", packetNumber);
            break;
        default:
            esyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%5d): avcodec_receive_frame decode failed with return code %d", packetNumber, rc);
            break;
        }
        av_frame_unref(&avFrame);
        Time(false);
        return rc;
    }

// we got a frame check if we used hardware acceleration for this frame
    if (avFrame.hw_frames_ctx && !firstHWaccelReceivedOK) {
        dsyslog("cDecoder::ReceiveFrameFromDecoder(): first video frame received from hwaccel decoder, codec and pixel format are valid");
        firstHWaccelReceivedOK = true;
    }

// check decoding error
    if (avFrame.decode_error_flags != 0) {
        if (packetNumber > decodeErrorFrame) {  // only count new frames
            decodeErrorFrame = packetNumber;
            decodeErrorCount++;
        }
        dsyslog("cDecoder::ReceiveFrameFromDecoder(): packet (%d), stream %d: frame corrupt: decode_error_flags %d, decoding errors %d", packetNumber, avpkt.stream_index, avFrame.decode_error_flags, decodeErrorCount);
        av_frame_unref(&avFrame);
        avcodec_flush_buffers(codecCtxArray[avpkt.stream_index]);
        Time(false);
        return -EAGAIN;   // no valid frame, try decode next
    }
    // decoding successful, frame is valid
    frameValid = true;
#ifdef DEBUG_DECODER
    dsyslog("cDecoder::cDecoder::ReceiveFrameFromDecoder(): packet (%5d), stream %d: avFrame.pict_type %d, avcodec_receive_frame() successful", packetNumber, avpkt.stream_index, avFrame.pict_type);
    LogSeparator();
#endif
    Time(false);
    return 0;
}


bool cDecoder::IsVideoStream(const unsigned int streamIndex) const {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsVideoStream(): stream index %d out of range", streamIndex);
        return false;
    }
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
    return false;
}


bool cDecoder::IsVideoPacket() const {
    if (!avctx) return false;
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
    return false;
}


bool cDecoder::IsVideoKeyPacket() const {
    if (!avctx) return false;
    if (!IsVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0) return true;
    return false;
}


bool cDecoder::IsVideoFrame() const {
    if (!avctx) return false;
    if ((hwDeviceType == AV_HWDEVICE_TYPE_DRM) && IsVideoPacket()) return true;  // avFrame.pict_type not set in DRM decoded H.264 B and P frames
    if (avFrame.pict_type == AV_PICTURE_TYPE_NONE) return false;
    return true;
}


/*
bool cDecoder::IsVideoIFrame() const {
    if (!avctx) return false;
    if (avFrame.pict_type == AV_PICTURE_TYPE_I) return true;
    return false;
}
*/


bool cDecoder::IsAudioStream(const unsigned int streamIndex) const {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsAudioStream(): stream index %d out of range", streamIndex);
        return false;
    }
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
    return false;
}


bool cDecoder::IsAudioPacket() const {
    if (!avctx) return false;
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
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
#else
    if (avctx->streams[streamIndex]->codecpar->format == AUDIOFORMATAC3) return true;
#endif
    return false;
}


bool cDecoder::IsAudioAC3Packet() {
    if (!avctx) return false;
#define AUDIOFORMATAC3 8
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_id == AV_CODEC_ID_AC3 ) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codecpar->format == AUDIOFORMATAC3) return true;
#endif
    return false;
}


int cDecoder::GetAC3ChannelCount(const int streamIndex) {
    unsigned int startIndex = 0;
    unsigned int endIndex   = avctx->nb_streams;
    if (streamIndex >= 0) {
        startIndex = streamIndex;
        endIndex   = streamIndex + 1;
    }
    for (unsigned int indexStream = startIndex; indexStream < endIndex; indexStream++) {
        if (!IsAudioAC3Stream(indexStream)) continue;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        return avctx->streams[indexStream]->codecpar->ch_layout.nb_channels;
#else
        return avctx->streams[indexStream]->codecpar->channels;
#endif
    }
    return 0;
}


bool cDecoder::IsSubtitleStream(const unsigned int streamIndex) const {
    if (!avctx) return false;
    if (streamIndex >= avctx->nb_streams) {
        dsyslog("cDecoder::IsSubtitleStream(): stream index %d out of range", streamIndex);
        return false;
    }
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
    return false;
}

/*  unused
bool cDecoder::IsSubtitlePacket() {
    if (!avctx) return false;
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) return true;
    return false;
}
*/


int cDecoder::GetPacketNumber() const {
    return packetNumber;
}


bool cDecoder::IsInterlacedFrame() const {
#if LIBAVCODEC_VERSION_INT < ((60<<16)+(22<<8)+100)
    return avFrame.interlaced_frame;
#else
    return avFrame.flags & AV_FRAME_FLAG_INTERLACED;
#endif
}


sAspectRatio *cDecoder::GetFrameAspectRatio() {
    if (!frameValid) return &beforeDAR;
    DAR.num = avFrame.sample_aspect_ratio.num;
    DAR.den = avFrame.sample_aspect_ratio.den;
    if ((DAR.num == 0) || (DAR.den == 0)) {
        esyslog("cDecoder::GetFrameAspectRatio(): packet (%d): invalid aspect ratio (%d:%d)", packetNumber, DAR.num, DAR.den);
        if ((beforeDAR.num != 0) && (beforeDAR.den != 0)) {  // maybe packet error, use DAR from frame before
            return &beforeDAR;
        }
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
            esyslog("cDecoder::GetFrameAspectRatio(): packet (%d): unknown aspect ratio to video width %d hight %d", packetNumber, avFrame.width, avFrame.height);
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
            esyslog("cDecoder::GetFrameAspectRatio(): packet (%d): unknown aspect ratio (%d:%d)", packetNumber, DAR.num, DAR.den);
            return nullptr;
        }
    }
    beforeDAR = DAR;
    return &DAR;
}
