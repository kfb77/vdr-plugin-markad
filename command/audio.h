/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include "global.h"
#include "index.h"


class cMarkAdAudio {
    private:
        MarkAdContext *macontext;
        cIndex *recordingIndexAudio = NULL;
        MarkAdMark mark;
        short int channels[MAXSTREAMS] = {0};

        void ResetMark();
        void SetMark(const int type, const int position, const int channelsbefore, const int channelsafter);
        bool ChannelChange(int a, int b);
    public:
        explicit cMarkAdAudio(MarkAdContext *maContext, cIndex *recordingIndex);
        ~cMarkAdAudio();
        MarkAdMark *Process();
        void Clear();
};
#endif
