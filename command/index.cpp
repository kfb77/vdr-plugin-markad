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
    size = pSliceVector.size();
    for (int i = 0 ; i < size; i++) {
        FREE(sizeof(int64_t), "pSliceVector");
    }
#endif
    indexVector.clear();
    ptsRing.clear();
    pSliceVector.clear();
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
    if (indexVector.empty() || (packetNumber > indexVector.back().packetNumber)) { // only add new packets to index
        if (!indexVector.empty() && !rollover) {
            if ((indexVector.back().pts > 0x200000000) && (pts < 0x200000000)) {  // PTS/DTS rollover
                dsyslog("cIndex::Add(): packet(%d): PTS/DTS rollover from PTS %" PRId64 " to PTS %" PRId64, packetNumber, indexVector.back().pts, pts);
                rollover = true;
            }
        }
        sIndexElement newIndex;
        newIndex.fileNumber         = fileNumber;
        newIndex.packetNumber       = packetNumber;
        newIndex.pts                = pts;
        newIndex.rollover           = rollover;

        if (indexVector.size() == indexVector.capacity()) {
            indexVector.reserve(1000);
        }
        indexVector.push_back(newIndex);
        ALLOC(sizeof(sIndexElement), "indexVector");

#ifdef DEBUG_INDEX
        dsyslog("cIndex::Add(): fileNumber %d, packetNumber (%5d), rollover %d, PTS %6ld: time offset PTS %6dms", fileNumber, packetNumber, rollover, pts, GetTimeOffsetFromKeyPacketAfter(packetNumber));
#endif
    }
}


// get key packet number before given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetKeyPacketNumberBeforePTS(const int64_t pts, int64_t *beforePTS, const bool isPTSinSlice) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberBeforePTS(): packet index not initialized");
        return -1;
    }
    int beforePacketNumber = -1;
    if (beforePTS) *beforePTS  = -1;

    for (std::vector<sIndexElement>::iterator packetIterator = indexVector.begin(); packetIterator != indexVector.end(); ++packetIterator) {
        if (rollover && (pts < indexVector.front().pts) && !packetIterator->rollover) continue;
        if (isPTSinSlice && !packetIterator->isPTSinSlice) continue;
        if (packetIterator->pts <= pts) {
            beforePacketNumber = packetIterator->packetNumber;
            if (beforePTS) *beforePTS  = packetIterator->pts;
        }
        else break;
    }
    if (beforePacketNumber < 0) dsyslog("cIndex::GetKeyPacketNumberBeforePTS(): PTS %" PRId64 ": not in index, index content: first PTS %" PRId64 " , last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
    return beforePacketNumber; // frame not (yet) in index
}


// get key packet number after given PTS
// return: iFrame number, -1 if index is not initialized or PTS not in index
//
int cIndex::GetKeyPacketNumberAfterPTS(const int64_t pts, int64_t *afterPTS, const bool isPTSinSlice) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberAfterPTS(): packet index not initialized");
        return -1;
    }
    if (pts < 0) {
        esyslog("cIndex::GetKeyPacketNumberAfterPTS(): invalid PTS %" PRId64, pts);
        return -1;
    }

    int afterPacketNumber = -1;
    if (afterPTS) *afterPTS = -1;
    // PTS of key packets are monotonically increasing
    for (std::vector<sIndexElement>::iterator packetIterator = indexVector.begin(); packetIterator != indexVector.end(); ++packetIterator) {
        if (rollover && (pts < indexVector.front().pts) && !packetIterator->rollover) continue;
        if (packetIterator->pts >= pts) {
            if (isPTSinSlice && !packetIterator->isPTSinSlice) continue;   // isPTSinSlice is always true for non H.264
            afterPacketNumber = packetIterator->packetNumber;
            if (afterPTS) *afterPTS = packetIterator->pts;
            break;
        }
    }
    if (afterPacketNumber < 0) {
        if (isPTSinSlice) dsyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": no p-slice after found", pts);
        else esyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": packet after not in index, index content: first PTS %" PRId64 ", last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
    }
    return afterPacketNumber;

    /*
        std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [pts, isPTSinSlice](sIndexElement const &value) ->bool {
            if ((value.isPTSinSlice || !isPTSinSlice) && (value.pts >= pts)) return true;
            else return false; });
        if (found == indexVector.end()) {
            dsyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": not in index, index content: first PTS %" PRId64 ", last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
            if (afterPTS) *afterPTS = -1;
            return -1;
        }
    #ifdef DEBUG_RING_PTS_LOOKUP
        dsyslog("cIndex::GetKeyPacketNumberAfterPTS(): PTS %" PRId64 ": packet (%d) found", pts, found->packetNumber);
    #endif
        if (afterPTS) *afterPTS = found->pts;
        return found->packetNumber;
        */
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


