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



cEncoder::cEncoder(cDecoder *decoderParam, cIndex *indexParam, const char *recDirParam, const bool fullEncodeParam, const bool bestStreamParam, const bool ac3ReEncodeParam) {
    decoder     = decoderParam;
    index       = indexParam;
    recDir      = recDirParam;
    fullEncode  = fullEncodeParam;
    bestStream  = bestStreamParam;
    ac3ReEncode = ac3ReEncodeParam;
    dsyslog("cEncoder::cEncoder(): init encoder with %d threads, %s, ac3ReEncode %d", decoder->GetThreadCount(), (fullEncode) ? "full re-encode" : "copy packets", ac3ReEncode);
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
}


void cEncoder::Reset(const int passEncoder) {
    dsyslog("cEncoder::Reset(): pass %d", passEncoder);
    cutInfo = {0};
    pass    = passEncoder;
#ifdef DEBUG_PTS_DTS_CUT
    frameOut = 0;
#endif
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

    codecCtxArrayOut = static_cast<AVCodecContext **>(malloc(sizeof(AVCodecContext *) * avctxIn->nb_streams));
    ALLOC(sizeof(AVCodecContext *) * avctxIn->nb_streams, "codecCtxArrayOut");
    memset(codecCtxArrayOut, 0, sizeof(AVCodecContext *) * avctxIn->nb_streams);

    swrArray = static_cast<SwrContext **>(malloc(sizeof(SwrContext *) * avctxIn->nb_streams));
    ALLOC(sizeof(SwrContext *) * avctxIn->nb_streams, "swrArray");
    memset(swrArray, 0, sizeof(SwrContext *) * avctxIn->nb_streams);

    streamMap = static_cast<int *>(malloc(sizeof(int) * avctxIn->nb_streams));
    ALLOC(sizeof(int) * avctxIn->nb_streams, "streamMap");
    memset(streamMap, -1, sizeof(int) * avctxIn->nb_streams);

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

    // init all needed encoder
    for (int streamIndex = 0; streamIndex < static_cast<int>(avctxIn->nb_streams); streamIndex++) {
        if (!codecCtxArrayIn[streamIndex]) break;   // if we have no input codec we can not decode and encode this stream and all after
        if (fullEncode && bestStream) {
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
        if (streamMap[streamIndex] >= 0) {  // only init used streams
            if (decoder->IsAudioStream(streamIndex) && codecCtxArrayIn[streamIndex]->sample_rate == 0) {  // ignore mute audio stream
                dsyslog("cEncoder::OpenFile(): input stream %d: sample_rate not set, ignore mute audio stream", streamIndex);
                streamMap[streamIndex] = -1;
            }
            else {
                if (!InitEncoderCodec(streamIndex, streamMap[streamIndex])) {
                    esyslog("cEncoder::OpenFile(): InitEncoderCodec failed");
                    // cleanup memory
                    FREE(strlen(filename)+1, "filename");
                    free(filename);
                    FREE(sizeof(int) * avctxIn->nb_streams, "streamMap");
                    free(streamMap);
                    for (unsigned int i = 0; i < avctxIn->nb_streams; i++) {
                        if (codecCtxArrayOut[i]) {
                            avcodec_free_context(&codecCtxArrayOut[i]);
                            FREE(sizeof(*codecCtxArrayOut[i]), "codecCtxArrayOut[streamIndex]");
                        }
                    }
                    FREE(sizeof(AVCodecContext *) * avctxIn->nb_streams, "codecCtxArrayOut");
                    free(codecCtxArrayOut);
                    FREE(sizeof(SwrContext *) * avctxIn->nb_streams, "swrArray");
                    free(swrArray);
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
#elif LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    AVCodec *codec = avcodec_find_encoder(avctxIn->streams[streamIndexIn]->codecpar->codec_id);
#else
    AVCodec *codec = avcodec_find_encoder(avctxIn->streams[streamIndexIn]->codec->codec_id);
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
        dsyslog("cEncoder::ChangeEncoderCodec(): packet (%d), frame (%d), input stream %d, output stream %d: AC3 input stream found, re-initialize volume filter", decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexIn, streamIndexOut);
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


bool cEncoder::InitEncoderCodec(const unsigned int streamIndexIn, const unsigned int streamIndexOut) {
    if (!decoder) return false;
    if (!avctxIn) return false;
    if (!avctxOut) return false;
    if (streamIndexIn >= avctxIn->nb_streams) {
        dsyslog("cEncoder::InitEncoderCodec(): streamindex %d out of range", streamIndexIn);
        return false;
    }
    if (!codecCtxArrayIn) {
        esyslog("cEncoder::InitEncoderCodec(): no input codec arry set");
        return false;
    }

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100) // ffmpeg 4.5
    AVCodecID codec_id = avctxIn->streams[streamIndexIn]->codecpar->codec_id;
    const AVCodec *codec = avcodec_find_encoder(codec_id);
#elif LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    AVCodecID codec_id = avctxIn->streams[streamIndexIn]->codecpar->codec_id;
    AVCodec *codec = avcodec_find_encoder(codec_id);
#else
    AVCodecID codec_id = avctxIn->streams[streamIndexIn]->codec->codec_id;
    AVCodec *codec = avcodec_find_encoder(codec_id);
#endif
    if (!codec) {
        if (codec_id == 94215) { // libavcodec does not support Libzvbi DVB teletext encoder, encode without this stream
            dsyslog("cEncoder::InitEncoderCodec(): Libzvbi DVB teletext for stream %i codec id %i not supported, ignoring this stream", streamIndexIn, codec_id);
            return true;
        }
        dsyslog("cEncoder::InitEncoderCodec(): could not find encoder for intput stream %i codec id %i", streamIndexIn, codec_id);
        return false;
    }
    dsyslog("cEncoder::InitEncoderCodec(): using encoder id %d '%s' for output stream %i", codec_id, codec->long_name, streamIndexOut);

    const AVStream *out_stream = avformat_new_stream(avctxOut, codec);
    if (!out_stream) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed allocating output stream");
        return false;
    }

    codecCtxArrayOut[streamIndexOut] = avcodec_alloc_context3(codec);
    if (!codecCtxArrayOut[streamIndexOut]) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_alloc_context3 failed");
        return false;
    }
    ALLOC(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avcodec_parameters_to_context(codecCtxArrayOut[streamIndexOut], avctxOut->streams[streamIndexOut]->codecpar) < 0)
#else
    if (avcodec_copy_context(codecCtxArrayOut[streamIndexOut], avctxOut->streams[streamIndexOut]->codec) < 0)
#endif
    {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_parameters_to_context failed");
        return false;
    }

// set encoding parameter
    // video stream
    if (decoder->IsVideoStream(streamIndexIn)) {
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d avg framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->avg_frame_rate.num, avctxIn->streams[streamIndexIn]->avg_frame_rate.den);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d real framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->r_frame_rate.num, avctxIn->streams[streamIndexIn]->r_frame_rate.den);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d keyint_min %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->keyint_min);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        dsyslog("cEncoder::InitEncoderCodec(): video input format stream %d  bit_rate %" PRId64, streamIndexIn, avctxIn->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d bit_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d rc_max_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->rc_max_rate);
#else
        dsyslog("cEncoder::InitEncoderCodec(): video input format stream %d  bit_rate %d", streamIndexIn, avctxIn->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d bit_rate %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d rc_max_rate %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->rc_max_rate);
#endif
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d bit_rate_tolerance %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate_tolerance);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d global_quality %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->global_quality);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d sample_rate %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d gop_size %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->gop_size);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d level %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->level);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d aspect ratio %d:%d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.num, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.den);

        codecCtxArrayOut[streamIndexOut]->time_base.num = avctxIn->streams[streamIndexIn]->avg_frame_rate.den;  // time_base = 1 / framerate
        codecCtxArrayOut[streamIndexOut]->time_base.den = avctxIn->streams[streamIndexIn]->avg_frame_rate.num;
        codecCtxArrayOut[streamIndexOut]->framerate = avctxIn->streams[streamIndexIn]->avg_frame_rate;

