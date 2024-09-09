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
    for (unsigned int i = 0; i < MAXSTREAMS; i++) {
        pts[i] = 0;
        dts[i] = 0;
    }
#ifdef DEBUG_PTS_DTS_CUT
    frameOut = 0;
#endif
}


void cEncoder::CheckInputFileChange() {
    if (decoder->GetFileNumber() > fileNumber) {
        dsyslog("cEncoder::CheckInputFileChange(): decoder packet (%d): input file changed from %d to %d", decoder->GetPacketNumber(), fileNumber, decoder->GetFileNumber());
        avctxIn = decoder->GetAVFormatContext();
        codecCtxArrayIn = decoder->GetAVCodecContext();
        fileNumber = decoder->GetFileNumber();
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
                        initCodec = InitEncoderCodec(streamIndex, streamMap[streamIndex]);    // init video codec with hardware encoder
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
                if (!initCodec) initCodec = InitEncoderCodec(streamIndex, streamMap[streamIndex]);   // init codec for fallback to software decoder and non video streams
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
    // mpeg2video -> mpeg2_vaapi
    // h264       -> h264_vaapi
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
        if (asprintf(&encoderName,"h264_%s", hwaccelName) == -1) {
            dsyslog("cEncoder::GetEncoderName(): failed to allocate string, out of memory");
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

    const AVStream *out_stream = avformat_new_stream(avctxOut, codec);   // parameter codec unused
    if (!out_stream) {
        dsyslog("cEncoder::InitEncoderCodec(): failed allocating output stream");
        return false;
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
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: pix_fmt %s", streamIndexIn, av_get_pix_fmt_name(codecCtxArrayIn[streamIndexIn]->pix_fmt));
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: avg framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->avg_frame_rate.num, avctxIn->streams[streamIndexIn]->avg_frame_rate.den);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: real framerate %d/%d", streamIndexIn, avctxIn->streams[streamIndexIn]->r_frame_rate.num, avctxIn->streams[streamIndexIn]->r_frame_rate.den);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: keyint_min %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->keyint_min);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: bit_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: rc_max_rate %" PRId64, streamIndexIn, codecCtxArrayIn[streamIndexIn]->rc_max_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: bit_rate_tolerance %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->bit_rate_tolerance);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: global_quality %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->global_quality);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: sample_rate %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_rate);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: gop_size %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->gop_size);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: level %d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->level);
        dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: aspect ratio %d:%d", streamIndexIn, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.num, codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio.den);
        dsyslog("cEncoder::InitEncoderCodec(): video input format context : bit_rate %" PRId64, avctxIn->bit_rate);

        // use hwaccel for video stream if decoder use it too
        if (useHWaccel) {
            dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: use hwaccel: %s", streamIndexOut, av_hwdevice_get_type_name(hwDeviceType));
            // store stoftware fixel format in case of we need it for fallback to software decoder
            software_pix_fmt = codecCtxArrayIn[streamIndexIn]->pix_fmt;
            // codecCtxArrayOut[streamIndexOut]->hw_device_ctx = av_buffer_ref(decoder->GetHardwareDeviceContext());
            // we need to ref hw_frames_ctx of decoder to initialize encoder's codec
            // only after we get a decoded frame, can we obtain its hw_frames_ctx
            decoder->DecodeNextFrame(false);  // decode one video frame to get hw_frames_ctx, also changes pix_fmt to hardware pixel format
            dsyslog("cEncoder::InitEncoderCodec(): video input codec stream %d: pix_fmt %s", streamIndexIn, av_get_pix_fmt_name(codecCtxArrayIn[streamIndexIn]->pix_fmt));
            codecCtxArrayOut[streamIndexOut]->hw_frames_ctx = av_buffer_ref(codecCtxArrayIn[streamIndexIn]->hw_frames_ctx);  // link hardware frame context to encoder
            if (!codecCtxArrayOut[streamIndexOut]->hw_frames_ctx) {
                esyslog("cEncoder::InitEncoderCodec(): av_buffer_ref() failed");
                return false;
            }
        }
        fileNumber = decoder->GetFileNumber();
        // use pixel software pixel format from decoder for fallback to software decoder
        if ((software_pix_fmt != AV_PIX_FMT_NONE) && !useHWaccel) codecCtxArrayOut[streamIndexOut]->pix_fmt = software_pix_fmt;
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
        codecCtxArrayOut[streamIndexOut]->time_base.num       = avctxIn->streams[streamIndexIn]->avg_frame_rate.den;  // time_base = 1 / framerate
        codecCtxArrayOut[streamIndexOut]->time_base.den       = avctxIn->streams[streamIndexIn]->avg_frame_rate.num;
        codecCtxArrayOut[streamIndexOut]->framerate           = avctxIn->streams[streamIndexIn]->avg_frame_rate;

        codecCtxArrayOut[streamIndexOut]->height              = codecCtxArrayIn[streamIndexIn]->height;
        codecCtxArrayOut[streamIndexOut]->width               = codecCtxArrayIn[streamIndexIn]->width;

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
            av_opt_set_int(codecCtxArrayOut[streamIndexOut]->priv_data, "b_strategy", 0, 0); // keep fixed B frames in GOP, additional needed after force-crf
            av_opt_set(codecCtxArrayOut[streamIndexOut]->priv_data, "x264opts", "force-cfr", 0);  // constand frame rate
            codecCtxArrayOut[streamIndexOut]->flags |= AV_CODEC_FLAG_CLOSED_GOP;

            codecCtxArrayOut[streamIndexOut]->bit_rate     = bit_rate;  // adapt target bit rate
            codecCtxArrayOut[streamIndexOut]->level        = codecCtxArrayIn[streamIndexIn]->level;
            codecCtxArrayOut[streamIndexOut]->gop_size     = 32;
            codecCtxArrayOut[streamIndexOut]->keyint_min   = 1;
            codecCtxArrayOut[streamIndexOut]->max_b_frames = 7;
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
        dsyslog("cEncoder::InitEncoderCodec(): video output stream %d: aspect ratio %d:%d", streamIndexOut, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.num, codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio.den);
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
            dsyslog("cEncoder::InitEncoderCodec(): audio output codec parameter for stream %d: bit_rate %" PRId64, streamIndexOut, codecCtxArrayOut[streamIndexOut]->bit_rate);
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
    LogSeparator();
    dsyslog("cEncoder::CutOut(): packet (%d): %s from start position (%d) to stop position (%d) in pass: %d", decoder->GetPacketNumber(), (fullEncode) ? "full encode" : "copy packets", startMark->position, stopMark->position, pass);

    // cut with full encoding
    if (fullEncode) {
        int startPos = startMark->position;
        int stopPos  = stopMark->position;
        if (startPos < decoder->GetPacketNumber()) {
            int newStartPos = index->GetKeyPacketNumberAfter(decoder->GetPacketNumber());
            dsyslog("cEncoder::CutOut(): startPos (%d) before decoder read position (%d) new startPos (%d)", startPos,  decoder->GetPacketNumber(), newStartPos);  // happens for too late recording starts
            startPos = newStartPos;
        }

        // seek to start position
        if (!decoder->SeekToPacket(index->GetKeyPacketNumberBefore(startPos))) {
            esyslog("cEncoder::CutOut(): seek to i-frame before start mark (%d) failed", startPos);
            return false;
        }
        if (decoder->IsVideoPacket()) decoder->DecodePacket(); // decode packet read from seek

        // check if we have a valid frame to set for start position
        AVFrame *avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);  // this should be a video packet, because we seek to video packets
        while (!avFrame || !decoder->IsVideoFrame() || (avFrame->pts == AV_NOPTS_VALUE) || (avFrame->pkt_dts == AV_NOPTS_VALUE) || (decoder->GetPacketNumber() < startPos)) {
            if (abortNow) return false;

            if (!avFrame) {
                if (decoder->IsVideoPacket()) dsyslog("cEncoder::CutOut(): packet (%d) stream %d: first video frame not yet decoded", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
            }
            else if (decoder->IsVideoPacket() && (decoder->GetPacketNumber() < startPos)) dsyslog("cEncoder::CutOut(): packet (%d) stream %d: frame before start position, prelod decoder", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
            else if (avFrame->pts == AV_NOPTS_VALUE) dsyslog("cEncoder::CutOut(): packet (%d) stream %d: frame not valid for start position, PTS invalid", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
            else if ((avFrame->pkt_dts == AV_NOPTS_VALUE)) dsyslog("cEncoder::CutOut(): packet (%d) stream %d: frame not valid for start position, DTS invalid", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);

            decoder->DropFrameFromGPU();     // we do not use this frame, cleanup GPU buffer
            if (!decoder->ReadNextPacket()) return false;
            if (decoder->IsVideoPacket()) decoder->DecodePacket(); // decode packet, no error break, maybe we only need more frames to decode (e.g. interlaced video)
            avFrame = decoder->GetFrame(AV_PIX_FMT_NONE);
        }
        // get PTS/DTS from start position
        cutInfo.startPosPTS = avFrame->pts;
        cutInfo.startPosDTS = avFrame->pkt_dts;
        // current start pos - last stop pos -> length of ad
        // use dts to prevent non monotonically increasing dts
        if (cutInfo.stopPosDTS > 0) cutInfo.offset += (cutInfo.startPosDTS - cutInfo.stopPosDTS);
        dsyslog("cEncoder::CutOut(): start cut from packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to frame (%d), offset %" PRId64, startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.offset);

        // read all packets
        while (decoder->GetPacketNumber() <= stopPos) {
            if (abortNow) return false;

#ifdef DEBUG_CUT  // first picures after start mark after
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
            AVPacket *avpktIn  = decoder->GetPacket();
            if ((avpktIn->pts >= cutInfo.startPosPTS) && (avpktIn->dts >= cutInfo.startPosDTS)) {
                // re-encode if needed
                if (decoder->IsVideoPacket()) {  // always decode video stream
                    if (!EncodeVideoFrame()) {
                        esyslog("cEncoder::CutOut(): decoder packet (%d): EncodeVideoFrame() failed", decoder->GetPacketNumber());
                        return false;
                    }
                }
                else if (ac3ReEncode && (pass == 2) && decoder->IsAudioAC3Packet()) {  // only re-encode AC3 if volume change is requested
                    if (!EncodeAC3Frame()) {
                        esyslog("cEncoder::CutOut(): decoder packet (%d): EncodeAC3Frame() failed", decoder->GetPacketNumber());
                        return false;
                    }
                }
                // no re-encode need, use input packet and write packet
                else if (!WritePacket(avpktIn, false)) {
                    esyslog("cEncoder::CutOut(): decoder packet (%d): WritePacket() failed", decoder->GetPacketNumber());
                    return false;
                }
            }
            else dsyslog("cEncoder::CutOut(): packet (%d), stream %d, PTS %" PRId64 ", DTS %" PRId64 ": drop packet before start PTS %" PRId64 ", DTS %" PRId64, decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPosPTS, cutInfo.startPosDTS);
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
        while (!(avFrame = decoder->GetFrame(AV_PIX_FMT_NONE))) {  // maybe last frame is corrupt
            dsyslog("cEncoder::CutOut(): packet (%d), stream %d: decode of frame after end position failed, try next", decoder->GetPacketNumber(), decoder->GetPacket()->stream_index);
            if (!decoder->ReadNextPacket()) return false;
            if (decoder->IsVideoPacket()) decoder->DecodePacket();
        }
        cutInfo.stopPosPTS = avFrame->pts;
        cutInfo.stopPosDTS = avFrame->pkt_dts;
        dsyslog("cEncoder::CutOut(): end cut from i-frame (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to i-frame (%6d) PTS %10" PRId64 " DTS %10" PRId64 ", offset %10" PRId64, startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.stopPosPTS, cutInfo.stopPosDTS, cutInfo.offset);
    }
// cut without decoding, only copy all packets
    else {
        // get start position
        int startPos  = -1;
        if (startMark->pts >= 0) startPos = index->GetKeyPacketNumberAfterPTS(startMark->pts);
        if (startPos < 0) {
            dsyslog("cEncoder::CutOut(): mark (%d): pts based cut position failed, use packet number", stopMark->position);
            startPos = startMark->position;
        }
        if (startPos < decoder->GetPacketNumber()) {
            dsyslog("cEncoder::CutOut(): startPos (%d) before decoder read position (%d)", startPos,  decoder->GetPacketNumber());  // happens for too late recording starts
            startPos = decoder->GetPacketNumber();
        }
        startPos = index->GetKeyPacketNumberAfter(startPos);  // adjust to i-frame
        if (startPos < 0) {
            esyslog("cEncoder::CutOut():: get i-frame after (%d) failed", startPos);
            return false;
        }

        // get stop position
        int stopPos  = -1;
        if (stopMark->pts >= 0) stopPos  = index->GetKeyPacketNumberBeforePTS(stopMark->pts);
        if (stopPos < 0) {
            dsyslog("cEncoder::CutOut(): mark (%d): pts based cut position failed, use packet number", stopMark->position);
            stopPos = stopMark->position;
        }
        stopPos = index->GetKeyPacketNumberBefore(stopPos);  // adjust to i-frame
        if (stopPos < 0) {
            esyslog("cEncoder::CutOut():: get i-frame before (%d) failed", stopPos);
            return false;
        }
        dsyslog("cEncoder::CutOut(): PTS based cut from key packet (%d) to key packet (%d)", startPos, stopPos);

        // seek to start position
        if (!decoder->SeekToPacket(startPos)) {  // this read startPos
            esyslog("cEncoder::CutOut(): seek to packet (%d) failed", startPos);
            return false;
        }
        // check if we have a valid packet to set for start position
        AVPacket *avpkt = decoder->GetPacket();  // this should be a video packet, because we seek to video packets
        while (!decoder->IsVideoKeyPacket() || (avpkt->pts == AV_NOPTS_VALUE) || (avpkt->dts == AV_NOPTS_VALUE)) {
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
        dsyslog("cEncoder::CutOut(): start cut from key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to key packet (%d), offset %" PRId64, startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.offset);

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
                if ((avpktIn->pts >= cutInfo.startPosPTS) && (avpktIn->dts >= cutInfo.startPosDTS)) {
                    if (!WritePacket(avpktIn, false)) {  // no re-encode done
                        esyslog("cEncoder::CutOut(): WritePacket() failed");
                        return false;
                    }
                }
                else dsyslog("cEncoder::CutOut(): packet (%d), stream %d, PTS %" PRId64", DTS %" PRId64 ": drop packet before start PTS %" PRId64 ", DTS %" PRId64, decoder->GetPacketNumber(), avpktIn->stream_index, avpktIn->pts, avpktIn->dts, cutInfo.startPosPTS, cutInfo.startPosDTS);
            }
            else esyslog("cEncoder::CutOut(): GetPacket() failed");
            // read next packet
            if (!decoder->ReadNextPacket()) return false;
        }

        // get PTS/DTS from end position
        avpkt =  decoder->GetPacket();  // this must be first video packet after cut because we only increase packet counter on video packets
        cutInfo.stopPosPTS = avpkt->pts;
        cutInfo.stopPosDTS = avpkt->dts;
        dsyslog("cEncoder::CutOut(): end cut from key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 " to key packet (%6d) PTS %10" PRId64 " DTS %10" PRId64 ", offset %10" PRId64, startPos, cutInfo.startPosPTS, cutInfo.startPosDTS, stopPos, cutInfo.stopPosPTS, cutInfo.stopPosDTS, cutInfo.offset);
    }
    LogSeparator();
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
        return true;  // can happen, try with next packet
    }
#ifdef DEBUG_PTS_DTS_CUT
    if (pass == 2) {
        dsyslog("cEncoder::EncodeVideoFrame(): decoder packet (%5d) stream %d in:  PTS %10" PRId64 " DTS %10" PRId64 ", diff PTS %10" PRId64 ", offset %10" PRId64, decoder->GetPacketNumber(), streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - inputFramePTSbefore[streamIndexIn], cutInfo.offset);
        inputFramePTSbefore[streamIndexIn] = avFrame->pts;
    }
#endif
    codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = avFrame->sample_aspect_ratio; // aspect ratio can change, set encoder pixel aspect ratio to decoded frames aspect ratio

#ifdef DEBUG_PTS_DTS_CUT
    if (pass == 2) {
        dsyslog("cEncoder::EncodeVideoFrame(): out frame (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - outputFramePTSbefore[streamIndexOut], cutInfo.offset);
        outputFramePTSbefore[streamIndexOut] = avFrame->pts;
    }
#endif

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

#ifdef DEBUG_PTS_DTS_CUT
        if (pass == 2) {
            dsyslog("cEncoder::EncodeVideoFrame(): out packet (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, avpktOut->stream_index, avpktOut->pts, avpktOut->dts, avpktOut->pts - outputPacketPTSbefore[streamIndexOut], cutInfo.offset);
            if (decoder->IsVideoPacket()) frameOut++;
            outputPacketPTSbefore[streamIndexOut] = avpktOut->pts;
        }
#endif

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
        esyslog("cEncoder::WritePacket(): decoder packet (%d): invalid packet", decoder->GetPacketNumber());  // packet invalid, try next
        return true;
    }
    if (!reEncoded) {
        // map input stream index to output stream index, drop packet if not used
        CheckInputFileChange();  // check if input file has changed
        int streamIndexIn = decoder->GetPacket()->stream_index;
        if ((streamIndexIn < 0) || (streamIndexIn >= MAXSTREAMS) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) { // prevent to overrun stream array
            esyslog("cEncoder::WritePacket(): decoder packet (%5d), stream %d: invalid input stream", decoder->GetPacketNumber(), streamIndexIn);
            return false;
        }
        int streamIndexOut = streamMap[streamIndexIn];
        if (streamIndexOut == -1) return true; // no target for this stream
        avpkt->stream_index = streamIndexOut;

        // correct pts after cut, only if not reencoded, in this case correct PTS/DTS is set from encoder
        avpkt->pts -= cutInfo.offset;
        avpkt->dts -= cutInfo.offset;
    }
    // check monotonically increasing dts
    if (avpkt->dts <= dts[avpkt->stream_index]) {
        dsyslog("cEncoder::WritePacket(): decoder packet (%5d), stream %d: dts %" PRId64 " <= last dts %" PRId64 ", drop packet", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->dts, dts[avpkt->stream_index]);
        return true;  // continue encoding
    }
    avpkt->pos = -1;   // byte position in stream unknown
#ifdef DEBUG_PTS_DTS_CUT
    if ((avpkt->stream_index == DEBUG_PTS_DTS_CUT) || (DEBUG_PTS_DTS_CUT == -1)) {
        dsyslog("cEncoder::WritePacket():      decoder packet (%5d), stream %d: output packet -> flags %d, PTS %10ld DTS %10ld, diff PTS to Key %10ld, offset %10ld", decoder->GetPacketNumber(), avpkt->stream_index, avpkt->flags, avpkt->pts, avpkt->dts, avpkt->pts - outputPacketPTSbefore[avpkt->stream_index], cutInfo.offset);
        if (decoder->IsVideoKeyPacket()) outputPacketPTSbefore[avpkt->stream_index] = avpkt->pts;
    }
#endif
    int rc = av_write_frame(avctxOut, avpkt);
    if (rc < 0) {
        esyslog("cEncoder::WritePacket(): decoder packet (%5d), stream %d: av_write_frame() failed, rc = %d: %s", decoder->GetPacketNumber(), avpkt->stream_index, rc, av_err2str(rc));
        return false;
    }
    dts[avpkt->stream_index] = avpkt->dts;
    return true;
}


bool cEncoder::SendFrameToEncoder(const int streamIndexOut, AVFrame *avFrame) {
#ifdef DEBUG_ENCODER
    LogSeparator();
#endif
    // correct PTS uncut to PTS after cut
    if (avFrame) { // can be nullptr to flush buffer
        avFrame->pts -= cutInfo.offset;
        if (avFrame->pts < pts[streamIndexOut]) {
            dsyslog("cEncoder::SendFrameToEncoder(): decoder packet (%5d), stream %d: pts %" PRId64 " <= last pts %" PRId64 ", drop packet", decoder->GetPacketNumber(), streamIndexOut, avFrame->pts, dts[streamIndexOut]);
            return true;   // can happen if we have recording errors
        }
        pts[streamIndexOut] = avFrame->pts;
    }

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
    if (rcReceive < 0) {
        switch (rcReceive) {
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
    }
#ifdef DEBUG_ENCODER
    dsyslog("cEncoder::ReceivePacketFromEncoder(): decoder packet (%d): avcodec_receive_packet() successful", decoder->GetPacketNumber());
#endif
    if (rcReceive == 0) return avpktOut;
    else {
        FREE(sizeof(*avpktOut), "avpktOut");
        av_packet_free(&avpktOut);
        return nullptr;
    }
}


bool cEncoder::CloseFile() {
    int ret = 0;

    // empty all encoder queue
    if (fullEncode) {
        for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {
            dsyslog("cEncoder::CloseFile(): stream %d: flush encoder queue", streamIndex);
            if (codecCtxArrayOut[streamIndex]) {
                if (codecCtxArrayOut[streamIndex]->codec_type == AVMEDIA_TYPE_SUBTITLE) continue; // draining encoder queue of subtitle stream is not valid, no encoding used
                // flash buffer
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
            avcodec_flush_buffers(codecCtxArrayOut[streamIndex]);
            dsyslog("cEncoder::CloseFile(): call avcodec_free_context for stream %d", streamIndex);
            FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
            avcodec_free_context(&codecCtxArrayOut[streamIndex]);
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


