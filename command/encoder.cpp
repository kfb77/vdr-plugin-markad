/*
 * encoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "decoder.h"
#include "encoder.h"


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


// global variables
extern bool abortNow;


cAC3VolumeFilter::cAC3VolumeFilter() {
    dsyslog("cAC3VolumeFilter::cAC3VolumeFilter(): create object");
}


cAC3VolumeFilter::~cAC3VolumeFilter() {
    FREE(sizeof(*filterGraph), "filterGraph");
    avfilter_graph_free(&filterGraph);
    dsyslog("cAC3VolumeFilter::~cAC3VolumeFilter(): delete object");
}


#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
bool cAC3VolumeFilter::Init(const AVChannelLayout channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate)
#else
bool cAC3VolumeFilter::Init(const uint64_t channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate)
#endif
{
    AVFilterContext *volume_ctx  = nullptr;
    const AVFilter  *abuffer     = nullptr;
    const AVFilter  *volume      = nullptr;
    const AVFilter  *abuffersink = nullptr;
    char ch_layout[64]           = {};
    int err                      = 0;

#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    avfilter_register_all();
#endif
// Create a new filtergraph, which will contain all the filters
    filterGraph = avfilter_graph_alloc();
    if (!filterGraph) {
        dsyslog("cAC3VolumeFilter:Init(): Unable to create filter graph %i", AVERROR(ENOMEM));
        return false;
    }
    ALLOC(sizeof(*filterGraph), "filterGraph");

// Create the abuffer filter, it will be used for feeding the data into the graph
    abuffer = avfilter_get_by_name("abuffer");
    if (!abuffer) {
        dsyslog("cAC3VolumeFilter::Init(): Could not find the abuffer filter");
        return false;
    }
    filterSrc = avfilter_graph_alloc_filter(filterGraph, abuffer, "src");
    if (!filterSrc) {
        dsyslog("cAC3VolumeFilter::Init(): Could not allocate the abuffer instance %i", AVERROR(ENOMEM));
        return false;
    }

// Set the filter options through the AVOptions API
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    int rc = av_channel_layout_describe(&channel_layout, ch_layout, sizeof(ch_layout));
    if (rc <= 0) {
        dsyslog("cAC3VolumeFilter::Init(): av_channel_layout_describe failed, rc = %d", rc);
        return false;
    }
#else
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, (int64_t) channel_layout);
#endif

    av_opt_set(filterSrc,     "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
    av_opt_set(filterSrc,     "sample_fmt",     av_get_sample_fmt_name(sample_fmt), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(filterSrc,   "time_base",      (AVRational) {
        1, sample_rate
    }, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(filterSrc, "sample_rate",    sample_rate, AV_OPT_SEARCH_CHILDREN);

// Now initialize the filter; we pass nullptr options, since we have already set all the options above
    err = avfilter_init_str(filterSrc, nullptr);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Could not initialize the abuffer filter %i", err);
        return false;
    }

// Create volume filter
    volume = avfilter_get_by_name("volume");
    if (!volume) {
        dsyslog("cAC3VolumeFilter::Init(): Could not find the volume filter");
        return false;
    }
    volume_ctx = avfilter_graph_alloc_filter(filterGraph, volume, "volume");
    if (!volume_ctx) {
        dsyslog("cAC3VolumeFilter::Init(): Could not allocate the volume instance %i", AVERROR(ENOMEM));
        return false;
    }
    av_opt_set(volume_ctx, "volume", AV_STRINGIFY(VOLUME), AV_OPT_SEARCH_CHILDREN);
    err = avfilter_init_str(volume_ctx, nullptr);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Could not initialize the volume filter %i", err);
        return false;
    }

// Finally create the abuffersink filter, it will be used to get the filtered data out of the graph
    abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffersink) {
        dsyslog("cAC3VolumeFilter::Init(): Could not find the abuffersink filter");
        return false;
    }
    filterSink = avfilter_graph_alloc_filter(filterGraph, abuffersink, "sink");
    if (!filterSink) {
        dsyslog("cAC3VolumeFilter::Init(): Could not allocate the abuffersink instance %i", AVERROR(ENOMEM));
        return false;
    }
// This filter takes no options
    err = avfilter_init_str(filterSink, nullptr);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Could not initialize the abuffersink instance %i", err);
        return false;
    }

// Connect the filters just form a linear chain
    err = avfilter_link(filterSrc, 0, volume_ctx, 0);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Could not link abuffer to volume %i", err);
        return false;
    }
    err = avfilter_link(volume_ctx, 0, filterSink, 0);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Could not link volume to abuffersink %i", err);
        return false;
    }

// Configure the graph
    err = avfilter_graph_config(filterGraph, nullptr);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::Init(): Error configuring the filter graph %i", err);
        return false;
    }

    dsyslog("cAC3VolumeFilter::Init(): successful with channel layout %s", ch_layout);
    return true;
}


bool cAC3VolumeFilter::SendFrame(AVFrame *avFrame) {
    if (!avFrame) return false;
    if (!filterSrc) return false;
    // Send the frame to the input of the filtergraph
    int err = av_buffersrc_add_frame(filterSrc, avFrame);
    if (err < 0) {
        esyslog("cAC3VolumeFilter::SendFrame(): send frame to the filtergraph failed, rc = %d: %s", err, av_err2str(err));
        return false;
    }
    return true;
}


bool cAC3VolumeFilter::GetFrame(AVFrame *avFrame) {
    if (!avFrame) return false;
    int err = 0;
// get the frame from the filtergraph
    err = av_buffersink_get_frame(filterSink, avFrame);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::GetFrame(): error getting the frame from the filtergraph %d", err);
        return false;
    }
    return true;
}



cEncoder::cEncoder(cDecoder *decoderParam, cIndex *indexParam, const char *recDirParam, const int cutModeParam, const bool bestStreamParam, const bool ac3ReEncodeParam) {
    decoder     = decoderParam;
    index       = indexParam;
    recDir      = recDirParam;
    cutMode     = cutModeParam;
    bestStream  = bestStreamParam;
    ac3ReEncode = ac3ReEncodeParam;
    dsyslog("cEncoder::cEncoder(): init encoder with %d threads, cut mode: %d, ac3ReEncode %d", decoder->GetThreadCount(), cutMode, ac3ReEncode);
    for (unsigned int streamIndex = 0; streamIndex < MAXSTREAMS; streamIndex ++) streamMap[streamIndex] = -1;  // init to -1 in declaration does not work with some compiler
}


cEncoder::~cEncoder() {
    dsyslog("cEncoder::~cEncoder(): delete object");
    for (unsigned int i = 0; i < avctxOut->nb_streams; i++) {
        if (volumeFilterAC3[i]) {
            FREE(sizeof(*volumeFilterAC3[i]), "volumeFilterAC3");
            delete volumeFilterAC3[i];
        }
    }

    if (stats_in.data) {
        FREE(stats_in.size, "stats_in");
        free(stats_in.data);
    }

    FREE(sizeof(*avctxOut), "avctxOut");
    avformat_free_context(avctxOut);

    if (decoderLocal) {
        dsyslog("cEncoder::~cEncoder(): delete local decoder");
        FREE(sizeof(*decoderLocal), "decoderLocal");
        delete decoderLocal;
    }

    if (indexLocal) {
        FREE(sizeof(*indexLocal), "indexLocal");
        delete indexLocal;
    }
#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::~cEncoder(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
}


void cEncoder::Reset(const int passEncoder) {
    dsyslog("cEncoder::Reset(): pass %d", passEncoder);
    cutInfo.startPacketNumber   = -1;                    //!< packet number of start position
    cutInfo.startDTS            =  0;                    //!< DTS timestamp of start position
    cutInfo.startPTS            =  0;                    //!< PTS timestamp of start position
    cutInfo.stopPacketNumber    = -1;                    //!< packet number of stop position
    cutInfo.stopDTS             =  0;                    //!< DTS timestamp of stop position
    cutInfo.stopPTS             =  0;                    //!< PTS timestamp of stop position
    cutInfo.offset              =  0;                    //!< current offset from input stream to output stream
    cutInfo.offsetPTSReEncode   =  0;                    //!< additional PTS offset for re-encoded packets
    cutInfo.offsetDTSReEncode   =  0;                    //!< additional DTS offset for re-encoded packets
    cutInfo.videoPacketDuration =  0;                    //!< duration of video packet
    cutInfo.state               = CUT_STATE_FIRSTPACKET; //!< state of smart cut
    pass                        = passEncoder;
    for (unsigned int i = 0; i < MAXSTREAMS; i++) {
        streamInfo.lastOutPTS[i] = -1;
        streamInfo.lastOutDTS[i] = -1;
    }
}


void cEncoder::CheckInputFileChange() {
    if (decoder->GetFileNumber() > fileNumber) {
        dsyslog("cEncoder::CheckInputFileChange(): decoder packet (%d): input file changed from %d to %d", decoder->GetPacketNumber(), fileNumber, decoder->GetFileNumber());
        avctxIn         = decoder->GetAVFormatContext();
        codecCtxArrayIn = decoder->GetAVCodecContext();
        fileNumber      = decoder->GetFileNumber();
    }
}


bool cEncoder::OpenFile() {
    if (!recDir) return false;
    if (!decoder) return false;

    int ret = 0;
    char *filename = nullptr;
    char *buffCutName;

    avctxIn = decoder->GetAVFormatContext();
    if (!avctxIn) {
        dsyslog("cEncoder::OpenFile(): failed to get input video context");
        return false;
    }
    if (asprintf(&buffCutName,"%s", recDir) == -1) {
        dsyslog("cEncoder::OpenFile(): failed to allocate string, out of memory?");
        return false;
    }
#ifdef DEBUG_MEM
    ALLOC(strlen(buffCutName)+1, "buffCutName");
    int memsize_buffCutName = strlen(buffCutName)+1;
#endif

    char *datePart = strrchr(buffCutName, '/');
    if (!datePart) {
        dsyslog("cEncoder::OpenFile(): failed to find last '/'");
        FREE(strlen(buffCutName)+1, "buffCutName");
        free(buffCutName);
        return false;
    }
    *datePart = 0;    // cut off date part

    char *cutName = strrchr(buffCutName, '/');  // "/" exists, testet with variable datePart
    cutName++;   // ignore first char = /
    dsyslog("cEncoder::OpenFile(): cutName '%s'",cutName);

    if (asprintf(&filename,"%s/%s.ts", recDir, cutName) == -1) {
        dsyslog("cEncoder::OpenFile(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename)+1, "filename");
#ifdef DEBUG_MEM
    FREE(memsize_buffCutName, "buffCutName");
#endif
    free(buffCutName);
    datePart = nullptr;
    dsyslog("cEncoder::OpenFile(): write to '%s'", filename);

    avformat_alloc_output_context2(&avctxOut, nullptr, nullptr, filename);
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    ALLOC(sizeof(*avctxOut), "avctxOut");
    dsyslog("cEncoder::OpenFile(): output format %s", avctxOut->oformat->long_name);

    codecCtxArrayIn = decoder->GetAVCodecContext();
    if (!codecCtxArrayIn) {
        esyslog("cEncoder::OpenFile(): failed to get input codec context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }

    // find best streams (video should be stream 0)
    int bestVideoStream = av_find_best_stream(avctxIn, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (bestVideoStream < 0) {
        dsyslog("cEncoder::OpenFile(): failed to find best video stream, rc=%d", bestVideoStream);
        return false;
    }
    int bestAudioStream = av_find_best_stream(avctxIn, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (bestAudioStream < 0) {
        dsyslog("cEncoder::OpenFile(): failed to find best audio stream, rc=%d", bestAudioStream);
        return false;
    }
    if (bestStream) {
        dsyslog("cEncoder::OpenFile(): best video: stream %d", bestVideoStream);
        dsyslog("cEncoder::OpenFile(): best audio: stream %d", bestAudioStream);
    }

    // init all needed encoder streams
    for (int streamIndex = 0; streamIndex < static_cast<int>(avctxIn->nb_streams); streamIndex++) {
        if (!codecCtxArrayIn[streamIndex]) break;   // if we have no input codec we can not decode and encode this stream and all after
        if ((cutMode == CUT_MODE_FULL) && bestStream) {
            if (streamIndex == bestVideoStream) streamMap[streamIndex] = 0;
            if (streamIndex == bestAudioStream) streamMap[streamIndex] = 1;
        }
        else {
            if (decoder->IsVideoStream(streamIndex) || decoder->IsAudioStream(streamIndex) || decoder->IsSubtitleStream(streamIndex)) streamMap[streamIndex] = streamIndex;
            else {
                dsyslog("cEncoder::OpenFile(): stream %d is no audio, no video and no subtitle, ignoring", streamIndex);
                streamMap[streamIndex] = -1;
            }
        }
        dsyslog("cEncoder::OpenFile(): source stream %d -----> target stream %d", streamIndex, streamMap[streamIndex]);
        if (streamMap[streamIndex] >= 0) {  // only init codec for target streams
            if (decoder->IsAudioStream(streamIndex) && codecCtxArrayIn[streamIndex]->sample_rate == 0) {  // ignore mute audio stream
                dsyslog("cEncoder::OpenFile(): input stream %d: sample_rate not set, ignore mute audio stream", streamIndex);
                streamMap[streamIndex] = -1;
            }
            else {
                bool initCodec = false;
                if (decoder->IsVideoStream(streamIndex) && decoder->GetHWaccelName()) {    // video stream and decoder uses hwaccel
                    if (decoder->GetMaxFileNumber() == 1) {
                        useHWaccel = true;
                        initCodec = InitEncoderCodec(streamIndex, streamMap[streamIndex], true, AV_PIX_FMT_NONE, true);    // init video codec with hardware encoder
                        if (!initCodec) {
                            esyslog("cEncoder::OpenFile(): init encoder with hwaccel failed, fallback to software encoder");
                            useHWaccel = false;
                            // output stream was added, we have to restart from the beginning
                            avformat_free_context(avctxOut);
                            FREE(sizeof(*avctxOut), "avctxOut");
                            avformat_alloc_output_context2(&avctxOut, nullptr, nullptr, filename);
                            if (!avctxOut) {
                                dsyslog("cEncoder::OpenFile(): Could not create output context");
                                FREE(strlen(filename)+1, "filename");
                                free(filename);
                                return false;
                            }
                            ALLOC(sizeof(*avctxOut), "avctxOut");

                        }
                    }
                    else isyslog("cEncoder::OpenFile(): more than one input file, fallback to software encoding");
                }
                // rest of streams and fallback to software decoder
                if (!initCodec) initCodec = InitEncoderCodec(streamIndex, streamMap[streamIndex], true, AV_PIX_FMT_NONE, true);   // init codec for fallback to software decoder and non video streams
                if (!initCodec) {   // only video codecs return false on failure, without video stream, abort
                    esyslog("cEncoder::OpenFile(): InitEncoderCodec failed");
                    // cleanup memory
                    FREE(strlen(filename)+1, "filename");
                    free(filename);
                    for (unsigned int i = 0; i < avctxIn->nb_streams; i++) {
                        if (codecCtxArrayOut[i]) {
                            avcodec_free_context(&codecCtxArrayOut[i]);
                            FREE(sizeof(*codecCtxArrayOut[i]), "codecCtxArrayOut[streamIndex]");
                        }
                    }
                    return false;
                }
            }
        }
    }

    // open output file
    if (pass == 1) ret = avio_open(&avctxOut->pb, "/dev/null", AVIO_FLAG_WRITE);  // for pass 1 we do not need the output file
    else ret = avio_open(&avctxOut->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        dsyslog("cEncoder::OpenFile(): Could not open output file '%s'", filename);
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    FREE(strlen(filename)+1, "filename");
    free(filename);
    ret = avformat_write_header(avctxOut, nullptr);
    if (ret < 0) {
        dsyslog("cEncoder::OpenFile(): could not write header");
        return false;
    }
    return true;
}


bool cEncoder::ChangeEncoderCodec(const int streamIndexIn,  const int streamIndexOut, AVCodecContext *avCodecCtxIn) {
    if(!decoder) {
        dsyslog("cEncoder::ChangeEncoderCodec(): decoder not valid");
        return false;
    }
    if (!avctxIn) {
        dsyslog("cEncoder::ChangeEncoderCodec(): input format context not valid");
        return false;
    }
    if (streamIndexIn >= static_cast<int>(avctxIn->nb_streams)) {
        dsyslog("cEncoder::ChangeEncoderCodec(): input stream index %d out of range of nb_streams %d", streamIndexIn, avctxIn->nb_streams);
        return false;
    }
    if (!avCodecCtxIn) {
        dsyslog("cEncoder::ChangeEncoderCodec(): input codec context not valid");
        return false;
    }

    // cleanup output codec context
    dsyslog("cEncoder::ChangeEncoderCodec(): call avcodec_free_context");
    FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
    avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100)  // ffmpeg 4.5
    const AVCodec *codec = avcodec_find_encoder(avctxIn->streams[streamIndexIn]->codecpar->codec_id);
#else
    AVCodec *codec = avcodec_find_encoder(avctxIn->streams[streamIndexIn]->codecpar->codec_id);
#endif

    if (!codec) {
        dsyslog("cEncoder::ChangeEncoderCodec(): could not find encoder for input stream %i", streamIndexIn);
        return false;
    }
    dsyslog("cEncoder::ChangeEncoderCodec(): using decoder id %s for output stream %i", codec->long_name, streamIndexOut);

    // allocate output codec context
    codecCtxArrayOut[streamIndexOut] = avcodec_alloc_context3(codec);
    if (!codecCtxArrayOut[streamIndexOut]) {
        esyslog("cEncoder::ChangeEncoderCodec(): failed to allocate codec context for output stream %d", streamIndexOut);
        return false;
    }
    ALLOC(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");

    codecCtxArrayOut[streamIndexOut]->time_base.num = avCodecCtxIn->time_base.num;
    codecCtxArrayOut[streamIndexOut]->time_base.den = avCodecCtxIn->time_base.den;
    if (decoder->IsVideoStream(streamIndexIn)) {
        codecCtxArrayOut[streamIndexOut]->pix_fmt = avCodecCtxIn->pix_fmt;
        codecCtxArrayOut[streamIndexOut]->height  = avCodecCtxIn->height;
        codecCtxArrayOut[streamIndexOut]->width   = avCodecCtxIn->width;
    }
    else if (decoder->IsAudioStream(streamIndexIn)) {
        codecCtxArrayOut[streamIndexOut]->sample_fmt  = avCodecCtxIn->sample_fmt;
        codecCtxArrayOut[streamIndexOut]->sample_rate = avCodecCtxIn->sample_rate;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 37<<8)+100)
        int rc = av_channel_layout_copy(&codecCtxArrayOut[streamIndexOut]->ch_layout, &avctxIn->streams[streamIndexIn]->codecpar->ch_layout);
        if (rc != 0) {
            dsyslog("cEncoder::ChangeEncoderCodec(): av_channel_layout_copy for output stream %d from input stream %d  failed, rc = %d", streamIndexOut, streamIndexIn, rc);
            return false;
        }
#elif LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        codecCtxArrayOut[streamIndexOut]->ch_layout.u.mask      = avCodecCtxIn->ch_layout.u.mask;
        codecCtxArrayOut[streamIndexOut]->ch_layout.nb_channels = avCodecCtxIn->ch_layout.nb_channels;
#else
        codecCtxArrayOut[streamIndexOut]->channel_layout = avctxIn->streams[streamIndexIn]->codecpar->channel_layout;
        codecCtxArrayOut[streamIndexOut]->channels       = avctxIn->streams[streamIndexIn]->codecpar->channels;
#endif
    }
    else {
        dsyslog("cEncoder::ChangeEncoderCodec(): codec of input stream %d not supported", streamIndexIn);
        return false;
    }
    codecCtxArrayOut[streamIndexOut]->thread_count = decoder->GetThreadCount();
    if (avcodec_open2(codecCtxArrayOut[streamIndexOut], codec, nullptr) < 0) {
        dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for output stream %i failed", streamIndexOut);
        dsyslog("cEncoder::ChangeEncoderCodec(): call avcodec_free_context for stream %d", streamIndexOut);
        FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
        codecCtxArrayOut[streamIndexOut]=nullptr;
        return false;
    }
    dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for output stream %d successful", streamIndexOut);

    if (decoder->IsAudioAC3Stream(streamIndexIn)) {
        dsyslog("cEncoder::ChangeEncoderCodec(): packet (%d), frame (%d), input stream %d, output stream %d: AC3 input stream found, re-initialize volume filter", decoder->GetPacketNumber(), decoder->GetPacketNumber(), streamIndexIn, streamIndexOut);
        if (ac3ReEncode) {
            if (!volumeFilterAC3[streamIndexOut]) {
                dsyslog("cEncoder::ChangeEncoderCodec(): volumeFilterAC3 not initialized for output stream %i", streamIndexOut);
                return false;
            }
            FREE(sizeof(*volumeFilterAC3[streamIndexOut]), "volumeFilterAC3");
            delete volumeFilterAC3[streamIndexOut];
            volumeFilterAC3[streamIndexOut] = new cAC3VolumeFilter();
            ALLOC(sizeof(*volumeFilterAC3[streamIndexOut]), "volumeFilterAC3");
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            if (!volumeFilterAC3[streamIndexOut]->Init(avctxIn->streams[streamIndexIn]->codecpar->ch_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate))
#else
            if (!volumeFilterAC3[streamIndexOut]->Init(avctxIn->streams[streamIndexIn]->codecpar->channel_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate))
#endif
            {
                dsyslog("cEncoder::ChangeEncoderCodec(): volumeFilterAC3->Init() failed");
                return false;
            }
        }
    }
    return true;
}


// check stats to avoid assert in avcodec_open2
// Assertion picture_number < rcc->num_entries failed at src/libavcodec/ratecontrol.c:587
//
bool cEncoder::CheckStats(const int max_b_frames) const {
    if (!stats_in.data) return false;
    char *p = stats_in.data;
    int line = 0;
    int max_in = 0;
    bool status = true;
    while(status) {
        int in = -1;
        int out = -1;
        sscanf(p, "in:%d out:%d", &in, &out);
        if ((in == -1) || (out == -1)) {
            dsyslog("cEncoder::CheckStats(): in %d or out %d frame number not found in line %d\n%s", in, out, line, p);
            status = false;
        }
        if (in > max_in) max_in = in;
        if (line != out) {
            dsyslog("cEncoder::CheckStats(): out frame number %d no not match line number %d\n%s", out, line, p);
            status = false;
        }
        p = strchr(p, ';');  // go to end of line
        if (!p) {
            status = true;
            break;
        }
        p = strstr(p, "in:");  // go to start of next line
        if (!p) {
            status = true;
            break;
        }
        line++;
    }
    if (status) {
        if (max_in <= (line + max_b_frames)) {  // maximum valid frame in
            dsyslog("cEncoder::CheckStats(): stats is valid, use it");
            return true;
        }
        else dsyslog("cEncoder::CheckStats(): line number %d, max frame number in %d, stats not valid, libavcodec would throw: Assertion picture_number < rcc->num_entries failed at src/libavcodec/ratecontrol.c:587", line, max_in);
    }
    esyslog("invalid stats input from pass 1, fallback to one pass encoding");
    return false;
}


char *cEncoder::GetEncoderName(const int streamIndexIn) {
    // map decoder name to encoder name
    // codec         vaapi           cuda
    // mpeg2video -> mpeg2_vaapi
    // h264       -> h264_vaapi      h264_nvenc
    // HVEC
    char *encoderName = nullptr;
    char *hwaccelName = decoder->GetHWaccelName();
    if (!hwaccelName) {
        esyslog("cEncoder::GetEncoderName(): hwaccel name not set");
        return nullptr;
    }
    if (strcmp(codecCtxArrayIn[streamIndexIn]->codec->name, "mpeg2video") == 0 ) {
        if (asprintf(&encoderName,"mpeg2_%s", hwaccelName) == -1) {
            dsyslog("cEncoder::GetEncoderName(): failed to allocate string, out of memory");
            return nullptr;
        }
        ALLOC(strlen(encoderName) + 1, "encoderName");
        return encoderName;
    }
    if (strcmp(codecCtxArrayIn[streamIndexIn]->codec->name, "h264") == 0 ) {
        switch (decoder->GetHWDeviceType()) {
        case AV_HWDEVICE_TYPE_VAAPI:
            if (asprintf(&encoderName,"h264_%s", hwaccelName) == -1) {
                dsyslog("cEncoder::GetEncoderName(): failed to allocate string, out of memory");
                return nullptr;
            }
            break;
        case AV_HWDEVICE_TYPE_CUDA:
            if (asprintf(&encoderName,"h264_nvenc") == -1) {
                dsyslog("cEncoder::GetEncoderName(): failed to allocate string, out of memory");
                return nullptr;
            }
            break;
        default:
            esyslog("cEncoder::GetEncoderName(): unknown hardware device type, fallback to software encoding");
            return nullptr;
        }
        ALLOC(strlen(encoderName) + 1, "encoderName");
        return encoderName;
    }
    if (strcmp(codecCtxArrayIn[streamIndexIn]->codec->name, "hevc") == 0 ) {
        if (asprintf(&encoderName,"hevc_%s", hwaccelName) == -1) {
            dsyslog("cEncoder::GetEncoderName(): failed to allocate string, out of memory");
            return nullptr;
        }
        ALLOC(strlen(encoderName) + 1, "encoderName");
        return encoderName;
    }
    esyslog("cEncoder::GetEncoderName(): unknown decoder codec, fallback to software encoding");
    return nullptr;
}


bool cEncoder::InitEncoderCodec(const unsigned int streamIndexIn, const unsigned int streamIndexOut, const bool addOutStream, AVPixelFormat forcePixFmt, const bool verbose) {
    if (!decoder)  return false;
    if (!avctxIn)  return false;
    if (!avctxOut) return false;
    LogSeparator();
    if (streamIndexIn >= avctxIn->nb_streams) {
        dsyslog("cEncoder::InitEncoderCodec(): streamindex %d out of range", streamIndexIn);
        return false;
    }
    if (!codecCtxArrayIn) {
        esyslog("cEncoder::InitEncoderCodec(): no input codec arry set");
        return false;
    }
    dsyslog("cEncoder::InitEncoderCodec(): stream index in %d, out %d, add stream %d, force pixel format %d, verbose %d", streamIndexIn, streamIndexOut, addOutStream, forcePixFmt, verbose);

#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::InitEncoderCodec(): stream %d, start av_buffer_get_ref_count(hw_device_ctx) %d", streamIndexOut, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    // select coded based on decoder used hwaccel
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100) // ffmpeg 4.5
    const AVCodec *codec = nullptr;
#else
    AVCodec *codec = nullptr;
#endif
    AVCodecID codec_id = AV_CODEC_ID_NONE;
    dsyslog("cEncoder::InitEncoderCodec(): decoder stream %d: id %d, name %s, long name %s", streamIndexIn, codecCtxArrayIn[streamIndexIn]->codec->id, codecCtxArrayIn[streamIndexIn]->codec->name, codecCtxArrayIn[streamIndexIn]->codec->long_name);

    enum AVHWDeviceType hwDeviceType = decoder->GetHWDeviceType();
    if (useHWaccel && decoder->IsVideoStream(streamIndexIn)) {
        char *encoderName = GetEncoderName(streamIndexIn);
        if (!encoderName) {
            esyslog("cEncoder::InitEncoderCodec(): unknown decoder codec, fallback to software encoding");
            return false;
        }
        dsyslog("cEncoder::InitEncoderCodec(): hwaccel encoder name: %s", encoderName);
        codec = avcodec_find_encoder_by_name(encoderName);
        if (codec) codec_id = codec->id;
        else {
            esyslog("cEncoder::InitEncoderCodec(): hwaccel encoder name: %s not found", encoderName);
            FREE(strlen(encoderName) + 1, "encoderName");
            free(encoderName);
            return false;
        }
        FREE(strlen(encoderName) + 1, "encoderName");
        free(encoderName);
    }
    else {  // no video stream or software encoded, use codec id from decoder
        dsyslog("cEncoder::InitEncoderCodec(): use software encoder");
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100) // ffmpeg 4.5
        codec_id = avctxIn->streams[streamIndexIn]->codecpar->codec_id;
        codec = avcodec_find_encoder(codec_id);
#else
        codec_id = avctxIn->streams[streamIndexIn]->codecpar->codec_id;
        codec = avcodec_find_encoder(codec_id);
#endif
    }

    if (codec) dsyslog("cEncoder::InitEncoderCodec(): using encoder id %d '%s' for output stream %i", codec_id, codec->long_name, streamIndexOut);
    else {
        dsyslog("cEncoder::InitEncoderCodec(): stream %d codec id %d: not supported by FFmpeg", streamIndexIn, codec_id);
        switch (codec_id) {
        case 86065:  // AAC LATM (Advanced Audio Coding LATM syntax), FFmpeg does not support encoding
            dsyslog("cEncoder::InitEncoderCodec(): stream %d codec id %d: no encoder for AAC LATM (Advanced Audio Coding LATM syntax), copy packets without re-encode", streamIndexIn, codec_id);
            codec = avcodec_find_decoder(codec_id);  // use data from decoder to write file header
            break;
        case 94215:  // libavcodec does not support Libzvbi DVB teletext encoder, encode without this stream
            dsyslog("cEncoder::InitEncoderCodec(): stream %d codec id %d: no encoder for Libzvbi DVB teletext, copy packets without re-encode", streamIndexIn, codec_id);
            codec = avcodec_find_decoder(codec_id);  // use data from decoder to write file header
            break;
        default:
            dsyslog("cEncoder::InitEncoderCodec(): could not find encoder for input stream %d codec id %d, ignore stream", streamIndexIn, codec_id);
            return true;
            break;
        }
    }

    if (addOutStream) {
        const AVStream *out_stream = avformat_new_stream(avctxOut, codec);   // parameter codec unused
        if (!out_stream) {
            dsyslog("cEncoder::InitEncoderCodec(): failed allocating output stream");
            return false;
        }
    }

    // init encoder codec context
    codecCtxArrayOut[streamIndexOut] = avcodec_alloc_context3(codec);
    if (!codecCtxArrayOut[streamIndexOut]) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_alloc_context3 failed");
        return false;
    }
    ALLOC(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");

    if (avcodec_parameters_to_context(codecCtxArrayOut[streamIndexOut], avctxOut->streams[streamIndexOut]->codecpar) < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_parameters_to_context failed");
        return false;
    }

// set encoding codec parameter
    // video stream
    if (decoder->IsVideoStream(streamIndexIn)) {
        if (verbose) {
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: pix_fmt %s", streamIndexIn, av_get_pix_fmt_name(codecCtxArrayIn[streamIndexIn]->pix_fmt));
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: avg framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->avg_frame_rate.num, avctxIn->streams[streamIndexIn]->avg_frame_rate.den);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: real framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->r_frame_rate.num, avctxIn->streams[streamIndexIn]->r_frame_rate.den);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: keyint_min %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->keyint_min);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: bit_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: rc_max_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->rc_max_rate);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: max_b_frames %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->max_b_frames);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: bit_rate_tolerance %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate_tolerance);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: global_quality %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->global_quality);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: sample_rate %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_rate);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: gop_size %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->gop_size);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: level %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->level);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: color_range %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->color_range);
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: aspect ratio %d:%d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.num, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.den);
            dsyslog("cEncoder::InitEncoderCodec(): video input format context : bit_rate %" PRId64, avctxIn->bit_rate);
        }

        // use hwaccel for video stream if decoder use it too
        if (useHWaccel) {
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: use hwaccel: %s", streamIndexOut, av_hwdevice_get_type_name(hwDeviceType));
#ifdef DEBUG_HW_DEVICE_CTX_REF
            dsyslog("cEncoder::InitEncoderCodec(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            // store stoftware fixel format in case of we need it for fallback to software decoder
            software_pix_fmt = codecCtxArrayIn[streamIndexIn]->pix_fmt;
            // we need to ref hw_frames_ctx of decoder to initialize encoder's codec
            // only after we get a decoded frame, can we obtain its hw_frames_ctx
            if (!codecCtxArrayIn[streamIndexIn]->hw_frames_ctx) {
#ifdef DEBUG_HW_DEVICE_CTX_REF
                dsyslog("cEncoder::InitEncoderCodec(): before DecodeNextFrame(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
                decoder->DecodeNextFrame(false);
                decoder->DropFrame();
#ifdef DEBUG_HW_DEVICE_CTX_REF
                dsyslog("cEncoder::InitEncoderCodec(): after DropFrame(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            }
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: pix_fmt %s", streamIndexIn, av_get_pix_fmt_name(codecCtxArrayIn[streamIndexIn]->pix_fmt));
            if (!codecCtxArrayIn[streamIndexIn]->hw_frames_ctx) {
                esyslog("cEncoder::InitEncoderCodec(): hwaccel encoder without hwaccel decoder is not valid");
                return false;
            }
            dsyslog("cEncoder::InitEncoderCodec(): link hw_frames_ctx to encoder codec");
#ifdef DEBUG_HW_DEVICE_CTX_REF
            dsyslog("cEncoder::InitEncoderCodec(): before ref hw_frames_ctx: av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            codecCtxArrayOut[streamIndexOut]->hw_frames_ctx = av_buffer_ref(codecCtxArrayIn[streamIndexIn]->hw_frames_ctx);  // link hardware frame context to encoder
            if (!codecCtxArrayOut[streamIndexOut]->hw_frames_ctx) {
                esyslog("cEncoder::InitEncoderCodec(): av_buffer_ref() failed");
                return false;
            }
#ifdef DEBUG_HW_DEVICE_CTX_REF
            dsyslog("cEncoder::InitEncoderCodec(): av_buffer_get_ref_count(codecCtxArrayOut[streamIndexOut]->hw_frames_ctx) %d", av_buffer_get_ref_count(codecCtxArrayOut[streamIndexOut]->hw_frames_ctx));
            dsyslog("cEncoder::InitEncoderCodec(): after  ref hw_frames_ctx: av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
        }
        fileNumber = decoder->GetFileNumber();
        // for re-init video decoder keep same pixel format as before
        if (forcePixFmt != AV_PIX_FMT_NONE) {
            dsyslog("cEncoder::InitEncoderCodec(): force video pixel format to %s", av_get_pix_fmt_name(forcePixFmt));
            codecCtxArrayOut[streamIndexOut]->pix_fmt = forcePixFmt;
        }
        // use pixel software pixel format from decoder for fallback to software decoder
        else if ((software_pix_fmt != AV_PIX_FMT_NONE) && !useHWaccel) codecCtxArrayOut[streamIndexOut]->pix_fmt = software_pix_fmt;
        else codecCtxArrayOut[streamIndexOut]->pix_fmt = codecCtxArrayIn[streamIndexIn]->pix_fmt;

        // set encoder codec parameters from decoder codec
        codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio;

        if (useHWaccel && (hwDeviceType == AV_HWDEVICE_TYPE_VAAPI) && (codec_id == AV_CODEC_ID_MPEG2VIDEO)) {
            dsyslog("cEncoder::InitEncoderCodec(): set reversed aspect ratio as workaround from FFmpeg bug");
            /* workaround of bug in libavcodec/vaapi_encode_mpeg2.c: { nun, den } reversed
             * bug ticket https://trac.ffmpeg.org/ticket/10099
             * last activity 2023-02-14

                if (avctx->sample_aspect_ratio.num != 0 &&
                    avctx->sample_aspect_ratio.den != 0) {
                    AVRational dar = av_div_q(avctx->sample_aspect_ratio,
                                              (AVRational) { avctx->width, avctx->height });

                    if (av_cmp_q(avctx->sample_aspect_ratio, (AVRational) { 1, 1 }) == 0) {
                        sh->aspect_ratio_information = 1;
                    } else if (av_cmp_q(dar, (AVRational) { 3, 4 }) == 0) {
                        sh->aspect_ratio_information = 2;
                    } else if (av_cmp_q(dar, (AVRational) { 9, 16 }) == 0) {
                        sh->aspect_ratio_information = 3;
                    } else if (av_cmp_q(dar, (AVRational) { 100, 221 }) == 0) {
                        sh->aspect_ratio_information = 4;
                    } else {
                        av_log(avctx, AV_LOG_WARNING, "Sample aspect ratio %d:%d is not "
                               "representable, signalling square pixels instead.\n",
                               avctx->sample_aspect_ratio.num,
                               avctx->sample_aspect_ratio.den);
                        sh->aspect_ratio_information = 1;
                    }
                } else {
                    // Unknown - assume square pixels.
                    sh->aspect_ratio_information = 1;
                }
            */
            // reverse num/den as workaround
            codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.num = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.den;
            codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.den = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.num;
        }
        codecCtxArrayOut[streamIndexOut]->time_base.num   = avctxIn->streams[streamIndexIn]->avg_frame_rate.den;  // time_base = 1 / framerate
        codecCtxArrayOut[streamIndexOut]->time_base.den   = avctxIn->streams[streamIndexIn]->avg_frame_rate.num;
        codecCtxArrayOut[streamIndexOut]->framerate       = avctxIn->streams[streamIndexIn]->avg_frame_rate;

        codecCtxArrayOut[streamIndexOut]->height          = codecCtxArrayIn[streamIndexIn]->height;
        codecCtxArrayOut[streamIndexOut]->width           = codecCtxArrayIn[streamIndexIn]->width;

        codecCtxArrayOut[streamIndexOut]->color_range     = codecCtxArrayIn[streamIndexIn]->color_range;
        codecCtxArrayOut[streamIndexOut]->colorspace      = codecCtxArrayIn[streamIndexIn]->colorspace;
        codecCtxArrayOut[streamIndexOut]->color_trc       = codecCtxArrayIn[streamIndexIn]->color_trc;
        codecCtxArrayOut[streamIndexOut]->color_primaries = codecCtxArrayIn[streamIndexIn]->color_primaries;


        // calculate target mpeg2 video stream bit rate from recording
        int bit_rate = avctxIn->bit_rate; // overall recording bitrate
        for (unsigned int streamIndex = 0; streamIndex < avctxIn->nb_streams; streamIndex ++) {
            if (codecCtxArrayIn[streamIndex] && !decoder->IsVideoStream(streamIndex)) bit_rate -= codecCtxArrayIn[streamIndex]->bit_rate;  // audio streams bit rate
        }
        if (verbose) dsyslog("cEncoder::InitEncoderCodec(): target video bit rate %d", bit_rate);

        // parameter from origial recording
        if (codec->id == AV_CODEC_ID_H264) {
            // set pass
            if (pass == 1) codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS1;
            else {
                if (pass == 2) {
                    codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS2;
                }
            }
//            av_opt_set_int(codecCtxArrayOut[streamIndexOut]->priv_data, "b_strategy", 0, 0); // keep fixed B frames in GOP, additional needed after force-crf
//            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "x264opts", "force-cfr", 0);  // constand frame rate
//            codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_CLOSED_GOP;

            codecCtxArrayOut[streamIndexOut]->bit_rate        = bit_rate;  // adapt target bit rate
            codecCtxArrayOut[streamIndexOut]->level           = codecCtxArrayIn[streamIndexIn]->level;
            codecCtxArrayOut[streamIndexOut]->gop_size        = codecCtxArrayIn[streamIndexIn]->gop_size;
            codecCtxArrayOut[streamIndexOut]->keyint_min      = codecCtxArrayIn[streamIndexIn]->keyint_min;
            codecCtxArrayOut[streamIndexOut]->max_b_frames    = codecCtxArrayIn[streamIndexIn]->max_b_frames;
            // set pass stats file
            char *passlogfile;
            if (asprintf(&passlogfile,"%s/encoder", recDir) == -1) {
                dsyslog("cEncoder::InitEncoderCodec(): failed to allocate string, out of memory?");
                return false;
            }
            ALLOC(strlen(passlogfile)+1, "passlogfile");
            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "passlogfile", passlogfile, 0);
            FREE(strlen(passlogfile)+1, "passlogfile");
            free(passlogfile);
        }
        else {
            if (codec->id == AV_CODEC_ID_MPEG2VIDEO) {  // MPEG2 SD Video
                codecCtxArrayOut[streamIndexOut]->bit_rate = bit_rate * 0.95;  // ffmpeg is a little too high
                codecCtxArrayOut[streamIndexOut]->gop_size = codecCtxArrayIn[streamIndexIn]->gop_size;  // GOP N =
                codecCtxArrayOut[streamIndexOut]->keyint_min = codecCtxArrayIn[streamIndexIn]->gop_size;
                codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;  // does not work with H.264
                codecCtxArrayOut[streamIndexOut]->max_b_frames = 2;  // GOP M = max_b_frames + 1
                codecCtxArrayOut[streamIndexOut]->rc_max_rate = 15000000; // taken from SD recordings
                av_opt_set_int(codecCtxArrayOut[streamIndexOut]->priv_data, "sc_threshold", 1000000000, 0);  // needed for fixed GOP and closed GOP
                // set pass
                if (pass == 1) codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS1;
                else {
                    if ((pass == 2) && stats_in.data && CheckStats(codecCtxArrayOut[streamIndexOut]->max_b_frames)) {
                        codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS2;
                        dsyslog("cEncoder::InitEncoderCodec(): output stream %d stats_in length %ld", streamIndexOut, stats_in.size);
                        codecCtxArrayOut[streamIndexOut]->stats_in = stats_in.data;
                    }
                }
            }
            else {
                if (codec->id == AV_CODEC_ID_H265) {
                    int crf = 19;
                    dsyslog("cEncoder::InitEncoderCodec(): video output stream %d crf %d", streamIndexOut, crf);
                    av_opt_set_int(codecCtxArrayOut[streamIndexOut]->priv_data, "crf", crf, AV_OPT_SEARCH_CHILDREN);  // less is higher bit rate
                }
            }
        }
        // set interlaced
        if (decoder->IsInterlacedFrame()) {
            codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_INTERLACED_DCT;
            codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_INTERLACED_ME;
        }

        // log encoder parameter
        if (verbose) {
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: pix_fmt %s", streamIndexOut, av_get_pix_fmt_name(codecCtxArrayOut[streamIndexOut]->pix_fmt));
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: keyint_min %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->keyint_min);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: max_b_frames %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->max_b_frames);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: bit_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: rc_max_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->rc_max_rate);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: bit_rate_tolerance %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate_tolerance);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: level %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->level);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: framerate %d/%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->framerate.num, codecCtxArrayOut[streamIndexOut]->framerate.den);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: width %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->width);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: height %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->height);

            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: gop_size %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->gop_size);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: level %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->level);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: color_range %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->color_range);
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: aspect ratio %d:%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.num, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.den);
        }
    }
    // audio stream
    else {
        if (decoder->IsAudioStream(streamIndexIn)) {
            if (verbose) dsyslog("cEncoder::InitEncoderCodec(): input codec sample rate %d, timebase %d/%d for stream %d", codecCtxArrayIn[streamIndexIn]->sample_rate, codecCtxArrayIn[streamIndexIn]->time_base.num, codecCtxArrayIn[streamIndexIn]->time_base.den, streamIndexIn);
            codecCtxArrayOut[streamIndexOut]->time_base.num = codecCtxArrayIn[streamIndexIn]->time_base.num;
            codecCtxArrayOut[streamIndexOut]->time_base.den = codecCtxArrayIn[streamIndexIn]->time_base.den;
            codecCtxArrayOut[streamIndexOut]->sample_rate   = codecCtxArrayIn[streamIndexIn]->sample_rate;

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            int rc = av_channel_layout_copy(&codecCtxArrayOut[streamIndexOut]->ch_layout, &codecCtxArrayIn[streamIndexIn]->ch_layout);
            if (rc != 0) {
                if (verbose) dsyslog("cEncoder::InitEncoderCodec(): av_channel_layout_copy for output stream %d from input stream %d  failed, rc = %d", streamIndexOut, streamIndexIn, rc);
                return false;
            }
#else
            codecCtxArrayOut[streamIndexOut]->channel_layout = codecCtxArrayIn[streamIndexIn]->channel_layout;
            codecCtxArrayOut[streamIndexOut]->channels       = codecCtxArrayIn[streamIndexIn]->channels;
#endif

            codecCtxArrayOut[streamIndexOut]->bit_rate = codecCtxArrayIn[streamIndexIn]->bit_rate;

            // audio sampe format
#if LIBAVCODEC_VERSION_INT < ((61<<16)+( 13<<8)+100)
            if (verbose) dsyslog("cEncoder::InitEncoderCodec():            input audio codec sample format %d -> %s", codecCtxArrayIn[streamIndexIn]->sample_fmt, av_get_sample_fmt_name(codecCtxArrayIn[streamIndexIn]->sample_fmt));
            const enum AVSampleFormat *sampleFormats = codec->sample_fmts;
            while (*sampleFormats != AV_SAMPLE_FMT_NONE) {
                if (verbose) dsyslog("cEncoder::InitEncoderCodec(): supported output audio codec sample format %d -> %s", *sampleFormats, av_get_sample_fmt_name(*sampleFormats));
                sampleFormats++;
            }
#else
            const enum AVSampleFormat *sampleFormats = nullptr;
            int num_sample_fmts = 0;
            int ret = avcodec_get_supported_config(codecCtxArrayOut[streamIndexOut], NULL, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **) &sampleFormats, &num_sample_fmts);
            if (ret < 0) {
                esyslog("cEncoder::InitEncoderCodec(): avcodec_get_supported_config failed, rc = %d", ret);
                return false;
            }
            for (int i = 0; i < num_sample_fmts; i++) {
                dsyslog("cEncoder::InitEncoderCodec(): supported output audio codec sample format %d -> %s", sampleFormats[i], av_get_sample_fmt_name(sampleFormats[i]));
            }

#endif

            if (codecCtxArrayIn[streamIndexIn]->sample_fmt == AV_SAMPLE_FMT_S16P) {  // libav do not support planar audio
                codecCtxArrayOut[streamIndexOut]->sample_fmt = AV_SAMPLE_FMT_S16;
                swrArray[streamIndexOut] = swr_alloc();
                ALLOC(sizeof(swrArray[streamIndexOut]), "swr");  // only pointer size as marker
                int rcAudio = 0;
#if LIBAVCODEC_VERSION_INT >= ((61<<16)+( 1<<8)+100)
                rcAudio = av_opt_set_chlayout(swrArray[streamIndexOut], "in_chlayout", &codecCtxArrayIn[streamIndexIn]->ch_layout, 0);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_chlayout in_chlayout failed: %s", errTXT);
                }
                rcAudio = av_opt_set_chlayout(swrArray[streamIndexOut], "out_chlayout", &codecCtxArrayIn[streamIndexIn]->ch_layout, 0);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_chlayout out_chlayout failed: %s", errTXT);
                }
