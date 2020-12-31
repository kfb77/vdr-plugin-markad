/*
 * index.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __index_h_
#define __index_h_

#include <stdlib.h>
#include <inttypes.h>
#include <vector>

#include "global.h"


class cIndex {
    public:
        cIndex();
        ~cIndex();
        int64_t GetLastTime();
        void Add(int fileNumber, int frameNumber, int64_t pts_time_ms);
        int GetIFrameBefore(int frame);
        int GetIFrameAfter(int frame);
        int64_t GetTimeFromFrame(int frame);
        int GetFrameFromOffset(int offset_ms);
        int GetIFrameRangeCount(int beginFrame, int endFrame);

    private:
        struct index {
            int fileNumber = 0;
            int frameNumber = 0;
            int64_t pts_time_ms = 0;
        };
        std::vector<index> indexVector;
        int diff_ms_maxValid = 0;

        int GetLastFrameNumber();
};
#endif
