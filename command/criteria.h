/*
 * criteria.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __criteria_h_
#define __criteria_h_

#include "marks.h"

enum eCriteria {
    CRITERIA_USED        =  2,
    CRITERIA_AVAILABLE   =  1,
    CRITERIA_UNKNOWN     =  0,
    CRITERIA_UNAVAILABLE = -1,
    CRITERIA_DISABLED    = -2,
};

/**
 * store valid mark creteria for bradcast
 */
class cMarkCriteria : public cMarks {
    public:

/**
 * cDecoder constructor
 */
        cMarkCriteria();
        ~cMarkCriteria();


/**
 * get status of a mark type
 * @return status
 */
        int GetMarkTypeState(const int type);


/**
 * set status of a mark type
 * @return status
 */
        void SetMarkTypeState(const int type, const int state);


/**
 * list mark type stati
 */
        void ListMarkTypeState();

/**
 *  get detection state of marks from type
 */
        bool GetDetectionState(const int type);


/**
 * get status of possible closing credits without logo
 * @return status
 */
        int GetClosingCreditsState();


/**
 * set status of closing credits without logo
 * @return status
 */
        void SetClosingCreditsState(const int state);


/**
 * turn on/of detection of marks from type
 */
        void SetDetectionState(const int type, const bool state);


/**
 * list detection stati
 */
        void ListDetection();


    private:
/**
 * convert state to printable text
 * @param state
 * @return state in printable text
 */
        char *StateToText(const int state);

        int logo                  = CRITERIA_UNKNOWN;  //!< status of logo in broadcast
                                                       //!<
        int hborder               = CRITERIA_UNKNOWN;  //!< status of hborder in broadcast
                                                       //!<
        int vborder               = CRITERIA_UNKNOWN;  //!< status of vborder in broadcast
                                                       //!<
        int aspectratio           = CRITERIA_UNKNOWN;  //!< status of aspact ration in broadcast
                                                       //!<
        int channel               = CRITERIA_UNKNOWN;  //!< status of channel changes in broadcast
                                                       //!<

        int closingCredits        = CRITERIA_UNKNOWN;  //!< status of closing credits after end mark
                                                       //!<

        bool sceneDetection       = true;              //!< true if we have to detect scene changes, false otherwise
                                                       //!<
        bool soundDetection       = true;              //!< true if we have to detect silence, false otherwise
                                                       //!<
        bool blackscreenDetection = true;              //!< true if we have to detect black screens, false otherwise
                                                       //!<
        bool logoDetection        = true;              //!< true if we have to detect logo, false otherwise
                                                       //!<
        bool vborderDetection     = true;              //!< true if we have to detect vertical border, false otherwise
                                                       //!<
        bool hborderDetection     = true;              //!< true if we have to detect horizontal border, false otherwise
                                                       //!<
        bool aspectratioDetection = true;              //!< true if we have to detect video aspect ratio changes, false otherwise
                                                       //!<
        bool channelDetection     = true;              //!< true if we have to detect channel changes, false otherwise
                                                       //!<

        bool videoDecoding        = true;              //!< true if we have do decode video stream, false otherwise
                                                       //!<
        bool audioDecoding        = true;              //!< true if we have do decode audio stream, false otherwise
                                                       //!<
};
#endif
