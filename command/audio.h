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


/**
 *  detect audio channel changes and set channel marks
 */
class cMarkAdAudio {
    public:

/**
 *  store maContext and recordingIndex for future use in this class
 *  @param maContext markad context
 *  @param recordingIndex recording index
 */
        explicit cMarkAdAudio(MarkAdContext *maContext, cIndex *recordingIndex);
        ~cMarkAdAudio();

/**
 *  compare current audio channels with channels before and add marks if channel count has changed
 */
        MarkAdMark *Process();

/**
 *  reset stored channel states of all audio streams
 */
        void Clear();


    private:

/**
 *  reset all values of mark
 */
        void ResetMark();

/**
 *  prepare mark to add
 *  @param type           type of the mark (MT_CHANNELSTART or MT_CHANNELSTOP)
 *  @param position frame number of the mark
 *  @param channelsbefore number of channels before change
 *  @param channelsafter  number of channels after change
 */
        void SetMark(const int type, const int position, const int channelsbefore, const int channelsafter);

/**
 *  detect if there is a change of the audio channel count
 *  @param channelsbefore number of channels before
 *  @param channelsafter  number of channels now
 *  @return true if channel count are different, false if not
 */
        bool ChannelChange(int channelsbefore, int channelsafter);

        MarkAdContext *macontext;
        cIndex *recordingIndexAudio = NULL;
        MarkAdMark mark;
        short int channels[MAXSTREAMS] = {0};
};
#endif
