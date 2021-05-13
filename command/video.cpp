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



cLogoSize::cLogoSize() {
}


cLogoSize::~cLogoSize() {
}


sLogoSize cLogoSize::GetDefaultLogoSize(const sMarkAdContext *maContext) {
    sLogoSize logoSize;
    switch (maContext->Video.Info.width) {
        case 544:
            logoSize.width  = 230;
            logoSize.height = 130;
            break;
        case 720:
            logoSize.width  = 230;
            logoSize.height = 130;
            break;
        case 1280:
            logoSize.width  = 400;
            logoSize.height = 200;
            break;
        case 1440:
            logoSize.width  = 400;
            logoSize.height = 200;
            break;
        case 1920:
            logoSize.width  = 400;
            logoSize.height = 200;
            break;
        default:
            dsyslog("cLogoSize::GetDefaultLogoSize() no default logo size rule for %dx%d", maContext->Video.Info.width, maContext->Video.Info.height);
            logoSize.width  = 400;
            logoSize.height = 200;
            break;
    }
    return logoSize;
}


sLogoSize cLogoSize::GetMaxLogoSize(const sMarkAdContext *maContext) {
    sLogoSize logoSize;
    switch (maContext->Video.Info.width) {
        case 544:
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
        case 720:
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
        case 1280:
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
        case 1440:
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
        case 1920:
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
        default:
            dsyslog("cLogoSize::GetDefaultLogoSize() no max logo size rule for %dx%d", maContext->Video.Info.width, maContext->Video.Info.height);
            logoSize.width  = 480;
            logoSize.height = 250;
            break;
    }
    return logoSize;
}


cMarkAdLogo::cMarkAdLogo(sMarkAdContext *maContextParam, cIndex *recordingIndex) {
    maContext = maContextParam;
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


cMarkAdLogo::~cMarkAdLogo() {
    Clear(false, false); // free memory for sobel plane

}


void cMarkAdLogo::Clear(const bool isRestart, const bool inBroadCast) {
    // free memory for sobel plane
    if (area.sobel) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.sobel");
        for (int plane = 0; plane < PLANES; plane++) {
            delete area.sobel[plane];
        }
        delete area.sobel;
        area.sobel = NULL;
    }
    // free memory for sobel masks
    if (area.mask) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.mask");
        for (int plane = 0; plane < PLANES; plane++) {
            delete area.mask[plane];
        }
        delete area.mask;
        area.mask = NULL;
    }
    // free memory for sobel result
    if (area.result) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.result");
        for (int plane = 0; plane < PLANES; plane++) {
            delete area.result[plane];
        }
        delete area.result;
        area.result = NULL;
    }
    area = {};

    if (isRestart) { // reset valid logo status after restart
        if (inBroadCast) area.status = LOGO_VISIBLE;
        else area.status = LOGO_INVISIBLE;
    }
    else area.status = LOGO_UNINITIALIZED;
}


sAreaT * cMarkAdLogo::GetArea() {
   return &area;
}


void cMarkAdLogo::SetLogoSize(const int width, const int height) {
    LOGOWIDTH = width;
    LOGOHEIGHT = height;
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
    if (fscanf(pFile, "P5\n#%1c%1i %4i\n%3d %3d\n255\n#", &c, &area.corner, &area.mPixel[plane], &width, &height) != 5) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }
    if (c == 'D') maContext->Audio.Options.ignoreDolbyDetection = true;

    if (height == 255) {
        height = width;
        width = area.mPixel[plane];
        area.mPixel[plane] = 0;
    }

    if ((width <= 0) || (height <= 0) || (width > LOGO_MAXWIDTH) || (height > LOGO_MAXHEIGHT) || (area.corner < TOP_LEFT) || (area.corner > BOTTOM_RIGHT)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }

    // alloc memory for mask planes (logo)
    if (area.mask) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.mask");
        for (int plane = 0; plane < PLANES; plane++) {
            delete area.mask[plane];
        }
        delete area.mask;
        area.mask = NULL;
    }
    area.mask = new uchar*[PLANES];
    for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
        area.mask[planeTMP] = new uchar[MAXPIXEL];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.mask");

    // read logo from file
    if (fread(area.mask[plane], 1, width * height, pFile) != (size_t)(width * height)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }
    fclose(pFile);

    if (area.mPixel[plane] == 0) {
        for (int i = 0; i < width * height; i++) {
            if (!area.mask[plane][i]) area.mPixel[plane]++;
        }
    }

    if (plane == 0) {   // plane 0 is the largest -> use this values
        LOGOWIDTH = width;
        LOGOHEIGHT = height;
    }
    maContext->Video.Logo.corner = area.corner;
    maContext->Video.Logo.height = LOGOHEIGHT;
    maContext->Video.Logo.width = LOGOWIDTH;

    area.valid[plane] = true;
    return 0;
}


