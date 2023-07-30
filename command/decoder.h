/*
 * decoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __decoder_h_
#define __decoder_h_

#include <vector>
#include "global.h"
#include "index.h"

extern "C"{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
    #include <libavutil/file.h>

    #if LIBAVCODEC_VERSION_INT >= ((59<<16)+(3<<8)+102)
        #include <libavutil/channel_layout.h>
    #endif
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


// libavcodec versions
//
// full suported markad ffmpeg versions (based on Ubuntu LTS has Standard Support)
//
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+(134<<8)+100)   ffmpeg 4.4.2  (Ubuntu 22.04, End of Standard Support: April 2027, updated 16.06.2023)
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+( 54<<8)+100)   ffmpeg 4.2.7  (Ubuntu 20.04, End of Standard Support: April 2025, updated 16.06.2023)
#define LIBAVCODEC_VERSION_VALID ((58<<16)+( 54<<8)+100)   // oldest full suported version

// deprecated markad ffmpeg versions (based on Ubuntu LTS is End of Standard Support but still has Extended Security Maintenance)
//
// #if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)   ffmpeg 3.4.11 (Ubuntu 18.04, End of Life: April 2028)
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 60<<8)+100)   ffmpeg 2.8.17 (Ubuntu 16.04) End of Life: April 2026)
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 57<<8)+100)   ffmpeg 2.7.2  (Ubuntu 14.04, End of Life: April 2024)
#define LIBAVCODEC_VERSION_DEPRECATED ((56<<16)+( 57<<8)+100)   // oldest deprecated version, older is invalid

// end of life markad ffmpeg versions (based on Ubuntu LTS has no support at all)
//
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 26<<8)+100)   ffmpeg 2      (Debian 8 Jessie)
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+(  1<<8)+  0)   ffmpeg 2.4    (Rasbian Jessie)


/**
 * main decoder class
 */
class cDecoder {
    public:

/**
 * cDecoder constructor
 * @param threads        count threads of ffmpeg decoder
 * @param recordingIndex recording index class
 */
        explicit cDecoder(int threads, cIndex *recordingIndex);

        ~cDecoder();

/**
 * get number of decoding error
 * @return number of decoding errors
 */
        int GetErrorCount();

/**
 * set decoder to first/next file of the directory
 * @param recDir name of the recording directory
 * @return true if first/next ts file found, false otherweise
 */
        bool DecodeDir(const char *recDir);

/**
 * setup decoder codec context for current file
 * @param filename file name
 * @return true if setup was succesful, false otherwiese
 */
        bool DecodeFile(const char * filename);

/**
 * get currently in progress TS file number
 * @return file number
 */
        int GetFileNumber();

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
        int GetVideoAvgFrameRate();

/**
 * get real video frame rate taken from avctx->streams[i]->r_frame_rate
 * @return real video frame rate (r_frame_rate)
 */
        int GetVideoRealFrameRate();

/// read next frame from current input ts file
/**
 * increase frame counter if video frame <br>
 * increase i-frame counter if video i-frame <br>
 * add presentation timestamp for each frame to ring buffer <br>
 * add offset from recording start for each i-frame to recording index <br>
 * @param ignorePTS_Ringbuffer no not fill PTS ring buffer, true if called by logo search to avoid out of sequence elements
 * @return true if successful, false if av_read_frame failed (e.g. end of file)
 */
        bool GetNextPacket(const bool buildIndex, bool buildPTSIndex);

/**
 * get current packet
 * @return current packet
 */
        AVPacket *GetPacket();

/// seek decoder read position
/**
 * only seek forward <br>
 * seek to i-frame before and start decode to fill decoder buffer
 * @param maContext   markad context
 * @param frameNumber frame number to seek
 * @return true if successful, false otherwise
 */
        bool SeekToFrame(sMarkAdContext *maContext, int frameNumber);

/**
 * decode audio or packet
 * @param avpkt packet to decode
 * @return decoded frame
 */
        AVFrame *DecodePacket(AVPacket *avpkt);

/// decode video packets and get audio/video infos
/**
 * decode video packets (audio frames are not decoded) <br>
 * get aspect ratio for video frames <br>
 * get audio channels for audio packets <br>
 * fill video data planes
 * @param[in,out] maContext       markad context
 * @param[in]     decodeVideo     true if we do decoding of video frames, false if we do no decoding at all
 * @param[in]     decodeFull      true if we do full decoding of all video frames, false if we decode only i-frames
 */
        bool GetFrameInfo(sMarkAdContext *maContext, const bool decodeVideo, const bool decodeFull, const bool decodeVolume, const bool decodeChannel);

/** check if stream is video stream
 * @param streamIndex stream index
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

/** check if stream is subtitle
 * @param streamIndex stream index
 * @return true if stream is subtitle, false otherwise
 */
        bool IsSubtitleStream(const unsigned int streamIndex);

/** check if current packet is a subtitle
 * @return true if current packet is subtitle, false otherwise
 */
        bool IsSubtitlePacket();

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
 * @return get number of i-frames between beginFrame and endFrame
 */
        int GetIFrameRangeCount(int beginFrame, int endFrame);

   private:
        cIndex *recordingIndexDecoder = NULL;  //!< recording index
                                               //!<
        char *recordingDir = NULL;             //!< name of recording directory
                                               //!<
        int fileNumber = 0;                    //!< current ts file number
                                               //!<
        int threadCount = 0;                   //!< thread count of decoder
                                               //!<
        AVFormatContext *avctx = NULL;         //!< avformat context
                                               //!<
        AVPacket avpkt = {};                   //!< packet
                                               //!<
        AVFrame *avFrame = NULL;               //!< frame
                                               //!<
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100) // ffmpeg 4.5
        const AVCodec *codec = NULL;           //!< codec
                                               //!<
#else
        AVCodec *codec = NULL;                 //!< codec
                                               //!<
#endif
        AVCodecContext **codecCtxArray = NULL; //!< codec context per stream
                                               //!<
        int currFrameNumber            = -1;   //!< current decoded frame number
                                               //!<
        long int currOffset            =  0;   //!< current offset from recording start, sum duration of all video packets in AVStream->time_base
                                               //!<
        int iFrameCount                =  0;   //!< count of decoed i-frames
                                               //!<
        int64_t offsetTime_ms_LastFile =  0;   //!< offset from recording start of last file in ms
                                               //!<
        int64_t offsetTime_ms_LastRead =  0;   //!< offset from recodring start of last frame in ms
                                               //!<
        int firstMP2Index              = -1;   //!< stream index for first MP2 audio stream
                                               //!<
/**
 * decoded frame data
 */
        struct sFrameData {
            bool Valid = false;                //!< flag, if true data is valid
                                               //!<
            uchar *Plane[PLANES] = {};         //!< picture planes (YUV420)
                                               //!<
            int PlaneLinesize[PLANES] = {};    //!< size in bytes of each picture plane line
                                               //!<
        } FrameData;                           //!< decoded frame picture data
                                               //!<
        int interlaced_frame = -1;             //!< -1 undefined, 0 the content of the picture is progressive, 1 the content of the picture is interlaced
                                               //!<
        bool stateEAGAIN = false;              //!< true if decoder needs more frames, false otherwise
                                               //!<
        int videoRealFrameRate = 0;            //!< video stream real frame rate
                                               //!<
        int64_t dtsBefore = -1;                //!< DTS of frame before
                                               //!<
        int decodeErrorCount = 0;              //!< number of decoding errors
                                               //!<
        int decodeErrorFrame = -1;             //!< frame number of last decoding error
                                               //!<
};
#endif
