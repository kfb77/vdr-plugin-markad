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


class clMark {
    private:
        clMark *next;
        clMark *prev;
    public:
        int type;
        int position;
        char *comment = NULL;
        bool inBroadCast = false;
        clMark(int Type = 0, int Position = 0, const char *Comment = NULL, bool InBroadCast = false);
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
#if defined CLASSIC_DECODER
        void WriteIndex(bool isTS, uint64_t Offset,int FrameType, int Number);
#endif
    public:
        clMarks() {
            strcpy(filename, "marks");
            first=last=NULL;
            savedcount=0;
            count=0;
            indexfd=-1;
        }
        ~clMarks();
        int Count(int Type=0xFF, int Mask=0xFF);
        int CountWithoutBlack();
        void SetFileName(const char *FileName) {
            if (FileName) {
                strncpy(filename,FileName,sizeof(filename));
                filename[sizeof(filename)-1]=0;

            }
        }
        clMark *Add(int Type, int Position, const char *Comment = NULL, bool inBroadCast = false);
        char *IndexToHMSF(int Index, MarkAdContext *maContext, cDecoder *ptr_cDecoder);
        void DelWeakFromTo(const int from, const int to, const short int type);
        void DelTill(int Position,bool FromStart=true);
        void DelAll();
        void Del(clMark *Mark);
        void Del(unsigned char Type);
        void Del(int Position);
        clMark *Get(int Position);
        clMark *GetAround(const int Frames, const int Position, const int Type=0xFF, const int Mask=0xFF);
        clMark *GetPrev(int Position,int Type=0xFF, int Mask=0xFF);
        clMark *GetNext(int Position,int Type=0xFF, int Mask=0xFF);
        clMark *GetFirst() {
            return first;
        }
        clMark *GetLast() {
            return last;
        }
        bool Backup(const char *Directory, bool isTS);
        bool Load(const char *Directory, double FrameRate, bool isTS);
        bool Save(const char *Directory, MarkAdContext *maContext, cDecoder *ptr_cDecoder, bool isTS, bool Force=false);
        int LoadVPS(const char *Directory, const char *type);

#define IERR_NOTFOUND 1
#define IERR_TOOSHORT 2
#define IERR_SEEK 3
#define IERR_READ 4
#define IERR_FRAME 5

#if defined CLASSIC_DECODER
        bool ReadIndex(const char *Directory, bool isTS, int FrameNumber, int Range, int *Number, off_t *Offset, int *Frame, int *iFrames);
        void WriteIndex(const char *Directory, bool isTS, uint64_t Offset, int FrameType, int Number);
        void RemoveGeneratedIndex(const char *Directory,bool isTS);
        bool CheckIndex(const char *Directory, bool isTS, int *FrameCnt, int *IndexError);
        void CloseIndex(const char *Directory, bool isTS);
#endif
};
#endif
