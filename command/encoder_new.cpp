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


cEncoder::cEncoder(bool ac3reencode) {
    ac3ReEncode=ac3reencode;
}


cEncoder::~cEncoder() {
    for (unsigned int i=0; i<avctxOut->nb_streams; i++) {
        avcodec_free_context(&codecCtxArrayOut[i]);
    }

}


bool cEncoder::OpenFile(const char * directory, cDecoder *ptr_cDecoder) {
    int ret = 0;
    char *filename;
    char *CutName;
    char *buffCutName;

    ptr_cDecoder->Reset();
    AVFormatContext *avctxIn = ptr_cDecoder->GetAVFormatContext();
    if (! avctxIn) {
        dsyslog("cEncoder::OpenFile(): failed to get input video context");
        return(false);
    }

    codecCtxArrayOut = (AVCodecContext **) malloc(sizeof(AVCodecContext *) * avctxIn->nb_streams);
    memset(codecCtxArrayOut, 0, sizeof(AVCodecContext *) * avctxIn->nb_streams);

    dts = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    memset(dts, 0, sizeof(int64_t) * avctxIn->nb_streams);
    dtsBefore = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    memset(dtsBefore, 0, sizeof(int64_t) * avctxIn->nb_streams);

    if (asprintf(&buffCutName,"%s", directory)==-1) {
        dsyslog("cEncoder::OpenFile(): failed to allocate string, out of memory?");
        return(false);
    }
    CutName=buffCutName;
    char *tmp = strrchr(CutName, '/');
    if (!tmp) {
        dsyslog("cEncoder::OpenFile(): faild to find last '/'");
        return(false);
    }
    CutName[tmp-CutName]=0;
    tmp = strrchr(CutName, '/')+1;
    if (!tmp) {
        dsyslog("cEncoder::OpenFile(): faild to find last '/'");
        return(false);
    }
    CutName=tmp;
    dsyslog("cEncoder::OpenFile(): CutName '%s'",CutName);

    if (asprintf(&filename,"%s/%s.ts", directory, CutName)==-1) {
        dsyslog("cEncoder::OpenFile(): failed to allocate string, out of memory?");
        return false;
    }
    free(buffCutName);
    dsyslog("cEncoder::OpenFile(): write to '%s'", filename);

#if LIBAVCODEC_VERSION_INT >= ((56<<16)+(26<<8)+100)
    avformat_alloc_output_context2(&avctxOut, NULL, NULL, filename);
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        return(false);
    }
#else  // Raspbian Jessie
    avctxOut = avformat_alloc_context();
    if (!avctxOut) {
        dsyslog("cEncoder::OpenFile(): Could not create output context");
        return(false);
    }
    snprintf(avctxOut->filename, sizeof(avctxOut->filename), "%s", filename);
    AVOutputFormat *avOutputFormat = av_guess_format(NULL, filename, NULL);
    if (!avOutputFormat) {
        dsyslog("cEncoder::OpenFile(): Could not create output format");
        return(false);
    }
    avctxOut->oformat=avOutputFormat;
#endif
    dsyslog("cEncoder::OpenFile(): output format %s", avctxOut->oformat->long_name);

    AVCodecContext **codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
        if (! codecCtxArrayIn) {
            dsyslog("cEncoder::OpenFile(): failed to get input codec context");
            return(false);
        }

    for (unsigned int i = 0; i < avctxIn->nb_streams; i++) {
            bool ret = InitEncoderCodec(ptr_cDecoder, avctxIn, avctxOut, i, codecCtxArrayIn[i]);
            if ( !ret ) {
                dsyslog("cEncoder::OpenFile(): InitEncoderCodec failed");
                return(false);
            }
    }

    ret = avio_open(&avctxOut->pb, filename, AVIO_FLAG_WRITE);
    if (ret < 0) {
        dsyslog("cEncoder::OpenFile(): Could not open output file '%s'", filename);
        return(false);
    }
    ret = avformat_write_header(avctxOut, NULL);
    if (ret < 0) {
        dsyslog("cEncoder::OpenFile(): could not write header");
        return(false);
    }
    free(filename);
    dsyslog("cEncoder::OpenFile(): successful");
    return(true);
}


