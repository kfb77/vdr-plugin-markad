/*
 * video.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "video.h"

cMarkAdLogo::cMarkAdLogo(MarkAdContext *maContext)
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

    LOGOHEIGHT=LOGO_DEFHEIGHT;
    if (maContext->General.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
    {
        LOGOWIDTH=LOGO_DEFHDWIDTH;
    }
    else
    {
        LOGOWIDTH=LOGO_DEFWIDTH;
    }

    area.status=UNINITIALIZED;
}

cMarkAdLogo::~cMarkAdLogo()
{
}

int cMarkAdLogo::Load(char *file)
{
    // Load mask
    FILE *pFile;
    area.valid=false;
    area.corner=-1;
    pFile=fopen(file, "rb");
    if (!pFile) return -1;

    fscanf(pFile, "P5\n#C%i %i\n%d %d\n255\n#", &area.corner,&area.mpixel,&LOGOWIDTH,&LOGOHEIGHT);

    if (LOGOHEIGHT==255)
    {
        LOGOHEIGHT=LOGOWIDTH;
        LOGOWIDTH=area.mpixel;
        area.mpixel=0;
    }

    if ((LOGOWIDTH<=0) || (LOGOHEIGHT<=0) || (LOGOWIDTH>LOGO_MAXWIDTH) || (LOGOHEIGHT>LOGO_MAXHEIGHT) ||
            (area.corner<TOP_LEFT) || (area.corner>BOTTOM_RIGHT))
    {
        fclose(pFile);
        return -2;
    }

    fread(&area.mask,1,LOGOWIDTH*LOGOHEIGHT,pFile);

    if (!area.mpixel)
    {
        for (int i=0; i<LOGOWIDTH*LOGOHEIGHT; i++)
        {
            if (!area.mask[i]) area.mpixel++;
        }
    }

    fclose(pFile);
    area.valid=true;
    return 0;
}



void cMarkAdLogo::Save(int lastiframe, uchar *picture)
{
    if (!macontext) return;


    char *buf=NULL;
    if (asprintf(&buf,"%s/%06d-%s-A%i_%i.pgm","/tmp/",lastiframe,
                 macontext->General.ChannelID,
                 area.aspectratio.Num,area.aspectratio.Den)!=-1)
    {
        // Open file
        FILE *pFile=fopen(buf, "wb");
        if (pFile==NULL)
        {
            free(buf);
            return;
        }

        // Write header
        fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", area.corner, LOGOWIDTH,LOGOHEIGHT);

        // Write pixel data
        fwrite(picture,1,LOGOWIDTH*LOGOHEIGHT,pFile);
        // Close file
        fclose(pFile);
        free(buf);
    }
}

int cMarkAdLogo::Detect(int lastiframe, int *logoiframe)
{
    // Detection is made with Sobel-Operator

    if (!macontext) return 0;
    if (!macontext->Video.Info.Width) return 0;
    if (!macontext->Video.Info.Height) return 0;
    if (!macontext->Video.Data.Valid) return 0;

    if (area.corner>BOTTOM_RIGHT) return 0;
    if (area.corner<TOP_LEFT) return 0;

    int xstart,xend,ystart,yend;

    switch (area.corner)
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
        return 0;
    }

    int SUMA=0;
    for (int Y=ystart; Y<=yend-1; Y++)
    {
        for (int X=xstart; X<=xend-1; X++)
        {
            int val=macontext->Video.Data.Plane[0][X+(Y*macontext->Video.Data.PlaneLinesize[0])];
            area.source[(X-xstart)+(Y-ystart)*LOGOWIDTH]=val;
            SUMA+=val;
        }
    }

    SUMA/=(LOGOWIDTH*LOGOHEIGHT);
#if 0
    if (SUMA>=100)
    {
        int maxval=(int) SUMA;
        SUMA=0;
        for (int Y=ystart; Y<=yend-1; Y++)
        {
            for (int X=xstart; X<=xend-1; X++)
            {
                int val=macontext->Video.Data.Plane[0][X+(Y*macontext->Video.Data.PlaneLinesize[0])];
                val=(int) (((double) val- (double) maxval/1.4)*1.4);
                if (val>maxval) val=maxval;
                if (val<0) val=0;
                area.source[(X-xstart)+(Y-ystart)*LOGOWIDTH]=val;
                SUMA+=val;
            }
        }
        SUMA/=(LOGOWIDTH*LOGOHEIGHT);
    }
#endif
    int ret=NOCHANGE;

    if (SUMA<100)
    {

        int SUM;
        int sumX,sumY;
        area.rpixel=0;
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
                                                 (Y+J)*macontext->Video.Data.PlaneLinesize[0]))
                                              *GX[I+1][J+1]);
                        }
                    }

                    // Y Gradient approximation
                    for (int I=-1; I<=1; I++)
                    {
                        for (int J=-1; J<=1; J++)
                        {
                            sumY=sumY+ (int) ((*(macontext->Video.Data.Plane[0]+X+I+
                                                 (Y+J)*macontext->Video.Data.PlaneLinesize[0]))*
                                              GY[I+1][J+1]);
                        }
                    }

                    // Gradient Magnitude approximation
                    SUM = abs(sumX) + abs(sumY);
                }

                if (SUM>=127) SUM=255;
                if (SUM<127) SUM=0;

                int val = 255-(uchar) SUM;

                area.sobel[(X-xstart)+(Y-ystart)*LOGOWIDTH]=val;

                area.result[(X-xstart)+(Y-ystart)*LOGOWIDTH]=
                    (area.mask[(X-xstart)+(Y-ystart)*LOGOWIDTH] + val) & 255;

                if (!area.result[(X-xstart)+(Y-ystart)*LOGOWIDTH]) area.rpixel++;

            }
        }
        if (macontext->StandAlone.LogoExtraction==-1)
        {
            if (area.status==UNINITIALIZED)
            {
                // Initialize
                if (area.rpixel>(area.mpixel*LOGO_VMARK))
                {
                    area.status=LOGO;
                }
                else
                {
                    area.status=NOLOGO;
                }
            }

            if (area.rpixel>=(area.mpixel*LOGO_VMARK))
            {
                if (area.status==NOLOGO)
                {
                    if (area.counter>=LOGO_VMAXCOUNT)
                    {
                        area.status=ret=LOGO;
                        *logoiframe=area.lastiframe;
                        area.counter=0;
                    }
                    else
                    {
                        if (!area.counter) area.lastiframe=lastiframe;
                        area.counter++;
                    }
                }
                else
                {
                    area.counter=0;
                }
            }

            if (area.rpixel<(area.mpixel*LOGO_IMARK))
            {
                if (area.status==LOGO)
                {
                    if (area.counter>=LOGO_IMAXCOUNT)
                    {
                        area.status=ret=NOLOGO;
                        *logoiframe=area.lastiframe;
                        area.counter=0;
                    }
                    else
                    {
                        if (!area.counter) area.lastiframe=lastiframe;
                        area.counter++;
                    }
                }
                else
                {
                    area.counter=0;
                }
            }

            if ((area.rpixel<(area.mpixel*LOGO_VMARK)) && (area.rpixel>(area.mpixel*LOGO_IMARK)))
            {
                area.counter=0;
            }

            //printf("%5i %3i %4i %4i %i %i %i\n",lastiframe,SUMA,area.rpixel,area.mpixel,
            //       (area.rpixel>=(area.mpixel*LOGO_VMARK)),(area.rpixel<(area.mpixel*LOGO_IMARK)),
            //       area.counter  );
            //Save(lastiframe,area.sobel); // TODO: JUST FOR DEBUGGING!
        }
        else
        {
            Save(lastiframe,area.sobel);
        }
    }
    return ret;
}

int cMarkAdLogo::Process(int LastIFrame, int *LogoIFrame)
{
    if (!macontext) return ERROR;
    if (!macontext->Video.Data.Valid) return ERROR;
    if (!macontext->Video.Info.Width) return ERROR;
    if (!macontext->LogoDir) return ERROR;
    if (!macontext->General.ChannelID) return ERROR;

    if (macontext->StandAlone.LogoExtraction==-1)
    {

        if ((area.aspectratio.Num!=macontext->Video.Info.AspectRatio.Num) ||
                (area.aspectratio.Den!=macontext->Video.Info.AspectRatio.Den))
        {
            area.valid=false;  // just to be sure!
            char *buf=NULL;
            if (asprintf(&buf,"%s/%s-A%i_%i.pgm",macontext->LogoDir,macontext->General.ChannelID,
                         macontext->Video.Info.AspectRatio.Num,macontext->Video.Info.AspectRatio.Den)!=-1)
            {
                int ret=Load(buf);
                switch (ret)
                {
                case -1:
                    esyslog("failed to open %s",buf);

                    break;

                case -2:
                    esyslog("format error in %s",buf);
                    break;
                }
                free(buf);
            }
            area.aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
            area.aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
        }
    }
    else
    {
        area.aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
        area.aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
        area.corner=macontext->StandAlone.LogoExtraction;
        if (macontext->StandAlone.LogoWidth!=-1)
        {
            LOGOWIDTH=macontext->StandAlone.LogoWidth;
        }
        if (macontext->StandAlone.LogoHeight!=-1)
        {
            LOGOHEIGHT=macontext->StandAlone.LogoHeight;
        }
        area.valid=true;
    }

    if (!area.valid) return ERROR;

    return Detect(LastIFrame,LogoIFrame);
}

cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(MarkAdContext *maContext)
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

    int x;
    bool ftop=true,fbottom=true;

    // "fast" method
    for (x=(macontext->Video.Info.Height-CHECKHEIGHT)*macontext->Video.Data.PlaneLinesize[0];
            x<macontext->Video.Info.Height*macontext->Video.Data.PlaneLinesize[0]; x++)
    {
        if (macontext->Video.Data.Plane[0][x]>BRIGHTNESS) fbottom=false;
    }

    if (fbottom)
    {
        for (x=0; x<(macontext->Video.Data.PlaneLinesize[0]*CHECKHEIGHT); x++)
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


cMarkAdVideo::cMarkAdVideo(MarkAdContext *maContext)
{
    macontext=maContext;

    aspectratio.Num=0;
    aspectratio.Den=0;
    mark.Comment=NULL;
    mark.Position=0;
    mark.Type=0;

    hborder=new cMarkAdBlackBordersHoriz(maContext);
    logo = new cMarkAdLogo(maContext);
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
    mark.Type=0;
}

bool cMarkAdVideo::AddMark(int Type, int Position, const char *Comment)
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
    mark.Type=Type;
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

    int logoiframe;
    int lret=logo->Process(LastIFrame,&logoiframe);
    if ((lret>=-1) && (lret!=0))
    {
        char *buf=NULL;
        if (lret>0)
        {
            if (asprintf(&buf,"detected logo start (%i)",logoiframe)!=-1)
            {
                isyslog(buf);
                AddMark(MT_LOGOSTART,logoiframe,buf);
                free(buf);
            }
        }
        else
        {
            if (asprintf(&buf,"detected logo stop (%i)",logoiframe)!=-1)
            {
                isyslog(buf);
                AddMark(MT_LOGOSTOP,logoiframe,buf);
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
            isyslog(buf);
            AddMark(MT_BORDERSTART,borderiframe,buf);
            free(buf);
        }
    }

    if ((hret<0) && (borderiframe))
    {
        char *buf=NULL;
        if (asprintf(&buf,"detected stop of horiz. borders (%i)",borderiframe)!=-1)
        {
            isyslog(buf);
            AddMark(MT_BORDERSTOP,borderiframe,buf);
            free(buf);
        }
    }

    if (!macontext->Video.Options.IgnoreAspectRatio)
    {
        if (AspectRatioChange(&macontext->Video.Info.AspectRatio,&aspectratio))
        {
            char *buf=NULL;
            if (asprintf(&buf,"aspect ratio change from %i:%i to %i:%i (%i)",
                         aspectratio.Num,aspectratio.Den,
                         macontext->Video.Info.AspectRatio.Num,
                         macontext->Video.Info.AspectRatio.Den,LastIFrame)!=-1)
            {
                isyslog(buf);
                AddMark(MT_ASPECTCHANGE,LastIFrame,buf);
                free(buf);
            }
        }

        aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
        aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
    }
    return &mark;
}