// save the area.corner picture after sobel transformation to /tmp
// debug = 0: save was called by --extract function
// debug > 0: save was called by debug statements, add debug identifier to filename
// return: true if successful
//
bool cMarkAdLogo::Save(const int framenumber, uchar **picture, const short int plane, const int debug) {
    if (!maContext) return false;
    if ((plane<0) || (plane >= PLANES)) return false;
    if (!maContext->Info.ChannelName) return false;
    if (!maContext->Video.Info.width) {
        dsyslog("cMarkAdLogo::Save(): maContext->Video.Info.width not set");
        return false;
    }
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdLogo::Save(): maContext->Video.Info.height not set");
        return false;
    }
    if (!maContext->Video.Data.valid) return false;
    if (!maContext->Video.Data.PlaneLinesize[plane]) return false;
    if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
        dsyslog("cMarkAdLogo::Save(): LOGOWIDTH or LOGOHEIGHT not set");
        return false;
    }

    char *buf = NULL;
    if (debug == 0) {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i.pgm","/tmp/",framenumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane)==-1) return false;
    }
    else if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i_debug%d.pgm","/tmp/",framenumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane, debug)==-1) return false;
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
            *xstart = maContext->Video.Info.width - LOGOWIDTH;
            *xend = maContext->Video.Info.width;
            *ystart = 0;
            *yend = LOGOHEIGHT;
            break;
        case BOTTOM_LEFT:
            *xstart = 0;
            *xend = LOGOWIDTH;
            *ystart = maContext->Video.Info.height - LOGOHEIGHT;
            *yend = maContext->Video.Info.height;
            break;
        case BOTTOM_RIGHT:
            *xstart = maContext->Video.Info.width - LOGOWIDTH;
            *xend = maContext->Video.Info.width;
            *ystart = maContext->Video.Info.height - LOGOHEIGHT;
            *yend = maContext->Video.Info.height;
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
            fwrite(&maContext->Video.Data.Plane[plane][line * maContext->Video.Data.PlaneLinesize[plane] + xstart], 1, width, pFile);
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
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column];
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
// contrast   6, brightness 230, plane 1: pixel diff  10, plane 2: pixel diff   6
    if ((area.status == LOGO_VISIBLE) && (brightnessLogo < 230) && (contrastLogo <= 6)) {  // we have a very low contrast, now check full plane 1 and plane 2, change from 232 to 230
        int diffPixel_12[2] = {0};
        for (int line =  1; line <  (maContext->Video.Info.height / 2) - 1; line++) {  // ignore first and last line, they have sometimes weird pixel
            for (int column = 1; column < (maContext->Video.Info.width / 2) - 2; column++) { // ignore first and last column, they have sometimes weird pixel
                for (int plane = 1; plane < PLANES; plane++) {
                    int diff = abs(maContext->Video.Data.Plane[plane][line * maContext->Video.Data.PlaneLinesize[plane] + column] - maContext->Video.Data.Plane[plane][line * maContext->Video.Data.PlaneLinesize[plane] + column + 1]);
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
// contrast 116, brightness  93  // no logo in frame with black picture
// contrast  90, brightness 183
// contrast  13, brightness 128
//
// logo in bright area
// contrast 112, brightness 168
// contrast 109, brightness 185
// contrast 108, brightness 175
// contrast 108, brightness 145
// contrast 106, brightness 153
// contrast  90, brightness 172
// contrast  89, brightness 187
// contrast  81, brightness 206
// contrast  76, brightness 197
// contrast  76, brightness 190
// contrast  73, brightness 191
// contrast  72, brightness 194
// contrast  67, brightness 187
// contrast  64, brightness 173
// contrast  62, brightness 186
// contrast  55, brightness 204
// contrast  55, brightness 201
// contrast  54, brightness 207
// contrast  50, brightness 205
// contrast  49, brightness 217
// contrast  48, brightness 206
// contrast  45, brightness 206
// contrast  42, brightness 217
// contrast  40, brightness 204
// contrast  39, brightness 216
// contrast  38, brightness 205
// contrast  36, brightness 205
// contrast  35, brightness 218
// contrast  34, brightness 223
// contrast  33, brightness 221
// contrast  33, brightness 219
// contrast  33, brightness 219
// contrast  20, brightness 204
// contrast  17, brightness 206
// contrast  13, brightness 207
// contrast   3, brightness 206
// contrast   9, brightness 205
    if ((contrastLogo < 30) && (brightnessLogo >= 204)) {   // low contrast in bright area, we can not work with this
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): contrast in logo area too low");
#endif
        return BRIGHTNESS_ERROR; // nothing we can work with
    }
    if (contrastLogo > 116) { // this could not be the contrast of the logo area because it would be detected without calling ReduceBrightness(), changed from 90 to 116
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
    if (((contrastLogo >= 106) && (brightnessLogo >= 145)) ||
        ((contrastLogo >=  62) && (brightnessLogo >= 172)) ||  // changed from 197 to 190 to 187 to 173 to 172, changed from 79 to 72 to 67 to 64 to 62
        ((contrastLogo >=  30) && (brightnessLogo >= 201))) {
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
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column] - reduceBrightness;
            if (pixel > 128) pixel += 20;  // increase contrast around logo part brightness +- 128, do not do too much, otherwise clouds will detected as logo parts
            if (pixel < 128) pixel -= 20;
            if (pixel < 0) pixel = 0;
            if (pixel > 255) pixel = 255;
            maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column] = pixel;
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
    if (!maContext->Video.Data.PlaneLinesize[plane]) return false;

    // alloc memory for sobel transformed planes
    if (!area.sobel) {
        area.sobel = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.sobel[planeTMP] = new uchar[MAXPIXEL];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.sobel");
    }
    // alloc memory for mask planes (logo)
    if (!area.mask) {
        area.mask = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.mask[planeTMP] = new uchar[MAXPIXEL];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.mask");
    }
    // alloc memory for mask result (machtes)
    if (!area.result) {
        area.result = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.result[planeTMP] = new uchar[MAXPIXEL];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * MAXPIXEL, "area.result");
    }

    if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
        if (maContext->Video.Info.width > 720){
            LOGOHEIGHT = LOGO_DEFHDHEIGHT;
            LOGOWIDTH = LOGO_DEFHDWIDTH;
        }
        else {
            LOGOHEIGHT = LOGO_DEFHEIGHT;
            LOGOWIDTH = LOGO_DEFWIDTH;
        }
    }
    if ((maContext->Video.Info.pixFmt != 0) && (maContext->Video.Info.pixFmt != 12)) {
        if (!pixfmt_info) {
            esyslog("unknown pixel format %i, please report!", maContext->Video.Info.pixFmt);
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
    area.rPixel[plane] = 0;
    if (!plane) area.intensity = 0;
    for (int Y = ystart; Y <= yend - 1; Y++) {
        for (int X = xstart; X <= xend - 1; X++) {
            if (!plane) {
                area.intensity += maContext->Video.Data.Plane[plane][X + (Y * maContext->Video.Data.PlaneLinesize[plane])];
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
                        sumX = sumX + static_cast<int> ((*(maContext->Video.Data.Plane[plane] + X + I + (Y + J) * maContext->Video.Data.PlaneLinesize[plane])) * GX[I + 1][J + 1]);
                    }
                }

                // Y Gradient approximation
                for (int I = -1; I <= 1; I++) {
                    for (int J = -1; J <= 1; J++) {
                        sumY = sumY+ static_cast<int> ((*(maContext->Video.Data.Plane[plane] + X + I + (Y + J) * maContext->Video.Data.PlaneLinesize[plane])) * GY[I + 1][J + 1]);
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

            if (!area.result[plane][(X-xstart)+(Y-ystart)*width]) area.rPixel[plane]++;
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
int cMarkAdLogo::Detect(const int frameBefore, const int frameCurrent, int *logoFrameNumber) {
    bool onlyFillArea = ( *logoFrameNumber < 0 );
    bool extract = (maContext->Config->logoExtraction != -1);
    if (*logoFrameNumber == -2) extract = true;
    int rPixel = 0, mPixel = 0;
    int processed = 0;
    *logoFrameNumber = -1;
    if (area.corner == -1) return LOGO_NOCHANGE;
    float logo_vmark = LOGO_VMARK;
    if (maContext->Video.Logo.isRotating) logo_vmark *= 0.8;   // reduce if we have a rotating logo (e.g. SAT_1)i, changed from 0.9 to 0.8

    for (int plane = 0; plane < PLANES; plane++) {
        if ((area.valid[plane]) || (extract) || (onlyFillArea)) {
            if (SobelPlane(plane)) {
                processed++;
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + 200) && !onlyFillArea) {
                    Save(frameCurrent, area.sobel, plane, 1);
                }
#endif
            }
        }
        if (extract) {
            if (!Save(frameCurrent, area.sobel, plane, 0)) dsyslog("cMarkAdLogo::Detect(): save logo from frame (%d) failed", frameCurrent);
        }
        else {
//            tsyslog("plane %i area.rPixel[plane] %i area.mPixel[plane] %i", plane, area.rPixel[plane], area.mPixel[plane]);
            rPixel += area.rPixel[plane];
            mPixel += area.mPixel[plane];
        }
    }
    if (extract || onlyFillArea) return LOGO_NOCHANGE;
    if (processed == 0) return LOGO_ERROR;  // we have no plane processed

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) {
        SaveFrameCorner(frameCurrent, 1);  // we are not called by logo.cpp frame debug
    }
#endif

#ifdef DEBUG_LOGO_DETECTION
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
    // if we only have one plane we are "vulnerable"
    // to very bright pictures, so ignore them...
    int contrast = BRIGHTNESS_UNINITIALIZED;
    if (processed == 1) {
#define MAX_AREA_INTENSITY 94  // change from 128 to 127 to 126 to 125 to 114 to 100 to 98 to 94
                               // notice: there can be very bright logo parts in dark areas, this will result in a lower brightness
                               // we handle there cases in ReduceBrightness() when we detect contrast
        if (((area.intensity > MAX_AREA_INTENSITY) || // if we found no logo try to reduce brightness
            ((area.intensity >= 62) && (strcmp(maContext->Info.ChannelName, "NITRO") == 0))) &&  // workaround for NITRO, this channel has a very transparent logo, brightness reduction needed earlyer, changed from 80 to 76 to 62
             (area.intensity < 220) &&  // if we are to bright, this will not work, max changed from 200 to 220
           ((((area.status == LOGO_INVISIBLE) || (area.status == LOGO_UNINITIALIZED)) && (rPixel < (mPixel * logo_vmark))) || // if we found the logo ignore area intensity
           ((area.status == LOGO_VISIBLE) && (rPixel < (mPixel * LOGO_IMARK))))) {
            contrast = ReduceBrightness(frameCurrent);
            if (contrast > 0) {  // we got a contrast
                area.rPixel[0] = 0;
                rPixel = 0;
                mPixel = 0;
                SobelPlane(0);
                rPixel += area.rPixel[0];
                mPixel += area.mPixel[0];
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) corrected, new area intensity %d", frameCurrent, area.intensity);
                dsyslog("frame (%6i) rp=%5i | mp=%5i | mpV=%5.f | mpI=%5.f | i=%3i | c=%d | s=%i | p=%i", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) Save(frameCurrent, area.sobel, 0, 2);
#endif
                if ((area.status == LOGO_INVISIBLE) && (contrast < 25)) {  // if we have a very low contrast this could not be a new logo
                    return LOGO_NOCHANGE;
                }
                if ((area.status == LOGO_VISIBLE) && (contrast < 25) && (rPixel < (mPixel * LOGO_IMARK)) && (rPixel > (mPixel * LOGO_IMARK) / 3)){  // if we have a very low contrast and some matches this could be a logo on a very bright area
                    return LOGO_NOCHANGE;
                }
            }
            else {
                if ((contrast == BRIGHTNESS_SEPARATOR) && (area.status == LOGO_VISIBLE)) { // sepatation image detected
                    area.intensity = 0; // force detection of sepatation image
                }
            }
            if ((area.status == LOGO_VISIBLE) && (rPixel < (mPixel * LOGO_IMARK)) && (strcmp(maContext->Info.ChannelName, "NITRO") == 0)) return LOGO_NOCHANGE; // dont belive brightness reduction for NITRO, logo too transparent
        }
        // if we have still no match, try to copy colour planes into grey planes
        // we can even try this if plane 0 is too bright, maybe plane 1 or 2 are better
        // for performance reason we do this only for the known channel
        if (((area.intensity > MAX_AREA_INTENSITY) ||              // we have no valid result
             (((rPixel < (mPixel * logo_vmark)) &&                  // we have a valid result, but maybe we can find a new coloured logo
                 (area.status == LOGO_INVISIBLE)) ||
               ((rPixel < (mPixel * LOGO_IMARK)) &&                  // we have a valid result, but maybe we can re-find a coloured logo
                 (area.status == LOGO_VISIBLE))))  &&
             (strcmp(maContext->Info.ChannelName, "DMAX") == 0)) { // and only on channel DMAX
            // save state
            int intensityOld = area.intensity;
            int rPixelOld = rPixel;
            int mPixelOld = mPixel;

            if (!isInitColourChange) {
                LogoGreyToColour();
                isInitColourChange = true;
            }
            rPixel = 0;
            mPixel = 0;
            for (int plane = 1; plane < PLANES; plane++) {
            // reset all planes
                area.rPixel[plane] = 0;
                area.valid[plane] = true;
                area.mPixel[plane] = area.mPixel[0] / 4;
                SobelPlane(plane);
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - 200) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + 200)) {
                    Save(frameCurrent, area.sobel, plane, 3);
                }
#endif
                rPixel += area.rPixel[plane];
                mPixel += area.mPixel[plane];
                area.valid[plane] = false;
                area.mPixel[plane] = 0;
                area.rPixel[plane] = 0;
            }
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) maybe logo had a colour change, try plane 1 and plan 2", frameCurrent);
            dsyslog("frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
            if (rPixel < (mPixel * LOGO_IMARK)) { // we found no coloured logo here, restore old state
                area.intensity = intensityOld;
                rPixel = rPixelOld;
                mPixel = mPixelOld;
            }
            else contrast = BRIGHTNESS_VALID;  // ignore bightness, we found a coloured logo
        }
        if ((area.intensity > MAX_AREA_INTENSITY) && // still too bright
           ((contrast == BRIGHTNESS_ERROR) || (contrast == BRIGHTNESS_UNINITIALIZED)) &&
            (rPixel < (mPixel * logo_vmark))) {  // accept it, if we can see a logo
            return LOGO_NOCHANGE;
        }
    }
    else {  // if we have more planes we can still have a problem with bright frames
        if ((area.status == LOGO_VISIBLE) && (area.intensity > 150) && (rPixel < (mPixel * LOGO_IMARK))) return LOGO_NOCHANGE; // too bright, logo detection can be wrong
        if ((((area.status == LOGO_UNINITIALIZED) && (rPixel < (mPixel * logo_vmark))) ||  // at start make sure we get at least a quick initial logo visible
            ((area.status == LOGO_VISIBLE) && (rPixel < (mPixel * LOGO_IMARK)) && (rPixel > (mPixel * LOGO_IMARK * 0.75)))) &&  // we have a lot of machtes but not enough
             (area.intensity > 120)) {
            rPixel -= area.rPixel[0]; //  try without plane 0
            mPixel -= area.mPixel[0];
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
#endif
        }
    }

    int ret = LOGO_NOCHANGE;
    if (area.status == LOGO_UNINITIALIZED) { // Initialize
        if (rPixel >= (mPixel * logo_vmark)) {
            area.status = ret = LOGO_VISIBLE;
        }
        else {
            area.status = LOGO_INVISIBLE;
        }
        area.frameNumber = frameCurrent;
        *logoFrameNumber = frameCurrent;
    }

    if (rPixel >= (mPixel * logo_vmark)) {
        if (area.status == LOGO_INVISIBLE) {
            if (area.counter >= LOGO_VMAXCOUNT) {
                area.status = ret = LOGO_VISIBLE;
                *logoFrameNumber = area.frameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.frameNumber = frameCurrent;
                area.counter++;
            }
        }
        else {
            area.frameNumber = frameCurrent;
            area.counter = 0;
        }
    }

    if (rPixel < (mPixel * LOGO_IMARK)) {
        if (area.status == LOGO_VISIBLE) {
            if (area.counter >= LOGO_IMAXCOUNT) {
                area.status = ret = LOGO_INVISIBLE;
                *logoFrameNumber = area.frameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.frameNumber = frameBefore;
                area.counter++;
                if (contrast >= 0) area.counter++;  // if we had optimzed the picture and still no match, this should be valid
                if (rPixel < (mPixel * LOGO_IMARK / 2)) area.counter++;   // good detect for logo invisible
                if (rPixel < (mPixel * LOGO_IMARK / 4)) area.counter++;   // good detect for logo invisible
                if (rPixel == 0) {
                    area.counter++;   // very good detect for logo invisible
                    if (area.intensity <= 80) { // best detect, blackscreen without logo, increased from 30 to 70 to 80
                        dsyslog("cMarkAdLogo::Detect(): black screen without logo detected at frame (%d)", frameCurrent);
                        area.status = ret = LOGO_INVISIBLE;
                        *logoFrameNumber = area.frameNumber;
                        area.counter = 0;
                    }
                }
            }
        }
        else {
            area.counter = 0;
        }
    }

    if ((rPixel < (mPixel * logo_vmark)) && (rPixel > (mPixel * LOGO_IMARK))) {
        area.counter--;  // we are more uncertain of logo state
        if (area.counter < 0) area.counter = 0;
    }
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * LOGO_IMARK), area.intensity, area.counter, area.status, processed);
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
#endif
    return ret;
}


