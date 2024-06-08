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


cMarkAdLogo::cMarkAdLogo(sMarkAdContext *maContextParam, cCriteria *criteriaParam, cIndex *recordingIndex) {
    maContext                = maContextParam;
    criteria                 = criteriaParam;
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
        area.sobel = nullptr;
    }
    // free memory for sobel masks
    if (area.mask) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.mask");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.mask[plane];
        }
        delete[] area.mask;
        area.mask = nullptr;
    }
    // free memory for sobel result
    if (area.result) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.result");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.result[plane];
        }
        delete[] area.result;
        area.result = nullptr;
    }
    // free memory for sobel inverse result
    if (area.inverse) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.inverse");
        for (int plane = 0; plane < PLANES; plane++) {
            delete[] area.inverse[plane];
        }
        delete[] area.inverse;
        area.inverse = nullptr;
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
            area.mask = nullptr;
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

    // calculate pixel ratio for special logo detection
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

    char *buf = nullptr;
    if (debug) {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i_debug_%s.pgm", "/tmp/", frameNumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane, debug)==-1) return false;
    }
    else {
        if (asprintf(&buf,"%s/%07d-%s-A%i_%i-P%i.pgm", "/tmp/", frameNumber, maContext->Info.ChannelName, area.AspectRatio.num, area.AspectRatio.den, plane)==-1) return false;
    }
    ALLOC(strlen(buf)+1, "buf");

    // Open file
    FILE *pFile = fopen(buf, "wb");
    if (pFile == nullptr) {
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


bool cMarkAdLogo::SetCoordinates(int *xstart, int *xend, int *ystart, int *yend, const int plane) const {
    switch (area.corner) {
    case TOP_LEFT:
        *xstart = 0;
        *xend   = logoWidth  - 1;
        *ystart = 0;
        *yend   = logoHeight - 1;
        break;
    case TOP_RIGHT:
        *xstart = maContext->Video.Info.width - logoWidth;
        *xend   = maContext->Video.Info.width - 1;
        *ystart = 0;
        *yend   = logoHeight - 1;
        break;
    case BOTTOM_LEFT:
        *xstart = 0;
        *xend   = logoWidth - 1;
        *ystart = maContext->Video.Info.height - logoHeight;
        *yend   = maContext->Video.Info.height - 1;
        break;
    case BOTTOM_RIGHT:
        *xstart = maContext->Video.Info.width  - logoWidth;
        *xend   = maContext->Video.Info.width  - 1;
        *ystart = maContext->Video.Info.height - logoHeight;
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
// return true if we now have a valid detection result
//
bool cMarkAdLogo::ReduceBrightness(__attribute__((unused)) const int frameNumber, const int logo_vmark, const int logo_imark) {  // frameNumber used only for debugging
    int xstart, xend, ystart, yend;
    if (!SetCoordinates(&xstart, &xend, &ystart, &yend, 0)) return false;

// calculate coorginates for logo black pixel area in logo corner
    if ((logo_xstart == -1) && (logo_xend == -1) && (logo_ystart == -1) && (logo_yend == -1)) {  // have to init
        switch (area.corner) {  // logo is usually in the inner part of the logo corner
#define LOGO_MIN_PIXEL 30  // big enough to get in the main part of the logo
        case TOP_LEFT: {
            // xend and yend from logo coordinates
            logo_xend = xend;
            logo_yend = yend;

            // xstart is first column with pixel in logo area
            int pixelCount = 0;
            int column;
            int line;
            for (column = 0; column < logoWidth; column++) {
                for (line = 0; line < logoHeight; line++) {
                    if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                if (pixelCount > LOGO_MIN_PIXEL) break;
            }
            logo_xstart = column;

            // ystart is first line with pixel in logo area
            pixelCount = 0;
            for (line = 0; line < logoHeight; line++) {
                for (column = 0; column < logoWidth; column++) {
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
                for (line = 0; line < logoHeight; line++) {
                    if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                if (pixelCount > LOGO_MIN_PIXEL) break;
            }
            logo_xend = xend - (logoWidth - column);

            // ystart is first line with pixel in logo area
            pixelCount = 0;
            for (line = 0; line < logoHeight; line++) {
                for (column = 0; column < logoWidth; column++) {
                    if (area.mask[0][line * logoWidth + column] == 0) pixelCount++;
                    if (pixelCount >= LOGO_MIN_PIXEL) break;
                }
                if (pixelCount >= LOGO_MIN_PIXEL) break;
            }
            logo_ystart = line;
            break;
        }
        // TODO: calculate exact coordinates
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
            return false;
            break;
        }
//        dsyslog("cMarkAdLogo::ReduceBrightness(): logo area: xstart %d xend %d, ystart %d yend %d", logo_xstart, logo_xend, logo_ystart, logo_yend);
    }

// detect contrast and brightness of logo part
    int minPixel = INT_MAX;
    int maxPixel = 0;
    int sumPixel = 0;
    for (int line = logo_ystart; line <= logo_yend; line++) {
        for (int column = logo_xstart; column <= logo_xend; column++) {
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column];
            if (pixel > maxPixel) maxPixel = pixel;
            if (pixel < minPixel) minPixel = pixel;
            sumPixel += pixel;
        }
    }
    int brightnessLogo = sumPixel / ((logo_yend - logo_ystart + 1) * (logo_xend - logo_xstart + 1));
    int contrastLogo = maxPixel - minPixel;
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): logo area before reduction: contrast %3d, brightness %3d", frameNumber, contrastLogo, brightnessLogo);
#endif

#define LOW_PIXEL_LOGO 70  // changed from 65 to 70, most of HD recording logo

// check if contrast and brightness is valid
// build a curve from examples

    // very high contrast with not very high brightness in logo area, trust detection
    //
    // high pixel logo
    // false negativ, logo is visible but not detected
    // tbd
    //
    if ((contrastLogo > 162) && (brightnessLogo < 96) && (maContext->Video.Logo.pixelRatio > LOW_PIXEL_LOGO)) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): very high contrast with not very high brightness in logo area, trust detection (high pixel logo)", frameNumber);
#endif
        return true; // if there is a logo we should had detected it
    }
    // low pixel logo
    // false negativ, logo is visible but not detected
    // contrast 202, brightness  85
    // contrast 200, brightness  85
    if ((contrastLogo > 202) && (brightnessLogo <  85) && (maContext->Video.Logo.pixelRatio <= LOW_PIXEL_LOGO)) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): very high contrast with not very high brightness in logo area, trust detection (low pixel logo)", frameNumber);
#endif
        return true; // if the is a logo should had detected it
    }

// -----------------------------------------------------------------
// not detected logo in bright area, also not detected with bridgtness reduction, take it as invalid
// contrast  28, brightness 205  -> bright background with logo
// contrast  25, brightness 216  -> bright background with logo
// contrast  25, brightness 195  -> bright blue sky with logo
// contrast  21, brightness 215  -> bright background with logo
// contrast  21, brightness 218  -> bright background with logo

// contrast  20, brightness 214  -> bright background with logo
// contrast  17, brightness 220  -> bright background with logo
// contrast  16, brightness 218  -> bright background with logo
// contrast  15, brightness 218  -> bright background with logo
// contrast  14, brightness 218  -> bright background with logo
// contrast  13, brightness 216  -> bright background with logo
//
// contrast  10, brightness 216  -> bright background with logo
// contrast   9, brightness 229  -> bright background with logo
// contrast   9, brightness 218  -> bright background with logo
// contrast   9, brightness 217  -> bright background with logo
// contrast   8, brightness 221  -> bright background with logo
// contrast   8, brightness 228  -> bright background with logo
// contrast   8, brightness 218  -> bright background with logo
//
// -----------------------------------------------------------------
// logo or no logo in bright area, not detected without brightness reduction, detected with brightness reduction, take it as valid
// contrast 170, brightness 141  -> bright background without logo
//
// contrast  54, brightness 181  -> no logo in frame                        NEW
// contrast  52, brightness 189  -> bright background without logo
//
// contrast  49, brightness 175  -> red separator picture without logo
// contrast  32, brightness 192  -> logo in bright background
//
// contrast  20, brightness 197  -> bright ad in frame without logo
// contrast  19, brightness 197  -> bright separator without logo
// contrast  14, brightness 195  -> bright scene without logo
//
// contrast   8, brightness 207  -> no logo on bright background
//
// contrast   3, brightness 218  -> bright separator without logo
// contrast   2, brightness 213  -> bright separator without logo
//
// contrast   0, brightness 111  -> red sepator picture without logo
// contrast   0, brightness 235  -> white separator without logo

    // build the curve for invalid contrast/brightness
    if ((    (contrastLogo  ==  0) &&                          (brightnessLogo > 235)) ||
            ((contrastLogo  >   0) && (contrastLogo <=   5) && (brightnessLogo > 218)) ||
            ((contrastLogo  >   5) && (contrastLogo <=  10) && (brightnessLogo > 207)) ||
            ((contrastLogo  >  10) && (contrastLogo <=  20) && (brightnessLogo > 197)) ||
            ((contrastLogo  >  20) && (contrastLogo <=  50) && (brightnessLogo > 192)) ||
            ((contrastLogo  >  50) && (contrastLogo <=  60) && (brightnessLogo > 189)) ||
            ((contrastLogo  >  60) && (contrastLogo <= 125) && (brightnessLogo > 171)) ||
            ((contrastLogo  > 125) && (contrastLogo <= 131) && (brightnessLogo > 153)) ||
            ((contrastLogo  > 131) && (contrastLogo <= 180) && (brightnessLogo > 141)) ||
            ((contrastLogo  > 180) && (contrastLogo <= 194) && (brightnessLogo > 120)) ||
            ((contrastLogo  > 194) &&                          (brightnessLogo > 110))) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): contrast/brightness in logo area is invalid for brightness reduction", frameNumber);
