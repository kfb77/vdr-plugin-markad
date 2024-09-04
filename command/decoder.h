/*
 * decoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __decoder_h_
#define __decoder_h_

#include <vector>
// #include <deque>
#include <chrono>

#include "global.h"
#include "debug.h"
#include "tools.h"
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
#include <libavutil/avutil.h>

#if LIBAVCODEC_VERSION_INT >= ((59<<16)+(3<<8)+102)
#include <libavutil/channel_layout.h>
#endif
}


#define AVLOGLEVEL AV_LOG_ERROR
// #define AVLOGLEVEL AV_LOG_VERBOSE


// error codes from AC3 parser
#define AAC_AC3_PARSE_ERROR_SYNC         -0x1030c0a
#define AAC_AC3_PARSE_ERROR_BSID         -0x2030c0a
#define AAC_AC3_PARSE_ERROR_SAMPLE_RATE  -0x3030c0a
#define AAC_AC3_PARSE_ERROR_FRAME_SIZE   -0x4030c0a
#define AAC_AC3_PARSE_ERROR_FRAME_TYPE   -0x5030c0a
#define AAC_AC3_PARSE_ERROR_CRC          -0x6030c0a
#define AAC_AC3_PARSE_ERROR_CHANNEL_CFG  -0x7030c0a


// supported libavcodec versions
//
// full supported FFmpeg versions for markad
//
//  #if LIBAVCODEC_VERSION_INT >= ((60<<16)+( 31<<8)+102) FFmpeg 6.1.1  (Ubuntu 24.04, End of Standard Support: April 2029)
//  #if LIBAVCODEC_VERSION_INT >= ((58<<16)+(134<<8)+100) FFmpeg 4.4.2  (Ubuntu 22.04, End of Standard Support: April 2027)
#define LIBAVCODEC_VERSION_VALID  ((58<<16)+(134<<8)+100) // oldest full supported version

// deprecated markad FFmpeg versions without full features and with limited support
//
//  #if LIBAVCODEC_VERSION_INT >= ((58<<16)+( 54<<8)+100) FFmpeg 4.2.7  (Ubuntu 20.04, End of Standard Support: April 2025), same libavcodec version as 4.2.10
//                                                                      issues found with V4.2.7 (not recommended to use with hwaccel):
//                                                                      very low hwaccel decoding performance
//                                                                      memory leak in av_hwframe_transfer_data(), fixed in 4.2.10
//                                                                      i-frame only hwaccel decoding with H.264 does not work
//                                                                      i-frame only decoding with interlaced H.264 does not work
#define LIBAVCODEC_VERSION_DEPRECATED ((58<<16)+( 54<<8)+100) // oldest deprecated version, older is invalid

// end of life FFmpeg versions for markad
//
// #if LIBAVCODEC_VERSION_INT >= ((57<<16)+(107<<8)+100)   FFmpeg 3.4.11 (Ubuntu 18.04, End of Life: April 2028)
// #if LIBAVCODEC_VERSION_INT >= ((56<<16)+( 60<<8)+100)   FFmpeg 2.8.17 (Ubuntu 16.04) End of Life: April 2026)
// #if LIBAVCODEC_VERSION_INT >= ((54<<16)+( 35<<8)+  1)   avconv 9.20   (Ubuntu 14.04, End of Life: April 2024)


/**
 * main decoder class
 */
class cDecoder : protected cTools {
public:

    /**
     * cDecoder constructor
     * @param recDir               recording directory
     * @param threadsParam         count threads of FFmpeg decoder
     * @param fullDecodeParam      true if full decode, fals if only decode i-frames
     * @param hwaccelParam         true if we use hwaccel
     * @param forceHWparam         true if force use of hwaccel on MPEG2 codec
     * @param forceInterlacedParam true to inform decoder with hwaccel, video is interlaced
     * @param indexParam           recording index class
     */
    explicit cDecoder(const char *recDir, int threadsParam, const bool fullDecodeParam, char *hwaccelParam, const bool forceHWparam, const bool forceInterlacedParam, cIndex *indexParam);