int cMarkAdLogo::Process(const int iFrameBefore, const int iFrameCurrent, const int frameCurrent, int *logoFrameNumber) {
    if (!maContext) return LOGO_ERROR;
    if (!maContext->Video.Data.valid) {
        area.status = LOGO_UNINITIALIZED;
        dsyslog("cMarkAdLogo::Process(): video data not valid at frame (%i)", iFrameCurrent);
        return LOGO_ERROR;
    }
    if (!maContext->Video.Info.width) {
        dsyslog("cMarkAdLogo::Process(): video width info missing");
        return LOGO_ERROR;
    }
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdLogo::Process(): video high info missing");
        return LOGO_ERROR;
    }
    if (!maContext->Config->logoDirectory[0]) {
        dsyslog("cMarkAdLogo::Process(): logoDirectory missing");
        return LOGO_ERROR;
    }
    if (!maContext->Info.ChannelName) {
        dsyslog("cMarkAdLogo::Process(): ChannelName missing");
        return LOGO_ERROR;
    }

    if (maContext->Config->logoExtraction == -1) {
        if ((area.AspectRatio.num != maContext->Video.Info.AspectRatio.num) || (area.AspectRatio.den != maContext->Video.Info.AspectRatio.den)) {
            dsyslog("cMarkAdLogo::Process(): aspect ratio changed from %i:%i to %i:%i, reload logo", area.AspectRatio.num, area.AspectRatio.den, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den);
            if (maContext->Info.checkedAspectRatio && (maContext->Info.AspectRatio.num == 4) && (maContext->Info.AspectRatio.den == 3) && (maContext->Video.Info.AspectRatio.num == 16) && (maContext->Video.Info.AspectRatio.den == 9)) {
                dsyslog("cMarkAdLogo::Process(): recording is 4:3, current frame is 16:9, we do not need a logo");
                maContext->Video.Options.ignoreLogoDetection = true;
            }
            else {
                char *buf=NULL;
                if (asprintf(&buf,"%s-A%i_%i", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den) != -1) {
                    ALLOC(strlen(buf)+1, "buf");
                    area.corner = -1;
                    bool logoStatus = false;
                    if (Load(maContext->Config->logoDirectory, buf, 0) == 0) {   // logo cache directory
                        isyslog("logo %s found in %s", buf, maContext->Config->logoDirectory);
                        logoStatus = true;
                        for (int plane = 1; plane < PLANES; plane++) {
                            if (Load(maContext->Config->logoDirectory, buf, plane) == 0) dsyslog("logo %s for plane %i found in %s", buf, plane, maContext->Config->logoDirectory);
                        }
                    }
                    else {
                        if (Load(maContext->Config->recDir,buf,0) == 0) {  // recording directory
                            isyslog("logo %s found in %s", buf, maContext->Config->recDir);
                            logoStatus = true;
                            for (int plane = 1; plane < PLANES; plane++) {
                                if (Load(maContext->Config->recDir, buf, plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, maContext->Config->recDir);
                            }
                        }
                        else {
                            if (maContext->Config->autoLogo > 0) {
                                isyslog("no valid logo for %s in logo cache and recording directory, extract logo from recording",buf);
                                cExtractLogo *ptr_cExtractLogo = new cExtractLogo(maContext->Video.Info.AspectRatio, recordingIndexMarkAdLogo);  // search logo from current frame
                                ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
                                if (ptr_cExtractLogo->SearchLogo(maContext, iFrameCurrent) > 0) dsyslog("cMarkAdLogo::Process(): no logo found in recording");
                                else dsyslog("cMarkAdLogo::Process(): new logo for %s found in recording",buf);
                                FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo"); // ptr_cExtraceLogo is valid because it it used above
                                delete ptr_cExtractLogo;
                                ptr_cExtractLogo = NULL;
                                if (Load(maContext->Config->recDir,buf,0) == 0) {  // try again recording directory
                                    isyslog("logo %s found in %s", buf, maContext->Config->recDir);
                                    logoStatus = true;
                                    for (int plane=1; plane < PLANES; plane++) {
                                        if (Load(maContext->Config->recDir,buf,plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, maContext->Config->recDir);
                                    }
                                }
                                else isyslog("still no valid logo for %s in recording directory",buf);
                            }
                        }
                    }
                    if (!logoStatus) {
                        dsyslog("cMarkAdLogo::Process(): no valid logo found for aspect ratio %i:%i, disable logo detection", maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den);
                        maContext->Video.Options.ignoreLogoDetection = true;
                    }
                    FREE(strlen(buf)+1, "buf");
                    free(buf);
                }
                else dsyslog("cMarkAdLogo::Process(): out of memory");
            }
            area.AspectRatio.num = maContext->Video.Info.AspectRatio.num;
            area.AspectRatio.den = maContext->Video.Info.AspectRatio.den;
        }
    }
    else {
        if ((LOGOWIDTH == 0) || (LOGOHEIGHT == 0)) {
            if ((maContext->Info.vPidType == MARKAD_PIDTYPE_VIDEO_H264) || (maContext->Info.vPidType == MARKAD_PIDTYPE_VIDEO_H265)) {
                LOGOHEIGHT = LOGO_DEFHDHEIGHT;
                LOGOWIDTH = LOGO_DEFHDWIDTH;
            }
            else if (maContext->Info.vPidType == MARKAD_PIDTYPE_VIDEO_H262) {
                LOGOHEIGHT = LOGO_DEFHEIGHT;
                LOGOWIDTH = LOGO_DEFWIDTH;
            }
            else {
                dsyslog("cMarkAdLogo::cMarkAdLogo maContext->Info.vPidType %i not valid", maContext->Info.vPidType);
                return LOGO_ERROR;
            }
        }
        area.AspectRatio.num = maContext->Video.Info.AspectRatio.num;
        area.AspectRatio.den = maContext->Video.Info.AspectRatio.den;
        area.corner = maContext->Config->logoExtraction;
        if (maContext->Config->logoWidth != -1) {
            LOGOWIDTH = maContext->Config->logoWidth;
        }
        if (maContext->Config->logoHeight != -1) {
            LOGOHEIGHT = maContext->Config->logoHeight;
        }
    }
    if (maContext->Config->fullDecode)  return Detect(frameCurrent - 1,  frameCurrent, logoFrameNumber);
    else return Detect(iFrameBefore, iFrameCurrent, logoFrameNumber);
}


// detect blackscreen
//
cMarkAdBlackScreen::cMarkAdBlackScreen(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
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
int cMarkAdBlackScreen::Process(__attribute__((unused)) const int frameCurrent) {
#define BLACKNESS 20  // maximum average brightness
    if (!maContext) return 0;
    if (!maContext->Video.Data.valid) return 0;
    if (maContext->Video.Info.framesPerSecond == 0) return 0;
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdBlackScreen::Process() missing maContext->Video.Info.height");
        return 0;
    }
    if (!maContext->Video.Info.width) {
        dsyslog("cMarkAdBlackScreen::Process() missing maContext->Video.Info.width");
        return 0;
    }
    if (!maContext->Video.Data.Plane[0]) {
        dsyslog("cMarkAdBlackScreen::Process() Video.Data.Plane[0] missing");
        return 0;
    }
    int end = maContext->Video.Info.height * maContext->Video.Info.width;
    int val = 0;
    int maxBrightness = BLACKNESS * end;
#ifdef DEBUG_BLACKSCREEN
    int debugVal = 0;
    for (int x = 0; x < end; x++) {
        debugVal += maContext->Video.Data.Plane[0][x];
    }
    debugVal /= end;
    dsyslog("cMarkAdBlackScreen::Process(): frame (%d) blackness %d (expect <%d)", frameCurrent, debugVal, BLACKNESS);
#endif
    for (int x = 0; x < end; x++) {
        val += maContext->Video.Data.Plane[0][x];
        if (val > maxBrightness) {
            if (blackScreenstatus != BLACKSCREEN_INVISIBLE) {
                blackScreenstatus = BLACKSCREEN_INVISIBLE;
                return 1; // detected stop of black screen
            }
            return 0;
        }
    }
    if (blackScreenstatus == BLACKSCREEN_INVISIBLE) {
        blackScreenstatus = BLACKSCREEN_VISIBLE;
        return -1; // detected start of black screen
    }
    return 0;
}


cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
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


int cMarkAdBlackBordersHoriz::Process(const int FrameNumber, int *BorderIFrame) {
#define CHECKHEIGHT 20
#define BRIGHTNESS_H_SURE 20
#define BRIGHTNESS_H_MAYBE 27  // some channel have logo in border, so we will get a higher value
#define VOFFSET 5
    if (!maContext) return HBORDER_ERROR;
    if (!maContext->Video.Data.valid) return HBORDER_ERROR;
    if (maContext->Video.Info.framesPerSecond == 0) return HBORDER_ERROR;
    *BorderIFrame = -1;   // framenumber if we has a change, otherwise -1
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() video hight missing");
        return HBORDER_ERROR;
    }
    int height = maContext->Video.Info.height - VOFFSET;

    if (!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() Video.Data.PlaneLinesize[0] not initalized");
        return HBORDER_ERROR;
    }
    int start = (height - CHECKHEIGHT) * maContext->Video.Data.PlaneLinesize[0];
    int end = height * maContext->Video.Data.PlaneLinesize[0];
    int valTop = 0;
    int valBottom = 0;
    int cnt = 0;
    int xz = 0;

    for (int x = start; x < end; x++) {
        if (xz < maContext->Video.Info.width) {
            valBottom += maContext->Video.Data.Plane[0][x];
            cnt++;
        }
        xz++;
        if (xz >= maContext->Video.Data.PlaneLinesize[0]) xz=0;
    }
    valBottom /= cnt;

    if (valBottom < BRIGHTNESS_H_MAYBE) { // we have a bottom border, test top border
        start = VOFFSET * maContext->Video.Data.PlaneLinesize[0];
        end = maContext->Video.Data.PlaneLinesize[0] * (CHECKHEIGHT+VOFFSET);
        cnt = 0;
        xz = 0;
        for (int x = start; x < end; x++) {
            if (xz < maContext->Video.Info.width) {
                valTop += maContext->Video.Data.Plane[0][x];
                cnt++;
            }
            xz++;
            if (xz >= maContext->Video.Data.PlaneLinesize[0]) xz=0;
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
                if (FrameNumber > (borderframenumber+maContext->Video.Info.framesPerSecond * MIN_H_BORDER_SECS)) {
                    *BorderIFrame = borderframenumber;
                    borderstatus = HBORDER_VISIBLE;
                    return HBORDER_VISIBLE; // detected start of black border
                }
            }
        }
    }
    else {
        if (borderstatus == HBORDER_VISIBLE) {
            *BorderIFrame = FrameNumber;
            borderstatus = HBORDER_INVISIBLE;
            borderframenumber = HBORDER_INVISIBLE;
            return HBORDER_INVISIBLE; // detected stop of black border
        }
        else {
            borderframenumber = -1; // restart from scratch
        }
    }
    return borderstatus;
}


