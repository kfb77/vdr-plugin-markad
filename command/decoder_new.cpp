/*
 * decoder_new.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "decoder_new.h"
extern "C"{
#include "debug.h"
}


cDecoder::cDecoder(int threads) {
    av_init_packet(&avpkt);
    codec = NULL;
    if (threads < 1) threads = 1;
    if (threads > 16) threads = 16;
    dsyslog("cDecoder::cDecoder(): init with %i threads", threads);
    threadCount = threads;
}


cDecoder::~cDecoder() {
    av_packet_unref(&avpkt);
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
        avcodec_free_context(&codecCtxArray[i]);
    }
    dsyslog("cDecoder::~cDecoder(): close avformat context");
    avformat_close_input(&avctx);
    free(recordingDir);
}


bool cDecoder::DecodeDir(const char * recDir) {
    if (!recDir) return false;
    char *filename;
    if ( ! recordingDir ) {
        if (asprintf(&recordingDir,"%s",recDir)==-1) {
            dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
            return false;
        }
    }
    fileNumber++;
    if (asprintf(&filename,"%s/%05i.ts",recDir,fileNumber)==-1) {
        dsyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
        return false;
    }
    return this->DecodeFile(filename);
    free(filename);
}


void cDecoder::Reset(){
    fileNumber=0;
    framenumber=0;
    msgGetFrameInfo=false;
}


AVFormatContext *cDecoder::GetAVFormatContext() {
    return avctx;
}


AVCodecContext **cDecoder::GetAVCodecContext() {
    return(codecCtxArray);
}


bool cDecoder::DecodeFile(const char * filename) {
    AVFormatContext *avctxNextFile = NULL;
    if (!filename) return false;
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    if (avformat_open_input(&avctxNextFile, filename, NULL, NULL) == 0) {
        dsyslog("cDecoder::DecodeFile(): decode file %s",filename);
        if (avctx) avformat_close_input(&avctx);
        avctx = avctxNextFile;
    }
    else {
        if (fileNumber <= 1) dsyslog("cDecoder::DecodeFile(): Could not open source file %s", filename);
        return(false);
    }
    if (avformat_find_stream_info(avctx, NULL) <0) {
        dsyslog("cDecoder::DecodeFile(): Could not get stream infos %s", filename);
        return(false);
    }

    if (codecCtxArray) {
        for (unsigned int i=0; i<avctx->nb_streams; i++) {
            avcodec_free_context(&codecCtxArray[i]);
        }
    }

    codecCtxArray = (AVCodecContext **) malloc(sizeof(AVCodecContext *) * avctx->nb_streams);
    memset(codecCtxArray, 0, sizeof(AVCodecContext *) * avctx->nb_streams);
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        codec=avcodec_find_decoder(avctx->streams[i]->codecpar->codec_id);
#else
        codec=avcodec_find_decoder(avctx->streams[i]->codec->codec_id);
#endif
        if (!codec) {
            dsyslog("cDecoder::DecodeFile(): could nit find decoder for stream");
            return(false);
        }
        if (msgDecodeFile) dsyslog("cDecoder::DecodeFile(): using decoder for stream %i: %s",i, codec->long_name);
        codecCtxArray[i]=avcodec_alloc_context3(codec);
        if (!codecCtxArray[i]) {
            dsyslog("cDecoder::DecodeFile(): avcodec_alloc_context3 failed");
            return(false);
        }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        if (avcodec_parameters_to_context(codecCtxArray[i],avctx->streams[i]->codecpar) < 0) {
#else
        if (avcodec_copy_context(codecCtxArray[i],avctx->streams[i]->codec) < 0) {
#endif
            dsyslog("cDecoder::DecodeFile(): avcodec_parameters_to_context failed");
            return(false);
        }
        codecCtxArray[i]->thread_count = threadCount;
        if (avcodec_open2(codecCtxArray[i], codec, NULL) < 0) {
            dsyslog("cDecoder::DecodeFile(): avcodec_open2 failed");
            return(false);
        }
    }
    msgDecodeFile=false;
    return(true);
}


int cDecoder::GetVideoHeight() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
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
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
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


int cDecoder::GetVideoFramesPerSecond() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
            return av_q2d(avctx->streams[i]->avg_frame_rate);
        }
    }
    dsyslog("cDecoder::GetVideoFramesPerSecond(): could not find average frame rate");
    return 0;
}


int cDecoder::GetVideoRealFrameRate() {
    if (!avctx) return 0;
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        if (avctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
        if (avctx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            return av_q2d(avctx->streams[i]->r_frame_rate);
#else
    #if LIBAVCODEC_VERSION_INT <= ((56<<16)+(1<<8)+0)    // Rasbian Jessie
            AVStream *st = avctx->streams[i];
            AVRational r_frame_rate;
            if ( st->codec->time_base.den * (int64_t) st->time_base.num <= st->codec->time_base.num * st->codec->ticks_per_frame * (int64_t) st->time_base.den) {
                r_frame_rate.num = st->codec->time_base.den;
                r_frame_rate.den = st->codec->time_base.num * st->codec->ticks_per_frame;
            }
            else {
                r_frame_rate.num = st->time_base.den;
                r_frame_rate.den = st->time_base.num;
            }
            return av_q2d(r_frame_rate);
    #else
            return av_q2d(av_stream_get_r_frame_rate(avctx->streams[i]));
    #endif
#endif
        }
    }
    dsyslog("cDecoder::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoder::GetNextFrame() {
    if (!avctx) return false;
    int64_t pts_time_ms=0;
    iFrameData.Valid=false;
    av_packet_unref(&avpkt);
    if (av_read_frame(avctx, &avpkt) == 0 ) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
       if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
#else
       if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
#endif
             framenumber++;
             if (isVideoIFrame()) {
                 iFrameCount++;
                 if ((iFrameInfoVector.empty()) || (framenumber > iFrameInfoVector.back().iFrameNumber)) {
                     if (avpkt.pts != AV_NOPTS_VALUE) {   // store a iframe number pts index
#if LIBAVCODEC_VERSION_INT <= ((56<<16)+(1<<8)+0)    // Rasbian Jessie
                         int64_t tmp_pts = avpkt.pts - avctx->streams[avpkt.stream_index]->start_time;
                         if ( tmp_pts < 0 ) { tmp_pts += 0x1ffffffff; }   // Respbian Jessie can overflow this value
                         pts_time_ms=tmp_pts*av_q2d(avctx->streams[avpkt.stream_index]->time_base)*100;
#else
                         pts_time_ms=(avpkt.pts - avctx->streams[avpkt.stream_index]->start_time)*av_q2d(avctx->streams[avpkt.stream_index]->time_base)*100;
#endif
                         iFrameInfo newFrameInfo;
                         newFrameInfo.fileNumber=fileNumber;
                         newFrameInfo.iFrameNumber=framenumber;
                         newFrameInfo.pts_time_ms=pts_time_ms_LastFile+pts_time_ms;
                         iFrameInfoVector.push_back(newFrameInfo);
                     }
                     else dsyslog("cDecoder::GetNextFrame(): failed to get pts for frame %li", framenumber);
                 }
             }
        }
        return true;
    }
    pts_time_ms_LastFile += iFrameInfoVector.back().pts_time_ms;
    dsyslog("cDecoder::GetNextFrame(): start time next file %" PRId64,pts_time_ms_LastFile);
    return false;
}

AVPacket *cDecoder::GetPacket() {
    return(&avpkt);
}


bool cDecoder::SeekToFrame(long int iFrame) {
    if (!avctx) return false;
    if (framenumber > iFrame) {
        dsyslog("cDecoder::SeekToFrame(): could not seek backward");
        return false;
    }
    dsyslog("cDecoder::SeekToFrame(): start");
    while (framenumber < iFrame) {
        if (!this->GetNextFrame())
            if (!this->DecodeDir(recordingDir)) {
                dsyslog("cDecoder::SeekFrame(): failed for frame (%li) at frame (%li)", iFrame, framenumber);
                return false;
        }
    }
    dsyslog("cDecoder::SeekToFrame(): successful");
    return true;
}


AVFrame *cDecoder::DecodePacket(AVFormatContext *avctx, AVPacket *avpkt) {
    AVFrame *avFrame = NULL;
//    dsyslog("cDecoder::DecodePacket(); framenumber %li",framenumber);
    avFrame=av_frame_alloc();
    if (!avFrame) {
        dsyslog("cDecoder::DecodePacket(): av_frame_alloc failed");
        return(NULL);
    }
    if (isVideoPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        avFrame->height=avctx->streams[avpkt->stream_index]->codecpar->height;
        avFrame->width=avctx->streams[avpkt->stream_index]->codecpar->width;
        avFrame->format=codecCtxArray[avpkt->stream_index]->pix_fmt;
#else
        avFrame->height=avctx->streams[avpkt->stream_index]->codec->height;
        avFrame->width=avctx->streams[avpkt->stream_index]->codec->width;
        avFrame->format=codecCtxArray[avpkt->stream_index]->pix_fmt;
#endif
    }
    else if (isAudioPacket()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
        avFrame->nb_samples=av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codecpar->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codecpar->channel_layout;
        avFrame->format=avctx->streams[avpkt->stream_index]->codecpar->format;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codecpar->sample_rate;
#elif LIBAVCODEC_VERSION_INT >= ((56<<16)+(26<<8)+100)
        avFrame->nb_samples=av_get_channel_layout_nb_channels(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format=codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codec->sample_rate;
#else  // Raspbian Jessie
        avFrame->nb_samples=av_popcount64(avctx->streams[avpkt->stream_index]->codec->channel_layout);
        avFrame->channel_layout=avctx->streams[avpkt->stream_index]->codec->channel_layout;
        avFrame->format=codecCtxArray[avpkt->stream_index]->sample_fmt;
        avFrame->sample_rate=avctx->streams[avpkt->stream_index]->codec->sample_rate;
#endif
    }
    else {
        dsyslog("cDecoder::DecodePacket(): stream type not supported");
        if (avFrame) av_frame_free(&avFrame);
        return(NULL);
    }
    int rc=av_frame_get_buffer(avFrame,32);
    if (rc != 0) {
        dsyslog("cDecoder::DecodePacket(): av_frame_get_buffer failed rc=%i", rc);
        if (avFrame) av_frame_free(&avFrame);
        return(NULL);
    }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    rc=avcodec_send_packet(codecCtxArray[avpkt->stream_index],avpkt);
    if (rc  < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error EAGAIN at frame %li", framenumber);
                break;
            case AVERROR(ENOMEM):
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet error ENOMEM at frame %li", framenumber);
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket():GetFrameInfo(): avcodec_send_packet error EINVAL at frame %li", framenumber);
                break;
            case AVERROR_INVALIDDATA:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AVERROR_INVALIDDATA at frame %li", framenumber);
                break;
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
            case AAC_AC3_PARSE_ERROR_SYNC:
                dsyslog("cDecoder::DecodePacket:(): avcodec_send_packet error AAC_AC3_PARSE_ERROR_SYNC at frame %li", framenumber);
                break;
#endif
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_send_packet failed with rc=%i at frame %li",rc,framenumber);
                break;
            }
        if (avFrame) av_frame_free(&avFrame);
        return(NULL);
    }
    rc = avcodec_receive_frame(codecCtxArray[avpkt->stream_index],avFrame);
    if (rc < 0) {
        switch (rc) {
            case AVERROR(EAGAIN):
                tsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EAGAIN at frame %li", framenumber);
                stateEAGAIN=true;
                break;
            case AVERROR(EINVAL):
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame error EINVAL at frame %li", framenumber);
                break;
            default:
                dsyslog("cDecoder::DecodePacket(): avcodec_receive_frame: decode of frame (%li) failed with return code %i", framenumber, rc);
                break;
        }
        if (avFrame) av_frame_free(&avFrame);
        return(NULL);
    }
#else
    int frame_ready=0;
    if (isVideoPacket()) {
        rc=avcodec_decode_video2(codecCtxArray[avpkt->stream_index],avFrame,&frame_ready,avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_video2 decode of frame (%li) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
            if (avFrame) av_frame_free(&avFrame);
            return(NULL);
        }
    }
    else if (isAudioPacket()) {
        rc=avcodec_decode_audio4(codecCtxArray[avpkt->stream_index],avFrame,&frame_ready,avpkt);
        if (rc < 0) {
            dsyslog("cDecoder::DecodePacket(): avcodec_decode_audio4 of frame (%li) from stream %i failed with return code %i", framenumber, avpkt->stream_index, rc);
            if (avFrame) av_frame_free(&avFrame);
            return(NULL);
        }
    }

    else {
       dsyslog("cDecoder::DecodePacket(): packet type of stream %i not supported", avpkt->stream_index);
       if (avFrame) av_frame_free(&avFrame);
       return(NULL);
    }

    if ( !frame_ready ) {
        stateEAGAIN=true;
        if (avFrame) av_frame_free(&avFrame);
        return(NULL);
    }
#endif
    return(avFrame);
}


bool cDecoder::GetFrameInfo(MarkAdContext *maContext) {
    if (!avctx) return false;
    AVFrame *avFrame = NULL;
    iFrameData.Valid=false;
    if (isVideoPacket()) {
        if (isVideoIFrame() || stateEAGAIN) {
            avFrame=this->DecodePacket(avctx, &avpkt);
            if (avFrame) {
                stateEAGAIN=false;
                if (avFrame->interlaced_frame != interlaced_frame) {
                    dsyslog("found %s video format",(avFrame->interlaced_frame) ? "interlaced" : "non interlaced");
                    interlaced_frame=avFrame->interlaced_frame;
                }
                for (int i=0; i<4; i++) {
                    if (avFrame->data[i]) {
                        maContext->Video.Data.Plane[i]=avFrame->data[i];
                        maContext->Video.Data.PlaneLinesize[i]=avFrame->linesize[i];
                        maContext->Video.Data.Valid=true;
                    }
                }

                int sample_aspect_ratio_num = avFrame->sample_aspect_ratio.num;
                int sample_aspect_ratio_den = avFrame->sample_aspect_ratio.den;
                if ((sample_aspect_ratio_num == 0) || (sample_aspect_ratio_den == 0)) {
                    dsyslog("cDecoder::GetFrameInfo(): invalid aspect ratio (%i:%i) at frame (%li)", sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
                    if (avFrame) av_frame_free(&avFrame);
                    return(false);
                }
                if ((sample_aspect_ratio_num == 1) && (sample_aspect_ratio_den == 1)) {
                    if ((avFrame->width == 1280) && (avFrame->height  ==  720) ||
                        (avFrame->width == 1920) && (avFrame->height  == 1080)) {
                        sample_aspect_ratio_num = 16;
                        sample_aspect_ratio_den = 9;
                    }
                    else {
                        dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio to video width %i hight %i at frame %li)",avFrame->width,avFrame->height,framenumber);
                        if (avFrame) av_frame_free(&avFrame);
                        return(false);
                    }
                }
                else {
                    if ((sample_aspect_ratio_num==64) && (sample_aspect_ratio_den==45)){
                        sample_aspect_ratio_num =16;
                        sample_aspect_ratio_den = 9;
                    }
                    else if ((sample_aspect_ratio_num==32) && (sample_aspect_ratio_den==17)){
                         sample_aspect_ratio_num =16;
                         sample_aspect_ratio_den = 9;
                    }
                    else if ((sample_aspect_ratio_num==16) && (sample_aspect_ratio_den==15)){
                        sample_aspect_ratio_num =4;
                        sample_aspect_ratio_den =3;
                    }
                    else if ((sample_aspect_ratio_num==4) && (sample_aspect_ratio_den==3)){
//                      sample_aspect_ratio_num =4;
//                      sample_aspect_ratio_den =3;
                    }
                    else dsyslog("cDecoder::GetFrameInfo(): unknown aspect ratio (%i:%i) at frame (%li)",sample_aspect_ratio_num,sample_aspect_ratio_den,framenumber);
                }
                if ((maContext->Video.Info.AspectRatio.Num != sample_aspect_ratio_num) ||
                   ( maContext->Video.Info.AspectRatio.Den != sample_aspect_ratio_den)) {
                    if (msgGetFrameInfo) dsyslog("cDecoder::GetFrameInfo(): aspect ratio changed from (%i:%i) to (%i:%i) at frame %li",
                                                                            maContext->Video.Info.AspectRatio.Num, maContext->Video.Info.AspectRatio.Den,
                                                                            sample_aspect_ratio_num, sample_aspect_ratio_den,
                                                                            framenumber);
                    maContext->Video.Info.AspectRatio.Num=sample_aspect_ratio_num;
                    maContext->Video.Info.AspectRatio.Den=sample_aspect_ratio_den;
                }
                if (avFrame) av_frame_free(&avFrame);
                return true;
            }
            if (avFrame) av_frame_free(&avFrame);
            return false;
        }
    }

    if (isAudioPacket()) {
        if (isAudioAC3Stream()) {
            if (avpkt.stream_index > MAXSTREAMS) {
                dsyslog("cDecoder::GetFrameInfo(): to much streams %i", avpkt.stream_index);
                return(false);
            }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codecpar->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %i changed from %i to %i at frame (%li)", avpkt.stream_index,
                                                                                                        maContext->Audio.Info.Channels[avpkt.stream_index],
                                                                                                        avctx->streams[avpkt.stream_index]->codecpar->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codecpar->channels;
#else
            if (maContext->Audio.Info.Channels[avpkt.stream_index] != avctx->streams[avpkt.stream_index]->codec->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels of stream %i changed from %i to %i at frame (%li)", avpkt.stream_index,
                                                                                                        maContext->Audio.Info.Channels[avpkt.stream_index],
                                                                                                        avctx->streams[avpkt.stream_index]->codec->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels[avpkt.stream_index] = avctx->streams[avpkt.stream_index]->codec->channels;
#endif
            }
        }
        return true;
    }
    return false;
}


bool cDecoder::isVideoStream(short int streamIndex) {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::isVideoPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}


bool cDecoder::isAudioStream(short int streamIndex) {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[streamIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[streamIndex]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}


bool cDecoder::isAudioPacket() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}

bool cDecoder::isAudioAC3Stream() {
    if (!avctx) return false;
#define AUDIOFORMATAC3 8
#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_id == AV_CODEC_ID_AC3 ) return true;
#elif LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->format == AUDIOFORMATAC3) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->sample_fmt == AUDIOFORMATAC3) return true;
#endif
    return false;
}


bool cDecoder::isVideoIFrame() {
    if (!avctx) return false;
    if (!isVideoPacket()) return false;
    if ((avpkt.flags & AV_PKT_FLAG_KEY) != 0)  return true;
    return false;
}


long int cDecoder::GetFrameNumber(){
    return framenumber;
}


long int cDecoder::GetIFrameCount(){
    return iFrameCount;
}


bool cDecoder::isInterlacedVideo(){
    if (interlaced_frame > 0) return true;
    return false;
}


long int cDecoder::GetIFrameRangeCount(long int beginFrame, long int endFrame) {
    int counter=0;
    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iInfo->iFrameNumber >= beginFrame) {
            counter++;
            if (iInfo->iFrameNumber >= endFrame) return counter;
        }
    }
    dsyslog("cDecoder::GetIFrameCount(): failed beginFrame (%li) endFrame (%li) last frame in index list (%li)", beginFrame, endFrame, iFrameInfoVector.back().iFrameNumber);
    return(0);
}


long int cDecoder::GetIFrameBefore(long int iFrame) {
    long int before_iFrame=0;
    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iInfo->iFrameNumber >= iFrame) {
            return before_iFrame;
        }
        else before_iFrame=iInfo->iFrameNumber;
    }
    dsyslog("cDecoder::GetNearestIFrame(): failed for frame (%li)", iFrame);
    return 0;
}


long int cDecoder::GetTimeFromIFrame(long int iFrame) {
    int64_t before_pts=0;
    long int before_iFrame=0;
    if (iFrameInfoVector.empty()) dsyslog("cDecoder::GetTimeFromIFrame(): iFrame Index not initialized");
    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iFrame == iInfo->iFrameNumber) {
            tsyslog("cDecoder::GetTimeFromIFrame(): iFrame (%li) time is %" PRId64" ms", iFrame, iInfo->pts_time_ms);
            return iInfo->pts_time_ms;
        }
        if (iInfo->iFrameNumber > iFrame) {
            if (abs(iFrame - before_iFrame) < abs(iFrame - iInfo->iFrameNumber)) {
                tsyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame before (%li) %" PRId64 "ms",iFrame,before_iFrame,before_pts);
                return before_pts;
            }
            else {
                dsyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame after (%li) %" PRId64 "ms",iFrame,iInfo->iFrameNumber,iInfo->pts_time_ms);
                return iInfo->pts_time_ms;
            }
        }
        else {
            before_iFrame=iInfo->iFrameNumber;
            before_pts=iInfo->pts_time_ms;
        }
    }
    dsyslog("cDecoder::GetTimeFromIFrame(): could not find time for frame %li",iFrame);
    return 0;
}
