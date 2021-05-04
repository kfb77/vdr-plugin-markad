/*
 * encoder_new.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

extern "C" {
    #include "debug.h"
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


#define VOLUME 3dB


class cAC3VolumeFilter {
    public:
        cAC3VolumeFilter();
        ~cAC3VolumeFilter();
        bool Init(const uint64_t channel_layout, const enum AVSampleFormat sample_fmt, const int sample_rate);
        bool SendFrame(AVFrame *avFrame);
        bool GetFrame(AVFrame *avFrame);
    private:
        AVFilterGraph *filterGraph = NULL;
        AVFilterContext *filterSrc = NULL;
        AVFilterContext *filterSink = NULL;
};


class cEncoder {
    public:
        explicit cEncoder(MarkAdContext *macontext);
        ~cEncoder();
        void Reset(const int Pass);
        bool OpenFile(const char *directory, cDecoder *pt_cDecoder);
        bool WritePacket(AVPacket *pktIn, cDecoder *ptr_cDecoder);
        bool CloseFile(cDecoder *ptr_cDecoder);

    private:
        bool EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodexCtx, AVFrame *avFrame, AVPacket *avpkt);
        bool InitEncoderCodec(cDecoder *ptr_cDecoder, const char *directory, const unsigned int streamIndexIn, const unsigned int streamIndexOut);
        bool ChangeEncoderCodec(cDecoder *ptr_cDecoder, const int streamIndexIn, const int streamIndexOut, AVCodecContext *avCodecCtxIn);
        void SaveFrame(const int frame, AVFrame *avFrame);
        bool ReSampleAudio(AVFrame *avFrameIn, AVFrame *avFrameOut, const int streamIndex);
        bool CheckStats(const int max_b_frames);

        MarkAdContext *maContext;
        int threadCount = 0;
        AVFormatContext *avctxIn = NULL;
        AVFormatContext *avctxOut = NULL;
        AVCodecContext **codecCtxArrayIn = NULL;
        AVCodecContext **codecCtxArrayOut = NULL;
        SwrContext **swrArray = NULL;
        int64_t ptsBefore = 0;
        int64_t ptsBeforeCut = INT64_MAX;
        int64_t ptsAfterCut = 0;
        bool stateEAGAIN = false;
        cAC3VolumeFilter *ptr_cAC3VolumeFilter[MAXSTREAMS] = {NULL};
        struct cutStatusType {
            int64_t videoStartPTS = INT64_MAX;
            int frameBefore = -2;
            int64_t *ptsInBefore = NULL;
            int64_t ptsOutBefore = -1;
            int64_t *dtsInBefore = NULL;
            int64_t pts_dts_CutOffset = 0; // offset from the cut out frames
            int64_t *pts_dts_CyclicalOffset = NULL;  // offset from pts/dts cyclicle, multiple of 0x200000000
        } cutStatus;
        int *streamMap = NULL;
        int pass = 0;

/**
 * statistic data for 2 pass encoding
 */
        struct sAVstatsIn {
            char *data = NULL; //!< statistic data generated from encoder
	                       //!<

            long int size = 0; //!< size of statistic data
	                       //!<

        } stats_in;

#ifdef DEBUG_CUT
       int64_t inputPacketPTSbefore[MAXSTREAMS] = {0};
       int64_t beforeDecodePacketPTSbefore[MAXSTREAMS] = {0};
       int64_t beforeEncodeFramePTSbefore[MAXSTREAMS] = {0};
       int64_t afterEncodePacketPTSbefore[MAXSTREAMS] = {0};
       int packet_out = 0;
#endif
};
