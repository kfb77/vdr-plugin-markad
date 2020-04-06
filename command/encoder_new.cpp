#include "decoder_new.h"
#include "encoder_new.h"

extern "C"{
#include "debug.h"
}


cEncoder::cEncoder() {
}


cEncoder::~cEncoder() {
}


bool cEncoder::OpenFile(const char * directory, AVFormatContext *avctxIn) {
    int ret = 0;
    char *filename;
    char *CutName;
    char *buffCutName;

    dts = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    memset(dts, 0, sizeof(int64_t) * avctxIn->nb_streams);
    dtsBefore = (int64_t *) malloc(sizeof(int64_t) * avctxIn->nb_streams);
    memset(dtsBefore, 0, sizeof(int64_t) * avctxIn->nb_streams);

    if (asprintf(&buffCutName,"%s", directory)==-1) {
        esyslog("cEncoder::OpenFile: failed to allocate string, out of memory?");
        return false;
    }
    CutName=buffCutName;
    char *tmp = strrchr(CutName, '/');
    if (!tmp) {
        esyslog("cEncoder::OpenFile: faild to find last '/'");
        return(false);
    }
    CutName[tmp-CutName]=0;
    tmp = strrchr(CutName, '/')+1;
    if (!tmp) {
        esyslog("cEncoder::OpenFile: faild to find last '/'");
        return(false);
    }
    CutName=tmp;
    dsyslog("cEncoder::OpenFile: CutName '%s'",CutName);

    if (asprintf(&filename,"%s/%s.ts", directory, CutName)==-1) {
        esyslog("cEncoder::OpenFile: failed to allocate string, out of memory?");
        return false;
    }
    free(buffCutName);
    dsyslog("cEncoder::OpenFile: write to '%s'", filename);

    if (! avctxIn ) {
        dsyslog("cEncoder::OpenFile: got no AVFormatContext from Input file");
        return(false);
    }
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

    for (unsigned int i = 0; i < avctxIn->nb_streams; i++) {
        AVStream *in_stream = avctxIn->streams[i];
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        AVCodec *codec=avcodec_find_encoder(in_stream->codecpar->codec_id);
#else
        AVCodec *codec=avcodec_find_encoder(in_stream->codec->codec_id);
#endif
        if (!codec) {
            esyslog(" cEncoder::OpenFile: could nit find encoder for stream");
            return(false);
        }
        dsyslog("cEncoder::OpenFile: using decoder %s for stream %i",codec->long_name,i);
        AVStream *out_stream = avformat_new_stream(avctxOut, codec);
        if (!out_stream) {
            dsyslog("cEncoder::OpenFile: Failed allocating output stream");
            return(false);
        }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
#else
        ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
#endif
        if (ret < 0) {
            dsyslog("cEncoder::OpenFile: Failed to copy codecpar context from input to output stream");
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
        dsyslog("cEncoder::OpenFile(): avformat_write_header() could not write header");
        return(false);
    }
    free(filename);
    dsyslog("cEncoder::OpenFile(): successful");
    return(true);
}


bool cEncoder::WritePacket(AVPacket *pkt, cDecoder *ptr_cDecoder) {
    int ret = 0;
    if (! avctxOut ) {
        dsyslog("cEncoder::WriteFrame: got no AVFormatContext from Output file");
        return(false);
    }

    if (dts[pkt->stream_index] == 0) {
        dts[pkt->stream_index] = pkt->dts;
    }
    else {
        if ((pkt->stream_index == 0) && (pkt->dts - pts_dts_offset) > dts[pkt->stream_index]){
            pts_dts_offset += (pkt->dts-pts_dts_offset - dts[pkt->stream_index]);
//            dsyslog("cEncoder::WritePacket frame (%ld) stream %d new offset: %ld",ptr_cDecoder->GetFrameNumber(),pkt->stream_index,pts_dts_offset);
            dsyslog("cEncoder::WritePacket frame (%ld) stream %d new offset: %" PRId64,ptr_cDecoder->GetFrameNumber(),pkt->stream_index,pts_dts_offset);
        }
    }
    pkt->pts = pkt->pts - pts_dts_offset;
    pkt->dts = pkt->dts - pts_dts_offset;
    pkt->pos=-1;   // byte position in stream unknown
    if ( pkt->pts <  pkt->dts ) {
        dsyslog("cEncoder::WritePacket: pts (%" PRId64 ") smaller than dts (%" PRId64 ") it frame (%ld) in stream %d",pkt->pts,pkt->dts,ptr_cDecoder->GetFrameNumber(),pkt->stream_index);
        return(false);
    }
    if (dtsBefore[pkt->stream_index] >= pkt->dts) {  // drop non monotonically increasing dts packets
        tsyslog("cEncoder::WritePacket: non monotonically increasing dts at stream %d, dts last packet %" PRId64 ", dts %" PRId64 ", offset %" PRId64, pkt->stream_index, dtsBefore[pkt->stream_index], pkt->dts, pkt->dts - dtsBefore[pkt->stream_index]);
        return(true);
    }
    dtsBefore[pkt->stream_index]=pkt->dts;
    ret = av_write_frame(avctxOut, pkt);
    if (ret < 0) {
        dsyslog("cEncoder::WritePacket: Error %i writing packet", ret);
        return(false);
    }
    dts[pkt->stream_index] += pkt->duration;
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
