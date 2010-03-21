/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog
#include <netinet/in.h> // for htonl

#include "global.h"
#if 0
#include "audio_gain_analysis.h"
#endif

class cMarkAdAudio
{
private:
    int lastiframe;
    MarkAdContext *macontext;

    MarkAdMark mark;
    void ResetMark();
    bool AddMark(int Type, int Position, const char *Comment);

#define CUT_VAL 10
#define MIN_LOWVALS 3
    bool SilenceDetection();
    int lastiframe_silence;

#if 0
#define ANALYZEFRAMES 1
    int lastiframe_gain;
    double lastgain;
    cMarkAdAudioGainAnalysis audiogain;
    bool AnalyzeGain();
#endif

    int channels;
    bool ChannelChange(int a, int b);
public:
    cMarkAdAudio(MarkAdContext *maContext);
    ~cMarkAdAudio();
    MarkAdMark *Process(int LastIFrame);
};


#endif
