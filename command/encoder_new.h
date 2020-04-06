extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
}

class cEncoder {
    public:
        cEncoder();
        ~cEncoder();
        bool OpenFile(const char * directory, AVFormatContext *avctxIn);
        bool WritePacket(AVPacket *pkt, cDecoder *ptr_cDecoder);
        bool CloseFile();

    private:
        AVFormatContext *avctxOut = NULL;
        int64_t pts_dts_offset = 0;
        int64_t *dts = NULL;
        int64_t *dtsBefore = NULL;
};
