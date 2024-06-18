/*
 * index.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "index.h"
#include "debug.h"


cIndex::cIndex() {
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


// add a new entry to the list of frame timestamps
void cIndex::Add(const int fileNumber, const int frameNumber, const int ptsTimeOffset_ms, const int frameTimeOffset_ms) {
    if ((frameNumber > 0) && ((ptsTimeOffset_ms == 0) || (frameTimeOffset_ms == 0))) {
        esyslog("cIndex::Add(): invalid index entry at frame (%5d): ptsTimeOffset_ms %d, frameTimeOffset_ms %d", frameNumber, ptsTimeOffset_ms, frameTimeOffset_ms);
    }
    if (indexVector.empty() || (frameNumber > indexVector.back().frameNumber)) {
        // add new frame timestamp to vector
        sIndexElement newIndex;
        newIndex.fileNumber         = fileNumber;
        newIndex.frameNumber        = frameNumber;
        newIndex.ptsTimeOffset_ms   = ptsTimeOffset_ms;
        newIndex.frameTimeOffset_ms = frameTimeOffset_ms;

        if (indexVector.size() == indexVector.capacity()) {
            indexVector.reserve(1000);
        }
#ifdef DEBUG_INDEX
        dsyslog("cIndex::Add(): fileNumber %d, frameNumber (%d), ptsTimeOffset_ms %d, frameTimeOffset_ms %d", fileNumber, frameNumber, ptsTimeOffset_ms, frameTimeOffset_ms);
#endif
        indexVector.push_back(newIndex);
        ALLOC(sizeof(sIndexElement), "indexVector");
    }
}


// get nearest iFrame to given frame
// if frame is a iFrame, frame will be returned
// return: iFrame number
//
/*  not used
int cIndex::GetIFrameNear(int frame) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameNear(): frame index not initialized");
        return -1;
    }
    int before_iFrame = -1;
    int after_iFrame  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber >= frame) {
            after_iFrame = frameIterator->frameNumber;
            break;
        }
        else before_iFrame = frameIterator->frameNumber;
    }
    if ((before_iFrame == -1) || (after_iFrame == -1)) {
        dsyslog("cIndex::GetIFrameNear(): failed for frame (%d), index: first frame (%d) last frame (%d)", frame, indexVector.front().frameNumber, indexVector.back().frameNumber);
        return -2; // frame not yet in index
    }
    if ((after_iFrame - frame) < (frame - before_iFrame)) return after_iFrame;
    else return before_iFrame;
}
*/


// get iFrame before given frame
// if frame is a iFrame, frame will be returned
// if frame > last iFrame from index, return last iFrame
// return: iFrame number, -1 if index is not initialized
//
int cIndex::GetIFrameBefore(int frameNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameBefore(): frame index not initialized");
        return -1;
    }
    int before_iFrame = -1;
    int iFrameBefore  = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber == frameNumber) {
            iFrameBefore = frameNumber; // given frame is a iFrame
            break;
        }
        if (frameIterator->frameNumber > frameNumber) {
            iFrameBefore = before_iFrame;
            break;
        }
        else before_iFrame = frameIterator->frameNumber;
    }
    if (iFrameBefore < 0) iFrameBefore = indexVector.back().frameNumber;  // use last iFrame from index
    return iFrameBefore; // frame not (yet) in index
}


// get iFrame after given frame
// if frame is a iFrame, frame will be returned
// return: iFrame number
//
int cIndex::GetIFrameAfter(int frameNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameAfter(): frame index not initialized");
        return -1;
    }

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](const sIndexElement &value) ->bool { if (value.frameNumber >= frameNumber) return true; else return false; });
    if (found != indexVector.end()) return found->frameNumber;

    dsyslog("cIndex::GetIFrameAfter(): failed for frame (%d)", frameNumber);
    return -1;

}


int cIndex::GetTimeFromFrame(const int frameNumber, const bool isVDR) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetTimeFromFrame(): frame index not initialized");
        return -1;
    }
#ifdef DEBUG_SAVEMARKS
    dsyslog("cIndex::GetTimeFromFrame(): frameNumber (%d), isVDR %d", frameNumber, isVDR);
#endif
    int before_ms     = -1;
    int before_iFrame = -1;
    int offset        = -1;

    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
