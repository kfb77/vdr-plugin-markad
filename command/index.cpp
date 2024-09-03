/*
 * index.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "index.h"
#include "debug.h"


cIndex::cIndex(const bool fullDecodeParam) {
    fullDecode = fullDecodeParam;
    ptsRing.reserve(MAX_PTSRING + 2);  // pre alloc memory of static length ptsRing
    indexVector.clear();
    indexVector.reserve(1000);       // pre alloc memory for 1000 index elements
}


cIndex::~cIndex() {
#ifdef DEBUG_MEM
    int size = indexVector.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sIndexElement), "indexVector");
    }
    size = ptsRing.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(sPTS_RingbufferElement), "ptsRing");
    }
#endif
    indexVector.clear();
    ptsRing.clear();
}


void cIndex::SetStartPTS(const int64_t start_time_param, const AVRational time_base_param) {
    dsyslog("cIndex::SetStartPTS(): PTS %" PRId64 ": start of video stream, time base %d/%d", start_time_param, time_base_param.num, time_base_param.den);
    start_time = start_time_param;
    time_base  = time_base_param;
}


int64_t cIndex::GetStartPTS() const {
    return start_time;
}


// add a new entry to the list of frame timestamps
void cIndex::Add(const int fileNumber, const int packetNumber, const int64_t pts) {
    if (indexVector.empty() || (packetNumber > indexVector.back().packetNumber)) {
        // add new frame timestamp to vector
        sIndexElement newIndex;
        newIndex.fileNumber         = fileNumber;
        newIndex.packetNumber       = packetNumber;
        newIndex.pts                = pts;

        if (indexVector.size() == indexVector.capacity()) {
            indexVector.reserve(1000);
        }
        indexVector.push_back(newIndex);
        ALLOC(sizeof(sIndexElement), "indexVector");

#ifdef DEBUG_INDEX
        dsyslog("cIndex::Add(): fileNumber %d, packetNumber (%5d), PTS %6ld: time offset PTS %6dms, VDR %6dms", fileNumber, packetNumber, pts, GetTimeFromFrame(packetNumber, false),  GetTimeFromFrame(packetNumber, true));
#endif

    }
}


// get key packet number before given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetKeyPacketNumberBeforePTS(const int64_t pts) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberBeforePTS(): frame index not initialized");
        return -1;
    }
    int iFrameBefore  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->pts <= pts) iFrameBefore = frameIterator->packetNumber;
        else break;
    }
    if (iFrameBefore < 0) dsyslog("cIndex::GetKeyPacketNumberBeforePTS(): PTS %" PRId64 ": not in index, index content: first PTS %" PRId64 " , last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
    dsyslog("cIndex::GetKeyPacketNumberBeforePTS(): PTS %" PRId64 ": frame (%d) found", pts, iFrameBefore);
    return iFrameBefore; // frame not (yet) in index
}


// get key packet number after given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetKeyPacketNumberAfterPTS(const int64_t pts) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberAfterPTS(): frame index not initialized");
        return -1;
    }

    /*
        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->pts >= pts) {
                iFrameAfter = frameIterator->frameNumber;
                break;
            }
        }
    */

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [pts](sIndexElement const &value) ->bool { if (value.pts >= pts) return true; else return false; });
    if (found == indexVector.end()) {
        dsyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": not in index, index content: first PTS %" PRId64 ", last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
        return -1;
    }
    dsyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": frame (%d) found", pts, found->packetNumber);
    return found->packetNumber;
}


int cIndex::GetFrameBefore(int frameNumber) {
    if (frameNumber == 0) return 0;
    if (fullDecode) return frameNumber - 1;
    else {
        int iFrameBefore = GetKeyPacketNumberBefore(frameNumber - 1);  // if frameNumber is i-frame, GetKeyPacketBefore() will return same frameNumber
        if (iFrameBefore < 0) {
            esyslog("cIndex::GetFrameBefore(): frame (%d): GetKeyPacketBefore() failed", frameNumber);
            iFrameBefore = frameNumber - 1;  // fallback
        }
        return iFrameBefore;
    }
}