    ~cDecoder();

    /**
    * restart decoder to first frame of first file
    */
    bool Restart();

    /** get current input file number
     */
    int GetFileNumber() const {
        return fileNumber;
    }

    /**
    * get max input file number
    * return input file number
    */
    int GetMaxFileNumber() const {
        return maxFileNumber;
    }

    /**
    * get full decoder state
    */
    bool GetFullDecode() const;

    /**
    * get FFmpeg threads
    */
    int GetThreads() const;

    /**
    * get name of hwaccel methode
    * @return name of hwaccel methode
    */
    char *GetHWaccelName();

    /**
    * get recording directory
    */
    const char *GetRecordingDir() const;

    /**
    * get force hwaccel
    */
    bool GetForceHWaccel() const;

    /**
    * get decoder thread count
    */
    int GetThreadCount() const;

    /**
     * set decoder to first/next file of the directory
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
     * get libav format context
     * @return AVFormatContext
     */
    AVFormatContext *GetAVFormatContext();

    /** get libav codec context
     * @return AVCodecContext
     */
    AVCodecContext **GetAVCodecContext();

    /** get hardware device type
     * @return hardware device type
     */
    enum AVHWDeviceType GetHWDeviceType() const;

    /**
     * get markad internal video type
     * return video type (MARKAD_PIDTYPE_VIDEO_H262, MARKAD_PIDTYPE_VIDEO_H264 or MARKAD_PIDTYPE_VIDEO_H265)
     */
    int GetVideoType() const;

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
     * get video frame rate
     * @return video frame rate
     */
    int GetVideoFrameRate();

    /**
     * get average video frame rate taken from avctx->streams[i]->avg_frame_rate
     * @return average video frame rate (avg_frame_rate)
     */
//    int GetVideoAvgFrameRate();

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
    * send current packet (no ream from file) to decoder and receive decoded frame
    * @return true if send and receive was successful, false otherwise
    */
    bool DecodePacket();

    /**
    * get next packet(s) from input file, send to decoder and read next frame from decoder
    * read next packet from input stream and decode packet
    * @param  audioDecode true if decode audio packets, false otherwise
    * @return true if we have a valid picture
    */
    bool DecodeNextFrame(const bool audioDecode);

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
     * get current frame
     * param pixelFormat  requested pixel format, AV_PIX_FMT_NONE if accept everyone
     * @return current frame
     */
    AVFrame *GetFrame(enum AVPixelFormat pixelFormat);

    /**
    * seek read position of recording
    * seek frame is read but not decoded
    * @param seekPacketNumber packet number to seek
    * @return                 true if successful, false otherwise
    */
    bool SeekToPacket(int seekPacketNumber);

    /**
     * send packet to decoder
     * @return return code from avcodec_send_packet
     */
    int SendPacketToDecoder(const bool flush);

    /**
     * receive frame from decoder
     * @return return code from avcodec_receive_frame
     */
    int ReceiveFrameFromDecoder();

    /** check if stream is video stream
     * @param streamIndex stream index
     * @return true if video stream, false otherwise
     */
    bool IsVideoStream(const unsigned int streamIndex) const;

    /**
     * check if current packet is from a video stream
     * @return true if current packet is from a video stream, false otherwise
     */
    bool IsVideoPacket() const;

    /**
     * check if current packet is a video key packet
     * @return true if current packet is a video key packet, false otherwise
     */
    bool IsVideoKeyPacket() const;

    /**
     * check if current frame is a video frame
     * @return true if current frame is a video frame, false otherwise
     */
    bool IsVideoFrame() const;

    /**
     * check if current frame is a video i-frame
     * @return true if current frame is a video i-frame, false otherwise
     */
//    bool IsVideoIFrame() const;

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
    bool IsAudioStream(const unsigned int streamIndex) const;

    /** check if current packet is audio
     * @return true if current packet is audio, false otherwise
     */
    bool IsAudioPacket() const;

