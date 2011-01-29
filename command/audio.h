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
    void ResetMark();
    bool AddMark(int Type, int Position, const char *Comment);

    int channels;
    bool ChannelChange(int a, int b);
    int framelast;

    MarkAdPos result;
public:
    cMarkAdAudio(MarkAdContext *maContext);
    ~cMarkAdAudio();
    MarkAdMark *Process(int FrameNumber, int FrameNumberBefore);
    void Clear();
};

#endif