int cIndex::GetFrameAfter(int frameNumber) {
    if (fullDecode) return (frameNumber + 1);
    else {
        int iFrameAfter = GetKeyPacketNumberAfter(frameNumber);
        if (iFrameAfter < 0) {
            esyslog("cIndex::GetFrameAfter(): frame (%d): GetKeyPacketAfter() failed", frameNumber);
            return - 1;
        }
        return iFrameAfter;
    }
}


int cIndex::GetLastFrame() {
    if (indexVector.empty()) {
        esyslog("cIndex::GetLast(): frame index not initialized");
        return -1;
    }
    return indexVector.back().packetNumber;
}


// get key packet before given packet
// if packet is a key packet, key packet before will be returned
// return: key packet number, -1 if index is not initialized
//
int cIndex::GetKeyPacketNumberBefore(int frameNumber) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberBefore(): frame index not initialized");
        return -1;
    }
    int before_iFrame = -1;
    int iFrameBefore  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->packetNumber > frameNumber) {
            iFrameBefore = before_iFrame;
            break;
        }
        else before_iFrame = frameIterator->packetNumber;
    }
    if (iFrameBefore < 0) {
        dsyslog("cIndex::GetKeyPacketNumberBefore(): packet (%d): failed, index content: packet frame (%d), last packet (%d)", frameNumber, indexVector.front().packetNumber, indexVector.back().packetNumber);
        return -1;
    }
    return iFrameBefore; // frame not (yet) in index
}


// get key packet number after given packet number
// if packetNumber is a key packet, packet number self will be returned
// return: next key packet number
//
int cIndex::GetKeyPacketNumberAfter(int packetNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetKeyPacketNumberAfter(): packet index not initialized");
        return -1;
    }
    // request from current packet after last key packet, this can happen if markad runs during recording
    if ((packetNumber > indexVector.back().packetNumber) && (packetNumber < indexVector.back().packetNumber - 100)) {  // max alowed key packet distance
        dsyslog("cIndex::GetKeyPacketNumberAfter(): packet (%d) > last packet in index, return last index element (%d)", packetNumber, indexVector.back().packetNumber);
        return indexVector.back().packetNumber;
    }

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [packetNumber](const sIndexElement &value) ->bool { if (value.packetNumber >= packetNumber) return true; else return false; });
    if (found != indexVector.end()) return found->packetNumber;

    esyslog("cIndex::GetKeyPacketNumberAfter(): packet (%d): failed, index content: first packet (%d), last packet (%d)", packetNumber, indexVector.front().packetNumber, indexVector.back().packetNumber);
    return -1;
}


// return PTS from key packet if called with an key number, otherwise from packet after
int64_t cIndex::GetPTSFromKeyPacket(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().packetNumber) return indexVector.back().pts;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](sIndexElement const &value) ->bool { if (value.packetNumber >= frameNumber) return true; else return false; });
    if (found == indexVector.end()) {
        esyslog("cIndex::GetPTSFromKeyPacket(): frame (%d) not in index", frameNumber);
        dsyslog("cIndex::GetPTSFromKeyPacket(): index content: first packet (%d) , last packet (%d)", indexVector.front().packetNumber, indexVector.back().packetNumber);
        return -1;
    }
    return found->pts;

    /*

        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->frameNumber >= frameNumber) return frameIterator->pts;
        }
        return -1;  // this shout never reached
    */
}


int cIndex::GetTimeFromFrame(const int packetNumber) {
    if (indexVector.empty()) {  // expected if called to write start mark during running recording
        dsyslog("cIndex::GetTimeFromFrame(): packet (%d): index not initialized", packetNumber);
        return -1;
    }
    // use PTS based time from i-frame index
    int64_t framePTS = GetPTSFromKeyPacket(packetNumber);
    if (framePTS < 0) {
        esyslog("cIndex::GetTimeFromFrame(): packet (%d): get PTS failed", packetNumber);
        return -1;
    }
    framePTS -= start_time;
    if (framePTS < 0 ) {
        framePTS += 0x200000000;    // libavodec restart at 0 if pts greater than 0x200000000
    }
    int offsetTime_ms = round(1000 * framePTS * av_q2d(time_base));
    return offsetTime_ms;
}


