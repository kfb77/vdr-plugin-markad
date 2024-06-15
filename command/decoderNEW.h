/*
 * decoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __decoderNEW_h_
#define __decoderNEW_h_

#include <vector>
#include <deque>
#include "global.h"
#include "index.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
// hwaccel
#include <libavutil/pixdesc.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

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


// supported libavcodec versions
//
// full supported markad ffmpeg versions (based on Ubuntu LTS has Standard Support, 5 years)
//
// #if LIBAVCODEC_VERSION_INT >= ((60<<16)+( 31<<8)+102)   ffmpeg 6.1.1  (Ubuntu 24.04, End of Standard Support: April 2029)
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+(134<<8)+100)   ffmpeg 4.4.2  (Ubuntu 22.04, End of Standard Support: April 2027)
// #if LIBAVCODEC_VERSION_INT >= ((58<<16)+( 54<<8)+100)   ffmpeg 4.2.7  (Ubuntu 20.04, End of Standard Support: April 2025)
#define LIBAVCODEC_VERSION_VALID ((58<<16)+( 54<<8)+100)   // oldest full supported version


// deprecated markad ffmpeg versions without full features and with limited support (based on Ubuntu LTS is End of Standard Support + 5 years time for system update)
//
// #if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)   ffmpeg 3.4.11 (Ubuntu 18.04, End of Life: April 2028)
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 60<<8)+100)   ffmpeg 2.8.17 (Ubuntu 16.04) End of Life: April 2026)
#define LIBAVCODEC_VERSION_DEPRECATED ((56<<16)+( 60<<8)+100)   // oldest deprecated version, older is invalid


// end of life markad ffmpeg versions
//
// #if LIBAVCODEC_VERSION_INT >= ((54<<16)+( 35<<8)+1)     avconv 9.20  (Ubuntu 14.04, End of Life: April 2024)


/**
 * main decoder class
 */
class cDecoderNEW {
public:

    /**
     * cDecoder constructor
     * @param threads        count threads of ffmpeg decoder
     * @param recordingIndex recording index class
     */
    explicit cDecoderNEW(const char *recDir, int threadsParam, const bool fullDecodeParam, const bool vaapiParam, cIndex *indexParam);

    ~cDecoderNEW();

    /**
     * set decoder to first/next file of the directory
     * @param recDir name of the recording directory
     * @return true if first/next ts file found, false otherwise
     */
    bool ReadNextFile();





    /**
     * get number of decoding error
     * @return number of decoding errors
     */
    int GetErrorCount() const;


    /**
     * setup decoder codec context for current file
     * @param filename file name
     * @return true if setup was successful, false otherwiese
     */
    bool InitDecoder(const char * filename);

    /**
     * get currently in progress TS file number
     * @return file number
     */
    int GetFileNumber() const;

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

    /**
     * read packet from current input ts file and fills avpkt
     * increase frame counter if video frame <br>
     * increase i-frame counter if video i-frame <br>
     * add presentation timestamp for each frame to ring buffer <br>
     * add offset from recording start for each i-frame to recording index <br>
     *
     * @return true if successful, false if av_read_frame failed (e.g. end of file)
     */
    bool ReadPacket();

    /**
     * read next packet from current input directory
     * do the file changes if necessary
     * @return true if successful, false if EOF of last ts file
     */
    bool ReadNextPacket();

    /**
    * get next picture from input stream
    * read next packet from input stream and decode packet
    * @return true if we have a valid picture
    */
    bool DecodeNextFrame();

    /**
    * get current picture from decoded frame
    * @return pointer to picture
    */
    sVideoPicture *GetVideoPicture();

    /**
     * get current packet
     * @return current packet
     */
    AVPacket *GetPacket();

    /**
     * seek decoder read position
     * only seek forward <br>
     * seek to i-frame before and start decode to fill decoder buffer
     * @param maContext   markad context
     * @param frameNumber frame number to seek
     * @return true if successful, false otherwise
     */
    bool SeekToFrame(sMarkAdContext *maContext, int frameNumber);

