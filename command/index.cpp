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
        FREE(sizeof(indexType), "indexVector");
    }
    size = ptsRing.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(ptsRingType), "ptsRing");
    }
#endif
    indexVector.clear();
    ptsRing.clear();
}


int cIndex::GetLastFrameNumber() {
     if (!indexVector.empty()) return indexVector.back().frameNumber;
     else return -1;
}


int64_t cIndex::GetLastTime() {
     if (!indexVector.empty()) return indexVector.back().pts_time_ms;
     else return -1;
}


// add a new entry to the list of frame timestamps
void cIndex::Add(int fileNumber, int frameNumber, int64_t pts_time_ms) {
     if (GetLastFrameNumber() < frameNumber) {
// check value of increasing timestamp
          int64_t lastTime = GetLastTime();
          if (lastTime >= 0) { // we have at least one frame stored
              int diff_ms = static_cast<int> (pts_time_ms - lastTime);
              if (diff_ms_maxValid == 0) diff_ms_maxValid = diff_ms;
              if (diff_ms_maxValid < 200) diff_ms_maxValid = 200;  // set minimum duration
              if (diff_ms > diff_ms_maxValid * 5) esyslog("presentation timestamp in video stream at frame (%5d) increased %3ds %3dms, max valid is %3ds %3dms", frameNumber, diff_ms / 1000, diff_ms % 1000, diff_ms_maxValid / 1000, diff_ms_maxValid % 1000);
              else if (diff_ms_maxValid < diff_ms) diff_ms_maxValid = diff_ms;
#ifdef DEBUG_INDEX_BUILD
             dsyslog("cIndex::Add(): file number %2d frame (%5d) pts_time_ms %8ld, increased %4dms, max valid %4dms", fileNumber, frameNumber, pts_time_ms, diff_ms, diff_ms_maxValid);
#endif
          }
        // add new frame timestamp to vector
         indexType newIndex;
         newIndex.fileNumber = fileNumber;
         newIndex.frameNumber = frameNumber;
         newIndex.pts_time_ms = pts_time_ms;
         indexVector.push_back(newIndex);
         ALLOC(sizeof(indexType), "indexVector");
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
    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
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
int cIndex::GetIFrameBefore(int frame) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameBefore(): frame index not initialized");
        return -1;
    }
    int before_iFrame = 0;
    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber > frame) {
            return before_iFrame;
        }
        else before_iFrame = frameIterator->frameNumber;
    }
    dsyslog("cIndex::GetIFrameBefore(): failed for frame (%d), index: first frame (%d) last frame (%d)", frame, indexVector.front().frameNumber, indexVector.back().frameNumber);
    return -2; // frame not yet in index
}


// get iFrame after given frame
// if frame is a iFrame, frame will be returned
// return: iFrame number
//
int cIndex::GetIFrameAfter(int frame) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetIFrameAfter(): frame index not initialized");
        return -1;
    }
    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber >= frame) {
            return frameIterator->frameNumber;
        }
    }
    dsyslog("cIndex::GetIFrameAfter(): failed for frame (%d)", frame);
    return -1;
}


int64_t cIndex::GetTimeFromFrame(int frame) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetTimeFromFrame(): frame index not initialized");
        return -1;
    }
    int64_t before_pts = 0;
    int before_iFrame = 0;

    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->frameNumber == frame) {
            tsyslog("cIndex::GetTimeFromFrame(): frame (%d) time is %" PRId64" ms", frame, frameIterator->pts_time_ms);
            return frameIterator->pts_time_ms;
        }
        if (frameIterator->frameNumber > frame) {
            if (abs(frame - before_iFrame) < abs(frame - frameIterator->frameNumber)) {
                return before_pts;
            }
            else {
                return frameIterator->pts_time_ms;
            }
        }
        else {
            before_iFrame = frameIterator->frameNumber;
            before_pts = frameIterator->pts_time_ms;
        }
    }
    if (frame > (indexVector.back().frameNumber - 30)) {  // we are after last iFrame but before next iFrame, possible not read jet, use last iFrame
        return indexVector.back().frameNumber;
    }
    dsyslog("cIndex::GetTimeFromFrame(): could not find time for frame (%d), last frame in index list (%d)", frame, indexVector.back().frameNumber);
    return -1;
}


int cIndex::GetFrameFromOffset(int offset_ms) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetFrameFromOffset: frame index not initialized");
        return -1;
    }
    int iFrameBefore = 0;
    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->pts_time_ms > offset_ms) return iFrameBefore;
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

    for (std::vector<indexType>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
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
    ptsRingType newPTS;
    newPTS.frameNumber = frameNumber;
    newPTS.pts = pts;
    ptsRing.push_back(newPTS);
    ALLOC(sizeof(ptsRingType), "ptsRing");

    // delete oldest entry
    if (ptsRing.size() > 20) {
        ptsRing.erase(ptsRing.begin());
        FREE(sizeof(ptsRingType), "ptsRing");
    }

}


int cIndex::GetFirstVideoFrameAfterPTS(const int64_t pts) {
    struct afterType {
        int frameNumber = -1;
        int64_t pts = INT64_MAX;
    } after;

    for (std::vector<ptsRingType>::iterator ptsIterator = ptsRing.begin(); ptsIterator != ptsRing.end(); ++ptsIterator) { // get framenumber after
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
    dsyslog("cIndex::GetFirstVideoFrameAfterPTS(): found video frame (%d) PTS %ld after PTS %ld", after.frameNumber, after.pts, pts);
    return after.frameNumber;
}
