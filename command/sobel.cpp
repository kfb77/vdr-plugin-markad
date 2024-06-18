/*
 * sobel.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <math.h>

#include "sobel.h"
#include "debug.h"


cSobel::cSobel(const int videoWidthParam, const int videoHeightParam, const int logoWidthParam, const int logoHeightParam, const int boundaryParam) {

    // set video size
    videoWidth  = videoWidthParam;
    videoHeight = videoHeightParam;

    // set logo size
    if ((logoWidthParam <= 0) || (logoHeightParam <= 0)) {  // called by logo search, we do not know the logo size
        logoSize = GetMaxLogoSize();
    }
    else {
        logoSize.width  = logoWidthParam;
        logoSize.height = logoHeightParam;
    }
    boundary = boundaryParam;

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

    // alloc memory for sobel transformed planes
    int logoPixel = logoSize.width * logoSize.height;
    sobelPicture = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        sobelPicture[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "sobelPicture");

    // alloc memory for mask result (machtes)
    sobelResult = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        sobelResult[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "sobelResult");

    // alloc memory for mask invers result
    sobelInverse = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        sobelInverse[plane] = new uchar[logoPixel];
    }
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "sobelInverse");

    dsyslog("cSobel::cSobel(): video %dx%d, logo %dx%d", videoWidth, videoHeight, logoSize.width, logoSize.height);
}


cSobel::~cSobel() {
    // free memory for sobel plane
    if (sobelPicture) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * logoSize.width * logoSize.height, "sobelPicture");
        for (int plane = 0; plane < PLANES; plane++) delete[] sobelPicture[plane];
        delete[] sobelPicture;
    }
    // free memory for sobel result
    if (sobelResult) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * logoSize.width * logoSize.height, "sobelResult");
        for (int plane = 0; plane < PLANES; plane++) delete[] sobelResult[plane];
        delete[] sobelResult;
    }
    // free memory for sobel inverse result
    if (sobelInverse) {
        FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * logoSize.width * logoSize.height, "sobelInverse");
        for (int plane = 0; plane < PLANES; plane++) delete[] sobelInverse[plane];
        delete[] sobelInverse;
    }
}


bool cSobel::SobelPlane(sVideoPicture *picture, uchar **logo, const int corner, const int plane) {
    if (!picture) return false;

    // get logo coordinates
    int xStart = 0;
    int xEnd   = 0;
    int yStart = 0;
    int yEnd   = 0;
    if (!SetCoordinates(corner, plane, &xStart, &xEnd, &yStart, &yEnd)) return false;

    int cutval           = 127;
    int planeLogoWidth   = logoSize.width;
    int planeVideoWidth  = videoWidth;
    int planeVideoHeight = videoHeight;
    int planeBoundary    = boundary;
    if (plane > 0) {
        planeBoundary    /= 2;
        cutval           /= 2;
        planeLogoWidth       /= 2;
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
    dsyslog("cMarkAdLogo::SobelPlane(): plane %d: xStart %d, xEend %d, yStart %d, yEnd %d", plane, xStart, xEnd, yStart, yEnd);
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

            sobelPicture[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = val;

            // only store results in logo coordinates range
            if (logo) {  // if we are called by logo search, we have no valid logo
                // area result
                sobelResult[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = (logo[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] + val) & 255;
                if (sobelResult[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] == 0) rPixel++;
                // area inverted
                sobelInverse[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] = ((255 - logo[plane][(X - xStart) + (Y - yStart) * planeLogoWidth]) + val) & 255;
                if (sobelInverse[plane][(X - xStart) + (Y - yStart) * planeLogoWidth] == 0) iPixel++;
            }
        }

    }
    if (plane == 0) intensity /= (planeVideoHeight * planeVideoWidth);
    return true;
}


uchar **cSobel::GetSobelPlanes() {
    return sobelPicture;
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
        logoSizeMax.height =  220;  // changed from 200 to 220 (BILD_HD)
        break;
    case 1920:
        logoSizeMax.width  =  400;
        logoSizeMax.height =  220;  // changed from 210 to 220
        break;
    case 3840:
        logoSizeMax.width  = 1500;
        logoSizeMax.height =  400;
        break;
    default:
        esyslog("cSobel::GetMaxLogoSize() no default logo size rule for video width %d", videoWidth);
        logoSizeMax.width  =  1500;
        logoSizeMax.height =   400;
        break;
    }
    return logoSizeMax;
}


sLogoSize cSobel::GetLogoSize() const {
    return logoSize;
}


bool cSobel::SetCoordinates(const int corner, const int plane, int *xstart, int *xend, int *ystart, int *yend) const {
    switch (corner) {
    case TOP_LEFT:
        *xstart = 0;
        *xend   = logoSize.width  - 1;
        *ystart = 0;
        *yend   = logoSize.height - 1;
        break;
    case TOP_RIGHT:
        *xstart = videoWidth - logoSize.width;
        *xend   = videoWidth - 1;
        *ystart = 0;
        *yend   = logoSize.height - 1;
        break;
    case BOTTOM_LEFT:
        *xstart = 0;
        *xend   = logoSize.width - 1;
        *ystart = videoHeight - logoSize.height;
        *yend   = videoHeight - 1;
        break;
    case BOTTOM_RIGHT:
        *xstart = videoWidth  - logoSize.width;
        *xend   = videoWidth  - 1;
        *ystart = videoHeight - logoSize.height;
        *yend   = videoHeight - 1;
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


