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


cAudio::cAudio(cDecoderNEW *decoderParam, cCriteria *criteriaParam) {
    decoder       = decoderParam;
    criteriaParam = criteria;
    Clear();
    ResetMarks();
}


cAudio::~cAudio() {
    ResetMarks();
    Clear();
}


void cAudio::Clear() {
    silencePTS             = -1;
    silenceFrame           = -1;
    soundPTS               = -1;
    retry                  =  0;
//    channels[MAXSTREAMS-1] = {0};
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


sMarkAdMarks *cAudio::Silence() {
    return nullptr;
    /*
    int volume = 100; // TODO: get audio volume from decoder
    if (volume >= 0) {
    #ifdef DEBUG_VOLUME
        int frameNumber = decoder->GetVideoFrameNumber();
        dsyslog("cAudio::Silence(): frame (%5d): volume %4d, silenceFrame (%5d), silenceStatus %2d, hasZero %d", frameNumber, volume, silenceFrame, silenceStatus, hasZero);
    #endif
        switch (silenceStatus) {
        case SILENCE_UNINITIALIZED:
            if (volume <= MAX_SILENCE_VOLUME) silenceStatus = SILENCE_TRUE;
            else                                                    silenceStatus = SILENCE_FALSE;
            break;
        case SILENCE_FALSE:
            if (volume <= MAX_SILENCE_VOLUME) {
                silenceStatus = SILENCE_TRUE;
                silencePTS    = macontext->Audio.Info.PTS;  TODO: get PTS from decoder
                silenceFrame  = recordingIndexAudio->GetVideoFrameToPTS(silencePTS, true); // get video frame from PTS before audio PTS
                if (silenceFrame < 0) esyslog("cAudio::Silence(): no video frame found before audio PTS %" PRId64, silencePTS);
            }
            else {
                silencePTS   = -1;
                silenceFrame = -1;
            }
            break;
        case SILENCE_TRUE:
            if (volume > MAX_SILENCE_VOLUME) {  // end of silence
                silenceStatus = SILENCE_FALSE;
                soundPTS      = macontext->Audio.Info.PTS; TODO: get from decoder
            }
            break;
        }
        if ((silenceStatus == SILENCE_TRUE) && (volume == 0)) hasZero = true;
    }
    // sometimes audio frame PTS is before video frame PTS in stream
    if (retry >= 10) { // give up
        esyslog("cAudio::Silence(): no video frame found after audio PTS %" PRId64, soundPTS);
        silencePTS   = -1;
        silenceFrame = -1;
        soundPTS     = -1;
        retry        =  0;
        hasZero      = false;
    }
    else {
        if ((silencePTS >= 0) && (soundPTS >= 0)) {
            int soundFrame = recordingIndexAudio->GetVideoFrameToPTS(soundPTS, false); // get video frame with pts after audio frame
            if (soundFrame >= 0) {
                if (hasZero && (silenceFrame >= 0) && (silenceFrame < soundFrame)) { // add both marks only if silence part is at least 1 video frame long
                    AddMark(MT_SOUNDSTOP,  silenceFrame,  0, 0);
                    AddMark(MT_SOUNDSTART, soundFrame,  0, 0);
                }
                silencePTS   = -1;
                silenceFrame = -1;
                soundPTS     = -1;
                retry        =  0;
                hasZero      = false;
            }
            else retry++;
        }
    }
    */
}


sMarkAdMarks *cAudio::ChannelChange() {
    ResetMarks();
    Silence();  // check volume

    // check channel change
    sAudioAC3 *audioAC3 = decoder->GetChannelChange();   // check channel change
    if (audioAC3) {   // we have unprocessed channel change
        switch (audioAC3->channelCount) {
        case 2:  // channel stop
            AddMark(MT_CHANNELSTOP, audioAC3->videoFrameNumber, 6, 2);
            break;
        case 6:   // channel start
            AddMark(MT_CHANNELSTART,  audioAC3->videoFrameNumber, 2, 6);
            break;
        default:
            esyslog("cAudio::Process(): invalid channel count %d", audioAC3->channelCount);
        }
        audioAC3->processed = true;
    }
    // return list of new marks
    if (audioMarks.Count > 0) {
        return &audioMarks;
    }
    else {
        return nullptr;
    }
}
