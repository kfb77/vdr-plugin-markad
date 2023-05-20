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

#include "debug.h"
#include "video.h"
#include "logo.h"


// global variables
extern bool abortNow;

cLogoSize::cLogoSize() {
}


cLogoSize::~cLogoSize() {
}


sLogoSize cLogoSize::GetDefaultLogoSize(const int width) {
    sLogoSize logoSize;
    if (videoWidth == 0) videoWidth = width;
    switch (videoWidth) {
        case 544:
            logoSize.width  =  230;
            logoSize.height =  130;
            break;
        case 720:
            logoSize.width  =  230;
            logoSize.height =  130;
            break;
        case 1280:
            logoSize.width  =  400;
            logoSize.height =  200;
            break;
        case 1440:
            logoSize.width  =  400;
            logoSize.height =  220;  // changed from 200 to 220 (BILD_HD)
            break;
        case 1920:
            logoSize.width  =  400;
            logoSize.height =  220;  // changed from 210 to 220
            break;
        case 3840:
            logoSize.width  = 1500;
            logoSize.height =  400;
            break;
        default:
            dsyslog("cLogoSize::GetDefaultLogoSize() no default logo size rule for video width %d", videoWidth);
            logoSize.width  =  400;
            logoSize.height =  200;
            break;
    }
    return logoSize;
}


sLogoSize cLogoSize::GetMaxLogoSize(const int width) {
    sLogoSize logoSize;
    if (videoWidth == 0) videoWidth = width;
    sLogoSize DefaultLogoSize = GetDefaultLogoSize(width);
    logoSize.width  = DefaultLogoSize.width  * 1.1;
    logoSize.height = DefaultLogoSize.height * 1.1;
    return logoSize;
}


int cLogoSize::GetMaxLogoPixel(const int width) {
    sLogoSize MaxLogoSize = GetMaxLogoSize(width);
    return MaxLogoSize.height * MaxLogoSize.width;
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
    Clear(false); // free memory for sobel plane

}


void cMarkAdLogo::Clear(const bool isRestart) {
#ifdef DEBUG_MEM
    if (!maContext) return;
    int maxLogoPixel = 0;
    if (area.sobel || area.mask || area.result)  maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
#endif

    // free memory for sobel plane
    if (area.sobel) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.sobel");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.sobel[plane];
        }
        delete[] area.sobel;
        area.sobel = NULL;
    }
    // free memory for sobel masks
    if (area.mask) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.mask");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.mask[plane];
        }
        delete[] area.mask;
        area.mask = NULL;
    }
    // free memory for sobel result
    if (area.result) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.result");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.result[plane];
        }
        delete[] area.result;
        area.result = NULL;
    }
    area = {};

    if (isRestart) area.status = LOGO_RESTART;
    else           area.status = LOGO_UNINITIALIZED;
}


sAreaT * cMarkAdLogo::GetArea() {
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
    sLogoSize MaxLogoSize = GetMaxLogoSize(maContext->Video.Info.width);
    if ((width <= 0) || (height <= 0) || (width > MaxLogoSize.width) || (height > MaxLogoSize.height) || (area.corner < TOP_LEFT) || (area.corner > BOTTOM_RIGHT)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }

    // alloc memory for mask planes (logo)
    if (plane == 0) {
        int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
        if (area.mask) {
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.mask");
            for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
                delete[] area.mask[planeTMP];
            }
            delete[] area.mask;
            area.mask = NULL;
        }
        area.mask = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.mask[planeTMP] = new uchar[maxLogoPixel];
            memset(area.mask[planeTMP], 0, sizeof(*area.mask[planeTMP]));
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.mask");
    }
    // read logo from file
    if (fread(area.mask[plane], 1, width * height, pFile) != (size_t)(width * height)) {
        fclose(pFile);
        esyslog("format error in %s", file);
        return -2;
    }
    fclose(pFile);

    // calulate pixel ratio for special logo detection
    if (area.mPixel[plane] == 0) {
        for (int i = 0; i < width * height; i++) {
            if (!area.mask[plane][i]) area.mPixel[plane]++;
        }
        dsyslog("cMarkAdLogo::Load(): logo plane 0 has %d pixel", area.mPixel[plane]);
        maContext->Video.Logo.pixelRatio = 1000 * area.mPixel[plane] / (width * height);
        dsyslog("cMarkAdLogo::Load(): logo pixel ratio of plane 0 is: %d per mille", maContext->Video.Logo.pixelRatio);
    }

    if (plane == 0) {   // plane 0 is the largest, use this values
        logoWidth = width;
        logoHeight = height;

        // set status for channel with transparent logos
        if (strcmp(maContext->Info.ChannelName, "SRF_zwei_HD") == 0) {
            dsyslog("cMarkAdLogo::Load(): channel %s has transparent logo, work with lower match values", maContext->Info.ChannelName);
            maContext->Video.Logo.isTransparent = true;
        }
    }

    maContext->Video.Logo.corner = area.corner;
    maContext->Video.Logo.height = logoHeight;
    maContext->Video.Logo.width  = logoWidth;

    area.valid[plane] = true;

    return 0;
}


// save the area.corner picture after sobel transformation to /tmp
// debug = 0: save was called by --extract function
// debug > 0: save was called by debug statements, add debug identifier to filename
// return: true if successful
//
bool cMarkAdLogo::Save(const int frameNumber, uchar **picture, const short int plane, const char *debug) {
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
    if ((logoWidth == 0) || (logoHeight == 0)) {
        dsyslog("cMarkAdLogo::Save(): logoWidth or logoHeight not set");
        return false;
    }

    char *buf = NULL;
    if (debug) {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i_debug_%s.pgm", "/tmp/", frameNumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane, debug)==-1) return false;
    }
    else {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i.pgm", "/tmp/", frameNumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane)==-1) return false;
    }
    ALLOC(strlen(buf)+1, "buf");

    // Open file
    FILE *pFile = fopen(buf, "wb");
    if (pFile == NULL) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        return false;
    }

    int width = logoWidth;
    int height = logoHeight;

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


bool cMarkAdLogo::SetCoordinates(int *xstart, int *xend, int *ystart, int *yend, const int plane) {
    switch (area.corner) {
        case TOP_LEFT:
            *xstart = 0;
            *xend   = logoWidth  - 1;
            *ystart = 0;
            *yend   = logoHeight - 1;
            break;
        case TOP_RIGHT:
            *xstart = maContext->Video.Info.width - 1 - logoWidth;
            *xend   = maContext->Video.Info.width - 1;
            *ystart = 0;
            *yend   = logoHeight - 1;
            break;
        case BOTTOM_LEFT:
            *xstart = 0;
            *xend   = logoWidth - 1;
            *ystart = maContext->Video.Info.height - 1 - logoHeight;
            *yend   = maContext->Video.Info.height - 1;
            break;
        case BOTTOM_RIGHT:
            *xstart = maContext->Video.Info.width  - 1 - logoWidth;
            *xend   = maContext->Video.Info.width  - 1;
            *ystart = maContext->Video.Info.height - 1 - logoHeight;
            *yend   = maContext->Video.Info.height - 1;
            break;
        default:
            return false;
    }
    if (plane > 0) {
        *xstart /= 2;
        *xend   /= 2;
        *ystart /= 2;
        *yend   /= 2;
    }
    return true;
}


