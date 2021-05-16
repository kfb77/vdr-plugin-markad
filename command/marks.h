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
#include "decoder_new.h"
#include "index.h"


class cMark {
    private:
        cMark *next;
        cMark *prev;
    public:
        int type;
        int position;
        char *comment = NULL;
        bool inBroadCast = false;
        cMark(const int Type = 0, const int Position = 0, const char *Comment = NULL, const bool InBroadCast = false);
        ~cMark();
        cMark *Next() {
            return next;
        };
        cMark *Prev() {
            return prev;
        };
        void Set(cMark *Prev, cMark *Next) {
            prev=Prev;
            next=Next;
        }
        void SetNext(cMark *Next) {
            next=Next;
        }
        void SetPrev(cMark *Prev) {
            prev=Prev;
        }
};


class cMarks {
    public:
        cMarks();
        ~cMarks();
        void RegisterIndex(cIndex *recordingIndex);
        int Count(const int Type = 0xFF, const int Mask = 0xFF);
        void SetFileName(const char *FileName) {
            if (FileName) {
                strncpy(filename,FileName,sizeof(filename));
                filename[sizeof(filename)-1]=0;

            }
        }
        cMark *Add(const int Type, const int Position, const char *Comment = NULL, const bool inBroadCast = false);
        char *IndexToHMSF(const int Index, const sMarkAdContext *maContext);
        void DelWeakFromTo(const int from, const int to, const short int type);
        void DelFromTo(const int from, const int to, const short int type);
        void DelTill(const int Position, const bool FromStart = true);
        void DelFrom(const int Position);
        void DelAll();
        void Del(cMark *Mark);
        void Del(const unsigned char Type);
        void Del(const int Position);
        cMark *Move(sMarkAdContext *maContext, cMark *mark, const int newPosition, const char* reason);
        cMark *Get(const int Position);
        cMark *GetAround(const int Frames, const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        cMark *GetPrev(const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        cMark *GetNext(const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        cMark *GetFirst() {
            return first;
        }
        cMark *GetLast() {
            return last;
        }
        bool Backup(const char *directory);
        bool Load(const char *directory, const double FrameRate);

/**
 * save marks to recording directory
 * @param directory recording directory
 * @param maContext markad context
 * @param force     true if to save in any cases, false if only save when not running recording
 * @return true if successfuly saved, false otherwise
 */
        bool Save(const char *directory, const sMarkAdContext *maContext, const bool force);

/**
 * get offset of first mark wtih type from markad.vps in recording directory
 * @param directory recording directory
 * @param type VPS mark type
 * @return offset from recording start in seconds
 */
        int LoadVPS(const char *directory, const char *type);

    private:
/**
 * convert mark type to text
 * @param type type of the mark
 * @return text of the mark type
 */
        char *TypeToText(const int type);

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