cMarkAdBlackBordersVert::cMarkAdBlackBordersVert(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
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
    if (!maContext) {
        dsyslog("cMarkAdBlackBordersVert::Process(): maContext not valid");
        return VBORDER_ERROR;
    }
    if (!maContext->Video.Data.valid) return VBORDER_ERROR;  // no error, this is expected if bDecodeVideo is disabled
    if (maContext->Video.Info.framesPerSecond == 0) {
        dsyslog("cMarkAdBlackBordersVert::Process(): video frames per second  not valid");
        return VBORDER_ERROR;
    }
    *BorderIFrame = -1;

    int valLeft = 0;
    int valRight = 0;
    int cnt = 0;

    if(!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("Video.Data.PlaneLinesize[0] missing");
        return VBORDER_ERROR;
    }
    int end = maContext->Video.Data.PlaneLinesize[0] * (maContext->Video.Info.height - VOFFSET_);
    int i = VOFFSET_ * maContext->Video.Data.PlaneLinesize[0];
    while (i < end) {
        for (int x = 0; x < CHECKWIDTH; x++) {
            valLeft += maContext->Video.Data.Plane[0][HOFFSET + x + i];
            cnt++;
        }
        i += maContext->Video.Data.PlaneLinesize[0];
    }
    valLeft /= cnt;

    if (valLeft <= BRIGHTNESS_V) {
        cnt = 0;
        i = VOFFSET_ * maContext->Video.Data.PlaneLinesize[0];
        int w = maContext->Video.Info.width - HOFFSET - CHECKWIDTH;
        while (i < end) {
            for (int x = 0; x < CHECKWIDTH; x++) {
                valRight += maContext->Video.Data.Plane[0][w+x+i];
                cnt++;
            }
            i += maContext->Video.Data.PlaneLinesize[0];
        }
        valRight /= cnt;
    }

#ifdef DEBUG_VBORDER
    dsyslog("cMarkAdBlackBordersVert(): frame (%5d) valLeft %d valRight %d", FrameNumber, valLeft, valRight);
#endif
    if ((valLeft<= BRIGHTNESS_V) && (valRight <= BRIGHTNESS_V)) {
        if (borderframenumber == -1) {
            borderframenumber = FrameNumber;
        }
        else {
            if (borderstatus != VBORDER_VISIBLE) {
#ifdef DEBUG_VBORDER
                dsyslog("cMarkAdBlackBordersVert(): frame (%5d) duration %ds", FrameNumber, static_cast<int> ((FrameNumber - borderframenumber) /  maContext->Video.Info.framesPerSecond));
#endif
                if (FrameNumber > (borderframenumber + maContext->Video.Info.framesPerSecond * MIN_V_BORDER_SECS)) {
                    *BorderIFrame = borderframenumber;
                    borderstatus = VBORDER_VISIBLE;
                    return VBORDER_VISIBLE; // detected start of black border
                }
            }
        }
    }
    else {
        if (borderstatus == VBORDER_VISIBLE) {
            *BorderIFrame = FrameNumber;
            borderstatus = VBORDER_INVISIBLE;
            borderframenumber = -1;
            return VBORDER_INVISIBLE; // detected stop of black border
        }
        else {
            borderframenumber = -1; // restart from scratch
        }
    }
    return borderstatus;
}


