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


class clMark {
    private:
        clMark *next;
        clMark *prev;
    public:
        int type;
        int position;
        char *comment = NULL;
        bool inBroadCast = false;
        clMark(const int Type = 0, const int Position = 0, const char *Comment = NULL, const bool InBroadCast = false);
        ~clMark();
        clMark *Next() {
            return next;
        };
        clMark *Prev() {
            return prev;
        };
        void Set(clMark *Prev, clMark *Next) {
            prev=Prev;
            next=Next;
        }
        void SetNext(clMark *Next) {
            next=Next;
        }
        void SetPrev(clMark *Prev) {
            prev=Prev;
        }
};


class clMarks {
    private:
        struct tIndexVDR {
            int offset;
            unsigned char type;
            unsigned char number;
            short reserved;
        };
        struct tIndexTS {
            uint64_t offset: 40;
            int reserved: 7;
            int independent: 1;
            uint16_t number: 16;
        };
        char filename[1024];
        clMark *first,*last;
        int count;
        int savedcount = 0;
        int indexfd;
        char *TypeToText(const int type);
    public:
        clMarks();
        ~clMarks();
        void RegisterIndex(cIndex *recordingIndex);
        int Count(const int Type = 0xFF, const int Mask = 0xFF);
        void SetFileName(const char *FileName) {
            if (FileName) {
                strncpy(filename,FileName,sizeof(filename));
                filename[sizeof(filename)-1]=0;

            }
        }
        clMark *Add(const int Type, const int Position, const char *Comment = NULL, const bool inBroadCast = false);
        char *IndexToHMSF(const int Index, const MarkAdContext *maContext);
        void DelWeakFromTo(const int from, const int to, const short int type);
        void DelTill(const int Position, clMarks *blackMarks, const bool FromStart = true);
        void DelFrom(const int Position);
        void DelAll();
        void Del(clMark *Mark);
        void Del(const unsigned char Type);
        void Del(const int Position);
        clMark *Move(MarkAdContext *maContext, clMark *mark, const int newPosition, const char* reason);
        clMark *Get(const int Position);
        clMark *GetAround(const int Frames, const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        clMark *GetPrev(const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        clMark *GetNext(const int Position, const int Type = 0xFF, const int Mask = 0xFF);
        clMark *GetFirst() {
            return first;
        }
        clMark *GetLast() {
            return last;
        }
        bool Backup(const char *Directory, const bool isTS);
        bool Load(const char *Directory, const double FrameRate, const bool isTS);
        bool Save(const char *Directory, const MarkAdContext *maContext, const bool isTS, const bool force);
        int LoadVPS(const char *Directory, const char *type);

        cIndex *recordingIndexMarks = NULL;
#define IERR_NOTFOUND 1
#define IERR_TOOSHORT 2
#define IERR_SEEK 3
#define IERR_READ 4
#define IERR_FRAME 5
};
#endif
