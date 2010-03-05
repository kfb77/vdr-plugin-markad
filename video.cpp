/*
 * video.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "video.h"

cMarkAdLogo::cMarkAdLogo(int RecvNumber, MarkAdContext *maContext)
{
    macontext=maContext;

    // 3x3 GX Sobel mask
    GX[0][0] = -1;
    GX[0][1] =  0;
    GX[0][2] =  1;
    GX[1][0] = -2;
    GX[1][1] =  0;
    GX[1][2] =  2;
    GX[2][0] = -1;
    GX[2][1] =  0;
    GX[2][2] =  1;

    // 3x3 GY Sobel mask
    GY[0][0] =  1;
    GY[0][1] =  2;
    GY[0][2] =  1;
    GY[1][0] =  0;
    GY[1][1] =  0;
    GY[1][2] =  0;
    GY[2][0] = -1;
    GY[2][1] = -2;
    GY[2][2] = -1;

    memset(&area,0,sizeof(area));

    area[TOP_LEFT].init=true;
    area[TOP_RIGHT].init=true;
    area[BOTTOM_LEFT].init=true;
    area[BOTTOM_RIGHT].init=true;

    LOGOHEIGHT=100;
    LOGOWIDTH=192;

    framecnt=0;
    savedlastiframe=-1;
    logostart=-1;
    logostate=-1;
    counter=0;
}

cMarkAdLogo::~cMarkAdLogo()
{
}

void cMarkAdLogo::SaveLogo(int corner, int lastiframe)
{
    if (!macontext) return;
    if (!macontext->Video.Info.Width) return;

    FILE *pFile;
    char szFilename[32];

    // Open file
    sprintf(szFilename, "%iframe%06d.pgm", corner,lastiframe);
    pFile=fopen(szFilename, "wb");
    if (pFile==NULL)
        return;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", LOGOWIDTH,LOGOHEIGHT);

    // Write pixel data
    fwrite(area[corner].plane,1,LOGOWIDTH*LOGOHEIGHT,pFile);
    // Close file
    fclose(pFile);
}

void cMarkAdLogo::CheckCorner(int corner)
{
    if (!macontext) return;
    if (!macontext->Video.Info.Width) return;
    if (!macontext->Video.Info.Height) return;
    if (!macontext->Video.Data.Valid) return;

    if (corner>BOTTOM_RIGHT) return;
    if (corner<TOP_LEFT) return;

    int xstart,xend,ystart,yend;

    switch (corner)
    {
    case TOP_LEFT:
        xstart=0;
        xend=LOGOWIDTH;
        ystart=0;
        yend=LOGOHEIGHT;
        break;
    case TOP_RIGHT:
        xstart=macontext->Video.Info.Width-LOGOWIDTH;
        xend=macontext->Video.Info.Width;
        ystart=0;
        yend=LOGOHEIGHT;
        break;
    case BOTTOM_LEFT:
        xstart=0;
        xend=LOGOWIDTH;
        ystart=macontext->Video.Info.Height-LOGOHEIGHT;
        yend=macontext->Video.Info.Height;
        break;
    case BOTTOM_RIGHT:
        xstart=macontext->Video.Info.Width-LOGOWIDTH;
        xend=macontext->Video.Info.Width;
        ystart=macontext->Video.Info.Height-LOGOHEIGHT;
        yend=macontext->Video.Info.Height;
        break;
    default:
        return;
    }

    int SUM;
    int sumX,sumY;
    area[corner].blackpixel=0;
    for (int Y=ystart; Y<=yend-1; Y++)
    {
        for (int X=xstart; X<=xend-1; X++)
        {
            sumX=0;
            sumY=0;

            // image boundaries
            if (Y<(ystart+15) || Y>(yend-15))
                SUM=0;
            else if (X<(xstart+15) || X>(xend-15))
                SUM=0;
            // convolution starts here
            else
            {
                // X Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumX=sumX+ (int) ((*(macontext->Video.Data.Plane[0]+X+I+
                                             (Y+J)*macontext->Video.Info.Width))*GX[I+1][J+1]);
                    }
                }

                // Y Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumY=sumY+ (int) ((*(macontext->Video.Data.Plane[0]+X+I+
                                             (Y+J)*macontext->Video.Info.Width))*GY[I+1][J+1]);
                    }
                }

                // Gradient Magnitude approximation
                SUM = abs(sumX) + abs(sumY);
            }

            if (SUM>=127) SUM=255;
            if (SUM<127) SUM=0;

            int val = 255-(uchar) SUM;

            if (area[corner].init)
            {
                area[corner].plane[(X-xstart)+(Y-ystart)*LOGOWIDTH]=val;
            }
            else
            {
                if (area[corner].plane[(X-xstart)+(Y-ystart)*LOGOWIDTH]!=val)
                {
                    area[corner].plane[(X-xstart)+(Y-ystart)*LOGOWIDTH]=255;
                }
            }

            if (area[corner].plane[(X-xstart)+(Y-ystart)*LOGOWIDTH]!=255)
                area[corner].blackpixel++;
        }

    }
    area[corner].init=false;
    if (area[corner].blackpixel<100) area[corner].blackpixel=0;
}

void cMarkAdLogo::CheckCorners(int lastiframe)
{
    for (int i=TOP_LEFT; i<=BOTTOM_RIGHT; i++)
    {
        CheckCorner(i);
//        printf("%i ",area[i].blackpixel);
//        SaveLogo(i,lastiframe);
    }
//    printf("\n");
}

void cMarkAdLogo::RestartLogoDetection()
{
    for (int i=TOP_LEFT; i<=BOTTOM_RIGHT; i++)
    {
        area[i].init=true;
//        area[i].cntfound=1;
    }
    framecnt=0;
    counter++;
}

bool cMarkAdLogo::LogoVisible()
{
    int sum=0;
    for (int i=TOP_LEFT; i<=BOTTOM_RIGHT; i++)
    {
        sum+=area[i].blackpixel;
    }
    return (sum!=0);
}

/*
void cMarkAdLogo::ResetLogoDetection()
{
    for (int i=TOP_LEFT; i<=BOTTOM_RIGHT; i++)
    {
        area[i].init=true;
        area[i].cntfound=0;
    }
    framecnt=0;
    counter=0;
}

bool cMarkAdLogo::LogoFound()
{
    for (int i=TOP_LEFT; i<=BOTTOM_RIGHT; i++)
    {
        printf("%i ",area[i].cntfound);
    }
    printf("\n");
    return false;
}
*/

