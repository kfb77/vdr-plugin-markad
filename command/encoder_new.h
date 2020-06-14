/*
 * encoder_new.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
    #include <libavutil/file.h>
    #include <libavutil/opt.h>
    #include <libavfilter/avfilter.h>
    #include <libavfilter/buffersink.h>
    #include <libavfilter/buffersrc.h>
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
        cEncoder(int threadCount, const bool ac3reencode);
        ~cEncoder();
        bool OpenFile(const char * directory, cDecoder *pt_cDecoder);
        bool WritePacket(AVPacket *pkt, cDecoder *ptr_cDecoder);
        bool EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodexCtx, AVFrame *avFrame, AVPacket *avpktAC3);
        bool CloseFile();

    private:
        int threadCount = 0;
        AVFormatContext *avctxOut = NULL;
        AVCodecContext **codecCtxArrayOut = NULL;
        unsigned int nb_streamsIn = 0;
        int64_t pts_dts_CutOffset = 0;
        int64_t ptsBefore = 0;
        int64_t ptsBeforeCut = INT64_MAX;
        int64_t ptsAfterCut = 0;
        int64_t *pts_dts_CyclicalOffset = NULL;
        int64_t *dtsOut = NULL;
        int64_t *dtsBefore = NULL;
        bool stateEAGAIN = false;
        bool ac3ReEncode = false;
        cAC3VolumeFilter *ptr_cAC3VolumeFilter[MAXSTREAMS] = {NULL};

        bool InitEncoderCodec(cDecoder *ptr_cDecoder,AVFormatContext *avctxIn,AVFormatContext *avctxOut,const unsigned int streamIndex,AVCodecContext *avCodecCtxIn);
        bool ChangeEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, const unsigned int streamIndex, AVCodecContext *avCodecCtxIn);
};
