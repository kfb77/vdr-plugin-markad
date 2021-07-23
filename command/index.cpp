/*
 * index.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "index.h"
extern "C" {
    #include "debug.h"
}


cIndex::cIndex() {
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


int cIndex::GetLastFrameNumber() {
     if (!indexVector.empty()) return indexVector.back().frameNumber;
     else return -1;
}


// add a new entry to the list of frame timestamps
void cIndex::Add(int fileNumber, int frameNumber, int timeOffset_ms) {
     if (GetLastFrameNumber() < frameNumber) {
        // add new frame timestamp to vector
         sIndexElement newIndex;
         newIndex.fileNumber = fileNumber;
         newIndex.frameNumber = frameNumber;
         newIndex.timeOffset_ms = timeOffset_ms;
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
// return: iFrame number
//
int cIndex::GetIFrameBefore(int frameNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameBefore(): frame index not initialized");
        return -1;
    }
    int before_iFrame = 0;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber >= frameNumber) {
            return before_iFrame;
        }
        else before_iFrame = frameIterator->frameNumber;
    }
    dsyslog("cIndex::GetIFrameBefore(): failed for frame (%d), index: first frame (%d) last frame (%d)", frameNumber, indexVector.front().frameNumber, indexVector.back().frameNumber);
    return -2; // frame not yet in index
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
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber >= frameNumber) {
            return frameIterator->frameNumber;
        }
    }
    dsyslog("cIndex::GetIFrameAfter(): failed for frame (%d)", frameNumber);
    return -1;
}


int cIndex::GetTimeFromFrame(int frameNumber) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetTimeFromFrame(): frame index not initialized");
        return -1;
    }
    int before_ms = 0;
    int before_iFrame = 0;

    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber == frameNumber) {
            tsyslog("cIndex::GetTimeFromFrame(): frame (%d) time is %dms", frameNumber, frameIterator->timeOffset_ms);
            return frameIterator->timeOffset_ms;
        }
        if (frameIterator->frameNumber > frameNumber) {
            if (abs(frameNumber - before_iFrame) < abs(frameNumber - frameIterator->frameNumber)) {
                return before_ms;
            }
            else {
                return frameIterator->timeOffset_ms;
            }
        }
        else {
            before_iFrame = frameIterator->frameNumber;
            before_ms = frameIterator->timeOffset_ms;
        }
    }
    if (frameNumber > (indexVector.back().frameNumber - 30)) {  // we are after last iFrame but before next iFrame, possible not read jet, use last iFrame
        return indexVector.back().timeOffset_ms;
    }
    dsyslog("cIndex::GetTimeFromFrame(): could not find time for frame (%d), last frame in index list (%d)", frameNumber, indexVector.back().frameNumber);
    return -1;
}


int cIndex::GetFrameFromOffset(int offset_ms) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetFrameFromOffset: frame index not initialized");
        return -1;
    }
    int iFrameBefore = 0;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->timeOffset_ms > offset_ms) return iFrameBefore;
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
    // add new frame timestamp to vector
    sPTS_RingbufferElement newPTS;
    newPTS.frameNumber = frameNumber;
    newPTS.pts = pts;
    ptsRing.push_back(newPTS);
    ALLOC(sizeof(sPTS_RingbufferElement), "ptsRing");

    // delete oldest entry
    if (ptsRing.size() > 35) {  // changed from 20 to 35
        ptsRing.erase(ptsRing.begin());
        FREE(sizeof(sPTS_RingbufferElement), "ptsRing");
    }

}


int cIndex::GetFirstVideoFrameAfterPTS(const int64_t pts) {
    struct afterType {
        int frameNumber = -1;
        int64_t pts = INT64_MAX;
    } after;

    for (std::vector<sPTS_RingbufferElement>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
        if (ptsIterator->pts < pts) {  // reset state if we found a frame with pts samaller than target
            after.frameNumber = -1;
            after.pts = INT64_MAX;
        }
        else {
            if (ptsIterator->pts < after.pts) {  // get frame with lowest pts after taget pts
                after.frameNumber = ptsIterator->frameNumber;
                after.pts = ptsIterator->pts;
            }
        }
    }
    dsyslog("cIndex::GetFirstVideoFrameAfterPTS(): found video frame (%d) PTS %" PRId64 " after PTS %" PRId64, after.frameNumber, after.pts, pts);
    return after.frameNumber;
}
