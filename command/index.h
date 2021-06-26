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

/**
 * recording index class
 * store offset from start in ms of each i-frame
 */
class cIndex {
    public:
        cIndex();
        ~cIndex();

/**
 * get last stored offset in ms
 * @return offset in ms of last stored i-frame
 */
        int GetLastTime();

/**
 * add new frame to index
 * @param fileNumber  number of ts file
 * @param frameNumber number of frame
 * @param timeOffset_ms offset in ms from recording start
 */
        void Add(int fileNumber, int frameNumber, int timeOffset_ms);

/**
 * get i-frame before frameNumber
 * @param frameNumber number of frame
 * @return number of i-frame before frameNumber
 */
        int GetIFrameBefore(int frameNumber);

/**
 * get i-frame after frameNumber
 * @param frameNumber number of frame
 * @return number of i-frame after frameNumber
 */
        int GetIFrameAfter(int frameNumber);

/**
 * get offset time from recoring start in ms
 * @param frameNumber number of the frame
 * @return offset time from recoring start in ms
 */
        int GetTimeFromFrame(int frameNumber);

/**
 * get frame number to offset of recording start
 * @param offset_ms offset from recording start in ms
 * @return frame number to offset of recording start
 */
        int GetFrameFromOffset(int offset_ms);

/**
 * get number of i-frames between beginFrame and endFrame
 * @param beginFrame frame number start of the range
 * @param endFrame   frame number end of the range
 * return number of i-frames between beginFrame and endFrame
 */
        int GetIFrameRangeCount(int beginFrame, int endFrame);

/**
 * add frame to frame number and PTS buffer
 * @param frameNumber number of the frame
 * @param pts         presentation timestamp of the frame
 */
        void AddPTS(const int frameNumber, const int64_t pts);

/** get first video frame number after given presentation timestamp of AC3 frame PTS
 * @param pts  presentation timestamp
 * @return first frame number after given presentation timestamp
 */
        int GetFirstVideoFrameAfterPTS(const int64_t pts);

    private:
/**
 * get last stored frame number of the recording index
 * @return last stored frame number of the recording index
 */
        int GetLastFrameNumber();

/**
 * element of the video index
 */
        struct sIndexElement {
            int fileNumber = 0;                     //!< number of TS file
                                                    //!<

            int frameNumber = 0;                    //!< video frame number
                                                    //!<

            int timeOffset_ms = 0;                  //!< time offset from start of the recording in ms
                                                    //!<
        };
        std::vector<sIndexElement> indexVector;     //!< recording index
                                                    //!<
/**
 * ring buffer element to store frame presentation timestamp
 */
        struct sPTS_RingbufferElement {
            int frameNumber = -1;                    //!< frame number
                                                     //!<
            int64_t pts = 0;                         //!<  presentation timestamp of the frame
                                                     //!<
        };
        std::vector<sPTS_RingbufferElement> ptsRing; //!< ring buffer for PTS per frameA
                                                     //!<
        int diff_ms_maxValid = 0;                    //!< maximal valid time difference between two i-frames
                                                     //!<

};
#endif
