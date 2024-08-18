/*
 * test.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <sys/time.h>
#include <chrono>

#include "test.h"
#include "debug.h"
#include "decoder.h"

// global variables
extern bool abortNow;


cTest::cTest(const char *recDirParam, const bool fullDecodeParam, char *hwaccelParam) {
    recDir     = recDirParam;
    fullDecode = fullDecodeParam;
    hwaccel    = hwaccelParam;
}


cTest::~cTest() {
}


/**
 * all decoder performance test
 */
void cTest::Perf() const {
    dsyslog("run decoder performance test");
    char no_hwaccel[16]           = {0};
    sPerfResult result[2 * 3 * 2] = {};   // 2 pass, 3 different thread counts, with and without hwaccel
    int index                     = 0;

    for (int pass = 0; pass <= 1; pass++) {
        dsyslog("pass %d *************************************************************************************************************", pass);
        for (int threads = 1; threads <=4; threads++) {
            if (threads == 3) continue;
            // software decoder
            dsyslog("pass %d, threads %d: decoder software *******************************************************************************", pass, threads);
            result[index].pass    = pass;
            result[index].threads = threads;
            result[index].hwaccel = no_hwaccel;
            PerfDecoder(&result[index]);
            index++;
            // hwaccel
            result[index].pass    = pass;
            result[index].threads = threads;
            result[index].hwaccel = hwaccel;
            dsyslog("pass %d, threads %d: decoder hwaccel: %-10s *****************************************************************", pass, threads, hwaccel);
            PerfDecoder(&result[index]);
            index++;
        }
    }
    dsyslog("*********************************************************************************************************************************************************");
    for (int i = 0; i < index; i++) {
        if (result[i].pass == 0) continue;  // ignore results from first pass, disk cache can have influence
        dsyslog("threads %d, hwaccel %-8s: decode %4.1fms, transfer %4.1fms, read %4.1fms -> sum: %4.1fms, test time %6.0fms", result[i].threads, (result[i].hwaccel[0] == 0) ? "none" : result[i].hwaccel, result[i].decode, result[i].transfer, result[i].read, result[i].decode + result[i].transfer + result[i].read, result[i].test);
    }
    dsyslog("*********************************************************************************************************************************************************");
}


// prevent optimizer to remove useless performance test code
#pragma GCC push_options
#pragma GCC optimize ("-O0")
void cTest::PerfDecoder(sPerfResult *result) const {
    const int testFrames   = 300;              // count test frames to decode
    double sumDecode       = 0;
    double sumTransfer     = 0;
    double sumRead         = 0;
    int countFrames        = 0;
    int countPictures      = 0;
    bool nextFrame         = true;
    sVideoPicture *picture = nullptr;

    // init decoder
    cDecoder *decoder = new cDecoder(recDir, result->threads, fullDecode, result->hwaccel, true, false, nullptr);  // recording directory, threads, full decode, hwaccel methode, force hwaccel, interlaced, index

    auto startTest = std::chrono::high_resolution_clock::now();
    while (nextFrame) {  // no audio decode
        if (abortNow) return;
        countFrames++;
        // read from file and decode
        auto startDecode = std::chrono::high_resolution_clock::now();
        nextFrame = decoder->DecodeNextFrame(false);  // no audio decode
        auto stopDecode = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> durationDecode = stopDecode - startDecode;
        sumDecode += durationDecode.count();

        // transfer picture from GPU to CPU and convert pixel format
        auto startTransfer = std::chrono::high_resolution_clock::now();
        picture = decoder->GetVideoPicture();
        auto stopTransfer = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> durationTransfer = stopTransfer - startTransfer;
        sumTransfer += durationTransfer.count();

        // read picture from momory
        if (picture) {
            countPictures++;
            // read picture
            auto startRead = std::chrono::high_resolution_clock::now();
            for (int line = 0; line < picture->height; line++) {
                for (int column = 0; column < picture->width; column++) {
                    uchar __attribute__((__unused__)) pixel = picture->plane[0][line * picture->width + column];
                }
            }
            auto stopRead = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> durationRead = stopRead - startRead;
            sumRead += durationRead.count();
        }

        if (countFrames >= testFrames) break;
    }
    // set result
    auto stopTest = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> durationTest = stopTest - startTest;
    result->test     = durationTest.count();
    result->decode   = sumDecode   / countFrames;
    result->transfer = sumTransfer / countFrames;
    result->read     = sumRead     / countPictures;

    delete decoder;

    return;
}
#pragma GCC pop_options
