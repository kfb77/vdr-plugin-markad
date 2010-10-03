/*
 * marks.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __marks_h_
#define __marks_h_

#include <string.h>

class clMark
{
private:
    clMark *next;
    clMark *prev;
public:
    int type;
    int position;
    char *comment;
    clMark(int Type=0, int Position = 0, const char *Comment = NULL);
    ~clMark();
    clMark *Next()
    {
        return next;
    };
    clMark *Prev()
    {
        return prev;
    };
    void Set(clMark *Prev, clMark *Next)
    {
        prev=Prev;
        next=Next;
    }
    void SetNext(clMark *Next)
    {
        next=Next;
    }
    void SetPrev(clMark *Prev)
    {
        prev=Prev;
    }
};

class clMarks
{
private:
    struct tIndexVDR
    {
        int offset;
        unsigned char type;
        unsigned char number;
        short reserved;
    };

    struct tIndexTS
    {
uint64_t offset:
        40;
int reserved:
        7;
int independent:
        1;
uint16_t number:
        16;
    };

    char filename[1024];
    clMark *first,*last;
    char *IndexToHMSF(int Index, double FramesPerSecond);
    int count;
    int savedcount;
    int indexfd;
public:
    clMarks()
    {
        strcpy(filename,"marks");
        first=last=NULL;
        savedcount=0;
        count=0;
        indexfd=-1;
    }
    ~clMarks();
    int Count(int Type=0xFF);
    void SetFileName(const char *FileName)
    {
        if (FileName)
        {
            strncpy(filename,FileName,sizeof(filename)-1);
            filename[sizeof(filename)-1]=0;
        }
    }
    clMark *Add(int Type, int Position, const char *Comment = NULL);
    void DelTill(int Position,bool FromStart=true);
    void DelAll();
    void Del(clMark *Mark);
    void Del(unsigned char Type);
    void Del(int Position);
    clMark *Get(int Position);
    clMark *GetPrev(int Position,int Type=0xFF, int Mask=0xFF);
    clMark *GetNext(int Position,int Type=0xFF, int Mask=0xFF);
    clMark *GetFirst()
    {
        return first;
    }
    clMark *GetLast()
    {
        return last;
    }
    bool Backup(const char *Directory, bool isTS);
    bool Load(const char *Directory, double FrameRate, bool isTS);
    bool Save(const char *Directory, double FrameRate, bool isTS, bool Force=false);
#define IERR_NOTFOUND 1
#define IERR_TOOSHORT 2
#define IERR_SEEK 3
#define IERR_READ 4
#define IERR_FRAME 5
    bool CheckIndex(const char *Directory, bool isTS, int *FrameCnt, int *IndexError);
    bool ReadIndex(const char *Directory, bool isTS, int FrameNumber, int Range, int *Number,
                   off_t *Offset, int *Frame, int *iFrames);
    void WriteIndex(const char *Directory, bool isTS, uint64_t Offset,
                    int FrameType, int Number);
    void CloseIndex(const char *Directory, bool isTS);
    void RemoveGeneratedIndex(const char *Directory,bool isTS);
};

#endif