cMarkAdOverlap::cMarkAdOverlap(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
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
    for (int Y = 0; Y < maContext->Video.Info.height;Y++) {
        for (int X = 0; X < maContext->Video.Info.width;X++) {
            uchar val = maContext->Video.Data.Plane[0][X+(Y*maContext->Video.Data.PlaneLinesize[0])];
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


sOverlapPos *cMarkAdOverlap::Detect() {
    if (result.frameNumberBefore == -1) return NULL;
    int startAfterMark = 0;
    int simcnt = 0;
    int tmpindexAfterStartMark = 0;
    int tmpindexBeforeStopMark = 0;
    result.frameNumberBefore = -1;
    int firstSimilarBeforeStopMark = 0;
    for (int indexBeforeStopMark = 0; indexBeforeStopMark < histcnt[OV_BEFORE]; indexBeforeStopMark++) {
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Detect(): ------------------ testing frame (%5d) before stop mark, indexBeforeStopMark %d, against all frames after start mark", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark);
#endif
        for (int indexAfterStartMark = startAfterMark; indexAfterStartMark < histcnt[OV_AFTER]; indexAfterStartMark++) {
            int simil = areSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark].histogram);
#ifdef DEBUG_OVERLAP
            if (simil > 0) dsyslog("cMarkAdOverlap::Detect(): compare frame  (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) simcnt %2i similarMaxCnt %2i)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simcnt, similarMaxCnt);
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
                    if ((histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber > result.frameNumberBefore) && (histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber > result.frameNumberAfter)) {
                        result.frameNumberBefore = histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber;
                        result.frameNumberAfter = histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber;
                    }
                }
                else {
                    startAfterMark = 0;
                }
                simcnt = 0;
            }
          }
    }
    if (result.frameNumberBefore == -1) {
        if (simcnt > similarMaxCnt) {
            result.frameNumberBefore = histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber;
            result.frameNumberAfter = histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber;
        }
        else {
            return NULL;
        }
    }
    return &result;
}


