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

extern "C" {
    #include "debug.h"
}

#include "video.h"
#include "logo.h"


cMarkAdLogo::cMarkAdLogo(MarkAdContext *maContext, cIndex *recordingIndex) {
    macontext = maContext;
    recordingIndexMarkAdLogo = recordingIndex;

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

    pixfmt_info = false;
    Clear();
}


void cMarkAdLogo::Clear(const bool isRestart, const bool inBroadCast) {
    area = {};
    if (isRestart) { // reset valid logo status after restart
        if (inBroadCast) area.status = LOGO_VISIBLE;
        else area.status = LOGO_INVISIBLE;
    }
    else area.status = LOGO_UNINITIALIZED;
}


areaT * cMarkAdLogo::GetArea() {
   return &area;
}


int cMarkAdLogo::Load(const char *directory, const char *file, const int plane) {
    if (!directory) return -1;
    if (!file) return -1;
    if ((plane < 0) || (plane >= PLANES)) {
        dsyslog("cMarkAdLogo::Load(): plane %d not valid", plane);
        return -3;
    }
    dsyslog("cMarkAdLogo::Load(): try to find logo %s plane %d in %s", file, plane, directory);

    char *path;
    if (asprintf(&path, "%s/%s-P%i.pgm", directory, file, plane) == -1) return -3;
    ALLOC(strlen(path)+1, "path");

    // Load mask
    FILE *pFile;
    area.valid[plane] = false;
    pFile=fopen(path, "rb");
    FREE(strlen(path)+1, "path");
    free(path);
    if (!pFile) {
        dsyslog("cMarkAdLogo::Load(): file not found for logo %s plane %d in %s",file, plane, directory);
        return -1;
    }
    else dsyslog("cMarkAdLogo::Load(): file found for logo %s plane %d in %s",file, plane, directory);

    int width,height;
    char c;
    if (fscanf(pFile, "P5\n#%1c%1i %4i\n%3d %3d\n255\n#", &c, &area.corner, &area.mpixel[plane], &width, &height) != 5) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }
    if (c == 'D') macontext->Audio.Options.IgnoreDolbyDetection = true;

    if (height == 255) {
        height = width;
        width = area.mpixel[plane];
        area.mpixel[plane] = 0;
    }

    if ((width <= 0) || (height <= 0) || (width > LOGO_MAXWIDTH) || (height > LOGO_MAXHEIGHT) || (area.corner < TOP_LEFT) || (area.corner > BOTTOM_RIGHT)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }

    if (fread(&area.mask[plane], 1, width*height, pFile) != (size_t) (width*height)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }
    fclose(pFile);

    if (!area.mpixel[plane]) {
        for (int i = 0; i < width*height; i++) {
            if (!area.mask[plane][i]) area.mpixel[plane]++;
        }
    }

    if (plane == 0) {   // plane 0 is the largest -> use this values
        LOGOWIDTH = width;
        LOGOHEIGHT = height;
    }
    macontext->Video.Logo.corner = area.corner;
    macontext->Video.Logo.height = LOGOHEIGHT;
    macontext->Video.Logo.width = LOGOWIDTH;

    area.valid[plane] = true;
    return 0;
}


// save the area.corner picture after sobel transformation to /tmp
// debug = 0: save was called by --extract function
// debug > 0: save was called by debug statements, add debug identifier to filename
// return: true if successful
//
bool cMarkAdLogo::Save(const int framenumber, uchar picture[PLANES][MAXPIXEL], const short int plane, const int debug) {
    if (!macontext) return false;
    if ((plane<0) || (plane >= PLANES)) return false;
    if (!macontext->Info.ChannelName) return false;
    if (!macontext->Video.Info.Width) {
        dsyslog("cMarkAdLogo::Save(): macontext->Video.Info.Width not set");
        return false;
    }
    if (!macontext->Video.Info.Height) {
        dsyslog("cMarkAdLogo::Save(): macontext->Video.Info.Height not set");
        return false;
    }
    if (!macontext->Video.Data.Valid) return false;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return false;
    if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
        dsyslog("cMarkAdLogo::Save(): LOGOWIDTH or LOGOHEIGHT not set");
        return false;
    }

    char *buf = NULL;
    if (debug == 0) {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i.pgm","/tmp/",framenumber, macontext->Info.ChannelName, area.aspectratio.Num,area.aspectratio.Den, plane)==-1) return false;
    }
    else if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i_debug%d.pgm","/tmp/",framenumber, macontext->Info.ChannelName, area.aspectratio.Num,area.aspectratio.Den, plane, debug)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    // Open file
    FILE *pFile = fopen(buf, "wb");
    if (pFile == NULL) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        return false;
    }

    int width = LOGOWIDTH;
    int height = LOGOHEIGHT;

    if (plane > 0) {
        width /= 2;
        height /= 2;
    }

    // Write header
    fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", area.corner, width, height);

    // Write pixel data
    if (fwrite(picture[plane], 1, width * height, pFile)) {};
    // Close file
    fclose(pFile);
    FREE(strlen(buf)+1, "buf");
    free(buf);
    return true;
}


bool cMarkAdLogo::SetCoorginates(int *xstart, int *xend, int *ystart, int *yend, const int plane) {
    switch (area.corner) {
        case TOP_LEFT:
            *xstart = 0;
            *xend = LOGOWIDTH;
            *ystart = 0;
            *yend = LOGOHEIGHT;
            break;
        case TOP_RIGHT:
            *xstart = macontext->Video.Info.Width - LOGOWIDTH;
            *xend = macontext->Video.Info.Width;
            *ystart = 0;
            *yend = LOGOHEIGHT;
            break;
        case BOTTOM_LEFT:
            *xstart = 0;
            *xend = LOGOWIDTH;
            *ystart = macontext->Video.Info.Height - LOGOHEIGHT;
            *yend = macontext->Video.Info.Height;
            break;
        case BOTTOM_RIGHT:
            *xstart = macontext->Video.Info.Width - LOGOWIDTH;
            *xend = macontext->Video.Info.Width;
            *ystart = macontext->Video.Info.Height - LOGOHEIGHT;
            *yend = macontext->Video.Info.Height;
            break;
        default:
            return false;
    }
    if (plane > 0) {
        *xstart /= 2;
        *xend /= 2;
        *ystart /= 2;
        *yend /= 2;
    }
    return true;
}


// save the original corner picture /tmp
// add debug identifier to filename
// return: true if successful
//
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
void cMarkAdLogo::SaveFrameCorner(const int framenumber, const int debug) {
    FILE *pFile;
    char szFilename[256];

    for (int plane = 0; plane < PLANES; plane ++) {
        int xstart, xend, ystart, yend;
        if (!SetCoorginates(&xstart, &xend, &ystart, &yend, plane)) return;
        int width = xend - xstart;
        int height = yend - ystart;

//        dsyslog("cMarkAdLogo::SaveFrameCorner(): framenumber (%5d) plane %d xstart %3d xend %3d ystart %3d yend %3d corner %d width %3d height %3d debug %d", framenumber, plane, xstart, xend, ystart, yend, area.corner, width, height, debug);
    // Open file
        sprintf(szFilename, "/tmp/frame%07d_C%d_P%d_D%02d.pgm", framenumber, area.corner, plane, debug);
        pFile=fopen(szFilename, "wb");
        if (pFile == NULL) {
            dsyslog("cMarkAdLogo::SaveFrameCorner(): open file %s failed", szFilename);
            return;
        }
        fprintf(pFile, "P5\n%d %d\n255\n", width, height); // Write header
        for (int line = ystart; line < yend; line++) {     // Write pixel data
            fwrite(&macontext->Video.Data.Plane[plane][line * macontext->Video.Data.PlaneLinesize[plane] + xstart], 1, width, pFile);
        }
        fclose(pFile); // Close file
    }
}
#endif


