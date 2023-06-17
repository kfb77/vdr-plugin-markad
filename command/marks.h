/*
 * marks.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __marks_h_
#define __marks_h_

#include <string.h>
#include "global.h"
#include "decoder.h"
#include "index.h"


/**
 * class for a single mark
 */
class cMark {
    public:

/**
 * mark constructor
 * @param typeParam        mark type
 * @param oldTypeParam     original mark type before move
 * @param positionParam    mark position
 * @param commentParam     mark comment
 * @param inBroadCastParam true if mark is in broadcast, false if mark is in advertising
 */
        explicit cMark(const int typeParam = MT_UNDEFINED, const int oldTypeParam = MT_UNDEFINED, const int newTypeParam = MT_UNDEFINED, const int positionParam = 0, const char *commentParam = NULL, const bool inBroadCastParam = false);

        ~cMark();

/**
 * get next mark
 * @return next mark
 */
        cMark *Next() {
            return next;
        };

/**
 * get previous mark
 * @return previous mark
 */
        cMark *Prev() {
            return prev;
        };

/**
 * set previous and next mark
 * @param prevParam previous mark
 * @param nextParam next mark
 */
        void Set(cMark *prevParam, cMark *nextParam) {
            prev = prevParam;
            next = nextParam;
        }

/**
 * set next mark
 * @param nextParam next mark
 */
        void SetNext(cMark *nextParam) {
            next = nextParam;
        }

/**
 * set previous mark
 * @param prevParam previous mark
 */
        void SetPrev(cMark *prevParam) {
            prev = prevParam;
        }

        int type         = MT_UNDEFINED;   //!< mark type
                                           //!<
        int oldType      = MT_UNDEFINED;   //!< old mark type after mark moved
                                           //!<
        int newType      = MT_UNDEFINED;   //!< new mark type after mark moved
                                           //!<
        int position     = -1;             //!< mark frame position
                                           //!<
        char *comment    = NULL;           //!< mark comment
                                           //!<
        bool inBroadCast = false;          //!< true if mark is in broadcast, false if mark is in advertising
                                           //!<

    private:
/**
 * copy mark Object (not used)
 */
        cMark(const cMark &cMarkCopy);

/**
 * = operator for mark object (not used)
 */
        cMark &operator=(const cMark &foo);

        cMark *next;                         //!< next mark
                                             //!<
        cMark *prev;                         //!< previous mark
                                             //!<

};

/**
 * class contains current marks
 */
class cMarks {
    public:
        cMarks();
        ~cMarks();

/**
 * register recording index
 * @param recordingIndex recording index
 */
        void RegisterIndex(cIndex *recordingIndex);

/**
 * calculate count of marks
 * @param type type of marks to count
 * @param mask type mask
 * @return counf of mark
 */
        int Count(const int type = 0xFF, const int mask = 0xFF);

/**
 * set marks filename
 * @param fileNameParam name of marks file
 */
        void SetFileName(const char *fileNameParam) {
            if (fileNameParam) {
                strncpy(filename, fileNameParam , sizeof(filename));
                filename[sizeof(filename)-1] = 0;

            }
        }


/**
 * add mark
 * @param type mark type
 * @param oldType original mark type before movecOSDMessage
 * @param position mark position
 * @param comment mark comment
 * @param inBroadCast true if mark is in broacast, false if mark is in advertising
 * @return ointer to new mark
 */
        cMark *Add(const int type, const int oldType, const int newType, const int position, const char *comment = NULL, const bool inBroadCast = false);

/**
 * convert frame number to time string
 * @param frameNumber frame number
 * @param isVDR true: calculate timestamp based on frame number, false: calculate timestamp based on PTS of frame number
 * @return time string
 */
        char *IndexToHMSF(const int frameNumber, const bool isVDR = false);

/**
 * delete weak marks between two positions
 * @param from start position
 * @param to   end position
 * @param type delete marks weaker than this type
 */
        void DelWeakFromTo(const int from, const int to, const short int type);

/**
 * delete marks between two positions
 * @param from start position
 * @param to   end position
 * @param type mark type to delete
 */
        void DelFromTo(const int from, const int to, const short int type);

/**
 * delete marks from/to position
 * @param position position frame number
 * @param fromStart true to delete all marks from start to position, false to delete all marks from position to end
 */
        void DelTill(const int position, const bool fromStart = true);

/**
 * delete all marks after position to last mark
 * @param position start position to delete from
 */
        void DelAfterFromToEnd(const int position);

/**
 * delete all marks
 */
        void DelAll();


/**
 * delete marks with invalid squence (double start or stop marks)
 */
        void DelInvalidSequence();

/**
 * delete mark
 * @param mark mark to delete
 */
        void Del(cMark *mark);

/**
 * delete all marks of given type
 * @param type mark type
 * @param mask binary mask for type
 */
        void DelType(const int type, const int mask);

/**
 * delete mark
 * @param position position to delete
 */
        void Del(const int position);


/**
 * change mark type (START or STOP)
 * @param mark    mark to move
 * @param newType new type of mark, allow values are MT_START or MT_STOP
 */
        void ChangeType(cMark *mark, const int newType);


/**
 * move mark position
 * @param mark        mark to move
 * @param newPosition new position of mark
 * @param reason      reason of move, added to comment
 * @return mark with new position
 */
        cMark *Move(cMark *mark, const int newPosition, const int newType, const char* reason);


/**
 * get first mark
 * @return first mark
 */
        cMark *First();


/**
 * get mark from position
 * @param position frame position
 * @return mark from position
 */
        cMark *Get(const int position);

/**
 * get nearest mark
 * @param frames   maximum frames distance
 * @param position position to search around
 * @param type     type of next mark
 * @param mask     binary mask for type
 * @return nearest mark
 */
        cMark *GetAround(const int frames, const int position, const int type = 0xFF, const int mask = 0xFF);

/**
 * get previous mark
 * @param position position to start search of next mark
 * @param type     type of next mark
 * @param mask     binary mask for type
 * @return previous mark
 */
        cMark *GetPrev(const int position, const int type = 0xFF, const int mask = 0xFF);

/**
 * get next mark
 * @param position position to start search of next mark
 * @param type     type of next mark
 * @param mask     binary mask for type
 * @return next mark
 */
        cMark *GetNext(const int position, const int type = 0xFF, const int mask = 0xFF);

/**
 * get first mark
 * @return first mark
 */
        cMark *GetFirst() {
            return first;
        }

/**
 * get last mark
 * @return last mark
 */
        cMark *GetLast() {
            return last;
        }

/**
 * backup marks file
 * @param directory recording directory
 * @return true if successful, false otherwise
 */
        bool Backup(const char *directory);

/**
 * save marks to recording directory
 * @param directory recording directory
 * @param maContext markad context
 * @param force     true if to save in any cases, false if only save when not running recording
 * @return true if successfuly saved, false otherwise
 */
        bool Save(const char *directory, const sMarkAdContext *maContext, const bool force);


/**
 * calculate length of the braodcast without advertisement
 * @return count frames of the broadcast without advertisement
 */
        int Length();

/**
 * convert mark type to text
 * @param type type of the mark
 * @return text of the mark type
 */
        char *TypeToText(const int type);

    private:

        cIndex *recordingIndexMarks = NULL;  //!< recording index
                                             //!<
        char filename[1024];                 //!< name of marks file (default: marks)
                                             //!<
        cMark *first;                        //!< pointer to first mark
                                             //!<
        cMark *last;                         //!< pointer to last mark
                                             //!<
        int count;                           //!< number of current marks
                                             //!<
};
#endif