sOverlapPos *cMarkAdOverlap::Process(const int FrameNumber, const int Frames, const bool BeforeAd, const bool H264) {
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
            if (result.frameNumberBefore) {
                Clear();
            }
            else {
                return NULL;
            }
        }
        if (!histbuf[OV_BEFORE]) {
            histframes[OV_BEFORE] = Frames;
            histbuf[OV_BEFORE] = new sHistBuffer[Frames + 1];
            ALLOC(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        }
        getHistogram(histbuf[OV_BEFORE][histcnt[OV_BEFORE]].histogram);
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].frameNumber = FrameNumber;
        histcnt[OV_BEFORE]++;
    }
    else {
        if (!histbuf[OV_AFTER]) {
            histframes[OV_AFTER] = Frames;
            histbuf[OV_AFTER] = new sHistBuffer[Frames + 1];
            ALLOC(sizeof(*histbuf[OV_AFTER]), "histbuf");
        }

        if (histcnt[OV_AFTER]>=histframes[OV_AFTER] - 1) {
            if (result.frameNumberBefore) return NULL;
            return Detect();
        }
        getHistogram(histbuf[OV_AFTER][histcnt[OV_AFTER]].histogram);
        histbuf[OV_AFTER][histcnt[OV_AFTER]].frameNumber = FrameNumber;
        histcnt[OV_AFTER]++;
    }
    lastframenumber = FrameNumber;
    return NULL;
}


