/*
 * encoder_new.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "decoder_new.h"
#include "encoder_new.h"

extern "C"{
#include "debug.h"
}


cAC3VolumeFilter::cAC3VolumeFilter() {
}


cAC3VolumeFilter::~cAC3VolumeFilter() {
    FREE(sizeof(*filterGraph), "filterGraph");
    avfilter_graph_free(&filterGraph);
}


bool cAC3VolumeFilter::Init(const uint64_t channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate){
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
    av_get_channel_layout_string(ch_layout, sizeof(ch_layout), 0, (int64_t) channel_layout);
    av_opt_set(filterSrc, "channel_layout", ch_layout, AV_OPT_SEARCH_CHILDREN);
    av_opt_set(filterSrc, "sample_fmt", av_get_sample_fmt_name(sample_fmt), AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(filterSrc, "time_base", (AVRational){ 1, sample_rate}, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(filterSrc, "sample_rate", sample_rate, AV_OPT_SEARCH_CHILDREN);
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
    int err = 0;
// Send the frame to the input of the filtergraph
    err = av_buffersrc_add_frame(filterSrc, avFrame);
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



cEncoder::cEncoder(int threads, const bool ac3reencode) {
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    dsyslog("cEncoder::cEncoder(): init with %i threads", threads);
    threadCount = threads;
    ac3ReEncode=ac3reencode;
}


cEncoder::~cEncoder() {
    for (unsigned int i = 0; i < avctxOut->nb_streams; i++) {
        if (ptr_cAC3VolumeFilter[i]) {
            FREE(sizeof(*ptr_cAC3VolumeFilter[i]), "ptr_cAC3VolumeFilter");
            delete ptr_cAC3VolumeFilter[i];
        }
    }
    for (unsigned int streamIndex = 0; streamIndex < avctxOut->nb_streams; streamIndex++) {
        if (codecCtxArrayOut[streamIndex]) {
            FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
            avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        }
    }
    FREE(sizeof(AVCodecContext *) * nb_streamsIn, "codecCtxArrayOut");
    free(codecCtxArrayOut);
    FREE(sizeof(int64_t) * nb_streamsIn, "pts_dts_CyclicalOffset");
    free(pts_dts_CyclicalOffset);
    FREE(sizeof(int64_t) * nb_streamsIn, "dtsOut");
    free(dtsOut);
    FREE(sizeof(int64_t) * nb_streamsIn, "dtsBefore");
    free(dtsBefore);

    if (avctxOut) {
        FREE(sizeof(*avctxOut), "avctxOut");
        avformat_free_context(avctxOut);
    }
}


bool cEncoder::OpenFile(const char * directory, cDecoder *ptr_cDecoder) {
    if (!directory) return false;
    if (!ptr_cDecoder) return false;
    int ret = 0;
    char *filename = NULL;
    char *buffCutName;

    ptr_cDecoder->Reset();
    AVFormatContext *avctxIn = ptr_cDecoder->GetAVFormatContext();
    if (! avctxIn) {
        dsyslog("cEncoder::OpenFile(): failed to get input video context");
        return false;
    }

    nb_streamsIn = avctxIn->nb_streams;   // needed from desctrucor
    codecCtxArrayOut = (AVCodecContext **) malloc(sizeof(AVCodecContext *) * avctxIn->nb_streams);
    ALLOC(sizeof(AVCodecContext *) * avctxIn->nb_streams, "codecCtxArrayOut");
    memset(codecCtxArrayOut, 0, sizeof(AVCodecContext *) * avctxIn->nb_streams);

    pts_dts_CyclicalOffset = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "pts_dts_CyclicalOffset");
    memset(pts_dts_CyclicalOffset, 0, sizeof(int64_t) * avctxIn->nb_streams);

    dtsOut = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "dtsOut");
    memset(dtsOut, 0, sizeof(int64_t) * avctxIn->nb_streams);

    dtsBefore = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    ALLOC(sizeof(int64_t) * avctxIn->nb_streams, "dtsBefore");
    memset(dtsBefore, 0, sizeof(int64_t) * avctxIn->nb_streams);

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
        dsyslog("cEncoder::OpenFile(): faild to find last '/'");
        FREE(strlen(buffCutName)+1, "buffCutName");
        free(buffCutName);
        return false;
    }
    *datePart = 0;    // cut off date part

    char *cutName = strrchr(buffCutName, '/');
    if (!cutName) {
        dsyslog("cEncoder::OpenFile(): faild to find last '/'");
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

#if LIBAVCODEC_VERSION_INT >= ((56<<16)+(26<<8)+100)
    avformat_alloc_output_context2(&avctxOut, NULL, NULL, filename);
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    ALLOC(sizeof(*avctxOut), "avctxOut");
#else  // Raspbian Jessie
    avctxOut = avformat_alloc_context();
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    ALLOC(sizeof(*avctxOut), "avctxOut");
    snprintf(avctxOut->filename, sizeof(avctxOut->filename), "%s", filename);
    AVOutputFormat *avOutputFormat = av_guess_format(NULL, filename, NULL);
    if (!avOutputFormat) {
        dsyslog("cEncoder::OpenFile(): Could not create output format");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }
    avctxOut->oformat=avOutputFormat;
#endif
    dsyslog("cEncoder::OpenFile(): output format %s", avctxOut->oformat->long_name);

    AVCodecContext **codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
    if (! codecCtxArrayIn) {
        dsyslog("cEncoder::OpenFile(): failed to get input codec context");
        FREE(strlen(filename)+1, "filename");
        free(filename);
        return false;
    }

    for (unsigned int i = 0; i < avctxIn->nb_streams; i++) {
        if (!codecCtxArrayIn[i]) break;   // if we have no input codec we can not decode and encode this stream
        ret = InitEncoderCodec(ptr_cDecoder, avctxIn, avctxOut, i, codecCtxArrayIn[i]);
        if ( !ret ) {
            dsyslog("cEncoder::OpenFile(): InitEncoderCodec failed");
            FREE(strlen(filename)+1, "filename");
            free(filename);
            return false;
        }
    }

    ret = avio_open(&avctxOut->pb, filename, AVIO_FLAG_WRITE);
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


bool cEncoder::ChangeEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, const unsigned int streamIndex, AVCodecContext *avCodecCtxIn) {
    if(!ptr_cDecoder) return false;
    if (!avctxIn) return false;
    if (streamIndex >= avctxIn->nb_streams) {
        dsyslog("cEncoder::ChangeEncoderCodec(): streamindex %d out of range", streamIndex);
        return false;
    }
    if (!avCodecCtxIn) return false;


    avcodec_close(codecCtxArrayOut[streamIndex]);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codecpar->codec_id);
#else
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codec->codec_id);
#endif
    if (!codec) {
        dsyslog("cEncoder::ChangeEncoderCodec(): could not find encoder for stream %i", streamIndex);
        return false;
    }
    dsyslog("cEncoder::ChangeEncoderCodec(): using decoder id %s for stream %i", codec->long_name,streamIndex);

    codecCtxArrayOut[streamIndex]->time_base.num =  avCodecCtxIn->time_base.num;
    codecCtxArrayOut[streamIndex]->time_base.den = avCodecCtxIn->time_base.den;
    if (ptr_cDecoder->isVideoStream(streamIndex)) {
        codecCtxArrayOut[streamIndex]->pix_fmt = avCodecCtxIn->pix_fmt;
        codecCtxArrayOut[streamIndex]->height = avCodecCtxIn->height;
        codecCtxArrayOut[streamIndex]->width = avCodecCtxIn->width;
    }
    else if (ptr_cDecoder->isAudioStream(streamIndex)) {
        codecCtxArrayOut[streamIndex]->sample_fmt = avCodecCtxIn->sample_fmt;
        codecCtxArrayOut[streamIndex]->channel_layout = avCodecCtxIn->channel_layout;
        codecCtxArrayOut[streamIndex]->sample_rate = avCodecCtxIn->sample_rate;
        codecCtxArrayOut[streamIndex]->channels = avCodecCtxIn->channels;
    }
    else {
        dsyslog("cEncoder::ChangeEncoderCodec():odec of stream %i not suported", streamIndex);
        return false;
    }
    codecCtxArrayOut[streamIndex]->thread_count = threadCount;
    if (avcodec_open2(codecCtxArrayOut[streamIndex], codec, NULL) < 0) {
        dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for stream %i failed", streamIndex);
        FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        codecCtxArrayOut[streamIndex]=NULL;
        return false;
    }
    dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for stream %i successful", streamIndex);

    if (ptr_cDecoder->isAudioAC3Stream(streamIndex)) {
        dsyslog("cEncoder::ChangeEncoderCodec(): AC3 stream found %i, re-initialize volume filter", streamIndex);
        if (!ptr_cAC3VolumeFilter[streamIndex]) {
            dsyslog("cEncoder::ChangeEncoderCodec(): ptr_cAC3VolumeFilter not initialized for stream %i", streamIndex);
            return false;
        }
        FREE(sizeof(*ptr_cAC3VolumeFilter[streamIndex]), "ptr_cAC3VolumeFilter");
        delete ptr_cAC3VolumeFilter[streamIndex];
        ptr_cAC3VolumeFilter[streamIndex] = new cAC3VolumeFilter();
        ALLOC(sizeof(*ptr_cAC3VolumeFilter[streamIndex]), "ptr_cAC3VolumeFilter");
        if (!ptr_cAC3VolumeFilter[streamIndex]->Init(avCodecCtxIn->channel_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate)) {
            dsyslog("cEncoder::ChangeEncoderCodec(): ptr_cAC3VolumeFilter->Init() failed");
            return false;
        }
    }
    return true;
}


bool cEncoder::InitEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, AVFormatContext *avctxOut, const unsigned int streamIndex, AVCodecContext *avCodecCtxIn) {
    if (!ptr_cDecoder) return false;
    if (!avctxIn) return false;
    if (!avctxOut) return false;
    if (streamIndex >= avctxIn->nb_streams) {
        dsyslog("cEncoder::InitEncoderCodec(): streamindex %d out of range", streamIndex);
        return false;
    }
    if (!avCodecCtxIn) {
        dsyslog("cEncoder::InitEncoderCodec(): no input codec set for streamindex %d", streamIndex);
        return false;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    AVCodecID codec_id = avctxIn->streams[streamIndex]->codecpar->codec_id;
    AVCodec *codec=avcodec_find_encoder(codec_id);
#else
    AVCodecID codec_id = avctxIn->streams[streamIndex]->codec->codec_id;
    AVCodec *codec=avcodec_find_encoder(codec_id);
#endif
    if (!codec) {
        if (codec_id == 94215) { // libavcodec does not support Libzvbi DVB teletext encoder, encode without this stream
            dsyslog("cEncoder::InitEncoderCodec(): Libzvbi DVB teletext for stream %i codec id %i not supported, ignoring this stream", streamIndex, codec_id);
            return true;
        }
        dsyslog("cEncoder::InitEncoderCodec(): could not find encoder for stream %i codec id %i", streamIndex, codec_id);
        return false;
    }
    dsyslog("cEncoder::InitEncoderCodec(): using encoder id %d '%s' for stream %i", codec_id, codec->long_name, streamIndex);

    AVStream *out_stream = avformat_new_stream(avctxOut, codec);
    if (!out_stream) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed allocating output stream");
        return false;
    }

    codecCtxArrayOut[streamIndex]=avcodec_alloc_context3(codec);
    if (!codecCtxArrayOut[streamIndex]) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_alloc_context3 failed");
        return false;
    }
    ALLOC(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (avcodec_parameters_to_context(codecCtxArrayOut[streamIndex],avctxOut->streams[streamIndex]->codecpar) < 0)
#else
    if (avcodec_copy_context(codecCtxArrayOut[streamIndex],avctxOut->streams[streamIndex]->codec) < 0)
#endif
    {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_parameters_to_context failed");
        return false;
    }

    if (ptr_cDecoder->isVideoStream(streamIndex)) {
        dsyslog("cEncoder::InitEncoderCodec(): input codec real framerate %d/%d for stream %d", avctxIn->streams[streamIndex]->r_frame_rate.num, avctxIn->streams[streamIndex]->r_frame_rate.den, streamIndex);
        codecCtxArrayOut[streamIndex]->time_base.num = avctxIn->streams[streamIndex]->r_frame_rate.den;  // time_base = 1 / framerate
        codecCtxArrayOut[streamIndex]->time_base.den = avctxIn->streams[streamIndex]->r_frame_rate.num;
        codecCtxArrayOut[streamIndex]->pix_fmt = avCodecCtxIn->pix_fmt;
        codecCtxArrayOut[streamIndex]->height = avCodecCtxIn->height;
        codecCtxArrayOut[streamIndex]->width = avCodecCtxIn->width;
    }
    else {
        if (ptr_cDecoder->isAudioStream(streamIndex)) {
            dsyslog("cEncoder::InitEncoderCodec(): input codec sample rate %d, timebase %d/%d for stream %d", avCodecCtxIn->sample_rate, avCodecCtxIn->time_base.num, avCodecCtxIn->time_base.den, streamIndex);
            codecCtxArrayOut[streamIndex]->time_base.num = avCodecCtxIn->time_base.num;
            codecCtxArrayOut[streamIndex]->time_base.den = avCodecCtxIn->time_base.den;
            codecCtxArrayOut[streamIndex]->sample_fmt = avCodecCtxIn->sample_fmt;
            codecCtxArrayOut[streamIndex]->channel_layout = avCodecCtxIn->channel_layout;
            codecCtxArrayOut[streamIndex]->sample_rate = avCodecCtxIn->sample_rate;
            codecCtxArrayOut[streamIndex]->channels = avCodecCtxIn->channels;
        }
        else {
            dsyslog("cEncoder::InitEncoderCodec(): codec of stream %i not audio or video, ignoring", streamIndex);
            return true;
        }
    }
    if (codecCtxArrayOut[streamIndex]->time_base.num == 0) {
        dsyslog("cEncoder::InitEncoderCodec(): output timebase %d/%d not valid", codecCtxArrayOut[streamIndex]->time_base.num, codecCtxArrayOut[streamIndex]->time_base.den);
        return false;
    }


#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    int ret = avcodec_parameters_copy(avctxOut->streams[streamIndex]->codecpar, avctxIn->streams[streamIndex]->codecpar);
#else
    int ret = avcodec_copy_context(avctxOut->streams[streamIndex]->codec, avctxIn->streams[streamIndex]->codec);
#endif
    if (ret < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed to copy codecpar context from input to output stream");
        return false;
    }

    codecCtxArrayOut[streamIndex]->thread_count = threadCount;
    if (avcodec_open2(codecCtxArrayOut[streamIndex], codec, NULL) < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i failed", streamIndex);
        FREE(sizeof(*codecCtxArrayOut[streamIndex]), "codecCtxArrayOut[streamIndex]");
        avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        codecCtxArrayOut[streamIndex]=NULL;
    }
    else dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i successful", streamIndex);

    if (ptr_cDecoder->isAudioAC3Stream(streamIndex)) {
        dsyslog("cEncoder::InitEncoderCodec(): AC3 stream found %i, initialize volume filter", streamIndex);
        if (ptr_cAC3VolumeFilter[streamIndex]) {
            dsyslog("cEncoder::InitEncoderCodec(): ptr_cAC3VolumeFilter is not NULL for stream %i", streamIndex);
            return false;
        }
        ptr_cAC3VolumeFilter[streamIndex] = new cAC3VolumeFilter();
        ALLOC(sizeof(*ptr_cAC3VolumeFilter[streamIndex]), "ptr_cAC3VolumeFilter");
        if (!ptr_cAC3VolumeFilter[streamIndex]->Init(avCodecCtxIn->channel_layout, avCodecCtxIn->sample_fmt, avCodecCtxIn->sample_rate)) {
            dsyslog("cEncoder::InitEncoderCodec(): ptr_cAC3VolumeFilter->Init() failed");
            return false;
        }
    }
    return true;
}


bool cEncoder::WritePacket(AVPacket *avpktOut, cDecoder *ptr_cDecoder) {
    if (!avctxOut ) {
        dsyslog("cEncoder::WritePacket(): got no AVFormatContext from output file");
        return false;
    }
    if (!ptr_cDecoder ) {
        dsyslog("cEncoder::WritePacket(): got no ptr_cDecoder from output file");
        return false;
    }
    if ((unsigned int) avpktOut->stream_index >= avctxOut->nb_streams) return true;

    AVPacket avpktAC3;
    av_init_packet(&avpktAC3);
    AVFrame *avFrame = NULL;
    avpktAC3.data = NULL;
    avpktAC3.size = 0;
    if (avpktOut->dts == AV_NOPTS_VALUE) {
         dsyslog("cEncoder::WritePacket(): frame (%d) got no dts value from input stream %d", ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index);
         return false;
    }
    int64_t cyclicalTest = dtsBefore[avpktOut->stream_index] + avpktOut->duration - avpktOut->dts - 0x200000000; // >=0 on cyclical
    int max_diff = -3 * avpktOut->duration;  // tolerance for some missed frames
    if (max_diff == 0) max_diff = -400000; // subtitle has no duration
    if (cyclicalTest >= max_diff) {
        pts_dts_CyclicalOffset[avpktOut->stream_index] += 0x200000000;
        dsyslog("cEncoder::WritePacket(): dts and pts cyclicle in stream %d at frame (%d), offset now 0x%lX", avpktOut->stream_index, ptr_cDecoder->GetFrameNumber(), pts_dts_CyclicalOffset[avpktOut->stream_index]);
    }

    if (avpktOut->pts <  avpktOut->dts) {
        dsyslog("cEncoder::WritePacket(): pts (%" PRId64 ") smaller than dts (%" PRId64 ") in frame (%d) of stream %d",avpktOut->pts,avpktOut->dts,ptr_cDecoder->GetFrameNumber(),avpktOut->stream_index);
        return false;
    }

    if (dtsOut[avpktOut->stream_index] == 0) {
        dtsOut[avpktOut->stream_index] = avpktOut->dts;   // initalize start value
    }
    else {
        if ((avpktOut->stream_index == 0) && ((avpktOut->dts - pts_dts_CutOffset) > dtsOut[avpktOut->stream_index])){
            int64_t newOffset = avpktOut->dts - pts_dts_CutOffset - dtsOut[avpktOut->stream_index];
            int newOffsetMin = static_cast<int> (newOffset*av_q2d(avctxOut->streams[avpktOut->stream_index]->time_base) / 60);
            if (newOffset > 1) dsyslog("cEncoder::WritePacket(): frame (%d) stream %d old offset: %" PRId64 " increase %" PRId64 " = %dmin", ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index, pts_dts_CutOffset, newOffset, newOffsetMin);
            if (newOffsetMin > 100) {
                dsyslog("cEncoder::WritePacket(): dts value not valid, ignoring");
                return false;
            }
            ptsAfterCut = avpktOut->pts;
            pts_dts_CutOffset += newOffset;
            if (newOffset > 1) {  // ignore very small offsets, that is not a cut
                ptsBeforeCut = ptsBefore;
                dsyslog("cEncoder::WritePacket(): frame (%d) stream %d new offset: %" PRId64,ptr_cDecoder->GetFrameNumber(),avpktOut->stream_index,pts_dts_CutOffset);
            }
        }
    }

    if ((avpktOut->pts > ptsBeforeCut) && (avpktOut->pts < ptsAfterCut)) {  // after cut ignore audio frames with smaller PTS than video frame
        dsyslog("cEncoder::WritePacket(): audio pts %ld is %ld smaller than video cut pts (%ld) at frame (%d) of stream %d", avpktOut->pts, ptsAfterCut - avpktOut->pts, ptsAfterCut, ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index);
        return true;
    }

    avpktOut->pts = avpktOut->pts - pts_dts_CutOffset + pts_dts_CyclicalOffset[avpktOut->stream_index];
    avpktOut->dts = avpktOut->dts - pts_dts_CutOffset + pts_dts_CyclicalOffset[avpktOut->stream_index];
    avpktOut->pos=-1;   // byte position in stream unknown
    if (dtsBefore[avpktOut->stream_index] >= avpktOut->dts) {  // drop non monotonically increasing dts packets
        dsyslog("cEncoder::WritePacket(): non monotonically increasing dts at frame (%6d) of stream %d, dts last packet %10" PRId64 ", dts offset %" PRId64, ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index, dtsBefore[avpktOut->stream_index], avpktOut->dts - dtsBefore[avpktOut->stream_index]);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)  // yavdr Xenial ffmpeg
        dsyslog("cEncoder::WritePacket():                                                                 dts this packet %10" PRId64 ", cyclical test %" PRId64 " duration %" PRId64 , avpktOut->dts, cyclicalTest, avpktOut->duration);
#else
        dsyslog("cEncoder::WritePacket():                                                                 dts this packet %10" PRId64 ", cyclical test %" PRId64 " duration %d", avpktOut->dts, cyclicalTest, avpktOut->duration);
#endif
        return true;
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    if (ac3ReEncode && (avctxOut->streams[avpktOut->stream_index]->codecpar->codec_id == AV_CODEC_ID_AC3))
#else
    if (ac3ReEncode && (avctxOut->streams[avpktOut->stream_index]->codec->codec_id == AV_CODEC_ID_AC3))
#endif
    {
        AVFormatContext *avctxIn = ptr_cDecoder->GetAVFormatContext();
        if (!avctxIn) {
            dsyslog("cEncoder::WritePacket(): failed to get AVFormatContext at frame %d",ptr_cDecoder->GetFrameNumber());
            return false;
        }

// decode packet
        avFrame = ptr_cDecoder->DecodePacket(avctxIn, avpktOut);
        if (!avFrame) {
            dsyslog("cEncoder::WritePacket(): AC3 Decoder failed at frame %d",ptr_cDecoder->GetFrameNumber());
            return false;
        }

        AVCodecContext **codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
        if (!codecCtxArrayIn) {
            dsyslog("cEncoder::WritePacket(): failed to get input codec context");
            return false;
        }
        if (! codecCtxArrayOut[avpktOut->stream_index]) {
           dsyslog("cEncoder::WritePacket(): Codec Context not found for stream %d", avpktOut->stream_index);
           return false;
        }

// Check if the audio strem changed channels
        if ((codecCtxArrayOut[avpktOut->stream_index]->channel_layout != codecCtxArrayIn[avpktOut->stream_index]->channel_layout) ||
            (codecCtxArrayOut[avpktOut->stream_index]->channels != codecCtxArrayIn[avpktOut->stream_index]->channels)) {
            dsyslog("cEncoder::WritePacket(): channel layout of stream %d changed at frame %d from %" PRIu64 " to %" PRIu64, avpktOut->stream_index,
                    ptr_cDecoder->GetFrameNumber(), codecCtxArrayOut[avpktOut->stream_index]->channel_layout, codecCtxArrayIn[avpktOut->stream_index]->channel_layout);
            dsyslog("cEncoder::WritePacket(): number of channels of stream %d changed at frame %d from %d to %d", avpktOut->stream_index,
                    ptr_cDecoder->GetFrameNumber(), codecCtxArrayOut[avpktOut->stream_index]->channels, codecCtxArrayIn[avpktOut->stream_index]->channels);

            if( !ChangeEncoderCodec(ptr_cDecoder, avctxIn, avpktOut->stream_index, codecCtxArrayIn[avpktOut->stream_index])) {
                dsyslog("cEncoder::WritePacket(): InitEncoderCodec failed");
                return false;
            }
        }

// use filter to adapt AC3 volume
        if (ptr_cDecoder->isAudioAC3Stream(avpktOut->stream_index)) {
            if (!ptr_cAC3VolumeFilter[avpktOut->stream_index]->SendFrame(avFrame)) {
                dsyslog("cEncoder::WriteFrame(): cAC3VolumeFilter SendFrame failed");
                return false;
            }
            if (!ptr_cAC3VolumeFilter[avpktOut->stream_index]->GetFrame(avFrame)) {
                dsyslog("cEncoder::WriteFrame(): cAC3VolumeFilter GetFrame failed");
                return false;
            }
        }

//encode frame
        if ( codecCtxArrayOut[avpktOut->stream_index]) {
            if ( ! this->EncodeFrame(ptr_cDecoder, codecCtxArrayOut[avpktOut->stream_index], avFrame, &avpktAC3 )) {
                dsyslog("cEncoder::WritePacket(): AC3 Encoder failed of stream %d at frame %d", avpktOut->stream_index, ptr_cDecoder->GetFrameNumber());
                av_packet_unref(&avpktAC3);
                return false;
            }
        }
        else {
            dsyslog("cEncoder::WritePacket(): encoding of stream %d not supported", avpktOut->stream_index);
            av_packet_unref(&avpktAC3);
            return false;
        }
        // restore packets timestamps and index to fit in recording
        avpktAC3.pts=avpktOut->pts;
        avpktAC3.dts=avpktOut->dts;
        avpktAC3.stream_index=avpktOut->stream_index;
        avpktAC3.pos=-1;   // byte position in stream unknown
        av_write_frame(avctxOut, &avpktAC3);
    }
    else av_write_frame(avctxOut, avpktOut);

    if (avpktOut->stream_index == 0) ptsBefore=avpktOut->pts;
    dtsBefore[avpktOut->stream_index]=avpktOut->dts - pts_dts_CyclicalOffset[avpktOut->stream_index];
    dtsOut[avpktOut->stream_index] += avpktOut->duration;
    av_packet_unref(&avpktAC3);
    return true;
}


bool cEncoder::EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodecCtx, AVFrame *avFrame, AVPacket *avpktAC3) {
    if (!ptr_cDecoder) return false;
    if (!avCodecCtx) {
        dsyslog("cEncoder::EncodeFrame(): codec context not set");
        return false;
    }
    if (!avFrame) return false;
    if (!avpktAC3) return false;

    int rc = 0;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(64<<8)+101)
    rc = avcodec_send_frame(avCodecCtx,avFrame);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EAGAIN at frame %d", ptr_cDecoder->GetFrameNumber());
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EINVAL at frame %d", ptr_cDecoder->GetFrameNumber());
                break;
            default:
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame(): decode of frame (%d) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rc);
                break;
        }
    }
    rc = avcodec_receive_packet(avCodecCtx,avpktAC3);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EAGAIN at frame %d", ptr_cDecoder->GetFrameNumber());
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EINVAL at frame %d", ptr_cDecoder->GetFrameNumber());
                break;
            default:
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame: decode of frame (%d) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rc);
                break;
        }
        return false;
    }

#else
    int frame_ready=0;
    if (ptr_cDecoder->isAudioPacket()) {
        rc=avcodec_encode_audio2(avCodecCtx, avpktAC3, avFrame, &frame_ready);
        if (rc < 0) {
            dsyslog("cEncoder::EncodeFrame(): avcodec_encode_audio2 of frame (%d) from stream %d failed with return code %i", ptr_cDecoder->GetFrameNumber(), avpktAC3->stream_index, rc);
            return false;
        }
    }
    else {
       dsyslog("cEncoder::EncodeFrame(): packet type of stream %d not supported", avpktAC3->stream_index);
       return false;
    }
#endif
    return true;
}


bool cEncoder::CloseFile() {
    int ret = 0;

    ret=av_write_trailer(avctxOut);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not write trailer");
        return false;
    }
    ret=avio_closep(&avctxOut->pb);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not close file");
        return false;
    }
    return true;
}
