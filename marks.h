/*
 * marks.h: A plugin for the Video Disk Recorder
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
    int position;
    char *comment;
    clMark(int Position = 0, const char *Comment = NULL);
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
    clMark *first;
    char *IndexToHMSF(int Index, double FramesPerSecond);
    bool CheckIndex(int FileDescriptor, int Index, bool isTS);
    int count;
public:
    ~clMarks();
    int Count()
    {
        return count;
    }
    clMarks()
    {
        strcpy(filename,"marks");
        first=NULL;
    };
    void SetFileName(const char *FileName)
    {
        if (FileName)
        {
            strncpy(filename,FileName,sizeof(filename)-1);
            filename[sizeof(filename)-1]=0;
        }
    }
    clMark *Add(int Position, const char *Comment = NULL);
    void Del(clMark *Mark);
    clMark *Get(int Position);
    clMark *GetPrev(int Position);
    clMark *GetNext(int Position);
    bool Load(const char *Directory, double FrameRate, bool isTS);
    bool Save(const char *Directory, double FrameRate, bool isTS, bool Backup, bool *IndexError);
};

#endif
