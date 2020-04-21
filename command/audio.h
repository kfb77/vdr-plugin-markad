/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include "global.h"

class cMarkAdAudio
{
private:
    MarkAdContext *macontext;

    MarkAdMark mark;
    void resetmark();
    void setmark(int type, int position, int channelsbefore, int channelsafter);
    short int channels[MAXSTREAMS] = {0};
    bool channelchange(int a, int b);
    int framelast;
public:
    cMarkAdAudio(MarkAdContext *maContext);
    ~cMarkAdAudio();
    MarkAdMark *Process(int FrameNumber, int FrameNumberBefore);
    void Clear();
};

#endif
