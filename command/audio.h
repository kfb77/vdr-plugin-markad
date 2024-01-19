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

enum {
    SILENCE_UNINITIALIZED = -2,
    SILENCE_FALSE         = -1,
    SILENCE_TRUE          =  1
};


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
    sMarkAdMarks *Process(const int frameNumber);

    /**
     *  reset stored channel states of all audio streams
     */
    void Clear();


private:

    /**
     *  reset audio marks array
     */
    void ResetMarks();

    /**
     *  add a new mark ti mark array
     *  @param type           mark type (MT_CHANNELSTART or MT_CHANNELSTOP)
     *  @param position frame number of the mark
     *  @param channelsBefore number of channels before change
     *  @param channelsAfter  number of channels after change
     */
    void AddMark(const int type, const int position, const int channelsBefore, const int channelsAfter);

    /**
     * detect silence marks
     */
    void Silence(const int frameNumber);

    /**
     *  detect if there is a change of the audio channel count
     *  @param channelsBefore number of channels before
     *  @param channelsAfter  number of channels now
     *  @return true if channel count are different, false if not
     */
    static bool ChannelChange(int channelsBefore, int channelsAfter);


    sMarkAdContext *macontext      = NULL;                   //!< markad context
    //!<
    int silenceStatus              = SILENCE_UNINITIALIZED;  //!< status of silence detection
    //!<
    int64_t silencePTS             = -1;                     //!< PTS of first detected silence
    //!<
    int silenceFrame               = -1;                     //!< frame number of first detected silence
    //!<
    bool hasZero                   = false;                  //!< true if we got 0 volume
    //!<
    int64_t soundPTS               = -1;                     //!< PTS of first detected sound
    //!<
    int retry                      = 0;                      //!< retry count to get video frame after first sound PTS
    //!<
    cIndex *recordingIndexAudio    = NULL;                   //!< recording index
    //!<
    short int channels[MAXSTREAMS] = {0};                    //!< count of audio channels per stream
    //!<
    sMarkAdMarks audioMarks        = {};                     //!< array of marks to add to list
    //!<
};
#endif