bool cEncoder::ChangeEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, AVFormatContext *avctxOut, int streamIndex, AVCodecContext *avCodecCtxIn) {
    avcodec_close(codecCtxArrayOut[streamIndex]);
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codecpar->codec_id);
#else
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codec->codec_id);
#endif
    if (!codec) {
        dsyslog("cEncoder::ChangeEncoderCodec(): could nit find encoder for stream %i", streamIndex);
        return(false);
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
    }

    if (avcodec_open2(codecCtxArrayOut[streamIndex], codec, NULL) < 0) {
        dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for stream %i failed", streamIndex);
        avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        codecCtxArrayOut[streamIndex]=NULL;
    }
    else dsyslog("cEncoder::ChangeEncoderCodec(): avcodec_open2 for stream %i successful", streamIndex);
    return(true);
}

bool cEncoder::InitEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, AVFormatContext *avctxOut, int streamIndex, AVCodecContext *avCodecCtxIn) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codecpar->codec_id);
#else
    AVCodec *codec=avcodec_find_encoder(avctxIn->streams[streamIndex]->codec->codec_id);
#endif
    if (!codec) {
        dsyslog("cEncoder::InitEncoderCodec(): could nit find encoder for stream %i", streamIndex);
        return(false);
    }
    dsyslog("cEncoder::InitEncoderCodec(): using decoder id %s for stream %i", codec->long_name,streamIndex);

    AVStream *out_stream = avformat_new_stream(avctxOut, codec);
    if (!out_stream) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed allocating output stream");
        return(false);
    }

    codecCtxArrayOut[streamIndex]=avcodec_alloc_context3(codec);
    if (!codecCtxArrayOut[streamIndex]) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_alloc_context3 failed");
        return(false);
    }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avcodec_parameters_to_context(codecCtxArrayOut[streamIndex],avctxOut->streams[streamIndex]->codecpar) < 0)
#else
    if (avcodec_copy_context(codecCtxArrayOut[streamIndex],avctxOut->streams[streamIndex]->codec) < 0)
#endif
    {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_parameters_to_context failed");
        return(false);
    }
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
        dsyslog("cEncoder::InitEncoderCodec(): codec of stream %i not suported", streamIndex);
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    int ret = avcodec_parameters_copy(avctxOut->streams[streamIndex]->codecpar, avctxIn->streams[streamIndex]->codecpar);
#else
    int ret = avcodec_copy_context(avctxOut->streams[streamIndex]->codec, avctxIn->streams[streamIndex]->codec);
#endif
    if (ret < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): Failed to copy codecpar context from input to output stream");
        return(false);
    }

    if (avcodec_open2(codecCtxArrayOut[streamIndex], codec, NULL) < 0) {
        dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i failed", streamIndex);
        avcodec_free_context(&codecCtxArrayOut[streamIndex]);
        codecCtxArrayOut[streamIndex]=NULL;
    }
    else dsyslog("cEncoder::InitEncoderCodec(): avcodec_open2 for stream %i successful", streamIndex);
    return(true);
}


