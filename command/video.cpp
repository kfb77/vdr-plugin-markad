/*
 * video.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <time.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C"
{
#include "debug.h"
}

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

    if (maContext->Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264)
    {
        LOGOHEIGHT=LOGO_DEFHDHEIGHT;
        LOGOWIDTH=LOGO_DEFHDWIDTH;
    }
    else
    {
        LOGOHEIGHT=LOGO_DEFHEIGHT;
        LOGOWIDTH=LOGO_DEFWIDTH;
    }

    pixfmt_info=false;
    Clear();
}

void cMarkAdLogo::Clear()
{
    memset(&area,0,sizeof(area));
    area.status=LOGO_UNINITIALIZED;
}

int cMarkAdLogo::Load(const char *directory, char *file, int plane)
{
    if ((plane<0) || (plane>3)) return -3;

    char *path;
    if (asprintf(&path,"%s/%s-P%i.pgm",directory,file,plane)==-1) return -3;

    // Load mask
    FILE *pFile;
    area.valid[plane]=false;
    pFile=fopen(path, "rb");
    free(path);
    if (!pFile)
    {
        if (plane>0) return 0; // only report for plane0
        return -1;
    }

    int width,height;
    char c;
    if (fscanf(pFile, "P5\n#%1c%1i %4i\n%3d %3d\n255\n#", &c,&area.corner,&area.mpixel[plane],&width,&height)!=5)
    {
        fclose(pFile);
        return -2;
    }
    if (c=='D') macontext->Audio.Options.IgnoreDolbyDetection=true;

    if (height==255)
    {
        height=width;
        width=area.mpixel[plane];
        area.mpixel[plane]=0;
    }

    if ((width<=0) || (height<=0) || (width>LOGO_MAXWIDTH) || (height>LOGO_MAXHEIGHT) ||
            (area.corner<TOP_LEFT) || (area.corner>BOTTOM_RIGHT))
    {
        fclose(pFile);
        return -2;
    }

    if (fread(&area.mask[plane],1,width*height,pFile)!=(size_t) (width*height))
    {
        fclose(pFile);
        return -2;
    }
    fclose(pFile);

    if (!area.mpixel[plane])
    {
        for (int i=0; i<width*height; i++)
        {
            if (!area.mask[plane][i]) area.mpixel[plane]++;
        }
    }

    if (!plane)
    {
        // plane 0 is the largest -> use this values
        LOGOWIDTH=width;
        LOGOHEIGHT=height;
    }

    area.valid[plane]=true;
    return 0;
}



void cMarkAdLogo::Save(int framenumber, uchar picture[4][MAXPIXEL], int plane)
{
    if (!macontext) return;
    if ((plane<0) || (plane>3)) return;
    if (!macontext->Info.ChannelName) return;
    if (!macontext->Video.Info.Width) return;
    if (!macontext->Video.Info.Height) return;
    if (!macontext->Video.Data.Valid) return;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return;

    char *buf=NULL;
    if (asprintf(&buf,"%s/%06d-%s-A%i_%i-P%i.pgm","/tmp/",framenumber,
                 macontext->Info.ChannelName,
                 area.aspectratio.Num,area.aspectratio.Den,plane)==-1) return;

    // Open file
    FILE *pFile=fopen(buf, "wb");
    if (pFile==NULL)
    {
        free(buf);
        return;
    }

    int width=LOGOWIDTH;
    int height=LOGOHEIGHT;

    if (plane>0)
    {
        width/=2;
        height/=2;
    }

    // Write header
    fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", area.corner,width,height);

    // Write pixel data
    if (fwrite(picture[plane],1,width*height,pFile)) {};
    // Close file
    fclose(pFile);
    free(buf);
}

int cMarkAdLogo::SobelPlane(int plane)
{
    if ((plane<0) || (plane>3)) return 0;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return 0;

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

    if ((macontext->Video.Info.Pix_Fmt!=0) && (macontext->Video.Info.Pix_Fmt!=12))
    {
        if (!pixfmt_info)
        {
            esyslog("unknown pix_fmt %i, please report!",macontext->Video.Info.Pix_Fmt);
            pixfmt_info=true;
        }
        return 0;
    }

    int boundary=6;
    int cutval=127;
    //int cutval=32;
    int width=LOGOWIDTH;

    if (plane>0)
    {
        xstart/=2;
        xend/=2;
        ystart/=2;
        yend/=2;
        boundary/=2;
        cutval/=2;
        width/=2;
    }

    int SUM;
    int sumX,sumY;
    area.rpixel[plane]=0;
    if (!plane) area.intensity=0;
    for (int Y=ystart; Y<=yend-1; Y++)
    {
        for (int X=xstart; X<=xend-1; X++)
        {
            if (!plane)
            {
                area.intensity+=macontext->Video.Data.Plane[plane][X+(Y*macontext->Video.Data.PlaneLinesize[plane])];
            }
            sumX=0;
            sumY=0;

            // image boundaries
            if (Y<(ystart+boundary) || Y>(yend-boundary))
                SUM=0;
            else if (X<(xstart+boundary) || X>(xend-boundary))
                SUM=0;
            // convolution starts here
            else
            {
                // X Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumX=sumX+ (int) ((*(macontext->Video.Data.Plane[plane]+X+I+
                                             (Y+J)*macontext->Video.Data.PlaneLinesize[plane]))
                                          *GX[I+1][J+1]);
                    }
                }

                // Y Gradient approximation
                for (int I=-1; I<=1; I++)
                {
                    for (int J=-1; J<=1; J++)
                    {
                        sumY=sumY+ (int) ((*(macontext->Video.Data.Plane[plane]+X+I+
                                             (Y+J)*macontext->Video.Data.PlaneLinesize[plane]))*
                                          GY[I+1][J+1]);
                    }
                }

                // Gradient Magnitude approximation
                SUM = abs(sumX) + abs(sumY);
            }

            if (SUM>=cutval) SUM=255;
            if (SUM<cutval) SUM=0;

            int val = 255-(uchar) SUM;

            area.sobel[plane][(X-xstart)+(Y-ystart)*width]=val;

            area.result[plane][(X-xstart)+(Y-ystart)*width]=
                (area.mask[plane][(X-xstart)+(Y-ystart)*width] + val) & 255;

            if (!area.result[plane][(X-xstart)+(Y-ystart)*width]) area.rpixel[plane]++;
#ifdef VDRDEBUG
            val=macontext->Video.Data.Plane[plane][X+(Y*macontext->Video.Data.PlaneLinesize[plane])];
            area.source[plane][(X-xstart)+(Y-ystart)*width]=val;
#endif

        }
    }
    if (!plane) area.intensity/=(LOGOHEIGHT*width);

    return 1;
}

int cMarkAdLogo::Detect(int framenumber, int *logoframenumber)
{
    bool extract=(macontext->Config->logoExtraction!=-1);
    int rpixel=0,mpixel=0;
    int processed=0;
    *logoframenumber=-1;
    if (area.corner==-1) return LOGO_NOCHANGE;

    for (int plane=0; plane<4; plane++)
    {
        if ((area.valid[plane]) || (extract))
        {
            if (SobelPlane(plane)) processed++;
        }
        if (extract)
        {
            Save(framenumber,area.sobel,plane);
        }
        else
        {
            rpixel+=area.rpixel[plane];
            mpixel+=area.mpixel[plane];
        }
    }
    if (extract) return LOGO_NOCHANGE;
    if (!processed) return LOGO_ERROR;

    //tsyslog("rp=%5i mp=%5i mpV=%5.f mpI=%5.f i=%3i s=%i",rpixel,mpixel,(mpixel*LOGO_VMARK),(mpixel*LOGO_IMARK),area.intensity,area.status);

    if (processed==1)
    {
        // if we only have one plane we are "vulnerable"
        // to very bright pictures, so ignore them...
        if (area.intensity>180) return LOGO_NOCHANGE;
    }

    int ret=LOGO_NOCHANGE;
    if (area.status==LOGO_UNINITIALIZED)
    {
        // Initialize
        if (rpixel>=(mpixel*LOGO_VMARK))
        {
            area.status=ret=LOGO_VISIBLE;
        }
        else
        {
            area.status=LOGO_INVISIBLE;
        }
        area.framenumber=framenumber;
        *logoframenumber=framenumber;
    }

    if (rpixel>=(mpixel*LOGO_VMARK))
    {
        if (area.status==LOGO_INVISIBLE)
        {
            if (area.counter>=LOGO_VMAXCOUNT)
            {
                area.status=ret=LOGO_VISIBLE;
                *logoframenumber=area.framenumber;
                area.counter=0;
            }
            else
            {
                if (!area.counter) area.framenumber=framenumber;
                area.counter++;
            }
        }
        else
        {
            area.framenumber=framenumber;
            area.counter=0;
        }
    }

    if (rpixel<(mpixel*LOGO_IMARK))
    {
        if (area.status==LOGO_VISIBLE)
        {
            if (area.counter>=LOGO_IMAXCOUNT)
            {
                area.status=ret=LOGO_INVISIBLE;
                *logoframenumber=area.framenumber;
                area.counter=0;
            }
            else
            {
                if (!area.counter) area.framenumber=framenumber;
                area.counter++;
            }
        }
        else
        {
            area.counter=0;
        }
    }

    if ((rpixel<(mpixel*LOGO_VMARK)) && (rpixel>(mpixel*LOGO_IMARK)))
    {
        area.counter=0;
    }
    return ret;
}

int cMarkAdLogo::Process(int FrameNumber, int *LogoFrameNumber)
{
    if (!macontext) return LOGO_ERROR;
    if (!macontext->Video.Data.Valid)
    {
        area.status=LOGO_UNINITIALIZED;
        return LOGO_ERROR;
    }
    if (!macontext->Video.Info.Width) return LOGO_ERROR;
    if (!macontext->Video.Info.Height) return LOGO_ERROR;
    if (!macontext->Config->logoDirectory[0]) return LOGO_ERROR;
    if (!macontext->Info.ChannelName) return LOGO_ERROR;

    if (macontext->Config->logoExtraction==-1)
    {
        if ((area.aspectratio.Num!=macontext->Video.Info.AspectRatio.Num) ||
                (area.aspectratio.Den!=macontext->Video.Info.AspectRatio.Den))
        {
            char *buf=NULL;
            if (asprintf(&buf,"%s-A%i_%i",macontext->Info.ChannelName,
                         macontext->Video.Info.AspectRatio.Num,macontext->Video.Info.AspectRatio.Den)!=-1)
            {
                area.corner=-1;
                for (int plane=0; plane<4; plane++)
                {
                    int ret=Load(macontext->Config->logoDirectory,buf,plane);
                    switch (ret)
                    {
                    case -1:
                        isyslog("no logo for %s",buf);
                        break;
                    case -2:
                        esyslog("format error in %s",buf);
                        break;
                    case -3:
                        esyslog("cannot load %s",buf);
                        break;
                    }
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
        area.corner=macontext->Config->logoExtraction;
        if (macontext->Config->logoWidth!=-1)
        {
            LOGOWIDTH=macontext->Config->logoWidth;
        }
        if (macontext->Config->logoHeight!=-1)
        {
            LOGOHEIGHT=macontext->Config->logoHeight;
        }
    }
    return Detect(FrameNumber,LogoFrameNumber);
}

cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(MarkAdContext *maContext)
{
    macontext=maContext;
    Clear();
}

void cMarkAdBlackBordersHoriz::Clear()
{
    borderstatus=HBORDER_UNINITIALIZED;
    borderframenumber=-1;
}

int cMarkAdBlackBordersHoriz::Process(int FrameNumber, int *BorderIFrame)
{
#define CHECKHEIGHT 20
#define BRIGHTNESS 20
#define VOFFSET 5
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;
    if (macontext->Video.Info.FramesPerSecond==0) return 0;
    // Assumption: If we have 4:3, we should have aspectratio-changes!
    //if (macontext->Video.Info.AspectRatio.Num==4) return 0; // seems not to be true in all countries?
    *BorderIFrame=0;

    int height=macontext->Video.Info.Height-VOFFSET;

    int start=(height-CHECKHEIGHT)*macontext->Video.Data.PlaneLinesize[0];
    int end=height*macontext->Video.Data.PlaneLinesize[0];
    bool ftop=true,fbottom=true;
    int val=0,cnt=0,xz=0;

    for (int x=start; x<end; x++)
    {
        if (xz<macontext->Video.Info.Width)
        {
            val+=macontext->Video.Data.Plane[0][x];
            cnt++;
        }
        xz++;
        if (xz>=macontext->Video.Data.PlaneLinesize[0]) xz=0;
    }
    val/=cnt;
    if (val>BRIGHTNESS) fbottom=false;

    if (fbottom)
    {
        start=VOFFSET*macontext->Video.Data.PlaneLinesize[0];
        end=macontext->Video.Data.PlaneLinesize[0]*(CHECKHEIGHT+VOFFSET);
        val=0;
        cnt=0;
        xz=0;
        for (int x=start; x<end; x++)
        {
            if (xz<macontext->Video.Info.Width)
            {
                val+=macontext->Video.Data.Plane[0][x];
                cnt++;
            }
            xz++;
            if (xz>=macontext->Video.Data.PlaneLinesize[0]) xz=0;
        }
        val/=cnt;
        if (val>BRIGHTNESS) ftop=false;
    }

    if ((fbottom) && (ftop)) {
        if (borderframenumber==-1) {
            borderframenumber=FrameNumber;
        } else {
            if (borderstatus!=HBORDER_VISIBLE) {
                if (FrameNumber>(borderframenumber+macontext->Video.Info.FramesPerSecond*MINBORDERSECS))
                {
                    *BorderIFrame=borderframenumber;
                    borderstatus=HBORDER_VISIBLE;
                    return 1; // detected start of black border
                }
            }
        }
    } else {
        if (borderstatus==HBORDER_VISIBLE)
        {
            *BorderIFrame=FrameNumber;
            borderstatus=HBORDER_INVISIBLE;
            borderframenumber=-1;
            return -1; // detected stop of black border
        } else {
            borderframenumber=-1; // restart from scratch
        }
    }
    return 0;
}

cMarkAdBlackBordersVert::cMarkAdBlackBordersVert(MarkAdContext *maContext)
{
    macontext=maContext;
    Clear();
}

void cMarkAdBlackBordersVert::Clear()
{
    borderstatus=VBORDER_UNINITIALIZED;
    borderframenumber=-1;
}

int cMarkAdBlackBordersVert::Process(int FrameNumber, int *BorderIFrame)
{
#define CHECKWIDTH 32
#define BRIGHTNESS 20
#define HOFFSET 50
#define VOFFSET_ 120
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;
    if (macontext->Video.Info.FramesPerSecond==0) return 0;
    // Assumption: If we have 4:3, we should have aspectratio-changes!
    //if (macontext->Video.Info.AspectRatio.Num==4) return 0; // seems not to be true in all countries?
    *BorderIFrame=0;

    bool fleft=true,fright=true;
    int val=0,cnt=0;

    int end=macontext->Video.Data.PlaneLinesize[0]*(macontext->Video.Info.Height-VOFFSET_);
    int i=VOFFSET_*macontext->Video.Data.PlaneLinesize[0];
    while (i<end) {
        for (int x=0; x<CHECKWIDTH; x++)
        {
            val+=macontext->Video.Data.Plane[0][HOFFSET+x+i];
            cnt++;
        }
        i+=macontext->Video.Data.PlaneLinesize[0];
    }
    val/=cnt;
    if (val>BRIGHTNESS) fleft=false;

    if (fleft)
    {
        val=cnt=0;
        i=VOFFSET_*macontext->Video.Data.PlaneLinesize[0];
        int w=macontext->Video.Info.Width-HOFFSET-CHECKWIDTH;
        while (i<end) {
            for (int x=0; x<CHECKWIDTH; x++)
            {
                val+=macontext->Video.Data.Plane[0][w+x+i];
                cnt++;
            }
            i+=macontext->Video.Data.PlaneLinesize[0];
        }
        val/=cnt;
        if (val>BRIGHTNESS) fright=false;
    }

    if ((fleft) && (fright)) {
        if (borderframenumber==-1) {
            borderframenumber=FrameNumber;
        } else {
            if (borderstatus!=VBORDER_VISIBLE) {
                if (FrameNumber>(borderframenumber+macontext->Video.Info.FramesPerSecond*MINBORDERSECS))
                {
                    *BorderIFrame=borderframenumber;
                    borderstatus=VBORDER_VISIBLE;
                    return 1; // detected start of black border
                }
            }
        }
    } else {
        if (borderstatus==VBORDER_VISIBLE)
        {
            *BorderIFrame=FrameNumber;
            borderstatus=VBORDER_INVISIBLE;
            borderframenumber=-1;
            return -1; // detected stop of black border
        } else {
            borderframenumber=-1; // restart from scratch
        }
    }
    return 0;
}

cMarkAdOverlap::cMarkAdOverlap(MarkAdContext *maContext)
{
    macontext=maContext;

    histbuf[OV_BEFORE]=NULL;
    histbuf[OV_AFTER]=NULL;
    Clear();
}

cMarkAdOverlap::~cMarkAdOverlap()
{
    Clear();
}

void cMarkAdOverlap::Clear()
{
    histcnt[OV_BEFORE]=0;
    histcnt[OV_AFTER]=0;
    histframes[OV_BEFORE]=0;
    histframes[OV_AFTER]=0;
    if (histbuf[OV_BEFORE])
    {
        delete[] histbuf[OV_BEFORE];
        histbuf[OV_BEFORE]=NULL;
    }
    if (histbuf[OV_AFTER])
    {
        delete[] histbuf[OV_AFTER];
        histbuf[OV_AFTER]=NULL;
    }
    memset(&result,0,sizeof(result));
    similarCutOff=0;
    similarMaxCnt=0;

    lastframenumber=-1;
}

void cMarkAdOverlap::getHistogram(simpleHistogram &dest)
{
    memset(dest,0,sizeof(simpleHistogram));
    for (int Y=0; Y<macontext->Video.Info.Height;Y++)
    {
        for (int X=0; X<macontext->Video.Info.Width;X++)
        {
            uchar val=macontext->Video.Data.Plane[0][X+(Y*macontext->Video.Data.PlaneLinesize[0])];
            dest[val]++;
        }
    }
}

bool cMarkAdOverlap::areSimilar(simpleHistogram &hist1, simpleHistogram &hist2)
{
    int similar=0;
    for (int i=0; i<256; i++)
    {
        similar+=abs(hist1[i]-hist2[i]);
    }
    //printf("%6i\n",similar);
    if (similar<similarCutOff) return true;
    return false;
}

MarkAdPos *cMarkAdOverlap::Detect()
{
    int start=0,simcnt=0;
    int tmpA=0,tmpB=0;
    if (result.FrameNumberBefore==-1) return NULL;
    result.FrameNumberBefore=-1;
    for (int B=0; B<histcnt[OV_BEFORE]; B++)
    {
        for (int A=start; A<histcnt[OV_AFTER]; A++)
        {
            //printf("%6i %6i ",histbuf[OV_BEFORE][B].framenumber,histbuf[OV_AFTER][A].framenumber);
            bool simil=areSimilar(histbuf[OV_BEFORE][B].histogram,histbuf[OV_AFTER][A].histogram);
            if (simil)
            {
                tmpA=A;
                tmpB=B;
                start=A+1;
                if (simil<(similarCutOff/2)) simcnt+=2;
                else if (simil<(similarCutOff/4)) simcnt+=4;
                else if (simil<(similarCutOff/6)) simcnt+=6;
                else simcnt++;
                break;
            }
            else
            {
                //if (simcnt) printf("simcnt=%i\n",simcnt);

                if (simcnt>similarMaxCnt)
                {
                    if ((histbuf[OV_BEFORE][tmpB].framenumber>result.FrameNumberBefore) &&
                            (histbuf[OV_AFTER][tmpA].framenumber>result.FrameNumberAfter))
                    {
                        result.FrameNumberBefore=histbuf[OV_BEFORE][tmpB].framenumber;
                        result.FrameNumberAfter=histbuf[OV_AFTER][tmpA].framenumber;
                    }
                }
                else
                {
                    start=0;
                }
                simcnt=0;
            }
        }
    }
    if (result.FrameNumberBefore==-1)
    {
        if (simcnt>similarMaxCnt)
        {
            result.FrameNumberBefore=histbuf[OV_BEFORE][tmpB].framenumber;
            result.FrameNumberAfter=histbuf[OV_AFTER][tmpA].framenumber;
        }
        else
        {
            return NULL;
        }
    }
    return &result;
}

MarkAdPos *cMarkAdOverlap::Process(int FrameNumber, int Frames, bool BeforeAd, bool H264)
{
    if ((lastframenumber>0) && (!similarMaxCnt))
    {
        similarCutOff=50000; // lower is harder!
        if (H264) similarCutOff*=6;
        similarMaxCnt=4;
    }

    if (BeforeAd)
    {
        if ((histframes[OV_BEFORE]) && (histcnt[OV_BEFORE]>=histframes[OV_BEFORE]))
        {
            if (result.FrameNumberBefore)
            {
                Clear();
            }
            else
            {
                return NULL;
            }
        }
        if (!histbuf[OV_BEFORE])
        {
            histframes[OV_BEFORE]=Frames;
            histbuf[OV_BEFORE]=new histbuffer[Frames+1];
        }
        getHistogram(histbuf[OV_BEFORE][histcnt[OV_BEFORE]].histogram);
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].framenumber=FrameNumber;
        histcnt[OV_BEFORE]++;
    }
    else
    {
        if (!histbuf[OV_AFTER])
        {
            histframes[OV_AFTER]=Frames;
            histbuf[OV_AFTER]=new histbuffer[Frames+1];
        }

        if (histcnt[OV_AFTER]>=histframes[OV_AFTER]-1)
        {
            if (result.FrameNumberBefore) return NULL;
            return Detect();
        }
        getHistogram(histbuf[OV_AFTER][histcnt[OV_AFTER]].histogram);
        histbuf[OV_AFTER][histcnt[OV_AFTER]].framenumber=FrameNumber;
        histcnt[OV_AFTER]++;
    }
    lastframenumber=FrameNumber;
    return NULL;
}

cMarkAdVideo::cMarkAdVideo(MarkAdContext *maContext)
{
    macontext=maContext;

    memset(&marks,0,sizeof(marks));

    hborder=new cMarkAdBlackBordersHoriz(maContext);
    vborder=new cMarkAdBlackBordersVert(maContext);
    logo = new cMarkAdLogo(maContext);
    overlap = NULL;
    Clear();
}

cMarkAdVideo::~cMarkAdVideo()
{
    resetmarks();
    if (hborder) delete hborder;
    if (vborder) delete vborder;
    if (logo) delete logo;
    if (overlap) delete overlap;
}

void cMarkAdVideo::Clear()
{
    aspectratio.Num=0;
    aspectratio.Den=0;
    framelast=0;
    framebeforelast=0;
    if (hborder) hborder->Clear();
    if (vborder) vborder->Clear();
    if (logo) logo->Clear();
}

void cMarkAdVideo::resetmarks()
{
    memset(&marks,0,sizeof(marks));
}

bool cMarkAdVideo::addmark(int type, int position, MarkAdAspectRatio *before,
                           MarkAdAspectRatio *after)
{
    if (marks.Count>marks.maxCount) return false;
    if (before)
    {
        marks.Number[marks.Count].AspectRatioBefore.Num=before->Num;
        marks.Number[marks.Count].AspectRatioBefore.Den=before->Den;
    }
    if (after)
    {
        marks.Number[marks.Count].AspectRatioAfter.Num=after->Num;
        marks.Number[marks.Count].AspectRatioAfter.Den=after->Den;
    }
    marks.Number[marks.Count].Position=position;
    marks.Number[marks.Count].Type=type;
    marks.Count++;
    return true;
}

bool cMarkAdVideo::aspectratiochange(MarkAdAspectRatio &a, MarkAdAspectRatio &b, bool &start)
{
    start=false;
    if (a.Num==0 || a.Den==0 || b.Num==0 || b.Den==0)
    {
        if (((a.Num==4) || (b.Num==4)) && ((a.Den==3) || (b.Den==3)))
        {
            start=true;
        }
        else
        {
            return false;
        }
    }
    if ((a.Num!=b.Num) && (a.Den!=b.Den)) return true;
    return false;

}

MarkAdPos *cMarkAdVideo::ProcessOverlap(int FrameNumber, int Frames, bool BeforeAd, bool H264)
{
    if (!FrameNumber) return NULL;
    if (!overlap) overlap=new cMarkAdOverlap(macontext);
    if (!overlap) return NULL;

    return overlap->Process(FrameNumber, Frames, BeforeAd, H264);
}

MarkAdMarks *cMarkAdVideo::Process(int FrameNumber, int FrameNumberNext)
{
    if ((!FrameNumber) && (!FrameNumberNext)) return NULL;

    resetmarks();

    int hborderframenumber;
    int hret=hborder->Process(FrameNumber,&hborderframenumber);

    if ((hret>0) && (hborderframenumber!=-1))
    {
        addmark(MT_HBORDERSTART,hborderframenumber);
    }

    if ((hret<0) && (hborderframenumber!=-1))
    {
        addmark(MT_HBORDERSTOP,hborderframenumber);
    }

    int vborderframenumber;
    int vret=vborder->Process(FrameNumber,&vborderframenumber);

    if ((vret>0) && (vborderframenumber!=-1))
    {
        addmark(MT_VBORDERSTART,vborderframenumber);
    }

    if ((vret<0) && (vborderframenumber!=-1))
    {
        addmark(MT_VBORDERSTOP,vborderframenumber);
    }

    if (!macontext->Video.Options.IgnoreAspectRatio)
    {
        bool start;
        if (aspectratiochange(macontext->Video.Info.AspectRatio,aspectratio,start))
        {
            if ((logo->Status()==LOGO_VISIBLE) && (!start))
            {
                addmark(MT_LOGOSTOP,framebeforelast);
                logo->SetStatusLogoInvisible();
            }

            if ((vborder->Status()==VBORDER_VISIBLE) && (!start))
            {
                addmark(MT_VBORDERSTOP,framebeforelast);
                vborder->SetStatusBorderInvisible();
            }

            if ((hborder->Status()==HBORDER_VISIBLE) && (!start))
            {
                addmark(MT_HBORDERSTOP,framebeforelast);
                hborder->SetStatusBorderInvisible();
            }

            if ((macontext->Video.Info.AspectRatio.Num==4) &&
                    (macontext->Video.Info.AspectRatio.Den==3))
            {
                addmark(MT_ASPECTSTART,start ? FrameNumber : FrameNumberNext,
                        &aspectratio,&macontext->Video.Info.AspectRatio);
            }
            else
            {
                addmark(MT_ASPECTSTOP,framelast,&aspectratio,
                        &macontext->Video.Info.AspectRatio);
            }
        }

        aspectratio.Num=macontext->Video.Info.AspectRatio.Num;
        aspectratio.Den=macontext->Video.Info.AspectRatio.Den;
    }

    if (!macontext->Video.Options.IgnoreLogoDetection)
    {
        int logoframenumber;
        int lret=logo->Process(FrameNumber,&logoframenumber);
        if ((lret>=-1) && (lret!=0) && (logoframenumber!=-1))
        {
            if (lret>0)
            {
                addmark(MT_LOGOSTART,logoframenumber);
            }
            else
            {
                addmark(MT_LOGOSTOP,logoframenumber);
            }
        }
    }
    else
    {
        logo->SetStatusUninitialized();
    }

    framelast=FrameNumberNext;
    framebeforelast=FrameNumber;
    if (marks.Count)
    {
        return &marks;
    }
    else
    {
        return NULL;
    }
}
