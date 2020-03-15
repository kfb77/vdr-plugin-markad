extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
}

class cEncoder {
    public:
        cEncoder(bool ac3reencode);
        ~cEncoder();
        bool OpenFile(const char * directory, cDecoder *pt_cDecoder);
        bool WritePacket(AVPacket *pkt, cDecoder *ptr_cDecoder);
        bool EncodeFrame(cDecoder *ptr_cDecoder, AVCodecContext *avCodexCtx, AVFrame *avFrameOut, AVPacket *avpktAC3);
        bool CloseFile();

    private:
        bool InitEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, AVFormatContext *avctxOut, int streamIndex, AVCodecContext *avCodecCtxIn);
        bool ChangeEncoderCodec(cDecoder *ptr_cDecoder, AVFormatContext *avctxIn, AVFormatContext *avctxOut, int streamIndex, AVCodecContext *avCodecCtxIn);
        AVFormatContext *avctxOut = NULL;
        AVCodecContext **codecCtxArrayOut = NULL;
        int64_t pts_dts_offset = 0;
        int64_t *dts = NULL;
        int64_t *dtsBefore = NULL;
        bool stateEAGAIN=false;
        bool ac3ReEncode;
};