//        codecCtxArrayOut[streamIndexOut]->pix_fmt             = AV_PIX_FMT_YUV420P;

        codecCtxArrayOut[streamIndexOut]->pix_fmt = codecCtxArrayIn[streamIndexIn]->pix_fmt;
        codecCtxArrayOut[streamIndexOut]->height = codecCtxArrayIn[streamIndexIn]->height;
        codecCtxArrayOut[streamIndexOut]->width = codecCtxArrayIn[streamIndexIn]->width;
        codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio;

        // calculate target mpeg2 video stream bit rate from recording
        int bit_rate = avctxIn->bit_rate; // overall recording bitrate
        for (unsigned int streamIndex = 0; streamIndex < avctxIn->nb_streams; streamIndex ++) {
            if (codecCtxArrayIn[streamIndex] && !decoder->IsVideoStream(streamIndex)) bit_rate -= codecCtxArrayIn[streamIndex]->bit_rate;  // audio streams bit rate
        }
        dsyslog("cEncoder::InitEncoderCodec(): target video bit rate %d", bit_rate);

        // parameter from origial recording
        if (codec->id == AV_CODEC_ID_H264) {
            // set pass
            if (pass == 1) codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS1;
            else {
                if (pass == 2) {
                    codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_PASS2;
                }
            }
            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "preset", "medium", 0);  // use h.264 defaults
            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "profile", "High", 0);

            codecCtxArrayOut[streamIndexOut]->bit_rate = bit_rate;  // adapt target bit rate

            av_opt_set_int(codecCtxArrayOut[streamIndexOut]->priv_data, "b_strategy", 0, 0); // keep fixed B frames in GOP, additional needed after force-crf
            codecCtxArrayOut[streamIndexOut]->level = codecCtxArrayIn[streamIndexIn]->level;
            codecCtxArrayOut[streamIndexOut]->gop_size = 32;
            codecCtxArrayOut[streamIndexOut]->keyint_min = 1;
            codecCtxArrayOut[streamIndexOut]->max_b_frames = 7;
            codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_CLOSED_GOP;
            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "x264opts", "force-cfr", 0);  // constand frame rate
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
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d pix_fmt %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->pix_fmt);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d keyint_min %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->keyint_min);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d max_b_frames %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->max_b_frames);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d bit_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d rc_max_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->rc_max_rate);
#else
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d bit_rate %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d rc_max_rate %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->rc_max_rate);
#endif
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d bit_rate_tolerance %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate_tolerance);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d level %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->level);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d framerate %d/%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->framerate.num, codecCtxArrayOut[streamIndexOut]->framerate.den);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d gop_size %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->gop_size);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d level %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->level);
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d aspect ratio %d:%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.num, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.den);
    }
    // audio stream
    else {
        if (decoder->IsAudioStream(streamIndexIn)) {
            dsyslog("cEncoder::InitEncoderCodec(): input codec sample rate %d, timebase %d/%d for stream %d", codecCtxArrayIn[streamIndexIn]->sample_rate, codecCtxArrayIn[streamIndexIn]->time_base.num, codecCtxArrayIn[streamIndexIn]->time_base.den, streamIndexIn);
            codecCtxArrayOut[streamIndexOut]->time_base.num = codecCtxArrayIn[streamIndexIn]->time_base.num;
            codecCtxArrayOut[streamIndexOut]->time_base.den = codecCtxArrayIn[streamIndexIn]->time_base.den;
            codecCtxArrayOut[streamIndexOut]->sample_rate   = codecCtxArrayIn[streamIndexIn]->sample_rate;

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            int rc = av_channel_layout_copy(&codecCtxArrayOut[streamIndexOut]->ch_layout, &codecCtxArrayIn[streamIndexIn]->ch_layout);
            if (rc != 0) {
                dsyslog("cEncoder::InitEncoderCodec(): av_channel_layout_copy for output stream %d from input stream %d  failed, rc = %d", streamIndexOut, streamIndexIn, rc);
                return false;
            }
#else
            codecCtxArrayOut[streamIndexOut]->channel_layout = codecCtxArrayIn[streamIndexIn]->channel_layout;
            codecCtxArrayOut[streamIndexOut]->channels       = codecCtxArrayIn[streamIndexIn]->channels;
#endif

            codecCtxArrayOut[streamIndexOut]->bit_rate = codecCtxArrayIn[streamIndexIn]->bit_rate;

            // audio sampe format
            dsyslog("cEncoder::InitEncoderCodec():            input audio codec sample format %d -> %s", codecCtxArrayIn[streamIndexIn]->sample_fmt, av_get_sample_fmt_name(codecCtxArrayIn[streamIndexIn]->sample_fmt));
            const enum AVSampleFormat *sampleFormats = codec->sample_fmts;
            while (*sampleFormats != AV_SAMPLE_FMT_NONE) {
                dsyslog("cEncoder::InitEncoderCodec(): supported output audio codec sample format %d -> %s", *sampleFormats, av_get_sample_fmt_name(*sampleFormats));
                sampleFormats++;
            }

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
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int in_channel_layout to %lu failed: %s", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, errTXT);
                }
                rcAudio = av_opt_set_int(swrArray[streamIndexOut], "out_channel_layout", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, 0);
                if (rcAudio < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rcAudio, errTXT, sizeof(errTXT));
                    esyslog("cEncoder::InitEncoderCodec(): av_opt_set_int out_channel_layout to %lu failed: %s", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, errTXT);
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
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
            dsyslog("cEncoder::InitEncoderCodec(): audio output codec parameter for stream %d: bit_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
#else
            dsyslog("cEncoder::InitEncoderCodec(): audio output codec parameter for stream %d: bit_rate %d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
#endif
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
    dsyslog("cEncoder::InitEncoderCodec():       output stream %d timebase %d/%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->time_base.num, codecCtxArrayOut[streamIndexOut]->time_base.den);
    if ((codecCtxArrayOut[streamIndexOut]->time_base.num == 0) && (!decoder->IsSubtitleStream(streamIndexIn))) {
        dsyslog("cEncoder::InitEncoderCodec(): output stream %d timebase %d/%d not valid", streamIndexOut, codecCtxArrayOut[streamIndexOut]->time_base.num, codecCtxArrayOut[streamIndexOut]->time_base.den);
        return false;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    int ret = avcodec_parameters_copy(avctxOut->streams[streamIndexOut]->codecpar, avctxIn->streams[streamIndexIn]->codecpar);
#else
    int ret = avcodec_copy_context(avctxOut->streams[streamIndexOut]->codec, avctxIn->streams[streamIndexIn]->codec);
#endif
    if (ret < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed to copy codecpar context from input to output stream");
        return false;
    }

    codecCtxArrayOut[streamIndexOut]->thread_count = decoder->GetThreadCount();
    if (avcodec_open2(codecCtxArrayOut[streamIndexOut], codec, nullptr) < 0) {
        esyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %d failed", streamIndexOut);
        dsyslog("cEncoder::InitEncoderCodec(): call avcodec_free_context for stream %d", streamIndexOut);
        FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
        codecCtxArrayOut[streamIndexOut] = nullptr;
    }
    else dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i successful", streamIndexOut);

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

bool cEncoder::CutOut(int startPos, int stopPos) {
    LogSeparator();
    dsyslog("cEncoder::CutOut(): packet (%d), frame (%d): %s from start position (%d) to stop position (%d) in pass: %d", decoder->GetPacketNumber(), decoder->GetFrameNumber(), (fullEncode) ? "full encode" : "copy packets", startPos, stopPos, pass);

    // cut with full encoding
    if (fullEncode) {
        // seek to start position
        if (!decoder->SeekToFrame(startPos)) {
            esyslog("cMarkAdStandalone::MarkadCut(): seek to start mark (%d) failed", startPos);
            return false;
        }
        // check if we have a valid frame to set for start position
        AVFrame *avFrame = decoder->GetFrame();  // this should be a video frame, because we seek to video packets
        while (!decoder->IsVideoFrame() || (avFrame->pts == AV_NOPTS_VALUE) || (avFrame->pkt_dts == AV_NOPTS_VALUE)) {
            if (abortNow) return false;
            esyslog("cEncoder::CutOut(): packet (%d), frame (%d), stream %d: frame not valid for start position", decoder->GetPacketNumber(), decoder->GetFrameNumber(), decoder->GetPacket()->stream_index);
            if (!decoder->ReadNextPacket()) return false;
            if (decoder->IsVideoPacket()) decoder->DecodePacket(); // decode packet, no error break, maybe we only need more frames to decode (e.g. interlaced video)
        }
        // get PTS/DTS from start position
        cutInfo.startPosPTS = avFrame->pts;
        cutInfo.startPosDTS = avFrame->pkt_dts;
        // current start pos - last stop pos -> length of ad
        // use dts to prevent non monotonically increasing dts
        if (cutInfo.stopPosDTS > 0) cutInfo.offset += (cutInfo.startPosDTS - cutInfo.stopPosDTS);
        dsyslog("cEncoder::CutOut(): start cut from frame (%6d) PTS %10ld DTS %10ld to frame (%d), offset %ld", startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.offset);
        avctxIn = decoder->GetAVFormatContext();  // avctx changes at each input file

        // read all packets
        while (decoder->GetFrameNumber() <= stopPos) {
            if (abortNow) return false;

#ifdef DEBUG_CUT  // first picures after start mark after
            if (decoder->IsVideoFrame() && ((abs(decoder->GetFrameNumber() - startPos) <= DEBUG_CUT) || (abs(decoder->GetFrameNumber() - stopPos) <= DEBUG_CUT))) {
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d_CUT.pgm", recDir, decoder->GetFrameNumber()) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                }
            }
#endif

            // write current packet
            AVPacket *avpktIn  = decoder->GetPacket();
            AVPacket *avpktOut = nullptr;
            if ((avpktIn->pts >= cutInfo.startPosPTS) && (avpktIn->dts >= cutInfo.startPosDTS)) {
                bool reEncoded = false;
                // re-encode if needed
                if (decoder->IsVideoPacket()) {  // always decode video stream
                    avpktOut = EncodeVideoFrame(decoder->GetFrame()); // encode video frame
                    reEncoded = true;
                    if (!avpktOut) dsyslog("cEncoder::CutOut(): packet (%d), frame (%d): EncodeVideoFrame() failed", decoder->GetPacketNumber(), decoder->GetFrameNumber());
                }
                if (ac3ReEncode && (pass == 2) && decoder->IsAudioAC3Packet()) {  // only re-encode AC3 if volume change is requested
                    dsyslog("xxx 1 %d", avpktIn->stream_index);
                    avpktOut = EncodeAC3Frame(decoder->GetFrame());               // change volume and encode AC3 frame
                    reEncoded = true;
                    if (!avpktOut) dsyslog("cEncoder::CutOut(): packet (%d), frame (%d): EncodeAC3Frame() failed", decoder->GetPacketNumber(), decoder->GetFrameNumber());
                }

                // no re-encode need, use input packet
                if (!reEncoded) {
                    avpktOut  = avpktIn;   // no re-encode, direct write input packet
                    reEncoded = false;
                }
                // write packet
                if (avpktOut && !WritePacket(avpktOut, reEncoded)) {
                    esyslog("cEncoder::CutOut(): WritePacket() failed");
                    return false;
                }
                // free encoded packet
                if (avpktOut && reEncoded) {  // no re-encdoe no free, it is owned by decoder
                    FREE(sizeof(*avpktOut), "avpktOut");
                    av_packet_free(&avpktOut);
                }
            }
            else dsyslog("cEncoder::CutOut(): packet (%d), stream %d, PTS %ld, DTS %ld: drop packet before start PTS %ld, DTS %ld", decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPosPTS, cutInfo.startPosDTS);
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
            avctxIn = decoder->GetAVFormatContext();  // avctx changes at each input file
        }
        // get PTS/DTS from end position
        avFrame = decoder->GetFrame();  // this must be first video packet after cut because we only increase frame counter on video frames
        cutInfo.stopPosPTS = avFrame->pts;
        cutInfo.stopPosDTS = avFrame->pkt_dts;
        dsyslog("cEncoder::CutOut(): end cut from i-frame (%6d) PTS %10ld DTS %10ld to i-frame (%6d) PTS %10ld DTS %10ld, offset %10ld", startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.stopPosPTS, cutInfo.stopPosDTS, cutInfo.offset);
    }
    // cut without full decoding, only copy all packets
    else {
        // without decoding/encoding adjust cut position to i-frame
        startPos = index->GetIFrameAfter(startPos);
        if (startPos < 0) {
            esyslog("cEncoder::CutOut(): get i-frame before failed");
            return false;
        }
        stopPos = index->GetIFrameBefore(stopPos);
        if (stopPos < 0) {
            esyslog("cEncoder::CutOut():: get i-frame after failed");
            return false;
        }
        dsyslog("cEncoder::CutOut(): adjust cut position from i-frame (%d) to i-frame (%d)", startPos, stopPos);

        // if we do not encode, we do not decode and so we have no valid decoder frame number, use packet number
        if (decoder->GetPacketNumber() > startPos) {
            esyslog("cEncoder::CutOut(): invalid decoder read position");
        }
        // seek to start position
        if (!decoder->SeekToPacket(startPos)) {  // ReadNextPacket() will read startPos
            esyslog("cEncoder::CutOut(): seek to packet (%d) failed", startPos);
            return false;
        }
        // check if we have a valid packet to set for start position
        AVPacket *avpkt = decoder->GetPacket();  // this should be a video packet, because we seek to video packets
        while (!decoder->IsVideoIPacket() || (avpkt->pts == AV_NOPTS_VALUE) || (avpkt->dts == AV_NOPTS_VALUE)) {
            if (abortNow) return false;
            esyslog("cEncoder::CutOut(): packet (%d), stream %d: packet not valid for start position", decoder->GetPacketNumber(), avpkt->stream_index);
            if (!decoder->ReadNextPacket()) return false;
            avpkt = decoder->GetPacket();  // this must be a video packet, because we seek to video packets
        }

        // get PTS/DTS from start position
        cutInfo.startPosPTS = avpkt->pts;
        cutInfo.startPosDTS = avpkt->dts;
        // current start pos - last stop pos -> length of ad
        // use dts to prevent non monotonically increasing dts
        if (cutInfo.stopPosDTS > 0) cutInfo.offset += (cutInfo.startPosDTS - cutInfo.stopPosDTS);
        dsyslog("cEncoder::CutOut(): start cut from i-frame (%6d) PTS %10ld DTS %10ld to i-frame (%d), offset %ld", startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.offset);
        avctxIn = decoder->GetAVFormatContext();  // avctx changes at each input file

        // copy all packets from startPos to endPos
        while (decoder->GetPacketNumber() <= stopPos) {
            if (abortNow) return false;

#ifdef DEBUG_CUT  // first pictures after start mark after
            decoder->DecodePacket();   // no decoding from encoder, do it here
            if (decoder->IsVideoFrame() && ((abs(decoder->GetFrameNumber() - startPos) <= DEBUG_CUT) || (abs(decoder->GetFrameNumber() - stopPos) <= DEBUG_CUT))) {
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d_CUT.pgm", recDir, decoder->GetFrameNumber()) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    SaveVideoPlane0(fileName, decoder->GetVideoPicture());
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                }
            }
#endif

            // write current packet
            AVPacket *avpktIn = decoder->GetPacket();
            if ((avpktIn->pts >= cutInfo.startPosPTS) && (avpktIn->dts >= cutInfo.startPosDTS)) {
                if (!WritePacket(avpktIn, false)) {  // no re-encode done
                    esyslog("cEncoder::CutOut(): WritePacket() failed");
                    return false;
                }
            }
            else dsyslog("cEncoder::CutOut(): packet (%d), stream %d, PTS %ld, DTS %ld: drop packet before start PTS %ld, DTS %ld", decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPosPTS, cutInfo.startPosDTS);
            // read next packet
            if (!decoder->ReadNextPacket()) return false;
            avctxIn = decoder->GetAVFormatContext();  // avctx changes at each input file
        }

        // get PTS/DTS from end position
        avpkt =  decoder->GetPacket();  // this must be first video packet after cut because we only increase packet counter on video packets
        cutInfo.stopPosPTS = avpkt->pts;
        cutInfo.stopPosDTS = avpkt->dts;
        dsyslog("cEncoder::CutOut(): end cut from i-frame (%6d) PTS %10ld DTS %10ld to i-frame (%6d) PTS %10ld DTS %10ld, offset %10ld", startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.stopPosPTS, cutInfo.stopPosDTS, cutInfo.offset);
    }
    LogSeparator();
    return true;
}


AVPacket *cEncoder::EncodeVideoFrame(AVFrame *avFrame) {
#ifdef DEBUG_PTS_DTS_CUT
    if (pass == 2) {
        dsyslog("cEncoder::EncodeVideoFrame(): frame (%5d) stream %d in:  PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", decoderFrameNumber, streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - inputFramePTSbefore[streamIndexIn], EncoderStatus.pts_dts_CutOffset);
        inputFramePTSbefore[streamIndexIn] = avFrame->pts;
    }
#endif

    // map input stream index to output stream index, should always be 0->0 for video stream
    int streamIndexIn = decoder->GetPacket()->stream_index;
    if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) return nullptr; // prevent to overrun stream array
    int streamIndexOut = streamMap[streamIndexIn];
    if (streamIndexOut == -1) return nullptr; // no target for this stream

    codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = avFrame->sample_aspect_ratio; // set encoder pixel aspect ratio to decoded frames aspect ratio
    avFrame->pts -= cutInfo.offset; // correct PTS uncut to PTS after cut

#ifdef DEBUG_PTS_DTS_CUT
    if (pass == 2) {
        dsyslog("cEncoder::EncodeVideoFrame(): out frame  (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - outputFramePTSbefore[streamIndexOut], EncoderStatus.pts_dts_CutOffset);
        outputFramePTSbefore[streamIndexOut] = avFrame->pts;
    }
#endif

    //encode frame
    AVPacket *avpktOut = av_packet_alloc();
    ALLOC(sizeof(*avpktOut), "avpktOut");
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
    av_init_packet(avpktOut);
#endif
    // init avpktOut
    avpktOut->size            = 0;
    avpktOut->data            = nullptr;
    avpktOut->side_data_elems = 0;
    avpktOut->side_data       = nullptr;
    avpktOut->buf             = nullptr;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
    avpktOut->opaque          = nullptr;
    avpktOut->opaque_ref      = nullptr;
#endif

    avFrame->pict_type = AV_PICTURE_TYPE_NONE;     // encoder decides picture type
    if (codecCtxArrayOut[streamIndexOut]) {
        if (!EncodeFrame(codecCtxArrayOut[streamIndexOut], avFrame, avpktOut)) {
            FREE(sizeof(*avpktOut), "avpktOut");
            av_packet_free(&avpktOut);
            if (stateEAGAIN) {
                //                    dsyslog("cEncoder::EncodeVideoFrame(): encoder for output stream %d at frame %d need more frames", streamIndexOut, decoderFrameNumber);
                return nullptr;
            }
            else {
                dsyslog("cEncoder::EncodeVideoFrame(): frame(%d), stream %d: encoder failed", decoder->GetFrameNumber(), streamIndexOut);
                return nullptr;
            }
        }
    }
    else {
        dsyslog("cEncoder::EncodeVideoFrame(): encoding of stream %d not supported", streamIndexIn);
        FREE(sizeof(*avpktOut), "avpktOut");
        av_packet_free(&avpktOut);
        return nullptr;
    }
    // store stats for second pass
    if ((pass == 1) && codecCtxArrayOut[streamIndexOut]->stats_out) {
        long int strLength = strlen(codecCtxArrayOut[streamIndexOut]->stats_out);
        if (!stats_in.data) {
            stats_in.data = static_cast<char *>(malloc(strLength + 1));  // take care of terminating 0
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
                return nullptr;
            }
            stats_in.data = stats_in_tmp;
            stats_in.size = strLength + oldLength + 1;
            strcat(stats_in.data, codecCtxArrayOut[streamIndexOut]->stats_out);
            stats_in.size = strLength + oldLength + 1;
            ALLOC(stats_in.size, "stats_in");
        }
    }
    avpktOut->stream_index = streamIndexOut;
#ifdef DEBUG_PTS_DTS_CUT
    if (pass == 2) {
        dsyslog("cEncoder::EncodeVideoFrame(): out packet (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, avpktOut.stream_index, avpktOut.pts, avpktOut.dts, avpktOut.pts - outputPacketPTSbefore[streamIndexOut], EncoderStatus.pts_dts_CutOffset);
        if (decoder->IsVideoPacket()) frameOut++;
        outputPacketPTSbefore[streamIndexOut] = avpktOut.pts;
    }
#endif
    return avpktOut;
}


int cEncoder::GetAC3ChannelCount(const int streamIndex) {
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    return codecCtxArrayOut[streamIndex]->ch_layout.nb_channels;
#else
    return codecCtxArrayOut[streamIndex]->channels;
#endif
}



AVPacket *cEncoder::EncodeAC3Frame(AVFrame *avFrame) {
    // map input stream index to output stream index
    int streamIndexIn = decoder->GetPacket()->stream_index;
    if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) return nullptr; // prevent to overrun stream array
    int streamIndexOut = streamMap[streamIndexIn];
    if (streamIndexOut == -1) return nullptr; // no target for this stream


    // check encoder, it can be wrong if recording is damaged
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (decoder->IsAudioAC3Packet() && avctxOut->streams[streamIndexOut]->codecpar->codec_id != AV_CODEC_ID_AC3)
#else
    if (decoder->IsAudioAC3Packet() && avctxOut->streams[streamIndexOut]->codec->codec_id != AV_CODEC_ID_AC3)
#endif
    {
        esyslog("cEncoder:EncodeAC3Frame(): packet (%d), frame (%d), stream %d: invalid encoder for AC3 packet", decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexOut);
        return nullptr;
    }

    // Check if the AC3 stream has changed channels
    if (decoder->GetAC3ChannelCount(streamIndexIn) != GetAC3ChannelCount(streamIndexOut)) {
        dsyslog("cEncoder:EncodeAC3Frame(): packet (%d), frame (%d), stream %d: channel count input stream %d different to output stream %d",decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexIn, decoder->GetAC3ChannelCount(streamIndexIn), GetAC3ChannelCount(streamIndexOut));
        if(!ChangeEncoderCodec(streamIndexIn, streamIndexOut, codecCtxArrayIn[streamIndexIn])) {
            esyslog("encoder initialization failed for output stream index %d, source is stream index %d", streamIndexOut, streamIndexIn);
            return nullptr;
        }
    }

    // use filter to adapt AC3 volume
    if (!volumeFilterAC3[streamIndexOut]->SendFrame(avFrame)) {
        esyslog("cEncoder::EncodeAC3Frame(): packet (%d), frame (%d), input %d, output %d: cAC3VolumeFilter::SendFrame() failed", decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexIn, streamIndexOut);
        return nullptr;
    }
    if (!volumeFilterAC3[streamIndexOut]->GetFrame(avFrame)) {
        esyslog("cEncoder:EncodeAC3Frame(): cAC3VolumeFilter GetFrame failed");
        return nullptr;
    }

    AVPacket *avpktOut = av_packet_alloc();
    ALLOC(sizeof(*avpktOut), "avpktOut");
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
    av_init_packet(avpktOut);
#endif
    // init avpktOut
    avpktOut->size            = 0;
    avpktOut->data            = nullptr;
    avpktOut->side_data_elems = 0;
    avpktOut->side_data       = nullptr;
    avpktOut->buf             = nullptr;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
    avpktOut->opaque          = nullptr;
    avpktOut->opaque_ref      = nullptr;
#endif
    // correct PTS uncut to PTS after cut
    avFrame->pts -= cutInfo.offset;

    if (codecCtxArrayOut[streamIndexOut]) {
        if (!EncodeFrame(codecCtxArrayOut[streamIndexOut], avFrame, avpktOut)) {
            av_packet_unref(avpktOut);
            dsyslog("cEncoder::EncodeAC3Frame(): packet (%d), frame (%d) output stream %d: encoder failed", decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexOut);
            return nullptr;
        }
    }
    else {
        dsyslog("cEncoder::EncodeAC3Frame(): encoding of stream %d not supported", streamIndexIn);
        av_packet_unref(avpktOut);
        return nullptr;
    }
    avpktOut->stream_index = streamIndexOut;
    dsyslog("xxxx %d", avpktOut->stream_index);
    return avpktOut;
}


bool cEncoder::WritePacket(AVPacket *avpkt, const bool reEncoded) {
    if (!avctxOut ) {
        esyslog("cEncoder::WritePacket(): got no AVFormatContext from output file");
        return false;
    }
    if (!reEncoded) {
        // map input stream index to output stream index, drop packet if not used
        int streamIndexIn = decoder->GetPacket()->stream_index;
        if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) { // prevent to overrun stream array
            esyslog("cEncoder::WritePacket(): packet (%d), frame (%d): invalid input stream index %d", decoder->GetPacketNumber(), decoder->GetFrameNumber(), streamIndexIn);
            return false;
        }
        int streamIndexOut = streamMap[streamIndexIn];
        if (streamIndexOut == -1) return true; // no target for this stream
        avpkt->stream_index = streamIndexOut;

        // correct pts after cut, only if not reencoded, in this case correct PTS/DTS is set from encoder
        avpkt->pts -= cutInfo.offset;
        avpkt->dts -= cutInfo.offset;
    }
    avpkt->pos = -1;   // byte position in stream unknown
    int rc = av_write_frame(avctxOut, avpkt);
    if (rc < 0) {
        esyslog("cEncoder::WritePacket(): packet (%d), frame (%d), stream %d: av_write_frame failed, rc = %d: %s", decoder->GetPacketNumber(), decoder->GetFrameNumber(), avpkt->stream_index, rc, av_err2str(rc));
        return false;
    }
    return true;
}


bool cEncoder::EncodeFrame(AVCodecContext *avCodecCtx, AVFrame *avFrame, AVPacket *avpkt) {
    if (!decoder) return false;
    if (!avCodecCtx) {
        dsyslog("cEncoder::EncodeFrame(): codec context not set");
        return false;
    }
    if (!avpkt) return false;

    int decoderFrameNumber = 0;
    if (decoder->GetFullDecode()) decoderFrameNumber = decoder->GetFrameNumber();
    else decoderFrameNumber = decoder->GetPacketNumber();  // decoder has no framenumber without decoding

    stateEAGAIN = false;

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avFrame) {
        int rcSend = avcodec_send_frame(avCodecCtx, avFrame);
        if (rcSend < 0) {
            switch (rcSend) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_send_frame() error EAGAIN", decoderFrameNumber);
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_send_frame() error EINVAL", decoderFrameNumber);
                break;
            default:
                esyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_send_frame() failed with rc = %d: %s", decoderFrameNumber, rcSend, av_err2str(rcSend));
                avcodec_flush_buffers(avCodecCtx);
                break;
            }
            return false;  // ignore send errors if we only empty encoder
        }
    }
    int rcReceive = avcodec_receive_packet(avCodecCtx, avpkt);
    if (rcReceive < 0) {
        switch (rcReceive) {
        case AVERROR(EAGAIN):
//                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_packet() error EAGAIN at frame %d", decoder->GetFrameNumber());
            stateEAGAIN=true;
            break;
        case AVERROR(EINVAL):
            dsyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_receive_packet() error EINVAL", decoderFrameNumber);
            break;
        case AVERROR_EOF:
            dsyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_receive_packet() end of file (AVERROR_EOF)", decoderFrameNumber);
            break;
        default:
            esyslog("cEncoder::EncodeFrame(): frame (%d): avcodec_receive_packet() failed with rc = %d: %s", decoder->GetFrameNumber(), rcReceive, av_err2str(rcReceive));
            break;
        }
        return false;
    }
#else
    int frame_ready = 0;
    if (decoder->IsAudioPacket()) {
        int rcEncode = avcodec_encode_audio2(avCodecCtx, avpkt, avFrame, &frame_ready);
        if (rcEncode < 0) {
            dsyslog("cEncoder::EncodeFrame(): frame (%d), stream %d: avcodec_encode_audio2 %d failed with rc = %d: %s", decoder->GetFrameNumber(), avpkt->stream_index, rcEncode,  av_err2str(rcReceive));
            return false;
        }
    }
    else {
        dsyslog("cEncoder::EncodeFrame(): packet type of stream %d not supported", avpkt->stream_index);
        return false;
    }
#endif
    return true;
}


