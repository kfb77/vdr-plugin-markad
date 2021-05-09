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
    macontext = maContext;
    recordingIndexAudio = recordingIndex;
    mark.position = 0;
    mark.type = 0;
    Clear();
}


cMarkAdAudio::~cMarkAdAudio() {
    ResetMark();
    Clear();
}


void cMarkAdAudio::Clear() {
    channels[MAXSTREAMS-1] = {0};
}


void cMarkAdAudio::ResetMark() {
    if (!mark.type) return;
    mark = {};
}


void cMarkAdAudio::SetMark(const int type, const int position, const int channelsBefore, const int channelsAfter) {
    mark.channelsBefore = channelsBefore;
    mark.channelsAfter = channelsAfter;
    mark.position = position;
    mark.type = type;
}


bool cMarkAdAudio::ChannelChange(int channelsBefore, int channelsAfter) {
    if ((channelsBefore == 0) || (channelsAfter == 0)) return false;
    if (channelsBefore != channelsAfter) return true;
    return false;
}


sMarkAdMark *cMarkAdAudio::Process() {
    ResetMark();
    for (short int stream = 0; stream < MAXSTREAMS; stream++){
        if (ChannelChange(macontext->Audio.Info.Channels[stream], channels[stream])) {
            // we accept to cut out a little audio, we do not want to have a frame from ad
            if (macontext->Audio.Info.Channels[stream] > 2) {
                if (macontext->Config->fullDecode) {
                    int markFrame = recordingIndexAudio->GetFirstVideoFrameAfterPTS(macontext->Audio.Info.channelChangePTS); // get video frame with pts before channel change
		    markFrame = recordingIndexAudio->GetIFrameAfter(markFrame);  // we need next iFrame for start cut, make sure we will not have last pic of ad
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                    SetMark(MT_CHANNELSTART, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
                else { // audio streams are alway full decoded, use next video iFrame
                    SetMark(MT_CHANNELSTART, recordingIndexAudio->GetIFrameAfter(macontext->Audio.Info.channelChangeFrame), channels[stream], macontext->Audio.Info.Channels[stream]);
                }
            }
            else { // frame before is last frame in broadcast
                if (macontext->Config->fullDecode) {
                    int markFrame = recordingIndexAudio->GetFirstVideoFrameAfterPTS(macontext->Audio.Info.channelChangePTS);
                    if (markFrame < 0) markFrame = macontext->Audio.Info.channelChangeFrame;
                    SetMark(MT_CHANNELSTOP, markFrame, channels[stream], macontext->Audio.Info.Channels[stream]);
                }
                else SetMark(MT_CHANNELSTOP, recordingIndexAudio->GetIFrameBefore(macontext->Audio.Info.channelChangeFrame), channels[stream], macontext->Audio.Info.Channels[stream]);
            }
        }
        channels[stream] = macontext->Audio.Info.Channels[stream];
    }
    if (mark.position) {
        return &mark;
    }
    else {
        return NULL;
    }
}
