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
#include "debug.h"


extern "C" {
#include <libavcodec/avcodec.h>
}

/**
 * element of the video index
*/
typedef struct sIndexElement {
    int fileNumber    = -1;             //!< number of TS file
    //!<
    int packetNumber  = -1;             //!< video packet number
    //!<
    int64_t pts       = -1;             //!< pts of i-frame
    //!<
    bool rollover     = false;          //!< true for packets after PTS/DTS rollover
    //!<
    bool isPTSinSlice = true;           //!< false if H.264 packet and any PTS from P/B frame after has before slice start, only stop cut if true
    //!<
} sIndexElement;

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
     * @param pts           packet PTS
     * @param beforePTS     PTS of key packet number before PTS
     * @param isPTSinSlice  false if H.264 packet and any PTS from P/B frame after has before slice start, only stop cut if true
     * @return key packet number before pts
     */
    int GetKeyPacketNumberBeforePTS(const int64_t pts, int64_t *beforePTS = nullptr, const bool isPTSinSlice = false);

    /**
     * get key packet number after PTS
     * @param pts frame PTS
     * @param afterPTS  PTS of found packet
     * @param isPTSinSlice  false if H.264 packet and any PTS from P/B frame after has before slice start, only stop cut if true
     * @return frame number after pts
     */
    int GetKeyPacketNumberAfterPTS(const int64_t pts,  int64_t *afterPTS = nullptr, const bool isPTSinSlice = false);

    /**
     * get last packet from key packet index
     * @return last packet of key packet index
     */
    sIndexElement *GetLastPacket();

    /**
     * get packet number after packet
     * @param packetNumber number of packet
     * @return if fullDecode packet number direct after packetNumber, else number of next key packet
     */
    int GetPacketNumberAfter(int packetNumber);

    /**
     * get frame number before frame number
     * @param frameNumber number of frame
     * @return if fullDecode frame number direct before frameNumber, else i-frame number before
     */
    int GetFrameBefore(int frameNumber);

    /**
     * get key packet before frameNumber
     * @param packetNumber number of packet
     * @param beforePTS    PTS of key packet number before
     * @return             number of key packet before packetNumber
     */
    int GetKeyPacketNumberBefore(int packetNumber, int64_t *beforePTS = nullptr);

    /**
     * get key packet after packetNumber
     * @param packetNumber packet number
     * @param afterPTS     PTS of key packet number after
     * @return             number of key packet frame after packetNumber
     */
    int GetKeyPacketNumberAfter(int packetNumber, int64_t *afterPTS = nullptr);

    /**
     * get offset time from recording start in ms
     * @param packetNumber number of the packet
     * @return offset time from recoring start in ms
     */
    int GetTimeOffsetFromKeyPacketAfter(const int packetNumber);

    /**
     * get frame number to offset of recording start
     * @param offset_ms packet number
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
     * add packet to packet number and PTS ring buffer
     * @param packetNumber number of the packet
     * @param pts          presentation timestamp of the packet
     */
    void AddPTS(const int packetNumber, const int64_t pts);

#ifdef DEBUG_DECODER
    /** video packet number from called PTS
     * @param pts  presentation timestamp
     * @return video packet number from given presentation timestamp
     */
    int GetPacketNumberFromPTS(const int64_t pts);
#endif

    /** video packet number before called PTS
     * @param pts       presentation timestamp
     * @param beforePTS PTS of video packet before PTS
     * @return          video packet number before given presentation timestamp
     */
    int GetPacketNumberBeforePTS(const int64_t pts, int64_t *beforePTS = nullptr);

    /** video packet number after given PTS of called PTS
     * @param pts      presentation timestamp
     * @param afterPTS PTS of video packet after PTS
     * @return         video packet number after given presentation timestamp
     */
    int GetPacketNumberAfterPTS(const int64_t pts, int64_t *afterPTS = nullptr);

    /** return PTS from packet if called with an key packet number, otherwise from packet before
     * @param    frameNumber frame number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSBeforeKeyPacketNumber(const int frameNumber);

    /** return PTS from packet if called with an key packet number, otherwise from packet after
     * @param    frameNumber frame number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSAfterKeyPacketNumber(const int frameNumber);

    /** return PTS from packet if called with an key packet number, otherwise from packet after
     * @param    frameNumber frame number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSFromKeyPacketNumber(const int frameNumber);

    /** return PTS from packet
     * @param    packetNumber packet number
     * @return   presentation timestamp of frame
     */
    int64_t GetPTSFromPacketNumber(const int packetNumber);

    /** return PTS from of key packet before
     * @param    pts presentation timestamp
     * @return   presentation timestamp of key packet before
     */
    int64_t GetKeyPacketPTSBeforePTS(int64_t pts);

    /** set start PTS of video stream
     * @param start_time_param  PTS start time of video stream
     * @param time_base_param   time base of video stream
     */
    void SetStartPTS(const int64_t start_time_param, const AVRational time_base_param);

    /** set start PTS of video stream
     *  @return  start PTS of video stream
     */
    int64_t GetStartPTS() const;

    /** add PTS from start of p-slice to index
     * @param pts  start of p-slice
     */
    void AddPSlice(const int64_t pts);

    /** get PTS from start of p-slice after given PTS
     * @param pts        given PTS
     * @param pSlicePTS  PTS of key packet found
     * @return           key packet number from start of p-slice
     */
    int GetPSliceKeyPacketNumberAfterPTS(const int64_t pts, int64_t *pSlicePTS);

private:
    /** get index element of given PTS
     * @param pts  given PTS
     * @return  pointer to index element
     */
    sIndexElement *GetIndexElementFromPTS(const int64_t pts);

    bool fullDecode       = false;               //!< decoder full decode modi
    //!<
    int64_t start_time    = 0;                   //!< PTS of video stream start
    //!<
    AVRational time_base  = {0};                 //!<  time base of video stream
    //!<
    bool rollover         = false;               //!< true after PTS/DTS rollover
    //!<
    std::vector<sIndexElement> indexVector;      //!< recording index
    //!<
    std::vector<int64_t> pSliceVector;           //!< p-slice index
    //!<

    /**
     * ring buffer element to store frame presentation timestamp
     */
    struct sPTS_RingbufferElement {
        int packetNumber = -1;                    //!< packet number
        //!<
        int64_t pts      = 0;                     //!< presentation timestamp of the frame
        //!<
        bool rollover    = false;                 //!< true for packets after PTS/DTS rollover
    };
    std::vector<sPTS_RingbufferElement> ptsRing; //!< ring buffer for PTS per frameA
    //!<

#define MAX_PTSRING 200                          // maximum Element in ptsRing Ring Buffer
};
#endif
