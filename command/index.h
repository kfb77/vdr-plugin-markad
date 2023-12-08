/*
 * index.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __index_h_
#define __index_h_

#include <stdlib.h>
#include <algorithm>
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
     * add new frame to index
     * @param fileNumber         number of ts file
     * @param frameNumber        number of frame
     * @param ptsTimeOffset_ms   offset in ms from recording start based on PTS fild
     * @param frameTimeOffset_ms offset in ms from recording start based sum of duration
     */
    void Add(const int fileNumber, const int frameNumber, const int ptsTimeOffset_ms, const int frameTimeOffset_ms);

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
     * @param isVDR true if timestamp should calculated based on vdr rules (frame duration offset), false if timestamp should claculated based on vlc ruled (frame pts)
     * @return offset time from recoring start in ms
     */
    int GetTimeFromFrame(const int frameNumber, const bool isVDR);

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
     * @param before true will return frame number before PTS, false will return frame number after PTS
     * @return first frame number after given presentation timestamp
     */
    int GetVideoFrameToPTS(const int64_t pts, const bool before = false);

private:
    /**
     * element of the video index
     */
    struct sIndexElement {
        int fileNumber         = -1;            //!< number of TS file
        //!<
        int frameNumber        = -1;            //!< video frame number
        //!<
        int ptsTimeOffset_ms   = -1;            //!< time offset from start of the recording in ms based on pts in frame, missing frame increase timestamp (imestamps for VLC player)
        //!<
        int frameTimeOffset_ms = -1;            //!< time offset from start of the recording in ms based in frame duration, missing frames are ignored (timestamps for VDR)
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

#define MAX_PTSRING 100                              // maximum Element in ptsRing Ring Buffer
};
#endif
