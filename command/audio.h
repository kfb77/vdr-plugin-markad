/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include "global.h"

#include "audio_gain_analysis.h"

class cMarkAdAudio
{
private:
    //int framenumber;
    MarkAdContext *macontext;

    MarkAdMark mark;
    void ResetMark();
    bool AddMark(int Type, int Position, const char *Comment);

#define CUT_VAL 4
#define MIN_LOWVALS 25
    bool SilenceDetection(int FrameNumber);
    int lastframe_silence;

    int lastframe_gain;
    double lastgain;
    cMarkAdAudioGainAnalysis audiogain;
    bool AnalyzeGain(int FrameNumber);

    int channels;
    bool ChannelChange(int a, int b);
    int framelast;

    MarkAdPos result;
public:
    cMarkAdAudio(MarkAdContext *maContext);
    ~cMarkAdAudio();
    MarkAdMark *Process(int FrameNumber, int FrameNumberBefore);
    MarkAdPos *Process2ndPass(int FrameNumber);
    void Clear();
};


#endif