    /** get current channel count of AC3 stream
     * param streamIndex optional stream index
     * @return channel count of AC3, 0 if no AC3 stream exists or streamIndex is no AC3 stream
     */
    int GetAC3ChannelCount(const int streamIndex = -1);

    /** get last channel change
     * @return pointer to sAudioAC3Channels structure if there was a change, nullptr otherwise
     */
    sAudioAC3Channels *GetChannelChange();

    /** get current frame PTS
     * @return PTS
     */
    int64_t GetFramePTS() const;

    /** get current packet PTS
     * @return PTS
     */
    int64_t GetPacketPTS() const;

    /** get current vulume of MP2 stream
     * @return MP2 volume
     */
    int GetVolume();

    /** check if stream is subtitle
     * @param streamIndex stream index
     * @return true if stream is subtitle, false otherwise
     */
    bool IsSubtitleStream(const unsigned int streamIndex) const;

    /** get current read video packet number
     * @return current packet number
     */
    int GetPacketNumber() const;

    /**
     * check if video stream is interlaced
     * @return true if video stream is interlaced, false otherwise
     */
    bool IsInterlacedFrame() const;

    /** get number of i-frames between to frames
     * @param beginFrame
     * @param endFrame
     * @return get number of i-frames between beginFrame and endFrame
     */
    int GetIFrameRangeCount(int beginFrame, int endFrame);

    /// get aspect ratio of current frame
    /**
     * @return  aspect ratio of current frame
     */
    sAspectRatio *GetFrameAspectRatio();

    /** check if current packet is a subtitle
     * @return true if current packet is subtitle, false otherwise
     */
//    bool IsSubtitlePacket();

    /** get pixel format of video stream
     * @return enum AVPixelFormat of video stream
     */
    enum AVPixelFormat GetVideoPixelFormat() const;

    /** drop currect decoded frame from GPU buffer
     *  use by logo extraction to skip frames
     */
    void DropFrameFromGPU();

private:
    /**
     * codec info structure
    */
    typedef struct sCodecInfo {
        const AVCodec *codec = nullptr;  //!<  pointer to codec
        //!<
        bool noHWaccelMPEG2  = false;    //!<  hwaccel decoder can decode MPEG2 codec
        //!<
        bool hwaccelDevice   = true;     //!<  hwaccel decoder uses hardware device
        //!<
    } sCodecInfo;

    /**
     * PTS to frame cache
    */
    typedef struct sPtsFrameCache {
        int64_t pts     = -1;  //!<  cached PTS
        //!<
        int frameNumber = -1;  //!<  frame number to cached PTS
    } sPtsFrameCache;

    /** read a line from a file
     * @param buf  read buffer
     * @param size buffer size
     * @param file name of file
     * @return  size of data read
     */
    static size_t ReadLineFromFile(char *buf, size_t size, char *file);

    /** read ARM hardware info and set codec requirements
     * @param codecInfo codec info structure
     */
    static void ReadHWPlatform(sCodecInfo *codecInfo);

    /** get codec object from codec ID
     * @param codecID    codec id
     * @param codecInfo  codec info structure
     */
    void GetVideoCodec(AVCodecID codecID, sCodecInfo *codecInfo) const;

    /** convert frame pixel format to AV_PIX_FMT_YUV420P
     * @param pixelFormat   target pixel format
     * @return true if successful, false otherwise
     */
    bool ConvertVideoPixelFormat(enum AVPixelFormat pixelFormat);

    /** set start and end time of decoding, use for statitics
     * @param start true for start decoding, false otherwise
     */
    void Time(bool start);

    /**
     * reset decoder
     */
    void Reset();

    /**
     * free codec context of decoder
     */
    void FreeCodecContext();

    /**
     * complete reset decoder without hwaccel
     * @return return code from avcodec_send_packet
     */
    int ResetToSW();

