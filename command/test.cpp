/*
 * test.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

class cTest {
private:
    sMarkAdContext *maContext = {};
    int testFrames = 30000;
public:
    cTest(sMarkAdContext *maContextParam) {
        maContext = maContextParam;

    }
    ~cTest() {
    }


    void Perf() {
        int oldDecoder[2][5]      = {};
        int newDecoder[2][5]      = {};
        int newDecoderVAAPI[2][5] = {};
        dsyslog("run decoder performance test");

        for (int pass = 0; pass <= 1; pass++) {
            dsyslog("pass %d *************************************************************************************************************************************", pass);
            for (int threads = 1; threads <=4; threads++) {
                if (threads == 3) continue;
                dsyslog("pass %d, threads %d: old decoder  ************************************************************************************************************", pass, threads);
                oldDecoder[pass][threads]      = PerfDecoder(threads);
                dsyslog("pass %d, threads %d: new decoder  ************************************************************************************************************", pass, threads);
                newDecoder[pass][threads]      = PerfDecoderNEW(threads, false);
                dsyslog("pass %d, threads %d: new decoder  VAAPI ******************************************************************************************************", pass, threads);
                newDecoderVAAPI[pass][threads] = PerfDecoderNEW(threads, true);
            }
        }
        for (int pass = 0; pass <= 1; pass++) {
            dsyslog("pass %d ***********************************************************************", pass);
            for (int threads = 1; threads <=4; threads++) {
                if (threads == 3) continue;
                dsyslog("threads %d ********************************************************************", threads);
                dsyslog("old decoder, threads %d:        %5dms", threads, oldDecoder[pass][threads]);
                dsyslog("new decoder, threads %d:        %5dms", threads, newDecoder[pass][threads]);
                dsyslog("new decoder, threads %d, vaapi: %5dms", threads, newDecoderVAAPI[pass][threads]);
            }
        }
        dsyslog("*****************************************************************************");
    }

    int PerfDecoder(const int threads) {
        // decode frames with old decoder
        struct timeval startDecode = {};
        gettimeofday(&startDecode, nullptr);
        cIndex *index = new cIndex();
        cDecoder *ptr_cDecoder = new cDecoder(threads, index);

        while(ptr_cDecoder->DecodeDir(maContext->Config->recDir)) {
            while(ptr_cDecoder->GetNextPacket(true, false)) { // fill frame index, but not fill PTS ring buffer, it will get out of sequence
                if (ptr_cDecoder->IsVideoPacket()) {
                    if (ptr_cDecoder->GetFrameInfo(maContext, true, false, false, false)) {
                        if (abortNow) return -1;
                        if (ptr_cDecoder->GetFrameNumber() >= testFrames) break;
                    }
                }
            }
        }
        delete ptr_cDecoder;
        delete index;

        struct timeval endDecode = {};
        gettimeofday(&endDecode, nullptr);
        time_t sec = endDecode.tv_sec - startDecode.tv_sec;
        suseconds_t usec = endDecode.tv_usec - startDecode.tv_usec;
        if (usec < 0) {
            usec += 1000000;
            sec--;
        }

        long int decodeTime_us = sec * 1000000 + usec;
        return decodeTime_us / 1000;

    }

    int PerfDecoderNEW(const int threads, const bool vaapi) {
        // decode frames with new decoder and new call
        struct timeval startDecode = {};
        gettimeofday(&startDecode, nullptr);

        // init new decoder
        cDecoderNEW *decoder = new cDecoderNEW(maContext->Config->recDir, threads, false, vaapi, nullptr);  // threads, index flag, vaapi flag
        while (decoder->DecodeNextFrame()) {
            if (abortNow) return -1;
            //        dsyslog("xxxx framenumber %d", decoder->GetFrameNumber());
            if (decoder->GetFrameNumber() >= testFrames) break;
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
        long int decodeTime_us = sec * 1000000 + usec;
        return decodeTime_us / 1000;
    }

    /*
        void DecoderPicture() {
            maContext->Video.Info.height = 576;
            maContext->Video.Info.width  = 720;
            cIndex *index = new cIndex();
            cDecoder *ptr_cDecoder = new cDecoder(maContext->Config->threads, index);
            cDecoderNEW *decoder = new cDecoderNEW(maContext->Config->recDir, 2, false, true, nullptr);  // threads, index flag, vaapi flag

            bool all = true;
            while(ptr_cDecoder->DecodeDir(maContext->Config->recDir)) {
                if (abortNow) return;
                while(ptr_cDecoder->GetNextPacket(true, false)) { // fill frame index, but not fill PTS ring buffer, it will get out of sequence
                    if (abortNow) return;
                    if (ptr_cDecoder->IsVideoIFrame()) {
                        if (ptr_cDecoder->GetFrameInfo(maContext, true, false, false, false)) {

                            char *fileName = nullptr;
                            if (asprintf(&fileName,"%s/F__%07d_1old.pgm", maContext->Config->recDir, ptr_cDecoder->GetFrameNumber()) >= 1) {
                                ALLOC(strlen(fileName)+1, "fileName");
                                SaveFrameBuffer(maContext, fileName);
                                FREE(strlen(fileName)+1, "fileName");
                                free(fileName);
                            }

                            decoder->ReadNextPicture();
                            sVideoPicture *videoPicture = decoder->GetVideoPicture();

                            dsyslog("frame old (%d), frame new (%d)", ptr_cDecoder->GetFrameNumber(), decoder->GetFrameNumber());
                            for (int plane = 0; plane < PLANES; plane++) {
                                for (int line = 0; line <= 100; line++) {
                                    for (int column = 0; column <= 100; column++) {
                                        int oldPixel = maContext->Video.Data.Plane[plane][line * maContext->Video.Data.PlaneLinesize[plane] + column];
                                        int newPixel = videoPicture->Plane[plane][line * videoPicture->PlaneLinesize[plane] + column];
                                        if (oldPixel != newPixel) {
                                            dsyslog("frame old (%d), frame new (%d): plane %d, line %d, column %d: pixel %d <-> %d", ptr_cDecoder->GetFrameNumber(), decoder->GetFrameNumber(), plane, line, column, oldPixel, newPixel);
                                            all = false;
                                        }
                                    }
                                }

                            }

                            maContext->Video.Data.valid = true;
                            for (int i = 0; i < PLANES; i++) {
                                maContext->Video.Data.Plane[i] = videoPicture->Plane[i];
                                maContext->Video.Data.PlaneLinesize[i] = videoPicture->PlaneLinesize[i];
                            }

                            fileName = nullptr;
                            if (asprintf(&fileName,"%s/F__%07d_2new.pgm", maContext->Config->recDir, ptr_cDecoder->GetFrameNumber()) >= 1) {
                                ALLOC(strlen(fileName)+1, "fileName");
                                SaveFrameBuffer(maContext, fileName);
                                FREE(strlen(fileName)+1, "fileName");
                                free(fileName);
                            }


                            if (ptr_cDecoder->GetFrameNumber() >= 30) break;
                        }
                    }
                }
            }

            delete decoder;
            delete ptr_cDecoder;
            delete index;

            dsyslog("xxx all %d", all);
            return;
        }
    */
};

