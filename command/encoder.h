/*
 * encoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
#include <libavutil/opt.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libswresample/swresample.h>
}


#define VOLUME 2dB

/**
 * libav volume filter class
 */
class cAC3VolumeFilter {
public:
    cAC3VolumeFilter();
    ~cAC3VolumeFilter();

    /**
     * init libav volume filter
     * @param  channel_layout audio channel layout
     * @param  sample_fmt     audio sample format
     * @param  sample_rate    samples per second
     * @return true if volume filter graph was successful created, false otherwise
     */
#if LIBAVCODEC_VERSION_INT >= ((59<<16)+( 25<<8)+100)
    bool Init(const AVChannelLayout channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate);
#else
    bool Init(const uint64_t channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate);
#endif

    /**
     * send frame to volume filter graph
     * @param avFrame audio frame
     * @return true if successful, false otherwise
     */
    bool SendFrame(AVFrame *avFrame);

    /**
     * receive frame from volume filter graph
     * @param avFrame audio frame
     * @return true if successful, false otherwise
     */
    bool GetFrame(AVFrame *avFrame);

private:
    AVFilterGraph *filterGraph = nullptr;   //!< filter graph
    //!<
    AVFilterContext *filterSrc = nullptr;   //!< filter source
    //!<
    AVFilterContext *filterSink = nullptr;  //!< filter sink
    //!<
};

/**
 * main encoder class
 */
class cEncoder {
public:

    /**
     * constructor
     * @param decoderParam      pointer to decoder
     * @param fullEncodeParam   true if full re-encodin
     * @param bestStreamParam   true only encode best video and audio stream
     * @param ac3ReEncodeParam  true if AC3 re-endcode with volume adjust
     */
    explicit cEncoder(cDecoder *decoderParam, const bool fullEncodeParam, const bool bestStreamParam, const bool ac3ReEncodeParam);

    ~cEncoder();

    /**
     * reset encoder state and start write file from begin
     * @param passEncoder 0 for only one pass planed, 1 first pass of 2 pass encoding, 2 second pass of 2 pass encoding)
     */

    void Reset(const int passEncoder);

    /**
     * open output file
     * @param directory    output directory
     * @param decoder      encoder class
     * @return true if successful, false otherwise
     */
    bool OpenFile(const char *directory, cDecoder *decoder);

    /** write packet to output file
     * @return true if successful, flase otherwise
     */
    bool WritePacket();

    /**
     * close output file
     * @param decoder decoder class
     * @return true if successful, false otherwise
     */
    bool CloseFile(cDecoder *decoder);

private:
    /** encode frame
     * @param decoder decoder
     * @param avCodecCtx   codec context
     * @param avFrame      decodes frame
     * @param avpkt        encoded packet
     */
    bool EncodeFrame(cDecoder *decoder, AVCodecContext *avCodecCtx, AVFrame *avFrame, AVPacket *avpkt);

    /**
     * init encoder codec
     * @param decoder   decoder
     * @param directory      recording directory
     * @param streamIndexIn  input stream index
     * @param streamIndexOut output stream index
     * @return true if successful, false otherwise
     */
    bool InitEncoderCodec(cDecoder *decoder, const char *directory, const unsigned int streamIndexIn, const unsigned int streamIndexOut);

    /**
     * change audio encoder channel count
     * @param decoder   decoder
     * @param streamIndexIn  stream index input stream
     * @param streamIndexOut stream index output stream
     * @param avCodecCtxIn   input stream codec context
     * @return true if successful, false otherwise
     */
    bool ChangeEncoderCodec(cDecoder *decoder, const int streamIndexIn, const int streamIndexOut, AVCodecContext *avCodecCtxIn);

    /**
     * save video frame as picture, used for debugging
     * @param frame   framenumber
     * @param avFrame frame data
     */
    void SaveFrame(const int frame, AVFrame *avFrame);