    char *recordingDir                 = nullptr;                 //!< name of recording directory
    //!<
    cIndex *index                      = nullptr;                 //!< recording index
    //!<
    int threads                        = 0;                       //!< thread count of decoder
    //!<
    bool fullDecode                    = false;                   //!< false if we decode only i-frames, true if we decode all frames
    //!<
    char *hwaccel                      = nullptr;                 //!< hardware accelerated methode
    //!<
    bool firstHWaccelReceivedOK        = false;                   //!< true if first video packet was successful received from decoder
    //!<
    bool forceInterlaced               = false;                   //!< inform decoder used hwaccel this video is interlaced
    //!<
    bool useHWaccel                    = false;                   //!< enable hardware accelerated video decode and encode
    //!<
    bool forceHWaccel                  = false;                   //!< force use of hardware accelerated video decode and encode for MEPG2
    //!<
    enum AVHWDeviceType hwDeviceType   = AV_HWDEVICE_TYPE_NONE ;  //!< hardware device type
    //!<
    bool logHwaccel                    = true;                    //!< true before fist log of hwaccel methode
    //!<
    enum AVPixelFormat hwPixelFormat   = AV_PIX_FMT_NONE;         //!< hardware decoder pixel format
    //!<
    AVBufferRef *hw_device_ctx         = nullptr;                 //!< hardware device context
    //!<
    struct SwsContext *swsContext      = nullptr;                 //!< pixel format conversion context
    //!<
    int fileNumber                     = 0;                       //!< current ts file number
    //!<
    int maxFileNumber                  = 0;                       //!< max ts file number
    //!<
    AVFormatContext *avctx             = nullptr;                 //!< avformat context
    //!<
    AVPacket avpkt                     = {};                      //!< packet from input file
    //!<
    AVFrame avFrame                    = {};                      //!< decoded frame
    //!<
    AVFrame avFrameConvert             = {};                      //!< decoded frame after pixel format transformation
    //!<
    bool frameValid                    = false;                   //!< decoding was successful, current avFrame content is valid
    //!<
    AVCodecContext **codecCtxArray     = nullptr;                 //!< codec context per stream
    //!<
    int packetNumber                   = -1;                      //!< current read video packet number
    //!<
    int frameNumber                    = -1;                      //!< current decoded frame number
    //!<
    sPtsFrameCache ptsFrameCache       = {};                      //!< cached PTS to frame map
    //!<
    int videoWidth                     = 0;                       //!< video width
    //!<
    int videoHeight                    = 0;                       //!< video height
    //!<
    bool eof                           = false;                   //!< true if end of all ts files reached
    //!<
    int decoderSendState               = 0;                       //!< last return code of SendPacketToDecoder()
    //!<
    int decoderReceiveState            = -EAGAIN;                 //!< last return code of ReceiveFrameFromDecoder()
    //!<
    bool decoderRestart                = true;                    //!< true if decoder has to be restarted with a key packet (at start or after seek)
    //!<
    sVideoPicture videoPicture         = {};                      //!< current decoded video picture
    //!<
    sAspectRatio DAR                   = {0};                     //!< display aspect ratio of current frame
    //!<
    sAspectRatio beforeDAR             = {0};                     //!< display aspect ratio of frame before
    //!<
    int64_t sumDuration                =  0;                      //!< current offset from recording start, sum duration of all video packets in AVStream->time_base
    //!<
    int firstMP2Index                  = -1;                      //!< stream index for first MP2 audio stream
    //!<
    int frameRate                      = 0;                       //!< video stream real frame rate
    //!<
    int64_t dtsBefore                  = -1;                      //!< DTS of frame before
    //!<
    int decodeErrorCount               = 0;                       //!< number of decoding errors
    //!<
    int decodeErrorFrame               = -1;                      //!< frame number of last decoding error
    //!<
    bool timeStartCalled               = false;                   //!< state of Time(true) was called
    //!<
    std::chrono::high_resolution_clock::time_point startDecode;   //!< time stamp of SendPacketToDecoder()
    //!<
    sAudioAC3Channels audioAC3Channels[MAXSTREAMS] = {};          //!< AC3 audio stream channel count state
    //!<
};
#endif
