/*
 * overlap.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "overlap.h"
#include "debug.h"

// global variable
extern bool abortNow;


// --------------------------------------------------------------------------------------------------------------------------------
cOverlap::cOverlap(cDecoder *decoderParam, cIndex *indexParam) {
    decoder   = decoderParam;
    index     = indexParam;
}


cOverlap::~cOverlap() {
}


bool cOverlap::DetectOverlap(cMarks *marksParam) {
    if (abortNow)    return false;
    if (!decoder)    return false;
    if (!index)      return false;
    if (!marksParam) return false;
    marks = marksParam;

    if (marks->Count() < 4) {
        dsyslog("cOverlap::DetectOverlap(): not enough marks for overlap detection");
        return false;
    }

    if (!decoder->Restart()) return false;

    bool save = false;
    cMark *p1 = nullptr;
    cMark *p2 = nullptr;

    // start around first ad (2. and 3. mark)
    p1 = marks->GetFirst();
    if (!p1) return false;
    p1 = p1->Next();
    if (p1) p2 = p1->Next();

    while ((p1) && (p2)) {
        if (abortNow) return false;
        if (decoder) {
            LogSeparator(false);
            dsyslog("cOverlap::DetectOverlap(): check overlap before stop mark (%d) and after start mark (%d)", p1->position, p2->position);

            // init overlap detection around ad
            cOverlapAroundAd *overlapAroundAd = new cOverlapAroundAd(decoder);
            ALLOC(sizeof(*overlapAroundAd), "overlapAroundAd");

            // detect overlap before stop and after start
            if (!ProcessMarksOverlap(overlapAroundAd, &p1, &p2)) {
                if (p1 && p2) dsyslog("cOverlap::DetectOverlap(): no overlap found before stop mark (%d) and after start (%d)", p1->position, p2->position);
                else esyslog("cOverlap::DetectOverlap(): marks invalid after ProcessMarksOverlap()");
            }
            else save = true;

            // free overlap detection object
            FREE(sizeof(*overlapAroundAd), "overlapAroundAd");
            delete overlapAroundAd;
        }
        if (!p1 || !p2) break;  // failed move will return nullptr pointer
        p1 = p2->Next();
        if (p1) {
            p2 = p1->Next();
        }
        else {
            p2 = nullptr;
        }
    }
    return save;
}


bool cOverlap::ProcessMarksOverlap(cOverlapAroundAd *overlapAroundAd, cMark **mark1, cMark **mark2) {
    if (!decoder)   return false;
    if (!index)     return false;
    if (!marks)     return false;
    if (!mark1)     return false;
    if (!(*mark1))  return false;
    if (!mark2)     return false;
    if (!(*mark2))  return false;

    sOverlapPos overlapPos;
    overlapPos.similarBeforeStartPacketNumber = -1;
    overlapPos.similarBeforeEndPacketNumber   = -1;
    overlapPos.similarAfterStartPacketNumber  = -1;
    overlapPos.similarAfterEndPacketNumber    = -1;

    int frameRate = decoder->GetVideoFrameRate();

// max overlap found: 94200ms (TELE 5)
#define OVERLAP_CHECK_BEFORE 100  // start before stop mark, changed from 90 to 100
#define OVERLAP_CHECK_AFTER  100  // end after start mark,   changed from 90 to 100

    // calculate overlap check positions
    int fRangeBegin = (*mark1)->position - (frameRate * OVERLAP_CHECK_BEFORE);
    if (fRangeBegin < 0) fRangeBegin = 0;                    // not before beginning of broadcast
    fRangeBegin = index->GetKeyPacketNumberAfter(fRangeBegin);
    if (fRangeBegin < 0) {
        esyslog("cOverlap::ProcessMarksOverlap(): GetKeyPacketNumberAfter() failed for frame (%d)", fRangeBegin);
        return false;
    }
    int fRangeEnd = (*mark2)->position + (frameRate * OVERLAP_CHECK_AFTER);
    fRangeEnd = index->GetKeyPacketNumberBefore(fRangeEnd);
    if (fRangeEnd < 0) {
        esyslog("cOverlap::ProcessMarksOverlap(): GetKeyPacketNumberBefore() failed for frame (%d)", fRangeEnd);
        return false;
    }

    // check if search range is possible
    if (decoder->GetPacketNumber() > fRangeBegin) {
        dsyslog("cOverlap::ProcessMarksOverlap(): current framenumber (%d) greater then start frame (%d), set start to current frame", decoder->GetPacketNumber(), fRangeBegin);
        fRangeBegin =  decoder->GetPacketNumber();
    }

    // seek to start frame of overlap check
    char *indexToHMSF = marks->IndexToHMSF(fRangeBegin, AV_NOPTS_VALUE, false);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("cOverlap::ProcessMarksOverlap(): start check %ds before start mark (%d) from frame (%d) at %s", OVERLAP_CHECK_BEFORE, (*mark1)->position, fRangeBegin, indexToHMSF);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    dsyslog("cOverlap::ProcessMarksOverlap(): preload from frame       (%5d) to (%5d)", fRangeBegin, (*mark1)->position);
    dsyslog("cOverlap::ProcessMarksOverlap(): compare with frames from (%5d) to (%5d)", (*mark2)->position, fRangeEnd);
    if (decoder->GetPacketNumber() > fRangeBegin) {
        dsyslog("cOverlap::ProcessMarksOverlap(): current framenumber (%d) greater then start frame (%d), set start to current frame", decoder->GetPacketNumber(), fRangeBegin);
        fRangeBegin =  decoder->GetPacketNumber();
    }

    // seek to start frame
    if (!decoder->SeekToPacket(fRangeBegin)) {
        esyslog("could not seek to frame (%i)", fRangeBegin);
        return false;
    }

    // get frame count of range before stop mark to check for overlap
    int frameCount;
    if (decoder->GetFullDecode()) frameCount = (*mark1)->position - fRangeBegin + 1;
    else frameCount = index->GetIFrameRangeCount(fRangeBegin, (*mark1)->position);
    if (frameCount < 0) {
        dsyslog("cOverlap::ProcessMarksOverlap(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
        return false;
    }
    dsyslog("cOverlap::ProcessMarksOverlap(): %d frames to preload between start of check (%d) and stop mark (%d)", frameCount, fRangeBegin, (*mark1)->position);


    // preload frames before stop mark
    while (decoder->DecodeNextFrame(false) && (decoder->GetPacketNumber() <= (*mark1)->position)) {  // no audio
        if (abortNow) return false;

#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif

#ifdef DEBUG_OVERLAP_FRAME_RANGE
        if ((decoder->GetPacketNumber() > (DEBUG_OVERLAP_FRAME_BEFORE - DEBUG_OVERLAP_FRAME_RANGE)) &&
                (decoder->GetPacketNumber() < (DEBUG_OVERLAP_FRAME_BEFORE + DEBUG_OVERLAP_FRAME_RANGE))) SaveFrame(decoder->GetPacketNumber(), nullptr, nullptr);
#endif
        const sVideoPicture *picture = decoder->GetVideoPicture();
        if (!picture) continue;
        overlapAroundAd->Process(picture, frameCount, true, (decoder->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264));
    }

    // seek to iFrame before start mark
    fRangeBegin = index->GetKeyPacketNumberBefore((*mark2)->position);
    if (fRangeBegin <= 0) {
        dsyslog("cOverlap::ProcessMarksOverlap(): GetKeyPacketNumberBefore failed for frame (%d)", fRangeBegin);
        return false;
    }
    if (fRangeBegin <  decoder->GetPacketNumber()) fRangeBegin = decoder->GetPacketNumber(); // on very short stop/start pairs we have no room to go before start mark
    indexToHMSF = marks->IndexToHMSF(fRangeBegin, AV_NOPTS_VALUE, false);
    if (indexToHMSF) {
        ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
        dsyslog("cOverlap::ProcessMarksOverlap(): seek forward to frame (%d) at %s before start mark (%d) and start overlap check", fRangeBegin, indexToHMSF, (*mark2)->position);
        FREE(strlen(indexToHMSF)+1, "indexToHMSF");
        free(indexToHMSF);
    }
    if (!decoder->SeekToPacket(fRangeBegin)) {
        esyslog("could not seek to frame (%d)", fRangeBegin);
        return false;
    }

    if (decoder->GetFullDecode()) frameCount = fRangeEnd - fRangeBegin + 1;
    else frameCount = index->GetIFrameRangeCount(fRangeBegin, fRangeEnd) - 2;
    if (frameCount < 0) {
        dsyslog("cOverlap::ProcessMarksOverlap(): GetIFrameRangeCount failed at range (%d,%d))", fRangeBegin, (*mark1)->position);
        return false;
    }
    dsyslog("cOverlap::ProcessMarksOverlap(): %d frames to preload between start mark (%d) and  end of check (%d)", frameCount, (*mark2)->position, fRangeEnd);

    // process frames after start mark and detect overlap
    while (decoder->DecodeNextFrame(false) && (decoder->GetPacketNumber() <= fRangeEnd)) {
        if (abortNow) return false;

#ifdef DEBUG_OVERLAP
        dsyslog("------------------------------------------------------------------------------------------------");
#endif

#ifdef DEBUG_OVERLAP_FRAME_RANGE
        if ((decoder->GetPacketNumber() > (DEBUG_OVERLAP_FRAME_AFTER - DEBUG_OVERLAP_FRAME_RANGE)) &&
                (decoder->GetPacketNumber() < (DEBUG_OVERLAP_FRAME_AFTER + DEBUG_OVERLAP_FRAME_RANGE))) SaveFrame(decoder->GetPacketNumber(), nullptr, nullptr);
#endif

        const sVideoPicture *picture = decoder->GetVideoPicture();
        if (!picture) continue;
        overlapAroundAd->Process(picture, frameCount, false, (decoder->GetVideoType() == MARKAD_PIDTYPE_VIDEO_H264));
    }

    dsyslog("cOverlapAroundAd::ProcessMarksOverlap(): start compare frames");
    overlapAroundAd->Detect(&overlapPos);
#ifdef DEBUG_OVERLAP
    dsyslog("cOverlapAroundAd::ProcessMarksOverlap(): overlap from (%d) before stop mark and (%d) after start mark", overlapPos.similarBeforeEndPacketNumber, overlapPos.similarAfterEndPacketNumber);
#endif
    if (overlapPos.similarAfterEndPacketNumber >= 0) {
        // found overlap
        int lengthBefore = 1000 * (overlapPos.similarBeforeEndPacketNumber - overlapPos.similarBeforeStartPacketNumber + 1) / frameRate; // include first and last
        int lengthAfter  = 1000 * (overlapPos.similarAfterEndPacketNumber  - overlapPos.similarAfterStartPacketNumber  + 1) / frameRate;

        char *indexToHMSFbeforeStart = marks->IndexToHMSF(overlapPos.similarBeforeStartPacketNumber, overlapPos.similarBeforeStartPTS, false);
        if (indexToHMSFbeforeStart) {
            ALLOC(strlen(indexToHMSFbeforeStart)+1, "indexToHMSFbeforeStart");
        }

        char *indexToHMSFbeforeEnd   = marks->IndexToHMSF(overlapPos.similarBeforeEndPacketNumber, overlapPos.similarBeforeEndPTS, false);
        if (indexToHMSFbeforeEnd) {
            ALLOC(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSFbeforeEnd");
        }

        char *indexToHMSFafterStart  = marks->IndexToHMSF(overlapPos.similarAfterStartPacketNumber, overlapPos.similarAfterStartPTS, false);
        if (indexToHMSFafterStart) {
            ALLOC(strlen(indexToHMSFafterStart)+1, "indexToHMSFafterStart");
        }

        char *indexToHMSFafterEnd    = marks->IndexToHMSF(overlapPos.similarAfterEndPacketNumber, overlapPos.similarAfterEndPTS, false);
        if (indexToHMSFafterEnd) {
            ALLOC(strlen(indexToHMSFafterEnd)+1, "indexToHMSFafterEnd");
        }

        dsyslog("cOverlap::ProcessMarksOverlap(): similar from (%5d) PTS %" PRId64 " at %s to (%5d) PTS %" PRId64 " at %s, length %5dms", overlapPos.similarBeforeStartPacketNumber, overlapPos.similarBeforeStartPTS, indexToHMSFbeforeStart, overlapPos.similarBeforeEndPacketNumber, overlapPos.similarBeforeEndPTS, indexToHMSFbeforeEnd, lengthBefore);
        dsyslog("cOverlap::ProcessMarksOverlap():              (%5d) at %s to (%5d) at %s, length %5dms",     overlapPos.similarAfterStartPacketNumber,  indexToHMSFafterStart,  overlapPos.similarAfterEndPacketNumber,  indexToHMSFafterEnd, lengthAfter);
        dsyslog("cOverlap::ProcessMarksOverlap():              maximum deviation in overlap %6d", overlapPos.similarMax);
        if (overlapPos.similarEnd > 0) dsyslog("cOverlap::ProcessMarksOverlap():              next deviation after overlap %6d", overlapPos.similarEnd); // can be 0 if overlap ends at the mark

        const char *indexToHMSFmark1  = marks->GetTime(*mark1);
        const char *indexToHMSFmark2  = marks->GetTime(*mark2);

        int gapStop          = ((*mark1)->position - overlapPos.similarBeforeEndPacketNumber)   / frameRate;
        int lengthBeforeStop = ((*mark1)->position - overlapPos.similarBeforeStartPacketNumber) / frameRate;
        int gapStart         = (overlapPos.similarAfterStartPacketNumber - (*mark2)->position)  / frameRate;
        int lengthAfterStart = (overlapPos.similarAfterEndPacketNumber - (*mark2)->position)    / frameRate;

        if (indexToHMSFbeforeStart && indexToHMSFbeforeEnd && indexToHMSFafterStart && indexToHMSFafterEnd && indexToHMSFmark1 && indexToHMSFmark2) {
            dsyslog("cOverlap::ProcessMarksOverlap(): overlap from (%6d) at %s to (%6d) at %s, before stop mark gap %3ds length %3ds, are identical with",overlapPos.similarBeforeEndPacketNumber, indexToHMSFbeforeEnd, (*mark1)->position, indexToHMSFmark1, gapStop, lengthBeforeStop);
            dsyslog("cOverlap::ProcessMarksOverlap():              (%6d) at %s to (%6d) at %s, after start mark gap %3ds length %3ds", (*mark2)->position, indexToHMSFmark2, overlapPos.similarAfterEndPacketNumber, indexToHMSFafterEnd, gapStart, lengthAfterStart);
        }
        if (indexToHMSFbeforeStart) {
            FREE(strlen(indexToHMSFbeforeStart)+1, "indexToHMSFbeforeStart");
            free(indexToHMSFbeforeStart);
        }
        if (indexToHMSFbeforeEnd) {
            FREE(strlen(indexToHMSFbeforeEnd)+1, "indexToHMSFbeforeEnd");
            free(indexToHMSFbeforeEnd);
        }
        if (indexToHMSFafterStart) {
            FREE(strlen(indexToHMSFafterStart)+1, "indexToHMSFafterStart");
            free(indexToHMSFafterStart);
        }
        if (indexToHMSFafterEnd) {
            FREE(strlen(indexToHMSFafterEnd)+1, "indexToHMSFafterEnd");
            free(indexToHMSFafterEnd);
        }
        // check overlap gap
        int gapStartMax = 16;                // changed gapStart from 21 to 18 to 16
        if (gapStop > 0) gapStartMax = 14;   // smaller valid diff if we do not hit stop mark, if both are not 0, this can be a invalid overlap
        else if (((*mark2)->type == MT_LOGOSTART) && (lengthBefore >= 38080)) gapStartMax = 21;  // trust long overlaps, there can be info logo after logo start mark

        if (((*mark2)->type == MT_ASPECTSTART) || ((*mark2)->type == MT_VBORDERSTART)) gapStartMax = 7; // for strong marks we can check with a lower value
        dsyslog("cOverlap::ProcessMarksOverlap(): maximum valid gap after start mark: %ds", gapStartMax);
        if ((lengthBefore >= 46640) ||                            // very long overlaps should be valid
                ((gapStop < 23) && (gapStart == 0)) ||            // if we hit start mark, trust greater stop gap, maybe we have no correct stop mark, changed from 34 to 23
                ((gapStop < 15) && (gapStart <= gapStartMax))) {  // we can not detect all similars during a scene changes, changed from 27 to 15
            // but if it is too far away it is a false positiv
            // changed gapStop from 36 to 27
            dsyslog("cOverlap::ProcessMarksOverlap(): overlap gap to marks are valid, before stop mark %ds, after start mark %ds, length %dms", gapStop, gapStart, lengthBefore);
            *mark1 = marks->Move((*mark1), overlapPos.similarBeforeEndPacketNumber, overlapPos.similarBeforeEndPTS, MT_OVERLAPSTOP);
            if (!(*mark1)) {
                esyslog("cOverlap::ProcessMarksOverlap(): move stop mark failed");
                return false;
            }
            *mark2 = marks->Move((*mark2), overlapPos.similarAfterEndPacketNumber, overlapPos.similarAfterEndPTS, MT_OVERLAPSTART);
            if (!(*mark2)) {
                esyslog("cOverlap::ProcessMarksOverlap(): move start mark failed");
                return false;
            }
        }
        else dsyslog("cOverlap::ProcessMarksOverlap(): overlap gap to marks are not valid, before stop mark %ds, after start mark %ds, length %dms", gapStop, gapStart, lengthBefore);
        return true;
    }
    return false;
}





// --------------------------------------------------------------------------------------------------------------------------------

cOverlapAroundAd::cOverlapAroundAd(cDecoder *decoderParam) {
    decoder = decoderParam;
}


cOverlapAroundAd::~cOverlapAroundAd() {
#ifdef DEBUG_OVERLAP
    dsyslog("cOverlapAroundAd::~cOverlapAroundAd()(): delete object");
#endif
    if (histbuf[OV_BEFORE]) {
        FREE(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        delete[] histbuf[OV_BEFORE];
        histbuf[OV_BEFORE] = nullptr;
    }

    if (histbuf[OV_AFTER]) {
        FREE(sizeof(*histbuf[OV_AFTER]), "histbuf");
        delete[] histbuf[OV_AFTER];
        histbuf[OV_AFTER] = nullptr;
    }
}

void cOverlapAroundAd::Process(const sVideoPicture *picture, const int frameCount, const bool beforeAd, const bool h264) {
#ifdef DEBUG_OVERLAP
    dsyslog("cOverlapAroundAd::Process(): frameNumber %d, frameCount %d, beforeAd %d, isH264 %d", frameNumber, frameCount, beforeAd, h264);
#endif

    if ((lastFrameNumber > 0) && (similarMinLength == 0)) {
        similarCutOff = 49000;            // lower is harder
        // do not increase, we will get false positive
        if (h264) similarCutOff = 196000; // reduce false similar detection in H.264 streams
        similarMinLength = 4040;          // shortest valid length of an overlap with 4040ms found
    }

    if (beforeAd) {
#ifdef DEBUG_OVERLAP
        dsyslog("cOverlapAroundAd::Process(): preload histogram with frames before stop mark, frame index %d of %d", histcnt[OV_BEFORE], frameCount - 1);
#endif
        // alloc memory for frames before stop mark
        if (!histbuf[OV_BEFORE]) {
            histframes[OV_BEFORE] = frameCount;
            histbuf[OV_BEFORE] = new sHistBuffer[frameCount + 1];
            ALLOC(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        }
        // fill histogram for frames before stop mark
        if (histcnt[OV_BEFORE] >= frameCount) {
            dsyslog("cOverlapAroundAd::Process(): got more frames before stop mark than expected");
            return;
        }
        GetHistogram(picture, histbuf[OV_BEFORE][histcnt[OV_BEFORE]].histogram);
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].valid = true;
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].frameNumber = picture->packetNumber;
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].pts         = picture->pts;
        histcnt[OV_BEFORE]++;
    }
    else {
#ifdef DEBUG_OVERLAP
        dsyslog("cOverlapAroundAd::Process(): preload histogram with frames after start mark, frame index %d of %d", histcnt[OV_AFTER], frameCount - 1);
#endif
        // alloc memory for frames after start mark
        if (!histbuf[OV_AFTER]) {
            histframes[OV_AFTER] = frameCount;
            histbuf[OV_AFTER] = new sHistBuffer[frameCount + 1];
            ALLOC(sizeof(*histbuf[OV_AFTER]), "histbuf");
        }

        // fill histogram for frames before after start mark
        if (histcnt[OV_AFTER] >= frameCount) {
            dsyslog("cOverlapAroundAd::Process(): got more frames after start mark than expected");
            return;
        }
        GetHistogram(picture, histbuf[OV_AFTER][histcnt[OV_AFTER]].histogram);
        histbuf[OV_AFTER][histcnt[OV_AFTER]].valid = true;
        histbuf[OV_AFTER][histcnt[OV_AFTER]].frameNumber = picture->packetNumber;
        histbuf[OV_AFTER][histcnt[OV_AFTER]].pts         = picture->pts;
        histcnt[OV_AFTER]++;
    }
    lastFrameNumber = picture->packetNumber;
    return;
}


void cOverlapAroundAd::Detect(sOverlapPos *overlapPos) {
    if (!overlapPos) return;

    int startAfterMark             =  0;
    int simLength                  =  0;
    int simMax                     =  0;
    int tmpindexAfterStartMark     =  0;
    int tmpindexBeforeStopMark     =  0;
    int firstSimilarBeforeStopMark = -1;
    int firstSimilarAfterStartMark = -1;
    int range                      =  1;  // on a scene change we can miss the same picture
    int frameRate                  = decoder->GetVideoFrameRate();

    if (decoder->GetFullDecode()) range = 10;  // we need more range with full decoding

    for (int indexBeforeStopMark = 0; indexBeforeStopMark < histcnt[OV_BEFORE]; indexBeforeStopMark++) {
#ifdef DEBUG_OVERLAP
        dsyslog("cOverlapAroundAd::Detect(): -------------------------------------------------------------------------------------------------------------");
        dsyslog("cOverlapAroundAd::Detect(): testing frame (%5d) before stop mark, indexBeforeStopMark %d, against all frames after start mark", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark);
#endif

        if (startAfterMark == histcnt[OV_AFTER]) {  // we reached end of range after start mark, reset state and contine with next frame before stop mark
            startAfterMark = 0;
            simLength      = 0;
            simMax         = 0;
            continue;
        }

        // check if histogram buffer before stop mark is valid
        if (!histbuf[OV_BEFORE][indexBeforeStopMark].valid) {
            dsyslog("cOverlapAroundAd::Detect(): histogram of frame (%d) before stop mark not valid, continue with next frame", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber);
        }

        for (int indexAfterStartMark = startAfterMark; indexAfterStartMark < histcnt[OV_AFTER]; indexAfterStartMark++) {
            // check if histogram buffer after start mark is valid
            if (!histbuf[OV_AFTER][indexAfterStartMark].valid) {  // not valid, continue with next pair
                indexBeforeStopMark++;
                if (indexBeforeStopMark >= histcnt[OV_BEFORE]) break;
                continue;
            }

            // check if pair is similar
            int simil = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark].histogram);
            if ((simLength >= 1800) && (simil < 0)) {  // not similar, but if we had found at least a short similar part, check neighbour frames
                int similBefore = -1;
                int similAfter  = -1;
                for (int i = 1 ; i <= range; i++) {
                    if ((indexAfterStartMark - i) > 0) similBefore = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark - i].histogram);
                    if ((indexAfterStartMark + i) <  histcnt[OV_AFTER]) similAfter = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark + i].histogram);
                    if ((similBefore >= 0) || (similAfter >= 0)) break;
                }
                if ((similBefore < 0) && (similAfter < 0)) {  // we have reached end of a similar part
//                    tsyslog("cMarkAdOverlap::Detect(): end of similar from (%5d) to (%5d) and (%5d) to (%5d) length %5dms",  histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber, histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber, histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber, histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber, simLength);
//                    tsyslog("cMarkAdOverlap::Detect():                with similBefore %5d, simil %5d, similAfter %5d", similBefore, simil, similAfter);
                }
                if (similBefore > 0) simil = similBefore;
                if (similAfter  > 0) simil = similAfter;
            }

#ifdef DEBUG_OVERLAP
            if (simil >= 0) dsyslog("cOverlapAroundAd::Detect(): +++++     similar frame (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) length %2dms similarMaxCnt %2d)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simLength, similarMinLength);
#endif
            // found long enough overlap, store position

            if ((simLength >= similarMinLength) &&
                    ((histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber - histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber) >= (overlapPos->similarBeforeEndPacketNumber - overlapPos->similarBeforeStartPacketNumber))) { // new overlap is longer than current overlap
                overlapPos->similarBeforeStartPacketNumber = histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber;
                overlapPos->similarBeforeStartPTS          = histbuf[OV_BEFORE][firstSimilarBeforeStopMark].pts;
                overlapPos->similarBeforeEndPacketNumber   = histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber;
                overlapPos->similarBeforeEndPTS            = histbuf[OV_BEFORE][tmpindexBeforeStopMark].pts;
                overlapPos->similarAfterStartPacketNumber  = histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber;
                overlapPos->similarAfterStartPTS           = histbuf[OV_AFTER][firstSimilarAfterStartMark].pts;
                overlapPos->similarAfterEndPacketNumber    = histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber;
                overlapPos->similarAfterEndPTS             = histbuf[OV_AFTER][tmpindexAfterStartMark].pts;
                overlapPos->similarMax                     = simMax;
                if (simil < 0) overlapPos->similarEnd = -simil;
            }

            if (simil >= 0) {
                if (simLength == 0) {  // this is the first similar frame pair, store position
                    firstSimilarBeforeStopMark = indexBeforeStopMark;
                    firstSimilarAfterStartMark = indexAfterStartMark;
                }
                tmpindexAfterStartMark = indexAfterStartMark;
                tmpindexBeforeStopMark = indexBeforeStopMark;
                startAfterMark = indexAfterStartMark + 1;
                simLength = 1000 * (histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber - histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber + 1) / frameRate;
                if (simil > simMax) simMax = simil;

#ifdef DEBUG_OVERLAP
                dsyslog("cOverlapAroundAd::Detect(): similar picture index  from %d to %d and %d to %d", firstSimilarBeforeStopMark, indexBeforeStopMark, firstSimilarAfterStartMark, indexAfterStartMark);
                dsyslog("cOverlapAroundAd::Detect(): similar picture frames from (%d) to (%d) and (%d) to (%d), length %dms", histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber, histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, simLength);
#endif

                break;
            }
            else {
                // reset to first similar frame
                if (simLength > 0) {
#ifdef DEBUG_OVERLAP
                    dsyslog("cOverlapAroundAd::Detect(): ---- not similar frame (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) length %2dms similarMaxCnt %2d)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simLength, similarMinLength);
                    dsyslog("cOverlapAroundAd::Detect(): ===================================================================================================================");
#endif
                    indexBeforeStopMark = firstSimilarBeforeStopMark;  // reset to first similar frame
                }

                if (simLength < similarMinLength) startAfterMark = 0;
                simLength = 0;
                simMax    = 0;
            }
        }
#ifdef DEBUG_OVERLAP
        dsyslog("cOverlapAroundAd::Detect(): current overlap from (%d) to (%d) and (%d) to (%d)", overlapPos->similarBeforeStart, overlapPos->similarBeforeEnd, overlapPos->similarAfterStart, overlapPos->similarAfterEnd);
#endif
    }
    return;
}


void cOverlapAroundAd::GetHistogram(const sVideoPicture *picture, simpleHistogram &dest) const {
    memset(dest, 0, sizeof(simpleHistogram));
    int videoHeight = decoder->GetVideoHeight();
    int videoWidth  = decoder->GetVideoWidth();

    int startY = videoHeight * 0.22; // ignore top part because there can be info border at start after the advertising, changed from 0.2 to 0.22
    int endY   = videoHeight * 0.82; // ignore bottom part because there can info border text at start after the advertising, changed from 0.87 to 0.82

    for (int Y = startY; Y < endY; Y++) {
        for (int X = 0; X < videoWidth; X++) {
            uchar val = picture->plane[0][X + (Y * picture->planeLineSize[0])];
            dest[val]++;
        }
    }
}


int cOverlapAroundAd::AreSimilar(const simpleHistogram &hist1, const simpleHistogram &hist2) const { // return > 0 if similar, else <= 0
    long int similar = 0;  // prevent integer overflow
    for (int i = 0; i < 256; i++) {
        similar += abs(hist1[i] - hist2[i]);  // calculte difference, smaller is more similar
    }
    if (similar > INT_MAX) similar = INT_MAX;  // we do need more
    if (similar < similarCutOff) {
        return similar;
    }
    return -similar;
}
