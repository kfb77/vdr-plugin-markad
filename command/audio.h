/*
 * audio.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __audio_h_
#define __audio_h_

#include "global.h"
#include "debug.h"
#include "index.h"
#include "decoder.h"
#include "criteria.h"

enum {
    SILENCE_UNINITIALIZED = -2,
    SILENCE_FALSE         = -1,
    SILENCE_TRUE          =  1
};


/**
 *  detect audio channel changes and set channel marks
 */
class cAudio {
public:
    /**
     *  store maContext and recordingIndex for future use in this class
     *  @param maContext markad context
     *  @param recordingIndex recording index
     */
    explicit cAudio(cDecoder *decoderParam, cCriteria *criteriaParam);
    ~cAudio();

    /**
     *  compare current audio channels with channels before and add marks if channel count has changed
     */
    sMarkAdMarks *ChannelChange();

    /**
     * detect silence marks
     */

    sMarkAdMarks *Silence();
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

    cDecoder *decoder           = nullptr;                //!< pointer to decoder
    //!<
    cCriteria *criteria            = nullptr;                //!< pointer to analyse criteria
    //!<
    int channelCountBefore         = 0;                      //!< AC3 channel count of frame before
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
    sMarkAdMarks audioMarks        = {};                     //!< array of marks to add to list
    //!<
};
#endif