// reduce brightness and increase contrast
// return contrast of logo area before reduction: if successfully corrected
//        BRIGHTNESS_SEPARATOR:                   possible separation image detected
//        BRIGHTNESS_ERROR:                       if correction not possible
//
int cMarkAdLogo::ReduceBrightness(__attribute__((unused)) const int framenumber) {  // framenaumer used only for debugging
    int xstart, xend, ystart, yend;
    if (!SetCoorginates(&xstart, &xend, &ystart, &yend, 0)) return BRIGHTNESS_ERROR;

// set coorginates for logo part in logo corner
    int corner_xstart = 0;
    int corner_xend   = 0;
    int corner_ystart = 0;
    int corner_yend   = 0;
    switch (area.corner) {  // logo is usualy in the inner part of the logo corner
        case TOP_LEFT:
            corner_xstart = xend - (xend - xstart) / 2;
            corner_xend = xend;
            corner_ystart = yend - (yend - ystart) / 2;
            corner_yend = yend;
            break;
        case TOP_RIGHT:
            corner_xstart = xstart;
            corner_xend = xend - (xend - xstart) / 2;
            corner_ystart = yend - (yend - ystart) / 2;
            corner_yend = yend;
            break;
        case BOTTOM_LEFT:
            corner_xstart = xend - (xend - xstart) / 2;
            corner_xend = xend;
            corner_ystart = ystart;
            corner_yend = yend - (yend - ystart) / 2;
            break;
        case BOTTOM_RIGHT:
            corner_xstart = xstart;
            corner_xend = xend - (xend - xstart) / 2 ;
            corner_ystart = ystart;
            corner_yend = yend - (yend - ystart) / 2;
            break;
        default:
            return BRIGHTNESS_ERROR;
            break;
    }

// detect contrast and brightness of logo part
    int minPixel = INT_MAX;
    int maxPixel = 0;
    int sumPixel = 0;
    for (int line = corner_ystart; line < corner_yend; line++) {
        for (int column = corner_xstart; column < corner_xend; column++) {
            int pixel = macontext->Video.Data.Plane[0][line * macontext->Video.Data.PlaneLinesize[0] + column];
            if (pixel > maxPixel) maxPixel = pixel;
            if (pixel < minPixel) minPixel = pixel;
            sumPixel += pixel;
        }
    }
    int brightnessLogo = sumPixel / ((corner_yend - corner_ystart) * (corner_xend - corner_xstart));
    int contrastLogo = maxPixel - minPixel;
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo::ReduceBrightness(): logo area: xstart %d xend %d, ystart %d yend %d", corner_xstart, corner_xend, corner_ystart, corner_yend);
    dsyslog("cMarkAdLogo::ReduceBrightness(): logo area before reduction:  contrast %3d, brightness %3d", contrastLogo, brightnessLogo);
#endif

// check if we have a separation image
//
// there are separation images
// contrast   6, brightness 154, plane 1: pixel diff   9, plane 2: pixel diff   4
// contrast   5, brightness 155, plane 1: pixel diff  10, plane 2: pixel diff   5
//
// these are no separation images
// contrast   0, brightness 235, plane 1: pixel diff   6, plane 2: pixel diff   6
// contrast   4, brightness 232, plane 1: pixel diff   8, plane 2: pixel diff   6
    if ((area.status == LOGO_VISIBLE) && (brightnessLogo < 232) && (contrastLogo <= 6)) {  // we have a very low contrast, now check full plane 1 and plane 2
        int diffPixel_12[2] = {0};
        for (int line =  1; line <  (macontext->Video.Info.Height / 2) - 1; line++) {  // ignore first and last line, they have sometimes weird pixel
            for (int column = 1; column < (macontext->Video.Info.Width / 2) - 2; column++) { // ignore first and last column, they have sometimes weird pixel
                for (int plane = 1; plane < PLANES; plane++) {
                    int diff = abs(macontext->Video.Data.Plane[plane][line * macontext->Video.Data.PlaneLinesize[plane] + column] - macontext->Video.Data.Plane[plane][line * macontext->Video.Data.PlaneLinesize[plane] + column + 1]);
                    if (diff > diffPixel_12[plane - 1]) {
                        diffPixel_12[plane -1] = diff;
//                      if (plane == 1) dsyslog("+++ new max pixel: plane %d line %d column %d pixel %3d", plane, line, column, diff);
                    }
                }
            }
        }
#ifdef DEBUG_LOGO_DETECTION
        for (int plane = 1; plane < PLANES; plane++) {
            dsyslog("cMarkAdLogo::ReduceBrightness(): plane %d: pixel diff %3d", plane, diffPixel_12[plane - 1]);
        }
#endif
        // if we have also low pixel diff in plane 1 and plane 2, this is a separation image
        // we can not use contrast because there is a soft colour change from right to left
        if ((diffPixel_12[0] <= 10) && (diffPixel_12[1] <= 10)) {
            dsyslog("cMarkAdLogo::ReduceBrightness(): possible separation image detected");
            return BRIGHTNESS_SEPARATOR;
        }
    }

// check if contrast and brightness is valid
// build a curve from examples
//
// no logo in bright area, take it as valid
// contrast  90, brightness 183
// contrast  30, brightness 217
// contrast  13, brightness 128
//
// logo in bright area
// contrast  89, brightness 187
// contrast  81, brightness 206
// contrast  76, brightness 197
// contrast  76, brightness 190
// contrast  73, brightness 191
// contrast  72, brightness 194
// contrast  55, brightness 204
// contrast  54, brightness 207
// contrast  49, brightness 217
// contrast  42, brightness 217
// contrast  39, brightness 216
// contrast  35, brightness 218
// contrast  34, brightness 223
// contrast  33, brightness 221
// contrast  33, brightness 219
// contrast  33, brightness 219
// contrast  13, brightness 207
// contrast   3, brightness 206
// contrast   9, brightness 205
    if ((contrastLogo <= 13) && (brightnessLogo >= 205)) {   // low contrast in bright area maybe there is a invisible logo
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): contrast in logo area too low");
#endif
        return BRIGHTNESS_ERROR; // nothing we can work with
    }
    if (contrastLogo > 90) {   // this could not be the contrast of the logo area because it would be detected without calling ReduceBrightness()
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): contrast in logo area too high");
#endif
        return BRIGHTNESS_ERROR; // nothing we can work with
    }
    if (brightnessLogo >= 221) { // this is too bright, nothing we can work with, changed from 227 to 223 to 221
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): brightness in logo area to high");
#endif
        return BRIGHTNESS_ERROR; // nothing we can work with
    }
    // build the curve
    if (((contrastLogo >= 72) && (brightnessLogo >= 187)) ||  // changed from 197 to 190 to 187, changed from 79 to 72
        ((contrastLogo >= 54) && (brightnessLogo >= 204)) ||
        ((contrastLogo >= 39) && (brightnessLogo >= 216)) ||  // changed contrast from 49 to 39, brightness from 217 to 216
        ((contrastLogo >= 33) && (brightnessLogo >= 218))) {  // changed from 219 to 218
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): contrast/brightness pair in logo area invalid");
#endif
        return BRIGHTNESS_ERROR; //  nothing we can work with
    }

// correct brightness and increase ontrast
    minPixel = INT_MAX;
    maxPixel = 0;
#ifdef DEBUG_LOGO_DETECTION
    sumPixel = 0;
#endif
    int reduceBrightness = brightnessLogo - 128; // bring logo area to +- 128
    for (int line = ystart; line < yend; line++) {
        for (int column = xstart; column < xend; column++) {
            int pixel = macontext->Video.Data.Plane[0][line * macontext->Video.Data.PlaneLinesize[0] + column] - reduceBrightness;
            if (pixel > 128) pixel += 20;  // increase contrast around logo part brightness +- 128, do not do too much, otherwise clouds will detected as logo parts
            if (pixel < 128) pixel -= 20;
            if (pixel < 0) pixel = 0;
            if (pixel > 255) pixel = 255;
            macontext->Video.Data.Plane[0][line * macontext->Video.Data.PlaneLinesize[0] + column] = pixel;
            if (pixel > maxPixel) maxPixel = pixel;
            if (pixel < minPixel) minPixel = pixel;
#ifdef DEBUG_LOGO_DETECTION
            sumPixel += pixel;
#endif
        }
    }