int cIndex::GetFrameFromOffset(int offset_ms) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetFrameFromOffset: frame index not initialized");
        return -1;
    }
    // convert offset in ms to PTS
    int64_t pts = (offset_ms / av_q2d(time_base) / 1000) + start_time;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [pts](sIndexElement const &value) ->bool { if (value.pts >= pts) return true; else return false; });
    if (found == indexVector.end()) {
        esyslog("cIndex::GetFrameFromOffset(): offset_ms %dms not in index", offset_ms);
        dsyslog("cIndex::GetFrameFromOffset(): search for PTS %" PRId64 ", index content: first PTS %" PRId64 ", last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
        return -1;
    }

    dsyslog("cIndex::GetFrameFromOffset(): packet (%d) found", found->packetNumber);
    return found->packetNumber;

    /*
        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->pts >= pts) return frameIterator->frameNumber;
        }
        esyslog("cIndex::GetFrameFromOffset(): frame number to offset %dms not found", offset_ms);
        return -1;
    */
}


int cIndex::GetIFrameRangeCount(int beginFrame, int endFrame) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameRangeCount(): frame index not initialized");
        return -1;
    }
    int counter=0;

    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->packetNumber >= beginFrame) {
            counter++;
            if (frameIterator->packetNumber >= endFrame) return counter;
        }
    }
    dsyslog("cIndex::GetIFrameRangeCount(): failed beginFrame (%d) endFrame (%d) last frame in index list (%d)", beginFrame, endFrame, indexVector.back().packetNumber);
    return -1;
}


void cIndex::AddPTS(const int frameNumber, const int64_t pts) {
#ifdef DEBUG_RING_PTS_ADD
    dsyslog("cIndex::AddPTS(): frame (%6d) PTS %" PRId64, frameNumber, pts);
#endif
    // add new frame timestamp to vector
    sPTS_RingbufferElement newPTS;
    newPTS.frameNumber = frameNumber;
    newPTS.pts = pts;
    ptsRing.push_back(newPTS);
    ALLOC(sizeof(sPTS_RingbufferElement), "ptsRing");

    // delete oldest entry
    if (ptsRing.size() > MAX_PTSRING) {
        ptsRing.erase(ptsRing.begin());
        FREE(sizeof(sPTS_RingbufferElement), "ptsRing");
    }

}


int64_t cIndex::GetPTSFromPacketNumber(const int packetNumber) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPTSFromPacketNumber(): index is empty");
        return -1;
    }
    std::vector<sPTS_RingbufferElement>::iterator found = std::find_if(ptsRing.begin(), ptsRing.end(), [packetNumber](sPTS_RingbufferElement const &value) ->bool { if (value.frameNumber == packetNumber) return true; else return false; });
    if (found != ptsRing.end()) {
        return found->pts;
    }
    /*
        for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
            if (ptsIterator->frameNumber == packetNumber) {
                return ptsIterator->pts;
            }
        }
    */
    esyslog("cIndex::GetPTSFromPacketNumber(): packet %d: not found, index contains from (%d) to (%d)", packetNumber, ptsRing.front().frameNumber, ptsRing.back().frameNumber);
    return -1;
}


/* unused
int cIndex::GetPacketNumberFromPTS(const int64_t pts) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPacketNumberFromPTS(): index is empty");
        return -1;
    }
    struct sFrameType {
        int frameNumber = -1;
        int64_t pts     =  0;
    } frame;
    int64_t ptsMin =  INT64_MAX;
    int64_t ptsMax =  0;
#ifdef DEBUG_RING_PTS_LOOKUP
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
#endif

    for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
#ifdef DEBUG_RING_PTS_LOOKUP
//        dsyslog("cIndex::GetPacketNumberFromPTS(): frame (%6d) PTS %" PRId64, ptsIterator->frameNumber, ptsIterator->pts);
#endif
        ptsMin = std::min(ptsMin, ptsIterator->pts);
        ptsMax = std::max(ptsMax, ptsIterator->pts);
        if (ptsIterator->pts == pts) {
            frame.frameNumber = ptsIterator->frameNumber;
            break;
        }
    }
#ifdef DEBUG_RING_PTS_LOOKUP
    dsyslog("cIndex::GetPacketNumberFromPTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetPacketNumberFromPTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetPacketNumberFromPTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetPacketNumberFromPTS(): index size %lu", ptsRing.size());
    dsyslog("cIndex::GetPacketNumberFromPTS(): found video frame (%d) PTS %" PRId64 " from PTS %" PRId64, frame.frameNumber, frame.pts, pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
    // missing audio PTS in index can happen if audio PTS is after video PTS
#endif
    if (frame.frameNumber < 0) esyslog("cIndex::GetPacketNumberFromPTS(): pts %ld: not found, index contains from %ld to %ld", pts, ptsMin, ptsMax);
    return frame.frameNumber;
}
*/