int cMarkAdLogo::Process(int LastIFrame)
{
    if (!macontext) return 0;
    if (!macontext->Video.Info.Width) return 0;
    if (!macontext->Video.Data.Valid) return 0;

    if ((macontext->Video.Info.Width>720) && (LOGOWIDTH==192))
    {
        LOGOWIDTH=288;
    }

    int ret=0;
    CheckCorners(LastIFrame);
//    if (framecnt>=250) abort();
    /*
        if (framecnt>=MAXFRAMES)
        {
            if (logostate==-1)
            {
                if (LogoVisible())
                {
                    logostate=1;
                }
                else
                {
                    logostate=0;
                }
                printf("Initial logo state %i\n",logostate);
    abort();
            }
            else
            {
                if (!LogoVisible() && logostate==1)
                {
                    if (logostart==-1) logostart=LastIFrame;
                    RestartLogoDetection();
                    printf("%i\n",counter);
                    if (counter>=2)
                    {
                        printf("%i Logo gone\n",logostart);
                        logostart=-1;
                        counter=0;
                        logostate=0;
                        ret=-1;
                    }
                }
                if (LogoVisible() && logostate==0)
                {
                    if (logostart==-1) logostart=LastIFrame;
                    RestartLogoDetection();
                    printf("%i\n",counter);
                    if (counter>=2)
                    {
                        printf("%i Logo start\n",logostart);
                        logostart=-1;
                        counter=0;
                        logostate=1;
                        ret=1;
                    }
                }
            }
        }
    */
    if (savedlastiframe!=-1) framecnt+=(LastIFrame-savedlastiframe);
    savedlastiframe=LastIFrame;

    return ret;
}

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
    if (macontext->Video.Data.PlaneLinesize[0]!=macontext->Video.Info.Width) return 0;

    *BorderIFrame=borderiframe;

    int x;
    bool ftop=true,fbottom=true;

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
    logo = new cMarkAdLogo(RecvNumber,maContext);
}

cMarkAdVideo::~cMarkAdVideo()
{
    ResetMark();
    if (hborder) delete hborder;
    if (logo) delete logo;
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

    int lret=logo->Process(LastIFrame);
    if (lret!=0)
    {
        char *buf=NULL;
        if (lret>0)
        {
            if (asprintf(&buf,"detected logo start (%i)",LastIFrame)!=-1)
            {
                isyslog("markad [%i]: %s",recvnumber,buf);
                AddMark(LastIFrame,buf);
                free(buf);
            }
        }
        else
        {
            if (asprintf(&buf,"detected logo stop (%i)",LastIFrame)!=-1)
            {
                isyslog("markad [%i]: %s",recvnumber,buf);
                AddMark(LastIFrame,buf);
                free(buf);
            }
        }
    }


    int borderiframe;
    int hret=hborder->Process(LastIFrame,&borderiframe);

    if ((hret>0) && (borderiframe))
    {
        char *buf=NULL;
        if (asprintf(&buf,"detected start of horiz. borders (%i)",borderiframe)!=-1)
        {
            isyslog("markad [%i]: %s",recvnumber,buf);
            AddMark(borderiframe,buf);
            free(buf);
        }
    }

    if ((hret<0) && (borderiframe))
    {
        char *buf=NULL;
        if (asprintf(&buf,"detected stop of horiz. borders (%i)",borderiframe)!=-1)
        {
            isyslog("markad [%i]: %s",recvnumber,buf);
            AddMark(borderiframe,buf);
            free(buf);
        }
    }

    if (AspectRatioChange(&macontext->Video.Info.AspectRatio,&aspectratio))
    {
        char *buf=NULL;
        if (asprintf(&buf,"aspect ratio change from %i:%i to %i:%i (%i)",
                     aspectratio.Num,aspectratio.Den,
                     macontext->Video.Info.AspectRatio.Num,
                     macontext->Video.Info.AspectRatio.Den,LastIFrame)!=-1)
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