bool cEncoder::CloseFile() {
    int ret = 0;

    // empty all encoder queue
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {
        if (codecCtxArrayOut[streamIndex]) {
            if (codecCtxArrayOut[streamIndex]->codec_type == AVMEDIA_TYPE_SUBTITLE) continue; // draining encoder queue of subtitle stream is not valid, no encoding used
            avcodec_send_frame(codecCtxArrayOut[streamIndex], nullptr);  // prevent crash if we have no valid encoder codec context
        }
        else {
            dsyslog("cEncoder::CloseFile(): output codec context of stream %d not valid", streamIndex);
            break;
        }
        AVPacket avpktOut;

#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
        av_init_packet(&avpktOut);
#endif

        // init avpktOut
        avpktOut.size            = 0;
        avpktOut.data            = nullptr;
        avpktOut.side_data_elems = 0;
        avpktOut.side_data       = nullptr;
        avpktOut.buf             = nullptr;

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
        avpktOut.opaque          = nullptr;
        avpktOut.opaque_ref      = nullptr;
#endif

        while(EncodeFrame(codecCtxArrayOut[streamIndex], nullptr, &avpktOut)) {
            avpktOut.stream_index = streamIndex;
            avpktOut.pos = -1;   // byte position in stream unknown
            // write packet
            av_write_frame(avctxOut, &avpktOut);
            // free memory
            av_packet_unref(&avpktOut);
        }
    }
#endif

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
            avcodec_flush_buffers(codecCtxArrayOut[streamIndex]);
            dsyslog("cEncoder::CloseFile(): call avcodec_free_context for stream %d", streamIndex);
            FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
            avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        }
    }
    FREE(sizeof(AVCodecContext *) *avctxIn->nb_streams, "codecCtxArrayOut");
    free(codecCtxArrayOut);

    // free cut status
    FREE(sizeof(int) * avctxIn->nb_streams, "streamMap");
    free(streamMap);

    // free sample context
    for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {  // we have alocaed codec context for all possible input streams
        if (swrArray[streamIndex]) {
            FREE(sizeof(swrArray[streamIndex]), "swr");  // only pointer size as marker
            swr_free(&swrArray[streamIndex]);
        }
    }
    FREE(sizeof(SwrContext *) * avctxIn->nb_streams, "swrArray");
    free(swrArray);

    // free output context
    if (pass == 1) {  // in other cases free in destructor
        dsyslog("cEncoder::CloseFile(): call avformat_free_context");
        FREE(sizeof(*avctxOut), "avctxOut");
        avformat_free_context(avctxOut);
    }
    return true;
}


#ifdef DEBUG_ENCODER
void cEncoder::SaveFrame(const int frame, AVFrame *avFrame) {
    if (frame > 20) return;
    dsyslog("cEncoder::SaveFrame(): called");
    char szFilename[1024];

    for (int plane = 0; plane < PLANES; plane++) {
        int width =  avFrame->width;
        int height = avFrame->height;
        if (plane > 0) {
            height /= 2;
            width /= 2;
        }
        // set path and file name
        sprintf(szFilename, "/tmp/frame%06dfull_P%d.pgm", frame, plane);
        // Open file
        FILE *pFile = fopen(szFilename, "wb");
        if (pFile == nullptr) {
            dsyslog("cMarkAdStandalone::SaveFrame(): open file %s failed", szFilename);
            return;
        }
        // Write header
        fprintf(pFile, "P5\n%d %d\n255\n", width, height);
        // Write pixel data
        for (int line = 0; line < height; line++) {
            if (fwrite(&avFrame->data[plane][line * avFrame->linesize[plane]], 1, width, pFile)) {};
        }
        // Close file
        fclose(pFile);
    }
    dsyslog("cEncoder::SaveFrame(): end");
}
#endif