bool cEncoder::WritePacket(AVPacket *avpktOut, cDecoder *ptr_cDecoder) {
    int ret = 0;
    AVPacket avpktAC3;
    av_init_packet(&avpktAC3);
    avpktAC3.data = NULL;
    avpktAC3.size = 0;

    if (! avctxOut ) {
        dsyslog("cEncoder::WriteFrame(): got no AVFormatContext from Output file");
        return(false);
    }

    if ( avpktOut->pts <  avpktOut->dts ) {
        dsyslog("cEncoder::WritePacket: pts (%" PRId64 ") smaller than dts (%" PRId64 ") in frame (%ld) of stream %d",avpktOut->pts,avpktOut->dts,ptr_cDecoder->GetFrameNumber(),avpktOut->stream_index);
        return(false);
    }

    if (dts[avpktOut->stream_index] == 0) {
        dts[avpktOut->stream_index] = avpktOut->dts;
    }
    else {
        if ((avpktOut->stream_index == 0) && (avpktOut->dts - pts_dts_offset) > dts[avpktOut->stream_index]){
            pts_dts_offset += (avpktOut->dts-pts_dts_offset - dts[avpktOut->stream_index]);
            dsyslog("cEncoder::WritePacket frame (%ld) stream %d new offset: %" PRId64,ptr_cDecoder->GetFrameNumber(),avpktOut->stream_index,pts_dts_offset);
        }
    }

    avpktOut->pts = avpktOut->pts - pts_dts_offset;
    avpktOut->dts = avpktOut->dts - pts_dts_offset;
    avpktOut->pos=-1;   // byte position in stream unknown
   if (dtsBefore[avpktOut->stream_index] >= avpktOut->dts) {  // drop non monotonically increasing dts packets
        dsyslog("cEncoder::WritePacket: non monotonically increasing dts at frame %ld of stream %d, dts last packet %" PRId64 ", dts %" PRId64 ", offset %" PRId64, ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index, dtsBefore[avpktOut->stream_index], avpktOut->dts, avpktOut->dts - dtsBefore[avpktOut->stream_index]);
        return(true);
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (ac3ReEncode && (avctxOut->streams[avpktOut->stream_index]->codecpar->codec_id == AV_CODEC_ID_AC3))
#else
    if (ac3ReEncode && (avctxOut->streams[avpktOut->stream_index]->codec->codec_id == AV_CODEC_ID_AC3))
#endif
    {
        AVFormatContext *avctxIn = ptr_cDecoder->GetAVFormatContext();
        if (!avctxIn) {
            dsyslog("cEncoder::WriteFrame(): failed to get AVFormatContext at frame %ld",ptr_cDecoder->GetFrameNumber());
            return(false);
        }
        AVFrame *avFrameOut = ptr_cDecoder->DecodePacket(avctxIn, avpktOut);
        if ( ! avFrameOut ) {
            dsyslog("cEncoder::WriteFrame(): AC3 Decoder failed at frame %ld",ptr_cDecoder->GetFrameNumber());
            return(false);
        }

        AVCodecContext **codecCtxArrayIn = ptr_cDecoder->GetAVCodecContext();
        if (! codecCtxArrayIn) {
            dsyslog("cEncoder::WriteFrame(): failed to get input codec context");
            if (avFrameOut) av_frame_free(&avFrameOut);
            return(false);
        }
        if (! codecCtxArrayOut[avpktOut->stream_index]) {
           dsyslog("cEncoder::WriteFrame(): Codec Context not found for stream %i", avpktOut->stream_index);
           if (avFrameOut) av_frame_free(&avFrameOut);
           return(false);
        }

        if ((codecCtxArrayOut[avpktOut->stream_index]->channel_layout != codecCtxArrayIn[avpktOut->stream_index]->channel_layout) ||
            (codecCtxArrayOut[avpktOut->stream_index]->channels != codecCtxArrayIn[avpktOut->stream_index]->channels)) {
            dsyslog("cEncoder::WriteFrame(): channel layout of stream %i changed at frame %ld from %" PRIu64 " to %" PRIu64, avpktOut->stream_index,
                    ptr_cDecoder->GetFrameNumber(), codecCtxArrayOut[avpktOut->stream_index]->channel_layout, codecCtxArrayIn[avpktOut->stream_index]->channel_layout);
            dsyslog("cEncoder::WriteFrame(): number of channels of stream %i changed at frame %ld from %i to %i", avpktOut->stream_index,
                    ptr_cDecoder->GetFrameNumber(), codecCtxArrayOut[avpktOut->stream_index]->channels, codecCtxArrayIn[avpktOut->stream_index]->channels);

            bool ret = ChangeEncoderCodec(ptr_cDecoder, avctxIn, avctxOut, avpktOut->stream_index, codecCtxArrayIn[avpktOut->stream_index]);
            if ( !ret ) {
                dsyslog("cEncoder::WriteFrame(): InitEncoderCodec failed");
                if (avFrameOut) av_frame_free(&avFrameOut);
                return(false);
            }
        }
        if ( codecCtxArrayOut[avpktOut->stream_index]) {
            if ( ! this->EncodeFrame(ptr_cDecoder, codecCtxArrayOut[avpktOut->stream_index], avFrameOut, &avpktAC3 )) {
                dsyslog("cEncoder::WriteFrame(): AC3 Encoder failed of stream %i at frame %ld", avpktOut->stream_index, ptr_cDecoder->GetFrameNumber());
                av_packet_unref(&avpktAC3);
                if (avFrameOut) av_frame_free(&avFrameOut);
                return(false);
            }
            else if (avFrameOut) av_frame_free(&avFrameOut);
        }
        else {
            dsyslog("cEncoder::WriteFrame(): encoding of stream %i not supported", avpktOut->stream_index);
            av_packet_unref(&avpktAC3);
            if (avFrameOut) av_frame_free(&avFrameOut);
            return(false);
        }
        // restore packets timestamps and index to fit in recording
        avpktAC3.pts=avpktOut->pts;
        avpktAC3.dts=avpktOut->dts;
        avpktAC3.stream_index=avpktOut->stream_index;
        avpktAC3.pos=-1;   // byte position in stream unknown
        av_write_frame(avctxOut, &avpktAC3);
    }
    else av_write_frame(avctxOut, avpktOut);
    if (ret < 0) {
        dsyslog("cEncoder::WritePacket: Error %i writing frame %ld to stream %i", ret, ptr_cDecoder->GetFrameNumber(), avpktOut->stream_index);
        av_packet_unref(&avpktAC3);
        return(false);
    }
    dtsBefore[avpktOut->stream_index]=avpktOut->dts;
    dts[avpktOut->stream_index] += avpktOut->duration;
    av_packet_unref(&avpktAC3);
    return(true);
}


bool cEncoder::EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodecCtx, AVFrame *avFrameOut, AVPacket *avpktAC3) {
    if (!avCodecCtx) {
        dsyslog("cEncoder::EncodeFrame(): codec context not set");
        return(false);
    }
    int rc = 0;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    rc = avcodec_send_frame(avCodecCtx,avFrameOut);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EAGAIN at frame %li", ptr_cDecoder->GetFrameNumber());
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EINVAL at frame %li", ptr_cDecoder->GetFrameNumber());
                break;
            default:
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame(): decode of frame (%li) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rc);
                break;
        }
    }
    rc = avcodec_receive_packet(avCodecCtx,avpktAC3);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EAGAIN at frame %li", ptr_cDecoder->GetFrameNumber());
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame error EINVAL at frame %li", ptr_cDecoder->GetFrameNumber());
                break;
            default:
                dsyslog("cEncoder::EncodeFrame(): avcodec_receive_frame: decode of frame (%li) failed with return code %i", ptr_cDecoder->GetFrameNumber(), rc);
                break;
        }
        if (avFrameOut) av_frame_free(&avFrameOut);
        return(false);
    }

#else
    int frame_ready=0;
    if (ptr_cDecoder->isAudioPacket()) {
        rc=avcodec_encode_audio2(avCodecCtx, avpktAC3, avFrameOut, &frame_ready);
        if (rc < 0) {
            dsyslog("cEncoder::EncodeFrame(): avcodec_encode_audio2 of frame (%li) from stream %i failed with return code %i", ptr_cDecoder->GetFrameNumber(), avpktAC3->stream_index, rc);
            if (avFrameOut) av_frame_free(&avFrameOut);
            return(false);
        }
    }
    else {
       dsyslog("cEncoder::EncodeFrame(): packet type of stream %i not supported", avpktAC3->stream_index);
       if (avFrameOut) av_frame_free(&avFrameOut);
       return(false);
    }
#endif
    return(true);
}


bool cEncoder::CloseFile() {
    int ret = 0;

    ret=av_write_trailer(avctxOut);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not write trailer");
        return(false);
    }
    ret=avio_closep(&avctxOut->pb);
    if (ret < 0) {
        dsyslog("cEncoder::CloseFile(): could not close file");
        return(false);
    }
    return(true);
}