// reduce brightness and increase contrast
// return result status as value from eBrightness
//        BRIGHTNESS_SEPARATOR:                   possible separation image detected
//        BRIGHTNESS_ERROR:                       if correction not possible
//
int cMarkAdLogo::ReduceBrightness(const int frameNumber, int *contrastReduced) {  // frameNumber used only for debugging
    int xstart, xend, ystart, yend;
    if (!SetCoordinates(&xstart, &xend, &ystart, &yend, 0)) return BRIGHTNESS_ERROR;

// set coorginates for logo pixel part in logo corner
    if ((logo_xstart == 0) && (logo_xend == 0) && (logo_ystart == 0) && (logo_yend == 0)) {
        switch (area.corner) {  // logo is usualy in the inner part of the logo corner
#define LOGO_MIN_PIXEL 30  // big enough to get in the main part of the logo
            case TOP_LEFT: {
                // xend and yend from logo coordinates
                logo_xend = xend;
                logo_yend = yend;

                // xstart is first column with pixel in logo area
                int pixelCount = 0;
                int column;
                int line;
                for (column = 0; column <= logoWidth; column++) {
                    for (line = 0; line <= logoHeight; line++) {
                        if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                        if (pixelCount > LOGO_MIN_PIXEL) break;
                    }
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                logo_xstart = column;

                // ystart is first line with pixel in logo area
                pixelCount = 0;
                for (line = 0; line <= logoHeight; line++) {
                    for (column = 0; column <= logoWidth; column++) {
                        if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                        if (pixelCount >= LOGO_MIN_PIXEL) break;
                    }
                    if (pixelCount >= LOGO_MIN_PIXEL) break;
                }
                logo_ystart = line;
                break;
            }
            case TOP_RIGHT: {
                // xstart and yend from logo coordinates
                logo_xstart = xstart;
                logo_yend   = yend;

                // xend is last column with pixel in logo area
                int pixelCount = 0;
                int column;
                int line;
                for (column = logoWidth; column >= 0; column--) {
                    for (line = 0; line <= logoHeight; line++) {
                        if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                        if (pixelCount > LOGO_MIN_PIXEL) break;
                    }
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                logo_xend = xend - (logoWidth - column);

                // ystart is first line with pixel in logo area
                pixelCount = 0;
                for (line = 0; line <= logoHeight; line++) {
                    for (column = 0; column <= logoWidth; column++) {
                        if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                        if (pixelCount >= LOGO_MIN_PIXEL) break;
                    }
                    if (pixelCount >= LOGO_MIN_PIXEL) break;
                }
                logo_ystart = line;
                break;
            }
            case BOTTOM_LEFT:
                logo_xstart = xend - (xend - xstart) / 2;
                logo_xend = xend;
                logo_ystart = ystart;
                logo_yend = yend - (yend - ystart) / 2;
                break;
            case BOTTOM_RIGHT:
                logo_xstart = xstart;
                logo_xend = xend - (xend - xstart) / 2 ;
                logo_ystart = ystart;
                logo_yend = yend - (yend - ystart) / 2;
                break;
            default:
                return BRIGHTNESS_ERROR;
                break;
        }
//        dsyslog("cMarkAdLogo::ReduceBrightness(): logo area: xstart %d xend %d, ystart %d yend %d", logo_xstart, logo_xend, logo_ystart, logo_yend);
    }

// detect contrast and brightness of logo part
    int minPixel = INT_MAX;
    int maxPixel = 0;
    int sumPixel = 0;
    for (int line = logo_ystart; line < logo_yend; line++) {
        for (int column = logo_xstart; column < logo_xend; column++) {
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column];
            if (pixel > maxPixel) maxPixel = pixel;
            if (pixel < minPixel) minPixel = pixel;
            sumPixel += pixel;
        }
    }
    int brightnessLogo = sumPixel / ((logo_yend - logo_ystart) * (logo_xend - logo_xstart));
    int contrastLogo = maxPixel - minPixel;
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo::ReduceBrightness(): logo area before reduction:  contrast %3d, brightness %3d", contrastLogo, brightnessLogo);
#endif

// check if we have a separation image (very bright with very log contrast in all planes)
//
// separation image without logo, still too bright after reduction, but need to detect as logo invisible to get a logo stop mark
// rp=    0, contrast  20, brightness 197, plane 1: pixel diff  40, plane 2: pixel diff  40  -> bright blue separator frame (ZDF)
// rp=    0, contrast  24, brightness 215, plane 1: pixel diff  29, plane 2: pixel diff  37  -> bright blue separator frame (ZDF)  (conflict)
//
// no separation images (bright picture with logo)
// rp=    0, contrast  23, brightness 207, plane 1: pixel diff  17, plane 2: pixel diff  26
// rp=    0, contrast  20, brightness 201, plane 1: pixel diff  10, plane 2: pixel diff   9
// rp=    0, contrast  18, brightness 210, plane 1: pixel diff  21, plane 2: pixel diff  20
// rp=    0, contrast  18, brightness 209, plane 1: pixel diff  23, plane 2: pixel diff  24
//
    if ((area.status == LOGO_VISIBLE) && (area.rPixel[0] == 0) && (brightnessLogo < 201) && (contrastLogo <= 20)) {  // we have a very low contrast, now check plane 1 and plane 2
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
        if ((diffPixel_12[0] <= 40) && (diffPixel_12[1] <= 40)) {
            dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%d): separation image detected", frameNumber);
            return BRIGHTNESS_SEPARATOR;
        }
    }

// check if contrast and brightness is valid
// build a curve from examples

    // very high contrast with not very high brightness in logo area, trust detection
    // false negativ, logo is visible but not detected
    // contrast 192, brightness 109
    // contrast 190, brightness 104
    if (((contrastLogo > 162) && (brightnessLogo < 104) && (maContext->Video.Logo.pixelRatio >  18)) ||   // contrastLogo changed from 164 to 162
                                                                                                          // brightnessLogo changed from 110 to 104
                                                                                                          // do not increase, we will get undetected logos in bright area
        ((contrastLogo > 162) && (brightnessLogo <  91) && (maContext->Video.Logo.pixelRatio <= 18))) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): very high contrast with not very high brightness in logo area, trust detection");
#endif
        return BRIGHTNESS_VALID; // if the is a logo we have detected it
    }


// logo with more than 16/1000 pixel in logo corner
//
// not detected logo without brightness reduction, detected with brightness reduction, take it as valid
// tbd
//
// not detected logo in bright area, also not detected with bridgtness reduction, take it as invalid
// contrast 194, brightness 147
// contrast 193, brightness 120
// contrast 192, brightness 165
// contrast 191, brightness 167
// contrast 191, brightness 133
// contrast 125, brightness 171
// contrast  60, brightness 174
// contrast  41, brightness 179
// contrast  38, brightness 172
// contrast  23, brightness 176
// contrast  23, brightness 181
// contrast  21, brightness 177
// contrast  17, brightness 182
// contrast  16, brightness 182
// contrast  13, brightness 176
//
// no logo in bright area, not detected without brightness reduction, detected with brightness reduction, take it as valid
// contrast 131, brightness 152
// contrast  94, brightness 158
// contrast  25, brightness 153

    if (maContext->Video.Logo.pixelRatio > 18) { // normal logo
        // build the curve
        if (((contrastLogo   <= 125) &&                          (brightnessLogo >  171)) ||
            ((contrastLogo   >  125) && (contrastLogo <= 131) && (brightnessLogo >  153)) ||
            ((contrastLogo   >  131) &&                          (brightnessLogo >= 133))) {
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::ReduceBrightness(): contrast/brightness in logo area is invalid for brightness reduction");
#endif
            return BRIGHTNESS_ERROR; //  nothing we can work with
        }
    }
// logo with 18/1000 or less pixel in logo corner (e.g. Kutonen_HD)
// brightness reduction does not work very well if we have only a few pixel
//
// not detected logo in bright area, also not detected with bridgtness reduction, take it as invalid
// contrast 189, brightness 127
// contrast 189, brightness 117
//
    else {  // logo with low pixel count
        if ((contrastLogo   <=  13) ||
            (brightnessLogo >= 117)) {
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::ReduceBrightness():  contrast/brightness in logo area is invalid for brightness reduction (low pixel logo)");
#endif
            return BRIGHTNESS_ERROR; //  nothing we can work with
        }
    }

// correct brightness and increase ontrast of plane 0
    minPixel = INT_MAX;
    maxPixel = 0;
#ifdef DEBUG_LOGO_DETECTION
    sumPixel = 0;
#endif