    /**
     * send packet to decoder
     * @return return code from avcodec_send_packet
     */
    int SendPacketToDecoder();

    /**
     * receive frame from decoder
     * @return return code from avcodec_receive_frame
     */
    int ReceiveFrameFromDecoder();

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
    bool IsVideoIPacket();


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

    /** get current read video packet number
     * @return current read packet number
     */
    int GetPacketNumber() const;

    /** get current decoded video frame number
     * @return current decoded frame number
     */
    int GetFrameNumber() const;

    /** get current number of processed i-frames
     * @return current number of processed i-frames
     */
    int GetIFrameCount() const;

    /**
     * check if video stream is interlaced
     * @return true if video stream is interlaced, false otherwise
     */
    bool IsInterlacedVideo() const;

    /** get number of i-frames between to frames
     * @param beginFrame
     * @param endFrame
     * @return get number of i-frames between beginFrame and endFrame
     */
    int GetIFrameRangeCount(int beginFrame, int endFrame);

private:
    char *recordingDir                 = nullptr;                 //!< name of recording directory
    //!<
    cIndex *index                      = nullptr;                 //!< recording index
    //!<
    int threads                        = 0;                       //!< thread count of decoder
    //!<
    bool fullDecode                    = false;                   //!< false if we decode only i-frames, true if we decode all frames
    //!<
    bool vaapi                         = false;                   //!< enable vaapi hardware accelerated video decode and encode
    //!<
    enum AVHWDeviceType hwDeviceType   = AV_HWDEVICE_TYPE_NONE ;  //!< hardware device type
    //!<
    enum AVPixelFormat hwPixelFormat   = AV_PIX_FMT_NONE;         //!< hardware decoder pixel format
    //!<
    AVBufferRef *hw_device_ctx         = nullptr;                 //!< hardware device context
    //!<
    struct SwsContext *nv12_to_yuv_ctx = nullptr;                 //!< pixel format conversion context
    //!<
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(1<<8)+100)               // ffmpeg 4.5
    const AVCodec *codec               = nullptr;                 //!< codec
    //!<
#else
    AVCodec *codec                     = nullptr;                 //!< codec
    //!<
#endif
    int fileNumber                     = 0;                       //!< current ts file number
    //!<
    AVFormatContext *avctx             = nullptr;                 //!< avformat context
    //!<
    AVPacket avpkt                     = {};                      //!< packet from input file
    //!<
    AVFrame avFrame                    = {};                      //!< decoded frame
    //!<
    AVCodecContext **codecCtxArray     = nullptr;                 //!< codec context per stream
    //!<
    int packetNumber                   = -1;                      //!< current read video packet number
    //!<
    int frameNumber                    = -1;                      //!< current decoded video frame number
    //!<
    std::deque<int> frameNumberMap;                               //!< ring buffer to map frameNumer to packetNumber without full decoding
    //!<
    int decoderSendState               = 0;                       //!< last return code of avcodec_send_packet()
    //!<
    sVideoPicture videoPicture         = {};                      //!< current decoded video picture
    //!<
    long int currOffset                =  0;                      //!< current offset from recording start, sum duration of all video packets in AVStream->time_base
    //!<
    int iFrameCount                    =  0;                      //!< count of decoed i-frames
    //!<
    int64_t offsetTime_ms_LastFile     =  0;                      //!< offset from recording start of last file in ms
    //!<
    int64_t offsetTime_ms_LastRead     =  0;                      //!< offset from recodring start of last frame in ms
    //!<
    int firstMP2Index                  = -1;                      //!< stream index for first MP2 audio stream
    //!<
    int interlaced_frame = -1;             //!< -1 undefined, 0 the content of the picture is progressive, 1 the content of the picture is interlaced
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
