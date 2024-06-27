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



/**
 *  detect audio channel changes and set channel marks
 */
class cAudio {
public:
    /**
     *  store maContext and recordingIndex for future use in this class
     *  @param decoderParam   pointer to decoder
     *  @param indexParam     recording index
     *  @param criteriaParam  analyse criteria
     */
    explicit cAudio(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam);
    ~cAudio();

    /**
     *  detect audio based marks
     */
    sMarkAdMarks *Detect();

private:
    /**
     * MP2 stream silence
     */
    typedef struct sAudioMP2Silence {
        int64_t startPTS     = -1;    //!< start PTS
        //!<
        int startFrameNumber = -1;    //!< start frame number
        //!<
        int64_t stopPTS      = -1;    //!< stop PTS
        //!<
        int stopFrameNumber  = -1;    //!< stop frame number
        //!<
    } sAudioMP2Silence;

    /**
     *  set channel change marks
     */
    void ChannelChange();

    /**
     * detect silence marks
     */
    void Silence();

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

    cDecoder *decoder              = nullptr;                //!< pointer to decoder
    //!<
    cIndex *index                  = nullptr;                //!< pointer to index
    //!<
    cCriteria *criteria            = nullptr;                //!< pointer to analyse criteria
    //!<
    int channelCountBefore         = 0;                      //!< AC3 channel count of frame before
    //!<
    sMarkAdMarks audioMarks        = {};                     //!< array of marks to add to list
    //!<
    sAudioMP2Silence audioMP2Silence   = {};                       //!< start/stop of silence
    //!<
};
#endif