#define REDUCE_BRIGHTNESS 30
#define INCREASE_CONTRAST 2
    for (int line = ystart; line < yend; line++) {
        for (int column = xstart; column < xend; column++) {
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column] - REDUCE_BRIGHTNESS;
            if (pixel < 0) pixel = 0;
            pixel = INCREASE_CONTRAST * (pixel - 128) + 128;
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
    *contrastReduced = maxPixel - minPixel;

#ifdef DEBUG_LOGO_DETECTION
    int brightnessReduced = sumPixel / ((yend - ystart) * (xend - xstart));
    dsyslog("cMarkAdLogo::ReduceBrightness(): after brightness correction: contrast %3d, brightness %3d", *contrastReduced, brightnessReduced);
#endif

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameNumber > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameNumber < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = NULL;
        if (asprintf(&fileName,"%s/F__%07d_corrected.pgm", maContext->Config->recDir, frameNumber) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveFrameBuffer(maContext, fileName);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
    }
#endif

    return BRIGHTNESS_CHANGED;
}


bool cMarkAdLogo::SobelPlane(const int plane, int boundary) {
    if ((plane < 0) || (plane >= PLANES)) return false;
    if (!maContext->Video.Data.PlaneLinesize[plane]) return false;
    if (boundary < 2) boundary = 2; // we have to stay at least 1 pixel away from max pixel because of X Gradient approximation (-1 to +1) to prevent heap-buffer-overflow
                                    // need double of this for plane > 1

    // alloc memory for sobel transformed planes
    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
    if (!area.sobel) {
        area.sobel = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.sobel[planeTMP] = new uchar[maxLogoPixel];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.sobel");
    }

    // alloc memory for mask planes (logo) is done in Load()

    // alloc memory for mask result (machtes)
    if (!area.result) {
        area.result = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.result[planeTMP] = new uchar[maxLogoPixel];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.result");
    }

    if ((logoWidth == 0) || (logoHeight == 0)) {
        sLogoSize DefaultLogoSize = GetDefaultLogoSize(maContext->Video.Info.width);
        logoHeight = DefaultLogoSize.height;
        logoWidth  = DefaultLogoSize.width;
    }
    if ((maContext->Video.Info.pixFmt != 0) && (maContext->Video.Info.pixFmt != 12)) {
        if (!pixfmt_info) {
            esyslog("unknown pixel format %i, please report!", maContext->Video.Info.pixFmt);
            pixfmt_info = true;
        }
        return false;
    }
    int xstart = 0;
    int xend   = 0;
    int ystart = 0;
    int yend   = 0;
    if (!SetCoordinates(&xstart, &xend, &ystart, &yend, plane)) return false;
    int xStart = xstart;
    int xEnd   = xend;
    int yStart = ystart;
    int yEnd   = yend;
    // we need 2 more pixel for sobel calculation to get a full logo picture
    int div = 1;
    if (plane > 0) div = 2;
    if ((xstart > 0) && (xstart < ((maContext->Video.Info.width  / div) - 3))) xstart += 2;
    if ((xend   > 0) && (xend   < ((maContext->Video.Info.width  / div) - 3))) xend   += 2;
    if ((ystart > 0) && (ystart < ((maContext->Video.Info.height / div) - 3))) ystart += 2;
    if ((yend   > 0) && (yend   < ((maContext->Video.Info.height / div) - 3))) yend   += 2;
//    dsyslog("cMarkAdLogo::SobelPlane(): plane %d: xStart %d, xEnd %d, yStart %d, yEnd %d", plane, xStart, xEnd, yStart, yEnd);
//    dsyslog("cMarkAdLogo::SobelPlane(): plane %d: xstart %d, xend %d, ystart %d, yend %d", plane, xstart, xend, ystart, yend);

    int cutval = 127;
    int width  = logoWidth;
    if (plane > 0) {
        boundary /= 2;
        cutval   /= 2;
        width    /= 2;
    }
    int SUM;
    int sumX, sumY;
    area.rPixel[plane] = 0;
    if (plane == 0) area.intensity = 0;
    for (int Y = ystart; Y <= yend; Y++) {
        for (int X = xstart; X <= xend; X++) {
            if (plane == 0) {
                area.intensity += maContext->Video.Data.Plane[plane][X + (Y * maContext->Video.Data.PlaneLinesize[plane])];
            }
            sumX = 0;
            sumY = 0;

            // image boundaries
            if (Y <= (ystart + boundary) || Y >= (yend - boundary)) SUM = 0;
            else if (X <= (xstart + boundary) || X >= (xend - boundary)) SUM = 0;
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
            if (SUM <  cutval) SUM =   0;

            int val = 255 - (uchar) SUM;

            area.sobel[plane][(X - xstart) + (Y - ystart) * width] = val;

            // only store results in logo coordinates range
            if ((area.valid[plane] && (X >= xStart) && (X <= xEnd) && (Y >= yStart) && (Y <= yEnd))) {  // if we are called by logo search, we have no valid area.mask
                area.result[plane][(X - xstart) + (Y - ystart) * width] = (area.mask[plane][(X - xstart) + (Y - ystart) * width] + val) & 255;
                if (!area.result[plane][(X - xstart) + (Y - ystart) * width]) area.rPixel[plane]++;
            }
        }
    }
    if (!plane) area.intensity /= (logoHeight * width);
    return true;
}


