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
    dsyslog("cIndex::SetStartPTS(): PTS %ld: start of video stream, time base %d/%d", start_time_param, time_base_param.num, time_base_param.den);
    start_time = start_time_param;
    time_base  = time_base_param;
}


// add a new entry to the list of frame timestamps
void cIndex::Add(const int fileNumber, const int packetNumber, const int64_t pts, const int frameTimeOffset_ms) {
    if ((packetNumber > 0) && (frameTimeOffset_ms == 0)) {
        esyslog("cIndex::Add(): invalid index entry at packet (%5d): frameTimeOffset_ms %d", packetNumber, frameTimeOffset_ms);
    }
    if (indexVector.empty() || (packetNumber > indexVector.back().frameNumber)) {
        // add new frame timestamp to vector
        sIndexElement newIndex;
        newIndex.fileNumber         = fileNumber;
        newIndex.frameNumber        = packetNumber;
        newIndex.pts                = pts;
        newIndex.frameTimeOffset_ms = frameTimeOffset_ms;

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


// get iFrame before given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetIFrameBeforePTS(const int64_t pts) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetIFrameBeforePTS(): frame index not initialized");
        return -1;
    }
    int iFrameBefore  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->pts <= pts) iFrameBefore = frameIterator->frameNumber;
        else break;
    }
    if (iFrameBefore < 0) dsyslog("cIndex::GetIFrameBeforePTS(): PTS %ld: not in index, index content: first PTS %ld , last PTS %ld", pts, indexVector.front().pts, indexVector.back().pts);
    dsyslog("cIndex::GetIFrameBeforePTS(): PTS %ld: frame (%d) found", pts, iFrameBefore);
    return iFrameBefore; // frame not (yet) in index
}


// get iFrame after given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetIFrameAfterPTS(const int64_t pts) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetIFrameAfterPTS(): frame index not initialized");
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
        dsyslog("cIndex::GetIFrameAfterPTS(): PTS %ld: not in index, index content: first PTS %ld , last PTS %ld", pts, indexVector.front().pts, indexVector.back().pts);
        return -1;
    }
    dsyslog("cIndex::GetIFrameAfterPTS(): PTS %ld: frame (%d) found", pts, found->frameNumber);
    return found->frameNumber;
}


int cIndex::GetFrameBefore(int frameNumber) {
    if (frameNumber == 0) return 0;
    if (fullDecode) return frameNumber - 1;
    else {
        int iFrameBefore = GetIFrameBefore(frameNumber - 1);  // if frameNumber is i-frame, GetIFrameBefore() will return same frameNumber
        if (iFrameBefore < 0) {
            esyslog("cIndex::GetFrameBefore(): frame (%d): GetIFrameBefore() failed", frameNumber);
            iFrameBefore = frameNumber - 1;  // fallback
        }
        return iFrameBefore;
    }
}


int cIndex::GetFrameAfter(int frameNumber) {
    if (fullDecode) return (frameNumber + 1);
    else {
        int iFrameAfter = GetIFrameAfter(frameNumber);
        if (iFrameAfter < 0) {
            esyslog("cIndex::GetFrameAfter(): frame (%d): GetIFrameAfter() failed", frameNumber);
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
    return indexVector.back().frameNumber;
}


// get iFrame before given frame
// if frame is a iFrame, i-frame before will be returned
// return: iFrame number, -1 if index is not initialized
//
int cIndex::GetIFrameBefore(int frameNumber) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetIFrameBefore(): frame index not initialized");
        return -1;
    }
    int before_iFrame = -1;
    int iFrameBefore  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber > frameNumber) {
            iFrameBefore = before_iFrame;
            break;
        }
        else before_iFrame = frameIterator->frameNumber;
    }
    if (iFrameBefore < 0) {
        dsyslog("cIndex::GetIFrameBefore(): frame (%d): failed, index content: first frame (%d), last frame (%d)", frameNumber, indexVector.front().frameNumber, indexVector.back().frameNumber);
        return -1;
    }
    return iFrameBefore; // frame not (yet) in index
}


// get iFrame after given frame
// if frame is a iFrame, next i-frame will be returned
// return: i-frame number
//
int cIndex::GetIFrameAfter(int packetNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameAfter(): packet index not initialized");
        return -1;
    }
    // request from current packet after last key packet, this can happen if markad runs during recording
    if ((packetNumber > indexVector.back().frameNumber) && (packetNumber < indexVector.back().frameNumber - 100)) {  // max alowed key packet distance
        dsyslog("cIndex::GetIFrameAfter(): packet (%d) > last packet in index, return last index element (%d)", packetNumber, indexVector.back().frameNumber);
        return indexVector.back().frameNumber;
    }

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [packetNumber](const sIndexElement &value) ->bool { if (value.frameNumber > packetNumber) return true; else return false; });
    if (found != indexVector.end()) return found->frameNumber;

    esyslog("cIndex::GetIFrameAfter(): packet (%d): failed, index content: first frame (%d), last frame (%d)", packetNumber, indexVector.front().frameNumber, indexVector.back().frameNumber);
    return -1;
}


