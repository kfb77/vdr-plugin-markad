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
    channels[MAXSTREAMS-1] = {0};
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


bool cAudio::ChannelChange(int channelsBefore, int channelsAfter) {
    if ((channelsBefore == 0) || (channelsAfter == 0)) return false;  // no channel count information
    if ((channelsBefore != 2) && (channelsBefore != 6)) {  // invalid status of channel count
        dsyslog("cAudio::ChannelChange(): invald status of channel count: %d", channelsBefore);
        return false;
    }
    if ((channelsAfter  != 2) && (channelsAfter  != 6)) { // invalid channel count in stream, maybe malformed audio packet
        dsyslog("cAudio::ChannelChange(): ignoring unexpected channel count %d in audio stream", channelsAfter);
        return false;
    }
    if (channelsBefore != channelsAfter) return true;
    return false;
}


void cAudio::Silence() {
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
//                silencePTS    = macontext->Audio.Info.PTS;  TODO: get PTS from decoder
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
//                soundPTS      = macontext->Audio.Info.PTS; TODO: get from decoder
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
}


sMarkAdMarks *cAudio::Process() {
    ResetMarks();
    Silence();  // check volume

    // check channel change  TODO get from decoder
    /*
    for (short int stream = 0; stream < MAXSTREAMS; stream++) {
        if (((macontext->Audio.Info.Channels[stream] == 2) || (macontext->Audio.Info.Channels[stream] == 6)) && (channels[stream] == 0)) {
            channels[stream] = macontext->Audio.Info.Channels[stream];
            dsyslog("cAudio::Process(): audio stream %d start at frame (%d) with %d channels", stream, macontext->Audio.Info.channelChangeFrame, channels[stream]);
        }
        if (ChannelChange(channels[stream], macontext->Audio.Info.Channels[stream])) {
            if (macontext->Audio.Info.Channels[stream] > 2) {  // channel start
                int markFrame = recordingIndexAudio->GetVideoFrameToPTS(macontext->Audio.Info.channelChangePTS, false); // get video frame with pts after channel change
                if (markFrame < 0) {
                    esyslog("cAudio::Process(): no video frame found after audio PTS %" PRId64, macontext->Audio.Info.channelChangePTS);
                    markFrame = macontext->Audio.Info.channelChangeFrame;
                }
                if (!macontext->Config->fullDecode) {
                    markFrame = recordingIndexAudio->GetIFrameAfter(markFrame);  // we need next iFrame for start cut, make sure we will not have last pic of ad
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                }
                AddMark(MT_CHANNELSTART, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
            }
            else { // channel stop, frame before is last frame in broadcast
                int markFrame = recordingIndexAudio->GetVideoFrameToPTS(macontext->Audio.Info.channelChangePTS, false); // get video frame with pts after channel change
                if (markFrame < 0) {
                    esyslog("cAudio::Process(): no video frame found after audio PTS %" PRId64, macontext->Audio.Info.channelChangePTS);
                    markFrame = macontext->Audio.Info.channelChangeFrame;
                }
                if (!macontext->Config->fullDecode) {
                    markFrame = recordingIndexAudio->GetIFrameBefore(markFrame);  // we need iFrame before for start cut, make sure we will not have first pic of ad
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                }
                AddMark(MT_CHANNELSTOP, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
            }
            channels[stream] = macontext->Audio.Info.Channels[stream];   // ignore invalid channel changes
        }
    }
    */
    // return list of new marks
    if (audioMarks.Count > 0) {
        return &audioMarks;
    }
    else {
        return nullptr;
    }
}
