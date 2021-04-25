/*
 * decoder_new.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __decoder_new_h_
#define __decoder_new_h_

#include <vector>
#include "global.h"
#include "index.h"

extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
    #include <libavutil/file.h>
}


#define AVLOGLEVEL AV_LOG_ERROR

#if LIBAVCODEC_VERSION_INT >= ((58<<16)+(35<<8)+100)   // error codes from AC3 parser
    #define AAC_AC3_PARSE_ERROR_SYNC         -0x1030c0a
    #define AAC_AC3_PARSE_ERROR_BSID         -0x2030c0a
    #define AAC_AC3_PARSE_ERROR_SAMPLE_RATE  -0x3030c0a
    #define AAC_AC3_PARSE_ERROR_FRAME_SIZE   -0x4030c0a
    #define AAC_AC3_PARSE_ERROR_FRAME_TYPE   -0x5030c0a
    #define AAC_AC3_PARSE_ERROR_CRC          -0x6030c0a
    #define AAC_AC3_PARSE_ERROR_CHANNEL_CFG  -0x7030c0a
#endif


// libavcodec versions of some distributions
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+(134<<8)+100)   ffmpeg 4.4
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+( 35<<8)+100)   Ubuntu 20.04 and Debian Buster
// #if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)   Ubuntu 18.04
// #if LIBAVCODEC_VERSION_INT >= ((57<<16)+( 64<<8)+101)   Debian Stretch
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 57<<8)+100)   Ubuntu 14.04
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 26<<8)+100)   Debian Jessie
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+(  1<<8)+  0)   Rasbian Jessie
//
#define LIBAVCODEC_VERSION_MIN 56

class cDecoder {
    public:
        explicit cDecoder(int threads, cIndex *recordingIndex);
        ~cDecoder();
        bool DecodeDir(const char * recDir);
        void Reset();
        AVFormatContext *GetAVFormatContext();
        AVCodecContext **GetAVCodecContext();
        bool DecodeFile(const char * filename);
        int GetVideoType();
        int GetVideoHeight();
        int GetVideoWidth();
        int GetVideoFramesPerSecond();
        int GetVideoRealFrameRate();
        bool GetNextFrame();
        AVPacket *GetPacket();
        bool SeekToFrame(MarkAdContext *maContext, int frame);
        AVFrame *DecodePacket(AVPacket *avpkt);
        bool GetFrameInfo(MarkAdContext *maContext, const bool full);
        bool isVideoStream(const unsigned int streamIndex);
        bool isVideoPacket();
        bool isVideoIFrame();
        bool isAudioAC3Stream(const unsigned int streamIndex);
        bool isAudioAC3Packet();
        bool isAudioStream(const unsigned int streamIndex);
        bool isAudioPacket();
        int GetFrameNumber();
        int GetIFrameCount();
        bool isInterlacedVideo();
        int GetIFrameRangeCount(int beginFrame, int endFrame);
        int64_t GetTimeFromIFrame(int iFrame);
        int GetIFrameFromOffset(int offset);
        int GetNextSilence(MarkAdContext *maContext, const int stopFrame, const bool isBeforeMark, const bool isStartMark);
    private:
        cIndex *recordingIndexDecoder = NULL;
        char *recordingDir = NULL;
        int fileNumber = 0;
        int threadCount = 0;
        AVFormatContext *avctx = NULL;
        AVPacket avpkt = {};
        AVFrame *avFrame = NULL;
        AVCodec *codec = NULL;
        AVCodecContext **codecCtxArray = NULL;
        int framenumber = -1;
        int iFrameCount = 0;
        int64_t pts_time_ms_LastFile = 0;
        int64_t pts_time_ms_LastRead = 0;
        struct structFrameData {
            bool Valid=false; // flag, if true data is valid
            uchar *Plane[PLANES] = {};  // picture planes (YUV420)
            int PlaneLinesize[PLANES] = {}; // size int bytes of each picture plane line
        } iFrameData;
        bool msgDecodeFile = true;
        bool msgGetFrameInfo = true;
        int interlaced_frame = -1;
        bool stateEAGAIN = false;
        int GetFirstMP2AudioStream();
};
#endif