int cIndex::GetPacketNumberAfter(int packetNumber) {
    if (fullDecode) return (packetNumber + 1);
    else {
        int keyPacketNumberAfter = GetKeyPacketNumberAfter(packetNumber + 1);  // prevent to get same packet if packetNumber is key packet
        if (keyPacketNumberAfter < 0) {
            esyslog("cIndex::GetPacketNumberAfter(): packet (%d): GetKeyPacketAfter() failed", packetNumber);
            return - 1;
        }
        return keyPacketNumberAfter;
    }
}


sIndexElement *cIndex::GetIndexElementFromPTS(const int64_t pts) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetIndexElementFromPTS: packet index not initialized");
        return nullptr;
    }
    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [pts](const sIndexElement &value) ->bool { if (value.pts == pts) return true; else return false; });
    if (found != indexVector.end()) return &(*found);
    return nullptr;

    /*
        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->pts == pts) return &(*frameIterator);
        }
        esyslog("cIndex::GetIndexElementFromPTS(): PTS %" PRId64 ": not in index, index content: first PTS %" PRId64 " , last PTS %" PRId64, pts, indexVector.front().pts, indexVector.back().pts);
        return nullptr; // frame not (yet) in index
        */
}


sIndexElement *cIndex::GetLastPacket() {
    if (indexVector.empty()) return nullptr;   // normal in first call before Add()
    return &indexVector.back();
}


// get key packet before given packet
// if packet is a key packet, key packet before will be returned
// return: key packet number, -1 if index is not initialized
//
int cIndex::GetKeyPacketNumberBefore(int packetNumber, int64_t *beforePTS) {
    if (indexVector.empty()) {
        esyslog("cIndex::GetKeyPacketNumberBefore(): frame index not initialized");
        return -1;
    }
    int beforePacketNumber = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->packetNumber > packetNumber) break;
        beforePacketNumber        = frameIterator->packetNumber;
        if (beforePTS) *beforePTS = frameIterator->pts;
    }
    if (beforePacketNumber < 0) dsyslog("cIndex::GetKeyPacketNumberBefore(): packet (%d): failed, index content: first packet (%d), last packet (%d)", packetNumber, indexVector.front().packetNumber, indexVector.back().packetNumber);
    return beforePacketNumber; // frame not (yet) in index
}


// get key packet number after given packet number
// if packetNumber is a key packet, packet number self will be returned
// return: next key packet number
//
int cIndex::GetKeyPacketNumberAfter(int packetNumber, int64_t *afterPTS) {
    if (indexVector.empty()) {
        dsyslog("cIndex::GetKeyPacketNumberAfter(): packet index not initialized");
        return -1;
    }
    if (packetNumber < 0) return -1;
    // request from current packet after last key packet, this can happen if markad runs during recording
    if ((packetNumber > indexVector.back().packetNumber) && (packetNumber < indexVector.back().packetNumber - 100)) {  // max alowed key packet distance
        dsyslog("cIndex::GetKeyPacketNumberAfter(): packet (%d) > last packet in index, return last index element (%d)", packetNumber, indexVector.back().packetNumber);
        if (afterPTS) *afterPTS = indexVector.back().pts;
        return indexVector.back().packetNumber;
    }

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [packetNumber](const sIndexElement &value) ->bool { if (value.packetNumber >= packetNumber) return true; else return false; });
    if (found != indexVector.end()) {
        if (afterPTS) *afterPTS = found->pts;
        return found->packetNumber;
    }

    esyslog("cIndex::GetKeyPacketNumberAfter(): packet (%d): failed, index content: first packet (%d), last packet (%d)", packetNumber, indexVector.front().packetNumber, indexVector.back().packetNumber);
    return -1;
}


/* unused
// return PTS from key packet if called with an key number, otherwise from key packet before
int64_t cIndex::GetPTSBeforeKeyPacketNumber(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().packetNumber) return indexVector.back().pts;

    int64_t pts = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->packetNumber <= frameNumber) pts = frameIterator->pts;
    }
    return pts;
}
*/


