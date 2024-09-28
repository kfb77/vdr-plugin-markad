/*
 * encoder.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"
#include "tools.h"
#include "index.h"
#include "marks.h"

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


enum {
    CUT_MODE_INVALID   = -1,
    CUT_MODE_KEY       =  0,
    CUT_MODE_SMART     =  1,
    CUT_MODE_FULL      =  2
};


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
     * @param cutModeParam      cut mode
     * @param bestStreamParam   true only encode best video and audio stream
     * @param ac3ReEncodeParam  true if AC3 re-endcode with volume adjust
     */
    explicit cEncoder(cDecoder *decoderParam, cIndex *indexParam, const char* recDirParam, const int cutModeParam, const bool bestStreamParam, const bool ac3ReEncodeParam);

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

    /** cut out video from start PTS to stop PTS
     * @param startMark start mark
     * @param stopMark  stop mark
     * @return true if successful, false otherwise
     */
    bool CutOut(cMark *startMark, cMark *stopMark);

    /**
     * close output file
     * @return true if successful, false otherwise
     */
    bool CloseFile();

private:
    /** cut out video from start position to stop position of mark with full re-encode
     * @param startMark start mark
     * @param stopMark  stop mark
     * @return true if successful, false otherwise
     */
    bool CutFullReEncode(cMark *startMark, cMark *stopMark);

    /** smart cut video from start PTS to stop PTS of mark
     * @param startMark start mark
     * @param stopMark  stop mark
     * @return true if successful, false otherwise
     */
    bool CutSmart(cMark *startMark, cMark *stopMark);

    /** cut out H.265 video from key packet after start mark to key packet before stop mark
     * @param startMark start mark
     * @param stopMark  stop mark
     * @return true if successful, false otherwise
     */
    bool CutKeyPacket(cMark *startMark, cMark *stopMark);

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
     * @param addOutStream   if true add output stream will be added to format context
     * @param forcePixFmt    force this pixel format
     * @param verbose        if true log codec parameters
     * @return               true if successful, false otherwise
     */
    bool InitEncoderCodec(const unsigned int streamIndexIn, const unsigned int streamIndexOut, const bool addOutStream, AVPixelFormat forcePixFmt, const bool verbose);

    /**
     * reset decoder and encoder codex context
     * have to start with empty decoder end encoder queues
     * @return true if successful, false otherwise
     */
    bool ResetDecoderEncodeCodec();

    /**
     * drain decoder and encoder queue
     * @param stopPTS  PTS of stop mark
     * @return true if successful, false otherwise
     */
    bool DrainVideoReEncode(const int64_t stopPTS);

    /**
     * calculate and set encoder queue PTS/DTS offset for smart re-encode
     * @param avpkt current output packet
     * @param startPart true if re-encode around start mark, false otherwise
     */
    void SetSmartReEncodeOffset(AVPacket *avpkt, const bool startPart);

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

    /**
     * get next p-slice after PTS
     * used if we have no p-slice index from decoder because of no full decode
     * @param pts                       presentation timestamp
     * @param pSlicePTS                 PTS from key packet number of next p-slice
     * @param keyPacketNumberBeforeStop key packet before next stop
     * @return                          key packet number of next p-slice
     */
    int GetPSliceKeyPacketNumberAfterPTS(int64_t pts, int64_t *pSlicePTS, const int keyPacketNumberBeforeStop);

    cDecoder *decoder                 = nullptr;                  //!< decoder
    //!<
    cDecoder *decoderLocal            = nullptr;                  //!< local decoder, used if we have no p-slice index
    //!<
    bool useHWaccel                   = false;                    //!< encoder use hwaccel (same as decoder)
    //!<
    cIndex *index                     = nullptr;                  //!< index
    //!<
    cIndex *indexLocal                = nullptr;                  //!< local index, used if we have no p-slice index
    //!<
    const char *recDir                = nullptr;                  //!< recording directory
    //!<
    int cutMode                       = CUT_MODE_INVALID;         //!< cut mode
    //!<
    bool bestStream                   = false;                    //!< true if only endcode best video and audio stream
    //!<
    bool ac3ReEncode                  = false;                    //!< true if ac3 re-encode with volume adjust
    //!<
    int fileNumber                    = 0;                        //!< input file number
    //!<
    bool forceIFrame                  = false;                    //!< force next encoded frame to i-frame
    //!<
    AVFormatContext *avctxIn          = nullptr;                  //!< avformat context for input
    //!<
    AVFormatContext *avctxOut         = nullptr;                  //!< avformat context for output
    //!<
    AVCodecContext **codecCtxArrayIn  = nullptr;                  //!< avcodec context for each input stream
    //!<
    int64_t ptsBefore                 = 0;                        //!< presentation timestamp of frame before
    //!<
    int64_t ptsBeforeCut              = INT64_MAX;                //!< presentation timestamp of frame before cut mark
    //!<
    int64_t ptsAfterCut               = 0;                        //!< presentation timestamp of frame after cut mark
    //!<
    int streamMap[MAXSTREAMS]         = {-1};                     //!< input stream to output stream map
    //!<
    int videoInputStreamIndex         = -1;                       //!< video input stream index
    //!<
    int videoOutputStreamIndex        = -1;                       //!< video output stream index
    //!<
    int pass                          = 0;                        //!< encoding pass
    //!<
    bool rollover                     = false;                    //!< PTS/DTS rollover
    //!<
    SwrContext *swrArray[MAXSTREAMS]              = {nullptr};        //!< array of libswresample (lswr) for audiosample format conversion
    //!<
    AVCodecContext *codecCtxArrayOut[MAXSTREAMS]  = {nullptr};        //!< avcodec context for each output stream
    //!<
    AVPixelFormat software_pix_fmt                = AV_PIX_FMT_NONE;  //!< software pixel format from decoder
    //!<
    cAC3VolumeFilter *volumeFilterAC3[MAXSTREAMS] = {nullptr};        //!< AC3 volume filter
    //!<


    /**
      * stream PTS/DTS infos
      */
    struct sStreamInfo {
        int64_t lastInPTS[MAXSTREAMS]     = {-1};               //!< pts of last intput packet
        //!<
        int64_t lastOutPTS[MAXSTREAMS]    = {-1};               //!< pts of last output packet
        //!<
        int64_t lastOutDTS[MAXSTREAMS]    = {-1};               //!< dts of last output packet
        //!<
        int64_t maxPTSofGOP               = -1;                 //!< max PTS of current output video GOP
        //!<
    } streamInfo;                                               //!< infos of stream PTS/DTS
    //!<

    enum {
        CUT_STATE_NULL         = 0,
        CUT_STATE_FIRSTPACKET  = 1,
        CUT_STATE_START        = 2,
        CUT_STATE_STOP         = 3,
    };

    /**
      * cut PTS/DTS infos
      */
    struct sCutInfo {
        int startPacketNumber       = -1;                    //!< packet number of start position
        //!<
        int64_t startDTS            =  0;                    //!< DTS timestamp of start position
        //!<
        int64_t startPTS            =  0;                    //!< PTS timestamp of start position
        //!<
        int stopPacketNumber        = -1;                    //!< packet number of stop position
        //!<
        int64_t stopDTS             =  0;                    //!< DTS timestamp of stop position
        //!<
        int64_t stopPTS             =  0;                    //!< PTS timestamp of stop position
        //!<
        int64_t offset              =  0;                    //!< current offset from input stream to output stream
        //!<
        int64_t offsetPTSReEncode   =  0;                    //!< additional PTS offset for re-encoded packets
        //!<
        int64_t offsetDTSReEncode   =  0;                    //!< additional DTS offset for re-encoded packets
        //!<
        int64_t offsetDTSReceive    =  0;                    //!< additional DTS offset for re-encoded packets with PTS < DTS (found with h264_nvenc)
        //!<
        int64_t videoPacketDuration =  0;                    //!< duration of video packet
        //!<
        int state                   = CUT_STATE_FIRSTPACKET; //!< state of smart cut
        //!<
    } cutInfo;                                               //!< infos of cut positions
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
    int64_t inputKeyPacketPTSbefore[MAXSTREAMS]  = {-1};
    int64_t outputKeyPacketPTSbefore[MAXSTREAMS] = {-1};
    int64_t lastPacketInPTS[MAXSTREAMS]          = {-1};      //!< PTS of last input packet
    int64_t lastPacketInDTS[MAXSTREAMS]          = {-1};      //!< DTS of last input packet
    int64_t lastPacketOutDTS[MAXSTREAMS]         = {-1};      //!< DTS of last output packet from encoder
    int64_t lastFrameInPTS[MAXSTREAMS]           = {-1};      //!< PTS of last input frame from decoder, send to encoder
    int64_t lastFrameInDTS[MAXSTREAMS]           = {-1};      //!< DTS of last input frame from decoder, send to encoder
    //!<
#endif
};
