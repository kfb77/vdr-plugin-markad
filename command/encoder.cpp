/*
 * encoder.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "decoder.h"
#include "encoder.h"


cAC3VolumeFilter::cAC3VolumeFilter() {
}


cAC3VolumeFilter::~cAC3VolumeFilter() {
    FREE(sizeof(*filterGraph), "filterGraph");
    avfilter_graph_free(&filterGraph);
}


#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
bool cAC3VolumeFilter::Init(const AVChannelLayout channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate)
#else
bool cAC3VolumeFilter::Init(const uint64_t channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate)
#endif
{
    AVFilterContext *volume_ctx = NULL;
    const AVFilter  *abuffer = NULL;
    const AVFilter  *volume = NULL;
    const AVFilter  *abuffersink = NULL;
    char ch_layout[64] = {};
    int err = 0;

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

// Now initialize the filter; we pass NULL options, since we have already set all the options above
    err = avfilter_init_str(filterSrc, NULL);
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
    err = avfilter_init_str(volume_ctx, NULL);
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
    err = avfilter_init_str(filterSink, NULL);
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
    err = avfilter_graph_config(filterGraph, NULL);
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
        dsyslog("cAC3VolumeFilter::SendFrame(): Error submitting the frame to the filtergraph %i", err);
        return false;
    }
    return true;
}


bool cAC3VolumeFilter::GetFrame(AVFrame *avFrame) {
    if (!avFrame) return false;
    int err = 0;
// Send the frame to the input of the filtergraph
    err = av_buffersink_get_frame(filterSink, avFrame);
    if (err < 0) {
        dsyslog("cAC3VolumeFilter::SendFrame(): Error getting the frame from the filtergraph %i", err);
        return false;
    }
    return true;
}



cEncoder::cEncoder(sMarkAdContext *macontext) {
    maContext = macontext;
    threadCount = maContext->Config->threads;
    if (threadCount < 1) threadCount = 1;
    if (threadCount > 16) threadCount = 16;
    dsyslog("cEncoder::cEncoder(): init with %i threads", threadCount);
}


cEncoder::~cEncoder() {
    for (unsigned int i = 0; i < avctxOut->nb_streams; i++) {
        if (ptr_cAC3VolumeFilter[i]) {
            FREE(sizeof(*ptr_cAC3VolumeFilter[i]), "ptr_cAC3VolumeFilter");
            delete ptr_cAC3VolumeFilter[i];
        }
    }

    if (stats_in.data) {
        FREE(stats_in.size, "stats_in");
        free(stats_in.data);
    }

    dsyslog("cEncoder::~cEncoder(): call avformat_free_context");
    FREE(sizeof(*avctxOut), "avctxOut");
    avformat_free_context(avctxOut);
}


void cEncoder::Reset(const int passEncoder) {
    EncoderStatus.videoStartDTS          = INT64_MAX;
    EncoderStatus.frameBefore            = -2;
    EncoderStatus.ptsOutBefore           = -1;
    EncoderStatus.pts_dts_CutOffset      = 0;     // offset from the cut out frames
    EncoderStatus.pts_dts_CyclicalOffset = NULL;  // offset from pts/dts cyclicle, multiple of 0x200000000
    pass                                 = passEncoder;
#ifdef DEBUG_CUT
    frameOut = 0;
#endif
}


bool cEncoder::OpenFile(const char *directory, cDecoder *ptr_cDecoder) {
    if (!directory) return false;
    if (!ptr_cDecoder) return false;

    int ret = 0;
    char *filename = NULL;
    char *buffCutName;

    ptr_cDecoder->Reset();

    avctxIn = ptr_cDecoder->GetAVFormatContext();
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

    EncoderStatus.pts_dts_CyclicalOffset = static_cast<int64_t *>(malloc(sizeof(int64_t) * avctxIn->nb_streams));
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "pts_dts_CyclicalOffset");
    memset(EncoderStatus.pts_dts_CyclicalOffset, 0, sizeof(int64_t) * avctxIn->nb_streams);

    EncoderStatus.ptsInBefore = static_cast<int64_t *>(malloc(sizeof(int64_t) * avctxIn->nb_streams));
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "ptsInBefore");
    memset(EncoderStatus.ptsInBefore, 0, sizeof(int64_t) * avctxIn->nb_streams);

    EncoderStatus.dtsInBefore = static_cast<int64_t *>(malloc(sizeof(int64_t) * avctxIn->nb_streams));
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "dtsInBefore");
    memset(EncoderStatus.dtsInBefore, 0, sizeof(int64_t) * avctxIn->nb_streams);

    streamMap = static_cast<int *>(malloc(sizeof(int) * avctxIn->nb_streams));
    ALLOC(sizeof(int) * avctxIn->nb_streams, "streamMap");
    memset(streamMap, -1, sizeof(int) * avctxIn->nb_streams);

    if (asprintf(&buffCutName,"%s", directory)==-1) {
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

    char *cutName = strrchr(buffCutName, '/');
    if (!cutName) {
        dsyslog("cEncoder::OpenFile(): failed to find last '/'");
        FREE(strlen(buffCutName)+1, "buffCutName");
        free(buffCutName);
        return false;
    }
    cutName++;   // ignore first char = /
    dsyslog("cEncoder::OpenFile(): cutName '%s'",cutName);

    if (asprintf(&filename,"%s/%s.ts", directory, cutName)==-1) {
        dsyslog("cEncoder::OpenFile(): failed to allocate string, out of memory?");
        return false;
    }
    ALLOC(strlen(filename)+1, "filename");
#ifdef DEBUG_MEM
    FREE(memsize_buffCutName, "buffCutName");
#endif
    free(buffCutName);
    datePart = NULL;
    dsyslog("cEncoder::OpenFile(): write to '%s'", filename);

    avformat_alloc_output_context2(&avctxOut, NULL, NULL, filename);
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    ALLOC(sizeof(*avctxOut), "avctxOut");
    dsyslog("cEncoder::OpenFile(): output format %s", avctxOut->oformat->long_name);

    codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
    if (!codecCtxArrayIn) {
        dsyslog("cEncoder::OpenFile(): failed to get input codec context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }

    // find best streams (video should be stream 0)
    int bestVideoStream = av_find_best_stream(avctxIn, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (bestVideoStream < 0) {
        dsyslog("cEncoder::OpenFile(): failed to find best video stream, rc=%d", bestVideoStream);
        return false;
    }
    int bestAudioStream = av_find_best_stream(avctxIn, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    if (bestAudioStream < 0) {
        dsyslog("cEncoder::OpenFile(): failed to find best audio stream, rc=%d", bestAudioStream);
        return false;
    }
    if (maContext->Config->bestEncode) {
        dsyslog("cEncoder::OpenFile(): best video: stream %d", bestVideoStream);
        dsyslog("cEncoder::OpenFile(): best audio: stream %d", bestAudioStream);
    }

    // init all needed encoder
    for (int index = 0; index < static_cast<int>(avctxIn->nb_streams); index++) {
        if (!codecCtxArrayIn[index]) break;   // if we have no input codec we can not decode and encode this stream and all after
        if (maContext->Config->fullEncode && maContext->Config->bestEncode) {
            if (index == bestVideoStream) streamMap[index] = 0;
            if (index == bestAudioStream) streamMap[index] = 1;
        }
        else {
            if (ptr_cDecoder->IsVideoStream(index) || ptr_cDecoder->IsAudioStream(index) || ptr_cDecoder->IsSubtitleStream(index)) streamMap[index] = index;
            else {
                dsyslog("cEncoder::OpenFile(): stream %d is no audio, no video and no subtitle, ignoring", index);
                streamMap[index] = -1;
            }
        }
        dsyslog("cEncoder::OpenFile(): source stream %d -----> target stream %d", index, streamMap[index]);
        if (streamMap[index] >= 0) {  // only init used streams
            if (ptr_cDecoder->IsAudioStream(index) && codecCtxArrayIn[index]->sample_rate == 0) {  // ignore mute audio stream
                dsyslog("cEncoder::OpenFile(): input stream %d: sample_rate not set, ignore mute audio stream", index);
                streamMap[index] = -1;
            }
            else {
                if (!InitEncoderCodec(ptr_cDecoder, directory, index, streamMap[index])) {
                    esyslog("cEncoder::OpenFile(): InitEncoderCodec failed");
                    // cleanup memory
                    FREE(strlen(filename)+1, "filename");
                    free(filename);
                    FREE(sizeof(int64_t) * avctxIn->nb_streams, "pts_dts_CyclicalOffset");
                    free(EncoderStatus.pts_dts_CyclicalOffset);
                    FREE(sizeof(int64_t) * avctxIn->nb_streams, "ptsInBefore");
                    free(EncoderStatus.ptsInBefore);
                    FREE(sizeof(int64_t) * avctxIn->nb_streams, "dtsInBefore");
                    free(EncoderStatus.dtsInBefore);
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
    ret = avformat_write_header(avctxOut, NULL);
    if (ret < 0) {
        dsyslog("cEncoder::OpenFile(): could not write header");
        return false;
    }
    return true;
}


bool cEncoder::ChangeEncoderCodec(cDecoder *ptr_cDecoder, const int streamIndexIn,  const int streamIndexOut, AVCodecContext *avCodecCtxIn) {
    if(!ptr_cDecoder) return false;
    if (!avctxIn) return false;
    if (streamIndexIn >= static_cast<int>(avctxIn->nb_streams)) {
        dsyslog("cEncoder::ChangeEncoderCodec(): input stream index %d out of range of nb_streams %d", streamIndexIn, avctxIn->nb_streams);
        return false;
    }
    if (!avCodecCtxIn) return false;


#if LIBAVCODEC_VERSION_INT >= ((60<<16)+(39<<8)+100)
    dsyslog("cEncoder::ChangeEncoderCodec(): call avcodec_free_context");
    avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
#else
    dsyslog("cEncoder::ChangeEncoderCodec(): call avcodec_close");
    avcodec_close(codecCtxArrayOut[streamIndexOut]);
#endif
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

    codecCtxArrayOut[streamIndexOut]->time_base.num =  avCodecCtxIn->time_base.num;
    codecCtxArrayOut[streamIndexOut]->time_base.den = avCodecCtxIn->time_base.den;
    if (ptr_cDecoder->IsVideoStream(streamIndexIn)) {
        codecCtxArrayOut[streamIndexOut]->pix_fmt = avCodecCtxIn->pix_fmt;
        codecCtxArrayOut[streamIndexOut]->height = avCodecCtxIn->height;
        codecCtxArrayOut[streamIndexOut]->width = avCodecCtxIn->width;
    }
    else if (ptr_cDecoder->IsAudioStream(streamIndexIn)) {
        codecCtxArrayOut[streamIndexOut]->sample_fmt = avCodecCtxIn->sample_fmt;
        codecCtxArrayOut[streamIndexOut]->sample_rate = avCodecCtxIn->sample_rate;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 37<<8)+100)
        int rc = av_channel_layout_copy(&codecCtxArrayOut[streamIndexOut]->ch_layout, &codecCtxArrayIn[streamIndexIn]->ch_layout);
        if (rc != 0) {
            dsyslog("cEncoder::ChangeEncoderCodec(): av_channel_layout_copy for output stream %d from input stream %d  failed, rc = %d", streamIndexOut, streamIndexIn, rc);
            return false;
        }
#elif LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        codecCtxArrayOut[streamIndexOut]->ch_layout.u.mask = avCodecCtxIn->ch_layout.u.mask;
        codecCtxArrayOut[streamIndexOut]->ch_layout.nb_channels = avCodecCtxIn->ch_layout.nb_channels;
#else
        codecCtxArrayOut[streamIndexOut]->channel_layout = avCodecCtxIn->channel_layout;
        codecCtxArrayOut[streamIndexOut]->channels = avCodecCtxIn->channels;
#endif
    }
    else {
        dsyslog("cEncoder::ChangeEncoderCodec(): codec of input stream %i not supported", streamIndexIn);
        return false;
    }
    codecCtxArrayOut[streamIndexOut]->thread_count = threadCount;
    if (avcodec_open2(codecCtxArrayOut[streamIndexOut], codec, NULL) < 0) {
        dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for output stream %i failed", streamIndexOut);
        dsyslog("cEncoder::ChangeEncoderCodec(): call avcodec_free_context for stream %d", streamIndexOut);
        FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
        codecCtxArrayOut[streamIndexOut]=NULL;
        return false;
    }
    dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for output stream %i successful", streamIndexOut);

    if (ptr_cDecoder->IsAudioAC3Stream(streamIndexIn)) {
        dsyslog("cEncoder::ChangeEncoderCodec(): AC3 input stream found %i, re-initialize volume filter for outpur stream %d", streamIndexIn, streamIndexOut);
        if (maContext->Config->ac3ReEncode) {
            if (!ptr_cAC3VolumeFilter[streamIndexOut]) {
                dsyslog("cEncoder::ChangeEncoderCodec(): ptr_cAC3VolumeFilter not initialized for output stream %i", streamIndexOut);
                return false;
            }
            FREE(sizeof(*ptr_cAC3VolumeFilter[streamIndexOut]), "ptr_cAC3VolumeFilter");
            delete ptr_cAC3VolumeFilter[streamIndexOut];
            ptr_cAC3VolumeFilter[streamIndexOut] = new cAC3VolumeFilter();
            ALLOC(sizeof(*ptr_cAC3VolumeFilter[streamIndexOut]), "ptr_cAC3VolumeFilter");
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            if (!ptr_cAC3VolumeFilter[streamIndexOut]->Init(avCodecCtxIn->ch_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate))
#else
            if (!ptr_cAC3VolumeFilter[streamIndexOut]->Init(avCodecCtxIn->channel_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate))
#endif
            {
                dsyslog("cEncoder::ChangeEncoderCodec(): ptr_cAC3VolumeFilter->Init() failed");
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


bool cEncoder::InitEncoderCodec(cDecoder *ptr_cDecoder, const char *directory, const unsigned int streamIndexIn, const unsigned int streamIndexOut) {
    if (!ptr_cDecoder) return false;
    if (!avctxIn) return false;
    if (!avctxOut) return false;
    if (streamIndexIn >= avctxIn->nb_streams) {
        dsyslog("cEncoder::InitEncoderCodec(): streamindex %d out of range", streamIndexIn);
        return false;
    }
    if (!codecCtxArrayIn) {
        dsyslog("cEncoder::InitEncoderCodec(): no input codec arry set");
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
    if (ptr_cDecoder->IsVideoStream(streamIndexIn)) {
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

        codecCtxArrayOut[streamIndexOut]->pix_fmt = codecCtxArrayIn[streamIndexIn]->pix_fmt;
        codecCtxArrayOut[streamIndexOut]->height = codecCtxArrayIn[streamIndexIn]->height;
        codecCtxArrayOut[streamIndexOut]->width = codecCtxArrayIn[streamIndexIn]->width;
        codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = codecCtxArrayIn[streamIndexIn]->sample_aspect_ratio;

        // calculate target mpeg2 video stream bit rate from recording
        int bit_rate = avctxIn->bit_rate; // overall recording bitrate
        for (unsigned int index = 0; index < avctxIn->nb_streams; index ++) {
            if (codecCtxArrayIn[index] && !ptr_cDecoder->IsVideoStream(index)) bit_rate -= codecCtxArrayIn[index]->bit_rate;  // audio streams bit rate
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
            if (asprintf(&passlogfile,"%s/encoder", directory) == -1) {
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
        if (ptr_cDecoder->IsInterlacedVideo()) {
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
        if (ptr_cDecoder->IsAudioStream(streamIndexIn)) {
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
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
                av_opt_set_int(swrArray[streamIndexOut], "in_channel_layout", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask, 0);
                av_opt_set_int(swrArray[streamIndexOut], "out_channel_layout", codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask,  0);
#else
                av_opt_set_int(swrArray[streamIndexOut], "in_channel_layout", codecCtxArrayIn[streamIndexIn]->channel_layout, 0);
                av_opt_set_int(swrArray[streamIndexOut], "out_channel_layout", codecCtxArrayIn[streamIndexIn]->channel_layout,  0);
#endif
                av_opt_set_int(swrArray[streamIndexOut], "in_sample_rate", codecCtxArrayIn[streamIndexIn]->sample_rate, 0);
                av_opt_set_int(swrArray[streamIndexOut], "out_sample_rate", codecCtxArrayIn[streamIndexIn]->sample_rate, 0);
                av_opt_set_sample_fmt(swrArray[streamIndexOut], "in_sample_fmt", AV_SAMPLE_FMT_S16P, 0);
                av_opt_set_sample_fmt(swrArray[streamIndexOut], "out_sample_fmt", AV_SAMPLE_FMT_S16,  0);
                dsyslog("cEncoder::InitEncoderCodec(): swr_init for output stream index %d", streamIndexOut);
                int rc = swr_init(swrArray[streamIndexOut]);
                if (rc < 0) {
                    char errTXT[64] = {0};
                    av_strerror(rc, errTXT, sizeof(errTXT));
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
            if (ptr_cDecoder->IsSubtitleStream(streamIndexIn)) {
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
    if ((codecCtxArrayOut[streamIndexOut]->time_base.num == 0) && (!ptr_cDecoder->IsSubtitleStream(streamIndexIn))) {
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

    codecCtxArrayOut[streamIndexOut]->thread_count = threadCount;
    if (avcodec_open2(codecCtxArrayOut[streamIndexOut], codec, NULL) < 0) {
        esyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %d failed", streamIndexOut);
        dsyslog("cEncoder::InitEncoderCodec(): call avcodec_free_context for stream %d", streamIndexOut);
        FREE(sizeof(*codecCtxArrayOut[streamIndexOut]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndexOut]);
        codecCtxArrayOut[streamIndexOut] = NULL;
    }
    else dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i successful", streamIndexOut);

    if (ptr_cDecoder->IsAudioAC3Stream(streamIndexIn) && maContext->Config->ac3ReEncode) {
        dsyslog("cEncoder::InitEncoderCodec(): AC3 input found at stream %i, initialize volume filter for output stream %d", streamIndexIn, streamIndexOut);
        if (ptr_cAC3VolumeFilter[streamIndexOut]) {
            dsyslog("cEncoder::InitEncoderCodec(): ptr_cAC3VolumeFilter is not NULL for output stream %i", streamIndexOut);
            FREE(sizeof(*ptr_cAC3VolumeFilter[streamIndexOut]), "ptr_cAC3VolumeFilter");
            delete ptr_cAC3VolumeFilter[streamIndexOut];
        }
        ptr_cAC3VolumeFilter[streamIndexOut] = new cAC3VolumeFilter();
        ALLOC(sizeof(*ptr_cAC3VolumeFilter[streamIndexOut]), "ptr_cAC3VolumeFilter");
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
        if (!ptr_cAC3VolumeFilter[streamIndexOut]->Init(codecCtxArrayIn[streamIndexIn]->ch_layout, codecCtxArrayIn[streamIndexIn]->sample_fmt, codecCtxArrayIn[streamIndexIn]->sample_rate))
#else
        if (!ptr_cAC3VolumeFilter[streamIndexOut]->Init(codecCtxArrayIn[streamIndexIn]->channel_layout, codecCtxArrayIn[streamIndexIn]->sample_fmt, codecCtxArrayIn[streamIndexIn]->sample_rate))
#endif
        {
            dsyslog("cEncoder::InitEncoderCodec(): ptr_cAC3VolumeFilter->Init() failed");
            return false;
        }
    }
    return true;
}


// resample audio frame from AV_SAMPLE_FMT_S16P to AV_SAMPLE_FMT_S16
//
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


bool cEncoder::WritePacket(AVPacket *avpktIn, cDecoder *ptr_cDecoder) {
    if (!avctxOut ) {
        dsyslog("cEncoder::WritePacket(): got no AVFormatContext from output file");
        return false;
    }
    if (!ptr_cDecoder ) {
        dsyslog("cEncoder::WritePacket(): got no ptr_cDecoder from output file");
        return false;
    }
    if ((pass == 1) && !ptr_cDecoder->IsVideoPacket()) return true;  // first pass we only need to re-encode video stream
    avctxIn = ptr_cDecoder->GetAVFormatContext();  // avctx changes at each input file
    int frameNumber =  ptr_cDecoder->GetFrameNumber();

    // check if stream is valid
    int streamIndexIn = avpktIn->stream_index;
    if ((streamIndexIn < 0) || (streamIndexIn >= static_cast<int>(avctxIn->nb_streams))) return false; // prevent to overrun stream array
    int streamIndexOut = streamMap[streamIndexIn];
    if (streamIndexOut == -1) return true; // no target for this stream

#ifdef DEBUG_CUT
    if (pass == 2) {
        dsyslog("cEncoder::WritePacket(): ----------------------------------------------------------------------------------------------------------------------");
        dsyslog("cEncoder::WritePacket(): in  packet (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameNumber, streamIndexIn, avpktIn->pts, avpktIn->dts, avpktIn->pts - inputPacketPTSbefore[streamIndexIn], EncoderStatus.pts_dts_CutOffset);
        inputPacketPTSbefore[streamIndexIn] = avpktIn->pts;
    }
#endif

    // check if packet is valid
    if (avpktIn->dts == AV_NOPTS_VALUE) {
        dsyslog("cEncoder::WritePacket(): frame (%d) got no dts value from input stream %d", frameNumber, streamIndexIn);
        EncoderStatus.frameBefore = frameNumber;
        return false;
    }
    if (avpktIn->pts <  avpktIn->dts) {
        dsyslog("cEncoder::WritePacket(): input stream %d frame   (%d) pts (%" PRId64 ") smaller than dts (%" PRId64 ")", streamIndexIn, frameNumber, avpktIn->pts, avpktIn->dts);
        EncoderStatus.frameBefore = frameNumber;
        return false;
    }
    if ((avpktIn->dts > EncoderStatus.dtsInBefore[streamIndexIn] + 0x100000000) && (EncoderStatus.dtsInBefore[streamIndexIn] > 0)) { // maybe input stream is faulty
        dsyslog("cEncoder::WritePacket(): invalid dts %" PRId64 " (dts before was %" PRId64 ") in input stream %d at frame (%d), ignore packet", avpktIn->dts, EncoderStatus.dtsInBefore[streamIndexIn], streamIndexIn, frameNumber);
        EncoderStatus.frameBefore = frameNumber;
        return false;
    }

    // check if there was a dts cyclicle
    avpktIn->dts += EncoderStatus.pts_dts_CyclicalOffset[streamIndexIn];
    avpktIn->pts += EncoderStatus.pts_dts_CyclicalOffset[streamIndexIn];
    if (EncoderStatus.dtsInBefore[streamIndexIn] >= avpktIn->dts) { // dts should monotonically increasing
        if (avpktIn->dts - avpktIn->duration - EncoderStatus.dtsInBefore[streamIndexIn] == -0x200000000) {
            EncoderStatus.pts_dts_CyclicalOffset[streamIndexIn] += 0x200000000;
            dsyslog("cEncoder::WritePacket(): dts and pts cyclicle in input stream %d at frame (%d), offset now 0x%" PRId64 "X", streamIndexIn, frameNumber, EncoderStatus.pts_dts_CyclicalOffset[streamIndexIn]);
            avpktIn->dts += 0x200000000;
            avpktIn->pts += 0x200000000;
        }
        else { // non monotonically increasing dts, drop this packet
            dsyslog("cEncoder::WritePacket(): non monotonically increasing dts at frame (%6d) of input stream %d, dts last packet %10" PRId64 ", dts offset %" PRId64, frameNumber, streamIndexIn, EncoderStatus.dtsInBefore[streamIndexIn], avpktIn->dts - EncoderStatus.dtsInBefore[streamIndexIn]);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)  // yavdr Xenial ffmpeg
            dsyslog("cEncoder::WritePacket():                                                                       dts this packet %10" PRId64 ", duration %" PRId64, avpktIn->dts, avpktIn->duration);
#else
            dsyslog("cEncoder::WritePacket():                                                                 dts this packet %10" PRId64 ", duration %d", avpktIn->dts, avpktIn->duration);
#endif
            EncoderStatus.frameBefore = frameNumber;
            return true;
        }
    }

    // set video start infos
    if (ptr_cDecoder->IsVideoPacket()) {
        if ((frameNumber - EncoderStatus.frameBefore) > 1) {  // first frame after start mark position
            if (EncoderStatus.dtsInBefore[streamIndexIn] == 0) EncoderStatus.dtsInBefore[streamIndexIn] = avpktIn->dts - avpktIn->duration; // first frame has no before, init with dts of start mark
            dsyslog("cEncoder::WritePacket(): start cut at            frame (%6d)                 PTS %" PRId64, frameNumber, avpktIn->pts);
            EncoderStatus.videoStartDTS = avpktIn->dts;
            EncoderStatus.pts_dts_CutOffset += (avpktIn->dts - EncoderStatus.dtsInBefore[streamIndexIn] - avpktIn->duration);
            dsyslog("cEncoder::WritePacket(): new pts/dts offset %" PRId64, EncoderStatus.pts_dts_CutOffset);
        }
    }
    EncoderStatus.frameBefore = frameNumber;

    // drop packets with pts before video start
    if (avpktIn->dts < EncoderStatus.videoStartDTS) {
#ifdef DEBUG_CUT
        if (pass == 2) {
            dsyslog("cEncoder::WritePacket(): in  packet (%5d) stream index %d DTS %10ld is lower then video start DTS %10ld, drop packet", frameNumber, streamIndexIn, avpktIn->dts, EncoderStatus.videoStartDTS);
        }
#endif
        return true;
    }

    // store values of frame before
    EncoderStatus.ptsInBefore[streamIndexIn] = avpktIn->pts;
    EncoderStatus.dtsInBefore[streamIndexIn] = avpktIn->dts;

    // re-encode packet if needed
    if ((streamIndexOut >= 0) &&  // only re-encode if stream is in streamMap
            ((maContext->Config->ac3ReEncode && ptr_cDecoder->IsAudioAC3Packet()) ||
             (maContext->Config->fullEncode && !ptr_cDecoder->IsSubtitlePacket()))) {  // even with full encode, do no re-encode subtitle, use it as it is

        // check valid stream index
        if (streamIndexOut >= static_cast<int>(avctxOut->nb_streams)) return false;

        // check encoder, it can be wrong if recording is damaged
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
        if (ptr_cDecoder->IsAudioAC3Packet() && avctxOut->streams[streamIndexOut]->codecpar->codec_id != AV_CODEC_ID_AC3)
#else
        if (ptr_cDecoder->IsAudioAC3Packet() && avctxOut->streams[streamIndexOut]->codec->codec_id != AV_CODEC_ID_AC3)
#endif
        {
            dsyslog("cEncoder::WritePacket(): invalid encoder for AC3 packet of output stream %d at frame (%6d)", streamIndexOut, frameNumber);
            return false;
        }

        // get context for decoding/encoding
        codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
        if (!codecCtxArrayIn) {
            dsyslog("cEncoder::WritePacket(): failed to get input codec context");
            return false;
        }
        if (!codecCtxArrayOut[streamIndexOut]) {
            dsyslog("cEncoder::WritePacket(): Codec Context not found for output stream %d", streamIndexIn);
            return false;
        }

        // decode packet
        AVFrame *avFrame = ptr_cDecoder->DecodePacket(avpktIn);
        if (!avFrame) {  // this is no error, maybe we only need more frames to decode (e.g. interlaced video)
            return true;
        }
#ifdef DEBUG_CUT
        if (pass == 2) {
            dsyslog("cEncoder::WritePacket(): in  frame  (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameNumber, streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - inputFramePTSbefore[streamIndexIn], EncoderStatus.pts_dts_CutOffset);
            inputFramePTSbefore[streamIndexIn] = avFrame->pts;
        }
#endif
        //encode frame
        codecCtxArrayOut[streamIndexOut]->sample_aspect_ratio = avFrame->sample_aspect_ratio; // set encoder pixel aspect ratio to decoded frames aspect ratio
        avFrame->pts = avFrame->pts - EncoderStatus.pts_dts_CutOffset; // correct pts after cut

        if (ptr_cDecoder->IsVideoPacket()) {
            // libav encoder does not accept two frames with same pts
            if (EncoderStatus.ptsOutBefore == avFrame->pts) {
                dsyslog("cEncoder::WritePacket(): got duplicate pts from video decoder, change pts from %" PRId64 " to %" PRId64, avFrame->pts, avFrame->pts + 1);
                avFrame->pts = EncoderStatus.ptsOutBefore + 1;
            }
            // check monotonically increasing pts in frame after decoding
            // prevent "AVlog(): Assertion pict_type == rce->new_pict_type failed at src/libavcodec/ratecontrol.c:939" with ffmpeg 4.2.2
            if (EncoderStatus.ptsOutBefore > avFrame->pts) {
                dsyslog("cEncoder::WritePacket(): got non monotonically increasing pts %" PRId64 " from video decoder, pts before was %" PRId64, avFrame->pts, EncoderStatus.ptsOutBefore);
                return false;
            }
            EncoderStatus.ptsOutBefore = avFrame->pts;
        }

        if (ptr_cDecoder->IsAudioAC3Packet()) {
            // Check if the audio stream has changed channels
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
            if ((codecCtxArrayOut[streamIndexOut]->ch_layout.u.mask != codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask) ||
                    (codecCtxArrayOut[streamIndexOut]->ch_layout.nb_channels != codecCtxArrayIn[streamIndexIn]->ch_layout.nb_channels)) {
                dsyslog("cEncoder::WritePacket(): channel layout of input stream %d changed at frame %d from %" PRIu64 " to %" PRIu64, streamIndexIn, frameNumber, codecCtxArrayOut[streamIndexOut]->ch_layout.u.mask, codecCtxArrayIn[streamIndexIn]->ch_layout.u.mask);
                dsyslog("cEncoder::WritePacket(): number of channels of input stream %d changed at frame %d from %d to %d", streamIndexIn, frameNumber, codecCtxArrayOut[streamIndexOut]->ch_layout.nb_channels, codecCtxArrayIn[streamIndexIn]->ch_layout.nb_channels);
#else
            if ((codecCtxArrayOut[streamIndexOut]->channel_layout != codecCtxArrayIn[streamIndexIn]->channel_layout) ||
                    (codecCtxArrayOut[streamIndexOut]->channels != codecCtxArrayIn[streamIndexIn]->channels)) {
                dsyslog("cEncoder::WritePacket(): channel layout of input stream %d changed at frame %d from %" PRIu64 " to %" PRIu64, streamIndexIn, frameNumber, codecCtxArrayOut[streamIndexOut]->channel_layout, codecCtxArrayIn[streamIndexIn]->channel_layout);
                dsyslog("cEncoder::WritePacket(): number of channels of input stream %d changed at frame %d from %d to %d", streamIndexIn, frameNumber, codecCtxArrayOut[streamIndexOut]->channels, codecCtxArrayIn[streamIndexIn]->channels);
#endif

                if(!ChangeEncoderCodec(ptr_cDecoder, streamIndexIn, streamIndexOut, codecCtxArrayIn[streamIndexIn])) {
                    esyslog("encoder initialization failed for output stream index %d, source is stream index %d", streamIndexOut, streamIndexIn);
                    return false;
                }
            }

            // use filter to adapt AC3 volume
            if (maContext->Config->ac3ReEncode) {
                if (!ptr_cAC3VolumeFilter[streamIndexOut]->SendFrame(avFrame)) {
                    dsyslog("cEncoder::WriteFrame(): cAC3VolumeFilter SendFrame failed");
                    return false;
                }
                if (!ptr_cAC3VolumeFilter[streamIndexOut]->GetFrame(avFrame)) {
                    dsyslog("cEncoder::WriteFrame(): cAC3VolumeFilter GetFrame failed");
                    return false;
                }
            }
        }
#ifdef DEBUG_ENCODER
        SaveFrame(frameNumber, avFrame);
#endif
        // resample by libav not supported planar audio
        AVFrame *avFrameOut = NULL;
        if (ptr_cDecoder->IsAudioPacket() && (codecCtxArrayIn[streamIndexIn]->sample_fmt == AV_SAMPLE_FMT_S16P)) {
            avFrameOut = av_frame_alloc();
            if (!avFrameOut) {
                dsyslog("cDecoder::WritePacket(): av_frame_alloc for avFrameOut ailed");
                return NULL;
            }
            ALLOC(sizeof(*avFrameOut), "avFrameOut");
            if (!ReSampleAudio(avFrame, avFrameOut, streamIndexOut)) {
                dsyslog("cEncoder::WriteFrame(): ReSampleAudio failed");
                FREE(sizeof(*avFrameOut), "avFrameOut");
                av_frame_free(&avFrameOut);
                return false;
            }
            avFrame = avFrameOut;
        }

#ifdef DEBUG_CUT
        if (pass == 2) {
            dsyslog("cEncoder::WritePacket(): out frame  (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, streamIndexOut, avFrame->pts, avFrame->pts, avFrame->pts - outputFramePTSbefore[streamIndexOut], EncoderStatus.pts_dts_CutOffset);
            outputFramePTSbefore[streamIndexOut] = avFrame->pts;
        }
#endif
        //encode frame
        AVPacket avpktOut;
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(134<<8)+100)
        av_init_packet(&avpktOut);
#endif
        // init avpktOut
        avpktOut.size            = 0;
        avpktOut.data            = NULL;
        avpktOut.side_data_elems = 0;
        avpktOut.side_data       = NULL;
        avpktOut.buf             = NULL;
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
        avpktOut.opaque          = NULL;
        avpktOut.opaque_ref      = NULL;
#endif

        avFrame->pict_type = AV_PICTURE_TYPE_NONE;  // encoder decides picture type
        if ((EncoderStatus.videoEncodeError) && ptr_cDecoder->IsVideoPacket()) {
            // prevent "AVlog(): Assertion pict_type == rce->new_pict_type failed at src/libavcodec/ratecontrol.c:939" with ffmpeg 4.2.2
            dsyslog("cEncoder::WritePacket(): recover from encoding error and force i-frame (%d)", frameNumber);
            avFrame->pict_type = AV_PICTURE_TYPE_I;
            EncoderStatus.videoEncodeError = false;
        }
        if (codecCtxArrayOut[streamIndexOut]) {
            if (!EncodeFrame(ptr_cDecoder, codecCtxArrayOut[streamIndexOut], avFrame, &avpktOut)) {
                av_packet_unref(&avpktOut);
                if (stateEAGAIN) {
//                    dsyslog("cEncoder::WritePacket(): encoder for output stream %d at frame %d need more frames", streamIndexOut, frameNumber);
                    return true;
                }
                else {
                    dsyslog("cEncoder::WritePacket(): encoder failed for output stream %d at frame %d", streamIndexOut, frameNumber);
                    return false;
                }
            }
        }
        else {
            dsyslog("cEncoder::WritePacket(): encoding of stream %d not supported", streamIndexIn);
            av_packet_unref(&avpktOut);
            return false;
        }
        // free resampled frame
        if (avFrameOut) {
            FREE(sizeof(*(avFrameOut->extended_data)), "extended_data");  // only size of pointer array as reminder
            av_freep(avFrameOut->extended_data);
            FREE(sizeof(*avFrameOut), "avFrameOut");
            av_frame_free(&avFrameOut);
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
                    return false;
                }
                stats_in.data = stats_in_tmp;
                stats_in.size = strLength + oldLength + 1;
                strcat(stats_in.data, codecCtxArrayOut[streamIndexOut]->stats_out);
                stats_in.size = strLength + oldLength + 1;
                ALLOC(stats_in.size, "stats_in");
            }
        }
        avpktOut.stream_index = streamIndexOut;
        avpktOut.pos = -1;   // byte position in stream unknown
#ifdef DEBUG_CUT
        if (pass == 2) {
            dsyslog("cEncoder::WritePacket(): out packet (%5d) stream index %d PTS %10ld DTS %10ld, diff PTS %10ld, offset %10ld", frameOut, avpktOut.stream_index, avpktOut.pts, avpktOut.dts, avpktOut.pts - outputPacketPTSbefore[streamIndexOut], EncoderStatus.pts_dts_CutOffset);
            if (ptr_cDecoder->IsVideoPacket()) frameOut++;
            outputPacketPTSbefore[streamIndexOut] = avpktOut.pts;
        }
#endif
        // write packet
        av_write_frame(avctxOut, &avpktOut);
        // free memory
        av_packet_unref(&avpktOut);
    }
    else {
        if (!maContext->Config->fullEncode || ptr_cDecoder->IsSubtitlePacket()) {  // no re-encode, copy input packet to output stream, never re-encode subtitle
            if (streamIndexOut >= static_cast<int>(avctxOut->nb_streams)) return true;  // ignore high streamindex from input stream, they are unsupported subtitle
            // correct pts after cut
            avpktIn->pts = avpktIn->pts - EncoderStatus.pts_dts_CutOffset;
            avpktIn->dts = avpktIn->dts - EncoderStatus.pts_dts_CutOffset;
            avpktIn->pos = -1;   // byte position in stream unknown
            av_write_frame(avctxOut, avpktIn);
        }
    }
    if (avpktIn->stream_index == 0) ptsBefore = avpktIn->pts;
    return true;
}


bool cEncoder::EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodecCtx, AVFrame *avFrame, AVPacket *avpkt) {
    if (!ptr_cDecoder) return false;
    if (!avCodecCtx) {
        dsyslog("cEncoder::EncodeFrame(): codec context not set");
        return false;
    }
    if (!avpkt) return false;
    stateEAGAIN = false;

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avFrame) {
        int rcSend = avcodec_send_frame(avCodecCtx, avFrame);
        if (rcSend < 0) {
            switch (rcSend) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): avcodec_send_frame() error EAGAIN at frame %d", ptr_cDecoder->GetFrameNumber());
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): avcodec_send_frame() error EINVAL at frame %d", ptr_cDecoder->GetFrameNumber());
                break;
            default:
                dsyslog("cEncoder::EncodeFrame(): avcodec_send_frame() encode of frame (%d) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rcSend);
                avcodec_flush_buffers(avCodecCtx);
                EncoderStatus.videoEncodeError = true;
                break;
            }
            return false;  // ignore send errors if we only empty encoder
        }
    }
    int rcReceive = avcodec_receive_packet(avCodecCtx, avpkt);
    if (rcReceive < 0) {
        switch (rcReceive) {
        case AVERROR(EAGAIN):
//                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_packet() error EAGAIN at frame %d", ptr_cDecoder->GetFrameNumber());
            stateEAGAIN=true;
            break;
        case AVERROR(EINVAL):
            dsyslog("cEncoder::EncodeFrame(): avcodec_receive_packet() error EINVAL at frame %d", ptr_cDecoder->GetFrameNumber());
            break;
        default:
            dsyslog("cEncoder::EncodeFrame(): avcodec_receive_packet() encode of frame (%d) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rcReceive);
            break;
        }
        return false;
    }
#else
    int frame_ready = 0;
    if (ptr_cDecoder->IsAudioPacket()) {
        int rcEncode = avcodec_encode_audio2(avCodecCtx, avpkt, avFrame, &frame_ready);
        if (rcEncode < 0) {
            dsyslog("cEncoder::EncodeFrame(): avcodec_encode_audio2 of frame (%d) from stream %d failed with return code %i", ptr_cDecoder->GetFrameNumber(), avpkt->stream_index, rcEncode);
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


bool cEncoder::CloseFile(__attribute__((unused)) cDecoder *ptr_cDecoder) {  // unused for libavcodec 56
    int ret = 0;

    // empty all encoder queue
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {
        if (codecCtxArrayOut[streamIndex]) {
            if (codecCtxArrayOut[streamIndex]->codec_type == AVMEDIA_TYPE_SUBTITLE) continue; // draining encoder queue of subtitle stream is not valid, no encoding used
            avcodec_send_frame(codecCtxArrayOut[streamIndex], NULL);  // prevent crash if we have no valid encoder codec context
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
        avpktOut.data            = NULL;
        avpktOut.side_data_elems = 0;
        avpktOut.side_data       = NULL;
        avpktOut.buf             = NULL;

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 12<<8)+100)
        avpktOut.opaque          = NULL;
        avpktOut.opaque_ref      = NULL;
#endif

        while(EncodeFrame(ptr_cDecoder, codecCtxArrayOut[streamIndex], NULL, &avpktOut)) {
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
    FREE(sizeof(int64_t) * avctxIn->nb_streams, "pts_dts_CyclicalOffset");
    free(EncoderStatus.pts_dts_CyclicalOffset);
    FREE(sizeof(int64_t) * avctxIn->nb_streams, "ptsInBefore");
    free(EncoderStatus.ptsInBefore);
    FREE(sizeof(int64_t) * avctxIn->nb_streams, "dtsInBefore");
    free(EncoderStatus.dtsInBefore);
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
        if (pFile == NULL) {
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
