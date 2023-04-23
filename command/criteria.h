/*
 * criteria.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "marks.h"

enum eCriteria {
    MARK_AVAILABLE   =  1,
    MARK_UNKNOWN     =  0,
    MARK_UNAVAILABLE = -1,
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
        int GetState(const int type);


/**
 * set status of a mark type
 * @return status
 */
        void SetState(const int type, const int state);


    private:
        char *StateToText(const int state);
        int hborder = MARK_UNKNOWN;
};
