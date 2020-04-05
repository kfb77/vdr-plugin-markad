/*
 * marks.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>

#include "marks.h"

clMark::clMark(int Type, int Position, const char *Comment)
{
    type=Type;
    position=Position;
    if (Comment)
    {
        comment=strdup(Comment);
    }
    else
    {
        comment=NULL;
    }
    prev=NULL;
    next=NULL;
}

clMark::~clMark()
{
    if (comment) free(comment);
}

// --------------------------------------------------------------------------

clMarks::~clMarks()
{
    DelAll();
    if (indexfd!=-1) close(indexfd);
}

int clMarks::Count(int Type, int Mask)
{
    if (Type==0xFF) return count;

    if (!first) return 0;

    int ret=0;
    clMark *mark=first;
    while (mark)
    {
        if ((mark->type & Mask)==Type) ret++;
        mark=mark->Next();
    }
    return ret;
}

void clMarks::Del(int Position)
{
    if (!first) return; // no elements yet

    clMark *next,*mark=first;
    while (mark)
    {
        next=mark->Next();
        if (mark->position==Position)
        {
            Del(mark);
            return;
        }
        mark=next;
    }
}

void clMarks::Del(unsigned char Type)
{
    if (!first) return; // no elements yet

    clMark *next,*mark=first;
    while (mark)
    {
        next=mark->Next();
        if (mark->type==Type) Del(mark);
        mark=next;
    }
}

void clMarks::DelTill(int Position, bool FromStart)
{
    clMark *next,*mark=first;
    if (!FromStart)
    {
        while (mark)
        {
            if (mark->position>Position) break;
            mark=mark->Next();
        }
    }

    while (mark)
    {
        next=mark->Next();
        if (FromStart)
        {
            if (mark->position<Position)
            {
                Del(mark);
            }
        }
        else
        {
            Del(mark);
        }
        mark=next;
    }
}

void clMarks::DelAll()
{
    clMark *next,*mark=first;
    while (mark)
    {
        next=mark->Next();
        Del(mark);
        mark=next;
    }
    first=NULL;
    last=NULL;
}


void clMarks::Del(clMark *Mark)
{
    if (!Mark) return;

    if (first==Mark)
    {
        // we are the first mark
        first=Mark->Next();
        if (first)
        {
            first->SetPrev(NULL);
        }
        else
        {
            last=NULL;
        }
    }
    else
    {
        if (Mark->Next() && (Mark->Prev()))
        {
            // there is a next and prev object
            Mark->Prev()->SetNext(Mark->Next());
            Mark->Next()->SetPrev(Mark->Prev());
        }
        else
        {
            // we are the last
            Mark->Prev()->SetNext(NULL);
            last=Mark->Prev();
        }
    }
    delete Mark;
    count--;
}

clMark *clMarks::Get(int Position)
{
    if (!first) return NULL; // no elements yet

    clMark *mark=first;
    while (mark)
    {
        if (Position==mark->position) break;
        mark=mark->Next();
    }
    return mark;
}

clMark *clMarks::GetAround(int Frames, int Position, int Type, int Mask)
{
    clMark *m1=GetPrev(Position,Type,Mask);
    clMark *m2=GetNext(Position,Type,Mask);

    if (!m1 && !m2) return NULL;
    if (!m1 && m2) return m2;
    if (m1 && !m2)
    {
        if (abs(Position-m1->position)>Frames) return NULL;
        return m1;
    }

    if (abs(m1->position-Position)>abs(m2->position-Position))
    {
        return m2;
    }
    else
    {
        return m1;
    }
}

clMark *clMarks::GetPrev(int Position, int Type, int Mask)
{
    if (!first) return NULL; // no elements yet

    // first advance
    clMark *mark=first;
    while (mark)
    {
        if (mark->position>=Position) break;
        mark=mark->Next();
    }
    if (Type==0xFF)
    {
        if (mark) return mark->Prev();
        return last;
    }
    else
    {
        if (!mark) mark=last;
        else mark=mark->Prev();
        while (mark)
        {
            if ((mark->type & Mask)==Type) break;
            mark=mark->Prev();
        }
        return mark;
    }
}

clMark *clMarks::GetNext(int Position, int Type, int Mask)
{
    if (!first) return NULL; // no elements yet
    clMark *mark=first;
    while (mark)
    {
        if (Type==0xFF)
        {
            if (mark->position>Position) break;
        }
        else
        {
            if ((mark->position>Position) && ((mark->type & Mask)==Type)) break;
        }
        mark=mark->Next();
    }
    if (mark) return mark;
    return NULL;
}

clMark *clMarks::Add(int Type, int Position,const char *Comment)
{
    clMark *newmark;
    if ((newmark=Get(Position)))
    {
        if ((newmark->comment) && (Comment))
        {
            free(newmark->comment);
            newmark->comment=strdup(Comment);
        }
        newmark->type=Type;
        return newmark;
    }

    newmark=new clMark(Type, Position,Comment);
    if (!newmark) return NULL;

    if (!first)
    {
        //first element
        first=last=newmark;
        count++;
        return newmark;
    }
    else
    {
        clMark *mark=first;
        while (mark)
        {
            if (!mark->Next())
            {
                if (Position>mark->position)
                {
                    // add as last element
                    newmark->Set(mark,NULL);
                    mark->SetNext(newmark);
                    last=newmark;
                    break;
                }
                else
                {
                    // add before
                    if (!mark->Prev())
                    {
                        // add as first element
                        newmark->Set(NULL,mark);
                        mark->SetPrev(newmark);
                        first=newmark;
                        break;
                    }
                    else
                    {
                        newmark->Set(mark->Prev(),mark);
                        mark->SetPrev(newmark);
                        break;
                    }
                }
            }
            else
            {
                if ((Position>mark->position) && (Position<mark->Next()->position))
                {
                    // add between two marks
                    newmark->Set(mark,mark->Next());
                    mark->SetNext(newmark);
                    newmark->Next()->SetPrev(newmark);
                    break;
                }
                else
                {
                    if ((Position<mark->position) && (mark==first))
                    {
                        // add as first mark
                        first=newmark;
                        mark->SetPrev(newmark);
                        newmark->SetNext(mark);
                        break;
                    }
                }
            }
            mark=mark->Next();
        }
        if (!mark)return NULL;
        count++;
        return newmark;
    }
    return NULL;
}

char *clMarks::IndexToHMSF(int Index, double FramesPerSecond)
{
    if (FramesPerSecond==0.0) return NULL;
    char *buf=NULL;
    double Seconds;
    int f = int(modf((Index+0.5)/FramesPerSecond,&Seconds)*FramesPerSecond+1);
    int s = int(Seconds);
    int m = s / 60 % 60;
    int h = s / 3600;
    s %= 60;
    if (asprintf(&buf,"%d:%02d:%02d.%02d",h,m,s,f)==-1) return NULL;
    return buf;
}

void clMarks::RemoveGeneratedIndex(const char *Directory, bool isTS)
{
    char *ipath=NULL;
    if (asprintf(&ipath,"%s/index%s.generated",Directory,isTS ? "" : ".vdr")==-1) return;
    unlink(ipath);
    free(ipath);
    return;
}

bool clMarks::ReadIndex(const char *Directory, bool isTS, int FrameNumber, int Range, int *Number,
                        off_t *Offset,  int *Frame, int *iFrames)
{
    if (!Offset) return false;
    if (!Number) return false;
    if (!Frame) return false;
    if (!iFrames) return false;
    *Offset=0;
    *Number=0;
    *Frame=0;
    *iFrames=0;

    char *ipath=NULL;
    if (asprintf(&ipath,"%s/index%s",Directory,isTS ? "" : ".vdr")==-1) return false;
    int ifd=open(ipath,O_RDONLY);
    free(ipath);
    if (ifd==-1) return false;

    if (isTS)
    {
        struct tIndexTS IndexTS;
        off_t pos=FrameNumber*sizeof(IndexTS);
        if (lseek(ifd,pos,SEEK_SET)!=pos)
        {
            close(ifd);
            return false;
        }
        do
        {
            if (read(ifd,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS))
            {
                close(ifd);
                return false;
            }
            if (IndexTS.independent)
            {
                *Offset=IndexTS.offset;
                *Number=IndexTS.number;
                pos=lseek(ifd,0,SEEK_CUR);
                *Frame=(int) (pos/sizeof(IndexTS))-1;
            }
        }
        while (!IndexTS.independent);

        int cnt=0;
        do
        {
            if (read(ifd,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS))
            {
                close(ifd);
                if (!*iFrames) return false;
                (*iFrames)-=2; // just to be safe
                return true;
            }
            if (IndexTS.independent) (*iFrames)++;
            cnt++;
        }
        while (cnt<Range);
    }
    else
    {
        struct tIndexVDR IndexVDR;
        off_t pos=FrameNumber*sizeof(IndexVDR);
        if (lseek(ifd,pos,SEEK_SET)!=pos)
        {
            close(ifd);
            return false;
        }
        do
        {
            if (read(ifd,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR))
            {
                close(ifd);
                return false;
            }
            if (IndexVDR.type==1)
            {
                *Offset=IndexVDR.offset;
                *Number=IndexVDR.number;
                pos=lseek(ifd,0,SEEK_CUR);
                *Frame=(int) (pos/sizeof(IndexVDR))-1;
            }
        }
        while (IndexVDR.type!=1);

        int cnt=0;
        do
        {
            if (read(ifd,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR))
            {
                close(ifd);
                if (!*iFrames) return false;
                (*iFrames)-=2; // just to be safe
                return true;
            }
            if (IndexVDR.type==1) (*iFrames)++;
            cnt++;
        }
        while (cnt<Range);
    }
    close(ifd);
    if (!*iFrames) return false;
    return true;
}

void clMarks::WriteIndex(bool isTS, uint64_t Offset, int FrameType, int Number)
{
    if (indexfd==-1) return;
    if (isTS)
    {
        struct tIndexTS IndexTS;
        IndexTS.offset=Offset;
        IndexTS.reserved=0;
        IndexTS.independent=(FrameType==1);
        IndexTS.number=(uint16_t) Number;
        if (write(indexfd,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS)) return;
    }
    else
    {
        struct tIndexVDR IndexVDR;
        IndexVDR.offset=(int) Offset;
        IndexVDR.type=(unsigned char) FrameType;
        IndexVDR.number=(unsigned char) Number;
        IndexVDR.reserved=0;
        if (write(indexfd,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR)) return;
    }
}

void clMarks::WriteIndex(const char *Directory, bool isTS, uint64_t Offset,
                         int FrameType, int Number)
{
    if (indexfd==-1)
    {
        char *ipath=NULL;
        if (asprintf(&ipath,"%s/index%s.generated",Directory,isTS ? "" : ".vdr")==-1) return;
        indexfd=open(ipath,O_WRONLY|O_CREAT|O_TRUNC,0644);
        free(ipath);
        if (indexfd==-1) return;
    }
    WriteIndex(isTS,Offset,FrameType,Number);
    return;
}

void clMarks::CloseIndex(const char *Directory, bool isTS)
{
    if (indexfd==-1) return;

    if (getuid()==0 || geteuid()!=0)
    {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath=NULL;
        if (asprintf(&spath,"%s/%s",Directory,isTS ? "00001.ts" : "001.vdr")!=-1)
        {
            struct stat statbuf;
            if (!stat(spath,&statbuf))
            {
                if (fchown(indexfd,statbuf.st_uid, statbuf.st_gid)) {};
            }
            free(spath);
        }
    }
    close(indexfd);
    indexfd=-1;
}

bool clMarks::CheckIndex(const char *Directory, bool isTS, int *FrameCnt, int *IndexError)
{
    if (!IndexError) return false;
    *IndexError=0;

    if (!first) return true;

    char *ipath=NULL;
    if (asprintf(&ipath,"%s/index%s",Directory,isTS ? "" : ".vdr")==-1) return false;

    int fd=open(ipath,O_RDONLY);
    free(ipath);
    if (fd==-1)
    {
        *IndexError=IERR_NOTFOUND;
        return true;
    }

    if ((FrameCnt) && (*FrameCnt))
    {
        struct stat statbuf;
        if (fstat(fd,&statbuf)!=-1)
        {
            int framecnt;
            if (isTS)
            {
                framecnt=statbuf.st_size/sizeof(struct tIndexTS);
            }
            else
            {
                framecnt=statbuf.st_size/sizeof(struct tIndexVDR);
            }
            if (abs(framecnt-*FrameCnt)>2000)
            {
                *FrameCnt=framecnt;
                *IndexError=IERR_TOOSHORT;
                close(fd);
                return true;
            }
        }
    }

    clMark *mark=first;
    while (mark)
    {
        if (isTS)
        {
            off_t offset = mark->position * sizeof(struct tIndexTS);
            if (lseek(fd,offset,SEEK_SET)!=offset)
            {
                *IndexError=IERR_SEEK;
                break;
            }
            struct tIndexTS IndexTS;
            if (read(fd,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS))
            {
                *IndexError=IERR_READ;
                break;
            }
            if (!IndexTS.independent)
            {
                *IndexError=IERR_FRAME;
                break;
            }
        }
        else
        {
            off_t offset = mark->position * sizeof(struct tIndexVDR);
            if (lseek(fd,offset,SEEK_SET)!=offset)
            {
                *IndexError=IERR_SEEK;
                break;
            }
            struct tIndexVDR IndexVDR;
            if (read(fd,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR))
            {
                *IndexError=IERR_READ;
                break;
            }
            if (IndexVDR.type!=1)
            {
                *IndexError=IERR_FRAME;
                break;
            }
        }
        mark=mark->Next();
    }
    close(fd);
    return true;
}

bool clMarks::Backup(const char *Directory, bool isTS)
{
    char *fpath=NULL;
    if (asprintf(&fpath,"%s/%s%s",Directory,filename,isTS ? "" : ".vdr")==-1) return false;

    // make backup of old marks, filename convention taken from noad
    char *bpath=NULL;
    if (asprintf(&bpath,"%s/%s0%s",Directory,filename,isTS ? "" : ".vdr")==-1)
    {
        free(fpath);
        return false;
    }

    int ret=rename(fpath,bpath);
    free(bpath);
    free(fpath);
    return (ret==0);
}

bool clMarks::Load(const char *Directory, double FrameRate, bool isTS)
{
    char *fpath=NULL;
    if (asprintf(&fpath,"%s/%s%s",Directory,filename,isTS ? "" : ".vdr")==-1) return false;

    FILE *mf;
    mf=fopen(fpath,"r+");
    free(fpath);
    if (!mf) return false;

    char *line=NULL;
    size_t length;
    int h, m, s, f;

    while (getline(&line,&length,mf)!=-1)
    {
        char descr[256]="";
        f=1;
        int n=sscanf(line,"%3d:%02d:%02d.%02d %80c",&h,&m,&s,&f,(char *) &descr);
        if (n==1)
        {
            Add(0,h);
        }
        if (n>=3)
        {
            int pos=int(round((h*3600+m*60+s)*FrameRate))+f-1;
            if (n<=4)
            {
                Add(0,pos);
            }
            else
            {
                char *lf=strchr(descr,10);
                if (lf) *lf=0;
                char *cr=strchr(descr,13);
                if (cr) *cr=0;
                Add(0,pos,descr);
            }
        }
    }
    if (line) free(line);
    fclose(mf);
    return true;
}

bool clMarks::Save(const char *Directory, double FrameRate, bool isTS, bool Force)
{
    if (!first) return false;
    if ((savedcount==count) && (!Force)) return false;

    char *fpath=NULL;
    if (asprintf(&fpath,"%s/%s%s",Directory,filename,isTS ? "" : ".vdr")==-1) return false;

    FILE *mf;
    mf=fopen(fpath,"w+");

    if (!mf)
    {
        free(fpath);
        return false;
    }

    clMark *mark=first;
    while (mark)
    {
        char *buf=IndexToHMSF(mark->position,FrameRate);
        if (buf)
        {
            fprintf(mf,"%s %s\n",buf,mark->comment ? mark->comment : "");
            free(buf);
        }
        mark=mark->Next();
    }
    fclose(mf);

    if (getuid()==0 || geteuid()!=0)
    {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath=NULL;
        if (asprintf(&spath,"%s/%s",Directory,isTS ? "00001.ts" : "001.vdr")!=-1)
        {
            struct stat statbuf;
            if (!stat(spath,&statbuf))
            {
                if (chown(fpath,statbuf.st_uid, statbuf.st_gid)) {};
            }
            free(spath);
        }
    }
    free(fpath);
    savedcount=count;
    return true;
}