#elif LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
                rcAudio = av_opt_set_int(swrArray[streamIndexOut], "in_channel_layout",  codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, 0);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int in_channel_layout to %" PRIu64 " failed: %s", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, errTXT);
                }
                rcAudio = av_opt_set_int(swrArray[streamIndexOut], "out_channel_layout", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, 0);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int out_channel_layout to %" PRIu64 " failed: %s", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, errTXT);
                }
#else
                if (av_opt_set_int(swrArray[streamIndexOut], "in_channel_layout",  codecCtxArrayIn[streamIndexIn]->channel_layout,   0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int in_channel_layout failed");
                if (av_opt_set_int(swrArray[streamIndexOut], "out_channel_layout", codecCtxArrayIn[streamIndexIn]->channel_layout,   0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int out_channel_layout failed");
#endif
                if (av_opt_set_int(swrArray[streamIndexOut], "in_sample_rate",     codecCtxArrayIn[streamIndexIn]->sample_rate,      0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int in_sample_rate failed");
                if (av_opt_set_int(swrArray[streamIndexOut], "out_sample_rate",    codecCtxArrayIn[streamIndexIn]->sample_rate,      0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int out_sample_rate failed");
                if (av_opt_set_sample_fmt(swrArray[streamIndexOut], "in_sample_fmt",  AV_SAMPLE_FMT_S16P, 0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_sample_fmt in_sample_fmt failed");
                if (av_opt_set_sample_fmt(swrArray[streamIndexOut], "out_sample_fmt", AV_SAMPLE_FMT_S16,  0) < 0) esyslog("cEncoder::InitEncoderCodec(): av_opt_set_sample_fmt out_sample_fmt failed");
                rcAudio = swr_init(swrArray[streamIndexOut]);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): failed to init audio resampling context for output stream index %d: %s", streamIndexOut, errTXT);
                    FREE(sizeof(swrArray[streamIndexOut]), "swr");  // only pointer size as marker
                    swr_free(&swrArray[streamIndexOut]);
                    return false;
                }

            }
            else codecCtxArrayOut[streamIndexOut]->sample_fmt = codecCtxArrayIn[streamIndexIn]->sample_fmt;
            if (verbose) dsyslog("cEncoder::InitEncoderCodec(): audio output codec parameter for stream %d: bit_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
        }
        // subtitle stream
        else {
            if (decoder->IsSubtitleStream(streamIndexIn)) {
                dsyslog("cEncoder::InitEncoderCodec(): codec of input stream %d is subtitle", streamIndexIn);
                codecCtxArrayOut[streamIndexOut]->time_base.num = 1;
                codecCtxArrayOut[streamIndexOut]->time_base.den = 1;
            }
            else {
                dsyslog("cEncoder::InitEncoderCodec(): codec of input stream %i not audio or video, ignoring", streamIndexIn);
                return true;
            }
        }
    }
    if (verbose) dsyslog("cEncoder::InitEncoderCodec():       output stream %d timebase %d/%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->time_base.num, codecCtxArrayOut[streamIndexOut]->time_base.den);
    if ((codecCtxArrayOut[streamIndexOut]->time_base.num == 0) && (!decoder->IsSubtitleStream(streamIndexIn))) {
        dsyslog("cEncoder::InitEncoderCodec(): output stream %d timebase %d/%d not valid", streamIndexOut, codecCtxArrayOut[streamIndexOut]->time_base.num, codecCtxArrayOut[streamIndexOut]->time_base.den);
        return false;
    }

    int ret = avcodec_parameters_copy(avctxOut->streams[streamIndexOut]->codecpar, avctxIn->streams[streamIndexIn]->codecpar);
    if (ret < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed to copy codecpar context from input to output stream");
        return false;
    }

    codecCtxArrayOut[streamIndexOut]->thread_count = decoder->GetThreadCount();
    if (avcodec_open2(codecCtxArrayOut[streamIndexOut], codec, nullptr) < 0) {    // this can happen if encoding is not supported by ffmpeg, have to copy packets
        esyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %d failed", streamIndexOut);
        dsyslog("cEncoder::InitEncoderCodec(): call avcodec_free_context for stream %d", streamIndexOut);
        FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
        codecCtxArrayOut[streamIndexOut] = nullptr;
        if (decoder->IsVideoStream(streamIndexIn)) return false;  // video stream is essential
    }
    else dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i successful", streamIndexOut);

    // restore correct value, intentionally set false from FFmpeg bug workaround
    codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio;

    if (decoder->IsAudioAC3Stream(streamIndexIn) && ac3ReEncode) {
        dsyslog("cEncoder::InitEncoderCodec(): AC3 input found at stream %i, initialize volume filter for output stream %d", streamIndexIn, streamIndexOut);
        if (volumeFilterAC3[streamIndexOut]) {
            dsyslog("cEncoder::InitEncoderCodec(): volumeFilterAC3 is not nullptr for output stream %i", streamIndexOut);
            FREE(sizeof(*volumeFilterAC3[streamIndexOut]), "volumeFilterAC3");
            delete volumeFilterAC3[streamIndexOut];
        }
        volumeFilterAC3[streamIndexOut] = new cAC3VolumeFilter();
        ALLOC(sizeof(*volumeFilterAC3[streamIndexOut]), "volumeFilterAC3");
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        if (!volumeFilterAC3[streamIndexOut]->Init(codecCtxArrayIn[streamIndexIn]->ch_layout, codecCtxArrayIn[streamIndexIn]->sample_fmt, codecCtxArrayIn[streamIndexIn]->sample_rate))
#else
        if (!volumeFilterAC3[streamIndexOut]->Init(codecCtxArrayIn[streamIndexIn]->channel_layout, codecCtxArrayIn[streamIndexIn]->sample_fmt, codecCtxArrayIn[streamIndexIn]->sample_rate))
#endif
        {
            dsyslog("cEncoder::InitEncoderCodec(): volumeFilterAC3->Init() failed");
            return false;
        }
    }
#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::InitEncoderCodec(): stream %d, end av_buffer_get_ref_count(hw_device_ctx) %d", streamIndexOut, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    return true;
}


// resample audio frame from AV_SAMPLE_FMT_S16P to AV_SAMPLE_FMT_S16
//
/* unused
bool cEncoder::ReSampleAudio(AVFrame *avFrameIn, AVFrame *avFrameOut, const int streamIndex) {
    if (!swrArray[streamIndex]) return false;
    if (!avFrameIn->extended_data) return false;

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    if (avFrameIn->ch_layout.nb_channels != 2) {
        dsyslog("cEncoder::ReSampleAudio(): %d MP2 audio channels not supported", avFrameIn->ch_layout.nb_channels);
#else
    if (avFrameIn->channels != 2) {
        dsyslog("cEncoder::ReSampleAudio(): %d MP2 audio channels not supported", avFrameIn->channels);
#endif
        return false;
    }
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    int ret = av_samples_alloc(avFrameOut->extended_data, avFrameOut->linesize, avFrameIn->ch_layout.nb_channels, avFrameIn->nb_samples, AV_SAMPLE_FMT_S16, 0);
#else
    int ret = av_samples_alloc(avFrameOut->extended_data, avFrameOut->linesize, avFrameIn->channels, avFrameIn->nb_samples, AV_SAMPLE_FMT_S16, 0);
#endif
    if (ret < 0) {
        dsyslog("cEncoder::ReSampleAudio(): Could not allocate source samples");
        return false;
    }
    // convert to destination format
    ret = swr_convert(swrArray[streamIndex], avFrameOut->extended_data, avFrameIn->nb_samples, const_cast<const uint8_t**>(avFrameIn->extended_data), avFrameIn->nb_samples);
    if (ret < 0) {
        dsyslog("cEncoder::ReSampleAudio():  Error while converting audio stream");
        return false;
    }
    ALLOC(sizeof(*(avFrameOut->extended_data)), "extended_data");  // only size of pointer array as reminder

    // set frame values
#if   LIBAVCODEC_VERSION_INT >= ((59<<16)+( 32<<8)+101)
    avFrameOut->ch_layout.nb_channels = avFrameIn->ch_layout.nb_channels;
#elif LIBAVCODEC_VERSION_INT >= ((58<<16)+(134<<8)+100)   // fix mp2 encoding with ffmpeg >= 4.4
    avFrameOut->channels              = avFrameIn->channels;
#endif
    avFrameOut->format                = AV_SAMPLE_FMT_S16;
    avFrameOut->nb_samples            = avFrameIn->nb_samples;
    avFrameOut->pts                   = avFrameIn->pts;
    return true;
}
*/

bool cEncoder::CutOut(cMark *startMark, cMark *stopMark) {
    LogSeparator(true);
    dsyslog("cEncoder::CutOut(): packet (%d): start position (%d) PTS %" PRId64 ", stop position (%d) PTS %" PRId64 " in pass: %d, cut mode %d", decoder->GetPacketNumber(), startMark->position, startMark->pts, stopMark->position, stopMark->pts, pass, cutMode);
    // store video input and output stream index
    for (int streamIndex = 0; streamIndex < static_cast<int>(avctxIn->nb_streams); streamIndex++) {
        if (codecCtxArrayIn[streamIndex] && decoder->IsVideoStream(streamIndex)) {
            videoInputStreamIndex  = streamIndex;
            videoOutputStreamIndex = streamMap[streamIndex];
        }
    }
    if (videoInputStreamIndex < 0) {
        esyslog("cEncoder::CutOut(): input video stream not found");
        return false;
    }
    if (videoOutputStreamIndex < 0) {
        esyslog("cEncoder::CutOut(): output video stream not found");
        return false;
    }
    dsyslog("cEncoder::CutOut(): video stream index: input %d, output %d", videoInputStreamIndex, videoOutputStreamIndex);

    // we need in any case a mark PTS
    if (startMark->pts < 0) startMark->pts = index->GetPTSAfterKeyPacketNumber(startMark->position);
    if (stopMark->pts  < 0) stopMark->pts  = index->GetPTSAfterKeyPacketNumber(stopMark->position);

    bool cutOK = false;
    switch (cutMode) {
    case CUT_MODE_KEY:
        cutOK = CutKeyPacket(startMark, stopMark);
        break;
    case CUT_MODE_SMART:
        cutOK = CutSmart(startMark, stopMark);
        break;
    case CUT_MODE_FULL:
        cutOK = CutFullReEncode(startMark, stopMark);
        break;
    default:
        esyslog("cEncoder::CutOut(): invalid cut mode %d", cutMode);
    }
    LogSeparator(true);
    return cutOK;
}


int cEncoder::GetPSliceKeyPacketNumberAfterPTS(int64_t pts, int64_t *pSlicePTS, const int keyPacketNumberBeforeStop) {
    if (!pSlicePTS) return -1;
    if (!indexLocal) {
        indexLocal = new cIndex(true);  // full decode
        ALLOC(sizeof(*indexLocal), "indexLocal");
    }
    if (!decoderLocal) {
        // full decode, no force interlaced
        decoderLocal = new cDecoder(decoder->GetRecordingDir(), decoder->GetThreads(), true, decoder->GetHWaccelName(), decoder->GetForceHWaccel(), false, indexLocal);
        ALLOC(sizeof(*decoderLocal), "decoderLocal");
    }
    int startDecodePacketNumber = index->GetKeyPacketNumberBeforePTS(pts);
    dsyslog("cEncoder::GetPSliceAfterPTS(): packet (%d): start full decoding", startDecodePacketNumber);
    decoderLocal->SeekToPacket(startDecodePacketNumber);
    while (decoderLocal->DecodeNextFrame(false)) {
        if (decoderLocal->GetPacketNumber() >= keyPacketNumberBeforeStop) {
            dsyslog("cEncoder::GetPSliceAfterPTS(): no p-slice found before next stop mark");
            return -1;
        }
        if (decoderLocal->IsVideoKeyPacket()) {
            int keyPacketNumberPSlice = indexLocal->GetPSliceKeyPacketNumberAfterPTS(pts, pSlicePTS);
            if (keyPacketNumberPSlice >= 0) {
                dsyslog("cEncoder::GetPSliceAfterPTS(): packet (%d): found p-slice key packet (%d) with PTS %" PRId64 " after PTS %" PRId64, decoderLocal->GetPacketNumber(), keyPacketNumberPSlice, *pSlicePTS, pts);
                return keyPacketNumberPSlice;
            }
        }
    }
    return -1;
}


bool cEncoder::DrainVideoReEncode(const int64_t startPTS, const int64_t stopPTS) {
    // flush decoder queue
    dsyslog("cEncoder::DrainVideoReEncode(): drain decoder queue");
    decoder->FlushDecoder(videoInputStreamIndex);
    while (decoder->ReceiveFrameFromDecoder() == 0) {
        if (abortNow) return false;
        if ((decoder->GetFramePTS() >= startPTS) && (decoder->GetFramePTS() <= stopPTS)) {
            dsyslog("cEncoder::DrainVideoReEncode(): frame decoder -> encoder: PTS %" PRId64 ", DTS %" PRId64, decoder->GetFramePTS(), decoder->GetFrameDTS());
            if (!EncodeVideoFrame()) {
                esyslog("cEncoder::DrainVideoReEncode(): decoder packet (%d): EncodeVideoFrame() failed during re-encoding", decoder->GetPacketNumber());
                return false;
            }
        }
        else {
            dsyslog("cEncoder::DrainVideoReEncode(): drop video input frame with PTS %" PRId64 "  not after start mark PTS or before stop mark PTS %" PRId64, decoder->GetFramePTS(), stopPTS);
            decoder->DropFrame();
        }
    }

    // flush encoder queue
    dsyslog("cEncoder::DrainVideoReEncode(): drain encoder queue");
    SendFrameToEncoder(videoOutputStreamIndex, nullptr);
    // receive rest packet from encoder queue
    while (AVPacket *avpktOut = ReceivePacketFromEncoder(videoOutputStreamIndex)) {
        if (abortNow) return false;
        dsyslog("cEncoder::DrainVideoReEncode(): packet from encoder: PTS %" PRId64 ", DTS %" PRId64, avpktOut->pts, avpktOut->dts);
        avpktOut->duration = cutInfo.videoPacketDuration;   // not set by encoder

        // set additional PTS/DTS offset from oncoder to fit before/after packets copied
        if (pass == 0) SetSmartReEncodeOffset(avpktOut);
        // write packet
        if (!WritePacket(avpktOut, true)) {  // packet was re-encoded
            esyslog("cEncoder::DrainVideoReEncode(): WritePacket() failed");
            return false;
        }
        FREE(sizeof(*avpktOut), "avpktOut");
        av_packet_free(&avpktOut);
    }
    return(ResetDecoderEncodeCodec());  // need to reset for future use
}


bool cEncoder::ResetDecoderEncodeCodec() {
    dsyslog("cEncoder::ResetDecoderEncodeCodec(): reset decoder and encoder codec context");
    firstFrameToEncoder = true;
    AVPixelFormat forcePixFmt = codecCtxArrayOut[videoOutputStreamIndex]->pix_fmt;   // keep same pixel format
    avcodec_flush_buffers(codecCtxArrayOut[videoOutputStreamIndex]);
    if (codecCtxArrayOut[videoOutputStreamIndex]->hw_frames_ctx) {
#ifdef DEBUG_HW_DEVICE_CTX_REF
        dsyslog("cEncoder::ResetDecoderEncodeCodec: av_buffer_get_ref_count(codecCtxArrayOut[videoOutputStreamIndex]->hw_frames_ctx) %d", av_buffer_get_ref_count(codecCtxArrayOut[videoOutputStreamIndex]->hw_frames_ctx));
#endif
        av_buffer_unref(&codecCtxArrayOut[videoOutputStreamIndex]->hw_frames_ctx);
    }
    FREE(sizeof(*codecCtxArrayOut[videoOutputStreamIndex]), "codecCtxArrayOut[streamIndex]");
    avcodec_free_context(&codecCtxArrayOut[videoOutputStreamIndex]);

    // restart input codec context, required after flush queue
    if (!decoder->RestartCodec(videoOutputStreamIndex)) {
        esyslog("cEncoder::ResetDecoderEncodeCodec(): restart decoder context failed");
        return false;
    }
    // restart output codec context
    if (!InitEncoderCodec(videoInputStreamIndex, videoOutputStreamIndex, false, forcePixFmt, false)) {  // keep same video pixel format
        esyslog("cEncoder::ResetDecoderEncodeCodec(): failed to re-init codec after flash buffer");
        return false;
    }

    return true;
}


// set additional PTS/DTS offset from encoder to fit before/after packets copied
void cEncoder::SetSmartReEncodeOffset(AVPacket *avpkt) {
#ifdef DEBUG_CUT_OFFSET
    dsyslog("cEncoder:SetSmartReEncodeOffset(): state %d", cutInfo.state);
#endif
    switch (cutInfo.state) {
    case CUT_STATE_FIRSTPACKET: // first packet back from encoder from first start mark, set PTS and PTS to start mark of input stream
        if (decoder->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264) {
            dsyslog("cEncoder::SetSmartReEncodeOffset(): start packet in  before re-encode PTS %" PRId64 ", DTS %" PRId64, cutInfo.startPTS, cutInfo.startDTS);
            dsyslog("cEncoder::SetSmartReEncodeOffset(): start packet out after  re-encode PTS %" PRId64 ", DTS %" PRId64, avpkt->pts, avpkt->dts);
            cutInfo.offsetPTSReEncode = cutInfo.startPTS - avpkt->pts; // start with same PTS as input stream
            cutInfo.offsetDTSReEncode = cutInfo.startDTS - avpkt->dts; // start with same DTS as input stream
            dsyslog("cEncoder::SetSmartReEncodeOffset(): set re-encoded recording start offset PTS %" PRId64 ", DTS %" PRId64, cutInfo.offsetPTSReEncode, cutInfo.offsetDTSReEncode);
        }
        cutInfo.state = CUT_STATE_NULL;
        break;
    // adjust first output key packet PTS from re-encode part to packet duration after max last GOP and DTS to packet duration after last packet
    case CUT_STATE_START:
    case CUT_STATE_STOP:
        cutInfo.offsetPTSReEncode = cutInfo.offset + streamInfo.maxPTSofGOP + avpkt->duration - avpkt->pts;
        cutInfo.offsetDTSReEncode = cutInfo.offset + streamInfo.lastOutDTS[videoOutputStreamIndex] + avpkt->duration - avpkt->dts;
        dsyslog("cEncoder::SetSmartReEncodeOffset(): packet PTS %" PRId64 ", DTS %" PRId64, avpkt->pts, avpkt->dts);
        dsyslog("cEncoder::SetSmartReEncodeOffset(): streamInfo.maxPTSofGOP %" PRId64 ", lastOutDTS %" PRId64, streamInfo.maxPTSofGOP, streamInfo.lastOutDTS[videoOutputStreamIndex]);
        dsyslog("cEncoder::SetSmartReEncodeOffset(): set re-encoded offset PTS %" PRId64 ", DTS %" PRId64, cutInfo.offsetPTSReEncode, cutInfo.offsetDTSReEncode);
        cutInfo.state = CUT_STATE_NULL;
        break;
    default:
        break;
    }
#ifdef DEBUG_CUT_OFFSET
    dsyslog("cEncoder:SetSmartReEncodeOffset(): current re-encode offset PTS %" PRId64 ", DTS %" PRId64, cutInfo.offsetPTSReEncode, cutInfo.offsetDTSReEncode);
#endif
    if (avpkt->pts != AV_NOPTS_VALUE) avpkt->pts += cutInfo.offsetPTSReEncode;
    if (avpkt->dts != AV_NOPTS_VALUE) avpkt->dts += cutInfo.offsetDTSReEncode;
}


bool cEncoder::CutFullReEncode(const cMark *startMark, const cMark *stopMark) {
    int keyPacketNumberBeforeStart = index->GetKeyPacketNumberBeforePTS(startMark->pts);
    keyPacketNumberBeforeStart     = index->GetKeyPacketNumberBefore(keyPacketNumberBeforeStart - 1); // start decode 2 key frames before start PTS
    if (keyPacketNumberBeforeStart < 0) {
        esyslog("cEncoder::CutFullReEncode(): H.262: get key packet number before start PTS %" PRId64 " failed", startMark->pts);
        return false;
    }
    dsyslog("cEncoder::CutFullReEncode(): start mark PTS %10" PRId64 ", 2 key packet before start (%6d)", startMark->pts, keyPacketNumberBeforeStart);
    if (keyPacketNumberBeforeStart < decoder->GetPacketNumber()) {
        int newStartPos = index->GetKeyPacketNumberAfter(decoder->GetPacketNumber());
        dsyslog("cEncoder::CutFullReEncode(): key packet before start (%d) is before decoder read position (%d) new start at (%d)", keyPacketNumberBeforeStart, decoder->GetPacketNumber(), newStartPos);  // happens for too late recording starts
        keyPacketNumberBeforeStart = newStartPos;
    }

    // seek to key packet before start position
#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CutFullReEncode(): before seek: av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    if (!decoder->SeekToPacket(keyPacketNumberBeforeStart)) {
        esyslog("cEncoder::CutFullReEncode(): seek to key packet before start mark (%d) failed", keyPacketNumberBeforeStart);
        return false;
    }
    if (decoder->IsVideoPacket()) decoder->DecodePacket(); // decode packet read from seek

    // check if we have a valid frame to set for start position
#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CutFullReEncode(): before start position: av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    AVFrame *avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);  // this should be a video packet, because we seek to video packets
    while (!avFrame || !decoder->IsVideoFrame() || (avFrame->pts == AV_NOPTS_VALUE) || (avFrame->pkt_dts == AV_NOPTS_VALUE) || (avFrame->pts < startMark->pts)) {
        if (abortNow) return false;

        if (!avFrame) {
            if (decoder->IsVideoPacket()) dsyslog("cEncoder::CutFullReEncode(): packet (%d) stream %d: first video frame not yet decoded", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
        }
        else if (decoder->IsVideoPacket() && (avFrame->pts < startMark->pts)) dsyslog("cEncoder::CutFullReEncode(): packet (%d) stream %d: frame before start position, prelod decoder", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
        else if (avFrame->pts == AV_NOPTS_VALUE) dsyslog("cEncoder::CutFullReEncode(): packet (%d) stream %d: frame not valid for start position, PTS invalid", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
        else if ((avFrame->pkt_dts == AV_NOPTS_VALUE)) dsyslog("cEncoder::CutFullReEncode(): packet (%d) stream %d: frame not valid for start position, DTS invalid", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);

        decoder->DropFrame();     // we do not use this frame, cleanup buffer
        if (!decoder->ReadNextPacket()) return false;
        if (decoder->IsVideoPacket()) decoder->DecodePacket(); // decode packet, no error break, maybe we only need more frames to decode (e.g. interlaced video)
        avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);
    }
    // get PTS/DTS from start position
#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CutFullReEncode(): at start position: av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    cutInfo.startPacketNumber = decoder->GetPacketNumber();
    cutInfo.startPTS          = avFrame->pts;
    cutInfo.startDTS          = avFrame->pkt_dts;
    // current start pos - last stop pos -> length of ad
    // use dts to prevent non monotonically increasing dts
    if (cutInfo.stopDTS > 0) cutInfo.offset += (cutInfo.startDTS - cutInfo.stopDTS);
    dsyslog("cEncoder::CutFullReEncode(): start cut from packet (%6d): pict_type %d, PTS %10" PRId64 ", DTS %10" PRId64 ", offset %" PRId64, cutInfo.startPacketNumber, avFrame->pict_type, cutInfo.startPTS, cutInfo.startDTS, cutInfo.offset);

    // read and decode all packets
    while (!decoder->IsVideoFrame() || (decoder->GetFramePTS() <= stopMark->pts)) {
        if (abortNow) return false;

#ifdef DEBUG_CUT  // first picures after start mark after
        if (decoder->IsVideoFrame() && ((abs(decoder->GetFramePTS() - startMark->pts) <= (DEBUG_CUT * cutInfo.videoPacketDuration)) || (abs(decoder->GetFramePTS() - stopMark->pts) <= (DEBUG_CUT * cutInfo.videoPacketDuration)))) {
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d_CUT.pgm", recDir, decoder->GetPacketNumber()) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
        }
#endif

        // write current packet
        AVPacket *avpktIn  = decoder->GetPacket();
#ifdef DEBUG_PTS_DTS_CUT
        if ((avpktIn->stream_index == DEBUG_PTS_DTS_CUT) || (DEBUG_PTS_DTS_CUT == -1)) {
            dsyslog("cEncoder::CutFullReEncode(): re-encode packet in (%5d), stream %d: flags %d, PTS %10ld, DTS %10ld, diff: PTS key %8ld, DTS %5ld", decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->flags, avpktIn->pts, avpktIn->dts, (inputKeyPacketPTSbefore[avpktIn->stream_index] >= 0) ? avpktIn->pts - inputKeyPacketPTSbefore[avpktIn->stream_index] : -1, avpktIn->dts - lastPacketInDTS[avpktIn->stream_index]);
            if (avpktIn->flags & AV_PKT_FLAG_KEY) inputKeyPacketPTSbefore[avpktIn->stream_index] = avpktIn->pts;
            lastPacketInDTS[avpktIn->stream_index] = avpktIn->dts;
        }
#endif
        if (avpktIn->pts >= cutInfo.startPTS) {
            // re-encode if needed
            if (decoder->IsVideoPacket()) {  // always decode video stream
                cutInfo.videoPacketDuration = avpktIn->duration;
                if (!EncodeVideoFrame()) {
                    esyslog("cEncoder::CutFullReEncode(): decoder packet (%d): EncodeVideoFrame() failed", decoder->GetPacketNumber());
                    return false;
                }
            }
            else if (ac3ReEncode && (pass == 2) && decoder->IsAudioAC3Packet()) {  // only re-encode AC3 if volume change is requested
                if (!EncodeAC3Frame()) {
                    esyslog("cEncoder::CutFullReEncode(): decoder packet (%d): EncodeAC3Frame() failed", decoder->GetPacketNumber());
                    return false;
                }
            }
            // no re-encode need, use input packet and write packet
            else if (!WritePacket(avpktIn, false)) {
                esyslog("cEncoder::CutFullReEncode(): decoder packet (%d): WritePacket() failed", decoder->GetPacketNumber());
                return false;
            }
        }
        else dsyslog("cEncoder::CutFullReEncode(): packet (%d), stream %d, PTS %" PRId64 ", DTS %" PRId64 ": drop packet before start PTS %" PRId64, decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPTS);
        // read and decode next packet
        while (true) {
            if (!decoder->ReadNextPacket()) return false;
            if (abortNow) return false;
            if (decoder->IsVideoPacket() ||
                    (ac3ReEncode && (pass == 2) && decoder->IsAudioAC3Packet())) { // always decode video, AC3 only if needed
                if (!decoder->DecodePacket()) continue;       // decode packet, no error break, maybe we only need more frames to decode (e.g. interlaced video)
            }
            break;
        }
    }
    // get PTS/DTS from end position
    avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);  // this must be first video packet after cut because we only increase frame counter on video frames
    cutInfo.stopPacketNumber = decoder->GetPacketNumber();
    cutInfo.stopPTS          = avFrame->pts;
    cutInfo.stopDTS          = avFrame->pkt_dts;
    dsyslog("cEncoder::CutFullReEncode(): end cut from i-frame (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to i-frame (%6d) PTS %10" PRId64 " DTS %10" PRId64 ", offset %10" PRId64, cutInfo.startPacketNumber, cutInfo.startPTS, cutInfo.startDTS, cutInfo.stopPacketNumber, cutInfo.stopPTS, cutInfo.stopDTS, cutInfo.offset);
    return true;
}


// decode from key packet before start position, re-encode from start position to next key frame
// copy from key packet after start position to key packet before stop position
// re-encode from key frame before stop position to stop position
bool cEncoder::CutSmart(cMark *startMark, cMark *stopMark) {
    if (!startMark) return false;
    if (!stopMark)  return false;

    int keyPacketNumberBeforeStart  = -1;
    int keyPacketNumberAfterStart   = -1;
    int64_t ptsKeyPacketBeforeStart = -1;
    int64_t ptsKeyPacketAfterStart  = -1;
    int keyPacketNumberBeforeStop   = -1;
    int keyPacketNumberAfterStop    = -1;
    int64_t ptsKeyPacketBeforeStop  = -1;
    int64_t ptsKeyPacketAfterStop   = -1;

    switch (decoder->GetVideoType()) {
    case MARKAD_PIDTYPE_VIDEO_H262:
        // calculate key packet before start position
        keyPacketNumberBeforeStart = index->GetKeyPacketNumberBeforePTS(startMark->pts);
        if (keyPacketNumberBeforeStart < 0) {
            esyslog("cEncoder::CutSmart(): H.262: get key packet number before start PTS %" PRId64 " failed", startMark->pts);
            return false;
        }
        ptsKeyPacketBeforeStart = index->GetPTSFromKeyPacketNumber(keyPacketNumberBeforeStart);
        // calculate key packet after start position
        keyPacketNumberAfterStart = index->GetKeyPacketNumberAfterPTS(startMark->pts, &ptsKeyPacketAfterStart);
        if (ptsKeyPacketAfterStart > startMark->pts) {  // have to re-encode start part, go one GOP after in case of start PTS is in next GOP with negativ offset to key packet
            keyPacketNumberAfterStart = index->GetKeyPacketNumberAfterPTS(ptsKeyPacketAfterStart + 1, &ptsKeyPacketAfterStart);
        }
        if (keyPacketNumberAfterStart < 0) {
            esyslog("cEncoder::CutSmart(): H.262: get key packet number after start PTS %" PRId64 " failed", startMark->pts);
            return false;
        }
        // calculate end position
        keyPacketNumberBeforeStop = index->GetKeyPacketNumberBeforePTS(stopMark->pts);
        if (keyPacketNumberBeforeStop < 0) {
            esyslog("cEncoder::CutSmart(): H.262: get key packet number before stop PTS %" PRId64 " failed", stopMark->pts);
            return false;
        }
        keyPacketNumberAfterStop = index->GetKeyPacketNumberAfterPTS(stopMark->pts);
        if (keyPacketNumberAfterStop < 0) {
            esyslog("cEncoder::CutSmart(): H.262: get key packet number after stop PTS %" PRId64 " failed", stopMark->pts);
            return false;
        }
        ptsKeyPacketBeforeStop = index->GetPTSFromKeyPacketNumber(keyPacketNumberBeforeStop);
        ptsKeyPacketAfterStop  = index->GetPTSFromKeyPacketNumber(keyPacketNumberAfterStop);

        break;

    case MARKAD_PIDTYPE_VIDEO_H264:
        // calculate key packet before start position
        if (startMark->pts == index->GetStartPTS()) {
            dsyslog("cEncoder::CutSmart(): H.264: cut from first key packet after recording start");
            // PTS start can be key packet, we can not use this, because we have to read first GOP to init hardware decoder
            keyPacketNumberBeforeStart = index->GetKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketBeforeStart, false);
            if (keyPacketNumberBeforeStart < 0) {
                esyslog("cEncoder::CutSmart(): H.264: get key packet number after recording start PTS %" PRId64 " failed", startMark->pts);
                return false;
            }
        }
        else {
            keyPacketNumberBeforeStart = index->GetKeyPacketNumberBeforePTS(startMark->pts);
            keyPacketNumberBeforeStart = index->GetKeyPacketNumberBefore(keyPacketNumberBeforeStart - 1);  // for H.264 start one GOP before because of B frame references before i-frame
            if (keyPacketNumberBeforeStart < 0) {
                esyslog("cEncoder::CutSmart(): H.264: get key packet number before start PTS %" PRId64 " failed", startMark->pts);
                return false;
            }
            ptsKeyPacketBeforeStart = index->GetPTSFromKeyPacketNumber(keyPacketNumberBeforeStart);
        }
        // calculate key packet before end position
        keyPacketNumberBeforeStop = index->GetKeyPacketNumberBeforePTS(stopMark->pts - 1);  // one GOP back because of negativ PTS offset
        if (keyPacketNumberBeforeStop < 0) {
            esyslog("cEncoder::CutSmart(): H.264: get key packet number before stop PTS %" PRId64 " failed", stopMark->pts);
            return false;
        }
        // calculate p-slice key packet after start position
        if (decoder->GetFullDecode()) {
            keyPacketNumberAfterStart = index->GetPSliceKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketAfterStart);
            if (keyPacketNumberAfterStart >= keyPacketNumberBeforeStop) {
                dsyslog("cEncoder::CutSmart(): no p-slice before next stop mark found, use key packet after start");
                keyPacketNumberAfterStart = -1;
                ptsKeyPacketAfterStart    = -1;
            }
        }
        else keyPacketNumberAfterStart = GetPSliceKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketAfterStart, keyPacketNumberBeforeStop);
        if (keyPacketNumberAfterStart < 0) {
            dsyslog("cEncoder::CutSmart(): H.264: get p-slice after start PTS %" PRId64 " failed, fallback to key packet", startMark->pts);
            keyPacketNumberAfterStart = index->GetKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketAfterStart, true);
            if (keyPacketNumberAfterStart < 0) {
                dsyslog("cEncoder::CutSmart(): H.264: get key packet number with PTS in slice after start PTS %" PRId64 " failed, try any key packet", startMark->pts);
                keyPacketNumberAfterStart = index->GetKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketAfterStart, false);
                if (keyPacketNumberAfterStart < 0) {
                    esyslog("cEncoder::CutSmart(): H.264: get key packet number after start PTS %" PRId64 " failed", startMark->pts);
                    return false;
                }
            }
        }
        ptsKeyPacketBeforeStop = index->GetPTSFromKeyPacketNumber(keyPacketNumberBeforeStop);
        // calculate key packet after end position
        keyPacketNumberAfterStop = index->GetKeyPacketNumberAfterPTS(stopMark->pts + 1);
        keyPacketNumberAfterStop = index->GetKeyPacketNumberAfter(keyPacketNumberAfterStop + 1); // one GOP more in case of negativ PTS offset
        if (keyPacketNumberAfterStop < 0) {
            esyslog("cEncoder::CutSmart(): H.264: get key packet number after stop PTS %" PRId64 " failed", stopMark->pts);
            return false;
        }
        ptsKeyPacketAfterStop  = index->GetPTSFromKeyPacketNumber(keyPacketNumberAfterStop);
        // for performance reason, we do no want to re-encode more than the half between start/stop mark
        if (ptsKeyPacketAfterStart >= ((keyPacketNumberBeforeStop - stopMark->pts) / 2)) {
            dsyslog("cEncoder::CutSmart(): key packet number with PTS in slice after start too far away, fallback to key packet");
            keyPacketNumberAfterStart = index->GetKeyPacketNumberAfterPTS(startMark->pts + 1, &ptsKeyPacketAfterStart, false);
        }
        break;
    case MARKAD_PIDTYPE_VIDEO_H265:
        return CutKeyPacket(startMark, stopMark);
        break;
    default:
        esyslog("cEncoder::CutSmart(): smart cut of this codec not supported");
        return false;
    }
    dsyslog("cEncoder::CutSmart(): start mark PTS %10" PRId64 ", key packet before start (%6d), PTS %" PRId64, startMark->pts, keyPacketNumberBeforeStart, ptsKeyPacketBeforeStart);
    dsyslog("cEncoder::CutSmart(): start mark PTS %10" PRId64 ", key packet after  start (%6d), PTS %" PRId64, startMark->pts, keyPacketNumberAfterStart,  ptsKeyPacketAfterStart);
    dsyslog("cEncoder::CutSmart(): stop  mark PTS %10" PRId64 ", key packet before stop  (%6d), PTS %" PRId64, stopMark->pts, keyPacketNumberBeforeStop, ptsKeyPacketBeforeStop);
    dsyslog("cEncoder::CutSmart(): stop  mark PTS %10" PRId64 ", key packet after  stop  (%6d), PTS %" PRId64, stopMark->pts, keyPacketNumberAfterStop,  ptsKeyPacketAfterStop);

    if (!decoder->SeekToPacket(keyPacketNumberBeforeStart)) {
        esyslog("cEncoder::CutSmart(): seek to packet (%d) failed", keyPacketNumberBeforeStart);
        return false;
    }
    AVPacket *avpkt = decoder->GetPacket();   // after seek we have a video packet

    // re-encode from key packet before start PTS to key packet after start position
    if (avpkt->pts < ptsKeyPacketAfterStart) {
        LogSeparator();
        dsyslog("cEncoder::CutSmart(): re-encode in start part");
        if (cutInfo.state != CUT_STATE_FIRSTPACKET) cutInfo.state = CUT_STATE_START;
        forceIFrame = true;
        while (decoder->GetPacketNumber() < keyPacketNumberAfterStart) {
            if (abortNow) return false;
            if (decoder->IsVideoPacket()) {  // re-encode video packet

#ifdef DEBUG_PTS_DTS_CUT
                dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d, PTS %ld, DTS %ld, flags %d, duration %ld: re-encode video packet in start part", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->pts, avpkt->dts, avpkt->flags, avpkt->duration);
#endif
                if (avpkt->pts == startMark->pts) {
                    if (streamInfo.lastOutDTS[videoOutputStreamIndex] >= 0) {
                        cutInfo.offset = avpkt->dts - streamInfo.lastOutDTS[videoOutputStreamIndex] - avpkt->duration;
                    }
                    cutInfo.startPTS = avpkt->pts;
                    cutInfo.startDTS = avpkt->dts;
                    dsyslog("cEncoder::CutSmart(): re-encode in: start mark (%d), stream %d,  PTS %" PRId64 ", DTS %" PRId64 ", duration %" PRId64 ": lastOutDTS %" PRId64 ", new offset %" PRId64, decoder->GetPacketNumber(), avpkt->stream_index, avpkt->pts, avpkt->dts, avpkt->duration, streamInfo.lastOutDTS[videoOutputStreamIndex], cutInfo.offset);
                }
                cutInfo.videoPacketDuration = avpkt->duration;
                if (decoder->DecodePacket()) {

#ifdef DEBUG_CUT  // debug all picures before and after start mark
                    char suffix[16] = "";
                    if (decoder->GetFramePTS() == startMark->pts) strcpy(suffix, "START");
                    else if (decoder->GetFramePTS() < startMark->pts) strcpy(suffix, "START_BEFORE");
                    else if (decoder->GetFramePTS() > startMark->pts) strcpy(suffix, "START_AFTER");
                    char *fileName = nullptr;
                    if (asprintf(&fileName,"%s/F__%07d_%10ld_%s_CUT.pgm", recDir, decoder->GetPacketNumber(), decoder->GetFramePTS(), suffix) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
#endif

                    if (decoder->GetFramePTS() >= startMark->pts) {
#ifdef DEBUG_PTS_DTS_CUT
                        dsyslog("cEncoder::CutSmart(): frame from decoder, send to encoder: PTS %" PRId64, decoder->GetFramePTS());
#endif
                        if (!EncodeVideoFrame()) {
                            esyslog("cEncoder::CutSmart(): decoder packet (%d): EncodeVideoFrame() failed during re-encoding", decoder->GetPacketNumber());
                            return false;
                        }
                    }
#ifdef DEBUG_PTS_DTS_CUT
                    else dsyslog("cEncoder::CutSmart(): frame from decoder: PTS %" PRId64 ": drop frame before start PTS %" PRId64, decoder->GetFramePTS(), startMark->pts);
#endif
                }
            }
            else { // copy non video packet
                if (avpkt->pts >= startMark->pts) {
                    if (!WritePacket(avpkt, false)) {  // no re-encode done
                        esyslog("cEncoder::CutSmart(): WritePacket() failed during re-encoding");
                        return false;
                    }
                }
#ifdef DEBUG_PTS_DTS_CUT
                else dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d: flags %d, PTS %" PRId64 ": drop packet %6" PRId64 " before start PTS %" PRId64, decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, startMark->pts - avpkt->pts, startMark->pts);
#endif
            }
            if (!decoder->ReadNextPacket()) return false;
            avpkt = decoder->GetPacket();
        }
        if (!DrainVideoReEncode(startMark->pts, stopMark->pts)) return false;
        // re-adjust offset, last packet was a video packet, fist key frame to copy is in decoder
        cutInfo.offsetPTSReEncode = 0;
        cutInfo.offsetDTSReEncode = 0;
        cutInfo.offset = avpkt->dts - streamInfo.lastOutDTS[videoOutputStreamIndex] - avpkt->duration;
        dsyslog("cEncoder::CutSmart(): copy packets (%d) PTS %" PRId64 ", DTS %" PRId64 ", duration %" PRId64 ": lastOutDTS %" PRId64 ", new offset %" PRId64, decoder->GetPacketNumber(), avpkt->pts, avpkt->dts, avpkt->duration, streamInfo.lastOutDTS[videoOutputStreamIndex], cutInfo.offset);
    }
    // we start cut with a key frame
    else {
        if (streamInfo.lastOutDTS[videoOutputStreamIndex] >= 0) {
            cutInfo.offset = avpkt->dts - streamInfo.lastOutDTS[videoOutputStreamIndex] - avpkt->duration;  // reset PTS offset to DTS offset
        }
        dsyslog("cEncoder::CutSmart(): offset %" PRId64, cutInfo.offset);
    }
    cutInfo.state = CUT_STATE_NULL;    // clear state CUT_STATE_FIRSTPACKET in case of first start mark is key packet

    // copy packet from key packet after start to before key packet before stop position
    LogSeparator();
    dsyslog("cEncoder::CutSmart(): copy packets");
    while (decoder->GetPacketNumber() < keyPacketNumberBeforeStop) {
        if (abortNow) return false;
#ifdef DEBUG_PTS_DTS_CUT
        if ((avpkt->stream_index == DEBUG_PTS_DTS_CUT) || (DEBUG_PTS_DTS_CUT == -1)) {
//            dsyslog("cEncoder::CutSmart(): copy in (%5d), stream %d: flags %d, PTS %10ld, DTS %10ld, diff: PTS key %8ld, DTS %5ld", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts, (inputKeyPacketPTSbefore[avpkt->stream_index] >= 0) ? avpkt->pts - inputKeyPacketPTSbefore[avpkt->stream_index] : -1, (lastPacketInDTS[avpkt->stream_index] >= 0) ? avpkt->dts - lastPacketInDTS[avpkt->stream_index] : -1);
            if (avpkt->flags & AV_PKT_FLAG_KEY) inputKeyPacketPTSbefore[avpkt->stream_index] = avpkt->pts;
            lastPacketInDTS[avpkt->stream_index] = avpkt->dts;

        }
#endif
        // check valid stream index
        if (avpkt->stream_index < static_cast<int>(avctxOut->nb_streams)) {
            // check PTS/DTS rollover
            if (!rollover && (avpkt->pts != AV_NOPTS_VALUE) && (avctxOut->streams[avpkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) && (streamInfo.lastInPTS[avpkt->stream_index] > 0x200000000) && (avpkt->pts < 0x100000000)) {
                rollover = true;
                dsyslog("cEncoder::CutSmart(): input stream PTS/DTS rollover from PTS %" PRId64 " to %" PRId64, streamInfo.lastInPTS[avpkt->stream_index], avpkt->pts);
            }
            if (rollover) {
                if (avpkt->pts != AV_NOPTS_VALUE) avpkt->pts += 0x200000000;
                if (avpkt->dts != AV_NOPTS_VALUE) avpkt->dts += 0x200000000;
            }
            streamInfo.lastInPTS[avpkt->stream_index] = avpkt->pts;

            // write current packet
            if (avpkt->pts >= startMark->pts) {
                if (!WritePacket(avpkt, false)) {  // no re-encode was done
                    esyslog("cEncoder::CutSmart(): WritePacket() failed");
                    return false;
                }
            }
            else {
                if (avpkt->pts == AV_NOPTS_VALUE) dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d: flags %d, PTS %" PRId64", DTS %" PRId64 ": PTS = AV_NOPTS_VALUE, drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts);
#ifdef DEBUG_PTS_DTS_CUT
                else dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d: flags %d, PTS %" PRId64", DTS %" PRId64 ": drop packet %6" PRId64 " before start PTS %" PRId64, decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts, startMark->pts - avpkt->pts, startMark->pts);
#endif
            }
        }
        else {
            if (!decoder->IsSubtitlePacket()) dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d: unexpected stream index, drop packet", decoder->GetPacketNumber(), avpkt->stream_index);  // expected for subtitle stream
        }
        if (!decoder->ReadNextPacket()) return false;
        avpkt = decoder->GetPacket();
    }

// re-encode from key packet before stop position to key packet after stop position (always decode complete GOP), drop packets with PTS after stop PTS
    if (avpkt->pts < stopMark->pts) {
        LogSeparator();
        dsyslog("cEncoder::CutSmart(): re-encode in stop part from key packet before stop mark (%d) to key packet after stop mark (%d)", decoder->GetPacketNumber(), keyPacketNumberAfterStop - 1);
        cutInfo.state = CUT_STATE_STOP;
        while (decoder->GetPacketNumber() < keyPacketNumberAfterStop) {
            if (abortNow) return false;
            if (rollover) {
                avpkt->pts += 0x200000000;
                avpkt->dts += 0x200000000;
            }
            streamInfo.lastInPTS[avpkt->stream_index] = avpkt->pts;
#ifdef DEBUG_PTS_DTS_CUT
            dsyslog("cEncoder::CutSmart(): packet in (%d) -> decoder: stream %d, flags %d, duration %" PRId64 ", PTS %" PRId64 ", DTS %" PRId64, decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->duration, avpkt->pts, avpkt->dts);
#endif
            if (decoder->IsVideoPacket()) {  // re-encode video packet
                if (avpkt->pts == stopMark->pts) {
                    cutInfo.stopPTS = avpkt->pts;
                    cutInfo.stopDTS = avpkt->dts;
                    dsyslog("cEncoder::CutSmart(): stop mark (%d) PTS %" PRId64 ", DTS %" PRId64 ", duration %" PRId64, decoder->GetPacketNumber(), avpkt->pts, avpkt->dts, avpkt->duration);
                }
                cutInfo.videoPacketDuration = avpkt->duration;
                if (decoder->DecodePacket()) {
#ifdef DEBUG_PTS_DTS_CUT
                    dsyslog("cEncoder::CutSmart(): frame decoder -> encoder: PTS %" PRId64 ", DTS %" PRId64, decoder->GetFramePTS(), decoder->GetFrameDTS());
#endif
                    if ((decoder->GetFramePTS() <= stopMark->pts) && (decoder->GetFramePTS() >= ptsKeyPacketBeforeStop)) {  // sometimes we got frame from start part back from decoder
                        if (!EncodeVideoFrame()) {
                            esyslog("cEncoder::CutSmart(): decoder packet (%d): EncodeVideoFrame() failed", decoder->GetPacketNumber());
                            return false;
                        }
                    }
                    else {
#ifdef DEBUG_PTS_DTS_CUT
                        dsyslog("cEncoder::CutSmart(): packet (%d): drop video input frame with PTS %" PRId64 " after stop mark PTS %" PRId64, decoder->GetPacketNumber(), decoder->GetFramePTS(), stopMark->pts);
#endif
                        decoder->DropFrame();     // we do not use this frame, cleanup buffer
                    }
                }
            }
            else { // copy non video packet
                if (avpkt->pts >= startMark->pts) {
                    if (!WritePacket(avpkt, false)) {  // no re-encode done
                        esyslog("cEncoder::CutSmart(): WritePacket() failed during re-encoding");
                        return false;
                    }
                }
#ifdef DEBUG_PTS_DTS_CUT
                else dsyslog("cEncoder::CutSmart(): packet (%5d), stream %d: flags %d, PTS %" PRId64 ": drop packet before start PTS %" PRId64, decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, startMark->pts);
#endif
            }
            if (!decoder->ReadNextPacket()) return false;
            avpkt = decoder->GetPacket();
        }
        if (!DrainVideoReEncode(startMark->pts, stopMark->pts)) return false;
        cutInfo.offsetPTSReEncode = 0;
        cutInfo.offsetDTSReEncode = 0;
    }
    return true;
}


// cut without decoding, only copy all packets from key packet after start mark to key packet before stop mark
bool cEncoder::CutKeyPacket(const cMark *startMark, cMark *stopMark) {
    // get start position
    int startPos  = -1;
    if (startMark->pts >= 0) startPos = index->GetKeyPacketNumberAfterPTS(startMark->pts);
    if (startPos < 0) {
        dsyslog("cEncoder::CutKeyPacket(): mark (%d): pts based cut position failed, use packet number", stopMark->position);
        startPos = startMark->position;
    }
    if (startPos < decoder->GetPacketNumber()) {
        dsyslog("cEncoder::CutKeyPacket(): startPos (%d) before decoder read position (%d)", startPos,  decoder->GetPacketNumber());  // happens for too late recording starts
        startPos = decoder->GetPacketNumber();
    }
    startPos = index->GetKeyPacketNumberAfter(startPos);  // adjust to i-frame
    if (startPos < 0) {
        esyslog("cEncoder::CutKeyPacket():: get i-frame after (%d) failed", startPos);
        return false;
    }

    // get stop position
    int stopPos  = -1;
    if (stopMark->pts >= 0) stopPos  = index->GetKeyPacketNumberBeforePTS(stopMark->pts);
    if (stopPos < 0) {
        dsyslog("cEncoder::CutKeyPacket(): mark (%d): pts based cut position failed, use packet number", stopMark->position);
        stopPos = stopMark->position;
    }
    stopPos = index->GetKeyPacketNumberBefore(stopPos);  // adjust to i-frame
    if (stopPos < 0) {
        esyslog("cEncoder::CutKeyPacket():: get i-frame before (%d) failed", stopPos);
        return false;
    }
    dsyslog("cEncoder::CutKeyPacket(): PTS based cut from key packet (%d) to key packet (%d)", startPos, stopPos);

    // seek to start position
    if (!decoder->SeekToPacket(startPos)) {  // this read startPos
        esyslog("cEncoder::CutKeyPacket(): seek to packet (%d) failed", startPos);
        return false;
    }
    // check if we have a valid packet to set for start position
    AVPacket *avpkt = decoder->GetPacket();  // this should be a video packet, because we seek to video packets
    while (!decoder->IsVideoKeyPacket() || (avpkt->pts == AV_NOPTS_VALUE) || (avpkt->dts == AV_NOPTS_VALUE)) {
        if (abortNow) return false;
        esyslog("cEncoder::CutKeyPacket(): packet (%d), stream %d: packet not valid for start position", decoder->GetPacketNumber(), avpkt->stream_index);
        if (!decoder->ReadNextPacket()) return false;
        avpkt = decoder->GetPacket();  // this must be a video packet, because we seek to video packets
    }

    // get PTS/DTS from start position
    cutInfo.startPTS = avpkt->pts;
    cutInfo.startDTS = avpkt->dts;
    // current start pos - last stop pos -> length of ad
    // use dts to prevent non monotonically increasing dts
    if (cutInfo.stopDTS > 0) cutInfo.offset += (cutInfo.startDTS - cutInfo.stopDTS);
    dsyslog("cEncoder::CutKeyPacket(): start cut from key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to key packet (%d), offset %" PRId64, startPos, cutInfo.startPTS, cutInfo.startDTS, stopPos, cutInfo.offset);

    // copy all packets from startPos to endPos
    // end before last key packet, B/P frame refs are always backward in TS stream
    // start packet will be next key packet
    while (decoder->GetPacketNumber() < stopPos) {
        if (abortNow) return false;

#ifdef DEBUG_CUT  // first pictures after start mark after
        decoder->DecodePacket();   // no decoding from encoder, do it here
        if (decoder->IsVideoFrame() && ((abs(decoder->GetPacketNumber() - startPos) <= DEBUG_CUT) || (abs(decoder->GetPacketNumber() - stopPos) <= DEBUG_CUT))) {
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d_CUT.pgm", recDir, decoder->GetPacketNumber()) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
        }
#endif

        // write current packet
        AVPacket *avpktIn = decoder->GetPacket();
        if (avpktIn) {
            if (avpktIn->pts >= cutInfo.startPTS) {
                if (!WritePacket(avpktIn, false)) {  // no re-encode done
                    esyslog("cEncoder::CutKeyPacket(): WritePacket() failed");
                    return false;
                }
            }
            else dsyslog("cEncoder::CutKeyPacket(): packet (%d), stream %d, PTS %" PRId64", DTS %" PRId64 ": drop packet before start PTS %" PRId64 ", DTS %" PRId64, decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPTS, cutInfo.startDTS);
        }
        else esyslog("cEncoder::CutKeyPacket(): GetPacket() failed");
        // read next packet
        if (!decoder->ReadNextPacket()) return false;
    }

    // get PTS/DTS from end position
    avpkt =  decoder->GetPacket();  // this must be first video packet after cut because we only increase packet counter on video packets
    cutInfo.stopPTS = avpkt->pts;
    cutInfo.stopDTS = avpkt->dts;
    dsyslog("cEncoder::CutKeyPacket(): end cut from key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 ", offset %10" PRId64, startPos, cutInfo.startPTS, cutInfo.startDTS, stopPos, cutInfo.stopPTS, cutInfo.stopDTS, cutInfo.offset);
    return true;
}


bool cEncoder::EncodeVideoFrame() {
    // map input stream index to output stream index, should always be 0->0 for video stream
    CheckInputFileChange();
    int streamIndexIn = decoder->GetPacket()->stream_index;
    if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) {
        esyslog("cEncoder::EncodeVideoFrame(): decoder packet (%d), stream %d: input stream index out of range (0 to %d)", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index, avctxIn->nb_streams);
        return false; // prevent to overrun stream array
    }
    int streamIndexOut = streamMap[streamIndexIn];
    if (streamIndexOut == -1) {
        esyslog("cEncoder::EncodeVideoFrame(): decoder packet (%d), stream %d: out stream index %d invalid", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index, streamIndexOut);
        return false; // no target for this stream
    }
    AVFrame *avFrame = decoder->GetFrame(codecCtxArrayOut[streamIndexOut]->pix_fmt);  // get video frame with converted data planes
    if (!avFrame) {
        dsyslog("cEncoder::EncodeVideoFrame(): decoder packet (%d): got no valid frame", decoder->GetPacketNumber());
        return true;  // can happen, try with next frame
    }
    // check PTS and DTS
    if (avFrame->pts == AV_NOPTS_VALUE) {
        dsyslog("cEncoder::EncodeVideoFrame(): decoder packet (%d): PTS not valid, drop packet", decoder->GetPacketNumber());
        decoder->DropFrame();
        return true;  // can happen, try with next frame
    }
    if (avFrame->pkt_dts == AV_NOPTS_VALUE) {
        dsyslog("cEncoder::EncodeVideoFrame(): decoder packet (%d): DTS not valid, drop packet", decoder->GetPacketNumber());
        decoder->DropFrame();
        return true;  // can happen, try with next frame
    }
#ifdef DEBUG_PTS_DTS_CUT
    dsyslog("cEncoder::EncodeVideoFrame():   frame decoder -> encoder: PTS %10" PRId64 ", DTS %10ld, diff PTS %" PRId64 ", DTS %" PRId64, avFrame->pts, avFrame->pkt_dts, (lastFrameInPTS[streamIndexOut] >= 0) ? avFrame->pts - lastFrameInPTS[streamIndexOut] : -1, (lastFrameInDTS[streamIndexOut] >= 0) ? avFrame->pkt_dts - lastFrameInDTS[streamIndexOut] : -1);
    lastFrameInPTS[streamIndexOut] = avFrame->pts;
    lastFrameInDTS[streamIndexOut] = avFrame->pkt_dts;
#endif
    codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = avFrame->sample_aspect_ratio; // aspect ratio can change, set encoder pixel aspect ratio to decoded frames aspect ratio

    if (!codecCtxArrayOut[streamIndexOut]) {
        esyslog("cEncoder::EncodeVideoFrame(): encoding of stream %d not supported", streamIndexIn);
        return false;
    }
    // send frame to encoder
    avFrame->pict_type = AV_PICTURE_TYPE_NONE;     // encoder decides picture type
    if (!SendFrameToEncoder(streamIndexOut, avFrame)) {
        esyslog("cEncoder::EncodeFrame(): decoder packet (%d): SendFrameToEncoder() failed", decoder->GetPacketNumber());
        return false;
    }
    // receive packet from decoder
    AVPacket *avpktOut = ReceivePacketFromEncoder(streamIndexOut);
    while (avpktOut) {
        // store stats for second pass
        if ((pass == 1) && codecCtxArrayOut[streamIndexOut]->stats_out) {
            long int strLength = strlen(codecCtxArrayOut[streamIndexOut]->stats_out);
            if (!stats_in.data) {
                stats_in.data = static_cast<char *>(malloc(strLength + 1));  // take care of terminating 0
                if (!stats_in.data) return false;
                stats_in.size = strLength + 1;
                strcpy(stats_in.data, codecCtxArrayOut[streamIndexOut]->stats_out);
                ALLOC(stats_in.size, "stats_in");
            }
            else {
                long int oldLength = strlen(stats_in.data);
                FREE(stats_in.size, "stats_in");
                char *stats_in_tmp;
                stats_in_tmp = static_cast<char *>(realloc(stats_in.data, strLength + oldLength + 1));
                if (!stats_in_tmp) {
                    esyslog("memory alloation for stats_in failed");  // free of stats_in in destructor
                    FREE(sizeof(*avpktOut), "avpktOut");
                    av_packet_free(&avpktOut);
                    return false;
                }
                stats_in.data = stats_in_tmp;
                stats_in.size = strLength + oldLength + 1;
                strcat(stats_in.data, codecCtxArrayOut[streamIndexOut]->stats_out);
                stats_in.size = strLength + oldLength + 1;
                ALLOC(stats_in.size, "stats_in");
            }
        }
#if LIBAVCODEC_VERSION_INT >= ((60<<16)+(3<<8)+100) // ffmpeg 6.0.1
        avpktOut->duration = avFrame->duration; // set packet duration, not set by encoder
#else
        avpktOut->duration = avFrame->pkt_duration; // set packet duration, not set by encoder
#endif
#ifdef DEBUG_PTS_DTS_CUT
        if (pass != 1) {
            dsyslog("cEncoder::EncodeVideoFrame(): packet from encoder: PTS %10ld, DTS %10ld, diff DTS %ld", avpktOut->pts, avpktOut->dts, avpktOut->dts - lastPacketOutDTS[avpktOut->stream_index]);
            lastPacketOutDTS[avpktOut->stream_index] = avpktOut->dts;

        }
#endif
        // adjust PTS/DTS offset for smart re-encode
        if (pass == 0) SetSmartReEncodeOffset(avpktOut);
        // write packet
        if (!WritePacket(avpktOut, true)) {  // packet was re-encoded
            esyslog("cEncoder::EncodeFrame(): WritePacket() failed");
            FREE(sizeof(*avpktOut), "avpktOut");
            av_packet_free(&avpktOut);
            return false;
        }
        FREE(sizeof(*avpktOut), "avpktOut");
        av_packet_free(&avpktOut);
        avpktOut = ReceivePacketFromEncoder(streamIndexOut);
    }
    return true;
}


int cEncoder::GetAC3ChannelCount(const int streamIndex) {
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    return codecCtxArrayOut[streamIndex]->ch_layout.nb_channels;
#else
    return codecCtxArrayOut[streamIndex]->channels;
#endif
}



bool cEncoder::EncodeAC3Frame() {
    // map input stream index to output stream index
    CheckInputFileChange();
    int streamIndexIn = decoder->GetPacket()->stream_index;
    if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) return false; // prevent to overrun stream array
    int streamIndexOut = streamMap[streamIndexIn];
    if (streamIndexOut == -1) return false; // no target for this stream

    AVFrame *avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);   // keep audio format
    if (!avFrame) {
        dsyslog("cEncoder::EncodeAC3Frame(): decoder packet (%d): got no valid frame", decoder->GetPacketNumber());
        return true;  // can happen, try with next packet
    }

    // check encoder, it can be wrong if recording is damaged
    if (decoder->IsAudioAC3Packet() && avctxOut->streams[streamIndexOut]->codecpar->codec_id != AV_CODEC_ID_AC3) {
        esyslog("cEncoder:EncodeAC3Frame(): decoder packet (%d), stream %d: invalid encoder for AC3 packet", decoder->GetPacketNumber(), streamIndexOut);
        return false;
    }

    // Check if the AC3 stream has changed channels
    if (decoder->GetAC3ChannelCount(streamIndexIn) != GetAC3ChannelCount(streamIndexOut)) {
        dsyslog("cEncoder:EncodeAC3Frame(): decoder packet (%d), stream %d: channel count input stream %d different to output stream %d", decoder->GetPacketNumber(), streamIndexIn, decoder->GetAC3ChannelCount(streamIndexIn), GetAC3ChannelCount(streamIndexOut));
        if(!ChangeEncoderCodec(streamIndexIn, streamIndexOut, codecCtxArrayIn[streamIndexIn])) {
            esyslog("encoder initialization failed for output stream index %d, source is stream index %d", streamIndexOut, streamIndexIn);
            return false;
        }
    }

    // use filter to adapt AC3 volume
    if (!volumeFilterAC3[streamIndexOut]->SendFrame(avFrame)) {
        esyslog("cEncoder::EncodeAC3Frame(): decoder packet (%d), input %d, output %d: cAC3VolumeFilter::SendFrame() failed", decoder->GetPacketNumber(), streamIndexIn, streamIndexOut);
        return false;
    }
    if (!volumeFilterAC3[streamIndexOut]->GetFrame(avFrame)) {
        esyslog("cEncoder:EncodeAC3Frame(): cAC3VolumeFilter GetFrame failed");
        return false;
    }
    if (!codecCtxArrayOut[streamIndexOut]) {
        esyslog("cEncoder::EncodeAC3Frame(): encoding of stream %d not supported", streamIndexIn);
        return false;
    }
    // send frame to encoder
    if (!SendFrameToEncoder(streamIndexOut, avFrame)) {
        esyslog("cEncoder::EncodeFrame(): decoder packet (%d): SendFrameToEncoder() failed", decoder->GetPacketNumber());
        return false;
    }
    // receive packet from decoder
    AVPacket *avpktOut = ReceivePacketFromEncoder(streamIndexOut);
    while (avpktOut) {
        // write packet
        if (!WritePacket(avpktOut, true)) {  // packet was re-encoded
            esyslog("cEncoder::EncodeFrame(): WritePacket() failed");
            FREE(sizeof(*avpktOut), "avpktOut");
            av_packet_free(&avpktOut);
            return false;
        }
        FREE(sizeof(*avpktOut), "avpktOut");
        av_packet_free(&avpktOut);
        avpktOut = ReceivePacketFromEncoder(streamIndexOut);
    }
    return true;
}


