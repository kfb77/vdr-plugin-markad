/*
 * test.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

/**
* performance test class
*/
class cTest {
private:
    int testFrames            = 30000;   //!< count test frames to decode
    //!<

public:

    /**
     * constructor for performance test class
     * @param maContextParam markad context
     */
    explicit cTest(const char *recDirParam, char *hwaccelParam) {
        recDir  = recDirParam;
        hwaccel = hwaccelParam;
    }

    ~cTest() {
    }


    /**
     * all decoder performance test
     */
    void Perf() {
        char no_hwaccel[16]       = {0};
        int resultDecoder[2][5]      = {};
        int resultDecoderHW[2][5] = {};
        dsyslog("run decoder performance test");

        for (int pass = 0; pass <= 1; pass++) {
            dsyslog("pass %d *************************************************************************************************************", pass);
            for (int threads = 1; threads <=4; threads++) {
                if (threads == 3) continue;
                dsyslog("pass %d, threads %d: decoder software *******************************************************************************", pass, threads);
                resultDecoder[pass][threads]      = PerfDecoder(threads, no_hwaccel);
                dsyslog("pass %d, threads %d: decoder  hwaccel:%-10s *****************************************************************", pass, threads, hwaccel);
                resultDecoderHW[pass][threads] = PerfDecoder(threads, hwaccel);
            }
        }
        for (int pass = 0; pass <= 1; pass++) {
            dsyslog("pass %d ***********************************************************************", pass);
            for (int threads = 1; threads <=4; threads++) {
                if (threads == 3) continue;
                dsyslog("threads %d ********************************************************************", threads);
                dsyslog("decoder, threads %d:                    %5dms", threads, resultDecoder[pass][threads]);
                dsyslog("decoder, threads %d, hwaccel:%-10s %5dms", threads, hwaccel, resultDecoderHW[pass][threads]);
            }
        }
        dsyslog("*****************************************************************************");
    }


    /**
    * decoder performance test
    * @param threads count of FFmpeg threads
    * @param hwaccel true if hwaccel is used, false otherwise
    */
    int PerfDecoder(const int threads, char *hwaccel) const {
        // decode frames
        struct timeval startDecode = {};
        gettimeofday(&startDecode, nullptr);

        // init decoder
        cDecoder *decoder = new cDecoder(recDir, threads, false, hwaccel, nullptr);  // recodring directory, threads, full decode, hwaccel mathone, index flag
        while (decoder->DecodeNextFrame(false)) {  // no audio decode
            if (abortNow) return -1;
            //        dsyslog("xxxx framenumber %d", decoder->GetVideoFrameNumber());
            if (decoder->GetVideoFrameNumber() >= testFrames) break;
        }

        delete decoder;

        struct timeval endDecode = {};
        gettimeofday(&endDecode, nullptr);
        time_t sec = endDecode.tv_sec - startDecode.tv_sec;
        suseconds_t usec = endDecode.tv_usec - startDecode.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }
        long int time_us = sec * 1000000 + usec;
        return time_us / 1000;
    }

private:
    const char * recDir;
    char * hwaccel;
};