    /**
     * resamle audio frame
     * @param[in] avFrameIn   input frame
     * @param[out] avFrameOut output frame
     * @param streamIndex     stream index
     * @return true if successful, false otherwise
     */
    bool ReSampleAudio(AVFrame *avFrameIn, AVFrame *avFrameOut, const int streamIndex);

    /**
     * check statistic data after first pass, ffmpeg assert if something is invalid
     * @param max_b_frames number of maximum b-frames
     * @return true of valid, false otherwise
     */
    bool CheckStats(const int max_b_frames) const;

    cDecoder *decoder                 = nullptr;                  //!< decoder
    //!<
    bool fullEncode                   = false;                    //!< true for full re-encode video and audio
    //!<
    bool bestStream                   = false;                    //!< true if only endcode best video and audio stream
    //!<
    bool ac3ReEncode                  = false;                    //!< true if ac3 re-encode with volume adjust
    //!<
    int decoderFrameNumber            = -1;                       //!< current frame number of decoder
    //!<
    AVFormatContext *avctxIn = nullptr;                           //!< avformat context for input
    //!<
    AVFormatContext *avctxOut = nullptr;                          //!< avformat context for output
    //!<
    AVCodecContext **codecCtxArrayIn = nullptr;                   //!< avcodec context for each input stream
    //!<
    AVCodecContext **codecCtxArrayOut = nullptr;                  //!< avcodec context for each output stream
    //!<
    SwrContext **swrArray = nullptr;                              //!< array of libswresample (lswr) for audiosample format conversion
    //!<
    int64_t ptsBefore = 0;                                        //!< presentation timestamp of frame before
    //!<
    int64_t ptsBeforeCut = INT64_MAX;                             //!< presentation timestamp of frame before cut mark
    //!<
    int64_t ptsAfterCut = 0;                                      //!< presentation timestamp of frame after cut mark
    //!<
    bool stateEAGAIN = false;                                     //!< true if encoder needs more frame, false otherwise
    //!<
    cAC3VolumeFilter *volumeFilterAC3[MAXSTREAMS] = {nullptr};  //!< AC3 volume filter
    //!<

    /**
     * encoder status
     */
    struct sEncoderStatus {
        int64_t videoStartDTS           = INT64_MAX;  //!< DTS timestamp of the video stream from first mark
        //!<
        int frameBefore                 = -2;         //!< decoded frame number before current frame
        //!<
        int64_t *ptsInBefore            = nullptr;       //!< presentation timestamp of the previous frame from each input stream
        //!<
        int64_t *dtsInBefore            = nullptr;       //!< decoding timestamp of the previous frame from each input stream
        //!<
        int64_t ptsOutBefore            = -1;         //!< presentation timestamp of the previous frame from video output stream
        //!<

        int64_t pts_dts_CutOffset       = 0;          //!< offset from the cuted out frames
        //!<
        int64_t *pts_dts_CyclicalOffset = nullptr;       //!< offset from pts/dts cyclicle of each frame, multiple of 0x200000000
        //!<
        bool videoEncodeError           = false;      //!< true if we got an encoder error, false otherwise
        //!<
    } EncoderStatus;                                  //!< encoder status

    //!<
    int *streamMap = nullptr;                       //!< input stream to output stream map
    //!<
    int pass = 0;                                //!< encoding pass
    //!<
    /**
     * structure for statistic data for 2 pass encoding
     */
    struct sAVstatsIn {
        char *data = nullptr; //!< statistic data generated from encoder
        //!<
        long int size = 0; //!< size of statistic data
        //!<
    } stats_in;            //!< variable for statistic data for 2 pass encoding
    //!<

#ifdef DEBUG_CUT
    int64_t inputPacketPTSbefore[MAXSTREAMS]  = {0};
    int64_t inputFramePTSbefore[MAXSTREAMS]   = {0};
    int64_t outputPacketPTSbefore[MAXSTREAMS] = {0};
    int64_t outputFramePTSbefore[MAXSTREAMS]  = {0};
    int frameOut = 0;
#endif
};
