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
    AVFilterGraph *filterGraph = NULL;   //!< filter graph
    //!<
    AVFilterContext *filterSrc = NULL;   //!< filter source
    //!<
    AVFilterContext *filterSink = NULL;  //!< filter sink
    //!<
};

/**
 * main encoder class
 */
class cEncoder {
public:

    /**
     * contructor
     * @param macontext markad context
     */
    explicit cEncoder(sMarkAdContext *macontext);

    ~cEncoder();

    /**
     * reset encoder state and start write file from begin
     * @param passEncoder 0 for only one pass planed, 1 first pass of 2 pass encoding, 2 second pass of 2 pass encoding)
     */

    void Reset(const int passEncoder);

    /**
     * open output file
     * @param directory    output directory
     * @param ptr_cDecoder encoder class
     * @return true if successful, false otherwise
     */
    bool OpenFile(const char *directory, cDecoder *ptr_cDecoder);

    /** write packet to output file
     * @param pktIn packet from input stream
     * @param ptr_cDecoder decoder class
     * @return true if sucessful, flase otherwise
     */
    bool WritePacket(AVPacket *pktIn, cDecoder *ptr_cDecoder);

    /**
     * close output file
     * @param ptr_cDecoder decoder class
     * @return true if sucessful, false otherwise
     */
    bool CloseFile(cDecoder *ptr_cDecoder);

private:
    /** encode frame
     * @param ptr_cDecoder decoder
     * @param avCodexCtx   codec context
     * @param avFrame      decodes frame
     * @param avpkt        encoded packet
     */
    bool EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodexCtx, AVFrame *avFrame, AVPacket *avpkt);

    /**
     * init encoder codec
     * @param ptr_cDecoder   decoder
     * @param directory      recording directory
     * @param streamIndexIn  input stream index
     * @param streamIndexOut output stream index
     * @return true if successful, false otherwise
     */
    bool InitEncoderCodec(cDecoder *ptr_cDecoder, const char *directory, const unsigned int streamIndexIn, const unsigned int streamIndexOut);

    /**
     * change audio encoder channel count
     * @param ptr_cDecoder   decoder
     * @param streamIndexIn  stream index input stream
     * @param streamIndexOut stream index output stream
     * @param avCodecCtxIn   input stream codec context
     * @return true if successful, false otherwise
     */
    bool ChangeEncoderCodec(cDecoder *ptr_cDecoder, const int streamIndexIn, const int streamIndexOut, AVCodecContext *avCodecCtxIn);

    /**
     * save video frame as picture, used for debugging
     * @param frame   frame number
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
    bool CheckStats(const int max_b_frames);

    sMarkAdContext *maContext;                                    //!< markad context
    //!<
    int threadCount = 0;                                          //!< encoder thread count
    //!<
    AVFormatContext *avctxIn = NULL;                              //!< avformat context for input
    //!<
    AVFormatContext *avctxOut = NULL;                             //!< avformat context for output
    //!<
    AVCodecContext **codecCtxArrayIn = NULL;                      //!< avcodec context for each input stream
    //!<
    AVCodecContext **codecCtxArrayOut = NULL;                     //!< avcodec context for each output stream
    //!<
    SwrContext **swrArray = NULL;                                 //!< array of libswresample (lswr) for audiosample format conversion
    //!<
    int64_t ptsBefore = 0;                                        //!< presentation timestamp of frame before
    //!<
    int64_t ptsBeforeCut = INT64_MAX;                             //!< presentation timestamp of frame before cut mark
    //!<
    int64_t ptsAfterCut = 0;                                      //!< presentation timestamp of frame after cut mark
    //!<
    bool stateEAGAIN = false;                                     //!< true if encoder needs more frame, false otherwise
    //!<
    cAC3VolumeFilter *ptr_cAC3VolumeFilter[MAXSTREAMS] = {NULL};  //!< AC3 volume filter
    //!<

    /**
     * encoder status
     */
    struct sEncoderStatus {
        int64_t videoStartPTS = INT64_MAX;       //!< decoded presentation timestamp of of the video stream from first mark
        //!<
        int frameBefore = -2;                    //!< decoded frame number before current frame
        //!<
        int64_t *ptsInBefore = NULL;             //!< presentation timestamp of the previous frame from each input stream
        //!<
        int64_t *dtsInBefore = NULL;             //!< decoding timestamp of the previous frame from each input stream
        //!<
        int64_t ptsOutBefore = -1;               //!< presentation timestamp of the previous frame from video output stream
        //!<

        int64_t pts_dts_CutOffset = 0;           //!< offset from the cuted out frames
        //!<
        int64_t *pts_dts_CyclicalOffset = NULL;  //!< offset from pts/dts cyclicle of each frame, multiple of 0x200000000
        //!<
        bool videoEncodeError = false;           //!< true if we got an encoder error, false otherwise
        //!<
    } EncoderStatus;                             //!< encoder status
    //!<
    int *streamMap = NULL;                       //!< input stream to output stream map
    //!<
    int pass = 0;                                //!< encoding pass
    //!<
    /**
     * structure for statistic data for 2 pass encoding
     */
    struct sAVstatsIn {
        char *data = NULL; //!< statistic data generated from encoder
        //!<
        long int size = 0; //!< size of statistic data
        //!<
    } stats_in;            //!< variable for statistic data for 2 pass encoding
    //!<

#ifdef DEBUG_CUT
    int64_t inputPacketPTSbefore[MAXSTREAMS] = {0};
    int64_t beforeDecodePacketPTSbefore[MAXSTREAMS] = {0};
    int64_t beforeEncodeFramePTSbefore[MAXSTREAMS] = {0};
    int64_t afterEncodePacketPTSbefore[MAXSTREAMS] = {0};
    int packet_out = 0;
#endif
};