#endif
        return false; //  nothing we can work with
    }

// correct brightness and increase ontrast of plane 0
    minPixel = INT_MAX;
    maxPixel = 0;
    sumPixel = 0;

#define REDUCE_BRIGHTNESS 30
#define INCREASE_CONTRAST 2
    // reduce brightness and increase contrast, transform 1 pixel more than logo size to prevent to detect an edge with sobel transformation
    for (int line = ystart - 1; line <= yend + 1; line++) {
        if (line < 0) continue;
        if (line > (maContext->Video.Info.height - 1)) continue;
        for (int column = xstart - 1; column <= xend + 1; column++) {
            if (column < 0) continue;
            if (column > (maContext->Video.Info.width - 1)) continue;
            int pixel = maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column] - REDUCE_BRIGHTNESS;
            if (pixel < 0) pixel = 0;
            pixel = INCREASE_CONTRAST * (pixel - 128) + 128;
            if (pixel < 0) pixel = 0;
            if (pixel > 255) pixel = 255;
            maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0] + column] = pixel;
            if ((line >= logo_ystart) && (line <= logo_yend) && (column >= logo_xstart) && (column <= logo_xend)) {
                if (pixel > maxPixel) maxPixel = pixel;
                if (pixel < minPixel) minPixel = pixel;
                sumPixel += pixel;
            }
        }
    }
    int contrastReduced   = maxPixel - minPixel;
    int brightnessReduced = sumPixel / ((logo_yend - logo_ystart + 1) * (logo_xend - logo_xstart + 1));

#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): logo area after  reduction: contrast %3d, brightness %3d", frameNumber, contrastReduced, brightnessReduced);
#endif

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameNumber > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameNumber < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d_corrected.pgm", maContext->Config->recDir, frameNumber) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveFrameBuffer(maContext, fileName);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
    }
#endif

// if we have a comple white picture after brightness reduction, we can not decide if there is a logo or not
    if ((contrastReduced == 0) && (brightnessReduced == 255)) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): detection impossible on white picture", frameNumber);
#endif
        return false;
    }

// redo sobel transformation with reduced brightness and verfy result picture
    // redo sobel transformation
    int rPixelBefore = area.rPixel[0];
    area.rPixel[0] = 0;
    SobelPlane(0, 0);       // only plane 0
    int rPixel = area.rPixel[0];
    int mPixel = area.mPixel[0];
    int iPixel = area.iPixel[0];

#ifdef DEBUG_LOGO_DETECTION
    char detectStatus[] = "o";
    if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
    if (rPixel <= logo_imark) strcpy(detectStatus, "-");
    dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameNumber, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, 1, detectStatus);
#endif

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameNumber > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameNumber < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_3sobelCorrected.pgm", maContext->Config->recDir, frameNumber, area.corner) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveSobel(fileName, area.sobel[0], maContext->Video.Logo.width, maContext->Video.Logo.height);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_4resultCorrected.pgm", maContext->Config->recDir, frameNumber, area.corner) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveSobel(fileName, area.result[0], maContext->Video.Logo.width, maContext->Video.Logo.height);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_3inverseCorrected.pgm", maContext->Config->recDir, frameNumber, area.corner) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveSobel(fileName, area.inverse[0], maContext->Video.Logo.width, maContext->Video.Logo.height);
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
    int quoteBlack   = 100 * black  /  (logoHeight * logoWidth);
    int quoteInverse = 100 * iPixel / ((logoHeight * logoWidth) - mPixel);
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo:ReduceBrightness(): frame (%6d): area intensity %d, black quote: %d%%, inverse quote %d%%", frameNumber, area.intensity, quoteBlack, quoteInverse);
#endif
    // check if we have matches only from backbround patten
    // no logo before brightness reduction, unclear result after, hight backgrund patten quote
    //
    // exmample no logo visible
    // area intensity 80, black quote: 32%, inverse quote 32%
    // area intensity 41, black quote: 27%, inverse quote 24%
    // area intensity 37, black quote: 25%, inverse quote 23%
    // area intensity 32, black quote: 25%, inverse quote 22%
    if ((area.status == LOGO_VISIBLE) && (area.intensity <= 86) && (rPixelBefore <= logo_imark) && (rPixel > logo_imark) && (rPixel < logo_vmark) &&
            (quoteBlack >= 25) && (quoteInverse >= 22)) {
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): inverse quote above limit, assume matches only from patten and logo is invisible", frameNumber);
        area.rPixel[0] = logo_imark;  // set logo machtes to invisible
        return true;
    }

    if (quoteBlack > 22) { // changed from 27 to 25
        if ((quoteBlack >= 60) && (area.status == LOGO_INVISIBLE)) {  // prevent false logo start detection from patten background
            area.counter--;
            if (area.counter < 0) area.counter = 0;
        }
        return false; // there is a pattern on the backbround, no logo detection possible
    }

    // now we trust logo visible
    if (rPixel >= logo_vmark) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): valid logo visible after brightness reducation", frameNumber);
#endif
        return true;  // we have a clear result
    }

    // ignore matches on still bright picture
    if ((area.intensity > 160) && (rPixel >= logo_imark / 5)) { // still too bright, trust only very low matches
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): logo area still too bright", frameNumber);
#endif
        return false;
    }

    // now we trust logo invisible
    if (rPixel <= logo_imark) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d): valid logo invisible after brightness reducation", frameNumber);
#endif
        return true;  // we have a clear result
    }

    // still no clear result
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cMarkAdLogo::ReduceBrightness(): frame (%6d) no valid result after brightness reducation", frameNumber);
#endif
    return false;
}


bool cMarkAdLogo::SobelPlane(const int plane, int boundary) {
    if ((plane < 0) || (plane >= PLANES)) return false;
    if (!maContext->Video.Data.PlaneLinesize[plane]) return false;

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
    // alloc memory for mask invers result
    if (!area.inverse) {
        area.inverse = new uchar*[PLANES];
        for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
            area.inverse[planeTMP] = new uchar[maxLogoPixel];
        }
        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "area.inverse");
    }

    if ((logoWidth == 0) || (logoHeight == 0)) {
        sLogoSize DefaultLogoSize = GetDefaultLogoSize(maContext->Video.Info.width);
        logoHeight = DefaultLogoSize.height;
        logoWidth  = DefaultLogoSize.width;
    }
    if ((maContext->Video.Info.pixFmt != 0) && (maContext->Video.Info.pixFmt != 12)) {
        if (!pixfmt_info) {
            esyslog("unknown pixel format %d, please report!", maContext->Video.Info.pixFmt);
            pixfmt_info = true;
        }
        return false;
    }

    // get logo coordinates
    int xStart = 0;
    int xEnd   = 0;
    int yStart = 0;
    int yEnd   = 0;
    if (!SetCoordinates(&xStart, &xEnd, &yStart, &yEnd, plane)) return false;

    int cutval     = 127;
    int planeWidth = logoWidth;
    int planeVideoWidth  = maContext->Video.Info.width;
    int planeVideoHeight = maContext->Video.Info.height;
    if (plane > 0) {
        boundary         /= 2;
        cutval           /= 2;
        planeWidth       /= 2;
        planeVideoWidth  /= 2;
        planeVideoHeight /= 2;
    }
    int SUM;
    int sumX;
    int sumY;
    area.rPixel[plane] = 0;
    area.iPixel[plane] = 0;
    if (plane == 0) area.intensity = 0;

