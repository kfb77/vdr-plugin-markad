/*
 * video.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "video.h"

cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(int RecvNumber, MarkAdContext *maContext)
{
    macontext=maContext;

    borderstatus=false;
    borderiframe=-1;
    borderstarttime=0;
}

void cMarkAdBlackBordersHoriz::SaveFrame(int LastIFrame)
{
    if (!macontext) return;
    if (!macontext->Video.Data.Valid) return;

    if (macontext->Video.Data.PlaneLinesize[0]!=macontext->Video.Info.Width) return;

    FILE *pFile;
    char szFilename[32];

    // Open file
    sprintf(szFilename, "frame%06d.pgm", LastIFrame);
    pFile=fopen(szFilename, "wb");
    if (pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", macontext->Video.Info.Width,
            macontext->Video.Info.Width);

    // Write pixel data
    fwrite(macontext->Video.Data.Plane[0],1,macontext->Video.Info.Width*
           macontext->Video.Info.Height,pFile);
    // Close file
    fclose(pFile);
}

int cMarkAdBlackBordersHoriz::Process(int LastIFrame, int *BorderIFrame)
{
#define CHECKHEIGHT 20
#define BRIGHTNESS 20
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;

    *BorderIFrame=borderiframe;

    int x,y;
    bool ftop=true,fbottom=true;

    if (macontext->Video.Data.PlaneLinesize[0]!=macontext->Video.Info.Width)
    {
        // slow (?) method
        for (y=(macontext->Video.Info.Height-CHECKHEIGHT); y<macontext->Video.Info.Height; y++)
        {
            for (x=0; x<macontext->Video.Info.Width; x++)
            {
                if (macontext->Video.Data.Plane[0][y*macontext->Video.Data.PlaneLinesize[0]+x]>
                        BRIGHTNESS)
                {
                    fbottom=false;
                    y=macontext->Video.Info.Height;
                    break;
                }
            }
        }

        if (fbottom)
        {
            for (y=0; y<CHECKHEIGHT; y++)
            {
                for (x=0; x<macontext->Video.Info.Width; x++)
                {
                    if (macontext->Video.Data.Plane[0][y*macontext->Video.Data.PlaneLinesize[0]+x]
                            >BRIGHTNESS)
                    {
                        ftop=false;
                        y=CHECKHEIGHT;
                        break;
                    }
                }
            }
        }
    }
    else
    {
        // "fast" method
        for (x=(macontext->Video.Info.Height-CHECKHEIGHT)*macontext->Video.Info.Width;
                x<macontext->Video.Info.Height*macontext->Video.Info.Width; x++)
        {
            if (macontext->Video.Data.Plane[0][x]>BRIGHTNESS) fbottom=false;
        }

        if (fbottom)
        {
            for (x=0; x<(macontext->Video.Info.Width*CHECKHEIGHT); x++)
            {
                if (macontext->Video.Data.Plane[0][x]>BRIGHTNESS) ftop=false;
            }
        }
    }

    if ((fbottom) && (ftop))
    {
        if (!borderstatus)
        {
            if (!borderstarttime)
            {
                borderiframe=LastIFrame;
                borderstarttime=time(NULL);
                borderstatus=false;
            }
            else
            {
                if ((time(NULL)>(borderstarttime+20)))
                {
                    borderstatus=true;
                    return 1; // detected black border
                }
            }
        }
    }
    else
    {
        if (borderstatus)
        {
            borderiframe=LastIFrame;
            borderstarttime=0;
            borderstatus=false;
            return -1;
        }
        else
        {
            borderiframe=-1;
            borderstarttime=0;
            return 0;
        }
    }
    return 0;
}


cMarkAdVideo::cMarkAdVideo(int RecvNumber,MarkAdContext *maContext)
{
    macontext=maContext;
    recvnumber=RecvNumber;

    aspectratio.Num=0;
    aspectratio.Den=0;
    mark.Comment=NULL;
    mark.Position=0;

    hborder=new cMarkAdBlackBordersHoriz(RecvNumber,maContext);
}

cMarkAdVideo::~cMarkAdVideo()
{
    ResetMark();
    delete hborder;
}

void cMarkAdVideo::ResetMark()
{
    if (mark.Comment) free(mark.Comment);
    mark.Comment=NULL;
    mark.Position=0;
}

bool cMarkAdVideo::AddMark(int Position, const char *Comment)
{
    if (!Comment) return false;
    if (mark.Comment)
    {
        int oldlen=strlen(mark.Comment);
        mark.Comment=(char *) realloc(mark.Comment,oldlen+10+strlen(Comment));
        if (!mark.Comment)
        {
            mark.Position=0;
            return false;
        }
        strcat(mark.Comment," [");
        strcat(mark.Comment,Comment);
        strcat(mark.Comment,"]");
    }
    else
    {
        mark.Comment=strdup(Comment);
    }
    mark.Position=Position;
    return true;
}

bool cMarkAdVideo::AspectRatioChange(MarkAdAspectRatio *a, MarkAdAspectRatio *b)
{
    if ((!a) || (!b)) return false;

    if (a->Num==0 || a->Den==0 || b->Num==0 || b->Den==0) return false;
    if ((a->Num!=b->Num) && (a->Den!=b->Den)) return true;
    return false;

}


MarkAdMark *cMarkAdVideo::Process(int LastIFrame)
{
    ResetMark();
    if (!LastIFrame) return NULL;

    if (macontext->State.ContentStarted)
    {
        int borderiframe;
        int hret=hborder->Process(LastIFrame,&borderiframe);

        if ((hret>0) && (borderiframe))
        {
            char *buf=NULL;
            asprintf(&buf,"detected start of horiz. borders (%i)",borderiframe);
            if (buf)
            {
                dsyslog("markad [%i]: %s",recvnumber,buf);
                AddMark(borderiframe,buf);
                free(buf);
            }
        }

        if ((hret<0) && (borderiframe))
        {
            char *buf=NULL;
            asprintf(&buf,"detected stop of horiz. borders (%i)",borderiframe);
            if (buf)
            {
                dsyslog("markad [%i]: %s",recvnumber,buf);
                AddMark(borderiframe,buf);
                free(buf);
            }
        }
    }

    if (AspectRatioChange(&macontext->Video.Info.AspectRatio,&aspectratio))
    {
        char *buf=NULL;
        asprintf(&buf,"aspect ratio change from %i:%i to %i:%i (%i)",
                 aspectratio.Num,aspectratio.Den,
                 macontext->Video.Info.AspectRatio.Num,
                 macontext->Video.Info.AspectRatio.Den,LastIFrame);
        if (buf)
        {
            isyslog("markad [%i]: %s",recvnumber, buf);
            AddMark(LastIFrame,buf);
            free(buf);
        }
    }

    aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
    aspectratio.Den=macontext->Video.Info.AspectRatio.Den;

    return &mark;
}
