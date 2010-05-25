/*
 * marks.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

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
    Clear();
}

int clMarks::Count(int Type)
{
    if (Type==0xFF) return count;

    if (!first) return 0;

    int ret=0;
    clMark *mark=first;
    while (mark)
    {
        if (mark->type==Type) ret++;
        mark=mark->Next();
    }
    return ret;
}

void clMarks::Del(int Type)
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

void clMarks::Clear(int Before)
{
    clMark *next,*mark=first;
    while (mark)
    {
        next=mark->Next();
        if (mark->position<Before)
        {
            Del(mark);
        }
        mark=next;
    }
    if (Before==0x7FFFFFFF)
    {
        first=NULL;
        last=NULL;
    }
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
            if (mark->position>=Position) break;
        }
        else
        {
            if ((mark->position>=Position) && ((mark->type & Mask)==Type)) break;
        }
        mark=mark->Next();
    }
    if (mark) return mark->Next();
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
                    break;
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

bool clMarks::CheckIndex(const char *Directory, bool isTS, bool *IndexError)
{
    if (!IndexError) return false;
    *IndexError=false;

    if (!first) return true;

    char *ipath=NULL;
    if (asprintf(&ipath,"%s/index%s",Directory,isTS ? "" : ".vdr")==-1) return false;

    int fd=open(ipath,O_RDONLY);
    free(ipath);
    if (fd==-1) return false;

    clMark *mark=first;
    while (mark)
    {
        if (isTS)
        {
            off_t offset = mark->position * sizeof(struct tIndexTS);
            if (lseek(fd,offset,SEEK_SET)!=offset)
            {
                *IndexError=true;
                break;
            }
            struct tIndexTS IndexTS;
            if (read(fd,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS))
            {
                *IndexError=true;
                break;
            }
            if (!IndexTS.independent)
            {
                *IndexError=true;
                break;
            }
        }
        else
        {
            off_t offset = mark->position * sizeof(struct tIndexVDR);
            if (lseek(fd,offset,SEEK_SET)!=offset)
            {
                *IndexError=true;
                break;
            }
            struct tIndexVDR IndexVDR;
            if (read(fd,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR))
            {
                *IndexError=true;
                break;
            }
            if (IndexVDR.type!=1)
            {
                *IndexError=true;
                break;
            }
        }
        mark=mark->Next();
    }

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

bool clMarks::Save(const char *Directory, double FrameRate, bool isTS)
{
    if (!first) return false;
    if (savedcount==count) return false;

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
    return true;
}
