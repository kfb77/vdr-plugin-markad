/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include "global.h"


class cMarkAdAudio {
    private:
        MarkAdContext *macontext;
        MarkAdMark mark;
        int framelast;
        short int channels[MAXSTREAMS] = {0};

        void resetmark();
        void setmark(int type, int position, int channelsbefore, int channelsafter);
        bool channelchange(int a, int b);
    public:
        explicit cMarkAdAudio(MarkAdContext *maContext);
        ~cMarkAdAudio();
        MarkAdMark *Process(int FrameNumber, int FrameNumberBefore);
        void Clear();
};
#endif
