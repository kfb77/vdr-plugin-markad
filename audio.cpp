/*
 * audio.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "audio.h"

cMarkAdAudio::cMarkAdAudio(MarkAdContext *maContext)
{
    macontext=maContext;
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
    channels=0;
#if 0
    lastiframe_gain=-ANALYZEFRAMES;
#endif
    lastiframe_silence=-1;
}

cMarkAdAudio::~cMarkAdAudio()
{
    ResetMark();
}

void cMarkAdAudio::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;
}

bool cMarkAdAudio::AddMark(int Type, int Position, const char *Comment)
{
    if (!Comment) return false;
    if (mark.Comment)
    {
        int oldlen=strlen(mark.Comment);
        mark.Comment=(char *) realloc(mark.Comment,oldlen+10+strlen(Comment));
        if (!mark.Comment)
        {
            mark.Position=0;
            return false;
        }
        strcat(mark.Comment," [");
        strcat(mark.Comment,Comment);
        strcat(mark.Comment,"]");
    }
    else
    {
        mark.Comment=strdup(Comment);
    }
    mark.Position=Position;
    mark.Type=Type;
    return true;
}

bool cMarkAdAudio::SilenceDetection()
{
    if (!macontext->Audio.Data.Valid) return false;
    if (lastiframe_silence==lastiframe) return false; // we already detected silence for this frame

    int samples=macontext->Audio.Data.SampleBufLen/
                sizeof(*macontext->Audio.Data.SampleBuf)/
                macontext->Audio.Info.Channels;

    short left,right;
    int lowvalcount=0;
    for (int i=0; i<samples; i++)
    {
        left=macontext->Audio.Data.SampleBuf[0+(i*2)];
        right=macontext->Audio.Data.SampleBuf[1+(i*2)];

        if ((abs(left)+abs(right))<CUT_VAL)
        {
            lowvalcount++;
            if (lowvalcount>MIN_LOWVALS)
            {
                lastiframe_silence=lastiframe;
                return true;
            }
        }
        else
        {
            lowvalcount=0;
        }
    }
    return false;
}

#if 0
bool cMarkAdAudio::AnalyzeGain()
{
    if (!macontext->Audio.Data.Valid) return false;

    int samples=macontext->Audio.Data.SampleBufLen/
                sizeof(*macontext->Audio.Data.SampleBuf)/
                macontext->Audio.Info.Channels;

    double left[samples];
    double right[samples];

    for (int i=0; i<samples; i++)
    {
        left[i]=macontext->Audio.Data.SampleBuf[0+(i*2)];
        right[i]=macontext->Audio.Data.SampleBuf[1+(i*2)];
    }

    if ((lastiframe-lastiframe_gain)>ANALYZEFRAMES)
    {
        if (lastiframe_gain>0)
        {
            double dgain,gain = audiogain.GetGain();
            dgain=gain-lastgain;
            printf("%05i %+.2f db %+.2f db\n",lastiframe_gain,gain,lastgain);
            lastgain=gain;
        }
        audiogain.Init(macontext->Audio.Info.SampleRate);
        lastiframe_gain=lastiframe;
    }
    if (audiogain.AnalyzeSamples(left,right,samples,2)!=GAIN_ANALYSIS_OK)
    {
        lastiframe_gain=-ANALYZEFRAMES;
    }

    return true;
}
#endif

bool cMarkAdAudio::ChannelChange(int a, int b)
{
    if ((a==0) || (b==0)) return false;
    if (a!=b) return true;
    return false;
}

MarkAdMark *cMarkAdAudio::Process(int LastIFrame)
{
    ResetMark();
    if (!LastIFrame) return NULL;
    lastiframe=LastIFrame;

#if 0
    AnalyzeGain();
#endif
    if (macontext->Audio.Options.AudioSilenceDetection)
    {
        if (SilenceDetection())
        {
            char *buf=NULL;
            if (asprintf(&buf,"audio channel silence detecion (%i)",lastiframe)!=-1)
            {
                isyslog(buf);
                AddMark(MT_SILENCECHANGE,lastiframe,buf);
                free(buf);
            }
        }
    }

    if (ChannelChange(macontext->Audio.Info.Channels,channels))
    {
        char *buf=NULL;
        if (asprintf(&buf,"audio channel change from %i to %i (%i)", channels,
                     macontext->Audio.Info.Channels,lastiframe)!=-1)
        {
            isyslog(buf);
            AddMark(MT_CHANNELCHANGE,lastiframe,buf);
            free(buf);
        }
    }

    channels=macontext->Audio.Info.Channels;
    return &mark;
}

