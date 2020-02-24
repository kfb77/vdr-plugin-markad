#include "decoder_new.h"
extern "C"{
#include "debug.h"
}


cDecoder::cDecoder() {
    av_init_packet(&avpkt);
    codec = NULL;
    codecCtx = NULL;
}

cDecoder::~cDecoder() {
    av_packet_unref(&avpkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&avctx);
}

bool cDecoder::DecodeDir(const char * recDir) {
    if (!recDir) return false;
    char *filename;
    if (asprintf(&recordingDir,"%s",recDir)==-1) {
        esyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
        return false;
    }
    fileNumber++;
    if (asprintf(&filename,"%s/%05i.ts",recDir,fileNumber)==-1) {
        esyslog("cDecoder::DecodeDir(): failed to allocate string, out of memory?");
        return false;
    }
    return this->DecodeFile(filename);
}

void cDecoder::Reset(){
    fileNumber=0;
    framenumber=0;
    msgGetFrameInfo=false;
}

bool cDecoder::DecodeFile(const char * filename) {
    if (!filename) return false;
    if (avctx) avformat_close_input(&avctx);
#if LIBAVCODEC_VERSION_INT < ((58<<16)+(35<<8)+100)
    av_register_all();
#endif
    if (avformat_open_input(&avctx, filename, NULL, NULL) == 0) {
        if (msgDecodeFile) isyslog("cDecoder::DecodeFile(): decode file %s",filename);
    }
    else {
        if (fileNumber <= 1) esyslog("cDecoder::DecodeFile(): Could not open source file %s", filename);
        return(false);
    }
    if (avformat_find_stream_info(avctx, NULL) <0) {
        esyslog("cDecoder::DecodeFile(): Could not get stream infos %s", filename);
        return(false);
    }
    for (unsigned int i=0; i<avctx->nb_streams; i++) {
        if (isVideoStream()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
            codec=avcodec_find_decoder(avctx->streams[avpkt.stream_index]->codecpar->codec_id);
#else
            codec=avcodec_find_decoder(avctx->streams[avpkt.stream_index]->codec->codec_id);
#endif
            if (!codec) {
                esyslog("cDecoder::DecodeFile(): could nit find decoder for stream");
                return(false);
            }
            if (msgDecodeFile) isyslog("cDecoder::DecodeFile(): using decoder %s for stream %i",codec->long_name,i);
            codecCtx=avcodec_alloc_context3(codec);
            if (!codecCtx) {
                esyslog("cDecoder::DecodeFile(): avcodec_alloc_context3 failed");
                return(false);
            }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
            if (avcodec_parameters_to_context(codecCtx,avctx->streams[avpkt.stream_index]->codecpar) < 0) {
#else
            if (avcodec_copy_context(codecCtx,avctx->streams[avpkt.stream_index]->codec) < 0) {
#endif
                esyslog("cDecoder::DecodeFile(): avcodec_parameters_to_context failed");
                return(false);
            }
            if (avcodec_open2(codecCtx, codec, NULL) < 0) {
                esyslog("cDecoder::DecodeFile(): avcodec_open2 failed");
                return(false);
            }
            break;
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
    esyslog("cDecoder::GetVideoHeight(): failed");
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
    esyslog("cDecoder::GetVideoWidth(): failed");
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
    esyslog("cDecoder::GetVideoFramesPerSecond(): could not find average frame rate");
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
    esyslog("cDecoder::GetVideoRealFrameRate(): could not find real frame rate");
    return 0;
}


bool cDecoder::GetNextFrame() {
    if (!avctx) return false;
    long int pts_time_ms=0;
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
                     else esyslog("cDecoder::GetNextFrame(): failed to get pts for frame %li", framenumber);
                 }
             }
        }
        return true;
    }
    pts_time_ms_LastFile += iFrameInfoVector.back().pts_time_ms;
    dsyslog("cDecoder::GetNextFrame(): start time next file %li",pts_time_ms_LastFile);
    return false;
}

bool cDecoder::SeekToFrame(long int iFrame) {
    if (!avctx) return false;
    if (framenumber > iFrame) {
        dsyslog("cDecoder::SeekToFrame(): could not seek backward");
        return false;
    }
    while (framenumber < iFrame) {
        if (!this->GetNextFrame())
            if (!this->DecodeDir(recordingDir)) {
                dsyslog("cDecoder::SeekFrame(): failed for frame (%li) at frame (%li)", iFrame, framenumber);
                return false;
        }
    }
    return true;
}