#ifdef DEBUG_SAVEMARKS
//        dsyslog("cIndex::GetTimeFromFrame(): frame (%6d): offset time is %6dms", frameIterator->frameNumber, frameIterator->ptsTimeOffset_ms);
#endif
        if (frameIterator->frameNumber == frameNumber) {
            if (isVDR) {
                offset = frameIterator->frameTimeOffset_ms;
                break;
            }
            else {
                offset = frameIterator->ptsTimeOffset_ms;
                break;
            }
        }
        if (frameIterator->frameNumber > frameNumber) {
            if (abs(frameNumber - before_iFrame) < abs(frameNumber - frameIterator->frameNumber)) {
                offset = before_ms;
                break;
            }
            else {
                if (isVDR) {
                    offset = frameIterator->frameTimeOffset_ms;
                    break;
                }
                else {
                    offset = frameIterator->ptsTimeOffset_ms;
                    break;
                }
            }
        }
        else {
            before_iFrame = frameIterator->frameNumber;
            if (isVDR) before_ms = frameIterator->frameTimeOffset_ms;
            else       before_ms = frameIterator->ptsTimeOffset_ms;
        }
    }
    if (frameNumber > (indexVector.back().frameNumber - 30)) {  // we are after last iFrame but before next iFrame, possible not read jet, use last iFrame
        if (isVDR) offset = indexVector.back().frameTimeOffset_ms;
        else offset = indexVector.back().ptsTimeOffset_ms;
    }
#ifdef DEBUG_SAVEMARKS
    dsyslog("cIndex::GetTimeFromFrame(): frame (%d): offset from start %dms, isVDR %d", frameNumber, offset, isVDR);
#endif
    if (offset < 0) esyslog("cIndex::GetTimeFromFrame(): could not find time for frame (%d), list content: first frame (%d) , last frame (%d)", frameNumber, indexVector.front().frameNumber, indexVector.back().frameNumber);
    return offset;
}


int cIndex::GetFrameFromOffset(int offset_ms) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetFrameFromOffset: frame index not initialized");
        return -1;
    }
    int iFrameBefore = 0;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->ptsTimeOffset_ms > offset_ms) return iFrameBefore;
        iFrameBefore = frameIterator->frameNumber;
    }
    return iFrameBefore;  // return last frame if offset is not in recording, needed for VPS stopped recordings
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


int cIndex::GetVideoFrameToPTS(const int64_t pts, const bool before) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetVideoFrameToPTS(): index is empty");
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
    if (!before) frame.pts = INT64_MAX;

    for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
#ifdef DEBUG_RING_PTS_LOOKUP
        dsyslog("cIndex::GetVideoFrameToPTS(): frame (%6d) PTS %" PRId64, ptsIterator->frameNumber, ptsIterator->pts);
        ptsMin = std::min(ptsMin, ptsIterator->pts);
        ptsMax = std::max(ptsMax, ptsIterator->pts);
#endif
        if (before) {  // search smallest Video PTS before or equal given PTS
            if ((ptsIterator->pts <= pts) && (ptsIterator->pts >= frame.pts)) {  // reset state if we found a frame with pts smaller than target, they can be out of sequence
                frame.frameNumber = ptsIterator->frameNumber;
                frame.pts         = ptsIterator->pts;
            }
        }
        else {  // search smallest video PTS after given PTS
            if ((ptsIterator->pts >= pts) && (ptsIterator->pts <= frame.pts)) {  // get frame with lowest pts after taget pts, we can not break here because it can out of sequence
                frame.frameNumber = ptsIterator->frameNumber;
                frame.pts         = ptsIterator->pts;
            }
        }
    }
#ifdef DEBUG_RING_PTS_LOOKUP
    dsyslog("cIndex::GetVideoFrameToPTS(): PTS      min: %" PRId64, ptsMin);
    dsyslog("cIndex::GetVideoFrameToPTS(): PTS searched: %" PRId64, pts);
    dsyslog("cIndex::GetVideoFrameToPTS(): PTS      max: %" PRId64, ptsMax);
    dsyslog("cIndex::GetVideoFrameToPTS(): index size %lu", ptsRing.size());
    dsyslog("cIndex::GetVideoFrameToPTS(): found video frame (%d) PTS %" PRId64 " %s PTS %" PRId64, frame.frameNumber, frame.pts, (before) ? "before" : "after", pts);
    dsyslog("----------------------------------------------------------------------------------------------------------------------");
#endif
    if (frame.frameNumber < 0) {
        if (ptsRing.front().frameNumber == 0) frame.frameNumber = 0;  // return start of recording
    }
    // error message should be done by calling function
    // missing audio PTS in index can happen if audio PTS is after video PTS
    return frame.frameNumber;
}