// return sum of packet duration from i-frame if called with an i-frame number, otherwise from i-frame after
int cIndex::GetSumDurationFromFrame(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().frameNumber) return indexVector.back().frameTimeOffset_ms;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](sIndexElement const &value) ->bool { if (value.frameNumber >= frameNumber) return true; else return false; });
    if (found == indexVector.end()) {
        esyslog("cIndex::GetSumDurationFromFrame(): frame (%d) not in index", frameNumber);
        dsyslog("cIndex::GetSumDurationFromFrame(): index content: first frame (%d) , last frame (%d)", indexVector.front().frameNumber, indexVector.back().frameNumber);
        return -1;
    }
    return found->frameTimeOffset_ms;

    /*
        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->frameNumber >= frameNumber) return frameIterator->frameTimeOffset_ms;
        }

    */
}


// return PTS from i-frame if called with an i-frame number, otherwise from i-frame after
int64_t cIndex::GetPTSfromFrame(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().frameNumber) return indexVector.back().pts;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](sIndexElement const &value) ->bool { if (value.frameNumber >= frameNumber) return true; else return false; });
    if (found == indexVector.end()) {
        esyslog("cIndex::GetSumDurationFromFrame(): frame (%d) not in index", frameNumber);
        dsyslog("cIndex::GetSumDurationFromFrame(): index content: first frame (%d) , last frame (%d)", indexVector.front().frameNumber, indexVector.back().frameNumber);
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


int cIndex::GetTimeFromFrame(const int packetNumber, const bool isVDR) {
    if (indexVector.empty()) {  // expected if called to write start mark during running recording
        dsyslog("cIndex::GetTimeFromFrame(): packet (%d): index not initialized", packetNumber);
        return -1;
    }
    if (isVDR) {  // use sum of packet duration
        return GetSumDurationFromFrame(packetNumber);
    }
    else {  // use PTS based time from i-frame index
        int64_t framePTS = GetPTSfromFrame(packetNumber);
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
        dsyslog("cIndex::GetFrameFromOffset(): search for PTS %ld, index content: first PTS %ld , last PTS %ld", pts, indexVector.front().pts, indexVector.back().pts);
        return -1;
    }

    dsyslog("cIndex::GetFrameFromOffset(): frame (%d) found", found->frameNumber);
    return found->frameNumber;

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
        if (frameIterator->frameNumber >= beginFrame) {
            counter++;
            if (frameIterator->frameNumber >= endFrame) return counter;
        }
    }
    dsyslog("cIndex::GetIFrameRangeCount(): failed beginFrame (%d) endFrame (%d) last frame in index list (%d)", beginFrame, endFrame, indexVector.back().frameNumber);
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


int cIndex::GetFrameBeforePTS(const int64_t pts) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetFrameBeforePTS(): index is empty");
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
    dsyslog("cIndex::GetFrameBeforePTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetFrameBeforePTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetFrameBeforePTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetFrameBeforePTS(): index size %lu", ptsRing.size());
    dsyslog("cIndex::GetFrameBeforePTS(): found video frame (%d) PTS %" PRId64 " before PTS %" PRId64, frame.frameNumber, frame.pts, pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
    // missing audio PTS in index can happen if audio PTS is after video PTS
    dsyslog("cIndex::GetFrameBeforePTS(): pts %ld: not found, index contains from %ld to %ld", pts, ptsMin, ptsMax);
#endif
    return frame.frameNumber;
}



int cIndex::GetFrameAfterPTS(const int64_t pts) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetFrameAfterPTS(): index is empty");
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
    dsyslog("cIndex::GetFrameAfterPTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetFrameAfterPTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetFrameAfterPTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetFrameAfterPTS(): index size %lu", ptsRing.size());
    if (frame.frameNumber == -1) dsyslog("cIndex::GetFrameAfterPTS(): video frame after PTS %ld not found", pts);
    else    dsyslog("cIndex::GetFrameAfterPTS(): found video frame (%d) PTS %" PRId64 " after PTS %" PRId64, frame.frameNumber, frame.pts, pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
    // missing audio PTS in index can happen if audio PTS is after video PTS
    dsyslog("cIndex::GetFrameAfterPTS(): PTS %ld: not found, index contains from %ld to %ld", pts, ptsMin, ptsMax);
#endif
    return frame.frameNumber;
}