/* unused
int64_t cIndex::GetKeyPacketPTSBeforePTS(int64_t pts) {
    int64_t beforePTS = -1;
    for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
        if (frameIterator->pts <= pts) beforePTS = frameIterator->pts;
    }
    if (beforePTS == -1) esyslog("cIndex::GetPTSKeyPacketBeforePTS: PTS %ld failed", pts);
    return beforePTS;
}
*/


// return PTS from key packet if called with an key number, otherwise from packet after
int64_t cIndex::GetPTSAfterKeyPacketNumber(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().packetNumber) return indexVector.back().pts;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](sIndexElement const &value) ->bool { if (value.packetNumber >= frameNumber) return true; else return false; });
    if (found == indexVector.end()) {
        esyslog("cIndex::GetPTSAfterKeyPacketNumber(): frame (%d) not in index", frameNumber);
        dsyslog("cIndex::GetPTSAfterKeyPacketNumber(): index content: first packet (%d) , last packet (%d)", indexVector.front().packetNumber, indexVector.back().packetNumber);
        return AV_NOPTS_VALUE;
    }
    return found->pts;

    /*

        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->frameNumber >= frameNumber) return frameIterator->pts;
        }
        return AV_NOPTS_VALUE;  // this shout never reached
    */
}


// return PTS from key packet if called with an key number, -1 otherwise
int64_t cIndex::GetPTSFromKeyPacketNumber(const int frameNumber) {
    // if frame number not yet in index, return PTS from last frame
    if (frameNumber >= indexVector.back().packetNumber) return indexVector.back().pts;

    std::vector<sIndexElement>::iterator found = std::find_if(indexVector.begin(), indexVector.end(), [frameNumber](sIndexElement const &value) ->bool { if (value.packetNumber == frameNumber) return true; else return false; });
    if (found == indexVector.end()) {
        dsyslog("cIndex::GetPTSFromKeyPacketNumber(): frame (%d) not in index", frameNumber);
        dsyslog("cIndex::GetPTSFromKeyPacketNumber(): index content: first packet (%d) , last packet (%d)", indexVector.front().packetNumber, indexVector.back().packetNumber);
        return AV_NOPTS_VALUE;
    }
    return found->pts;

    /*

        for (std::vector<sIndexElement>::iterator frameIterator = indexVector.begin(); frameIterator != indexVector.end(); ++frameIterator) {
            if (frameIterator->frameNumber >= frameNumber) return frameIterator->pts;
        }
        return AV_NOPTS_VALUE;  // this shout never reached
    */
}


int cIndex::GetTimeOffsetFromPTS(int64_t pts) {
    if (pts == AV_NOPTS_VALUE) {
        esyslog("cIndex::GetTimeOffsetFromPTS(): invalid PTS");
        return -1;
    }
    pts -= start_time;
    if (pts < 0 ) {
        pts += 0x200000000;    // libavodec restart at 0 if pts greater than 0x200000000
    }
    return round(1000 * pts * av_q2d(time_base));
}


