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


void cAudio::AddMark(int type, int position, const int channelsBefore, const int channelsAfter) {
    if (audioMarks.Count >= audioMarks.maxCount) {  // array start with 0
        esyslog("cAudio::AddMark(): too much audio marks %d at once detected", audioMarks.Count);
        return;
    }
    audioMarks.Number[audioMarks.Count].position       = position;
    audioMarks.Number[audioMarks.Count].type           = type;
    audioMarks.Number[audioMarks.Count].channelsBefore = channelsBefore;
    audioMarks.Number[audioMarks.Count].channelsAfter  = channelsAfter;
    audioMarks.Count++;
}


void cAudio::Silence() {
    int normVolume = decoder->GetVolume();

    if (normVolume >= 0) {
        // set start/end
        if ((normVolume == 0) && (audioMP2Silence.startPTS < 0)) audioMP2Silence.startPTS = decoder->GetPacketPTS();                                  // start of silence
        if ((normVolume > 2) && (audioMP2Silence.startPTS >= 0) && (audioMP2Silence.stopPTS < 0)) audioMP2Silence.stopPTS = decoder->GetPacketPTS();  // end of silence

        // get frame number
        if ((audioMP2Silence.startPTS >= 0) && (audioMP2Silence.startFrameNumber < 0)) audioMP2Silence.startFrameNumber = index->GetFrameBeforePTS(audioMP2Silence.startPTS);
        if ((audioMP2Silence.stopPTS  >= 0) && (audioMP2Silence.stopFrameNumber  < 0)) audioMP2Silence.stopFrameNumber = index->GetFrameAfterPTS(audioMP2Silence.stopPTS);
        // return result
        if ((audioMP2Silence.startFrameNumber >= 0) && (audioMP2Silence.stopFrameNumber >= 0)) {  // silence ready, can be processed

#ifdef DEBUG_VOLUME
            if (normVolume == 0) dsyslog("cAudio::Silence(): silence detected");
            dsyslog("cAudio::Silence(): packet (%d), frame (%d): startPTS %ld, startFrameNumber %d, stopPTS %ld, stopFrameNumber %d", decoder->GetPacketNumber(), decoder->GetFrameNumber(), audioMP2Silence.startPTS, audioMP2Silence.startFrameNumber, audioMP2Silence.stopPTS, audioMP2Silence.stopFrameNumber);
#endif
            // add marks
            AddMark(MT_SOUNDSTOP,  audioMP2Silence.startFrameNumber, 0, 0);
            AddMark(MT_SOUNDSTART, audioMP2Silence.stopFrameNumber,  0, 0);
            // reset all values
            audioMP2Silence.startPTS         = -1;
            audioMP2Silence.stopPTS          = -1;
            audioMP2Silence.startFrameNumber = -1;
            audioMP2Silence.stopFrameNumber  = -1;
        }
    }
    return;
}


void cAudio::ChannelChange() {
    // check channel change
    sAudioAC3Channels *channelChange = decoder->GetChannelChange();   // check channel change
    if (channelChange) {   // we have unprocessed channel change
        dsyslog("cAudio::ChannelChange(): frame (%d): AC3 audio stream changed channel from %d to %d", channelChange->videoFrameNumber, channelChange->channelCountBefore, channelChange->channelCountAfter);

        if (channelChange->channelCountAfter == 2) AddMark(MT_CHANNELSTOP, channelChange->videoFrameNumber, channelChange->channelCountBefore, channelChange->channelCountAfter);
        else if ((channelChange->channelCountAfter == 5) || (channelChange->channelCountAfter == 6)) AddMark(MT_CHANNELSTART, channelChange->videoFrameNumber, channelChange->channelCountBefore, channelChange->channelCountAfter);
        else esyslog("cAudio::Process(): invalid channel count %d", channelChange->channelCountAfter);
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