bool cEncoder::WritePacket(AVPacket *avpkt, const bool reEncoded) {
    if (!avpkt) {
        esyslog("cEncoder::WritePacket():  out (%d): invalid packet", decoder->GetPacketNumber());  // packet invalid, try next
        return true;
    }
#ifdef DEBUG_CUT_WRITE
    if ((avpkt->stream_index == DEBUG_CUT_WRITE) || (DEBUG_CUT_WRITE == -1)) {
        dsyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d, PTS %10ld, DTS %10ld", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts);
    }
#endif
    if (!reEncoded || pass == 0) {
        // map input stream index to output stream index, drop packet if not used
        CheckInputFileChange();  // check if input file has changed
        int streamIndexIn = decoder->GetPacket()->stream_index;
        if ((streamIndexIn < 0) || (streamIndexIn >= MAXSTREAMS) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) { // prevent to overrun stream array
            esyslog("cEncoder::WritePacket():  out (%5d), stream %d: invalid input stream", decoder->GetPacketNumber(), streamIndexIn);
            return false;
        }
        int streamIndexOut = streamMap[streamIndexIn];
        if (streamIndexOut == -1) return true; // no target for this stream
        avpkt->stream_index = streamIndexOut;

        // correct pts after cut, only if not reencoded, in this case correct PTS/DTS is set from encoder
        if (avpkt->pts != AV_NOPTS_VALUE) avpkt->pts -= cutInfo.offset;
        if (avpkt->dts != AV_NOPTS_VALUE) avpkt->dts -= cutInfo.offset;
    }
#ifdef DEBUG_CUT_WRITE
    if ((avpkt->stream_index == DEBUG_CUT_WRITE) || (DEBUG_CUT_WRITE == -1)) {
        dsyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d, PTS %10ld, DTS %10ld, diff: PTS key %8ld, DTS %5ld, diff PTS/DTS % " PRId64 ", offset %5ld", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts, (outputKeyPacketPTSbefore[avpkt->stream_index] >= 0) ? avpkt->pts - outputKeyPacketPTSbefore[avpkt->stream_index] : -1, (streamInfo.lastOutDTS[avpkt->stream_index] >= 0) ? avpkt->dts - streamInfo.lastOutDTS[avpkt->stream_index] : -1, avpkt->pts - avpkt->dts, cutInfo.offset);
        if (avpkt->flags & AV_PKT_FLAG_KEY) outputKeyPacketPTSbefore[avpkt->stream_index] = avpkt->pts;
        LogSeparator();
    }
#endif
    // check monotonically increasing DTS
    if (avpkt->dts <= streamInfo.lastOutDTS[avpkt->stream_index]) {
        if (avctxOut->streams[avpkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            esyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d: DTS %" PRId64 " <= last DTS %" PRId64 ", drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->dts, streamInfo.lastOutDTS[avpkt->stream_index]);
        }
        else {
            dsyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d: DTS %" PRId64 " <= last DTS %" PRId64 ", drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->dts, streamInfo.lastOutDTS[avpkt->stream_index]);
        }
        return true;  // continue encoding
    }
    // check PTS > DTS
    if (avpkt->pts < avpkt->dts) {
        if (avctxOut->streams[avpkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            esyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d: PTS %" PRId64 " < DTS %" PRId64 ", diff %" PRId64 ", drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts, avpkt->pts - avpkt->dts);
        }
        else {
            dsyslog("cEncoder::WritePacket():  out (%5d), stream %d: flags %d: PTS %" PRId64 " < DTS %" PRId64 ", drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts);
        }
        return true;  // continue encoding
    }
    avpkt->pos = -1;   // byte position in stream unknown
    int rc = av_write_frame(avctxOut, avpkt);
    if (rc < 0) {
        esyslog("cEncoder::WritePacket():  out (%5d), stream %d: av_write_frame() failed, rc = %d: %s", decoder->GetPacketNumber(), avpkt->stream_index, rc, av_err2str(rc));
        return false;
    }
    // store current PTS/DTS state of output stream
    streamInfo.lastOutPTS[avpkt->stream_index] = avpkt->pts;
    streamInfo.lastOutDTS[avpkt->stream_index] = avpkt->dts;
    if (avctxOut->streams[avpkt->stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (avpkt->flags & AV_PKT_FLAG_KEY) streamInfo.maxPTSofGOP = avpkt->pts;
        else streamInfo.maxPTSofGOP = std::max(streamInfo.maxPTSofGOP, avpkt->pts);
    }
    return true;
}


bool cEncoder::SendFrameToEncoder(const int streamIndexOut, AVFrame *avFrame) {
    // correct PTS uncut to PTS after cut only with full decoding
    // with smart cut we correct PST/DTS in WritePacket()
    if (firstFrameToEncoder && !avFrame) {
        dsyslog("cEncoder::SendFrameToEncoder(): no frames encoded, flush not necessary");
        return true;
    }
    firstFrameToEncoder = false;
    if (avFrame && (pass > 0) && (avFrame->pts != AV_NOPTS_VALUE)) avFrame->pts -= cutInfo.offset;     // can be nullptr to flush buffer

#ifdef DEBUG_ENCODER
    if (avFrame) dsyslog("cEncoder::SendFrameToEncoder(): encode frame: PTS %" PRId64 ", DTS %" PRId64, avFrame->pts, avFrame->pkt_dts);
    else dsyslog("cEncoder::SendFrameToEncoder(): send flush");
#endif

    int rcSend = avcodec_send_frame(codecCtxArrayOut[streamIndexOut], avFrame);
    if (rcSend < 0) {
        switch (rcSend) {
        case AVERROR(EAGAIN):
            dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() EAGAIN", decoder->GetPacketNumber(), streamIndexOut);
            break;
        case AVERROR(EINVAL):
            dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() EINVAL", decoder->GetPacketNumber(), streamIndexOut);
            break;
        case AVERROR(EIO):
            dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() EIO", decoder->GetPacketNumber(), streamIndexOut);
            break;
        case AVERROR_EOF:  // expected return code after flash buffer
            dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() EOF", decoder->GetPacketNumber(), streamIndexOut);
            break;
        default:
            esyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() failed with rc = %d: %s", decoder->GetPacketNumber(), streamIndexOut, rcSend, av_err2str(rcSend));
            avcodec_flush_buffers(codecCtxArrayOut[streamIndexOut]);
            break;
        }
        return false;
    }
#ifdef DEBUG_ENCODER
    dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%d), output stream %d: avcodec_send_frame() successful", decoder->GetPacketNumber(), streamIndexOut);
#endif
    return true;
}