#ifdef DEBUG_LOGO_DETECTION
    int brightnessReduced = sumPixel / ((yend - ystart) * (xend - xstart));
    int contrastReduced = maxPixel - minPixel;
    dsyslog("cMarkAdLogo::ReduceBrightness(): after brightness correction: contrast %3d, brightness %3d", contrastReduced, brightnessReduced);
#endif
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((framenumber > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (framenumber < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) SaveFrameCorner(framenumber, 2);
#endif
    return contrastLogo;
}


bool cMarkAdLogo::SobelPlane(const int plane) {
    if ((plane < 0) || (plane >= PLANES)) return false;
    if (!macontext->Video.Data.PlaneLinesize[plane]) return false;

    if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
        if (macontext->Video.Info.Width > 720){
            LOGOHEIGHT = LOGO_DEFHDHEIGHT;
            LOGOWIDTH = LOGO_DEFHDWIDTH;
        }
        else {
            LOGOHEIGHT = LOGO_DEFHEIGHT;
            LOGOWIDTH = LOGO_DEFWIDTH;
        }
    }
    if ((macontext->Video.Info.Pix_Fmt != 0) && (macontext->Video.Info.Pix_Fmt != 12)) {
        if (!pixfmt_info) {
            esyslog("unknown pix_fmt %i, please report!", macontext->Video.Info.Pix_Fmt);
            pixfmt_info = true;
        }
        return false;
    }
    int xstart, xend, ystart, yend;
    if (!SetCoorginates(&xstart, &xend, &ystart, &yend, plane)) return false;
    int boundary = 6;
    int cutval = 127;
    int width = LOGOWIDTH;
    if (plane > 0) {
        boundary /= 2;
        cutval /= 2;
        width /= 2;
    }
    int SUM;
    int sumX,sumY;
    area.rpixel[plane] = 0;
    if (!plane) area.intensity = 0;
    for (int Y = ystart; Y <= yend - 1; Y++) {
        for (int X = xstart; X <= xend - 1; X++) {
            if (!plane) {
                area.intensity += macontext->Video.Data.Plane[plane][X + (Y * macontext->Video.Data.PlaneLinesize[plane])];
            }
            sumX = 0;
            sumY = 0;

            // image boundaries
            if (Y < (ystart + boundary) || Y > (yend - boundary)) SUM = 0;
            else if (X < (xstart + boundary) || X > (xend - boundary)) SUM = 0;
            // convolution starts here
            else {
                // X Gradient approximation
                for (int I = -1; I <= 1; I++) {
                    for (int J = -1; J <= 1; J++) {
                        sumX = sumX + static_cast<int> ((*(macontext->Video.Data.Plane[plane] + X + I + (Y + J) * macontext->Video.Data.PlaneLinesize[plane])) * GX[I + 1][J + 1]);
                    }
                }

                // Y Gradient approximation
                for (int I = -1; I <= 1; I++) {
                    for (int J = -1; J <= 1; J++) {
                        sumY = sumY+ static_cast<int> ((*(macontext->Video.Data.Plane[plane] + X + I + (Y + J) * macontext->Video.Data.PlaneLinesize[plane])) * GY[I + 1][J + 1]);
                    }
                }

                // Gradient Magnitude approximation
                SUM = abs(sumX) + abs(sumY);
            }

            if (SUM >= cutval) SUM = 255;
            if (SUM < cutval) SUM = 0;

            int val = 255 - (uchar) SUM;

            area.sobel[plane][(X-xstart)+(Y-ystart)*width] = val;

            area.result[plane][(X-xstart)+(Y-ystart)*width] = (area.mask[plane][(X-xstart)+(Y-ystart)*width] + val) & 255;

            if (!area.result[plane][(X-xstart)+(Y-ystart)*width]) area.rpixel[plane]++;
#ifdef VDRDEBUG
            val=macontext->Video.Data.Plane[plane][X+(Y*macontext->Video.Data.PlaneLinesize[plane])];
            area.source[plane][(X-xstart)+(Y-ystart)*width] = val;
#endif

        }
    }
    if (!plane) area.intensity /= (LOGOHEIGHT*width);
    return true;
}


// copy all black pixels from logo pane 0 into plan 1 and plane 2
// we need this for channels with usually grey logos, but at start and end they can be red (DMAX)
//
void cMarkAdLogo::LogoGreyToColour() {
    for (int line = 0; line < LOGOHEIGHT; line++) {
        for (int column = 0; column < LOGOWIDTH; column++) {
            if (area.mask[0][line * LOGOWIDTH + column] == 0 ){
                area.mask[1][line / 2 * LOGOWIDTH / 2 + column / 2] = 0;
                area.mask[2][line / 2 * LOGOWIDTH / 2 + column / 2] = 0;
            }
            else {
                area.mask[1][line / 2 * LOGOWIDTH / 2 + column / 2] = 255;
                area.mask[2][line / 2 * LOGOWIDTH / 2 + column / 2] = 255;
            }
        }
    }
}