#ifdef DEBUG_SOBEL
    dsyslog("cMarkAdLogo::SobelPlane(): plane %d: xStart %d, xEend %d, yStart %d, yEnd %d", plane, xStart, xEnd, yStart, yEnd);
#endif

    for (int Y = yStart; Y <= yEnd; Y++) {
        for (int X = xStart; X <= xEnd; X++) {
            if (plane == 0) {
                area.intensity += maContext->Video.Data.Plane[plane][X + (Y * maContext->Video.Data.PlaneLinesize[plane])];
            }
            sumX = 0;
            sumY = 0;

            // image boundaries from coordinates
            if ((X < (xStart + boundary))     || (X <= 0) ||
                    (X >= (xEnd - boundary))  || (X >= (planeVideoWidth - 2)) ||
                    (Y < (yStart + boundary)) || (Y <= 0) ||
                    (Y > (yEnd - boundary))   || (Y >= (planeVideoHeight - 2))) SUM = 0;
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

            area.sobel[plane][(X - xStart) + (Y - yStart) * planeWidth] = val;

            // only store results in logo coordinates range
            if (area.valid[plane]) {  // if we are called by logo search, we have no valid area.mask
                // area result
                area.result[plane][(X - xStart) + (Y - yStart) * planeWidth] = (area.mask[plane][(X - xStart) + (Y - yStart) * planeWidth] + val) & 255;
                if (area.result[plane][(X - xStart) + (Y - yStart) * planeWidth] == 0) area.rPixel[plane]++;
                // area inverted
                area.inverse[plane][(X - xStart) + (Y - yStart) * planeWidth] = ((255 - area.mask[plane][(X - xStart) + (Y - yStart) * planeWidth]) + val) & 255;
                if (area.inverse[plane][(X - xStart) + (Y - yStart) * planeWidth] == 0) area.iPixel[plane]++;
            }
        }
    }
    if (plane == 0) area.intensity /= (logoHeight * planeWidth);
    return true;
}


// copy all black pixels from logo pane 0 into plan 1 and plane 2
// we need this for channels with usually grey logos, but at start and end they can be red (DMAX)
//
void cMarkAdLogo::LogoGreyToColour() {
    for (int line = 0; line < logoHeight; line++) {
        for (int column = 0; column < logoWidth; column++) {
            if (area.mask[0][line * logoWidth + column] == 0 ) {
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
    int rPixel = 0;
    int mPixel = 0;
    int iPixel = 0;
    int processed = 0;
    *logoFrameNumber = -1;
    if (area.corner == -1) return LOGO_NOCHANGE;

    // apply sobel transformation to all planes
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
                    char *fileName = nullptr;
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
                    if (asprintf(&fileName,"%s/F__%07d-P%d-C%1d_3inverse.pgm", maContext->Config->recDir, frameCurrent, plane, area.corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        SaveSobel(fileName, area.inverse[plane], width, height);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                }
#endif
            }
        }
        if (extract) {
            if (!Save(frameCurrent, area.sobel, plane, nullptr)) dsyslog("cMarkAdLogo::Detect(): save logo from frame (%d) failed", frameCurrent);
        }
        else {
//            tsyslog("plane %i area.rPixel[plane] %i area.mPixel[plane] %i", plane, area.rPixel[plane], area.mPixel[plane]);
            rPixel += area.rPixel[plane];
            mPixel += area.mPixel[plane];
            iPixel += area.iPixel[plane];
        }
    }
    if (extract || onlyFillArea) return LOGO_NOCHANGE;
    if (processed == 0) return LOGO_ERROR;  // we have no plane processed

    // set logo visable and invisable limits
    int logo_vmark = LOGO_VMARK * mPixel;
    int logo_imark = LOGO_IMARK * mPixel;
    if (criteria->LogoRotating(maContext->Info.ChannelName)) {  // reduce if we have a rotating logo (e.g. SAT_1), changed from 0.9 to 0.8
        logo_vmark *= 0.8;
        logo_imark *= 0.8;
    }
    if (criteria->LogoTransparent(maContext->Info.ChannelName)) { // reduce if we have a transparent logo (e.g. SRF_zwei_HD)
        logo_vmark *= 0.9;
        logo_imark *= 0.9;
    }

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d.pgm", maContext->Config->recDir, frameCurrent) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            SaveFrameBuffer(maContext, fileName);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
        }
    }
#endif

    bool logoStatus     = false;

    // in dark scene we can use stronger detection
    // don't miss logo invisable for:
    // - down shiftet logo in add (Pro7_MAXX), will only work on dark background
    // - part of logo in black screen as stop mark instead of no logo (Comedy_Central)
#define AREA_INTENSITY_TRUST    54 // we trust detection, use higher invisable value
#define QUOTE_TRUST              2 // uplift factor for logo invisable threshold
    if (area.intensity <= AREA_INTENSITY_TRUST) logo_imark *= QUOTE_TRUST;

#ifdef DEBUG_LOGO_DETECTION
    char detectStatus[] = "o";
    if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
    if (rPixel <= logo_imark) strcpy(detectStatus, "-");
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("cMarkAdLogo::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
#endif

    // we have only 1 plane (no coloured logo)
    // if we only have one plane we are "vulnerable"
    // to very bright pictures, so ignore them...
    if (processed == 1) {
        // special cases where detection is not possible:
        // prevent to detect logo start on very bright background, this is not possible
        if ((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark) && area.intensity >= 218) {  // possible state change from invisible to visible
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) too bright %d for logo start", frameCurrent, area.intensity);
#endif
            return LOGO_NOCHANGE;
        }

        // transparent logo decetion on bright backbround is imposible, changed from 189 to 173
        if (criteria->LogoTransparent(maContext->Info.ChannelName) && (area.intensity >= 161)) {  // changed from 173 to 161
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) with transparent logo too bright %d for detection", frameCurrent, area.intensity);
#endif
            return LOGO_NOCHANGE;
        }

        // background pattern can mess up soble transformation result
        // prevent to detect background pattern as new logo start
        // prvent to miss logo stop from background pattern
        if (((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark)) || // logo state was invisible, new logo state visible
                // logo state was visible, new state unclear result
                // ignore very bright pictures, we can have low logo result even on pattern background, better do brighntness reduction before to get a clear result
                ((area.status == LOGO_VISIBLE) && (rPixel > logo_imark) && (rPixel < logo_vmark) && area.intensity <= 141)) {
            int black   = 0;
            for (int i = 0; i < logoHeight * logoWidth; i++) {
                if (area.sobel[0][i]     == 0) black++;
            }
            int quoteBlack   = 100 * black  / (logoHeight * logoWidth);
            int quoteInverse = 100 * iPixel / ((logoHeight * logoWidth) -mPixel);
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) black quote: %d%%, inverse quote %d%%", frameCurrent, quoteBlack, quoteInverse);
#endif
            // check logo state invisible, new logo visible
            if ((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark)) {
                // example logo visible
                // area intensity 115, black quote: 48%, inverse quote 49%    -> logo visible, tree in background
                if ((quoteBlack > 48) && (quoteInverse > 49)) {
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cMarkAdLogo::Detect(): frame (%6d): quote above limit, assume matches only from patten and logo is still invisible", frameCurrent);
#endif
                    return LOGO_NOCHANGE;
                }
                // pattern in bright background, no current logo, no logo detection possible
                if ((quoteBlack >= 45) && (area.intensity >= 160)) {
                    area.counter--;       // assume only pattern, no logo
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cMarkAdLogo::Detect(): frame (%6d): quote above limit, assume matches only from patten and logo is still invisible", frameCurrent);
#endif
                    return LOGO_NOCHANGE;
                }
                logoStatus = true;  // now we trust a positiv logo detection
            }
            else {
                // check logo state visible, new unclear result, trust less bright picture more than bright picures
                if (((quoteBlack >= 46) && ((quoteBlack - quoteInverse) <= 0)) ||                                   // eg. tree before bright sky
                        ((area.intensity < 128) && (quoteBlack >= 21) && ((quoteBlack - quoteInverse) <= 4))) {
                    rPixel = logo_imark; // set to invisible
                    logoStatus = true;
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cMarkAdLogo::Detect(): frame (%6d) quote above limit, assume matches only from patten and logo is still visible", frameCurrent);
#endif
                }
            }
        }

        // if current state is logo visible or uninitialized (to get an early logo start) and we have a lot of matches, now we trust logo is still there
        if (!logoStatus &&
                ((area.status == LOGO_VISIBLE) ||  (area.status == LOGO_UNINITIALIZED)) &&
                (rPixel >= logo_imark)) {
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) high machtes, trust logo still visable", frameCurrent);
#endif
            logoStatus = true;
        }

        // check if we have a valid logo visible/invisible result