int cIndex::GetTimeOffsetFromKeyPacketAfter(const int packetNumber) {
    if (indexVector.empty()) {  // expected if called to write start mark during running recording
        dsyslog("cIndex::GetTimeOffsetFromKeyPacketAfter(): packet (%d): index not initialized", packetNumber);
        return -1;
    }
    // use PTS based time from key packet index
    int64_t framePTS = GetPTSAfterKeyPacketNumber(packetNumber);
    if (framePTS == AV_NOPTS_VALUE) {
        esyslog("cIndex::GetTimeOffsetFromKeyPacketAfter(): packet (%d): get PTS failed", packetNumber);
        return -1;
    }
    int offsetTime_ms = GetTimeOffsetFromPTS(framePTS);
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


void cIndex::AddPTS(const int packetNumber, const int64_t pts) {
#ifdef DEBUG_RING_PTS_ADD
    dsyslog("cIndex::AddPTS(): packet (%6d) PTS %" PRId64, packetNumber, pts);
#endif
    if (!ptsRing.empty() && !rollover) {
        if ((ptsRing.back().pts > 0x200000000) && (pts < 0x100000000)) {  // PTS/DTS rollover, give some room for missing packets during rollover
            dsyslog("cIndex::AddPTS(): packet(%d): PTS/DTS rollover from PTS %" PRId64 " to PTS %" PRId64, packetNumber, ptsRing.back().pts, pts);
            rollover = true;
        }
    }
    // add new frame timestamp to vector
    sPTS_RingbufferElement newPTS;
    newPTS.packetNumber = packetNumber;
    newPTS.pts          = pts;
    newPTS.rollover     = pts;
    ptsRing.push_back(newPTS);
    ALLOC(sizeof(sPTS_RingbufferElement), "ptsRing");

    // delete oldest entry
    if (ptsRing.size() > MAX_PTSRING) {
        ptsRing.erase(ptsRing.begin());
        FREE(sizeof(sPTS_RingbufferElement), "ptsRing");
    }

}


void cIndex::AddPSlice(const int64_t pts) {
    if (!pSliceVector.empty() && (pts <= pSliceVector.back())) return;  // do not add in second pass detection
#ifdef DEBUG_INDEX
    dsyslog("cIndex::AddPSlice(): PTS %ld: is start of p-slice", pts);
#endif
    pSliceVector.push_back(pts);
    ALLOC(sizeof(pts), "pSliceVector");
}


int cIndex::GetPSliceKeyPacketNumberAfterPTS(const int64_t pts, int64_t *pSlicePTS) {
    if (pSliceVector.size() == 0) return -1;   // can happen if no full decoding is set
    for (std::vector<int64_t>::iterator sliceIterator = pSliceVector.begin(); sliceIterator != pSliceVector.end(); ++sliceIterator) {
        if (*sliceIterator >= pts) {
            const sIndexElement *indexElement = GetIndexElementFromPTS(*sliceIterator);
            if (indexElement && indexElement->isPTSinSlice) {
                dsyslog("cIndex::GetPSliceKeyPacketNumberAfterPTS(): packet (%d) PTS %" PRId64 ": found p-slice after after PTS %" PRId64, indexElement->packetNumber, pts, *sliceIterator);
                *pSlicePTS = *sliceIterator;
                return indexElement->packetNumber;
            }
        }
    }
    return -1;
}


int64_t cIndex::GetPTSFromPacketNumber(const int packetNumber) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPTSFromPacketNumber(): index is empty");
        return AV_NOPTS_VALUE;
    }
    std::vector<sPTS_RingbufferElement>::iterator found = std::find_if(ptsRing.begin(), ptsRing.end(), [packetNumber](sPTS_RingbufferElement const &value) ->bool { if (value.packetNumber == packetNumber) return true; else return false; });
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
    esyslog("cIndex::GetPTSFromPacketNumber(): packet %d: not found, index contains from (%d) to (%d)", packetNumber, ptsRing.front().packetNumber, ptsRing.back().packetNumber);
    return AV_NOPTS_VALUE;
}


#ifdef DEBUG_DECODER
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
            frame.frameNumber = ptsIterator->packetNumber;
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
#endif


// get nearest video packet before PTS
int cIndex::GetPacketNumberBeforePTS(const int64_t pts, int64_t *beforePTS) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPacketNumberBeforePTS(): index is empty");
        return -1;
    }
    int packetNumber   = -1;
    int64_t diffResult = INT64_MAX;
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
        int64_t diff = pts - ptsIterator->pts;
        if ((diff >= 0) && (diff < diffResult)) {
            packetNumber = ptsIterator->packetNumber;
            if (beforePTS) *beforePTS = ptsIterator->pts;
            diffResult   = diff;
            if (diff == 0) break;
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
    if (beforePTS) dsyslog("cIndex::GetPacketNumberBeforePTS(): found video packet (%d) PTS %" PRId64 " before PTS %" PRId64, packetNumber, *beforePTS, pts);
#endif
    return packetNumber;
}



int cIndex::GetPacketNumberAfterPTS(const int64_t pts, int64_t *afterPTS) {
    if (ptsRing.size() == 0) {
        esyslog("cIndex::GetPacketNumberAfterPTS(): index is empty");
        return -1;
    }
    int packetNumber   = -1;
    int64_t diffResult = INT64_MAX;
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
        int64_t diff = ptsIterator->pts - pts;
        if ((diff >= 0) && (diff < diffResult)) {
            packetNumber = ptsIterator->packetNumber;
            if (afterPTS) *afterPTS = ptsIterator->pts;
            diffResult   = diff;
            if (diff == 0) break;
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
    if (afterPTS) dsyslog("cIndex::GetPacketNumberAfterPTS(): found video packet (%d) PTS %" PRId64 " after PTS %" PRId64, packetNumber, *afterPTS, pts);
#endif
    return packetNumber;
}
