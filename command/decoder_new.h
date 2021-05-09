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


/**
 * main decoder class
 */
class cDecoder {
    public:

/**
 * cDecoder constructor
 * @param threads        count threads of ffmpeg decoder
 * @param RecordingIndex recording index class
 */
        explicit cDecoder(int threads, cIndex *recordingIndex);

        ~cDecoder();

/**
 * set decoder to first/next file of the directory
 * @param recDir name of the recording directory
 * @return true if first/next ts file found, false otherweise
 */
        bool DecodeDir(const char *recDir);

/**
 * setup decoder codec context for current file
 * @param file name
 * @return true if setup was succesful, false otherwiese
 */
        bool DecodeFile(const char * filename);

/**
 * reset decoder to first frame of first file
 */
        void Reset();

/**
 * get libav format context
 * @return AVFormatContext
 */
        AVFormatContext *GetAVFormatContext();

/** get libav codec context
 * @return AVCodecContext
 */
        AVCodecContext **GetAVCodecContext();

/**
 * get markad internal video type
 * return video type (MARKAD_PIDTYPE_VIDEO_H262, MARKAD_PIDTYPE_VIDEO_H264 or MARKAD_PIDTYPE_VIDEO_H265)
 */
        int GetVideoType();

/**
 * get video height
 * @return video height in pixel
 */
        int GetVideoHeight();

/**
 * get video width
 * @return video width in pixel
 */
        int GetVideoWidth();

/**
 * get average video frame rate taken from avctx->streams[i]->avg_frame_rate
 * @return average video frame rate (avg_frame_rate)
 */
        int GetVideoFramesPerSecond();

/**
 * get real video frame rate taken from avctx->streams[i]->r_frame_rate
 * @return real video frame rate (r_frame_rate)
 */
        int GetVideoRealFrameRate();

/**
 * read next frame from current file
 * @return true if successful, false otherwise (e.g. end of file)
 */
        bool GetNextFrame();

/**
 * get current packet
 * @return current packet
 */
        AVPacket *GetPacket();

/**
 * seek read position
 * @param maContext   markad context
 * @param frameNumber frame number to seek
 * return true if successful, false otherwise
 */
        bool SeekToFrame(sMarkAdContext *maContext, int frameNumber);

/**
 * decode packet
 * @param avpkt packet to decode
 * @return decoded frame
 */
        AVFrame *DecodePacket(AVPacket *avpkt);

/**
 * decode current packet, get all global frame infos, fill data planes
 * @param maContext markad context
 * @param full      true if we do full decoding of all frames, false if we decode only i-frames
 */
        bool GetFrameInfo(sMarkAdContext *maContext, const bool full);

/** check if stream is video stream
 * @param stream index
 * @return true if video stream, false otherwise
 */
        bool IsVideoStream(const unsigned int streamIndex);

/**
 * check if current packet is from a video stream
 * @return true if current packet is from a video stream, false otherwise
 */
        bool IsVideoPacket();

/**
 * check if current packet is a video i-frame
 * @return true if current packet is a video i-frame, false otherwise
 */
        bool IsVideoIFrame();

/**
 * check if stream is AC3
 * @param streamIndex stream index
 * @return true if stream is AC3, false otherwise
 */
        bool IsAudioAC3Stream(const unsigned int streamIndex);

/**
 * check if current packet is AC3
 * @return true if current packet is AC3, false otherwise
 */
        bool IsAudioAC3Packet();

/** check if stream is audio
 * @param streamIndex stream index
 * @return true if stream is audio, false otherwise
 */
        bool IsAudioStream(const unsigned int streamIndex);

/** check if current packet is audio
 * @return true if current packet is audio, false otherwise
 */
        bool IsAudioPacket();

/** get current frame number
 * @return current frame number
 */
        int GetFrameNumber();

/** get current number of processed i-frames
 * @return current number of processed i-frames
 */
        int GetIFrameCount();

/**
 * check if video stream is interlaced
 * @return true if video stream is interlaced, false otherwise
 */
        bool IsInterlacedVideo();

/** get number of i-frames between to frames
 * @param beginFrame
 * @param endFrame
 * @return get number ofg i-frames between beginFrame and endFrame
 */
        int GetIFrameRangeCount(int beginFrame, int endFrame);

/**
 * get next silent audio part from current frame position to stopFrame
 * @param maContext    markad context
 * @param stopFrame    stop search at this frame
 * @param isBeforeMark true if search is from current frame to mark position, false if search is from mark position to stopFrame
 * @param isStartMark  true if we check for a start mark, false if we check for a stop mark
 * @return frame number of silence part, -1 if no silence part was found
 */
        int GetNextSilence(sMarkAdContext *maContext, const int stopFrame, const bool isBeforeMark, const bool isStartMark);

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
        int currFrameNumber = -1;
        int iFrameCount = 0;
        int64_t pts_time_ms_LastFile = 0;
        int64_t pts_time_ms_LastRead = 0;

/**
 * decoded frame data
 */
        struct sFrameData {
            bool Valid = false;             //!< flag, if true data is valid
                                            //!<

            uchar *Plane[PLANES] = {};      //!< picture planes (YUV420)
                                            //!<

            int PlaneLinesize[PLANES] = {}; //!< size in bytes of each picture plane line
                                            //!<

        } FrameData;

        bool msgDecodeFile = true;
        bool msgGetFrameInfo = true;
        int interlaced_frame = -1;
        bool stateEAGAIN = false;
        int GetFirstMP2AudioStream();
};
#endif