#define MAX_AREA_INTENSITY      69    // limit to reduce brightness if we have a high pixel logo
#define MAX_AREA_INTENSITY_LOW  56    // limit to reduce brightness if we have a low pixel logo
        int maxBrightness = MAX_AREA_INTENSITY;
        if (maContext->Video.Logo.pixelRatio <= LOW_PIXEL_LOGO) maxBrightness = MAX_AREA_INTENSITY_LOW;
        if (!logoStatus && (area.intensity <= maxBrightness) && (rPixel <= logo_imark)) logoStatus = true;  // we have no bright picture so we have a valid logo invisible result

        // if we have still no match, try to copy colour planes into grey planes
        // some channel use coloured logo at broadcast start
        // for performance reason we do this only for the known channel
        if (!logoStatus && criteria->LogoColorChange(maContext->Info.ChannelName)) {
            // save state
            int intensityOld = area.intensity;
            int rPixelOld = rPixel;

            if (!isInitColourChange) {
                LogoGreyToColour();
                isInitColourChange = true;
            }
            rPixel = 0;
            for (int plane = 1; plane < PLANES; plane++) {
                // reset all planes
                area.rPixel[plane] = 0;
                area.valid[plane]  = true;
                area.mPixel[plane] = area.mPixel[0] / 4;
                SobelPlane(plane, 0);
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
                if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
                    Save(frameCurrent, area.sobel, plane, "area.sobel_coloured");
                }
#endif
                rPixel += area.rPixel[plane];
                area.valid[plane] = false;
                area.mPixel[plane] = 0;
                area.rPixel[plane] = 0;
            }
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cMarkAdLogo::Detect(): frame (%6d) maybe colour change, try plane 1 and plan 2", frameCurrent);
            char detectStatus[] = "o";
            if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
            if (rPixel <= logo_imark) strcpy(detectStatus, "-");
            dsyslog("cMarkAdLogo::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
#endif
            if (rPixel <= logo_imark) { // we found no coloured logo here, restore old state
                area.intensity = intensityOld;
                rPixel = rPixelOld;
            }
            if (rPixel >= logo_vmark) logoStatus = true;  // we found colored logo
        }

        // try to reduce brightness and increase contrast
        // check area intensitiy
        // notice: there can be very bright logo parts in dark areas, this will result in a lower brightness
        // we handle this cases in ReduceBrightness() when we detect contrast
        // check if area is bright
        if (!logoStatus && (area.intensity > maxBrightness) && (area.intensity <= 221)) {  //  only if we don't have a valid result yet
            // reduce brightness and increase contrast
            logoStatus = ReduceBrightness(frameCurrent, logo_vmark, logo_imark);
            if (logoStatus) rPixel = area.rPixel[0];  // set new pixel result
        }
    }
    else {
        // if we have more planes we can still have a problem with coloured logo on same colored background
        if ((rPixel >= logo_vmark)) logoStatus = true;  // trust logo visible result
        if (rPixel == 0)            logoStatus = true;  // trust logo invisible result without any matches

        // maybe coloured logo on same colored background, check planes separated, all planes must be under invisible limit
        if (!logoStatus && (rPixel <= logo_imark) && (area.intensity <= 132)) {  // do not trust logo invisible detection on bright background
            bool planeStatus = true;
            for (int i = 0; i < PLANES; i++) {
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cMarkAdLogo::Detect():       plane %d: rp=%5d | ip=%5d | mp=%5d | mpV=%5.f | mpI=%5.f |", i, area.rPixel[i], area.iPixel[i], area.mPixel[i], area.mPixel[i] * LOGO_VMARK, area.mPixel[i] * LOGO_IMARK);
#endif
                if (area.rPixel[i] >= (area.mPixel[i] * LOGO_IMARK)) {
                    planeStatus = false;
                    break;
                }
            }
            logoStatus = planeStatus;
        }
    }

    if (!logoStatus) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cMarkAdLogo::Detect(): frame (%6d) no valid result", frameCurrent);
#endif
        return LOGO_NOCHANGE;
    }