cMarkAdVideo::cMarkAdVideo(sMarkAdContext *maContextParam, cIndex *recordingIndex) {
    maContext = maContextParam;
    recordingIndexMarkAdVideo = recordingIndex;

    blackScreen = new cMarkAdBlackScreen(maContext);
    ALLOC(sizeof(*blackScreen), "blackScreen");

    hborder=new cMarkAdBlackBordersHoriz(maContext);
    ALLOC(sizeof(*hborder), "hborder");

    vborder=new cMarkAdBlackBordersVert(maContext);
    ALLOC(sizeof(*vborder), "vborder");

    logo = new cMarkAdLogo(maContext, recordingIndexMarkAdVideo);
    ALLOC(sizeof(*logo), "cMarkAdVideo_logo");

    overlap = NULL;
    Clear(false);
}


cMarkAdVideo::~cMarkAdVideo() {
    ResetMarks();
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
        FREE(sizeof(*logo), "cMarkAdVideo_logo");
        delete logo;
    }
    if (overlap) {
        FREE(sizeof(*overlap), "overlap");
        delete overlap;
    }
}


void cMarkAdVideo::Clear(bool isRestart, bool inBroadCast) {
    if (! isRestart) {
        aspectRatio.num=0;
        aspectRatio.den=0;
        if (hborder) hborder->Clear();
        if (vborder) vborder->Clear();
    }
    if (blackScreen) blackScreen->Clear();
    if (logo) logo->Clear(isRestart, inBroadCast);
}


void cMarkAdVideo::ResetMarks() {
    marks={};
}


bool cMarkAdVideo::AddMark(int type, int position, sAspectRatio *before, sAspectRatio *after) {
    if (marks.Count>marks.maxCount) return false;
    if (before) {
        marks.Number[marks.Count].AspectRatioBefore.num = before->num;
        marks.Number[marks.Count].AspectRatioBefore.den = before->den;
    }
    if (after) {
        marks.Number[marks.Count].AspectRatioAfter.num = after->num;
        marks.Number[marks.Count].AspectRatioAfter.den = after->den;
    }
    marks.Number[marks.Count].position = position;
    marks.Number[marks.Count].type = type;
    marks.Count++;
    return true;
}


bool cMarkAdVideo::AspectRatioChange(const sAspectRatio &AspectRatioA, const sAspectRatio &AspectRatioB, bool &start) {
    start = false;
    if ((AspectRatioA.num == 0) || (AspectRatioA.den == 0) || (AspectRatioB.num == 0) || (AspectRatioB.den == 0)) {
        if (((AspectRatioA.num == 4) || (AspectRatioB.num == 4)) && ((AspectRatioA.den == 3) || (AspectRatioB.den == 3))) {
            start = true;
        }
        else {
            return false;
        }
    }
    if ((AspectRatioA.num != AspectRatioB.num) && (AspectRatioA.den != AspectRatioB.den)) return true;
    return false;
}