int cIndex::GetPacketNumberBeforePTS(const int64_t pts) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPacketNumberBeforePTS(): index is empty");
        return -1;
    }
    struct sFrameType {
        int frameNumber = -1;
        int64_t pts     =  0;
    } frame;
#ifdef DEBUG_RING_PTS_LOOKUP
    int64_t ptsMin =  INT64_MAX;
    int64_t ptsMax =  0;
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
#endif

    for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
#ifdef DEBUG_RING_PTS_LOOKUP
//        dsyslog("cIndex::GetFrameBeforePTS(): frame (%6d) PTS %" PRId64, ptsIterator->frameNumber, ptsIterator->pts);
        ptsMin = std::min(ptsMin, ptsIterator->pts);
        ptsMax = std::max(ptsMax, ptsIterator->pts);
#endif
        if ((ptsIterator->pts <= pts) && (ptsIterator->pts >= frame.pts)) {  // reset state if we found a frame with pts smaller than target, they can be out of sequence
            frame.frameNumber = ptsIterator->frameNumber;
            frame.pts         = ptsIterator->pts;
        }
    }
#ifdef DEBUG_RING_PTS_LOOKUP
    dsyslog("cIndex::GetPacketNumberBeforePTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetPacketNumberBeforePTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetPacketNumberBeforePTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetPacketNumberBeforePTS(): index size %lu", ptsRing.size());
    dsyslog("cIndex::GetPacketNumberBeforePTS(): found video frame (%d) PTS %" PRId64 " before PTS %" PRId64, frame.frameNumber, frame.pts, pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
    // missing audio PTS in index can happen if audio PTS is after video PTS
    dsyslog("cIndex::GetPacketNumberBeforePTS(): pts %ld: not found, index contains from %ld to %ld", pts, ptsMin, ptsMax);
#endif
    return frame.frameNumber;
}



int cIndex::GetPacketNumberAfterPTS(const int64_t pts) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPacketNumberAfterPTS(): index is empty");
        return -1;
    }
    struct sFrameType {
        int frameNumber = -1;
        int64_t pts     = INT64_MAX;
    } frame;
#ifdef DEBUG_RING_PTS_LOOKUP
    int64_t ptsMin =  INT64_MAX;
    int64_t ptsMax =  0;
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
#endif

    for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
#ifdef DEBUG_RING_PTS_LOOKUP
//        dsyslog("cIndex::GetFrameAfterPTS(): frame (%6d) PTS %" PRId64, ptsIterator->frameNumber, ptsIterator->pts);
        ptsMin = std::min(ptsMin, ptsIterator->pts);
        ptsMax = std::max(ptsMax, ptsIterator->pts);
#endif
        // search smallest video PTS after given PTS
        if ((ptsIterator->pts >= pts) && (ptsIterator->pts <= frame.pts)) {  // get frame with lowest pts after taget pts, we can not break here because it can out of sequence
            frame.frameNumber = ptsIterator->frameNumber;
            frame.pts         = ptsIterator->pts;
        }
    }
#ifdef DEBUG_RING_PTS_LOOKUP
    dsyslog("cIndex::GetPacketNumberAfterPTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetPacketNumberAfterPTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetPacketNumberAfterPTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetPacketNumberAfterPTS(): index size %lu", ptsRing.size());
    if (frame.frameNumber == -1) dsyslog("cIndex::GetPacketNumberAfterPTS(): video frame after PTS %ld not found", pts);
    else    dsyslog("cIndex::GetPacketNumberAfterPTS(): found video frame (%d) PTS %" PRId64 " after PTS %" PRId64, frame.frameNumber, frame.pts, pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
    // missing audio PTS in index can happen if audio PTS is after video PTS
    dsyslog("cIndex::GetPacketNumberAfterPTS(): PTS %ld: not found, index contains from %ld to %ld", pts, ptsMin, ptsMax);
#endif
    return frame.frameNumber;
}
