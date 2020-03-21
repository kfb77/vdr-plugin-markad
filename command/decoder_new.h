#include <vector>
#include "global.h"

extern "C"{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>
}

#define CDECODERVERSION 103
#define MAXEAGAINERRORS 10

class cDecoder
{
    public:
        cDecoder();
        ~cDecoder();
        bool DecodeDir(const char * recDir);
        void Reset();
        bool DecodeFile(const char * filename);
        int GetVideoHeight();
        int GetVideoWidth();
        int GetVideoFramesPerSecond();
        int GetVideoRealFrameRate();
        bool GetNextFrame();
        bool SeekToFrame(long int iFrame);
        bool GetFrameInfo(MarkAdContext *maContext);
        bool isVideoStream();
        bool isVideoIFrame();
        bool isAudioStream();
        bool isAudioAC3Stream();
        long int GetFrameNumber();
        long int GetIFrameCount();
        bool isInterlacedVideo();
        long int GetIFrameRangeCount(long int beginFrame, long int endFrame);
        long int GetIFrameBefore(long int iFrame);
        long int GetTimeFromIFrame(long int iFrame);

    private:
        char *recordingDir=NULL;
        int fileNumber=0;
        AVFormatContext *avctx = NULL;
        AVPacket avpkt;
        AVCodec *codec;
        AVCodecContext *codecCtx;
        AVFrame *avFrame = NULL;
        long int framenumber=-1;
        long int iFrameCount=0;
        int64_t pts_time_ms_LastFile=0;
        struct iFrameInfo
        {
            int fileNumber=0;
            long int iFrameNumber=0;
            int64_t pts_time_ms=0;
        };
        std::vector<iFrameInfo> iFrameInfoVector;
        struct structFrameData {
            bool Valid=false; // flag, if true data is valid
            uchar *Plane[4];  // picture planes (YUV420)
            int PlaneLinesize[4]; // size int bytes of each picture plane line
        } iFrameData;
        bool msgDecodeFile=true;
        bool msgGetFrameInfo=true;
        int interlaced_frame=-1;
        bool stateEAGAIN=false;
};