bool cDecoder::GetFrameInfo(MarkAdContext *maContext) {
    if (!avctx) return false;
    iFrameData.Valid=false;
    if (avFrame) av_frame_free(&avFrame);
    if (isVideoStream()) {
        if (isVideoIFrame() || stateEAGAIN) {
            avFrame=av_frame_alloc();
            if (!avFrame) {
               esyslog("cDecoder::GetFrameInfo(): av_frame_alloc failed");
               return false;
            }
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
            avFrame->height=avctx->streams[avpkt.stream_index]->codecpar->height;
            avFrame->width=avctx->streams[avpkt.stream_index]->codecpar->width;
            avFrame->format=codecCtx->pix_fmt;
#else
            avFrame->height=avctx->streams[avpkt.stream_index]->codec->height;
            avFrame->width=avctx->streams[avpkt.stream_index]->codec->width;
            avFrame->format=codecCtx->pix_fmt;
#endif
            int rc=av_frame_get_buffer(avFrame,32);
            if (rc != 0) {
                esyslog("cDecoder::GetFrameInfo(): av_frame_get_buffer failed rc=%i", rc);
                return false;
            }

#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
//            dsyslog("---framenumber %li",framenumber);
            rc=avcodec_send_packet(codecCtx,&avpkt);
            if (rc  < 0) {
                switch (rc) {
                    case AVERROR(EAGAIN):
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_send_packet error EAGAIN at frame %li", framenumber);
                        break;
                    case AVERROR(ENOMEM):
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_send_packet error ENOMEM at frame %li", framenumber);
                        break;
                    case AVERROR(EINVAL):
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_send_packet error EINVAL at frame %li", framenumber);
                        break;
                    case AVERROR_INVALIDDATA:
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_send_packet error AVERROR_INVALIDDATA at frame %li", framenumber); // this could happen on the start of a recording
                        break;
                    default:
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_send_packet failed with rc=%i at frame %li",rc,framenumber);
                        break;
                }
                return false;
            }
            rc = avcodec_receive_frame(codecCtx,avFrame);
            if (rc < 0) {
                switch (rc) {
                    case AVERROR(EAGAIN):
                        tsyslog("TRACE: cDecoder::GetFrameInfo(): avcodec_receive_frame error EAGAIN at frame %li", framenumber);
                        stateEAGAIN=true;
                        break;
                    case AVERROR(EINVAL):
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_receive_frame error EINVAL at frame %li", framenumber);
                        break;
                    default:
                        dsyslog("DEBUG: cDecoder::GetFrameInfo(): avcodec_receive_frame: decode of frame (%li) failed with return code %i", framenumber, rc);
                        break;
                }
                return false;
            }
#else
            int video_frame_ready=0;
            rc=avcodec_decode_video2(codecCtx,avFrame,&video_frame_ready,&avpkt);
            if (rc < 0) {
                esyslog("cDecoder::GetFrameInfo(): avcodec_decode_video2 decode of frame (%li) failed with return code %i", framenumber, rc);
                return false;
            }
            if ( !video_frame_ready ) {
                stateEAGAIN=true;
                return false;
            }
#endif
            stateEAGAIN=false;

            if (avFrame->interlaced_frame != interlaced_frame) {
                isyslog("found %s video format",(avFrame->interlaced_frame) ? "interlaced" : "non interlaced");
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
                    esyslog("cDecoder::GetFrameInfo(): invalid aspect ratio (%i:%i) at frame (%li)", sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
                    return false;
            }
            if ((sample_aspect_ratio_num == 1) && (sample_aspect_ratio_den == 1)) {
                if ((avFrame->width == 1280) && (avFrame->height  ==  720) ||
                    (avFrame->width == 1920) && (avFrame->height  == 1080)) {
                    sample_aspect_ratio_num = 16;
                    sample_aspect_ratio_den = 9;
                }
                else {
                    esyslog("cDecoder::GetFrameInfo(): unknown aspect ratio to video width %i hight %i at frame %li)", avFrame->width, avFrame->height, framenumber);
                    return false;
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
//                                   sample_aspect_ratio_num =4;
//                                   sample_aspect_ratio_den =3;
                               }
                          else esyslog("cDecoder::GetFrameInfo(): unknown aspect ratio (%i:%i) at frame (%li)",
                                                                               sample_aspect_ratio_num, sample_aspect_ratio_den, framenumber);
            }
            if ((maContext->Video.Info.AspectRatio.Num != sample_aspect_ratio_num) ||
               ( maContext->Video.Info.AspectRatio.Den != sample_aspect_ratio_den)) {
                if (msgGetFrameInfo) dsyslog("cDecoder::GetFrameInfo(): aspect ratio changed from (%i:%i) to (%i:%i) at frame %li",
                                                                                                        maContext->Video.Info.AspectRatio.Num,
                                                                                                        maContext->Video.Info.AspectRatio.Den,
                                                                                                        sample_aspect_ratio_num,
                                                                                                        sample_aspect_ratio_den,
                                                                                                        framenumber);
                maContext->Video.Info.AspectRatio.Num=sample_aspect_ratio_num;
                maContext->Video.Info.AspectRatio.Den=sample_aspect_ratio_den;
            }
            return true;
        }
        return false;
    }

    if (isAudioStream()) {
        if (isAudioAC3Frame()) {
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
            if (maContext->Audio.Info.Channels != avctx->streams[avpkt.stream_index]->codecpar->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels changed from %i to %i at frame (%li)", maContext->Audio.Info.Channels,
                                                                                                        avctx->streams[avpkt.stream_index]->codecpar->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels = avctx->streams[avpkt.stream_index]->codecpar->channels;
#else
            if (maContext->Audio.Info.Channels != avctx->streams[avpkt.stream_index]->codec->channels) {
                dsyslog("cDecoder::GetFrameInfo(): audio channels changed from %i to %i at frame (%li)", maContext->Audio.Info.Channels,
                                                                                                        avctx->streams[avpkt.stream_index]->codec->channels,
                                                                                                        framenumber);
                maContext->Audio.Info.Channels = avctx->streams[avpkt.stream_index]->codec->channels;
#endif
            }
        }
        return true;
    }
    return false;
}

bool cDecoder::isVideoStream() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_VIDEO) return true;
#endif
    return false;
}