// notice: if we are called by logo detection, <framenumber> is last iFrame before, otherwise it is current frame
int cMarkAdLogo::Detect(const int framenumber, int *logoframenumber) {
    bool onlyFillArea = ( *logoframenumber < 0 );
    bool extract = (macontext->Config->logoExtraction != -1);
    if (*logoframenumber == -2) extract = true;
    int rpixel = 0,mpixel = 0;
    int processed = 0;
    *logoframenumber = -1;
    if (area.corner == -1) return LOGO_NOCHANGE;
    float logo_vmark = LOGO_VMARK;
    if (macontext->Video.Logo.isRotating) logo_vmark *= 0.8;   // reduce if we have a rotating logo (e.g. SAT_1)i, changed from 0.9 to 0.8

    for (int plane = 0; plane < PLANES; plane++) {
        if ((area.valid[plane]) || (extract) || (onlyFillArea)) {
            if (SobelPlane(plane)) {
                processed++;
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((framenumber > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (framenumber < DEBUG_LOGO_DETECT_FRAME_CORNER + 200) && !onlyFillArea) {
                    Save(framenumber, area.sobel, plane, 1);
                }
#endif
            }
        }
        if (extract) {
            if (!Save(framenumber, area.sobel, plane, 0)) dsyslog("cMarkAdLogo::Detect(): save logo from frame (%d) failed", framenumber);
        }
        else {
//            tsyslog("plane %i area.rpixel[plane] %i area.mpixel[plane] %i", plane, area.rpixel[plane], area.mpixel[plane]);
            rpixel += area.rpixel[plane];
            mpixel += area.mpixel[plane];
        }
    }
    if (extract || onlyFillArea) return LOGO_NOCHANGE;
    if (processed == 0) return LOGO_ERROR;  // we have no plane processed

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((framenumber > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (framenumber < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) {
        SaveFrameCorner(framenumber, 1);  // we are not called by logo.cpp frame debug
    }
#endif

#ifdef DEBUG_LOGO_DETECTION
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("frame (%6i) rp=%5i | mp=%5i | mpV=%5.f | mpI=%5.f | i=%3i | c=%d | s=%i | p=%i", framenumber, rpixel, mpixel, (mpixel * logo_vmark), (mpixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
    // if we only have one plane we are "vulnerable"
    // to very bright pictures, so ignore them...
    int contrast = BRIGHTNESS_UNINITIALIZED;
    if (processed == 1) {
#define MAX_AREA_INTENSITY 98  // change from 128 to 127 to 126 to 125 to 114 to 100 to 98
                               // notice: there can be very bright logo parts in dark areas, this will result in a lower brightness
                               // we handle there cases in ReduceBrightness() when we detect contrast
        if (((area.intensity > MAX_AREA_INTENSITY) || // if we found no logo try to reduce brightness
            ((area.intensity >= 80) && (strcmp(macontext->Info.ChannelName, "NITRO") == 0))) &&  // workaround for NITRO, this channel has a very transparent logo, brightness reduction needed earlyer
             (area.intensity < 220) &&  // if we are to bright, this will not work, max changed from 200 to 220
           ((((area.status == LOGO_INVISIBLE) || (area.status == LOGO_UNINITIALIZED)) && (rpixel < (mpixel * logo_vmark))) || // if we found the logo ignore area intensity
           ((area.status == LOGO_VISIBLE) && (rpixel < (mpixel * LOGO_IMARK))))) {
            contrast = ReduceBrightness(framenumber);
            if (contrast > 0) {  // we got a contrast
                area.rpixel[0] = 0;
                rpixel = 0;
                mpixel = 0;
                SobelPlane(0);
                rpixel += area.rpixel[0];
                mpixel += area.mpixel[0];
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) corrected, new area intensity %d", framenumber, area.intensity);
                dsyslog("frame (%6i) rp=%5i | mp=%5i | mpV=%5.f | mpI=%5.f | i=%3i | c=%d | s=%i | p=%i", framenumber, rpixel, mpixel, (mpixel * logo_vmark), (mpixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((framenumber > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (framenumber < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) Save(framenumber, area.sobel, 0, 2);
#endif
                if ((area.status == LOGO_INVISIBLE) && (contrast < 25)) {  // if we have a very low contrast this could not be a new logo
                    return LOGO_NOCHANGE;
                }
                if ((area.status == LOGO_VISIBLE) && (contrast < 25) && (rpixel < (mpixel * LOGO_IMARK)) && (rpixel > (mpixel * LOGO_IMARK) / 3)){  // if we have a very low contrast and some matches this could be a logo on a very bright area
                    return LOGO_NOCHANGE;
                }
            }
            else {
                if ((contrast == BRIGHTNESS_SEPARATOR) && (area.status == LOGO_VISIBLE)) { // sepatation image detected
                    area.intensity = 0; // force detection of sepatation image
                }
            }
            if ((area.status == LOGO_VISIBLE) && (rpixel < (mpixel * LOGO_IMARK)) && (strcmp(macontext->Info.ChannelName, "NITRO") == 0)) return LOGO_NOCHANGE; // dont belive brightness reduction for NITRO, logo too transparent
        }
        // if we have still no match, try to copy colour planes into grey planes
        // we can even try this if plane 0 is too bright, maybe plane 1 or 2 are better
        // for performance reason we do this only for the known channel
        if (((area.intensity > MAX_AREA_INTENSITY) ||              // we have no valid result
             (((rpixel < (mpixel * logo_vmark)) &&                  // we have a valid result, but maybe we can find a new coloured logo
                 (area.status == LOGO_INVISIBLE)) ||
               ((rpixel < (mpixel * LOGO_IMARK)) &&                  // we have a valid result, but maybe we can re-find a coloured logo
                 (area.status == LOGO_VISIBLE))))  &&
             (strcmp(macontext->Info.ChannelName, "DMAX") == 0)) { // and only on channel DMAX
            // save state
            int intensityOld = area.intensity;
            int rpixelOld = rpixel;
            int mpixelOld = mpixel;

            if (!isInitColourChange) {
                LogoGreyToColour();
                isInitColourChange = true;
            }
            rpixel = 0;
            mpixel = 0;
            for (int plane = 1; plane < PLANES; plane++) {
            // reset all planes
                area.rpixel[plane] = 0;
                area.valid[plane] = true;
                area.mpixel[plane] = area.mpixel[0] / 4;
                SobelPlane(plane);
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((framenumber > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (framenumber < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) {
                    Save(framenumber, area.sobel, plane, 3);
                }
#endif
                rpixel += area.rpixel[plane];
                mpixel += area.mpixel[plane];
                area.valid[plane] = false;
                area.mpixel[plane] = 0;
                area.rpixel[plane] = 0;
            }
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) maybe logo had a colour change, try plane 1 and plan 2", framenumber);
            dsyslog("frame (%6i) rp=%5i | mp=%5i | mpV=%5.f | mpI=%5.f | i=%3i | c=%d | s=%i | p=%i", framenumber, rpixel, mpixel, (mpixel * logo_vmark), (mpixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
            if (rpixel < (mpixel * LOGO_IMARK)) { // we found no coloured logo here, restore old state
                area.intensity = intensityOld;
                rpixel = rpixelOld;
                mpixel = mpixelOld;
            }
            else contrast = BRIGHTNESS_VALID;  // ignore bightness, we found a coloured logo
        }
        if ((area.intensity > MAX_AREA_INTENSITY) && // still too bright
           ((contrast == BRIGHTNESS_ERROR) || (contrast == BRIGHTNESS_UNINITIALIZED)) &&
            (rpixel < (mpixel * logo_vmark))) {  // accept it, if we can see a logo
            return LOGO_NOCHANGE;
        }
    }
    else {  // if we have more planes we can still have a problem with bright frames at start, make sure we get at least a quick initial logo visible
        if ((area.status == LOGO_UNINITIALIZED) && (area.intensity > 120) && (rpixel < (mpixel * logo_vmark))) {
            rpixel -= area.rpixel[0];
            mpixel -= area.mpixel[0];
        }
    }

    int ret = LOGO_NOCHANGE;
    if (area.status == LOGO_UNINITIALIZED) { // Initialize
        if (rpixel >= (mpixel * logo_vmark)) {
            area.status = ret = LOGO_VISIBLE;
        }
        else {
            area.status = LOGO_INVISIBLE;
        }
        area.framenumber = framenumber;
        *logoframenumber = framenumber;
    }

    if (rpixel >= (mpixel * logo_vmark)) {
        if (area.status == LOGO_INVISIBLE) {
            if (area.counter >= LOGO_VMAXCOUNT) {
                area.status = ret = LOGO_VISIBLE;
                *logoframenumber = area.framenumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.framenumber = framenumber;
                area.counter++;
            }
        }
        else {
            area.framenumber = framenumber;
            area.counter = 0;
        }
    }

    if (rpixel < (mpixel*LOGO_IMARK)) {
        if (area.status == LOGO_VISIBLE) {
            if (area.counter >= LOGO_IMAXCOUNT) {
                area.status = ret = LOGO_INVISIBLE;
                *logoframenumber = area.framenumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.framenumber = framenumber;
                area.counter++;
                if (contrast >= 0) area.counter++;  // if we had optimzed the picture and still no match, this should be valid
                if (rpixel < (mpixel*LOGO_IMARK/2)) area.counter++;   // good detect for logo invisible
                if (rpixel < (mpixel*LOGO_IMARK/4)) area.counter++;   // good detect for logo invisible
                if (rpixel == 0) {
                    area.counter++;   // very good detect for logo invisible
                    if (area.intensity <= 80) { // best detect, blackscreen without logo, increased from 30 to 70 to 80
                        dsyslog("cMarkAdLogo::Detect(): black screen without logo detected at frame (%d)", framenumber);
                        area.status = ret = LOGO_INVISIBLE;
                        *logoframenumber = area.framenumber;
                        area.counter = 0;
                    }
                }
            }
        }
        else {
            area.counter = 0;
        }
    }

    if ((rpixel < (mpixel * logo_vmark)) && (rpixel > (mpixel * LOGO_IMARK))) {
        area.counter--;  // we are more uncertain of logo state
        if (area.counter < 0) area.counter = 0;
    }
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("frame (%6i) rp=%5i | mp=%5i | mpV=%5.f | mpI=%5.f | i=%3i | c=%d | s=%i | p=%i", framenumber, rpixel, mpixel, (mpixel * logo_vmark), (mpixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
#endif
    return ret;
}


int cMarkAdLogo::Process(int FrameNumber, int *LogoFrameNumber) {
    if (!macontext) return LOGO_ERROR;
    if (!macontext->Video.Data.Valid) {
        area.status = LOGO_UNINITIALIZED;
        dsyslog("cMarkAdLogo::Process(): video data not valid at frame (%i)", FrameNumber);
        return LOGO_ERROR;
    }
    if (!macontext->Video.Info.Width) {
        dsyslog("cMarkAdLogo::Process(): video width info missing");
        return LOGO_ERROR;
    }
    if (!macontext->Video.Info.Height) {
        dsyslog("cMarkAdLogo::Process(): video high info missing");
        return LOGO_ERROR;
    }
    if (!macontext->Config->logoDirectory[0]) {
        dsyslog("cMarkAdLogo::Process(): logoDirectory missing");
        return LOGO_ERROR;
    }
    if (!macontext->Info.ChannelName) {
        dsyslog("cMarkAdLogo::Process(): ChannelName missing");
        return LOGO_ERROR;
    }


    if (macontext->Config->logoExtraction == -1) {
        if ((area.aspectratio.Num != macontext->Video.Info.AspectRatio.Num) || (area.aspectratio.Den != macontext->Video.Info.AspectRatio.Den)) {
            dsyslog("cMarkAdLogo::Process(): aspect ratio changed from %i:%i to %i:%i, reload logo", area.aspectratio.Num, area.aspectratio.Den, macontext->Video.Info.AspectRatio.Num, macontext->Video.Info.AspectRatio.Den);
            if (macontext->Info.checkedAspectRatio && (macontext->Info.AspectRatio.Num == 4) && (macontext->Info.AspectRatio.Den == 3) && (macontext->Video.Info.AspectRatio.Num == 16) && (macontext->Video.Info.AspectRatio.Den == 9)) {
                dsyslog("cMarkAdLogo::Process(): recording is 4:3, current frame is 16:9, we do not need a logo");
                macontext->Video.Options.IgnoreLogoDetection = true;
            }
            else {
                char *buf=NULL;
                if (asprintf(&buf,"%s-A%i_%i", macontext->Info.ChannelName, macontext->Video.Info.AspectRatio.Num, macontext->Video.Info.AspectRatio.Den) != -1) {
                    ALLOC(strlen(buf)+1, "buf");
                    area.corner = -1;
                    bool logoStatus = false;
                    if (Load(macontext->Config->logoDirectory, buf, 0) == 0) {   // logo cache directory
                        isyslog("logo %s found in %s", buf, macontext->Config->logoDirectory);
                        logoStatus = true;
                        for (int plane = 1; plane < PLANES; plane++) {
                            if (Load(macontext->Config->logoDirectory, buf, plane) == 0) dsyslog("logo %s for plane %i found in %s", buf, plane, macontext->Config->logoDirectory);
                        }
                    }
                    else {
                        if (Load(macontext->Config->recDir,buf,0) == 0) {  // recording directory
                            isyslog("logo %s found in %s", buf, macontext->Config->recDir);
                            logoStatus = true;
                            for (int plane = 1; plane < PLANES; plane++) {
                                if (Load(macontext->Config->recDir, buf, plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, macontext->Config->recDir);
                            }
                        }
                        else {
                            if (macontext->Config->autoLogo > 0) {
                                isyslog("no valid logo for %s in logo cache and recording directory, extract logo from recording",buf);
                                cExtractLogo *ptr_cExtractLogo = new cExtractLogo(macontext->Video.Info.AspectRatio, recordingIndexMarkAdLogo);  // search logo from current frame
                                ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
                                if (ptr_cExtractLogo->SearchLogo(macontext, FrameNumber) > 0) dsyslog("cMarkAdLogo::Process(): no logo found in recording");
                                else dsyslog("cMarkAdLogo::Process(): new logo for %s found in recording",buf);
                                FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo"); // ptr_cExtraceLogo is valid because it it used above
                                delete ptr_cExtractLogo;
                                ptr_cExtractLogo = NULL;
                                if (Load(macontext->Config->recDir,buf,0) == 0) {  // try again recording directory
                                    isyslog("logo %s found in %s", buf, macontext->Config->recDir);
                                    logoStatus = true;
                                    for (int plane=1; plane < PLANES; plane++) {
                                        if (Load(macontext->Config->recDir,buf,plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, macontext->Config->recDir);
                                    }
                                }
                                else isyslog("still no valid logo for %s in recording directory",buf);
                            }
                        }
                    }
                    if (!logoStatus) {
                        dsyslog("cMarkAdLogo::Process(): no valid logo found for aspect ratio %i:%i, disable logo detection", macontext->Video.Info.AspectRatio.Num, macontext->Video.Info.AspectRatio.Den);
                        macontext->Video.Options.IgnoreLogoDetection = true;
                    }
                    FREE(strlen(buf)+1, "buf");
                    free(buf);
                }
                else dsyslog("cMarkAdLogo::Process(): out of memory");
            }
            area.aspectratio.Num = macontext->Video.Info.AspectRatio.Num;
            area.aspectratio.Den = macontext->Video.Info.AspectRatio.Den;
        }
    }
    else {
        if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
            if ((macontext->Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H264) || (macontext->Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H265)) {
                LOGOHEIGHT = LOGO_DEFHDHEIGHT;
                LOGOWIDTH = LOGO_DEFHDWIDTH;
            }
            else if (macontext->Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H262) {
                LOGOHEIGHT = LOGO_DEFHEIGHT;
                LOGOWIDTH = LOGO_DEFWIDTH;
            }
            else {
                dsyslog("cMarkAdLogo::cMarkAdLogo macontext->Info.VPid.Type %i not valid", macontext->Info.VPid.Type);
                return LOGO_ERROR;
            }
        }
        area.aspectratio.Num = macontext->Video.Info.AspectRatio.Num;
        area.aspectratio.Den = macontext->Video.Info.AspectRatio.Den;
        area.corner = macontext->Config->logoExtraction;
        if (macontext->Config->logoWidth != -1) {
            LOGOWIDTH = macontext->Config->logoWidth;
        }
        if (macontext->Config->logoHeight != -1) {
            LOGOHEIGHT = macontext->Config->logoHeight;
        }
    }
    return Detect(FrameNumber, LogoFrameNumber);
}


// detect blackscreen
//
cMarkAdBlackScreen::cMarkAdBlackScreen(MarkAdContext *maContext) {
    macontext = maContext;
    Clear();
}


void cMarkAdBlackScreen::Clear() {
    blackScreenstatus = BLACKSCREEN_UNINITIALIZED;
}


// check if current frame is a blackscreen
// return: -1 blackscreen start (notice: this is a STOP mark)
//          0 no status change
//          1 blackscreen end (notice: this is a START mark)
//
int cMarkAdBlackScreen::Process() {
#define BLACKNESS 20  // maximum average brightness
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;
    if (macontext->Video.Info.FramesPerSecond == 0) return 0;
    if (!macontext->Video.Info.Height) {
        dsyslog("cMarkAdBlackScreen::Process() missing macontext->Video.Info.Height");
        return 0;
    }
    if (!macontext->Video.Info.Width) {
        dsyslog("cMarkAdBlackScreen::Process() missing macontext->Video.Info.Width");
        return 0;
    }
    if (!macontext->Video.Data.Plane[0]) {
        dsyslog("cMarkAdBlackScreen::Process() Video.Data.Plane[0] missing");
        return 0;
    }
    int end = macontext->Video.Info.Height * macontext->Video.Info.Width;
    int val = 0;
    int maxBrightness = BLACKNESS * end;
    for (int x = 0; x < end; x++) {
        val += macontext->Video.Data.Plane[0][x];
        if (val > maxBrightness) {
            if (blackScreenstatus != BLACKSCREEN_INVISIBLE) {
                blackScreenstatus = BLACKSCREEN_INVISIBLE;
                if (blackScreenstatus != BLACKSCREEN_UNINITIALIZED) return 1; // detected stop of black screen
            }
            return 0;
        }
    }
    if (blackScreenstatus == BLACKSCREEN_INVISIBLE) {
        blackScreenstatus = BLACKSCREEN_VISIBLE;
        if (blackScreenstatus != BLACKSCREEN_UNINITIALIZED) return -1; // detected start of black screen
    }
    return 0;
}


cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(MarkAdContext *maContext) {
    macontext = maContext;
    Clear();
}


int cMarkAdBlackBordersHoriz::GetFirstBorderFrame() {
    if (borderstatus != HBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


void cMarkAdBlackBordersHoriz::Clear() {
    borderstatus = HBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cMarkAdBlackBordersHoriz::Process(const int FrameNumber, int *BorderIFrame) { // return 0 no border change
                                                                            //        1 detected start of black border
                                                                            //       -1 detected stop of black border
#define CHECKHEIGHT 20
#define BRIGHTNESS_H_SURE 20
#define BRIGHTNESS_H_MAYBE 27  // some channel have logo in border, so we will get a higher value
#define VOFFSET 5
    if (!macontext) return 0;
    if (!macontext->Video.Data.Valid) return 0;
    if (macontext->Video.Info.FramesPerSecond == 0) return 0;
    // Assumption: If we have 4:3, we should have aspectratio-changes!
    //if (macontext->Video.Info.AspectRatio.Num==4) return 0; // seems not to be true in all countries?
    *BorderIFrame = 0;
    if (!macontext->Video.Info.Height) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() video hight missing");
        return 0;
    }
    int height = macontext->Video.Info.Height - VOFFSET;

    if (!macontext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() Video.Data.PlaneLinesize[0] not initalized");
        return 0;
    }
    int start = (height - CHECKHEIGHT) * macontext->Video.Data.PlaneLinesize[0];
    int end = height * macontext->Video.Data.PlaneLinesize[0];
    int valTop = 0;
    int valBottom = 0;
    int cnt = 0;
    int xz = 0;

    for (int x = start; x < end; x++) {
        if (xz < macontext->Video.Info.Width) {
            valBottom += macontext->Video.Data.Plane[0][x];
            cnt++;
        }
        xz++;
        if (xz >= macontext->Video.Data.PlaneLinesize[0]) xz=0;
    }
    valBottom /= cnt;

    if (valBottom < BRIGHTNESS_H_MAYBE) { // we have a bottom border, test top border
        start = VOFFSET * macontext->Video.Data.PlaneLinesize[0];
        end = macontext->Video.Data.PlaneLinesize[0] * (CHECKHEIGHT+VOFFSET);
        cnt = 0;
        xz = 0;
        for (int x = start; x < end; x++) {
            if (xz < macontext->Video.Info.Width) {
                valTop += macontext->Video.Data.Plane[0][x];
                cnt++;
            }
            xz++;
            if (xz >= macontext->Video.Data.PlaneLinesize[0]) xz=0;
        }
        valTop /= cnt;
    }
    else valTop = INT_MAX;

#ifdef DEBUG_HBORDER
    dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%5d) hborder brightness top %3d bottom %3d", FrameNumber, valTop, valBottom);
#endif

    if ((valTop < BRIGHTNESS_H_MAYBE) && (valBottom < BRIGHTNESS_H_SURE) || (valTop < BRIGHTNESS_H_SURE) && (valBottom < BRIGHTNESS_H_MAYBE)) {
        if (borderframenumber == -1) {
            borderframenumber = FrameNumber;
        }
        else {
            if (borderstatus != HBORDER_VISIBLE) {
                if (FrameNumber > (borderframenumber+macontext->Video.Info.FramesPerSecond * MIN_H_BORDER_SECS)) {
                    *BorderIFrame = borderframenumber;
                    borderstatus = HBORDER_VISIBLE;
                    return 1; // detected start of black border
                }
            }
        }
    }
    else {
        if (borderstatus == HBORDER_VISIBLE) {
            *BorderIFrame = FrameNumber;
            borderstatus = HBORDER_INVISIBLE;
            borderframenumber = -1;
            return -1; // detected stop of black border
        }
        else {
            borderframenumber = -1; // restart from scratch
        }
    }
    return 0;
}


cMarkAdBlackBordersVert::cMarkAdBlackBordersVert(MarkAdContext *maContext) {
    macontext = maContext;
    Clear();
}


void cMarkAdBlackBordersVert::Clear() {
    borderstatus = VBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cMarkAdBlackBordersVert::GetFirstBorderFrame() {
    if (borderstatus != VBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cMarkAdBlackBordersVert::Process(int FrameNumber, int *BorderIFrame) {
#define CHECKWIDTH 32
#define BRIGHTNESS_V 20
#define HOFFSET 50
#define VOFFSET_ 120
    if (!macontext) {
        dsyslog("cMarkAdBlackBordersVert::Process(): macontext not valid");
        return 0;
    }
    if (!macontext->Video.Data.Valid) return 0;  // no error, this is expected if bDecodeVideo is disabled
    if (macontext->Video.Info.FramesPerSecond == 0) {
        dsyslog("cMarkAdBlackBordersVert::Process(): video frames per second  not valid");
        return 0;
    }
    // Assumption: If we have 4:3, we should have aspectratio-changes!
    //if (macontext->Video.Info.AspectRatio.Num==4) return 0; // seems not to be true in all countries?
    *BorderIFrame = 0;

    bool fleft = true, fright = true;
    int val = 0,cnt = 0;

    if(!macontext->Video.Data.PlaneLinesize[0]) {
        dsyslog("Video.Data.PlaneLinesize[0] missing");
        return 0;
    }
    int end = macontext->Video.Data.PlaneLinesize[0] * (macontext->Video.Info.Height-VOFFSET_);
    int i = VOFFSET_*macontext->Video.Data.PlaneLinesize[0];
    while (i < end) {
        for (int x = 0; x < CHECKWIDTH; x++) {
            val += macontext->Video.Data.Plane[0][HOFFSET+x+i];
            cnt++;
        }
        i += macontext->Video.Data.PlaneLinesize[0];
    }
    val /= cnt;
    if (val > BRIGHTNESS_V) fleft = false;

    if (fleft) {
        val = cnt = 0;
        i = VOFFSET_*macontext->Video.Data.PlaneLinesize[0];
        int w = macontext->Video.Info.Width - HOFFSET - CHECKWIDTH;
        while (i < end) {
            for (int x = 0; x < CHECKWIDTH; x++) {
                val += macontext->Video.Data.Plane[0][w+x+i];
                cnt++;
            }
            i += macontext->Video.Data.PlaneLinesize[0];
        }
        val /= cnt;
        if (val > BRIGHTNESS_V) fright = false;
    }

#ifdef DEBUG_VBORDER
    dsyslog("cMarkAdBlackBordersVert(): frame (%5d) fleft %d fright %d", FrameNumber, fleft, fright);
#endif
    if ((fleft) && (fright)) {
        if (borderframenumber == -1) {
            borderframenumber = FrameNumber;
        }
        else {
            if (borderstatus != VBORDER_VISIBLE) {
#ifdef DEBUG_VBORDER
                dsyslog("cMarkAdBlackBordersVert(): frame (%5d) duration %ds", FrameNumber, static_cast<int> ((FrameNumber - borderframenumber) /  macontext->Video.Info.FramesPerSecond));
#endif
                if (FrameNumber > (borderframenumber + macontext->Video.Info.FramesPerSecond * MIN_V_BORDER_SECS)) {
                    *BorderIFrame = borderframenumber;
                    borderstatus = VBORDER_VISIBLE;
                    return 1; // detected start of black border
                }
            }
        }
    }
    else {
        if (borderstatus == VBORDER_VISIBLE) {
            *BorderIFrame = FrameNumber;
            borderstatus = VBORDER_INVISIBLE;
            borderframenumber = -1;
            return -1; // detected stop of black border
        }
        else {
            borderframenumber = -1; // restart from scratch
        }
    }
    return 0;
}


cMarkAdOverlap::cMarkAdOverlap(MarkAdContext *maContext) {
    macontext = maContext;
    histbuf[OV_BEFORE] = NULL;
    histbuf[OV_AFTER] = NULL;
    Clear();
}


cMarkAdOverlap::~cMarkAdOverlap() {
    Clear();
}


void cMarkAdOverlap::Clear() {
    histcnt[OV_BEFORE] = 0;
    histcnt[OV_AFTER] = 0;
    histframes[OV_BEFORE] = 0;
    histframes[OV_AFTER] = 0;
    if (histbuf[OV_BEFORE]) {
        FREE(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        delete[] histbuf[OV_BEFORE];
        histbuf[OV_BEFORE] = NULL;
    }
    if (histbuf[OV_AFTER]) {
        FREE(sizeof(*histbuf[OV_AFTER]), "histbuf");
        delete[] histbuf[OV_AFTER];
        histbuf[OV_AFTER] = NULL;
    }
    memset(&result, 0, sizeof(result));
    similarCutOff = 0;
    similarMaxCnt = 0;

    lastframenumber = -1;
}


void cMarkAdOverlap::getHistogram(simpleHistogram &dest) {
    memset(dest, 0, sizeof(simpleHistogram));
    for (int Y = 0; Y < macontext->Video.Info.Height;Y++) {
        for (int X = 0; X < macontext->Video.Info.Width;X++) {
            uchar val = macontext->Video.Data.Plane[0][X+(Y*macontext->Video.Data.PlaneLinesize[0])];
            dest[val]++;
        }
    }
}


int cMarkAdOverlap::areSimilar(simpleHistogram &hist1, simpleHistogram &hist2) { // return > 0 if similar, else <= 0
    int similar = 0;
    for (int i = 0; i < 256; i++) {
        similar += abs(hist1[i] - hist2[i]);  // calculte difference, smaller is more similar
    }
    if (similar < similarCutOff) {
       return similar;
    }
    return -similar;
}


MarkAdPos *cMarkAdOverlap::Detect() {
    if (result.FrameNumberBefore == -1) return NULL;
    int startAfterMark = 0;
    int simcnt = 0;
    int tmpindexAfterStartMark = 0;
    int tmpindexBeforeStopMark = 0;
    result.FrameNumberBefore = -1;
    int firstSimilarBeforeStopMark = 0;
    for (int indexBeforeStopMark = 0; indexBeforeStopMark < histcnt[OV_BEFORE]; indexBeforeStopMark++) {
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Detect(): ------------------ testing frame (%5d) before stop mark, indexBeforeStopMark %d, against all frames after start mark", histbuf[OV_BEFORE][indexBeforeStopMark].framenumber, indexBeforeStopMark);
#endif
        for (int indexAfterStartMark = startAfterMark; indexAfterStartMark < histcnt[OV_AFTER]; indexAfterStartMark++) {
            int simil = areSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark].histogram);
#ifdef DEBUG_OVERLAP
            if (simil > 0) dsyslog("cMarkAdOverlap::Detect(): compare frame  (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) simcnt %2i similarMaxCnt %2i)", histbuf[OV_BEFORE][indexBeforeStopMark].framenumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].framenumber, indexAfterStartMark, simil, similarCutOff, simcnt, similarMaxCnt);
#endif
            if (simil > 0) {
                if (simcnt == 0) {  // this is the first similar frame pair, store position
                    firstSimilarBeforeStopMark = indexBeforeStopMark;
                }
                tmpindexAfterStartMark = indexAfterStartMark;
                tmpindexBeforeStopMark = indexBeforeStopMark;
                startAfterMark = indexAfterStartMark + 1;
                if (simil < (similarCutOff / 2)) simcnt += 2;
                else if (simil < (similarCutOff/4)) simcnt += 4;
                else if (simil < (similarCutOff/6)) simcnt += 6;
                else simcnt++;
                break;
            }
            else {
                if (simcnt > 0) {
                    indexBeforeStopMark = firstSimilarBeforeStopMark;  // reset to first similar frame
                }
                if (simcnt > similarMaxCnt) {
                    if ((histbuf[OV_BEFORE][tmpindexBeforeStopMark].framenumber > result.FrameNumberBefore) && (histbuf[OV_AFTER][tmpindexAfterStartMark].framenumber > result.FrameNumberAfter)) {
                        result.FrameNumberBefore = histbuf[OV_BEFORE][tmpindexBeforeStopMark].framenumber;
                        result.FrameNumberAfter = histbuf[OV_AFTER][tmpindexAfterStartMark].framenumber;
                    }
                }
                else {
                    startAfterMark = 0;
                }
                simcnt = 0;
            }
          }
    }
    if (result.FrameNumberBefore == -1) {
        if (simcnt > similarMaxCnt) {
            result.FrameNumberBefore = histbuf[OV_BEFORE][tmpindexBeforeStopMark].framenumber;
            result.FrameNumberAfter = histbuf[OV_AFTER][tmpindexAfterStartMark].framenumber;
        }
        else {
            return NULL;
        }
    }
    return &result;
}


MarkAdPos *cMarkAdOverlap::Process(const int FrameNumber, const int Frames, const bool BeforeAd, const bool H264) {
//    dsyslog("---cMarkAdOverlap::Process FrameNumber %i", FrameNumber);
//    dsyslog("---cMarkAdOverlap::Process Frames %i", Frames);
//    dsyslog("---cMarkAdOverlap::Process BeforeAd %i", BeforeAd);
//    dsyslog("---cMarkAdOverlap::Process H264 %i", H264);
//    dsyslog("---cMarkAdOverlap::Process lastframenumber %i", lastframenumber);
//    dsyslog("---cMarkAdOverlap::Process histcnt[OV_BEFORE] %i", histcnt[OV_BEFORE]);
//    dsyslog("---cMarkAdOverlap::Process histcnt[OV_AFTER] %i", histcnt[OV_AFTER]);
    if ((lastframenumber > 0) && (!similarMaxCnt)) {
        similarCutOff = 49000; // lower is harder! reduced from 50000 to 49000
//        if (H264) similarCutOff*=6;
        if (H264) similarCutOff *= 4;       // reduce false similar detection in H.264 streams
//        similarMaxCnt=4;
        similarMaxCnt = 10;
    }

    if (BeforeAd) {
        if ((histframes[OV_BEFORE]) && (histcnt[OV_BEFORE] >= histframes[OV_BEFORE])) {
            if (result.FrameNumberBefore) {
                Clear();
            }
            else {
                return NULL;
            }
        }
        if (!histbuf[OV_BEFORE]) {
            histframes[OV_BEFORE] = Frames;
            histbuf[OV_BEFORE] = new histbuffer[Frames+1];
            ALLOC(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        }
        getHistogram(histbuf[OV_BEFORE][histcnt[OV_BEFORE]].histogram);
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].framenumber = FrameNumber;
        histcnt[OV_BEFORE]++;
    }
    else {
        if (!histbuf[OV_AFTER]) {
            histframes[OV_AFTER] = Frames;
            histbuf[OV_AFTER] = new histbuffer[Frames+1];
            ALLOC(sizeof(*histbuf[OV_AFTER]), "histbuf");
        }

        if (histcnt[OV_AFTER]>=histframes[OV_AFTER]-1) {
            if (result.FrameNumberBefore) return NULL;
            return Detect();
        }
        getHistogram(histbuf[OV_AFTER][histcnt[OV_AFTER]].histogram);
        histbuf[OV_AFTER][histcnt[OV_AFTER]].framenumber = FrameNumber;
        histcnt[OV_AFTER]++;
    }
    lastframenumber = FrameNumber;
    return NULL;
}


cMarkAdVideo::cMarkAdVideo(MarkAdContext *maContext, cIndex *recordingIndex) {
    macontext = maContext;
    recordingIndexMarkAdVideo = recordingIndex;

    blackScreen = new cMarkAdBlackScreen(maContext);
    ALLOC(sizeof(*blackScreen), "blackScreen");

    hborder=new cMarkAdBlackBordersHoriz(maContext);
    ALLOC(sizeof(*hborder), "hborder");

    vborder=new cMarkAdBlackBordersVert(maContext);
    ALLOC(sizeof(*vborder), "vborder");

    logo = new cMarkAdLogo(maContext, recordingIndexMarkAdVideo);
    ALLOC(sizeof(*logo), "logo");

    overlap = NULL;
    Clear(false);
}


cMarkAdVideo::~cMarkAdVideo() {
    resetmarks();
    if (blackScreen) {
        FREE(sizeof(*blackScreen), "blackScreen");
        delete blackScreen;
    }
    if (hborder) {
        FREE(sizeof(*hborder), "hborder");
        delete hborder;
    }
    if (vborder) {
        FREE(sizeof(*vborder), "vborder");
        delete vborder;
    }
    if (logo) {
        FREE(sizeof(*logo), "logo");
        delete logo;
    }
    if (overlap) {
        FREE(sizeof(*overlap), "overlap");
        delete overlap;
    }
}


void cMarkAdVideo::Clear(bool isRestart, bool inBroadCast) {
    if (! isRestart) {
        aspectratio.Num=0;
        aspectratio.Den=0;
        if (hborder) hborder->Clear();
        if (vborder) vborder->Clear();
    }
    framelast=0;
    framebeforelast=0;
    if (blackScreen) blackScreen->Clear();
    if (logo) logo->Clear(isRestart, inBroadCast);
}


void cMarkAdVideo::resetmarks() {
    marks={};
}


bool cMarkAdVideo::addmark(int type, int position, MarkAdAspectRatio *before, MarkAdAspectRatio *after) {
    if (marks.Count>marks.maxCount) return false;
    if (before) {
        marks.Number[marks.Count].AspectRatioBefore.Num = before->Num;
        marks.Number[marks.Count].AspectRatioBefore.Den = before->Den;
    }
    if (after) {
        marks.Number[marks.Count].AspectRatioAfter.Num = after->Num;
        marks.Number[marks.Count].AspectRatioAfter.Den = after->Den;
    }
    marks.Number[marks.Count].Position = position;
    marks.Number[marks.Count].Type = type;
    marks.Count++;
    return true;
}


bool cMarkAdVideo::aspectratiochange(const MarkAdAspectRatio &a, const MarkAdAspectRatio &b, bool &start) {
    start = false;
    if ((a.Num == 0) || (a.Den == 0) || (b.Num == 0) || (b.Den == 0)) {
        if (((a.Num == 4) || (b.Num == 4)) && ((a.Den == 3) || (b.Den == 3))) {
            start = true;
        }
        else {
            return false;
        }
    }
    if ((a.Num != b.Num) && (a.Den != b.Den)) return true;
    return false;
}


MarkAdPos *cMarkAdVideo::ProcessOverlap(const int FrameNumber, const int Frames, const bool BeforeAd, const bool H264) {
    if (!FrameNumber) return NULL;
    if (!overlap) {
        overlap = new cMarkAdOverlap(macontext);
        ALLOC(sizeof(*overlap), "overlap");
    }
    if (!overlap) return NULL;
    return overlap->Process(FrameNumber, Frames, BeforeAd, H264);
}


MarkAdMarks *cMarkAdVideo::Process(int FrameNumber, int FrameNumberNext) {
    if ((!FrameNumber) && (!FrameNumberNext)) return NULL;

    resetmarks();
    if (!macontext->Video.Options.IgnoreBlackScreenDetection) {
        int blackret = blackScreen->Process();
        if ((blackret > 0) && (FrameNumber > 0)) {  // first frame can be invalid result
            addmark(MT_NOBLACKSTART, FrameNumber);
        }
        else {
            if ((blackret < 0) && (FrameNumber > 0)) { // first frame can be invalid result
                addmark(MT_NOBLACKSTOP, FrameNumber);
            }
        }
    }
    if (!macontext->Video.Options.ignoreHborder) {
        int hborderframenumber;
        int hret=hborder->Process(FrameNumber,&hborderframenumber);
        if ((hret>0) && (hborderframenumber!=-1)) {
            addmark(MT_HBORDERSTART,hborderframenumber);
        }
        if ((hret<0) && (hborderframenumber!=-1)) {
            addmark(MT_HBORDERSTOP,hborderframenumber);
        }
    }
    else if (hborder) hborder->Clear();

    if (!macontext->Video.Options.ignoreVborder) {
        int vborderframenumber;
        int vret=vborder->Process(FrameNumber,&vborderframenumber);
        if ((vret>0) && (vborderframenumber!=-1)) {
            addmark(MT_VBORDERSTART,vborderframenumber);
        }
        if ((vret<0) && (vborderframenumber!=-1)) {
            addmark(MT_VBORDERSTOP,vborderframenumber);
        }
    }
    else if (vborder) vborder->Clear();

    if (!macontext->Video.Options.IgnoreAspectRatio) {
        bool start;
        if (aspectratiochange(macontext->Video.Info.AspectRatio,aspectratio,start)) {
            if ((logo->Status()==LOGO_VISIBLE) && (!start)) {
                addmark(MT_LOGOSTOP,framebeforelast);
                logo->SetStatusLogoInvisible();
            }

            if ((vborder->Status()==VBORDER_VISIBLE) && (!start)) {
                addmark(MT_VBORDERSTOP,framebeforelast);
                vborder->SetStatusBorderInvisible();
            }

            if ((hborder->Status()==HBORDER_VISIBLE) && (!start)) {
                addmark(MT_HBORDERSTOP,framebeforelast);
                hborder->SetStatusBorderInvisible();
            }
            if (((macontext->Info.AspectRatio.Num == 4) && (macontext->Info.AspectRatio.Den == 3)) ||
                ((macontext->Info.AspectRatio.Num == 0) && (macontext->Info.AspectRatio.Den == 0))) {
                if ((macontext->Video.Info.AspectRatio.Num==4) && (macontext->Video.Info.AspectRatio.Den==3)) {
                    addmark(MT_ASPECTSTART,start ? FrameNumber : FrameNumberNext,&aspectratio,&macontext->Video.Info.AspectRatio);
                }
                else {
                    addmark(MT_ASPECTSTOP,framelast,&aspectratio,&macontext->Video.Info.AspectRatio);
                }
            }
            else {
                if ((macontext->Video.Info.AspectRatio.Num == 16) && (macontext->Video.Info.AspectRatio.Den == 9)) {
                    addmark(MT_ASPECTSTART,start ? FrameNumber : FrameNumberNext, &aspectratio, &macontext->Video.Info.AspectRatio);
                }
                else {
                    addmark(MT_ASPECTSTOP, framelast, &aspectratio, &macontext->Video.Info.AspectRatio);
                }
            }
        }

        aspectratio.Num = macontext->Video.Info.AspectRatio.Num;
        aspectratio.Den = macontext->Video.Info.AspectRatio.Den;
    }

    if (!macontext->Video.Options.IgnoreLogoDetection) {
        int logoframenumber = 0;
        int lret=logo->Process(FrameNumber, &logoframenumber);
        if ((lret >= -1) && (lret != 0) && (logoframenumber != -1)) {
            if (lret > 0) {
                addmark(MT_LOGOSTART, logoframenumber);
            }
            else {
                addmark(MT_LOGOSTOP, logoframenumber);
            }
        }
    }
    else {
        logo->SetStatusUninitialized();
    }

    framelast=FrameNumberNext;
    framebeforelast=FrameNumber;
    if (marks.Count) {
        return &marks;
    }
    else {
        return NULL;
    }
}


bool cMarkAdVideo::ReducePlanes() {
    if (!logo) return false;
    areaT *area = logo->GetArea();
    if (!area) return false;
    bool ret = false;
    for (int plane = 1; plane < PLANES; plane++) {
        if (area->valid[plane]) {
           area->valid[plane] = false;
           area->mpixel[plane] = 0;
           area->rpixel[plane] = 0;
           ret = true;
        }
    }
    return ret;
}
