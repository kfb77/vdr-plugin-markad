/*
 * criteria.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


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


    private:
        char *StateToText(const int state);
        int hborder = CRITERIA_UNKNOWN;
        int vborder = CRITERIA_UNKNOWN;
};
