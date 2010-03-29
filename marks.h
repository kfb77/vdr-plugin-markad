/*
 * marks.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __marks_h_
#define __marks_h_

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <fcntl.h>

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
public:
    clMarks()
    {
        strcpy(filename,"marks");
        first=last=NULL;
        savedcount=0;
        count=0;
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
    void Del(clMark *Mark);
    void Del(int Type);
    clMark *Get(int Position);
    clMark *GetPrev(int Position,int Type=0xFF);
    clMark *GetNext(int Position,int Type=0xFF);
    clMark *GetFirst()
    {
        return first;
    }
    clMark *GetLast()
    {
        return last;
    }
    bool Backup(const char *Directory, bool isTS);
    bool Save(const char *Directory, double FrameRate, bool isTS);
    bool CheckIndex(const char *Directory, bool isTS, bool *IndexError);
};

#endif
