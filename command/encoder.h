/*
 * encoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"
#include "tools.h"
#include "index.h"

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
    AVFilterGraph *filterGraph  = nullptr;   //!< filter graph
    //!<
    AVFilterContext *filterSrc  = nullptr;   //!< filter source
    //!<
    AVFilterContext *filterSink = nullptr;   //!< filter sink
    //!<
};

/**
 * main encoder class
 */
class cEncoder : protected cTools {
public:

    /**
     * constructor
     * @param decoderParam      pointer to decoder
     * @param indexParam        recording index
     * @param recDirParam       recording directory
     * @param fullEncodeParam   true if full re-encodin
     * @param bestStreamParam   true only encode best video and audio stream
     * @param ac3ReEncodeParam  true if AC3 re-endcode with volume adjust
     */
    explicit cEncoder(cDecoder *decoderParam, cIndex *indexParam, const char* recDirParam, const bool fullEncodeParam, const bool bestStreamParam, const bool ac3ReEncodeParam);

    ~cEncoder();

    /**
     * reset encoder state and start write file from begin
     * @param passEncoder 0 for only one pass planed, 1 first pass of 2 pass encoding, 2 second pass of 2 pass encoding)
     */

    void Reset(const int passEncoder);

    /**
     * open output file
     * @return true if successful, false otherwise
     */
    bool OpenFile();

    /** cut out video from start frame number to stop frame number
     * @param startPos start frame position
     * @param stopPos  stop frame position
     * @return true if successful, false otherwise
     */
    bool CutOut(int startPos, int stopPos);

    /**
     * close output file
     * @return true if successful, false otherwise
     */
    bool CloseFile();

private:
    /** check if input file changed an set new decoder context
    */
    void CheckInputFileChange();

    /** get hwaccel encoder name appropriate hwaccel decoder
     * param streamIndexIn input stream index
     * @return name of encoder
     */
    char *GetEncoderName(const int streamIndexIn);

    /** write packet to output file
    * @param avpkt pointer to packet
    * @param reEncoded true if packet was re-encoded, false otherwise
    * @return true if successful, false otherwise
    */
    bool WritePacket(AVPacket *avpkt, const bool reEncoded);

    /** prepare video frame to encode
     * @return pointer to encoded packet
     */
    bool EncodeVideoFrame();

    /** prepare AC3 audio frame to encode
     * @return pointer to encoded packet
     */
    bool EncodeAC3Frame();

    /** get channel count of AC3 output stream
     * @param streamIndex  stream index
     * @return channel count
     */
    int GetAC3ChannelCount(const int streamIndex);

    /** send frame to encoder
     * @param streamIndexOut output sream index
     * @param avFrame      frame to encode
     */
    bool SendFrameToEncoder(const int streamIndexOut, AVFrame *avFrame);

    /** receive packet from encoder
     * @param streamIndexOut output stream index
     * @return  encoded packet, nullptr otherwise
     */
    AVPacket *ReceivePacketFromEncoder(const int streamIndexOut);

    /**
     * init encoder codec
     * @param streamIndexIn  input stream index
     * @param streamIndexOut output stream index
     * @return true if successful, false otherwise
     */
    bool InitEncoderCodec(const unsigned int streamIndexIn, const unsigned int streamIndexOut);

    /**
     * change audio encoder channel count
     * @param streamIndexIn  stream index input stream
     * @param streamIndexOut stream index output stream
     * @param avCodecCtxIn   input stream codec context
     * @return true if successful, false otherwise
     */
    bool ChangeEncoderCodec(const int streamIndexIn, const int streamIndexOut, AVCodecContext *avCodecCtxIn);

    /**
     * check statistic data after first pass, ffmpeg assert if something is invalid
     * @param max_b_frames number of maximum b-frames
     * @return true of valid, false otherwise
     */
    bool CheckStats(const int max_b_frames) const;

    cDecoder *decoder                 = nullptr;                  //!< decoder
    //!<
    bool useHWaccel                   = false;                    //!< encoder use hwaccel (same as decoder)
    //!<
    cIndex *index                     = nullptr;                  //!< index
    //!<
    const char *recDir                = nullptr;                  //!< recording directory
    //!<
    bool fullEncode                   = false;                    //!< true for full re-encode video and audio
    //!<
    bool bestStream                   = false;                    //!< true if only endcode best video and audio stream
    //!<
    bool ac3ReEncode                  = false;                    //!< true if ac3 re-encode with volume adjust
    //!<
    int fileNumber                    = 0;                        //!< input file number
    //!<
    AVFormatContext *avctxIn          = nullptr;                  //!< avformat context for input
    //!<
    AVFormatContext *avctxOut         = nullptr;                  //!< avformat context for output
    //!<
    AVCodecContext **codecCtxArrayIn  = nullptr;                  //!< avcodec context for each input stream
    //!<
    AVCodecContext **codecCtxArrayOut = nullptr;                  //!< avcodec context for each output stream
    //!<
    SwrContext **swrArray             = nullptr;                  //!< array of libswresample (lswr) for audiosample format conversion
    //!<
    int64_t ptsBefore                 = 0;                        //!< presentation timestamp of frame before
    //!<
    int64_t ptsBeforeCut              = INT64_MAX;                //!< presentation timestamp of frame before cut mark
    //!<
    int64_t ptsAfterCut               = 0;                        //!< presentation timestamp of frame after cut mark
    //!<
    int *streamMap                    = nullptr;                  //!< input stream to output stream map
    //!<
    int pass                          = 0;                        //!< encoding pass
    //!<
    int64_t dts[MAXSTREAMS]           = {0};                      //!< dts of last output packet
    //!<
    AVPixelFormat software_pix_fmt                = AV_PIX_FMT_NONE;  //!< software pixel format from decoder
    //!<
    cAC3VolumeFilter *volumeFilterAC3[MAXSTREAMS] = {nullptr};        //!< AC3 volume filter
    //!<

    /**
      * cut PTS/DTS infos
      */
    struct sCutInfo {
        int64_t startPosDTS         = 0;  //!< DTS timestamp of last start position
        //!<
        int64_t startPosPTS         = 0;  //!< PTS timestamp of last start position
        //!<
        int64_t stopPosDTS          = 0;  //!< DTS timestamp of last stop position
        //!<
        int64_t stopPosPTS          = 0;  //!< PTS timestamp of last stop position
        //!<
        int64_t offset              = 0;  //!< current PTS/DTS offset from input stream to output stream
        //!<
    } cutInfo;                            //!< infos of cut positions
    //!<

    /**
     * structure for statistic data for 2 pass encoding
     */
    struct sAVstatsIn {
        char *data    = nullptr; //!< statistic data generated from encoder
        //!<
        long int size = 0;       //!< size of statistic data
        //!<
    } stats_in;                  //!< variable for statistic data for 2 pass encoding
    //!<

#ifdef DEBUG_PTS_DTS_CUT
    int64_t inputPacketPTSbefore[MAXSTREAMS]  = {0};
    int64_t inputFramePTSbefore[MAXSTREAMS]   = {0};
    int64_t outputPacketPTSbefore[MAXSTREAMS] = {0};
    int64_t outputFramePTSbefore[MAXSTREAMS]  = {0};
    int frameOut = 0;
#endif
};