bool cDecoder::isAudioStream() {
    if (!avctx) return false;
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->codec_type == AVMEDIA_TYPE_AUDIO) return true;
#endif
    return false;
}

bool cDecoder::isAudioAC3Frame() {
    if (!avctx) return false;
#define AUDIOFORMATAC3 8
#if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)
    if (avctx->streams[avpkt.stream_index]->codecpar->format == AUDIOFORMATAC3) return true;
#else
    if (avctx->streams[avpkt.stream_index]->codec->sample_fmt == AUDIOFORMATAC3) return true;
#endif
    return false;
}

bool cDecoder::isVideoIFrame() {
    if (!avctx) return false;
    if (!isVideoStream()) return false;
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
    long int before_pts=0;
    long int before_iFrame=0;
    if (iFrameInfoVector.empty()) esyslog("cDecoder::GetTimeFromIFrame(): iFrame Index not initialized");
    for (std::vector<iFrameInfo>::iterator iInfo = iFrameInfoVector.begin(); iInfo != iFrameInfoVector.end(); ++iInfo) {
        if (iFrame == iInfo->iFrameNumber) {
            dsyslog("cDecoder::GetTimeFromIFrame(): iFrame (%li) time is %lims", iFrame, iInfo->pts_time_ms);
            return iInfo->pts_time_ms;
        }
        if (iInfo->iFrameNumber > iFrame) {
            if (abs(iFrame - before_iFrame) < abs(iFrame - iInfo->iFrameNumber)) {
                esyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame before (%li) %lims",iFrame,before_iFrame,before_pts);
                return before_pts;
            }
            else {
                dsyslog("cDecoder::GetTimeFromIFrame(): frame (%li) is not an iFrame, returning time from iFrame after (%li) %lims",iFrame,iInfo->iFrameNumber,iInfo->pts_time_ms);
                return iInfo->pts_time_ms;
            }
        }
        else {
            before_iFrame=iInfo->iFrameNumber;
            before_pts=iInfo->pts_time_ms;
        }
    }
    esyslog("cDecoder::GetTimeFromIFrame(): could not find time for frame %li",iFrame);
    return 0;
}