AVPacket *cEncoder::ReceivePacketFromEncoder(const int streamIndexOut) {
    AVCodecContext *avCodecCtx = codecCtxArrayOut[streamIndexOut];
    AVPacket *avpktOut = av_packet_alloc();
    ALLOC(sizeof(*avpktOut), "avpktOut");
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
    av_init_packet(avpktOut);
#endif
    avpktOut->size            = 0;
    avpktOut->data            = nullptr;
    avpktOut->side_data_elems = 0;
    avpktOut->side_data       = nullptr;
    avpktOut->buf             = nullptr;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
    avpktOut->opaque          = nullptr;
    avpktOut->opaque_ref      = nullptr;
#endif

    // receive packet from decoder
    int rcReceive = avcodec_receive_packet(avCodecCtx, avpktOut);
    switch (rcReceive) {
    case 0:  // got packet
#ifdef DEBUG_ENCODER
        if (avpktOut->stream_index == 0) {
            dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d), PTS %ld, DTS %ld: diff DTS %ld, avcodec_receive_packet() successful", decoder->GetPacketNumber(), avpktOut->pts, avpktOut->dts, avpktOut->dts - lastPacketOutDTS[avpktOut->stream_index]);
            lastPacketOutDTS[avpktOut->stream_index] = avpktOut->dts;
        }
#endif
        avpktOut->dts -= cutInfo.offsetDTSReceive;
        if (avpktOut->pts < avpktOut->dts) {  // should not happen, but saw with h264_nvenc
            cutInfo.offsetDTSReceive += cutInfo.videoPacketDuration;
            dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d), stream %d: invalid packet from encoder, PTS %" PRId64 "< DTS %" PRId64 ", new DTS offset %" PRId64, decoder->GetPacketNumber(), avpktOut->stream_index, avpktOut->pts, avpktOut->dts, cutInfo.offsetDTSReceive);
            FREE(sizeof(*avpktOut), "avpktOut");
            av_packet_free(&avpktOut);
            return nullptr;
        }
        return avpktOut;
        break;
    case AVERROR(EAGAIN):