sOverlapPos *cMarkAdVideo::ProcessOverlap(const int frameNumber, const int frameCount, const bool beforeAd, const bool isH264) {
    if (!frameNumber) return NULL;
    if (!overlap) {
        overlap = new cMarkAdOverlap(maContext);
        ALLOC(sizeof(*overlap), "overlap");
    }
    if (!overlap) return NULL;
    return overlap->Process(frameNumber, frameCount, beforeAd, isH264);
}


sMarkAdMarks *cMarkAdVideo::Process(int iFrameBefore, const int iFrameCurrent, const int frameCurrent) {
    if ((iFrameCurrent < 0) || (frameCurrent < 0)) return NULL;
    if (iFrameBefore < 0) iFrameBefore = 0; // this could happen at the start of recording

    int useFrame;
    if (maContext->Config->fullDecode) useFrame = frameCurrent;
    else useFrame = iFrameCurrent;
    ResetMarks();

    if ((frameCurrent > 0) && !maContext->Video.Options.ignoreBlackScreenDetection) { // first frame can be invalid result
        int blackret;
        blackret = blackScreen->Process(useFrame);
        if (blackret > 0) {
            if (maContext->Config->fullDecode) AddMark(MT_NOBLACKSTART, useFrame - 1);  // frame before is last frame with blackscreen
            else AddMark(MT_NOBLACKSTART, useFrame); // with iFrames only we must set mark on first frame after blackscreen to avoid start and stop on same iFrame
        }
        else {
            if (blackret < 0) {
                AddMark(MT_NOBLACKSTOP, useFrame);
            }
        }
    }
    int hret = HBORDER_ERROR;
    if (!maContext->Video.Options.ignoreHborder) {
        int hborderframenumber;
        hret = hborder->Process(useFrame, &hborderframenumber);  // we get start frame of hborder back
        if ((hret == HBORDER_VISIBLE) && (hborderframenumber >= 0)) {
            AddMark(MT_HBORDERSTART, hborderframenumber);
        }
        if ((hret == HBORDER_INVISIBLE) && (hborderframenumber >= 0)) {
            if (maContext->Config->fullDecode)  AddMark(MT_HBORDERSTOP, useFrame - 1);
            else AddMark(MT_HBORDERSTOP, iFrameBefore);  // we use iFrame before current frame as stop mark, this was the last frame with hborder
        }
    }
    else if (hborder) hborder->Clear();

    int vret = VBORDER_ERROR;
    if (!maContext->Video.Options.ignoreVborder) {
        int vborderframenumber;
        vret = vborder->Process(useFrame, &vborderframenumber);
        if ((vret == VBORDER_VISIBLE) && (vborderframenumber >= 0)) {
            if (hret == HBORDER_VISIBLE) dsyslog("cMarkAdVideo::Process(); hborder and vborder detected, ignore this, it is a very long black screen");
            else AddMark(MT_VBORDERSTART, vborderframenumber);
        }
        if ((vret == VBORDER_INVISIBLE) && (vborderframenumber >= 0)) {
            if (maContext->Config->fullDecode) AddMark(MT_VBORDERSTOP, useFrame - 1);
            else AddMark(MT_VBORDERSTOP, iFrameBefore);
        }
    }
    else if (vborder) vborder->Clear();

    if (!maContext->Video.Options.ignoreAspectRatio) {
        bool start;
        if (AspectRatioChange(maContext->Video.Info.AspectRatio, aspectRatio, start)) {
            if ((logo->Status() == LOGO_VISIBLE) && (!start)) {
                AddMark(MT_LOGOSTOP, iFrameBefore);
                logo->SetStatusLogoInvisible();
            }

            if ((vret == VBORDER_VISIBLE) && (!start)) {
                AddMark(MT_VBORDERSTOP, iFrameBefore);
                vborder->SetStatusBorderInvisible();
            }

            if ((hret == HBORDER_VISIBLE) && (!start)) {
                AddMark(MT_HBORDERSTOP, iFrameBefore);
                hborder->SetStatusBorderInvisible();
            }

            int startFrame;
            if (start && !maContext->Config->fullDecode) startFrame = iFrameBefore;
            else startFrame = frameCurrent;

            if (((maContext->Info.AspectRatio.num == 4) && (maContext->Info.AspectRatio.den == 3)) ||
                ((maContext->Info.AspectRatio.num == 0) && (maContext->Info.AspectRatio.den == 0))) {
                if ((maContext->Video.Info.AspectRatio.num == 4) && (maContext->Video.Info.AspectRatio.den == 3)) {
                    AddMark(MT_ASPECTSTART, startFrame, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
                else {
                    AddMark(MT_ASPECTSTOP, iFrameBefore, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
            }
            else {
                if ((maContext->Video.Info.AspectRatio.num == 16) && (maContext->Video.Info.AspectRatio.den == 9)) {
                    AddMark(MT_ASPECTSTART, startFrame, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
                else {
                    AddMark(MT_ASPECTSTOP, iFrameBefore, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
            }
        }

        aspectRatio.num = maContext->Video.Info.AspectRatio.num;
        aspectRatio.den = maContext->Video.Info.AspectRatio.den;
    }

    if (!maContext->Video.Options.ignoreLogoDetection) {
        int logoframenumber = 0;
        int lret=logo->Process(iFrameBefore, iFrameCurrent, frameCurrent, &logoframenumber);
        if ((lret >= -1) && (lret != 0) && (logoframenumber != -1)) {
            if (lret > 0) {
                AddMark(MT_LOGOSTART, logoframenumber);
            }
            else {
                AddMark(MT_LOGOSTOP, logoframenumber);
            }
        }
    }
    else {
        logo->SetStatusUninitialized();
    }

    if (marks.Count) {
        return &marks;
    }
    else {
        return NULL;
    }
}


bool cMarkAdVideo::ReducePlanes() {
    if (!logo) return false;
    sAreaT *area = logo->GetArea();
    if (!area) return false;
    bool ret = false;
    for (int plane = 1; plane < PLANES; plane++) {
        if (area->valid[plane]) {
           area->valid[plane] = false;
           area->mPixel[plane] = 0;
           area->rPixel[plane] = 0;
           ret = true;
        }
    }
    return ret;
}
