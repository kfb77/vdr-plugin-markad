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
extern "C"{
#include "debug.h"
}


cMarkAdAudio::cMarkAdAudio(sMarkAdContext *maContext, cIndex *recordingIndex) {
    macontext           = maContext;
    recordingIndexAudio = recordingIndex;
    Clear();
    ResetMarks();
}


cMarkAdAudio::~cMarkAdAudio() {
    ResetMarks();
    Clear();
}


void cMarkAdAudio::Clear() {
    silencePTS             = -1;
    silenceFrame           = -1;
    soundPTS               = -1;
    retry                  =  0;
    channels[MAXSTREAMS-1] = {0};
}


void cMarkAdAudio::ResetMarks() {
    audioMarks = {};
}


void cMarkAdAudio::AddMark(int type, int position, const int channelsBefore, const int channelsAfter) {
    if (audioMarks.Count >= audioMarks.maxCount) {  // array start with 0
        esyslog("cMarkAdAudio::AddMark(): too much audio marks %d at once detected", audioMarks.Count);
        return;
    }
    audioMarks.Number[audioMarks.Count].position       = position;
    audioMarks.Number[audioMarks.Count].type           = type;
    audioMarks.Number[audioMarks.Count].channelsBefore = channelsBefore;
    audioMarks.Number[audioMarks.Count].channelsAfter  = channelsAfter;
    audioMarks.Count++;
}


bool cMarkAdAudio::ChannelChange(int channelsBefore, int channelsAfter) {
    if ((channelsBefore == 0) || (channelsAfter == 0)) return false;  // no channel count informations
    if ((channelsBefore != 2) && (channelsBefore != 6)) return false; // invalid channel count, maybe malformed audio packet
    if ((channelsAfter  != 2) && (channelsAfter  != 6)) return false; // invalid channel count, maybe malformed audio packet
    if (channelsBefore != channelsAfter) return true;
    return false;
}


void cMarkAdAudio::Silence(__attribute__((unused)) const int frameNumber) {
    if (macontext->Audio.Info.volume >= 0) {
#ifdef DEBUG_VOLUME
        dsyslog("cMarkAdAudio::Silence(): frame (%5d): macontext->Audio.Info.volume %4d, silenceFrame (%5d), silenceStatus %d", frameNumber, macontext->Audio.Info.volume, silenceFrame, silenceStatus);
#endif
#define MAX_VOLUME 15
        switch (silenceStatus) {
            case SILENCE_UNINITIALIZED:
                if (macontext->Audio.Info.volume <= MAX_VOLUME) silenceStatus = SILENCE_TRUE;
                else                                            silenceStatus = SILENCE_FALSE;
                break;
            case SILENCE_FALSE:
                if (macontext->Audio.Info.volume <= MAX_VOLUME) {
                    silenceStatus = SILENCE_TRUE;
                    silencePTS    = macontext->Audio.Info.PTS;
                    silenceFrame  = recordingIndexAudio->GetVideoFrameToPTS(silencePTS, true); // get video frame from PTS before audio PTS
                    if (silenceFrame < 0) esyslog("cMarkAdAudio::Silence(): no video frame found before audio PTS %" PRId64, silencePTS);
                }
                else {
                    silencePTS   = -1;
                    silenceFrame = -1;
                }
                break;
            case SILENCE_TRUE:
                if (macontext->Audio.Info.volume > MAX_VOLUME) {  // end of silence
                    silenceStatus = SILENCE_FALSE;
                    soundPTS      = macontext->Audio.Info.PTS;
               }
                break;
        }
    }
    // sometimes audio frame PTS is before video frame PTS in stream
    if (retry >= 10) { // give up
        esyslog("cMarkAdAudio::Silence(): no video frame found after audio PTS %" PRId64, soundPTS);
        silencePTS   = -1;
        silenceFrame = -1;
        soundPTS     = -1;
        retry        =  0;
    }
    else {
        if ((silencePTS >= 0) && (soundPTS >= 0)) {
            int soundFrame = recordingIndexAudio->GetVideoFrameToPTS(soundPTS, false); // get video frame with pts after audio frame
            if (soundFrame >= 0) {
                if ((silenceFrame >= 0) && (silenceFrame < soundFrame)) { // add both marks only if they have different frame numbers
                    AddMark(MT_SOUNDSTOP,  silenceFrame,  0, 0);
                    AddMark(MT_SOUNDSTART, soundFrame  , 0, 0);
                }
                silencePTS   = -1;
                silenceFrame = -1;
                soundPTS     = -1;
                retry        =  0;
            }
            else retry++;
        }
    }
}


sMarkAdMarks *cMarkAdAudio::Process(const int frameNumber) {
    ResetMarks();
    Silence(frameNumber);  // check volume

    // check channel change
    for (short int stream = 0; stream < MAXSTREAMS; stream++){
        if ((macontext->Audio.Info.Channels[stream] != 0) && (channels[stream] == 0)) dsyslog("cMarkAdAudio::ChannelChange(): new audio stream %d start at frame (%d)", stream, macontext->Audio.Info.channelChangeFrame);
        if (ChannelChange(macontext->Audio.Info.Channels[stream], channels[stream])) {
            // we accept to cut out a little audio, we do not want to have a frame from ad
            if (macontext->Audio.Info.Channels[stream] > 2) {
                if (macontext->Config->fullDecode) {
                    int markFrame = recordingIndexAudio->GetVideoFrameToPTS(macontext->Audio.Info.channelChangePTS); // get video frame with pts before channel change
                    if (markFrame < 0) {
                        esyslog("cMarkAdAudio::Process(): no video frame found after audio PTS %" PRId64, macontext->Audio.Info.channelChangePTS);
                        markFrame = macontext->Audio.Info.channelChangeFrame;
                    }
                    markFrame = recordingIndexAudio->GetIFrameAfter(markFrame);  // we need next iFrame for start cut, make sure we will not have last pic of ad
                    dsyslog("*cMarkAdAudio::Process(): next i-frame (%d)", markFrame);
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                    AddMark(MT_CHANNELSTART, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
                else { // audio streams are alway full decoded, use next video iFrame
                    int markFrame = recordingIndexAudio->GetIFrameAfter(macontext->Audio.Info.channelChangeFrame);  // we need next iFrame for start cut, make sure we will not have last pic of ad
                    dsyslog("*cMarkAdAudio::Process(): next i-frame (%d)", markFrame);
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                    AddMark(MT_CHANNELSTART, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
            }
            else { // frame before is last frame in broadcast
                if (macontext->Config->fullDecode) {
                    int markFrame = recordingIndexAudio->GetVideoFrameToPTS(macontext->Audio.Info.channelChangePTS);
                    if (markFrame < 0) {
                        esyslog("cMarkAdAudio::Process(): no video frame found after audio PTS %" PRId64, macontext->Audio.Info.channelChangePTS);
                        markFrame = macontext->Audio.Info.channelChangeFrame;
                    }
                    AddMark(MT_CHANNELSTOP, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
                else {
                    int markFrame = recordingIndexAudio->GetIFrameBefore(macontext->Audio.Info.channelChangeFrame);
                    dsyslog("*cMarkAdAudio::Process(): previous i-frame (%d)", markFrame);
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                    AddMark(MT_CHANNELSTOP, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
            }
        }
        channels[stream] = macontext->Audio.Info.Channels[stream];
    }

    // return list of new marks
    if (audioMarks.Count > 0) {
        return &audioMarks;
    }
    else {
        return NULL;
    }
}
