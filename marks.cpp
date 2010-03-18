/*
 * marks.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "marks.h"

clMark::clMark(int Position, const char *Comment)
{
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
    clMark *next,*mark=first;
    while (mark)
    {
        next=mark->Next();
        Del(mark);
        mark=next;
    }

}

void clMarks::Del(clMark *Mark)
{
    if (!Mark) return;
    if (Mark->Next())
    {
        if (Mark->Prev())
        {
            // there is a next and prev object
            Mark->Prev()->SetNext(Mark->Next());
            Mark->Next()->SetPrev(Mark->Prev());
        }
        else
        {
            // just a next, so we are number 1
            Mark->Next()->SetPrev(NULL);
            first=Mark->Next();
        }
    }
    else
    {
        // we are the last
        first=NULL;
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

clMark *clMarks::GetPrev(int Position)
{
    if (!first) return NULL; // no elements yet

    clMark *mark=first;
    while (mark)
    {
        if (mark->position>=Position) break;
        mark=mark->Next();
    }
    return mark->Prev();
}

clMark *clMarks::GetNext(int Position)
{
    if (!first) return NULL; // no elements yet

    clMark *mark=first;
    while (mark)
    {
        if (Position>mark->position) break;
        mark=mark->Next();
    }
    return mark->Next();
}

clMark *clMarks::Add(int Position,const char *Comment)
{
    clMark *newmark;
    if ((newmark=Get(Position)))
    {
        if ((newmark->comment) && (Comment))
        {
            free(newmark->comment);
            newmark->comment=strdup(Comment);
        }
        return newmark;
    }

    newmark=new clMark(Position,Comment);
    if (!newmark) return NULL;

    if (!first)
    {
        //first element
        first=newmark;
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
                    // add after mark
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

bool clMarks::Load(const char *Directory,double FrameRate, bool isTS)
{
    char *fpath=NULL;
    if (asprintf(&fpath,"%s/%s%s",Directory,filename,isTS ? "" : ".vdr")==-1) return false;

    FILE *mf;
    mf=fopen(fpath,"r");
    free(fpath);
    if (!mf) return false;

    char *line=NULL;
    size_t length;
    int h, m, s, f;

    while (getline(&line,&length,mf)!=-1)
    {
        char descr[256]="";
        f=1;
        int n=sscanf(line,"%d:%d:%d.%d %80c",&h, &m, &s, &f,(char *) &descr);
        if (n==1)
        {
            Add(h);
        }
        if (n>=3)
        {
            int pos=int(round((h*3600+m*60+s)*FrameRate))+f-1;
            if (n<=4)
            {
                Add(pos);
            }
            else
            {
                char *lf=strchr(descr,10);
                if (lf) *lf=0;
                char *cr=strchr(descr,13);
                if (cr) *cr=0;
                Add(pos,descr);
            }
        }
    }
    if (line) free(line);
    fclose(mf);

    return true;
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

bool clMarks::CheckIndex(int FileDescriptor, int Index, bool isTS)
{
    // return true on error
    if (FileDescriptor==-1) return true;
    if (Index<0) return true;

    if (isTS)
    {
        off_t offset = Index * sizeof(struct tIndexTS);
        if (lseek(FileDescriptor,offset,SEEK_SET)!=offset) return true;
        struct tIndexTS IndexTS;
        if (read(FileDescriptor,&IndexTS,sizeof(IndexTS))!=sizeof(IndexTS)) return true;
        if (IndexTS.independent) return false;
    }
    else
    {
        off_t offset = Index * sizeof(struct tIndexVDR);
        if (lseek(FileDescriptor,offset,SEEK_SET)!=offset) return true;
        struct tIndexVDR IndexVDR;
        if (read(FileDescriptor,&IndexVDR,sizeof(IndexVDR))!=sizeof(IndexVDR)) return true;
        if (IndexVDR.type==1) return false;
    }
    return true;
}

bool clMarks::Save(const char *Directory, double FrameRate, bool isTS, bool Backup, bool *IndexError)
{
    if (IndexError) *IndexError=false;
    if (!first) return false;

    char *fpath=NULL;
    if (asprintf(&fpath,"%s/%s%s",Directory,filename,isTS ? "" : ".vdr")==-1) return false;

    char *ipath=NULL;
    if (asprintf(&ipath,"%s/index%s",Directory,isTS ? "" : ".vdr")==-1) ipath=NULL;

    if (Backup)
    {
        // make backup of old marks, filename convention taken from noad
        char *bpath=NULL;
        if (asprintf(&bpath,"%s/%s0%s",Directory,filename,isTS ? "" : ".vdr")!=-1)
        {
            rename(fpath,bpath);
            free(bpath);
        }
    }

    FILE *mf;
    mf=fopen(fpath,"w+");

    if (!mf)
    {
        free(fpath);
        if (ipath) free(ipath);
        return false;
    }

    int fd=-1;
    if (ipath) fd=open(ipath,O_RDONLY);

    clMark *mark=first;
    while (mark)
    {
        char *buf=IndexToHMSF(mark->position,FrameRate);
        if (buf)
        {
            fprintf(mf,"%s %s\n",buf,mark->comment ? mark->comment : "");
            free(buf);
            if ((IndexError) && (fd!=-1))
            {
                *IndexError=CheckIndex(fd,mark->position,isTS);
            }
        }
        mark=mark->Next();
    }
    fclose(mf);
    free(ipath);

    if (fd!=-1) close(fd);

    if (getuid()==0 || geteuid()!=0)
    {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath=NULL;
        if (asprintf(&spath,"%s/%s",Directory,isTS ? "00001.ts" : "001.vdr")!=-1)
        {
            struct stat statbuf;
            if (!stat(spath,&statbuf))
            {
                chown(fpath,statbuf.st_uid, statbuf.st_gid);
            }
            free(spath);
        }
    }
    free(fpath);
    return true;
}
