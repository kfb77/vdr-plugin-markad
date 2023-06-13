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
    CRITERIA_AVAILABLE   =  1,
    CRITERIA_UNKNOWN     =  0,
    CRITERIA_UNAVAILABLE = -1,
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
 *  get detection state of marks from <type>
 */
        bool GetDetectionState(const int type);

/**
 * turn on/of detection of marks from <type>
 */
        void SetDetectionState(const int type, const bool state);

/**
 * list detection stati
 */
        void ListDetection();

    private:
        char *StateToText(const int state);
        int logo           = CRITERIA_UNKNOWN;
        int hborder        = CRITERIA_UNKNOWN;
        int vborder        = CRITERIA_UNKNOWN;
        int aspectratio    = CRITERIA_UNKNOWN;
        int channel        = CRITERIA_UNKNOWN;

        int closingCredits = CRITERIA_UNKNOWN;

        bool sceneDetection       = true;
        bool blackscreenDetection = true;
        bool logoDetection        = true;
        bool vborderDetection     = true;
        bool hborderDetection     = true;
        bool aspectratioDetection = true;
        bool channelDetection     = true;

        bool videoDecoding        = true;
};
#endif