#ifdef DEBUG_ENCODER
        dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d): avcodec_receive_packet() error EAGAIN", decoder->GetPacketNumber());
#endif
        break;
    case AVERROR(EINVAL):
        dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d): avcodec_receive_packet() error EINVAL", decoder->GetPacketNumber());
        break;
    case AVERROR_EOF:
        dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d): avcodec_receive_packet() end of file (AVERROR_EOF)", decoder->GetPacketNumber());
        break;
    default:
        esyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d): avcodec_receive_packet() failed with rc = %d: %s", decoder->GetPacketNumber(), rcReceive, av_err2str(rcReceive));
        break;
    }
    FREE(sizeof(*avpktOut), "avpktOut");
    av_packet_free(&avpktOut);
    return nullptr;
}


bool cEncoder::CloseFile() {
    int ret = 0;

#ifdef DEBUG_HW_DEVICE_CTX_REF
    if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): av_buffer_get_ref_count(hw_device_ctx) %d", av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
    // empty all encoder queue
    if (cutMode == CUT_MODE_FULL) {
        for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {
            dsyslog("cEncoder::CloseFile(): stream %d: flush encoder queue", streamIndex);
            if (codecCtxArrayOut[streamIndex]) {
                if (codecCtxArrayOut[streamIndex]->codec_type == AVMEDIA_TYPE_SUBTITLE) continue; // draining encoder queue of subtitle stream is not valid, no encoding used
#ifdef DEBUG_HW_DEVICE_CTX_REF
                if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d before flush: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
                // flush buffer
                SendFrameToEncoder(streamIndex, nullptr);
                // receive packet from decoder
                AVPacket *avpktOut = ReceivePacketFromEncoder(streamIndex);
                while (avpktOut) {
                    // write packet
                    if (!WritePacket(avpktOut, true)) {  // packet was re-encoded
                        esyslog("cEncoder::EncodeFrame(): WritePacket() failed");
                        return false;
                    }
                    FREE(sizeof(*avpktOut), "avpktOut");
                    av_packet_free(&avpktOut);
                    avpktOut = ReceivePacketFromEncoder(streamIndex);
                }
            }
            else {
                dsyslog("cEncoder::CloseFile(): output codec context of stream %d not valid", streamIndex);
                continue;
            }
#ifdef DEBUG_HW_DEVICE_CTX_REF
            if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d after flush: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
        }
    }
    ret = av_write_trailer(avctxOut);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not write trailer");
        return false;
    }
    ret = avio_closep(&avctxOut->pb);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not close file");
        return false;
    }

    // free output codec context
    for (unsigned int streamIndex = 0; streamIndex < avctxIn->nb_streams; streamIndex++) {  // we have alocaed codec context for all possible input streams
        if (codecCtxArrayOut[streamIndex]) {
#ifdef DEBUG_HW_DEVICE_CTX_REF
            if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d before flush: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            avcodec_flush_buffers(codecCtxArrayOut[streamIndex]);
#ifdef DEBUG_HW_DEVICE_CTX_REF
            if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d after flush: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            if (codecCtxArrayOut[streamIndex]->hw_frames_ctx) {
#ifdef DEBUG_HW_DEVICE_CTX_REF
                dsyslog("cEncoder::CloseFile(): stream %d before unref: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
                av_buffer_unref(&codecCtxArrayOut[streamIndex]->hw_frames_ctx);
#ifdef DEBUG_HW_DEVICE_CTX_REF
                dsyslog("cEncoder::CloseFile(): stream %d after  unref: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            }
            dsyslog("cEncoder::CloseFile(): call avcodec_free_context for stream %d", streamIndex);
#ifdef DEBUG_HW_DEVICE_CTX_REF
            if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d before avcodec_free_context: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
            FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
            avcodec_free_context(&codecCtxArrayOut[streamIndex]);
#ifdef DEBUG_HW_DEVICE_CTX_REF
            if (decoder->GetHardwareDeviceContext()) dsyslog("cEncoder::CloseFile(): stream %d after avcodec_free_context: av_buffer_get_ref_count(hw_device_ctx) %d", streamIndex, av_buffer_get_ref_count(decoder->GetHardwareDeviceContext()));
#endif
        }
    }

    // free sample context
    for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {  // we have alocaed codec context for all possible input streams
        if (swrArray[streamIndex]) {
            FREE(sizeof(swrArray[streamIndex]), "swr");  // only pointer size as marker
            swr_free(&swrArray[streamIndex]);
        }
    }

    // free output context
    if (pass == 1) {  // in other cases free in destructor
        dsyslog("cEncoder::CloseFile(): call avformat_free_context");
        FREE(sizeof(*avctxOut), "avctxOut");
        avformat_free_context(avctxOut);
    }
    return true;
}


