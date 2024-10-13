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
#include "tools.h"
#include "decoder.h"
#include "index.h"


/**
 * class for a single mark
 */
class cMark {
public:
    /**
     * mark constructor
     * @param typeParam         mark type
     * @param oldTypeParam      original mark type before move
     * @param newTypeParam      new mark type after move
     * @param positionParam     mark position
     * @param ptsParam          mark pts
     * @param commentParam      mark comment
     * @param inBroadCastParam  true if mark is in broadcast, false if mark is in advertising
     */
    explicit cMark(const int typeParam, const int oldTypeParam, const int newTypeParam, const int positionParam, const int64_t ptsParam, const char *commentParam = nullptr, const bool inBroadCastParam = false);

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

    /**
     * set PTS based time offset text from mark position
     * @param time pointer to char array, nullptr is valid and clears value
     * @param offset in seconds from recording start
     */
    void SetTime(char* time, int offset);

    /**
     * get time offset in seconds from recording start from mark position
     * @return offset from recording start in seconds
     */
    int GetTimeSeconds() const;

    /**
     * get PTS based time offset text from mark position
     * @return char array of time stamp with format HH:MM:SS.FF, nullptr if not set
     */
    char *GetTime();


    int type         = MT_UNDEFINED;   //!< mark type
    //!<
    int oldType      = MT_UNDEFINED;   //!< old mark type after mark moved
    //!<
    int newType      = MT_UNDEFINED;   //!< new mark type after mark moved
    //!<
    int position     = -1;             //!< mark frame position (on continuous read/decoding, this packet number was read, when mark picture was decoded, used markad intern)
    //!<
    int64_t pts      = -1;             //!< pts from frame of mark (used for mark timestamps)
    //!<
    char *comment    = nullptr;        //!< mark comment
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

    cMark *next         = nullptr;       //!< next mark
    //!<
    cMark *prev         = nullptr;       //!< previous mark
    //!<
    char *timeOffsetPTS = nullptr;       //!< time stamp of the mark position
    //!<
    int secOffsetPTS    = -1;         //!< offset in seconds to recording start of the mark position
    //!<
};

/**
 * class contains current marks
 */
class cMarks : protected cTools {
public:
    cMarks();
    ~cMarks();

    /**
     * write all current marks to log file
     */
    void Debug();


    /**
     * register recording index
     * @param indexParam recording index
     */
    void SetIndex(cIndex *indexParam) {
        index = indexParam;
    }


    /**
     * register frame rate
     * @param frameRateParam frame rate of video
     */
    void SetFrameRate(const int frameRateParam) {
        frameRate = frameRateParam;
    }


    /**
     * set marks filename
     * @param fileNameParam name of marks file
     */
    void SetFileName(const char *fileNameParam) {
        if (fileNameParam) {
            strncpy(filename, fileNameParam, sizeof(filename));
            filename[sizeof(filename)-1] = 0;

        }
    }

    /**
     * calculate count of marks
     * @param type marks type to count
     * @param mask type mask
     * @return counf of mark
     */
    int Count(const int type = 0xFF, const int mask = 0xFF) const;

    /**
     * add mark
     * @param type         mark type
     * @param oldType      original mark type before move
     * @param newType      new mark type after move
     * @param position     mark position
     * @param framePTS     PTS of decoded frame
     * @param comment      mark comment
     * @param inBroadCast true if mark is in broacast, false if mark is in advertising
     * @return ointer to new mark
     */
    cMark *Add(const int type, const int oldType, const int newType, int position, const int64_t framePTS, const char *comment = nullptr, const bool inBroadCast = false);

    /**
     * convert packet number to time string
     * if isVDR packetNumber / frameRate is used, else PTS based time offset is used
     * @param packetNumber packet number
     * @param isVDR true: calculate timestamp based on packet number, false: calculate timestamp based on PTS of packet number
     * @param offsetSeconds offset in seconds since recording start
     * @return time string
     */
    char *IndexToHMSF(const int packetNumber, const bool isVDR, int *offsetSeconds = nullptr);

    /**
     * get PTS based time offset of mark position
     * @param mark pointer to mark
     * @return char array of time stamp with format HH:MM:SS.FF
     */
    char *GetTime(cMark *mark);


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
     * @param mask apply mask for type
     */
    void DelFromTo(const int from, const int to, const int type, const int mask);

    /**
     * delete marks from/to position
     * @param position frame number position
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
     * @param mark delete this mark
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
     * @param position delete this position
     */
    void Del(const int position);

    /**
     * change mark type (START or STOP)
     * @param mark    move this mark
     * @param newType new type of mark, allow values are MT_START or MT_STOP
     * @return mark with new type
     */
    static cMark *ChangeType(cMark *mark, const int newType);

    /**
     * move mark position
     * @param dscMark       move this mark
     * @param newPosition   packet number of new position
     * @param newPTS        if >= 0 use this PTS for new position, else use srcMark
     * @param newType       new type of mark
     * @return              mark with new position and new tye
     */
    cMark *Move(cMark *dscMark, int newPosition, int64_t newPTS, const int newType);

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
     * @param position search around this position
     * @param type     next mark type
     * @param mask     binary mask for type
     * @return nearest mark
     */
    cMark *GetAround(const int frames, const int position, const int type = 0xFF, const int mask = 0xFF);

    /**
     * get previous mark
     * @param position start search of next mark at this position
     * @param type     next mark type
     * @param mask     binary mask for type
     * @return previous mark
     */
    cMark *GetPrev(const int position, const int type = 0xFF, const int mask = 0xFF);

    /**
     * get next mark
     * @param position start search of next mark at this position
     * @param type     next mark type
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
     * @param isRunningRecording true if save during running recording
     * @param writePTS true if additional write PTS based timestamps
     * @param force     true if to save in any cases, false if only save when not running recording
     * @return true if successfully saved, false otherwise
     */
    bool Save(const char *directory, const bool isRunningRecording, const bool writePTS, const bool force);


    /**
     * calculate length of the braodcast without advertisement
     * @return count frames of the broadcast without advertisement
     */
    int Length() const;

    /**
     * convert mark type to text
     * @param type mark type
     * @return text of the mark type
     */
    static char *TypeToText(const int type);

private:

    cIndex *index        = nullptr;  //!< recording index
    //!<
    int frameRate        = 0;        //!< recording frame rate for fallback if we have no index (used by logo search)
    //!<
    char filename[1024]  = {0};      //!< name of marks file (default: marks)
    //!<
    cMark *first         = nullptr;  //!< pointer to first mark
    //!<
    cMark *last          = nullptr;  //!< pointer to last mark
    //!<
    int count            = 0;        //!< number of current marks
    //!<
};
#endif