// copy all black pixels from logo pane 0 into plan 1 and plane 2
// we need this for channels with usually grey logos, but at start and end they can be red (DMAX)
//
void cMarkAdLogo::LogoGreyToColour() {
    for (int line = 0; line < logoHeight; line++) {
        for (int column = 0; column < logoWidth; column++) {
            if (area.mask[0][line * logoWidth + column] == 0 ){
                area.mask[1][line / 2 * logoWidth / 2 + column / 2] = 0;
                area.mask[2][line / 2 * logoWidth / 2 + column / 2] = 0;
            }
            else {
                area.mask[1][line / 2 * logoWidth / 2 + column / 2] = 255;
                area.mask[2][line / 2 * logoWidth / 2 + column / 2] = 255;
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
    float logo_imark = LOGO_IMARK;

    if (maContext->Video.Logo.isRotating) logo_vmark *= 0.8;  // reduce if we have a rotating logo (e.g. SAT_1), changed from 0.9 to 0.8
    if (maContext->Video.Logo.isTransparent) { // reduce if we have a transparent logo (e.g. SRF_zwei_HD)
        logo_vmark *= 0.95;
        logo_imark *= 0.95;
    }

    for (int plane = 0; plane < PLANES; plane++) {
        if ((area.valid[plane]) || (extract) || (onlyFillArea)) {
            int boundary = 0;               // logo detection ignores lines in corner with sobel.mask (logo), we want to use full logo surface
            if (onlyFillArea) boundary = 5; // called by cExtractLogo, need boundary to remove lines in corner
            if (SobelPlane(plane, boundary)) {
                processed++;

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && !onlyFillArea) {
                    int width  = maContext->Video.Logo.width;
                    int height = maContext->Video.Logo.height;
                    if (plane > 0) {
                        width  /= 2;
                        height /= 2;
                    }
                    char *fileName = NULL;
                    if (asprintf(&fileName,"%s/F__%07d-P%d-C%1d_0sobel.pgm", maContext->Config->recDir, frameCurrent, plane, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.sobel[plane], width, height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                    if (asprintf(&fileName,"%s/F__%07d-P%d-C%1d_1mask.pgm", maContext->Config->recDir, frameCurrent, plane, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.mask[plane], width, height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                    if (asprintf(&fileName,"%s/F__%07d-P%d-C%1d_2result.pgm", maContext->Config->recDir, frameCurrent, plane, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.result[plane], width, height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                }
#endif
            }
        }
        if (extract) {
            if (!Save(frameCurrent, area.sobel, plane, NULL)) dsyslog("cMarkAdLogo::Detect(): save logo from frame (%d) failed", frameCurrent);
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
    if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
//        SaveFrameCorner(frameCurrent, 1);  // we are not called by logo.cpp frame debug
    }
#endif

#ifdef DEBUG_LOGO_DETECTION
    char detectStatus[] = "o";
    if (rPixel > (mPixel * logo_vmark)) strcpy(detectStatus, "+");
    if (rPixel < (mPixel * logo_imark)) strcpy(detectStatus, "-");
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("cMarkAdLogo::Detect(): frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * logo_imark), area.intensity, area.counter, area.status, processed, detectStatus);
#endif
    // if we only have one plane we are "vulnerable"
    // to very bright pictures, so ignore them...
    int brightnessState = BRIGHTNESS_UNINITIALIZED;
    int contrastReduced = -1;
    if (processed == 1) {   // we have only 1 plane (no coloured logo

        // prevent to detect logo start on very bright background, this is not possible
        if ((area.status == LOGO_INVISIBLE) && (rPixel > (mPixel * logo_vmark)) && area.intensity >= 218) {  // possible state change from invisible to visible
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) to bright %d for logo start", frameCurrent, area.intensity);
#endif
            return LOGO_NOCHANGE;
        }

        // prevent to detect background patten as logo
        if ((area.status == LOGO_INVISIBLE) && (rPixel > (mPixel * logo_vmark))) {  // possible state change from invisible to visible
            int black = 0;
            for (int i = 0; i < logoHeight * logoWidth; i++) {
                if (area.sobel[0][i] == 0) black++;
            }
            int quote = 100 * black / (logoHeight * logoWidth);
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) pixel quote: %d%%", frameCurrent, quote);
#endif
            if (quote > 45) return LOGO_NOCHANGE; // there is a pattern on the backbround, no logo detection possible
                                                  // changed from 27 to 45
                                                  // do not reduce, we need to detect logo on trees or gras background
        }

        // check area intensitiy
#define MAX_AREA_INTENSITY 75  // change from 128 to 127 to 126 to 125 to 114 to 100 to 98 to 94 to 80 to 75
                               // notice: there can be very bright logo parts in dark areas, this will result in a lower brightness
                               // we handle this cases in ReduceBrightness() when we detect contrast
#define AREA_INTENSITY_NO_TRUST 70 // we do not trust very close result under this area intensity
#define QUOTE_NO_TRUST 0.75        // at least this quote of matches we should have
        if (((area.intensity > MAX_AREA_INTENSITY) ||                                            // if area is bright
            // if we have a change from logo visable to logo invisable with very close result, verify it
            ((area.intensity >= AREA_INTENSITY_NO_TRUST) && (area.status == LOGO_VISIBLE) && (rPixel < (mPixel * logo_imark)) && (rPixel > (QUOTE_NO_TRUST * mPixel * logo_imark))) ||
            ((area.intensity >= 56) && (maContext->Video.Logo.pixelRatio <= 16)) ||  // logo with low pixel count, check with lowe value
            ((area.intensity >= 62) && (strcmp(maContext->Info.ChannelName, "NITRO") == 0))) &&  // workaround for NITRO, this channel has a very transparent logo,
                                                                                                 // brightness reduction needed earlyer, changed from 80 to 76 to 62
             (area.intensity < 220) &&  // if we are to bright, this will not work, max changed from 200 to 220
           ((((area.status == LOGO_INVISIBLE) || (area.status == LOGO_UNINITIALIZED)) && (rPixel < (mPixel * logo_vmark))) || // only status is no logo visable
                                                                                                                              // and we have no clear result or
           ((area.status == LOGO_VISIBLE) && (rPixel < (mPixel * logo_imark))))) {                                            // status is logo and we found no logo
            // reduce brightness and increase contrast
            brightnessState = ReduceBrightness(frameCurrent, &contrastReduced);
            if (brightnessState > BRIGHTNESS_VALID) {  // we got a new contrast, redo logo detection
                area.rPixel[0] = 0;
                rPixel = 0;
                mPixel = 0;
                SobelPlane(0, 0);
                rPixel += area.rPixel[0];
                mPixel += area.mPixel[0];
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) corrected: new area intensity: %d, brightnessState: %d, contrastReduced: %d", frameCurrent, area.intensity, brightnessState, contrastReduced);
                char detectStatus[] = "o";
                if (rPixel > (mPixel * logo_vmark)) strcpy(detectStatus, "+");
                if (rPixel < (mPixel * logo_imark)) strcpy(detectStatus, "-");
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * logo_imark), area.intensity, area.counter, area.status, processed, detectStatus);
#endif
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
                    char *fileName = NULL;
                    if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_3sobelCorrected.pgm", maContext->Config->recDir, frameCurrent, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.sobel[0], maContext->Video.Logo.width, maContext->Video.Logo.height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                    if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_4resultCorrected.pgm", maContext->Config->recDir, frameCurrent, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.result[0], maContext->Video.Logo.width, maContext->Video.Logo.height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                }
#endif
                // check background pattern
                int black = 0;
                for (int i = 0; i < logoHeight * logoWidth; i++) {
                    if (area.sobel[0][i] == 0) black++;
                }
                int quote = 100 * black / (logoHeight * logoWidth);
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) pixel quote after brighness reduction: %d%%", frameCurrent, quote);
#endif
                if (quote > 22) return LOGO_NOCHANGE; // there is a pattern on the backbround, no logo detection possible
                                                      // changed from 27 to 25

                // check new brightness
                if (((area.intensity >= 152) && (rPixel >  0)) || // still too bright, we can not use the result
                    ((area.intensity >= 208) && (rPixel == 0))) {
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cMarkAdLogo::Detect(): frame (%6d) brightness reducation successful, but logo area still too bright", frameCurrent);
#endif
                    return LOGO_NOCHANGE;
                }
                if ((area.status == LOGO_INVISIBLE) && contrastReduced < 25) {  // if we have a very low contrast this could not be a new logo
                    return LOGO_NOCHANGE;
                }
                // if we have a very low contrast and some matches this could be a logo on a very bright area
                if ((area.status == LOGO_VISIBLE) && (area.intensity > 8) && (contrastReduced < 25) && (rPixel < (mPixel * logo_imark)) && (rPixel > (mPixel * logo_imark) / 3)) {
                    return LOGO_NOCHANGE;
                }
            }
            else {
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) not valid after brightness reduction", frameCurrent);
#endif
                if ((brightnessState == BRIGHTNESS_SEPARATOR) && (area.status == LOGO_VISIBLE)) { // sepatation image detected
                    area.intensity = 0; // force detection of sepatation image
                }

                // we have a low pixel logo
                if ((area.intensity >= 56) && (maContext->Video.Logo.pixelRatio <= 16)) return LOGO_NOCHANGE;

                // we have a close result and can not reduce brightness, do not trust result
                if ((brightnessState == BRIGHTNESS_ERROR) && (area.intensity >= AREA_INTENSITY_NO_TRUST) && (area.status == LOGO_VISIBLE) &&
                    (rPixel < (mPixel * logo_imark)) && (rPixel > (QUOTE_NO_TRUST * mPixel * logo_imark))) return LOGO_NOCHANGE;
            }
            // dont belive brightness reduction for NITRO if we got a low contrast, logo too transparent, changed from 217
            if ((area.status == LOGO_VISIBLE) && (rPixel < (mPixel * logo_imark)) && (contrastReduced < 217) && (strcmp(maContext->Info.ChannelName, "NITRO") == 0)) return LOGO_NOCHANGE;
        }
        // if we have still no match, try to copy colour planes into grey planes
        // we can even try this if plane 0 is too bright, maybe plane 1 or 2 are better
        // for performance reason we do this only for the known channel
        if (((area.intensity > MAX_AREA_INTENSITY) ||              // we have no valid result
             (((rPixel < (mPixel * logo_vmark)) &&                  // we have a valid result, but maybe we can find a new coloured logo
                 (area.status == LOGO_INVISIBLE)) ||
               ((rPixel < (mPixel * logo_imark)) &&                  // we have a valid result, but maybe we can re-find a coloured logo
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
                SobelPlane(plane, 0);
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
                    Save(frameCurrent, area.sobel, plane, "area.sobel_coloured");
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
            char detectStatus[] = "o";
            if (rPixel > (mPixel * logo_vmark)) strcpy(detectStatus, "+");
            if (rPixel < (mPixel * logo_imark)) strcpy(detectStatus, "-");
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * logo_imark), area.intensity, area.counter, area.status, processed, detectStatus);
#endif
            if (rPixel < (mPixel * logo_imark)) { // we found no coloured logo here, restore old state
                area.intensity = intensityOld;
                rPixel = rPixelOld;
                mPixel = mPixelOld;
            }
            else brightnessState = BRIGHTNESS_VALID;  // ignore bightness, we found a coloured logo
        }
        if ((area.intensity > MAX_AREA_INTENSITY) && // still too bright
           ((brightnessState == BRIGHTNESS_ERROR) || (brightnessState == BRIGHTNESS_UNINITIALIZED)) &&
            (rPixel < (mPixel * logo_vmark))) {  // accept it, if we can see a logo
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) too bright", frameCurrent);
#endif
            return LOGO_NOCHANGE;
        }
    }
    else {  // if we have more planes we can still have a problem with coloured logo on same colored background
        // too bright
        if ((area.status == LOGO_VISIBLE) && (area.intensity >= 142) &&              // too bright, logo detection can be wrong, changed from 195 to 142
                                                                                     // do not increase, will get too much false logo invisible on all planes
            (rPixel > 0) && (rPixel < (mPixel * logo_imark))) return LOGO_NOCHANGE; // in very bright pictures try to get result only if we have some matches

        // maybe coloured logo on same colored background, try without plane 0
        if ((((area.status == LOGO_UNINITIALIZED) && (rPixel < (mPixel * logo_vmark))) ||  // at start make sure we get at least a quick initial logo visible
             ((area.status == LOGO_INVISIBLE)     && (rPixel < (mPixel * logo_vmark)) && (rPixel > (mPixel * logo_imark * 0.8))) ||  // try get early logo start on bright background
             ((area.status == LOGO_VISIBLE)       && (rPixel < (mPixel * logo_imark)) && (rPixel > (mPixel * logo_imark * 0.3)))) && // we have a some machtes but not enough
                                                                                                                               // changed from 0.5 to 0.4 to 0.3
             (area.intensity > 50)) {  // changed from 120 to 50
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect():       plane 0: rp=%5d | mp=%5d", area.rPixel[0], area.mPixel[0]);
            dsyslog("cMarkAdLogo::Detect():       plane 1: rp=%5d | mp=%5d", area.rPixel[1], area.mPixel[1]);
            dsyslog("cMarkAdLogo::Detect():       plane 2: rp=%5d | mp=%5d", area.rPixel[2], area.mPixel[2]);
#endif
            if ((area.rPixel[1] + area.rPixel[2]) > 0) {  // if we have no result in plane 1/2, do not use it
                rPixel -= area.rPixel[0]; //  try without plane 0
                mPixel -= area.mPixel[0];
#ifdef DEBUG_LOGO_DETECTION
                char detectStatus[] = "o";
                if (rPixel > (mPixel * logo_vmark)) strcpy(detectStatus, "+");
                if (rPixel < (mPixel * logo_imark)) strcpy(detectStatus, "-");
                dsyslog("cMarkAdLogo::Detect(): frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * logo_imark), area.intensity, area.counter, area.status, processed, detectStatus);
#endif
            }
        }
    }

// set logo visible/unvisible status
    // set initial start status
    if (area.status == LOGO_UNINITIALIZED) {
        if (rPixel >= (mPixel * logo_vmark)) area.status = LOGO_VISIBLE;
        else                                 area.status = LOGO_INVISIBLE;
        *logoFrameNumber = frameCurrent;
        area.frameNumber = frameCurrent;
        return area.status;
    }
    if (area.status == LOGO_RESTART) {
        if (rPixel >= (mPixel * logo_vmark)) area.status = LOGO_VISIBLE;
        if (rPixel <  (mPixel * logo_imark)) area.status = LOGO_INVISIBLE;  // wait for a clear result
        *logoFrameNumber = -1;   // no logo change report after detection restart
        area.frameNumber = frameCurrent;
        return area.status;
    }


    int ret = LOGO_NOCHANGE;
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

    if (rPixel < (mPixel * logo_imark)) {
        if (area.status == LOGO_VISIBLE) {
            if (area.counter >= LOGO_IMAXCOUNT) {
                area.status = ret = LOGO_INVISIBLE;
                *logoFrameNumber = area.frameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.frameNumber = frameBefore;
                area.counter++;
                if (brightnessState >= BRIGHTNESS_VALID) area.counter++;  // if we had optimzed the picture and still no match, this should be valid
                if (rPixel < (mPixel * logo_imark / 2)) area.counter++;   // good detect for logo invisible
                if (rPixel < (mPixel * logo_imark / 4)) area.counter++;   // good detect for logo invisible
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

    if ((rPixel < (mPixel * logo_vmark)) && (rPixel > (mPixel * logo_imark))) {
        area.counter--;  // we are more uncertain of logo state
        if (area.counter < 0) area.counter = 0;
    }
#ifdef DEBUG_LOGO_DETECTION
    strcpy(detectStatus, "o");
    if (rPixel > (mPixel * logo_vmark)) strcpy(detectStatus, "+");
    if (rPixel < (mPixel * logo_imark)) strcpy(detectStatus, "-");
    dsyslog("cMarkAdLogo::Detect(): frame (%6d) rp=%5d | mp=%5d | mpV=%5.f | mpI=%5.f | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, mPixel, (mPixel * logo_vmark), (mPixel * logo_imark), area.intensity, area.counter, area.status, processed, detectStatus);
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
#endif
    return ret;
}


int cMarkAdLogo::Process(const int iFrameBefore, const int iFrameCurrent, const int frameCurrent, int *logoFrameNumber) {
    if (!maContext) return LOGO_ERROR;
    if (!maContext->Video.Data.valid) {
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
                        isyslog("logo for %s %d:%d found in %s", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den, maContext->Config->logoDirectory);
                        logoStatus = true;
                        for (int plane = 1; plane < PLANES; plane++) {
                            if (Load(maContext->Config->logoDirectory, buf, plane) == 0) dsyslog("logo %s for plane %i found in %s", buf, plane, maContext->Config->logoDirectory);
                        }
                    }
                    else {
                        if (Load(maContext->Config->recDir,buf,0) == 0) {  // recording directory
                            isyslog("logo for %s %d:%d found in %s", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den, maContext->Config->recDir);
                            logoStatus = true;
                            for (int plane = 1; plane < PLANES; plane++) {
                                if (Load(maContext->Config->recDir, buf, plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, maContext->Config->recDir);
                            }
                        }
                        else {
                            if (maContext->Config->autoLogo > 0) {
                                isyslog("no logo for %s (%d:%d) found in logo cache or recording directory, extract logo from recording", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den);
                                cExtractLogo *ptr_cExtractLogo = new cExtractLogo(maContext, maContext->Video.Info.AspectRatio, recordingIndexMarkAdLogo);  // search logo from current frame
                                ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
                                if (ptr_cExtractLogo->SearchLogo(maContext, iFrameCurrent) > 0) dsyslog("cMarkAdLogo::Process(): no logo found in recording");
                                else dsyslog("cMarkAdLogo::Process(): new logo for %s found in recording",buf);
                                FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo"); // ptr_cExtraceLogo is valid because it it used above
                                delete ptr_cExtractLogo;
                                ptr_cExtractLogo = NULL;
                                if (Load(maContext->Config->recDir,buf,0) == 0) {  // try again recording directory
                                    isyslog("logo for %s %d:%d found in %s", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den, maContext->Config->recDir);
                                    logoStatus = true;
                                    for (int plane=1; plane < PLANES; plane++) {
                                        if (Load(maContext->Config->recDir,buf,plane) == 0) dsyslog("logo %s plane %i found in %s", buf, plane, maContext->Config->recDir);
                                    }
                                }
                                else dsyslog("cMarkAdLogo::Process(): no logo for %s %d:%d found in recording", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den);
                            }
                        }
                    }
                    if (!logoStatus) {
                        isyslog("no valid logo found for %s %d:%d, disable logo detection", maContext->Info.ChannelName, maContext->Video.Info.AspectRatio.num, maContext->Video.Info.AspectRatio.den);
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
        if ((logoWidth == 0) || (logoHeight == 0)) {
            sLogoSize DefaultLogoSize = GetDefaultLogoSize(maContext->Video.Info.width);
            logoHeight = DefaultLogoSize.height;
            logoWidth = DefaultLogoSize.width;
        }
        area.AspectRatio.num = maContext->Video.Info.AspectRatio.num;
        area.AspectRatio.den = maContext->Video.Info.AspectRatio.den;
        area.corner = maContext->Config->logoExtraction;
        sLogoSize MaxLogoSize = GetMaxLogoSize(maContext->Video.Info.width);
        if (maContext->Config->logoWidth != -1) {
            if (MaxLogoSize.width >= maContext->Config->logoWidth) logoWidth = maContext->Config->logoWidth;
            else {
                esyslog("configured logo width of %d exceeds max logo width %d", maContext->Config->logoWidth, MaxLogoSize.width);
                abortNow = true;
                return LOGO_ERROR;
            }
        }
        if (maContext->Config->logoHeight != -1) {
            if (MaxLogoSize.height >= maContext->Config->logoHeight) logoHeight = maContext->Config->logoHeight;
            else {
                esyslog("configured logo height of %d exceeds max logo height %d", maContext->Config->logoHeight, MaxLogoSize.height);
                abortNow = true;
                return LOGO_ERROR;
            }
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
#define BLACKNESS 20  // maximum brightness to detect a blackscreen, +1 to detect end of blackscreen
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
    // calulate limit with hysteresis
    int maxBrightness;
    if (blackScreenstatus == BLACKSCREEN_INVISIBLE) maxBrightness = BLACKNESS * end;
    else maxBrightness = (BLACKNESS + 1) * end;

#ifdef DEBUG_BLACKSCREEN
    int debugVal = 0;
    for (int x = 0; x < end; x++) {
        debugVal += maContext->Video.Data.Plane[0][x];
    }
    debugVal /= end;
    dsyslog("cMarkAdBlackScreen::Process(): frame (%d) blackness %d (expect <%d for start, >%d for end)", frameCurrent, debugVal, BLACKNESS, BLACKNESS);
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


void cMarkAdBlackBordersHoriz::Clear(const bool isRestart) {
    if (isRestart) borderstatus = HBORDER_RESTART;
    else           borderstatus = HBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cMarkAdBlackBordersHoriz::Process(const int FrameNumber, int *borderFrame) {
#define CHECKHEIGHT 5  // changed from 20 to 16 to 8 to 5
#define BRIGHTNESS_H_SURE  22  // changed from 20 to 22
#define BRIGHTNESS_H_MAYBE 81  // some channel have logo in border, so we will get a higher value, changed from 39 to 47 to 71 to 81
#define VOFFSET 5
    if (!maContext) return HBORDER_ERROR;
    if (!maContext->Video.Data.valid) return HBORDER_ERROR;
    if (maContext->Video.Info.framesPerSecond == 0) return HBORDER_ERROR;
    *borderFrame = -1;   // framenumber if we has a change, otherwise -1
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() video hight missing");
        return HBORDER_ERROR;
    }
    int height = maContext->Video.Info.height - VOFFSET;

    if (!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() Video.Data.PlaneLinesize[0] not initalized");
        return HBORDER_ERROR;
    }
    int start     = (height - CHECKHEIGHT) * maContext->Video.Data.PlaneLinesize[0];
    int end       = height * maContext->Video.Data.PlaneLinesize[0];
    int valTop    = 0;
    int valBottom = 0;
    int cnt       = 0;
    int xz        = 0;

    for (int x = start; x < end; x++) {
        if (xz < maContext->Video.Info.width) {
            valBottom += maContext->Video.Data.Plane[0][x];
            cnt++;
        }
        xz++;
        if (xz >= maContext->Video.Data.PlaneLinesize[0]) xz = 0;
    }
    valBottom /= cnt;

    if (valBottom <= BRIGHTNESS_H_MAYBE) { // we have a bottom border, test top border
        start = VOFFSET * maContext->Video.Data.PlaneLinesize[0];
        end = maContext->Video.Data.PlaneLinesize[0] * (CHECKHEIGHT+VOFFSET);
        cnt = 0;
        xz  = 0;
        for (int x = start; x < end; x++) {
            if (xz < maContext->Video.Info.width) {
                valTop += maContext->Video.Data.Plane[0][x];
                cnt++;
            }
            xz++;
            if (xz >= maContext->Video.Data.PlaneLinesize[0]) xz = 0;
        }
        valTop /= cnt;
    }
    else valTop = INT_MAX;

#ifdef DEBUG_HBORDER
    dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder brightness top %3d bottom %3d (expect one <=%d and one <= %d)", FrameNumber, valTop, valBottom, BRIGHTNESS_H_SURE, BRIGHTNESS_H_MAYBE);
#endif

    if ((valTop <= BRIGHTNESS_H_MAYBE) && (valBottom <= BRIGHTNESS_H_SURE) || (valTop <= BRIGHTNESS_H_SURE) && (valBottom <= BRIGHTNESS_H_MAYBE)) {
        // hborder detected
#ifdef DEBUG_HBORDER
        dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder ++++++: borderstatus %d, borderframenumber (%d)", FrameNumber, borderstatus, borderframenumber);
#endif
        if (borderframenumber == -1) {  // got first frame with hborder
            borderframenumber = FrameNumber;
        }
        if (borderstatus != HBORDER_VISIBLE) {
            if (FrameNumber > (borderframenumber + maContext->Video.Info.framesPerSecond * MIN_H_BORDER_SECS)) {
                switch (borderstatus) {
                    case HBORDER_UNINITIALIZED:
                        *borderFrame = 0;  // report back a border change after recording start
                        break;
                    case HBORDER_RESTART:
                        *borderFrame = -1;  // do not report back a border change after detection restart, only set internal state
                        break;
                    default:
                        *borderFrame = borderframenumber;
                }
                borderstatus = HBORDER_VISIBLE; // detected start of black border
            }
        }
    }
    else {
        // no hborder detected
#ifdef DEBUG_HBORDER
        dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder ------: borderstatus %d, borderframenumber (%d)", FrameNumber, borderstatus, borderframenumber);
#endif
        if (borderstatus != HBORDER_INVISIBLE) {
            if ((borderstatus == HBORDER_UNINITIALIZED) || (borderstatus == HBORDER_RESTART)) *borderFrame = -1;  // do not report back a border change after detection restart, only set internal state
            else *borderFrame = borderframenumber;
            borderstatus = HBORDER_INVISIBLE; // detected stop of black border
        }
        borderframenumber = -1; // restart from scratch
    }
#ifdef DEBUG_HBORDER
        dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder return: borderstatus %d, borderframenumber (%d), borderFrame (%d)", FrameNumber, borderstatus, borderframenumber, *borderFrame);
#endif
    return borderstatus;
}


cMarkAdBlackBordersVert::cMarkAdBlackBordersVert(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
    Clear();
}


void cMarkAdBlackBordersVert::Clear(const bool isRestart) {
    if (isRestart) borderstatus = VBORDER_RESTART;
    else           borderstatus = VBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cMarkAdBlackBordersVert::GetFirstBorderFrame() {
    if (borderstatus != VBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cMarkAdBlackBordersVert::Process(int frameNumber, int *borderFrame) {
#define CHECKWIDTH 20           // changed from 32 to 20
#define BRIGHTNESS_V_SURE   26  // changed from 24 to 26, some channels has flying pictures in border
#define BRIGHTNESS_V_MAYBE 100  // some channel have logo or infos in one border, so we must accept a higher value, changed from 68 to 100
    if (!maContext) {
        dsyslog("cMarkAdBlackBordersVert::Process(): maContext not valid");
        return VBORDER_ERROR;
    }
    if (!maContext->Video.Data.valid) return VBORDER_ERROR;  // no error, this is expected if bDecodeVideo is disabled
    if (maContext->Video.Info.framesPerSecond == 0) {
        dsyslog("cMarkAdBlackBordersVert::Process(): video frames per second  not valid");
        return VBORDER_ERROR;
    }
    *borderFrame = -1;

    int valLeft = 0;
    int valRight = 0;
    int cnt = 0;

    if(!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersVert::Process(): Video.Data.PlaneLinesize[0] missing");
        return VBORDER_ERROR;
    }


    // check left border
    for (int y = 0; y < maContext->Video.Info.height ; y++) {
        for (int x = 0; x <= CHECKWIDTH; x++) {
            valLeft += maContext->Video.Data.Plane[0][x + (y * maContext->Video.Data.PlaneLinesize[0])];
            cnt++;
        }
    }
    valLeft /= cnt;

    // check right border
    if (valLeft <= BRIGHTNESS_V_MAYBE) {
        cnt = 0;
        for (int y = 0; y < maContext->Video.Info.height ; y++) {
            for (int x = maContext->Video.Info.width - CHECKWIDTH; x < maContext->Video.Info.width; x++) {
                valRight += maContext->Video.Data.Plane[0][x + (y * maContext->Video.Data.PlaneLinesize[0])];
                cnt++;
            }
        }
        valRight /= cnt;
    }
    else valLeft = INT_MAX;

#ifdef DEBUG_VBORDER
    dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d) valLeft %d valRight %d", frameNumber, valLeft, valRight);
#endif
    if (((valLeft <= BRIGHTNESS_V_MAYBE) && (valRight <= BRIGHTNESS_V_SURE)) || ((valLeft <= BRIGHTNESS_V_SURE) && (valRight <= BRIGHTNESS_V_MAYBE))) {
        // vborder detected
        if (borderframenumber == -1) {   // first vborder detected
            borderframenumber = frameNumber;
        }
        if (borderstatus != VBORDER_VISIBLE) {
#ifdef DEBUG_VBORDER
            dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d) duration %ds", frameNumber, static_cast<int> ((frameNumber - borderframenumber) /  maContext->Video.Info.framesPerSecond));
#endif
            if (frameNumber > (borderframenumber + maContext->Video.Info.framesPerSecond * MIN_V_BORDER_SECS)) {
                switch (borderstatus) {
                    case VBORDER_UNINITIALIZED:
                        *borderFrame = 0;
                        break;
                    case VBORDER_RESTART:
                        *borderFrame = -1;  // do not report back a border change after detection restart, only set internal state
                        break;
                    default: *borderFrame = borderframenumber;
                }
                borderstatus = VBORDER_VISIBLE; // detected start of black border
            }
        }
    }
    else {
        // no vborder detected
        if (borderstatus != VBORDER_INVISIBLE) {
            if ((borderstatus == VBORDER_UNINITIALIZED) || (borderstatus == VBORDER_RESTART)) *borderFrame = -1;  // do not report back a border change, only set internal state
            else *borderFrame = frameNumber;
            borderstatus = VBORDER_INVISIBLE; // detected stop of black border
        }
        borderframenumber = -1; // restart from scratch
    }
    return borderstatus;
}


cMarkAdOverlap::cMarkAdOverlap(sMarkAdContext *maContextParam) {
    maContext          = maContextParam;
    histbuf[OV_BEFORE] = NULL;
    histbuf[OV_AFTER]  = NULL;
    Clear();
}


cMarkAdOverlap::~cMarkAdOverlap() {
    Clear();
}


void cMarkAdOverlap::Clear() {
#ifdef DEBUG_OVERLAP
    dsyslog("cMarkAdOverlap::Clear(): clear histogram buffers");
#endif
    histcnt[OV_BEFORE]    = 0;
    histcnt[OV_AFTER]     = 0;
    histframes[OV_BEFORE] = 0;
    histframes[OV_AFTER]  = 0;

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

    similarCutOff    =  0;
    similarMinLength =  0;
    lastFrameNumber  = -1;
}


void cMarkAdOverlap::GetHistogram(simpleHistogram &dest) {
    memset(dest, 0, sizeof(simpleHistogram));
    int startY =  maContext->Video.Info.height * 0.22;  // ignore top part because there can be info border at start after the advertising, changed from 0.16 to 0.2 to 0.22
    int endY   =  maContext->Video.Info.height * 0.82; // ignore bottom part because there can info border text at start after the advertising, changed from 0.87 to 0.82
    for (int Y = startY; Y < endY; Y++) {
        for (int X = 0; X < maContext->Video.Info.width;X++) {
            uchar val = maContext->Video.Data.Plane[0][X + (Y * maContext->Video.Data.PlaneLinesize[0])];
            dest[val]++;
        }
    }
}


int cMarkAdOverlap::AreSimilar(const simpleHistogram &hist1, const simpleHistogram &hist2) { // return > 0 if similar, else <= 0
    long int similar = 0;  // prevent integer overflow
    for (int i = 0; i < 256; i++) {
        similar += abs(hist1[i] - hist2[i]);  // calculte difference, smaller is more similar
    }
    if (similar > INT_MAX) similar = INT_MAX;  // we do need more
    if (similar < similarCutOff) {
       return similar;
    }
    return -similar;
}


void cMarkAdOverlap::Detect(sOverlapPos *ptr_OverlapPos) {
    if (!ptr_OverlapPos) return;

    int startAfterMark             =  0;
    int simLength                  =  0;
    int simMax                     =  0;
    int tmpindexAfterStartMark     =  0;
    int tmpindexBeforeStopMark     =  0;
    int firstSimilarBeforeStopMark = -1;
    int firstSimilarAfterStartMark = -1;
    int range                      =  1;  // on a scene change we can miss the same picture

    if (maContext->Config->fullDecode) range = 10;  // we need more range with full decoding

    for (int indexBeforeStopMark = 0; indexBeforeStopMark < histcnt[OV_BEFORE]; indexBeforeStopMark++) {
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Detect(): -------------------------------------------------------------------------------------------------------------");
        dsyslog("cMarkAdOverlap::Detect(): testing frame (%5d) before stop mark, indexBeforeStopMark %d, against all frames after start mark", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark);
#endif

        if (startAfterMark == histcnt[OV_AFTER]) {  // we reached end of range after start mark, reset state and contine with next frame before stop mark
            startAfterMark = 0;
            simLength      = 0;
            simMax         = 0;
            continue;
        }

        // check if histogram buffer before stop mark is valid
        if (!histbuf[OV_BEFORE][indexBeforeStopMark].valid) {
            dsyslog("cMarkAdOverlap::Detect(): histogram of frame (%d) before stop mark not valid, continue with next frame", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber);
        }

        for (int indexAfterStartMark = startAfterMark; indexAfterStartMark < histcnt[OV_AFTER]; indexAfterStartMark++) {
            // check if histogram buffer after start mark is valid
            if (!histbuf[OV_AFTER][indexAfterStartMark].valid) {  // not valid, continue with next pair
                indexBeforeStopMark++;
                if (indexBeforeStopMark >= histcnt[OV_BEFORE]) break;
                continue;
            }

            // check if pair is similar
            int simil = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark].histogram);
            if ((simLength >= 1800) && (simil < 0)) {  // not similar, but if we had found at least a short similar part, check neighbour frames
                int similBefore = -1;
                int similAfter  = -1;
                for (int i = 1 ; i <= range; i++) {
                    if ((indexAfterStartMark - i) > 0) similBefore = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark - i].histogram);
                    if ((indexAfterStartMark + i) <  histcnt[OV_AFTER]) similAfter = AreSimilar(histbuf[OV_BEFORE][indexBeforeStopMark].histogram, histbuf[OV_AFTER][indexAfterStartMark + i].histogram);
                    if ((similBefore >= 0) || (similAfter >= 0)) break;
                }
                if ((similBefore < 0) && (similAfter < 0)) {  // we have reached end of a similar part
                    tsyslog("cMarkAdOverlap::Detect(): end of similar from (%5d) to (%5d) and (%5d) to (%5d) length %5dms",  histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber, histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber, histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber, histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber, simLength);
                    tsyslog("cMarkAdOverlap::Detect():                with similBefore %5d, simil %5d, similAfter %5d", similBefore, simil, similAfter);
                }
                if (similBefore > 0) simil = similBefore;
                if (similAfter  > 0) simil = similAfter;
            }

#ifdef DEBUG_OVERLAP
            if (simil >= 0) dsyslog("cMarkAdOverlap::Detect(): +++++     similar frame (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) length %2dms similarMaxCnt %2d)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simLength, similarMinLength);
#endif
            // found long enought overlap, store position

            if ((simLength >= similarMinLength) &&
               ((histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber - histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber) >= (ptr_OverlapPos->similarBeforeEnd - ptr_OverlapPos->similarBeforeStart))) { // new overlap is longer than current overlap
                ptr_OverlapPos->similarBeforeStart = histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber;
                ptr_OverlapPos->similarBeforeEnd   = histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber;
                ptr_OverlapPos->similarAfterStart  = histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber;
                ptr_OverlapPos->similarAfterEnd    = histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber;
                ptr_OverlapPos->similarMax         = simMax;
                if (simil < 0) ptr_OverlapPos->similarEnd = -simil;
            }

            if (simil >= 0) {
                if (simLength == 0) {  // this is the first similar frame pair, store position
                    firstSimilarBeforeStopMark = indexBeforeStopMark;
                    firstSimilarAfterStartMark = indexAfterStartMark;
                }
                tmpindexAfterStartMark = indexAfterStartMark;
                tmpindexBeforeStopMark = indexBeforeStopMark;
                startAfterMark = indexAfterStartMark + 1;
                simLength = 1000 * (histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber - histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber + 1) / maContext->Video.Info.framesPerSecond;
                if (simil > simMax) simMax = simil;

#ifdef DEBUG_OVERLAP
                dsyslog("cMarkAdOverlap::Detect(): similar picture index  from %d to %d and %d to %d", firstSimilarBeforeStopMark, indexBeforeStopMark, firstSimilarAfterStartMark, indexAfterStartMark);
                dsyslog("cMarkAdOverlap::Detect(): similar picture frames from (%d) to (%d) and (%d) to (%d), length %dms", histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber, histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, simLength);
#endif

                break;
            }
            else {
                // reset to first similar frame
                if (simLength > 0) {
#ifdef DEBUG_OVERLAP
                    dsyslog("cMarkAdOverlap::Detect(): ---- not similar frame (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) length %2dms similarMaxCnt %2d)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simLength, similarMinLength);
                    dsyslog("cMarkAdOverlap::Detect(): ===================================================================================================================");
#endif
                    indexBeforeStopMark = firstSimilarBeforeStopMark;  // reset to first similar frame
                }

                if (simLength < similarMinLength) startAfterMark = 0;
                simLength = 0;
                simMax    = 0;
            }
        }
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Detect(): current overlap from (%d) to (%d) and (%d) to (%d)", ptr_OverlapPos->similarBeforeStart, ptr_OverlapPos->similarBeforeEnd, ptr_OverlapPos->similarAfterStart, ptr_OverlapPos->similarAfterEnd);
#endif
    }
    return;
}


void cMarkAdOverlap::Process(sOverlapPos *ptr_OverlapPos, const int frameNumber, const int frameCount, const bool beforeAd, const bool h264) {
    if (!ptr_OverlapPos) return;
#ifdef DEBUG_OVERLAP
    dsyslog("cMarkAdOverlap::Process(): frameNumber %d, frameCount %d, beforeAd %d, isH264 %d", frameNumber, frameCount, beforeAd, h264);
#endif

    if ((lastFrameNumber > 0) && (similarMinLength == 0)) {
        similarCutOff = 49000;            // lower is harder
                                          // do not increase, we will get false positive
        if (h264) similarCutOff = 196000; // reduce false similar detection in H.264 streams
        similarMinLength = 4040;          // shortest valid length of an overlap with 4040ms found
    }

    if (beforeAd) {
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Process(): preload histogram with frames before stop mark, frame index %d of %d", histcnt[OV_BEFORE], frameCount - 1);
#endif
        // alloc memory for frames before stop mark
        if (!histbuf[OV_BEFORE]) {
            histframes[OV_BEFORE] = frameCount;
            histbuf[OV_BEFORE] = new sHistBuffer[frameCount + 1];
            ALLOC(sizeof(*histbuf[OV_BEFORE]), "histbuf");
        }
        // fill histogram for frames before stop mark
        if (histcnt[OV_BEFORE] >= frameCount) {
            dsyslog("cMarkAdOverlap::Process(): got more frames before stop mark than expected");
            return;
        }
        if (maContext->Video.Data.valid) {
            GetHistogram(histbuf[OV_BEFORE][histcnt[OV_BEFORE]].histogram);
            histbuf[OV_BEFORE][histcnt[OV_BEFORE]].valid = true;
        }
        else {
            dsyslog("cMarkAdOverlap::Process(): data before stop mark of frame (%d) not valid", frameNumber);
            histbuf[OV_BEFORE][histcnt[OV_BEFORE]].valid = true;
        }
        histbuf[OV_BEFORE][histcnt[OV_BEFORE]].frameNumber = frameNumber;
        histcnt[OV_BEFORE]++;
    }
    else {
#ifdef DEBUG_OVERLAP
        dsyslog("cMarkAdOverlap::Process(): preload histogram with frames after start mark, frame index %d of %d", histcnt[OV_AFTER], frameCount - 1);
#endif
        // alloc memory for frames after start mark
        if (!histbuf[OV_AFTER]) {
            histframes[OV_AFTER] = frameCount;
            histbuf[OV_AFTER] = new sHistBuffer[frameCount + 1];
            ALLOC(sizeof(*histbuf[OV_AFTER]), "histbuf");
        }

        if (histcnt[OV_AFTER] >= histframes[OV_AFTER] - 1) {
            dsyslog("cMarkAdOverlap::Process(): start compare frames");
            Detect(ptr_OverlapPos);
#ifdef DEBUG_OVERLAP
            dsyslog("cMarkAdOverlap::Process(): overlap from (%d) before stop mark and (%d) after start mark", ptr_OverlapPos->similarBeforeEnd, ptr_OverlapPos->similarAfterEnd);
#endif
            return;
        }
        // fill histogram for frames before after start mark
        if (histcnt[OV_AFTER] >= frameCount) {
            dsyslog("cMarkAdOverlap::Process(): got more frames after start mark than expected");
            return;
        }
        if (maContext->Video.Data.valid) {
            GetHistogram(histbuf[OV_AFTER][histcnt[OV_AFTER]].histogram);
            histbuf[OV_AFTER][histcnt[OV_AFTER]].valid = true;
        }
        else {
            dsyslog("cMarkAdOverlap::Process(): data after start mark of frame (%d) not valid", frameNumber);
            histbuf[OV_AFTER][histcnt[OV_AFTER]].valid = false;
        }
        histbuf[OV_AFTER][histcnt[OV_AFTER]].frameNumber = frameNumber;
        histcnt[OV_AFTER]++;
    }
    lastFrameNumber = frameNumber;
    return;
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
}


void cMarkAdVideo::Clear(const bool isRestart) {
    dsyslog("cMarkAdVideo::Clear(): reset detection status, isRestart = %d", isRestart);
    if (!isRestart) {
        aspectRatio.num=0;
        aspectRatio.den=0;
    }
    if (isRestart) {  // only clear if detection is disabled
        if (blackScreen && maContext->Video.Options.ignoreBlackScreenDetection) blackScreen->Clear();
        if (logo &&        maContext->Video.Options.ignoreLogoDetection)        logo->Clear(true);
        if (vborder &&     maContext->Video.Options.ignoreVborder)              vborder->Clear(true);
        if (hborder &&     maContext->Video.Options.ignoreHborder)              hborder->Clear(true);
    }
    else {
        if (blackScreen) blackScreen->Clear();
        if (logo)    logo->Clear(false);
        if (vborder) vborder->Clear(false);
        if (hborder) hborder->Clear(false);
    }
}


void cMarkAdVideo::ClearBorder() {
    dsyslog("cMarkAdVideo::ClearBorder(): reset border detection status");
    if (vborder) vborder->Clear();
    if (hborder) hborder->Clear();
}


void cMarkAdVideo::ResetMarks() {
    marks={};
}


bool cMarkAdVideo::AddMark(int type, int position, const sAspectRatio *before, const sAspectRatio *after) {
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
            if (maContext->Config->fullDecode) AddMark(MT_NOBLACKSTART, useFrame);  // first frame without blackscreen is start mark position
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
            if (hret == HBORDER_VISIBLE) {
                maContext->Video.Info.frameDarkOpeningCredits = vborderframenumber;
                dsyslog("cMarkAdVideo::Process(): hborder and vborder detected at frame (%d), this is a very long black screen, maybe opening credits", vborderframenumber);
            }
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
