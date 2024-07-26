/*
 * sobel.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "sobel.h"


cSobel::cSobel(const int videoWidthParam, const int videoHeightParam, const int boundaryParam) {

    // set video size
    videoWidth  = videoWidthParam;
    videoHeight = videoHeightParam;
    boundary    = boundaryParam;

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

    dsyslog("cSobel::cSobel(): video %dx%d", videoWidth, videoHeight);
}


cSobel::~cSobel() {
}


bool cSobel::AllocAreaBuffer(sAreaT *area) const {
    // if we have no area logo size, we use max size for this resolution
    if ((area->logoSize.width == 0) || (area->logoSize.height == 0)) {
        area->logoSize = GetMaxLogoSize();
    }
    dsyslog("cSobel::AllocResultBuffer(): logo size %dx%d", area->logoSize.width, area->logoSize.height);

    // alloc memory for sobel transformed planes
    int logoPixel = area->logoSize.width * area->logoSize.height;
    area->sobel = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        area->sobel[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "area.sobel");

    // alloc memory for logo
    area->logo = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        area->logo[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "area.logo");

    // alloc memory for mask result (machtes)
    area->result = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        area->result[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "area.result");

    // alloc memory for mask inverse result
    area->inverse = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        area->inverse[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "area.inverse");

    return true;
}


bool cSobel::FreeAreaBuffer(sAreaT *area) {
    if ((area->logoSize.width <= 0) || (area->logoSize.height <= 0)) {
        esyslog("cSobel::FreeResultBuffer(): invalid logo size %d:%d", area->logoSize.width, area->logoSize.height);
        return false;
    }
    dsyslog("cSobel::FreeResultBuffer(): logo size %dx%d", area->logoSize.width, area->logoSize.height);
    // free memory for sobel plane
    if (area->sobel) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area->logoSize.width * area->logoSize.height, "area.sobel");
        for (int plane = 0; plane < PLANES; plane++) delete[] area->sobel[plane];
        delete[] area->sobel;
        area->sobel = nullptr;
    }
    // free memory for logo
    if (area->logo) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area->logoSize.width * area->logoSize.height, "area.logo");
        for (int plane = 0; plane < PLANES; plane++) delete[] area->logo[plane];
        delete[] area->logo;
        area->logo = nullptr;
    }
    // free memory for sobel result
    if (area->result) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area->logoSize.width * area->logoSize.height, "area.result");
        for (int plane = 0; plane < PLANES; plane++) delete[] area->result[plane];
        delete[] area->result;
        area->result = nullptr;
    }
    // free memory for sobel inverse result
    if (area->inverse) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area->logoSize.width * area->logoSize.height, "area.inverse");
        for (int plane = 0; plane < PLANES; plane++) delete[] area->inverse[plane];
        delete[] area->inverse;
        area->inverse = nullptr;
    }
    for (int plane = 0; plane < PLANES; plane++) {
        area->valid[plane]  = false;
        area->rPixel[plane] = 0;
        area->iPixel[plane] = 0;
    }
    return true;
}


#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
int cSobel::SobelPicture(const char *recDir, sVideoPicture *picture, sAreaT *area, const bool ignoreLogo) {
#else
int cSobel::SobelPicture(sVideoPicture *picture, sAreaT *area, const bool ignoreLogo) {
#endif

    if (!ignoreLogo && !area->valid[0]) {
        esyslog("cSobel::SobelPicture(): at least plane 0 must be valid for logo detection");
        return 0;
    }
    if ((area->logoSize.width <= 0) || (area->logoSize.height <= 0)) {
        esyslog("cSobel::SobelPicture(): logo size %dx%d invalid", area->logoSize.width, area->logoSize.height);
        return 0;
    }

    // apply sobel transformation to all planes
    int processed = 0;
    for (int plane = 0; plane < PLANES; plane++) {
        if (area->valid[plane] || ignoreLogo) {
            if (SobelPlane(picture, area, plane)) {
                processed = plane + 1;   // for logo with pixel in plane 0 and plane 2 we have to return 3 to do all valid planes in logo detection
            }
        }
    }
    return processed;
}


bool cSobel::SobelPlane(sVideoPicture *picture, sAreaT *area, const int plane) {
    if (!picture) {
        esyslog("cSobel::SobelPlane(): picture missing");
        return false;
    }
    if (!area) {
        esyslog("cSobel::SobelPlane(): area missing");
        return false;
    }
    if (!area->sobel) {
        esyslog("cSobel::SobelPlane(): sobel missing");
        return false;
    }
    if (!area->result) {
        esyslog("cSobel::SobelPlane(): result missing");
        return false;
    }
    if (!area->inverse) {
        esyslog("cSobel::SobelPlane(): inverse missing");
        return false;
    }

    // reset values
    area->rPixel[plane] = 0;
    area->iPixel[plane] = 0;

    // get logo coordinates
    int xStart = 0;
    int xEnd   = 0;
    int yStart = 0;
    int yEnd   = 0;
    if (!SetCoordinates(area, plane, &xStart, &xEnd, &yStart, &yEnd)) {
        esyslog("cSobel::SobelPlane(): unable to set coordinates");
        return false;
    }

    int cutval           = 127;
    int planeLogoWidth   = area->logoSize.width;
    int planeVideoWidth  = videoWidth;
    int planeVideoHeight = videoHeight;
    int planeBoundary    = boundary;
    if (plane > 0) {
        planeBoundary    /= 2;
        cutval           /= 2;
        planeLogoWidth   /= 2;
        planeVideoWidth  /= 2;
        planeVideoHeight /= 2;
    }
    int SUM;
    int sumX;
    int sumY;
    rPixel    = 0;
    iPixel    = 0;
    intensity = 0;

#ifdef DEBUG_SOBEL
    dsyslog("cSobel::SobelPlane(): plane %d: xStart %d, xEend %d, yStart %d, yEnd %d", plane, xStart, xEnd, yStart, yEnd);
#endif

    for (int Y = yStart; Y <= yEnd; Y++) {
        for (int X = xStart; X <= xEnd; X++) {
            if (plane == 0) {
                intensity += picture->plane[plane][X + (Y * picture->planeLineSize[plane])];
            }
            sumX = 0;
            sumY = 0;

            // image boundaries from coordinates
            if ((X < (xStart + planeBoundary))     || (X <= 0) ||
                    (X >= (xEnd - planeBoundary))  || (X >= (planeVideoWidth - 2)) ||
                    (Y < (yStart + planeBoundary)) || (Y <= 0) ||
                    (Y > (yEnd - planeBoundary))   || (Y >= (planeVideoHeight - 2))) SUM = 0;
            // convolution starts here
            else {
                // X Gradient approximation
                for (int I = -1; I <= 1; I++) {
                    for (int J = -1; J <= 1; J++) {
                        sumX = sumX + static_cast<int> ((*(picture->plane[plane] + X + I + (Y + J) * picture->planeLineSize[plane])) * GX[I + 1][J + 1]);
                    }
                }

                // Y Gradient approximation
                for (int I = -1; I <= 1; I++) {
                    for (int J = -1; J <= 1; J++) {
                        sumY = sumY+ static_cast<int> ((*(picture->plane[plane] + X + I + (Y + J) * picture->planeLineSize[plane])) * GY[I + 1][J + 1]);
                    }
                }

                // Gradient Magnitude approximation
                SUM = abs(sumX) + abs(sumY);
            }

            if (SUM >= cutval) SUM = 255;
            if (SUM <  cutval) SUM =   0;

            int val = 255 - (uchar) SUM;

            area->sobel[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = val;

            // only store results in logo coordinates range
            if (area->valid[plane]) {  // if we are called by logo search, we have no valid logo
                // area result
                area->result[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = (area->logo[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] + val) & 255;
                if (area->result[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] == 0) area->rPixel[plane]++;
                // area inverted
                area->inverse[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = ((255 - area->logo[plane][(X - xStart) + (Y - yStart) * planeLogoWidth]) + val) & 255;
                if (area->inverse[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] == 0) area->iPixel[plane]++;
            }
        }

    }
    if (plane == 0) {
        area->intensity = intensity / (area->logoSize.width * area->logoSize.height);
    }
    return true;
}


sLogoSize cSobel::GetMaxLogoSize() const {
    sLogoSize logoSizeMax;
    switch (videoWidth) {
    case 544:
        logoSizeMax.width  =  230;
        logoSizeMax.height =  130;
        break;
    case 720:
        logoSizeMax.width  =  230;
        logoSizeMax.height =  130;
        break;
    case 1280:
        logoSizeMax.width  =  400;
        logoSizeMax.height =  200;
        break;
    case 1440:
        logoSizeMax.width  =  400;
        logoSizeMax.height =  220;
        break;
    case 1920:
        logoSizeMax.width  =  440;  // changed from 400 to 440 for münchen.tv
        logoSizeMax.height =  220;
        break;
    case 3840:
        logoSizeMax.width  = 750;
        logoSizeMax.height = 250;
        break;
    default:
        esyslog("cSobel::GetMaxLogoSize() no default logo size rule for video width %d", videoWidth);
        logoSizeMax.width  =  1500;
        logoSizeMax.height =   400;
        break;
    }
    return logoSizeMax;
}


bool cSobel::SetCoordinates(sAreaT *area, const int plane, int *xstart, int *xend, int *ystart, int *yend) const {
    switch (area->logoCorner) {
    case TOP_LEFT:
        *xstart = 0;
        *xend   = area->logoSize.width  - 1;
        *ystart = 0;
        *yend   = area->logoSize.height - 1;
        break;
    case TOP_RIGHT:
        *xstart = videoWidth - area->logoSize.width;
        *xend   = videoWidth - 1;
        *ystart = 0;
        *yend   = area->logoSize.height - 1;
        break;
    case BOTTOM_LEFT:
        *xstart = 0;
        *xend   = area->logoSize.width - 1;
        *ystart = videoHeight - area->logoSize.height;
        *yend   = videoHeight - 1;
        break;
    case BOTTOM_RIGHT:
        *xstart = videoWidth  - area->logoSize.width;
        *xend   = videoWidth  - 1;
        *ystart = videoHeight - area->logoSize.height;
        *yend   = videoHeight - 1;
        break;
    default:
        esyslog("cSobel::SetCoordinates(): corner %d invalid", area->logoCorner);
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

// save sobel plane as picture
// return: true if successful
//
bool cSobel::SaveSobelPlane(const char *fileName, const uchar *picture, const int width, const int height) {
    if (!fileName) {
        esyslog("cSobel::SaveSobelPlane(): file name missing");
        return false;
    }
    if ((width == 0) || (height == 0)) {
        esyslog("SaveSobelPlane: logo width or logo height not set");
        return false;
    }

#ifdef DEBUG_SOBEL
    dsyslog("SaveSobelPlane: logo size %dx%d", width, height);
#endif

    // Open file
    FILE *pFile = fopen(fileName, "wb");
    if (pFile == nullptr) {
        esyslog("cSobel::SaveSobelPlane(): failed to open file: %s", fileName);
        return false;
    }

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", width, height);

    // Write pixel data
    if (fwrite(picture, 1, width * height, pFile)) {};

    // Close file
    fclose(pFile);
    return true;
}