// set logo visible/unvisible status
// set initial start status
    if (area.status == LOGO_UNINITIALIZED) {
        if (rPixel >= logo_vmark) area.status = LOGO_VISIBLE;
        if (rPixel <= logo_imark) area.status = LOGO_INVISIBLE;  // wait for a clear result
        if (area.frameNumber == -1) area.frameNumber = frameCurrent;
        *logoFrameNumber = area.frameNumber;
        return area.status;
    }
    if (area.status == LOGO_RESTART) {
        if (rPixel >= logo_vmark) area.status = LOGO_VISIBLE;
        if (rPixel <= logo_imark) area.status = LOGO_INVISIBLE;  // wait for a clear result
        *logoFrameNumber = -1;   // no logo change report after detection restart
        area.frameNumber = frameCurrent;
        return area.status;
    }


    int ret = LOGO_NOCHANGE;
    if (rPixel >= logo_vmark) {
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

    if (rPixel <=logo_imark) {
        if (area.status == LOGO_VISIBLE) {
            if (area.counter >= LOGO_IMAXCOUNT) {
                area.status = ret = LOGO_INVISIBLE;
                *logoFrameNumber = area.frameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.frameNumber = frameBefore;
                area.counter++;
                if (rPixel <= (logo_imark / 2)) area.counter++;   // good detect for logo invisible
                if (rPixel <= (logo_imark / 4)) area.counter++;   // good detect for logo invisible
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


    // if we have no clear result, we are more uncertain of logo state
    if ((rPixel < logo_vmark) && (rPixel > logo_imark)) {
        area.counter--;
        if (area.counter < 0) area.counter = 0;
    }

#ifdef DEBUG_LOGO_DETECTION
    strcpy(detectStatus, "o");
    if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
    if (rPixel <= logo_imark) strcpy(detectStatus, "-");
    dsyslog("cMarkAdLogo::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
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
                criteria->SetDetectionState(MT_LOGOCHANGE, false);
            }
            else {
                char *buf=nullptr;
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
                                cExtractLogo *ptr_cExtractLogo = new cExtractLogo(maContext, criteria, maContext->Video.Info.AspectRatio, recordingIndexMarkAdLogo);  // search logo from current frame
                                ALLOC(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo");
                                if (ptr_cExtractLogo->SearchLogo(maContext, criteria, iFrameCurrent, true) > 0) dsyslog("cMarkAdLogo::Process(): no logo found in recording");
                                else dsyslog("cMarkAdLogo::Process(): new logo for %s found in recording",buf);
                                FREE(sizeof(*ptr_cExtractLogo), "ptr_cExtractLogo"); // ptr_cExtraceLogo is valid because it was used above
                                delete ptr_cExtractLogo;
                                ptr_cExtractLogo = nullptr;
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
                        criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_DISABLED);
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


// detect scene change
cMarkAdSceneChange::cMarkAdSceneChange(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
}


cMarkAdSceneChange::~cMarkAdSceneChange() {
    if (prevHistogram) {  // in case constructor called but never Process()
        FREE(sizeof(*prevHistogram), "SceneChangeHistogramm");
        free(prevHistogram);
    }
}


int cMarkAdSceneChange::Process(const int currentFrameNumber, int *changeFrameNumber) {
    if (!maContext) return SCENE_ERROR;
    if (!changeFrameNumber) return SCENE_ERROR;
    if (!maContext->Video.Data.valid) return SCENE_ERROR;
    if (maContext->Video.Info.framesPerSecond == 0) return SCENE_ERROR;
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdSceneChange::Process(): missing maContext->Video.Info.height");
        return SCENE_ERROR;
    }
    if (!maContext->Video.Info.width) {
        dsyslog("cMarkAdSceneChange::Process(): missing maContext->Video.Info.width");
        return SCENE_ERROR;
    }
    if (!maContext->Video.Data.Plane[0]) {
        dsyslog("cMarkAdSceneChange::Process(): Video.Data.Plane[0] missing");
        return SCENE_ERROR;
    }

    // get simple histogramm from current frame
    int *currentHistogram = nullptr;
    currentHistogram = static_cast<int *>(malloc(sizeof(int) * 256));
    ALLOC(sizeof(*currentHistogram), "SceneChangeHistogramm");
    memset(currentHistogram, 0, sizeof(int[256]));
    for (int Y = 0; Y < maContext->Video.Info.height; Y++) {
        for (int X = 0; X < maContext->Video.Info.width; X++) {
            uchar val = maContext->Video.Data.Plane[0][X + (Y * maContext->Video.Data.PlaneLinesize[0])];
            currentHistogram[val]++;
        }
    }
    if (!prevHistogram) {
        prevHistogram = currentHistogram;
        return SCENE_UNINITIALIZED;
    }

    // calculate distance between pevios und current frame
    long int difference = 0;  // prevent integer overflow
    for (int i = 0; i < 256; i++) {
        difference += abs(prevHistogram[i] - currentHistogram[i]);  // calculte difference, smaller is more similar
    }
    int diffQuote = 1000 * difference / (maContext->Video.Info.height * maContext->Video.Info.width * 2);
#ifdef DEBUG_SCENE_CHANGE
    dsyslog("cMarkAdSceneChange::Process(): previous frame (%7d) and current frame (%7d): status %2d, blendCount %2d, blendFrame %7d, difference %7ld, diffQute %4d", prevFrameNumber, currentFrameNumber, sceneStatus, blendCount, blendFrame, difference, diffQuote);
#endif
    FREE(sizeof(*prevHistogram), "SceneChangeHistogramm");
    free(prevHistogram);

#define DIFF_SCENE_NEW         400   // new scene during blend, force new scene stop/start, changed from 500 to 400
#define DIFF_SCENE_CHANGE      110
#define DIFF_SCENE_BLEND_START  60
#define DIFF_SCENE_BLEND_STOP   40
#define SCENE_BLEND_FRAMES       5
// end of scene during active scene blend
    if ((diffQuote >= DIFF_SCENE_NEW) && (sceneStatus == SCENE_BLEND)) {
        *changeFrameNumber = prevFrameNumber;
        sceneStatus        = SCENE_STOP;
#ifdef DEBUG_SCENE_CHANGE
        dsyslog("cMarkAdSceneChange::Process(): frame (%7d) end of scene during active blend", prevFrameNumber);
#endif
    }
// end of scene
    else if (diffQuote >= DIFF_SCENE_CHANGE) {
        if (blendFrame < 0) blendFrame = prevFrameNumber;
        blendCount++;
        if ((blendCount <= SCENE_BLEND_FRAMES) && (sceneStatus != SCENE_STOP)) {
            if (blendCount < SCENE_BLEND_FRAMES) blendCount = SCENE_BLEND_FRAMES;  // use blendCount as active scene change
            *changeFrameNumber = blendFrame;
            sceneStatus        = SCENE_STOP;
#ifdef DEBUG_SCENE_CHANGE
            dsyslog("cMarkAdSceneChange::Process(): frame (%7d) end of scene", prevFrameNumber);
#endif
        }
        else sceneStatus = SCENE_BLEND;
    }
// activ scene blend
    else if (diffQuote >= DIFF_SCENE_BLEND_START) {
        if (blendFrame < 0) blendFrame = prevFrameNumber;
        blendCount++;
        if ((blendCount == SCENE_BLEND_FRAMES)) {
            *changeFrameNumber = blendFrame;
            sceneStatus = SCENE_STOP;
#ifdef DEBUG_SCENE_CHANGE
            dsyslog("cMarkAdSceneChange::Process(): frame (%7d) scene blend start at frame (%d)", prevFrameNumber, blendFrame);
#endif
        }
        else sceneStatus = SCENE_BLEND;
    }
// unclear result, keep state
    else if ((diffQuote < DIFF_SCENE_BLEND_START) && (diffQuote > DIFF_SCENE_BLEND_STOP)) {
#ifdef DEBUG_SCENE_CHANGE
        if (sceneStatus == SCENE_BLEND) dsyslog("cMarkAdSceneChange::Process(): frame (%7d) scene blend continue at frame (%d)", prevFrameNumber, blendFrame);
#endif
    }
// start of next scene
    else {
        if ((sceneStatus == SCENE_STOP) || ((sceneStatus == SCENE_BLEND) && (blendCount >= SCENE_BLEND_FRAMES))) {
            *changeFrameNumber = prevFrameNumber;
            sceneStatus        = SCENE_START;
#ifdef DEBUG_SCENE_CHANGE
            dsyslog("cMarkAdSceneChange::Process(): frame (%7d) start of scene", prevFrameNumber);
#endif
        }
        else sceneStatus = SCENE_NOCHANGE;
        blendFrame = -1;
        blendCount =  0;
    }

    prevHistogram   = currentHistogram;
    prevFrameNumber = currentFrameNumber;

#ifdef DEBUG_SCENE_CHANGE
    if (*changeFrameNumber >= 0) {
        if (sceneStatus == SCENE_START) dsyslog("cMarkAdSceneChange::Process(): new mark: MT_SCENESTART at frame (%7d)", *changeFrameNumber);
        if (sceneStatus == SCENE_STOP)  dsyslog("cMarkAdSceneChange::Process(): new mark: MT_SCENESTOP  at frame (%7d)", *changeFrameNumber);
    }
#endif

    if (*changeFrameNumber >= 0) return sceneStatus;
    else return SCENE_NOCHANGE;
}


// detect blackscreen
cMarkAdBlackScreen::cMarkAdBlackScreen(sMarkAdContext *maContextParam) {
    maContext = maContextParam;
    Clear();
}


void cMarkAdBlackScreen::Clear() {
    blackScreenStatus = BLACKSCREEN_UNINITIALIZED;
    lowerBorderStatus = BLACKSCREEN_UNINITIALIZED;
}


// check if current frame is a blackscreen
// return: -1 blackscreen start (notice: this is a STOP mark)
//          0 no status change
//          1 blackscreen end (notice: this is a START mark)
//
int cMarkAdBlackScreen::Process(__attribute__((unused)) const int frameCurrent) {
#define BLACKNESS    19  // maximum brightness to detect a blackscreen, +1 to detect end of blackscreen, changed from 17 to 19 because of undetected black screen
#define WHITE_LOWER 220  // minimum brightness to detect white lower border
    if (!maContext) return 0;
    if (!maContext->Video.Data.valid) return 0;
    if (maContext->Video.Info.framesPerSecond == 0) return 0;
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdBlackScreen::Process(): missing maContext->Video.Info.height");
        return 0;
    }
    if (!maContext->Video.Info.width) {
        dsyslog("cMarkAdBlackScreen::Process(): missing maContext->Video.Info.width");
        return 0;
    }
    if (!maContext->Video.Data.Plane[0]) {
        dsyslog("cMarkAdBlackScreen::Process(): Video.Data.Plane[0] missing");
        return 0;
    }

#define PIXEL_COUNT_LOWER 25  // count pixel from bottom for detetion of lower border, changed from 40 to 25
    int maxBrightnessAll;
    int maxBrightnessLower;   // for detetion of black lower border
    int minBrightnessLower;   // for detetion of white lower border

    // calculate limit with hysteresis
    if (blackScreenStatus == BLACKSCREEN_INVISIBLE) maxBrightnessAll = BLACKNESS * maContext->Video.Info.width * maContext->Video.Info.height;
    else maxBrightnessAll = (BLACKNESS + 1) * maContext->Video.Info.width * maContext->Video.Info.height;

    // limit for black lower border
    if (lowerBorderStatus == BLACKLOWER_INVISIBLE) maxBrightnessLower = BLACKNESS * maContext->Video.Info.width * PIXEL_COUNT_LOWER;
    else maxBrightnessLower = (BLACKNESS + 1) * maContext->Video.Info.width * PIXEL_COUNT_LOWER;

    // limit for white lower border
    if (lowerBorderStatus == BLACKLOWER_INVISIBLE) minBrightnessLower = WHITE_LOWER * maContext->Video.Info.width * PIXEL_COUNT_LOWER;
    else minBrightnessLower = (WHITE_LOWER - 1) * maContext->Video.Info.width * PIXEL_COUNT_LOWER;

    int maxBrightnessGrey = 28 * maContext->Video.Info.width * maContext->Video.Info.height;

    int valAll   = 0;
    int valLower = 0;
    int maxPixel = 0;
    // calculate blackness
    for (int x = 0; x < maContext->Video.Info.width; x++) {
        for (int y = 0; y < maContext->Video.Info.height; y++) {
            int pixel = maContext->Video.Data.Plane[0][x + y * maContext->Video.Data.PlaneLinesize[0]];
            valAll += pixel;
            if (y > (maContext->Video.Info.height - PIXEL_COUNT_LOWER)) valLower += pixel;
            if (pixel > maxPixel) maxPixel = pixel;
        }
    }

#ifdef DEBUG_BLACKSCREEN
    int debugValAll   = valAll   / (maContext->Video.Info.width * maContext->Video.Info.height);
    int debugValLower = valLower / (maContext->Video.Info.width * PIXEL_COUNT_LOWER);
    dsyslog("cMarkAdBlackScreen::Process(): frame (%d): blackScreenStatus %d, blackness %3d (expect <%d for start, >%d for end), lowerBorderStatus %d, lower %3d", frameCurrent, blackScreenStatus, debugValAll, BLACKNESS, BLACKNESS, lowerBorderStatus, debugValLower);
#endif

    // full blackscreen now visible
    if (((valAll <= maxBrightnessAll) || ((valAll <= maxBrightnessGrey) && (maxPixel <= 73))) && (blackScreenStatus != BLACKSCREEN_VISIBLE)) {
        int ret = BLACKSCREEN_VISIBLE;
        if (blackScreenStatus == BLACKSCREEN_UNINITIALIZED) ret = BLACKSCREEN_NOCHANGE;
        blackScreenStatus = BLACKSCREEN_VISIBLE;
        return ret; // detected start of black screen
    }
    // full blackscreen now invisible
    if ((valAll > maxBrightnessAll) && ((valAll > maxBrightnessGrey) || (maxPixel > 73)) && (blackScreenStatus != BLACKSCREEN_INVISIBLE)) {  // TLC use one dark grey separator picture between broadcasts, changed from 50 to 73
        int ret = BLACKSCREEN_INVISIBLE;
        if (blackScreenStatus == BLACKSCREEN_UNINITIALIZED) ret = BLACKSCREEN_NOCHANGE;
        blackScreenStatus = BLACKSCREEN_INVISIBLE;
        return ret; // detected stop of black screen
    }

    // now lower black/white border visible, only report lower black/white border if we have no full black screen
    if ((((valLower <= maxBrightnessLower) && (valAll >= 2 * maxBrightnessAll)) || (valLower >= minBrightnessLower)) &&  // do not detect black lower border start in very dark scene
            (lowerBorderStatus != BLACKLOWER_VISIBLE) && (blackScreenStatus != BLACKSCREEN_VISIBLE)) {
        int ret = BLACKLOWER_VISIBLE;
        if (lowerBorderStatus == BLACKSCREEN_UNINITIALIZED) ret = BLACKSCREEN_NOCHANGE;
        lowerBorderStatus = BLACKLOWER_VISIBLE;
        return ret; // detected start of black screen
    }
    // lower black border now invisible
    if ((valLower > maxBrightnessLower) && (valLower < minBrightnessLower) &&
            (lowerBorderStatus != BLACKLOWER_INVISIBLE) && (blackScreenStatus == BLACKSCREEN_INVISIBLE)) {  // only report if no active blackscreen
        int ret = BLACKLOWER_INVISIBLE;
        if (lowerBorderStatus == BLACKSCREEN_UNINITIALIZED) ret = BLACKSCREEN_NOCHANGE;
        lowerBorderStatus = BLACKLOWER_INVISIBLE;
        return ret; // detected stop of black screen
    }

    return BLACKSCREEN_NOCHANGE;
}


cMarkAdBlackBordersHoriz::cMarkAdBlackBordersHoriz(sMarkAdContext *maContextParam, cCriteria *criteriaParam) {
    criteria = criteriaParam;
    maContext = maContextParam;
    Clear();
}


int cMarkAdBlackBordersHoriz::GetFirstBorderFrame() const {
    if (borderstatus != HBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cMarkAdBlackBordersHoriz::State() const {
    return borderstatus;
}


void cMarkAdBlackBordersHoriz::Clear(const bool isRestart) {
    dsyslog("cMarkAdBlackBordersHoriz::Clear():  clear hborder state");
    if (isRestart) borderstatus = HBORDER_RESTART;
    else           borderstatus = HBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cMarkAdBlackBordersHoriz::Process(const int FrameNumber, int *borderFrame) {
#define CHECKHEIGHT           5  // changed from 8 to 5
#define BRIGHTNESS_H_SURE    22
#define BRIGHTNESS_H_MAYBE  137  // some channel have logo or infos in border, so we will detect a higher value, changed from 131 to 137
#define NO_HBORDER          200  // internal limit for early loop exit, must be more than BRIGHTNESS_H_MAYBE
    if (!maContext) return HBORDER_ERROR;
    if (!maContext->Video.Data.valid) return HBORDER_ERROR;
    if (maContext->Video.Info.framesPerSecond == 0) return HBORDER_ERROR;

    int brightnessSure  = BRIGHTNESS_H_SURE;
    if (criteria->LogoInBorder(maContext->Info.ChannelName)) brightnessSure = BRIGHTNESS_H_SURE + 1;  // for pixel from logo
    int brightnessMaybe = BRIGHTNESS_H_SURE;
    if (criteria->InfoInBorder(maContext->Info.ChannelName)) brightnessMaybe = BRIGHTNESS_H_MAYBE;    // for pixel from info in border
    *borderFrame = -1;   // framenumber of first hborder, otherwise -1
    if (!maContext->Video.Info.height) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() video hight missing");
        return HBORDER_ERROR;
    }
    int height = maContext->Video.Info.height;

    if (!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersHoriz::Process() Video.Data.PlaneLinesize[0] not initialized");
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

    // if we have a bottom border, test top border
    if (valBottom <= brightnessMaybe) {
        start = maContext->Video.Data.PlaneLinesize[0];
        end = maContext->Video.Data.PlaneLinesize[0] * CHECKHEIGHT;
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
    else valTop = NO_HBORDER;   // we have no botton border, so we do not have to calculate top border

#ifdef DEBUG_HBORDER
    dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder brightness top %4d bottom %4d (expect one <=%d and one <= %d)", FrameNumber, valTop, valBottom, brightnessSure, brightnessMaybe);
#endif

    if ((valTop <= brightnessMaybe) && (valBottom <= brightnessSure) || (valTop <= brightnessSure) && (valBottom <= brightnessMaybe)) {
        // hborder detected
#ifdef DEBUG_HBORDER
        int duration = (FrameNumber - borderframenumber) / maContext->Video.Info.framesPerSecond;
        dsyslog("cMarkAdBlackBordersHoriz::Process(): frame (%7d) hborder ++++++: borderstatus %d, borderframenumber (%d), duration %ds", FrameNumber, borderstatus, borderframenumber, duration);
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


cMarkAdBlackBordersVert::cMarkAdBlackBordersVert(sMarkAdContext *maContextParam, cCriteria *criteriaParam) {
    maContext = maContextParam;
    criteria  = criteriaParam;
    Clear();
}


void cMarkAdBlackBordersVert::Clear(const bool isRestart) {
    if (isRestart) borderstatus = VBORDER_RESTART;
    else           borderstatus = VBORDER_UNINITIALIZED;
    borderframenumber = -1;
    darkFrameNumber   = INT_MAX;
}


int cMarkAdBlackBordersVert::GetFirstBorderFrame() const {
    if (borderstatus != VBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cMarkAdBlackBordersVert::Process(int frameNumber, int *borderFrame) {
#define CHECKWIDTH 10           // do not reduce, very small vborder are unreliable to detect, better use logo in this case
#define BRIGHTNESS_V_SURE   27  // changed from 33 to 27, some channels has dark separator before vborder start
#define BRIGHTNESS_V_MAYBE 101  // some channel have logo or infos in one border, so we must accept a higher value, changed from 100 to 101
    if (!maContext) {
        dsyslog("cMarkAdBlackBordersVert::Process(): maContext not valid");
        return VBORDER_ERROR;
    }
    if (!maContext->Video.Data.valid) return VBORDER_ERROR;  // no error, this is expected if bDecodeVideo is disabled
    if (maContext->Video.Info.framesPerSecond == 0) {
        dsyslog("cMarkAdBlackBordersVert::Process(): video frames per second  not valid");
        return VBORDER_ERROR;
    }
    // set limits
    int brightnessSure  = BRIGHTNESS_V_SURE;
    if (criteria->LogoInBorder(maContext->Info.ChannelName)) brightnessSure = BRIGHTNESS_V_SURE + 1;  // for pixel from logo
    int brightnessMaybe = BRIGHTNESS_V_SURE;
    if (criteria->InfoInBorder(maContext->Info.ChannelName)) brightnessMaybe = BRIGHTNESS_V_MAYBE;    // for pixel from info in border

    *borderFrame = -1;
    int valLeft  =  0;
    int valRight =  0;
    int cnt      =  0;

    if(!maContext->Video.Data.PlaneLinesize[0]) {
        dsyslog("cMarkAdBlackBordersVert::Process(): Video.Data.PlaneLinesize[0] missing");
        return VBORDER_ERROR;
    }


    // check left border
    for (int y = 0; y < maContext->Video.Info.height; y++) {
        for (int x = 0; x < CHECKWIDTH; x++) {
            valLeft += maContext->Video.Data.Plane[0][x + (y * maContext->Video.Data.PlaneLinesize[0])];
            cnt++;
        }
    }
    valLeft /= cnt;

    // check right border
    if (valLeft <= brightnessMaybe) {
        cnt = 0;
        for (int y = 0; y < maContext->Video.Info.height; y++) {
            for (int x = maContext->Video.Info.width - CHECKWIDTH; x < maContext->Video.Info.width; x++) {
                valRight += maContext->Video.Data.Plane[0][x + (y * maContext->Video.Data.PlaneLinesize[0])];
                cnt++;
            }
        }
        valRight /= cnt;
    }
    else valRight = INT_MAX;  // left side has no border, so we have not to check right side

#ifdef DEBUG_VBORDER
    dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d): vborder status: %d, valLeft: %10d, valRight: %10d", frameNumber, borderstatus, valLeft, valRight);
    if (darkFrameNumber < INT_MAX) dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d):  frist vborder in dark picture: (%5d)", frameNumber, darkFrameNumber);
    if (borderframenumber >= 0) dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d):  frist vborder: [bright (%5d), dark (%5d)], duration: %ds", frameNumber, borderframenumber, darkFrameNumber, static_cast<int> ((frameNumber - borderframenumber) / maContext->Video.Info.framesPerSecond));
#endif
#define BRIGHTNESS_MIN (38 * maContext->Video.Info.height * maContext->Video.Info.width)  // changed from 51 to 38, dark part with vborder
    if (((valLeft <= brightnessMaybe) && (valRight <= brightnessSure)) || ((valLeft <= brightnessSure) && (valRight <= brightnessMaybe))) {
        // vborder detected
        if (borderframenumber == -1) {   // first vborder detected
            // prevent false detection in a very dark scene, we have to get at least one vborder in a bright picture to accept start frame
#ifndef DEBUG_VBORDER
            bool end_loop   = false;
#endif
            int  brightness = 0;
            for (int y = 0; y < maContext->Video.Info.height ; y++) {
                for (int x = 0; x < maContext->Video.Info.width; x++) {
                    brightness += maContext->Video.Data.Plane[0][x + (y * maContext->Video.Data.PlaneLinesize[0])];
                    if (brightness >= BRIGHTNESS_MIN) {
#ifndef DEBUG_VBORDER
                        end_loop = true;
                        break;
#endif
                    }
                }
#ifndef DEBUG_VBORDER
                if (end_loop) break;
#endif
            }
            if (brightness < BRIGHTNESS_MIN) {
                darkFrameNumber = std::min(darkFrameNumber, frameNumber);  // set to first frame with vborder
#ifdef DEBUG_VBORDER
                minBrightness = std::min(brightness, minBrightness);
                maxBrightness = std::max(brightness, maxBrightness);
                dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d) has a dark picture: %d, delay vborder start at (%7d), minBrightness %d, maxBrightness %d", frameNumber, brightness, darkFrameNumber, minBrightness, maxBrightness);
#endif
            }
            else {
                borderframenumber = std::min(frameNumber, darkFrameNumber);      // use first vborder
#ifdef DEBUG_VBORDER
                minBrightness = INT_MAX;
                maxBrightness = 0;
                dsyslog("cMarkAdBlackBordersVert::Process(): frame (%7d) has a bright picture %d, accept vborder start at (%7d)", frameNumber, brightness, borderframenumber);
#endif
                darkFrameNumber = INT_MAX;
            }
        }
        if (borderstatus != VBORDER_VISIBLE) {
            if ((borderframenumber >= 0) && (frameNumber > (borderframenumber + maContext->Video.Info.framesPerSecond * MIN_V_BORDER_SECS))) {
                switch (borderstatus) {
                case VBORDER_UNINITIALIZED:
                    *borderFrame = 0;
                    break;
                case VBORDER_RESTART:
                    *borderFrame = -1;  // do not report back a border change after detection restart, only set internal state
                    break;
                default:
                    *borderFrame = borderframenumber;
                }
                borderstatus = VBORDER_VISIBLE; // detected start of black border
            }
        }
    }
    else {
#ifdef DEBUG_VBORDER
        minBrightness = INT_MAX;
        maxBrightness = 0;
#endif
        // no vborder detected
        if (borderstatus != VBORDER_INVISIBLE) {
            if ((borderstatus == VBORDER_UNINITIALIZED) || (borderstatus == VBORDER_RESTART)) *borderFrame = -1;  // do not report back a border change, only set internal state
            else *borderFrame = frameNumber;
            borderstatus = VBORDER_INVISIBLE; // detected stop of black border
        }
        borderframenumber = -1; // restart from scratch
        darkFrameNumber = INT_MAX;
    }
    return borderstatus;
}


cMarkAdOverlap::cMarkAdOverlap(sMarkAdContext *maContextParam) {
    maContext          = maContextParam;
    histbuf[OV_BEFORE] = nullptr;
    histbuf[OV_AFTER]  = nullptr;
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
        histbuf[OV_BEFORE] = nullptr;
    }

    if (histbuf[OV_AFTER]) {
        FREE(sizeof(*histbuf[OV_AFTER]), "histbuf");
        delete[] histbuf[OV_AFTER];
        histbuf[OV_AFTER] = nullptr;
    }

    similarCutOff    =  0;
    similarMinLength =  0;
    lastFrameNumber  = -1;
}


void cMarkAdOverlap::GetHistogram(simpleHistogram &dest) const {
    memset(dest, 0, sizeof(simpleHistogram));
    int startY =  maContext->Video.Info.height * 0.22;  // ignore top part because there can be info border at start after the advertising, changed from 0.16 to 0.2 to 0.22
    int endY   =  maContext->Video.Info.height * 0.82; // ignore bottom part because there can info border text at start after the advertising, changed from 0.87 to 0.82
    for (int Y = startY; Y < endY; Y++) {
        for (int X = 0; X < maContext->Video.Info.width; X++) {
            uchar val = maContext->Video.Data.Plane[0][X + (Y * maContext->Video.Data.PlaneLinesize[0])];
            dest[val]++;
        }
    }
}


int cMarkAdOverlap::AreSimilar(const simpleHistogram &hist1, const simpleHistogram &hist2) const { // return > 0 if similar, else <= 0
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
//                    tsyslog("cMarkAdOverlap::Detect(): end of similar from (%5d) to (%5d) and (%5d) to (%5d) length %5dms",  histbuf[OV_BEFORE][firstSimilarBeforeStopMark].frameNumber, histbuf[OV_BEFORE][tmpindexBeforeStopMark].frameNumber, histbuf[OV_AFTER][firstSimilarAfterStartMark].frameNumber, histbuf[OV_AFTER][tmpindexAfterStartMark].frameNumber, simLength);
//                    tsyslog("cMarkAdOverlap::Detect():                with similBefore %5d, simil %5d, similAfter %5d", similBefore, simil, similAfter);
                }
                if (similBefore > 0) simil = similBefore;
                if (similAfter  > 0) simil = similAfter;
            }

#ifdef DEBUG_OVERLAP
            if (simil >= 0) dsyslog("cMarkAdOverlap::Detect(): +++++     similar frame (%5d) (index %3d) and (%5d) (index %3d) -> simil %5d (max %d) length %2dms similarMaxCnt %2d)", histbuf[OV_BEFORE][indexBeforeStopMark].frameNumber, indexBeforeStopMark, histbuf[OV_AFTER][indexAfterStartMark].frameNumber, indexAfterStartMark, simil, similarCutOff, simLength, similarMinLength);
#endif
            // found long enough overlap, store position

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

        if (histcnt[OV_AFTER] >= histframes[OV_AFTER] - 3) {  // for interlaced videos, we will not get some start frames
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


cMarkAdVideo::cMarkAdVideo(sMarkAdContext *maContextParam, cCriteria *criteriaParam, cIndex *recordingIndex) {
    maContext                 = maContextParam;
    criteria                  = criteriaParam;
    recordingIndexMarkAdVideo = recordingIndex;

    aspectRatio.num = 0;
    aspectRatio.den = 0;

    sceneChange = new cMarkAdSceneChange(maContext);
    ALLOC(sizeof(*sceneChange), "sceneChange");

    blackScreen = new cMarkAdBlackScreen(maContext);
    ALLOC(sizeof(*blackScreen), "blackScreen");

    hborder = new cMarkAdBlackBordersHoriz(maContext, criteria);
    ALLOC(sizeof(*hborder), "hborder");

    vborder = new cMarkAdBlackBordersVert(maContext, criteria);
    ALLOC(sizeof(*vborder), "vborder");

    logo = new cMarkAdLogo(maContext, criteria, recordingIndexMarkAdVideo);
    ALLOC(sizeof(*logo), "cMarkAdVideo_logo");

    Clear(false);
}


cMarkAdVideo::~cMarkAdVideo() {
    ResetMarks();
    if (sceneChange) {
        FREE(sizeof(*sceneChange), "sceneChange");
        delete sceneChange;
    }
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
        if (blackScreen && !criteria->GetDetectionState(MT_BLACKCHANGE))   blackScreen->Clear();
        if (logo        && !criteria->GetDetectionState(MT_LOGOCHANGE))    logo->Clear(true);
        if (vborder     && !criteria->GetDetectionState(MT_VBORDERCHANGE)) vborder->Clear(true);
        if (hborder     && !criteria->GetDetectionState(MT_HBORDERCHANGE)) hborder->Clear(true);
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
    videoMarks = {};
}


bool cMarkAdVideo::AddMark(int type, int position, const sAspectRatio *before, const sAspectRatio *after) {
    if (videoMarks.Count >= videoMarks.maxCount) {  // array start with 0
        esyslog("cMarkAdVideo::AddMark(): too much marks %d at once detected", videoMarks.Count);
        return false;
    }
    if (before) {
        videoMarks.Number[videoMarks.Count].AspectRatioBefore.num = before->num;
        videoMarks.Number[videoMarks.Count].AspectRatioBefore.den = before->den;
    }
    if (after) {
        videoMarks.Number[videoMarks.Count].AspectRatioAfter.num = after->num;
        videoMarks.Number[videoMarks.Count].AspectRatioAfter.den = after->den;
    }
    videoMarks.Number[videoMarks.Count].position = position;
    videoMarks.Number[videoMarks.Count].type     = type;
    videoMarks.Count++;
    return true;
}


bool cMarkAdVideo::AspectRatioChange(const sAspectRatio &AspectRatioA, const sAspectRatio &AspectRatioB) {
    if (((AspectRatioA.num == 0) || (AspectRatioA.den == 0)) &&
            ((AspectRatioB.num == 4) && (AspectRatioB.den == 3))) return true;

    if ((AspectRatioA.num != AspectRatioB.num) && (AspectRatioA.den != AspectRatioB.den)) return true;
    return false;
}


sMarkAdMarks *cMarkAdVideo::Process(int iFrameBefore, const int iFrameCurrent, const int frameCurrent) {
    if ((iFrameCurrent < 0) || (frameCurrent < 0)) return nullptr;
    if (iFrameBefore < 0) iFrameBefore = 0; // this could happen at the start of recording

    int useFrame;
    if (maContext->Config->fullDecode) useFrame = frameCurrent;
    else useFrame = iFrameCurrent;
    ResetMarks();

    // scene change detection
    if (criteria->GetDetectionState(MT_SCENECHANGE)) {
        int changeFrame = -1;
        int sceneRet = sceneChange->Process(useFrame, &changeFrame);
        if (sceneRet == SCENE_START) AddMark(MT_SCENESTART, changeFrame);
        if (sceneRet == SCENE_STOP)  AddMark(MT_SCENESTOP,  changeFrame);
    }

    // black screen change detection
    if ((frameCurrent > 0) && criteria->GetDetectionState(MT_BLACKCHANGE)) { // first frame can be invalid result
        int blackret = blackScreen->Process(useFrame);
        switch (blackret) {
        case BLACKSCREEN_INVISIBLE:
            AddMark(MT_NOBLACKSTART, useFrame);  // first frame without blackscreen is start mark position
            break;
        case BLACKSCREEN_VISIBLE:
            AddMark(MT_NOBLACKSTOP,  useFrame);
            break;
        case BLACKLOWER_INVISIBLE:
            AddMark(MT_NOLOWERBORDERSTART, useFrame);  // first frame without lower border is start mark position
            break;
        case BLACKLOWER_VISIBLE:
            AddMark(MT_NOLOWERBORDERSTOP,  useFrame);
            break;
        default:
            break;
        }
    }

    // hborder change detection
    int hret = HBORDER_ERROR;
    if (criteria->GetDetectionState(MT_HBORDERCHANGE)) {
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
    else {
        if (hborder && (hborder->State() != HBORDER_UNINITIALIZED)) hborder->Clear();
    }

    // vborder change detection
    if (criteria->GetDetectionState(MT_VBORDERCHANGE)) {
        int vborderframenumber;
        int vret = vborder->Process(useFrame, &vborderframenumber);
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

    // aspect ratio change detection
    if (criteria->GetDetectionState(MT_ASPECTCHANGE)) {
        if (AspectRatioChange(aspectRatio, maContext->Video.Info.AspectRatio)) {
            if ((maContext->Info.AspectRatio.num == 4) && (maContext->Info.AspectRatio.den == 3)) {
                if ((maContext->Video.Info.AspectRatio.num == 4) && (maContext->Video.Info.AspectRatio.den == 3)) {
                    AddMark(MT_ASPECTSTART, frameCurrent, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
                else {
                    int stopPos = recordingIndexMarkAdVideo->GetIFrameBefore(frameCurrent - 1);
                    if (maContext->Video.Info.interlaced) {
                        stopPos--;  // one frame before to get last full frame with old aspect ratio
                        if (stopPos < 0) stopPos = 0;
                    }
                    AddMark(MT_ASPECTSTOP, stopPos, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
            }
            else {
                if ((maContext->Video.Info.AspectRatio.num == 16) && (maContext->Video.Info.AspectRatio.den == 9)) {
                    // do not set a initial 16:9 aspect ratio start mark for 16:9 videos, not useful
                    if ((aspectRatio.num != 0) || (aspectRatio.den != 0)) AddMark(MT_ASPECTSTART, aspectRatioBeforeFrame, &aspectRatio, &maContext->Video.Info.AspectRatio);
                }
                else {
                    int stopPos = recordingIndexMarkAdVideo->GetIFrameBefore(frameCurrent - 1);
                    if (maContext->Video.Info.interlaced) {
                        stopPos--;  // one frame before to get last full frame with old aspect ratio
                        if (stopPos < 0) stopPos = 0;
                    }
                    AddMark(MT_ASPECTSTOP, stopPos, &aspectRatio, &maContext->Video.Info.AspectRatio);
                    // 16:9 -> 4:3, this is end of broadcast (16:9) and start of next broadcast (4:3)
                    // if we have activ hborder add hborder stop mark, because hborder state will be cleared
                    if (hborder->State() == HBORDER_VISIBLE) {
                        dsyslog("cMarkAdVideo::Process(): hborder activ during aspect ratio change from 16:9 to 4:3, add hborder stop mark");
                        AddMark(MT_HBORDERSTOP, stopPos - 1);
                    }
                }
            }
            aspectRatio.num = maContext->Video.Info.AspectRatio.num;   // store new aspect ratio
            aspectRatio.den = maContext->Video.Info.AspectRatio.den;
        }
        else aspectRatioBeforeFrame = frameCurrent;
    }

    // logo change detection
    if (criteria->GetDetectionState(MT_LOGOCHANGE)) {
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

    if (videoMarks.Count > 0) {
        return &videoMarks;
    }
    else {
        return nullptr;
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
