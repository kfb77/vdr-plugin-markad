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


extern "C" {
#include <libavcodec/avcodec.h>
}


/**
 * recording index class
 * store offset from start in ms of each i-frame
 */
class cIndex {
public:
    /**
     * recording index class
     * @param fullDecodeParam full decode state of decoder
     */
    explicit cIndex(const bool fullDecodeParam);
    ~cIndex();

    /**
     * add new frame to index
     * @param fileNumber         number of ts file
     * @param packetNumber       number of packet
     * @param pts                frame PTS
     */
    void Add(const int fileNumber, const int packetNumber, const int64_t pts);

    /**
     * get key packet number before PTS
     * @param pts frame PTS
     * @return frame number before pts
     */
    int GetKeyPacketNumberBeforePTS(const int64_t pts);

    /**
     * get key packet number after PTS
     * @param pts frame PTS
     * @return frame number after pts
     */
    int GetKeyPacketNumberAfterPTS(const int64_t pts);

    /**
     * get last frame number from index
     * @return last frame number of index
     */
    int GetLastFrame();

    /**
     * get frame number after frame
     * @param frameNumber number of frame
     * @return if fullDecode frame number direct after frameNumber, else i-frame number after
     */
    int GetFrameAfter(int frameNumber);

    /**
     * get frame number before frame number
     * @param frameNumber number of frame
     * @return if fullDecode frame number direct before frameNumber, else i-frame number before
     */
    int GetFrameBefore(int frameNumber);

    /**
     * get key packet before frameNumber
     * @param frameNumber number of packet
     * @return number of key packet before frameNumber
     */
    int GetKeyPacketNumberBefore(int frameNumber);

    /**
     * get key packet after packetNumber
     * @param packetNumber packet number
     * @return number of key packet frame after packetNumber
     */
    int GetKeyPacketNumberAfter(int packetNumber);

    /**
     * get offset time from recording start in ms
     * @param packetNumber number of the packet
     * @return offset time from recoring start in ms
     */
    int GetTimeFromFrame(const int packetNumber);

    /**
     * get frame number to offset of recording start
     * @param packetNumber packet number
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

    /** video frame number before called PTS
     * @param pts  presentation timestamp
     * @return video frame number before given presentation timestamp
     */
    int GetPacketNumberBeforePTS(const int64_t pts);

    /** video frame number after given PTS of called PTS
     * @param pts  presentation timestamp
     * @return video frame number after given presentation timestamp
     */
    int GetPacketNumberAfterPTS(const int64_t pts);

    /** return PTS from packet if called with an key packet number, otherwise from packet after
     * @param    frameNumber frame number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSFromKeyPacket(const int frameNumber);

    /** return PTS from packet
     * @param    packetNumber packet number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSFromPacketNumber(const int packetNumber);

    /** set start PTS of video stream
     * @param start_time_param  PTS start time of video stream
     * @param time_base_param   time base of video stream
     */
    void SetStartPTS(const int64_t start_time_param, const AVRational time_base_param);

    /** set start PTS of video stream
     *  @return  start PTS of video stream
     */
    int64_t GetStartPTS() const;

private:
    bool fullDecode       = false;               //!< decoder full decode modi
    //!<
    int64_t start_time    = 0;                   //!< PTS of video stream start
    //!<
    AVRational time_base  = {0};                 //!<  time base of video stream
    //!<

    /**
     * element of the video index
     */
    struct sIndexElement {
        int fileNumber         = -1;             //!< number of TS file
        //!<
        int packetNumber       = -1;             //!< video packet number
        //!<
        int64_t pts            = -1;             //!< pts of i-frame
        //!<
    };
    std::vector<sIndexElement> indexVector;      //!< recording index

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

#define MAX_PTSRING 200                          // maximum Element in ptsRing Ring Buffer
};
#endif
