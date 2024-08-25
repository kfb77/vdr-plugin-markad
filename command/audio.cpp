/*
 * audio.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"


cAudio::cAudio(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam) {
    decoder  = decoderParam;
    index    = indexParam;
    criteria = criteriaParam;
}


cAudio::~cAudio() {
    ResetMarks();
}


void cAudio::ResetMarks() {
    audioMarks = {};
}


void cAudio::AddMark(int type, const int packetNumber, const int64_t framePTS, const int channelsBefore, const int channelsAfter) {
    if (audioMarks.Count >= audioMarks.maxCount) {  // array start with 0
        esyslog("cAudio::AddMark(): too much audio marks %d at once detected", audioMarks.Count);
        return;
    }
    audioMarks.Number[audioMarks.Count].packetNumber   = packetNumber;
    audioMarks.Number[audioMarks.Count].framePTS       = framePTS;
    audioMarks.Number[audioMarks.Count].type           = type;
    audioMarks.Number[audioMarks.Count].channelsBefore = channelsBefore;
    audioMarks.Number[audioMarks.Count].channelsAfter  = channelsAfter;
    audioMarks.Count++;
}


void cAudio::Silence() {
    int normVolume = decoder->GetVolume();

    if (normVolume >= 0) {  // valid deteced volume
        // set start/end, with audio packets there is packet PTS the same as frame PTS
        if ((normVolume == 0) && (audioMP2Silence.startFramePTS <  0)) {
            audioMP2Silence.startFramePTS = decoder->GetPacketPTS();   // start of silence
#ifdef DEBUG_VOLUME
            dsyslog("cAudio::Silence(): packet (%d): start silence at PTS %ld detected", decoder->GetPacketNumber(), audioMP2Silence.startFramePTS);
#endif
        }
        if ((normVolume >  2) && (audioMP2Silence.startFramePTS >= 0) && (audioMP2Silence.stopFramePTS < 0)) {
            audioMP2Silence.stopFramePTS = decoder->GetPacketPTS();  // end of silence
#ifdef DEBUG_VOLUME
            dsyslog("cAudio::Silence(): packet (%d): stop silence at PTS %ld detected", decoder->GetPacketNumber(), audioMP2Silence.stopFramePTS);
#endif
        }
        // get frame number
        if ((audioMP2Silence.startFramePTS >= 0) && (audioMP2Silence.startPacketNumber < 0)) audioMP2Silence.startPacketNumber = index->GetPacketNumberBeforePTS(audioMP2Silence.startFramePTS);
        if ((audioMP2Silence.stopFramePTS  >= 0) && (audioMP2Silence.stopPacketNumber  < 0)) audioMP2Silence.stopPacketNumber = index->GetPacketNumberAfterPTS(audioMP2Silence.stopFramePTS);
        // return result
        if ((audioMP2Silence.startPacketNumber >= 0) && (audioMP2Silence.stopPacketNumber >= 0)) {  // silence ready, can be processed
            // very short silence can result in reversed start/stop video packet numbers because they have no monotonous increasing PTS
            if (audioMP2Silence.startPacketNumber > audioMP2Silence.stopPacketNumber) {
                audioMP2Silence.startPacketNumber--;
                audioMP2Silence.stopPacketNumber = audioMP2Silence.startPacketNumber + 1;
            }

#ifdef DEBUG_VOLUME
            dsyslog("cAudio::Silence(): packet (%d): startPTS %ld, startPacketNumber %d, stopPTS %ld, stopPacketNumber %d", decoder->GetPacketNumber(), audioMP2Silence.startFramePTS, audioMP2Silence.startPacketNumber, audioMP2Silence.stopFramePTS, audioMP2Silence.stopPacketNumber);
#endif
            // add marks
            AddMark(MT_SOUNDSTOP,  audioMP2Silence.startPacketNumber, audioMP2Silence.startFramePTS, 0, 0);
            AddMark(MT_SOUNDSTART, audioMP2Silence.stopPacketNumber,  audioMP2Silence.stopFramePTS,  0, 0);
            // reset all values
            audioMP2Silence.startPacketNumber = -1;
            audioMP2Silence.startFramePTS     = -1;
            audioMP2Silence.stopPacketNumber  = -1;
            audioMP2Silence.stopFramePTS      = -1;
        }
    }
    return;
}


void cAudio::ChannelChange() {
    // check channel change
    sAudioAC3Channels *channelChange = decoder->GetChannelChange();   // check channel change
    if (channelChange) {   // we have unprocessed channel change
        dsyslog("cAudio::ChannelChange(): packet (%d) PTS %ld: AC3 audio stream changed channel from %d to %d", channelChange->videoPacketNumber, channelChange->videoFramePTS, channelChange->channelCountBefore, channelChange->channelCountAfter);

        if (channelChange->channelCountAfter == 2) AddMark(MT_CHANNELSTOP, channelChange->videoPacketNumber, channelChange->videoFramePTS, channelChange->channelCountBefore, channelChange->channelCountAfter);
        else if ((channelChange->channelCountBefore == 2) &&   // ignore channel change from 5 to 6 or from 6 to 5
                 ((channelChange->channelCountAfter == 5) || (channelChange->channelCountAfter == 6))) AddMark(MT_CHANNELSTART, channelChange->videoPacketNumber, channelChange->videoFramePTS, channelChange->channelCountBefore, channelChange->channelCountAfter);
        else dsyslog("cAudio::Process(): ignore channel count change from %d to %d", channelChange->channelCountBefore, channelChange->channelCountAfter);
        channelChange->channelCountBefore = channelChange->channelCountAfter;
        channelChange->processed          = true;
    }
}


sMarkAdMarks *cAudio::Detect() {
    ResetMarks();

    // do audio based checks
    if (criteria->GetDetectionState(MT_CHANNELCHANGE)) ChannelChange();
    if (criteria->GetDetectionState(MT_SOUNDCHANGE))   Silence();

    // return list of new marks
    if (audioMarks.Count > 0) {
        return &audioMarks;
    }
    else {
        return nullptr;
    }

}
