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
#include "debug.h"


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
        explicit cMarkAdAudio(sMarkAdContext *maContext, cIndex *recordingIndex);
        ~cMarkAdAudio();

/**
 *  compare current audio channels with channels before and add marks if channel count has changed
 */
        sMarkAdMark *Process();

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
 *  @param channelsBefore number of channels before change
 *  @param channelsAfter  number of channels after change
 */
        void SetMark(const int type, const int position, const int channelsBefore, const int channelsAfter);

/**
 *  detect if there is a change of the audio channel count
 *  @param channelsBefore number of channels before
 *  @param channelsAfter  number of channels now
 *  @return true if channel count are different, false if not
 */
        bool ChannelChange(int channelsBefore, int channelsAfter);

        sMarkAdContext *macontext;             //!< markad context
                                               //!<
        cIndex *recordingIndexAudio = NULL;    //!< recording index
                                               //!<
        sMarkAdMark mark;                      //!< new mark to add
                                               //!<
        short int channels[MAXSTREAMS] = {0};  //!< count of audio channels per stream
                                               //!<
};
#endif
