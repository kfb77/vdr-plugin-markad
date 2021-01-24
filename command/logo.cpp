/*
 * logo.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <sys/stat.h>
#include <unistd.h>

#include "logo.h"

extern "C"{
    #include "debug.h"
}

// based on this idee to find the logo in a recording:
// 1. take 1000 iframes
// 2. compare each corner of the iframes with all other iframes of the same corner
// 3. take the iframe who has the most similar frame on the same corner, this hopefully should be the logo
// 4. remove the white frame from the logo
// 5. store the logo files in the recording directory for future use

// logo size limits
#define LOGO_720W_MIN_H 54      // SIXX and SUPER RTL
#define LOGO_MIN_LETTERING_H 38 // 41 for "DIE NEUEN FOLGEN" SAT_1
                                // 38 for "#wir bleiben zuhause" RTL2

extern bool abortNow;
extern int logoSearchTime_ms;


cExtractLogo::cExtractLogo(const MarkAdAspectRatio aspectRatio) {
    logoAspectRatio.Num = aspectRatio.Num;
    logoAspectRatio.Den = aspectRatio.Den;
}


cExtractLogo::~cExtractLogo() {
    for (int corner = 0; corner < CORNERS; corner++) {  // free memory of all corners
#ifdef DEBUG_MEM
        int size = logoInfoVector[corner].size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(logoInfo), "logoInfoVector");
        }
        size = logoInfoVectorPacked[corner].size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(logoInfoPacked), "logoInfoVectorPacked");
        }
#endif
        logoInfoVector[corner].clear();
        logoInfoVectorPacked[corner].clear();
    }
}


bool cExtractLogo::IsWhitePlane(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane) {
    if (!ptr_actLogoInfo) return false;
    if (logoHeight < 1) return false;
    if (logoWidth < 1) return false;
    if ((plane < 0) || (plane >= PLANES)) return false;

    int countBlack = 0;
    for (int i = 0; i < logoHeight * logoWidth; i++) {
        if (ptr_actLogoInfo->sobel[plane][i] == 0) {
            countBlack++;
            if (countBlack >= 60) return false;   // only if there are some pixel, changed from 5 to 60
        }
    }
    return true;
}


void cExtractLogo::SetLogoSize(const MarkAdContext *maContext, int *logoHeight, int *logoWidth) {
    if (!maContext) return;
    if (!logoHeight) return;
    if (!logoWidth) return;
    if (maContext->Video.Info.Width > 720){
        *logoHeight = LOGO_DEFHDHEIGHT;
        *logoWidth = LOGO_DEFHDWIDTH;
    }
    else {
        *logoHeight = LOGO_DEFHEIGHT;
        *logoWidth = LOGO_DEFWIDTH;
    }
}


// check plane 1 and calculate quote of white pictures
// return: true if only some frames have have pixels in plane 1, a channel with logo coulor change is detected
//         false if almost all frames have pixel in plane 1, this is realy a coloured logo
//
bool cExtractLogo::IsLogoColourChange(const MarkAdContext *maContext, const int corner) {
    if (!maContext) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;

    int logoHeight = 0;
    int logoWidth = 0;
    SetLogoSize(maContext, &logoHeight, &logoWidth);
    logoHeight /= 2;  // we use plane 1 to check
    logoWidth /= 2;

    int count = 0;
    int countWhite = 0;

    for (int plane = 1; plane < PLANES; plane++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            for (std::vector<logoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                count++;
                logoInfo actLogo = {};
                UnpackLogoInfo(&actLogo, &(*actLogoPacked));
                if (IsWhitePlane(&actLogo, logoHeight, logoWidth, plane)) countWhite++;
           }
        }
        if (maContext->Config->autoLogo == 2){  // use unpacked logos
            for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                count++;
                if (IsWhitePlane(&(*actLogo), logoHeight, logoWidth, plane)) countWhite++;
            }
        }
    }
    if (count > 0) {
        dsyslog("cExtractLogo::isLogoColourChange(): %d valid frames in corner %d, %d are white, ratio %d%%", count, corner, countWhite, countWhite * 100 / count);
        if ((countWhite * 100 / count) >= 40) return true;
    }
    return false;
}


bool cExtractLogo::Save(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner, const int framenumber = -1) { // framenumber >= 0: save from debug function
    if (!maContext) return false;
    if (!ptr_actLogoInfo) return false;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;
    if (!maContext->Info.ChannelName) return false;

    bool isLogoColourChange = false;
    if (framenumber < 0)  isLogoColourChange = IsLogoColourChange(maContext, corner);  // some channels have transparent or color changing logos, do not save plane > 0 in this case
    for (int plane = 0; plane < PLANES; plane++) {
        char *buf = NULL;
        int height = logoHeight;
        int width = logoWidth;
        if (plane > 0) {
            width /= 2;
            height /= 2;
        }
        if (framenumber < 0) { // no debug flag, save logo to recording directory
            int black = 0;
            for (int i = 0; i < height*width; i++) {
                if (ptr_actLogoInfo->sobel[plane][i] == 0) black++;
            }
            if (plane > 0) {
                if (black <= 140) {  // increased from 80 to 100 to 110 to 115 to 130 to 140
                    dsyslog("cExtractLogo::Save(): not enough pixel (%i) in plane %i", black, plane);
                    continue;
                }
                else dsyslog("cExtractLogo::Save(): got enough pixel (%i) in plane %i", black, plane);

                if (isLogoColourChange) {
                    dsyslog("cExtractLogo::Save(): logo is transparent or changed color, save only plane 0");
                    break;
                }
            }
            else dsyslog("cExtractLogo::Save(): %i pixel in plane %i", black, plane);

            if (asprintf(&buf, "%s/%s-A%i_%i-P%i.pgm", maContext->Config->recDir, maContext->Info.ChannelName, logoAspectRatio.Num, logoAspectRatio.Den, plane)==-1) return false;
            ALLOC(strlen(buf)+1, "buf");
            dsyslog("cExtractLogo::Save(): store logo in %s", buf);
            isyslog("Logo size for Channel: %s %d:%d %dW %dH: %dW %dH %s", maContext->Info.ChannelName, logoAspectRatio.Num, logoAspectRatio.Den, maContext->Video.Info.Width, maContext->Video.Info.Height, logoWidth, logoHeight, aCorner[corner]);
        }
        else {  // debug function, store logo to /tmp
            if (asprintf(&buf, "%s/%06d-%s-A%i_%i-P%i.pgm", "/tmp/",framenumber, maContext->Info.ChannelName, logoAspectRatio.Num, logoAspectRatio.Den, plane) == -1) return false;
            ALLOC(strlen(buf)+1, "buf");
        }
        // Open file
        FILE *pFile=fopen(buf, "wb");
        if (pFile==NULL)
        {
            FREE(sizeof(buf), "buf");
            free(buf);
            dsyslog("cExtractLogo::Save(): open file failed");
            return false;
        }
       // Write header
        fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", corner, width, height);

        // Write pixel data
        if (!fwrite(ptr_actLogoInfo->sobel[plane], 1, width * height, pFile)) {
            dsyslog("cExtractLogo::Save(): write data failed");
            fclose(pFile);
            FREE(sizeof(buf), "buf");
            free(buf);
            return false;
        }
        // Close file
        fclose(pFile);
        FREE(strlen(buf)+1, "buf");
        free(buf);
    }
    return true;
}


void cExtractLogo::CutOut(logoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, const int corner) {
    if (!logoInfo) return;
    if (!logoHeight) return;
    if (!logoWidth) return;
    if ((corner < 0) || (corner >= CORNERS)) return;
// plane 0
    if (cutPixelH % 2) cutPixelH--;
    if (cutPixelV % 2) cutPixelV--;

    int heightPlane_1_2 = *logoHeight / 2;
    int widthPlane_1_2 = *logoWidth / 2;

    if (cutPixelH > 0) {
        dsyslog("cExtractLogo::CutOut(): cut out %3dp lines horizontal and %3dp column vertical", cutPixelH, cutPixelV);
        if (corner <= TOP_RIGHT) {  // top corners, cut from below
        }
        else { // bottom corners, cut from above
            for (int i = 0; i < (*logoHeight - cutPixelH) * *logoWidth; i++) {
                logoInfo->sobel[0][i] = logoInfo->sobel[0][i + cutPixelH * (*logoWidth)];
            }
        }
        *logoHeight -= cutPixelH;
    }

    if (cutPixelV > 0) {
        dsyslog("cExtractLogo::CutOut(): cut out %3dp lines horizontal and %3dp column vertical", cutPixelH, cutPixelV);
        if ((corner == TOP_RIGHT) || (corner == BOTTOM_RIGHT)) {  // right corners, cut from left
            for (int i = 0; i < *logoHeight * (*logoWidth - cutPixelV); i++) {
                logoInfo->sobel[0][i] =  logoInfo->sobel[0][i + cutPixelV * (1 + (i / (*logoWidth - cutPixelV)))];
            }
        }
        else { // left corners, cut from right
            for (int i = 0; i < *logoHeight * (*logoWidth - cutPixelV); i++) {
                logoInfo->sobel[0][i] =  logoInfo->sobel[0][i + cutPixelV * (i / (*logoWidth - cutPixelV))];
            }
        }
        *logoWidth -= cutPixelV;
    }

// resize plane 1 and 2
    cutPixelH /= 2;
    cutPixelV /= 2;
    for (int plane = 1; plane < PLANES; plane++) {
        if (cutPixelH > 0) {
            if (corner <= TOP_RIGHT) { // top corners, cut from below
            }
            else { // bottom corners, cut from above
                for (int i = 0; i < (heightPlane_1_2 - cutPixelH) * widthPlane_1_2; i++) {
                    logoInfo->sobel[plane][i] =  logoInfo->sobel[plane][i + cutPixelH * widthPlane_1_2];
                }
            }
        }
        if (cutPixelV > 0) {
            if ((corner == TOP_RIGHT) || (corner == BOTTOM_RIGHT)) {  // right corners, cut from left
                for (int i = 0; i < heightPlane_1_2 * (widthPlane_1_2 - cutPixelV); i++) {
                    logoInfo->sobel[plane][i] =  logoInfo->sobel[plane][i + cutPixelV * (1 + (i / (widthPlane_1_2 - cutPixelV)))];
                }
            }
            else { // left corners, cut from right
                for (int i = 0; i < heightPlane_1_2 * (widthPlane_1_2 - cutPixelV); i++) {
                    logoInfo->sobel[plane][i] =  logoInfo->sobel[plane][i + cutPixelV * (i / (widthPlane_1_2 - cutPixelV))];
                }
            }
        }
    }
    dsyslog("cExtractLogo::CutOut(): logo size after cut out:    %3d width %3d height on corner %12s", *logoWidth, *logoHeight, aCorner[corner]);
}


bool cExtractLogo::CheckLogoSize(const MarkAdContext *maContext, const int logoHeight, const int logoWidth, const int corner) {
// check special channels and special logos
    if (strcmp(maContext->Info.ChannelName, "DMAX") == 0) {
        if (logoWidth < 126) {  // DMAX logo is 126 pixel wide
            dsyslog("cExtractLogo::CheckLogoSize(): DMAX logo to narrow, this is possibly NEUE FOLGE");
            return false;
        }
    }
    if (strcmp(maContext->Info.ChannelName, "SUPER_RTL") == 0) {
        if ((logoWidth == 160) && (logoHeight == 54) && (corner >= TOP_LEFT)) {  // SUPER RTL PRIMETIME logo
            dsyslog("cExtractLogo::CheckLogoSize(): SUPER RTL PRIMETIME special logo detected");
            return true;
        }
    }

// check other logo sizes
    switch (maContext->Video.Info.Width) {
        case 720:
            // check logo width
            if ((corner >= BOTTOM_LEFT) && (logoHeight >= 60) && (logoHeight <= 65) && (logoWidth >= 185)) { // if logo size is low and wide on BOTTON, it is in a news ticker
            dsyslog("cExtractLogo::CheckLogoSize(): found SD logo in a news ticker");
            }
            else {  // logo on TOP
                if (strcmp(maContext->Info.ChannelName, "RTLplus") == 0) {
                    if (logoWidth > 212) { // RTLplus
                        dsyslog("cExtractLogo::CheckLogoSize(): SD logo for RTLPlus is too wide");
                        return false;
                    }
                }
                else {
                    if (logoWidth >= 150) {
                        dsyslog("cExtractLogo::CheckLogoSize(): SD logo is too wide");
                        return false;
                    }
                }
            }

            // check logo height
            if (strcmp(maContext->Info.ChannelName, "SIXX") == 0) {
                if (logoHeight < LOGO_720W_MIN_H) {
                    dsyslog("cExtractLogo::CheckLogoSize(): SD logo is not heigh enough");
                    return false;
                }
            }
            else {
                if (logoHeight < LOGO_720W_MIN_H + 3) { // increased from 2 to 3
                    dsyslog("cExtractLogo::CheckLogoSize(): SD logo is not heigh enough");
                    return false;
                }
            }

            if (strcmp(maContext->Info.ChannelName, "Welt_der_Wunder") == 0) {
                if (logoHeight > 112) { // Welt der Wunder
                    dsyslog("cExtractLogo::CheckLogoSize(): SD logo is too heigh");
                    return false;
                }
            }
            else {
                if (logoHeight > 88) {  // NICK 88H
                    dsyslog("cExtractLogo::CheckLogoSize(): SD logo is too heigh");
                    return false;
                }
            }
            break;
        case 1280:
            if (maContext->Video.Info.Height == 1080) {  // ANIXE+
            }
            else {  // 720W
                if (logoWidth >= 256) {
                    dsyslog("cExtractLogo::CheckLogoSize(): HD logo is too wide");
                    return false;
                }
                if (logoHeight >= 134) { // logo is vertial or too high
                    if (strcmp(maContext->Info.ChannelName, "arte_HD") == 0) {  // vertical logo
                        if (logoWidth < 84) { // arte HD
                            dsyslog("cExtractLogo::CheckLogoSize(): vertical HD logo is too narrow");
                            return false;
                        }
                    }
                    else return false;
                }
                else { // logo is horizontal
                    if (logoWidth <= 116) {
                        dsyslog("cExtractLogo::CheckLogoSize(): HD logo is too narrow");
                        return false;
                    }
                }
            }
            break;
        case 1920:
            if (strcmp(maContext->Info.ChannelName, "münchen_tv_HD") == 0) {
                if (logoHeight < 96) { // münchen_tv_HD
                    dsyslog("cExtractLogo::CheckLogoSize(): HD logo for münchen_tv_HD is not heigh enough");
                    return false;
                }
            }
            else {
                if (logoHeight <= 106) {
                    dsyslog("cExtractLogo::CheckLogoSize(): HD logo is not heigh enough");
                    return false;
                }
            }
            break;
        default:
            dsyslog("cExtractLogo::CheckLogoSize(): no logo size rules for %dx%d", maContext->Video.Info.Width, maContext->Video.Info.Height);
            break;
    }
    return true;
}


bool cExtractLogo::Resize(const MarkAdContext *maContext, logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner) {
    if (!maContext) return false;
    if (!bestLogoInfo) return false;
    if (!logoHeight) return false;
    if (!logoWidth) return false;
    if ((bestLogoCorner < 0) || (bestLogoCorner >= CORNERS)) return false;

    dsyslog("cExtractLogo::Resize(): logo size before resize:    %3d width %3d height on corner %12s", *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
    int logoHeightBeforeResize = *logoHeight;
    int logoWidthBeforeResize = *logoWidth;
    int acceptFalsePixelH = *logoWidth / 30;  // reduced from 60 to 20, increased to 30 for vertical logo of arte HD
    int acceptFalsePixelV = *logoHeight / 20; // reduced from 30 to 20

    for (int repeat = 1; repeat <= 2; repeat++) {
        if ((*logoWidth <= 0) || (*logoHeight <= 0)) {
            dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", maContext->Video.Info.Width, maContext->Video.Info.Height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
            *logoHeight = logoHeightBeforeResize; // restore logo size
            *logoWidth = logoWidthBeforeResize;
            return false;
        }

// resize plane 0
        if (bestLogoCorner <= TOP_RIGHT) {  // top corners, calculate new height and cut from below
            int whiteLines = 0;
            for (int line = *logoHeight - 1; line > 0; line--) {
                int blackPixel = 0;
                for (int column = 0; column < *logoWidth; column++) {
                    if ( bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelH) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelH) {  // accept false pixel
                    whiteLines++;
                }
                else break;
            }
            CutOut(bestLogoInfo, whiteLines, 0, logoHeight, logoWidth, bestLogoCorner);
// search for text under logo
// search for at least 2 (SD) or 4 (HD) white lines to cut logos with text addon (e.g. "Neue Folge" or "Live")
            int countWhite = 0;
            int cutLine = 0;
            int topBlackLineOfLogo= 0;
            int leftBlackPixel = INT_MAX;
            int rightBlackPixel = 0;
            int minWhiteLines;
            if ((maContext->Video.Info.Width) == 720) minWhiteLines = 2;
            else minWhiteLines = 4;
            for (int line = *logoHeight - 1; line > 0; line--) {
                int countBlackPixel = 0;
                for (int column = 0; column < *logoWidth; column++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        countBlackPixel++;
                        if (column < leftBlackPixel) leftBlackPixel = column;
                        if (column > rightBlackPixel) rightBlackPixel = column;
                    }
                }
                if (countBlackPixel <= 1) {  // accept 1 false pixel
                    countWhite++;
                }
                else {
                    countWhite = 0;
                    topBlackLineOfLogo = line;
                    if (cutLine > 0) break;
                }
                if (countWhite >= minWhiteLines) {
                    cutLine = line;
                }
            }
            if (topBlackLineOfLogo < cutLine) {
                if (cutLine >= LOGO_MIN_LETTERING_H) {
                    if ((((rightBlackPixel - leftBlackPixel) >= 38) && ((*logoHeight - cutLine) > 8)) || // cut our "love your" from TLC with 38 pixel width, do not cut out lines in the logo
                       (((rightBlackPixel - leftBlackPixel) <= 20) && ((*logoHeight - cutLine) <= 8))) { // cut out small pixel errors
                        dsyslog("cExtractLogo::Resize(): found text under logo, cut at line %d, size %dWx%dH, pixel before: left %d right %d, width is valid", cutLine, rightBlackPixel - leftBlackPixel, *logoHeight - cutLine, leftBlackPixel, rightBlackPixel);
                        CutOut(bestLogoInfo, *logoHeight - cutLine, 0, logoHeight, logoWidth, bestLogoCorner);
                    }
                    else dsyslog("cExtractLogo::Resize(): found text under logo, cut at line %d, size %dWx%dH, pixel before: left %d right %d, width is invalid", cutLine, rightBlackPixel - leftBlackPixel, *logoHeight - cutLine, leftBlackPixel, rightBlackPixel);
                }
                else dsyslog("cExtractLogo::Resize(): cutline at %d not valid", cutLine);
            }
        }
        else { // bottom corners, calculate new height and cut from above
            int whiteLines = 0;
            for (int line = 0; line < *logoHeight; line++) {
                int blackPixel = 0;
                for (int column = 0; column < *logoWidth; column++) {
                    if ( bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelH) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelH) {  // accept false pixel
                    whiteLines++;
                }
                else break;
            }
            CutOut(bestLogoInfo, whiteLines, 0, logoHeight, logoWidth, bestLogoCorner);
// search for text above logo
// search for at least 3 white lines to cut logos with text addon (e.g. "Neue Folge" or "Live")
            int countWhite = 0;
            int cutLine = 0;
            int topBlackLineOfLogo = 0;
            int leftBlackPixel = INT_MAX;
            int rightBlackPixel = 0;
            int minWhiteLines;
            if ((maContext->Video.Info.Width) == 720) minWhiteLines = 2;
            else minWhiteLines = 4;
            for (int line = 0; line < *logoHeight; line++) {
                int countBlackPixel = 0;
                for (int column = *logoWidth - 1; column > 0; column--) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        countBlackPixel++;
                        if (column < leftBlackPixel) leftBlackPixel = column;
                        if (column > rightBlackPixel) rightBlackPixel = column;
                    }
                }
                if (countBlackPixel <= 1) {  // accept 1 false pixel
                    countWhite++;
                }
                else {
                    countWhite = 0;
                    topBlackLineOfLogo = line;
                    if (cutLine > 0) break;
                }
                if (countWhite >= minWhiteLines) {
                    cutLine = line;
                }
            }
            if (topBlackLineOfLogo > cutLine) {
                if (cutLine >= LOGO_MIN_LETTERING_H) {
                    dsyslog("cExtractLogo::Resize(): found text above logo, cut at line %d, size %dWx%dH, pixel before: left %d right %d, width is valid", cutLine, rightBlackPixel - leftBlackPixel, cutLine, leftBlackPixel, rightBlackPixel);
                    CutOut(bestLogoInfo, cutLine, 0, logoHeight, logoWidth, bestLogoCorner);
                }
                else dsyslog("cExtractLogo::Resize(): cutline at %d not valid (expect min %d)", cutLine, LOGO_MIN_LETTERING_H);
            }
        }

        if ((bestLogoCorner == TOP_RIGHT) || (bestLogoCorner == BOTTOM_RIGHT)) {  // right corners, cut from left
            int whiteColumns = 0;
            for (int column = 0; column < *logoWidth; column++) {
                int blackPixel = 0;
                for (int line = 0; line < *logoHeight; line++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelV) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelV ) {  // accept false pixel
                    whiteColumns++;
                }
                else break;
            }
            CutOut(bestLogoInfo, 0, whiteColumns, logoHeight, logoWidth, bestLogoCorner);
// search for at least 3 white columns to cut logos with text addon (e.g. "Neue Folge")
            int countWhite = 0;
            int cutColumn = 0;
            int lastBlackColumn = 0;
            int topBlackPixel =  INT_MAX;
            int topBlackPixelBefore = INT_MAX;
            int bottomBlackPixelBefore = 0;
            int bottomBlackPixel = 0;
            for (int column = 0; column < *logoWidth; column++) {
                bool isAllWhite = true;
                topBlackPixel = topBlackPixelBefore;
                bottomBlackPixel = bottomBlackPixelBefore;
                for (int line = 0; line < *logoHeight; line++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        isAllWhite = false;
                        if (line < topBlackPixelBefore) topBlackPixelBefore = line;
                        if (line > bottomBlackPixelBefore) bottomBlackPixelBefore = line;
                    }
                }
                if (isAllWhite) {
                    countWhite++;
                }
                else {
                    countWhite = 0;
                    lastBlackColumn = column;
                    if (cutColumn > 0) break;
                }
                if (countWhite >= 3) {
                    cutColumn = column;
                }
            }
            if (lastBlackColumn > cutColumn) {
                if ((bottomBlackPixel - topBlackPixel) <= 13) {
                    dsyslog("cExtractLogo::Resize(): found text before logo, cut at column %d, pixel of text: top %d bottom %d, text height %d is valid", cutColumn, topBlackPixel, bottomBlackPixel, bottomBlackPixel - topBlackPixel);
                    CutOut(bestLogoInfo, 0, cutColumn, logoHeight, logoWidth, bestLogoCorner);
                }
                else dsyslog("cExtractLogo::Resize(): found text before logo, cut at column %d, pixel test: top %d bottom %d, text height %d is not valid", cutColumn, topBlackPixel, bottomBlackPixel, bottomBlackPixel - topBlackPixel);
            }
        }
        else { // left corners, cut from right
            int whiteColumns = 0;
            for (int column = *logoWidth - 1; column > 0; column--) {
                int blackPixel = 0;
                for (int line = 0; line < *logoHeight; line++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelV) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelV ) {  // accept false pixel
                    whiteColumns++;
                }
                else break;
            }
            CutOut(bestLogoInfo, 0, whiteColumns, logoHeight, logoWidth, bestLogoCorner);
        }
        dsyslog("cExtractLogo::Resize(): logo size after %d. resize:  %3d width %3d height on corner %12s", repeat, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
    }
    if (CheckLogoSize(maContext, *logoHeight, *logoWidth, bestLogoCorner)) {
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is valid", maContext->Video.Info.Width, maContext->Video.Info.Height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
        return true;
    }
    else {
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", maContext->Video.Info.Width, maContext->Video.Info.Height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
        *logoHeight = logoHeightBeforeResize; // restore logo size
        *logoWidth = logoWidthBeforeResize;
        return false;
    }
}


// check if extracted picture from the corner could be a logo
// used before picture is stored in logo cantidates list
// return: true if valid
//
bool cExtractLogo::CheckValid(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
#define WHITEHORIZONTAL_BIG 10
#define WHITEHORIZONTAL_SMALL 7 // reduced from 8 to 7
#define WHITEVERTICAL_BIG 10
    if ((logoHeight <= 0) || (logoWidth <= 0)) return 0;
    if ((corner < 0) || (corner >= CORNERS)) return 0;
    if (!ptr_actLogoInfo) return 0;
    if (!ptr_actLogoInfo->valid) {
        dsyslog("cExtractLogo::CheckValid(): invalid logo data at frame %i", ptr_actLogoInfo->iFrameNumber);
        return false;
    }
    if (corner <= TOP_RIGHT) {
        for (int i = 0 ; i < WHITEHORIZONTAL_BIG * logoWidth; i++) { // a valid top logo should have a white top part
            if ((ptr_actLogoInfo->sobel[0][i] == 0) ||
               ((i < WHITEHORIZONTAL_BIG * logoWidth / 4) && ((ptr_actLogoInfo->sobel[1][i] == 0) || (ptr_actLogoInfo->sobel[2][i] == 0)))) {
#ifdef DEBUG_LOGO_CORNER
                if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no big white top part at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }
        if ((corner != TOP_RIGHT) || (strcmp(maContext->Info.ChannelName, "SPORT1") != 0)) { // this channels have sometimes a big preview text below the logo on the right side
                                                                                             // more general solution will be: make the possible logo size bigger
                                                                                             // but this wll have a performence impact
            for (int i = (logoHeight - WHITEHORIZONTAL_SMALL) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid top logo should have at least a small white bottom part in plane 0
                if (ptr_actLogoInfo->sobel[0][i] == 0) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no small white bottom part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                    return false;
                }
            }
        }

    }
    else {
        for (int i = (logoHeight - WHITEHORIZONTAL_BIG) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid bottom logo should have a white bottom part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                 if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no big white bottom part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }
        for (int i = 0 ; i < WHITEHORIZONTAL_SMALL * logoWidth; i++) { // a valid bottom logo should have at least a small white top part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                 if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no small white top part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }

    }

    if ((corner == TOP_LEFT) || (corner == BOTTOM_LEFT)) { // a valid left logo should have white left part in pane 0
        for (int column = 0; column <= WHITEVERTICAL_BIG; column++) {
            for (int i = column; i < logoHeight * logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                     if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no big white left part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                    return false;
                }
            }
        }
    }
    else { // a valid right logo should have white right part in pane 0
        for (int column = 0; column <= WHITEVERTICAL_BIG; column++) {
            for (int i = logoWidth - column; i < logoHeight * logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no big white right part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                    return false;
                }
            }
        }
    }

    int blackPixel1 = 0;
    for (int i = 0; i < logoHeight * logoWidth; i++) {
        if (ptr_actLogoInfo->sobel[0][i] == 0) blackPixel1++;
    }
    if (blackPixel1 < 300) {
#ifdef DEBUG_LOGO_CORNER
        if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo has no enough pixel %d plane 0 at frame %i", blackPixel1, ptr_actLogoInfo->iFrameNumber);
#endif
        return false;
    }
#ifdef DEBUG_LOGO_CORNER
    if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s plane 0 is valid at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
    return true;
}


int cExtractLogo::Compare(const MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!maContext) return 0;
    if (!ptr_actLogoInfo) return 0;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return 0;
    if ((corner < 0) || (corner >= CORNERS)) return 0;
    if (!ptr_actLogoInfo->valid) {
        dsyslog("cExtractLogo::Compare(): invalid logo data at frame %i", ptr_actLogoInfo->iFrameNumber);
        return 0;
    }
    int hits=0;

    if (maContext->Config->autoLogo == 1) { // use packed logos
        for (std::vector<logoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
            logoInfo actLogo = {};
            UnpackLogoInfo(&actLogo, &(*actLogoPacked));
            if (maContext->Info.rotatingLogo) {
                if (CompareLogoPairRotating(&actLogo, ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                    hits++;
                    actLogoPacked->hits++;
                }
            }
            else {
                if (CompareLogoPair(&actLogo, ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                    hits++;
                    actLogoPacked->hits++;
                }
            }
        }
    }
    if (maContext->Config->autoLogo == 2){  // use unpacked logos
        for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            if (maContext->Info.rotatingLogo) {
                if (CompareLogoPairRotating(&(*actLogo), ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                    hits++;
                    actLogo->hits++;
                }
            }
            else {
                if (CompareLogoPair(&(*actLogo), ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                    hits++;
                    actLogo->hits++;
                }
            }
        }
    }
    return hits;
}


bool cExtractLogo::CompareLogoPairRotating(logoInfo *logo1, logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner) {
    if (!logo1) return false;
    if (!logo2) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;
// TODO do not hardcode the logo range
#define LOGO_START_LINE 18 // change from 30 to 18 for santa claus hat
#define LOGO_END_LINE 75
#define LOGO_START_COLUMN 143 // change from 150 to 143 for santa claus hat
#define LOGO_END_COLUMN 185
// check if pixel in both frames are only in the corner but let the pixel be different
    if (corner != TOP_RIGHT) return false; // to optimze performance, only check TOP_RIGHT (SAT.1)
// we use only logo with pixel in the expected logo range
    for (int line = 0; line < logoHeight; line++) {
        for (int column = 0; column < logoWidth; column++) {
            if ((line >= LOGO_START_LINE) && (line < LOGO_END_LINE) && (column >= LOGO_START_COLUMN) && (column < LOGO_END_COLUMN)) continue;
            if (logo1->sobel[0][line * logoWidth + column] == 0) {
#ifdef DEBUG_LOGO_CORNER
                dsyslog("cExtractLogo::CompareLogoPairRotating(): frame logo1 (%5i) pixel out of valid range: line %3i column %3i", logo1->iFrameNumber, line, column);
#endif
                return false;
            }
            if (logo2->sobel[0][line * logoWidth + column] == 0) {
#ifdef DEBUG_LOGO_CORNER
                dsyslog("cExtractLogo::CompareLogoPairRotating(): frame logo2 (%5i) pixel out of valid range: line %3i column %3i", logo2->iFrameNumber, line, column);
#endif
                return false;
            }
        }
    }
#ifdef DEBUG_LOGO_CORNER
    dsyslog("cExtractLogo::CompareLogoPairRotating(): frame logo1 (%5i) valid", logo1->iFrameNumber);
    dsyslog("cExtractLogo::CompareLogoPairRotating(): frame logo2 (%5i) valid", logo2->iFrameNumber);
#endif
// merge pixel in logo range
    for (int line = LOGO_START_LINE; line <= LOGO_END_LINE; line++) {
        for (int column = LOGO_START_COLUMN; column <= LOGO_END_COLUMN; column++) {
            logo1->sobel[0][line * logoWidth + column] &= logo2->sobel[0][line * logoWidth + column];
            logo2->sobel[0][line * logoWidth + column] &= logo1->sobel[0][line * logoWidth + column];
        }
    }
    return true;
}


bool cExtractLogo::CompareLogoPair(const logoInfo *logo1, const logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner) {
    if (!logo1) return false;
    if (!logo2) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;

    int similar_0 = 0;
    int similar_1_2 = 0;
    int oneBlack_0 = 0;
    int rate_0 = 0;
    int rate_1_2 = 0;
    for (int i = 0; i < logoHeight*logoWidth; i++) {    // compare all black pixel in plane 0
        if ((logo1->sobel[0][i] == 255) && (logo2->sobel[0][i] == 255)) continue;   // ignore white pixel
        else oneBlack_0 ++;
        if (logo1->sobel[0][i] == logo2->sobel[0][i]) {
            similar_0++;
        }
    }
    for (int i = 0; i < logoHeight / 2 * logoWidth / 2; i++) {    // compare all pixel in plane 1 and 2
        for (int plane = 1; plane < PLANES; plane ++) {
            if (logo1->sobel[plane][i] == logo2->sobel[plane][i]) similar_1_2++;
        }
    }
    if (oneBlack_0 > 100) rate_0 = 1000 * similar_0 / oneBlack_0;   // accept only if we found some pixels
    else rate_0 = 0;
    rate_1_2 = 1000 * similar_1_2 / (logoHeight * logoWidth) * 2;

#define MINMATCH_0 800  // reduced from 890 to 870 to 860 to 800
#define MINMATCH_1_2 980  // reduced from 985 to 980
    if ((rate_0 > MINMATCH_0) && (rate_1_2 > MINMATCH_1_2)) { // reduced from 890 to 870
#ifdef DEBUG_LOGO_CORNER
        if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ++++ frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d)", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_LOGO_CORNER
if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ---- frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d) ", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);
#endif
    return false;
}


int cExtractLogo::DeleteFrames(const MarkAdContext *maContext, const int from, const int to) {
    if (!maContext) return 0;
    if (from >= to) return 0;
    int deleteCount = 0;
    dsyslog("cExtractLogo::DeleteFrames(): delete frames from %d to %d", from, to);
    for (int corner = 0; corner < CORNERS; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            for (std::vector<logoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                if (abortNow) return 0;
                if (actLogoPacked->iFrameNumber < from) continue;
                if (actLogoPacked->iFrameNumber <= to) {
                    FREE(sizeof(*actLogoPacked), "logoInfoVectorPacked");
                    logoInfoVectorPacked[corner].erase(actLogoPacked--);  // "erase" increments the iterator, "for" also does, that is 1 to much
                    deleteCount++;
                }
                else break;
            }
        }
        if (maContext->Config->autoLogo == 2) {  // use unpacked logos
            for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                if (abortNow) return 0;
                if (actLogo->iFrameNumber < from) continue;
                if (actLogo->iFrameNumber <= to) {
                    FREE(sizeof(*actLogo), "logoInfoVector");
                    logoInfoVector[corner].erase(actLogo--);    // "erase" increments the iterator, "for" also does, that is 1 to much
                    deleteCount++;
                }
                else break;
            }
        }
   }
   return deleteCount/4;  // 4 corner
}


int cExtractLogo::GetFirstFrame(const MarkAdContext *maContext) {
    if (!maContext) return 0;

    int firstFrame = INT_MAX;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            if (!logoInfoVectorPacked[corner].empty() && logoInfoVectorPacked[corner].front().iFrameNumber < firstFrame) firstFrame = logoInfoVectorPacked[corner].front().iFrameNumber;
        }
        if (maContext->Config->autoLogo == 2) {  // use unpacked logos
            if (!logoInfoVector[corner].empty() && logoInfoVector[corner].front().iFrameNumber < firstFrame) firstFrame = logoInfoVector[corner].front().iFrameNumber;
        }
    }
    return firstFrame;
}


int cExtractLogo::GetLastFrame(const MarkAdContext *maContext) {
    if (!maContext) return 0;

    int lastFrame = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            if (!logoInfoVectorPacked[corner].empty() && logoInfoVectorPacked[corner].back().iFrameNumber > lastFrame) lastFrame = logoInfoVectorPacked[corner].back().iFrameNumber;
        }
        if (maContext->Config->autoLogo == 2) {  // use unpacked logos
            if (!logoInfoVector[corner].empty() && logoInfoVector[corner].back().iFrameNumber > lastFrame) lastFrame = logoInfoVector[corner].back().iFrameNumber;
        }
    }
    return lastFrame;
}


int cExtractLogo::CountFrames(const MarkAdContext *maContext) {
    if (!maContext) return 0;

    long unsigned int count = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            if (!logoInfoVectorPacked[corner].empty() && logoInfoVectorPacked[corner].size() > count) count = logoInfoVectorPacked[corner].size();
        }
        if (maContext->Config->autoLogo == 2) {  // use unpacked logos
            if (!logoInfoVector[corner].empty() && logoInfoVector[corner].size() > count) count = logoInfoVector[corner].size();
        }
    }
    return count;
}


bool cExtractLogo::WaitForFrames(const MarkAdContext *maContext, cDecoder *ptr_cDecoder, const int minFrame = 0) {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;

    if ((recordingFrameCount > (ptr_cDecoder->GetFrameNumber() + 200)) && (recordingFrameCount > minFrame)) return true; // we have already found enough frames

#define WAITTIME 60
    char *indexFile = NULL;
    if (asprintf(&indexFile, "%s/index", maContext->Config->recDir) == -1) {
        dsyslog("cExtractLogo::WaitForFrames: out of memory in asprintf");
        return false;
    }
    ALLOC(strlen(indexFile)+1, "indexFile");

    bool ret = false;
    struct stat indexStatus;
    for (int retry = 0; retry < 10; retry++) {
        if (stat(indexFile,&indexStatus) == -1) {
            dsyslog("cExtractLogo::WaitForFrames: failed to stat %s", indexFile);
            ret = false;
            break;
        }
        recordingFrameCount = indexStatus.st_size / 8;
        dsyslog("cExtractLogo::WaitForFrames(): frames recorded (%d) read frames (%d) minFrame (%d)", recordingFrameCount, ptr_cDecoder->GetFrameNumber(), minFrame);
        if ((recordingFrameCount > (ptr_cDecoder->GetFrameNumber() + 200)) && (recordingFrameCount > minFrame)) {
            ret = true;  // recording has enough frames
            break;
        }
        time_t now = time(NULL);
        char systemTime[50] = {0};
        char indexTime[50] = {0};
        strftime(systemTime, sizeof(systemTime), "%d-%m-%Y %H:%M:%S", localtime(&now));
        strftime(indexTime, sizeof(indexTime), "%d-%m-%Y %H:%M:%S", localtime(&indexStatus.st_mtime));
        dsyslog("cExtractLogo::WaitForFrames(): index file size %ld bytes, system time %s index time %s, wait %ds", indexStatus.st_size, systemTime, indexTime, WAITTIME);
        if ((difftime(now, indexStatus.st_mtime)) >= 2 * WAITTIME) {
            dsyslog("cExtractLogo::isRunningRecording(): index not growing at frame (%d), old or interrupted recording", ptr_cDecoder->GetFrameNumber());
            ret = false;
            break;
        }
        sleep(WAITTIME); // now we sleep and hopefully the index will grow
    }
    FREE(strlen(indexFile)+1, "indexFile");
    free(indexFile);
    return ret;
}


void cExtractLogo::PackLogoInfo(const logoInfo *logoInfo, logoInfoPacked *logoInfoPacked) {
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;

    logoInfoPacked->iFrameNumber = logoInfo->iFrameNumber;
    logoInfoPacked->hits = logoInfo->hits;
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfoPacked->valid[plane] = logoInfo->valid[plane];
        for (int i = 0; i < MAXPIXEL / 8; i++) {
            logoInfoPacked->sobel[plane][i] = 0;
            if (logoInfo->sobel[plane][i*8+0] > 0) logoInfoPacked->sobel[plane][i] += 1;
            if (logoInfo->sobel[plane][i*8+1] > 0) logoInfoPacked->sobel[plane][i] += 2;
            if (logoInfo->sobel[plane][i*8+2] > 0) logoInfoPacked->sobel[plane][i] += 4;
            if (logoInfo->sobel[plane][i*8+3] > 0) logoInfoPacked->sobel[plane][i] += 8;
            if (logoInfo->sobel[plane][i*8+4] > 0) logoInfoPacked->sobel[plane][i] += 16;
            if (logoInfo->sobel[plane][i*8+5] > 0) logoInfoPacked->sobel[plane][i] += 32;
            if (logoInfo->sobel[plane][i*8+6] > 0) logoInfoPacked->sobel[plane][i] += 64;
            if (logoInfo->sobel[plane][i*8+7] > 0) logoInfoPacked->sobel[plane][i] += 128;
        }
    }
}


void cExtractLogo::UnpackLogoInfo(logoInfo *logoInfo, const logoInfoPacked *logoInfoPacked) {
    if (!logoInfo) return;
    if (!logoInfoPacked) return;

    logoInfo->iFrameNumber=logoInfoPacked->iFrameNumber;
    logoInfo->hits=logoInfoPacked->hits;
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfo->valid[plane] = logoInfoPacked->valid[plane];
        for (int i = 0; i < MAXPIXEL / 8; i++) {
            if (logoInfoPacked->sobel[plane][i] & 1) logoInfo->sobel[plane][i*8+0] = 255; else logoInfo->sobel[plane][i*8+0] = 0;
            if (logoInfoPacked->sobel[plane][i] & 2) logoInfo->sobel[plane][i*8+1] = 255; else logoInfo->sobel[plane][i*8+1] = 0;
            if (logoInfoPacked->sobel[plane][i] & 4) logoInfo->sobel[plane][i*8+2] = 255; else logoInfo->sobel[plane][i*8+2] = 0;
            if (logoInfoPacked->sobel[plane][i] & 8) logoInfo->sobel[plane][i*8+3] = 255; else logoInfo->sobel[plane][i*8+3] = 0;
            if (logoInfoPacked->sobel[plane][i] & 16) logoInfo->sobel[plane][i*8+4] = 255; else logoInfo->sobel[plane][i*8+4] = 0;
            if (logoInfoPacked->sobel[plane][i] & 32) logoInfo->sobel[plane][i*8+5] = 255; else logoInfo->sobel[plane][i*8+5] = 0;
            if (logoInfoPacked->sobel[plane][i] & 64) logoInfo->sobel[plane][i*8+6] = 255; else logoInfo->sobel[plane][i*8+6] = 0;
            if (logoInfoPacked->sobel[plane][i] & 128) logoInfo->sobel[plane][i*8+7] = 255; else logoInfo->sobel[plane][i*8+7] = 0;
        }
    }
}


void cExtractLogo::RemovePixelDefects(const MarkAdContext *maContext, logoInfo *logoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!maContext) return;
    if (!logoInfo) return;
    if ((corner < 0) || (corner >= CORNERS)) return;

#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 1
    Save(maContext, logoInfo, logoHeight, logoWidth, corner, logoInfo->iFrameNumber);
#endif

    for (int plane = 0; plane < PLANES; plane++) {
        int height;
        int width;
        if (plane == 0) {
            height = logoHeight;
            width = logoWidth;
        }
        else {
            height = logoHeight / 2;
            width = logoWidth / 2;
        }
        for (int line = height - 1; line >= 0; line--) {
            for (int column = 0; column < width; column++) {
                if ( logoInfo->sobel[plane][line * width + column] == 0) {  // remove single separate pixel
                    if (( logoInfo->sobel[plane][(line + 1) * width + column] == 255) &&
                        ( logoInfo->sobel[plane][(line - 1) * width + column] == 255) &&
                        ( logoInfo->sobel[plane][line * width + (column + 1)] == 255) &&
                        ( logoInfo->sobel[plane][line * width + (column - 1)] == 255) &&
                        ( logoInfo->sobel[plane][(line + 1) * width + (column + 1)] == 255) &&
                        ( logoInfo->sobel[plane][(line - 1) * width + (column - 1)] == 255)) {
                        logoInfo->sobel[plane][line * width + column] = 255;
#if defined(DEBUG_LOGO_CORNER)
                        tsyslog("cExtractLogo::RemovePixelDefects(): fix single separate pixel found at line %d column %d at frame %d in plane %d", line, column, logoInfo->iFrameNumber, plane);
#endif
                    }
                }
                else if ( logoInfo->sobel[plane][line * width + column] == 255) {  //  add single missing pixel
                    if (( logoInfo->sobel[plane][(line + 1) * width + column] == 0) &&
                        ( logoInfo->sobel[plane][(line - 1) * width + column] == 0) &&
                        ( logoInfo->sobel[plane][line * width + (column + 1)] == 0) &&
                        ( logoInfo->sobel[plane][line * width + (column - 1)] == 0) &&
                        ( logoInfo->sobel[plane][(line + 1) * width + (column + 1)] == 0) &&
                        ( logoInfo->sobel[plane][(line - 1) * width + (column - 1)] == 0)) {
                        logoInfo->sobel[plane][line * width + column] = 0;
                    }
                }
            }
        }
    }
#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 2
    Save(maContext, logoInfo, logoHeight, logoWidth, corner, logoInfo->iFrameNumber);
#endif
}


int cExtractLogo::AudioInBroadcast(const MarkAdContext *maContext, const int iFrameNumber) {
    if (!maContext) return 0;

// AudioState 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
    bool is6Channel = false;
    for (short int stream = 0; stream < MAXSTREAMS; stream++){
        if (maContext->Audio.Info.Channels[stream] > 2) {
            is6Channel = true;
            break;
        }
    }
    if (!is6Channel) {
        if (AudioState == 0) {
            dsyslog("cExtractLogo::AudioInBroadcast(): got first time 2 channel at frame (%d)", iFrameNumber);
            AudioState = 1;
            return AudioState;
        }
        if (AudioState == 2) {
            dsyslog("cExtractLogo::AudioInBroadcast(): 2 channel start at frame (%d)", iFrameNumber);
            AudioState = 3;
            return AudioState;
        }
    }
    if (is6Channel) {
        if (AudioState == 1) {
            dsyslog("cExtractLogo::AudioInBroadcast(): got first time 6 channel at frame (%d)", iFrameNumber);
            AudioState = 2;
            return AudioState;
        }
        if (AudioState == 3) {
            dsyslog("cExtractLogo::AudioInBroadcast(): 6 channel start at frame (%d)", iFrameNumber);
            AudioState = 2;
            return AudioState;
        }
    }
    return AudioState;
}


int cExtractLogo::SearchLogo(MarkAdContext *maContext, int startFrame) {  // return -1 internal error, 0 ok, > 0 no logo found, return last framenumber of search
    dsyslog("----------------------------------------------------------------------------");
    dsyslog("cExtractLogo::SearchLogo(): start extract logo from frame %i with aspect ratio %d:%d", startFrame, logoAspectRatio.Num, logoAspectRatio.Den);

    if (!maContext) {
        dsyslog("cExtractLogo::SearchLogo(): maContext not valid");
        return -1;
    }
    if (startFrame < 0) return -1;

    struct timeval startTime, stopTime;
    int iFrameNumber = 0;
    int iFrameCountAll = 0;
    int logoHeight = 0;
    int logoWidth = 0;
    bool retStatus = true;
    bool readNextFile = true;

    gettimeofday(&startTime, NULL);
    MarkAdContext maContextSaveState = {};
    maContextSaveState.Video = maContext->Video;     // save state of calling video context
    maContextSaveState.Audio = maContext->Audio;     // save state of calling audio context

    cDecoder *ptr_cDecoder = new cDecoder(maContext->Config->threads);
    ALLOC(sizeof(*ptr_cDecoder), "ptr_cDecoder");
    cMarkAdLogo *ptr_Logo = new cMarkAdLogo(maContext);
    ALLOC(sizeof(*ptr_Logo), "ptr_Logo");
    cMarkAdBlackBordersHoriz *hborder = new cMarkAdBlackBordersHoriz(maContext);
    ALLOC(sizeof(*hborder), "hborder");
    cMarkAdBlackBordersVert *vborder = new cMarkAdBlackBordersVert(maContext);
    ALLOC(sizeof(*vborder), "vborder");
    areaT *area = ptr_Logo->GetArea();

    if (!WaitForFrames(maContext, ptr_cDecoder)) {
        dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed");
        FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
        delete ptr_cDecoder;
        FREE(sizeof(*ptr_Logo), "ptr_Logo");
        delete ptr_Logo;
        FREE(sizeof(*hborder), "hborder");
        delete hborder;
        FREE(sizeof(*vborder), "vborder");
        delete vborder;
        return -1;
    }

// set start point
    DeleteFrames(maContext, 0, startFrame);
    int firstFrame = GetFirstFrame(maContext);
    int lastFrame = GetLastFrame(maContext);
    int countFrame = CountFrames(maContext);
    if (firstFrame == INT_MAX) dsyslog("cExtractLogo::SearchLogo(): we have no frames already stored");
    else dsyslog("cExtractLogo::SearchLogo(): already have %d frames from (%d) to frame (%d)", countFrame, firstFrame, lastFrame);
    iFrameCountValid = countFrame;
    if (lastFrame > startFrame) startFrame = lastFrame;

    while(retStatus && readNextFile && (ptr_cDecoder->DecodeDir(maContext->Config->recDir))) {
        maContext->Info.VPid.Type = ptr_cDecoder->GetVideoType();
        if (maContext->Info.VPid.Type == 0) {
            dsyslog("cExtractLogo::SearchLogo(): video type not set");
            FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
            delete ptr_cDecoder;
            FREE(sizeof(*ptr_Logo), "ptr_Logo");
            delete ptr_Logo;
            FREE(sizeof(*hborder), "hborder");
            delete hborder;
            FREE(sizeof(*vborder), "vborder");
            delete vborder;
            return -1;
        }
        maContext->Video.Info.Height = ptr_cDecoder->GetVideoHeight();
        maContext->Video.Info.Width = ptr_cDecoder->GetVideoWidth();
        dsyslog("cExtractLogo::SearchLogo(): video resolution %dx%d", maContext->Video.Info.Width, maContext->Video.Info.Height);
        SetLogoSize(maContext, &logoHeight, &logoWidth);
        dsyslog("cExtractLogo::SearchLogo(): logo size %dx%d", logoWidth, logoHeight);

        while(ptr_cDecoder->GetNextFrame()) {
            if (abortNow) return -1;
            if (!WaitForFrames(maContext, ptr_cDecoder)) {
                dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed at frame (%d), got %d valid frames of %d frames read", ptr_cDecoder->GetFrameNumber(), iFrameCountValid, iFrameCountAll);
                retStatus=false;
            }
            if ((ptr_cDecoder->GetFrameInfo(maContext) && retStatus)) {
                if (ptr_cDecoder->isVideoPacket()) {
                    iFrameNumber = ptr_cDecoder->GetFrameNumber();
                    if (iFrameNumber < startFrame) {
                        dsyslog("cExtractLogo::SearchLogo(): seek to frame %i", startFrame);
                        if (!WaitForFrames(maContext, ptr_cDecoder, startFrame)) {
                            dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() for startFrame %d failed", startFrame);
                            retStatus = false;
                        }
                        if (!ptr_cDecoder->SeekToFrame(maContext, startFrame)) {
                            dsyslog("cExtractLogo::SearchLogo(): seek to startFrame %d failed", startFrame);
                            retStatus = false;
                        }
                        continue;
                    }
                    iFrameCountAll++;

                    if (AudioInBroadcast(maContext, iFrameNumber)  == 3) {  // we are in advertising
                        continue;
                    }

                    if ((logoAspectRatio.Num == 0) || (logoAspectRatio.Den == 0)) {
                        logoAspectRatio.Num = maContext->Video.Info.AspectRatio.Num;
                        logoAspectRatio.Den = maContext->Video.Info.AspectRatio.Den;
                        dsyslog("cExtractLogo::SearchLogo(): aspect ratio set to %d:%d", logoAspectRatio.Num, logoAspectRatio.Den);
                    }
                    if ((logoAspectRatio.Num != maContext->Video.Info.AspectRatio.Num) || (logoAspectRatio.Den != maContext->Video.Info.AspectRatio.Den)) {
                        continue;
                    }

                    int hBorderIFrame = 0;
                    int vBorderIFrame = 0;
                    int isHBorder = hborder->Process(iFrameNumber, &hBorderIFrame);
                    int isVBorder = vborder->Process(iFrameNumber, &vBorderIFrame);
                    if (isHBorder) {  // -1 invisible, 1 visible
                        if (hborder->Status() == HBORDER_VISIBLE) {
                            dsyslog("cExtractLogo::SearchLogo(): detect new horizontal border from frame (%d) to frame (%d)", hBorderIFrame, iFrameNumber);
                            iFrameCountValid-=DeleteFrames(maContext, hBorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no horizontal border from frame (%d)", iFrameNumber);
                        }
                    }
                    if (isVBorder) { // -1 invisible, 1 visible
                        if (vborder->Status() == VBORDER_VISIBLE) {
                            dsyslog("cExtractLogo::SearchLogo(): detect new vertical border from frame (%d) to frame (%d)", vBorderIFrame, iFrameNumber);
                            iFrameCountValid-=DeleteFrames(maContext, vBorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no vertical border from frame (%d)", iFrameNumber);
                        }
                    }
                    if ((vborder->Status() == VBORDER_VISIBLE) || (hborder->Status() == HBORDER_VISIBLE)) {
                        dsyslog("cExtractLogo::SearchLogo(): border frame detected, abort logo search");
                        retStatus = false;
                    }

                    iFrameCountValid++;
                    if (!maContext->Video.Data.Valid) {
                        dsyslog("cExtractLogo::SearchLogo(): faild to get video data of frame (%d)", iFrameNumber);
                        continue;
                    }
                    for (int corner = 0; corner < CORNERS; corner++) {
                        int iFrameNumberNext = -1;  // flag for detect logo: -1: called by cExtractLogo, dont analyse, only fill area
                                                    //                       -2: called by cExtractLogo, dont analyse, only fill area, store logos in /tmp for debug
#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 0
                        if (corner == DEBUG_LOGO_CORNER) iFrameNumberNext = -2;   // only for debuging, store logo file to /tmp
#endif
                        area->corner = corner;
                        ptr_Logo->Detect(iFrameNumber, &iFrameNumberNext);  // we do not take care if we detect the logo, we only fill the area
                        logoInfo actLogoInfo = {};
                        actLogoInfo.iFrameNumber = iFrameNumber;
                        memcpy(actLogoInfo.sobel,area->sobel, sizeof(area->sobel));
                        if (CheckValid(maContext, &actLogoInfo, logoHeight, logoWidth, corner)) {
                            RemovePixelDefects(maContext, &actLogoInfo, logoHeight, logoWidth, corner);
                            actLogoInfo.hits = Compare(maContext, &actLogoInfo, logoHeight, logoWidth, corner);

                            if (maContext->Config->autoLogo == 1) { // use packed logos
                                logoInfoPacked actLogoInfoPacked = {};
                                PackLogoInfo(&actLogoInfo, &actLogoInfoPacked);
                                try { logoInfoVectorPacked[corner].push_back(actLogoInfoPacked); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus = false;
                                    break;
                                }
                                ALLOC((sizeof(logoInfoPacked)), "logoInfoVectorPacked");
                            }
                            if (maContext->Config->autoLogo == 2){  // use unpacked logos
                                try { logoInfoVector[corner].push_back(actLogoInfo); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus = false;
                                    break;
                                }
                                ALLOC((sizeof(logoInfo)), "logoInfoVector");
                            }
                        }
                    }
                    if (iFrameCountValid > 1000) {
                        int firstBorder = hborder->GetFirstBorderFrame();
                        if (firstBorder > 0) {
                            dsyslog("cExtractLogo::SearchLogo(): detect unprocessed horizontal border from frame (%d) to frame (%d)", firstBorder, iFrameNumber);
                            iFrameCountValid-=DeleteFrames(maContext, firstBorder, iFrameNumber);
                        }
                        firstBorder = vborder->GetFirstBorderFrame();
                        if (firstBorder > 0) {
                            dsyslog("cExtractLogo::SearchLogo(): detect unprocessed vertical border from frame (%d) to frame (%d)", firstBorder, iFrameNumber);
                            iFrameCountValid-=DeleteFrames(maContext, firstBorder, iFrameNumber);
                        }
                    }
                    if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus) {
                        readNextFile = false;  // force DecodeDir loop to exit
                        break; // finish inner loop and find best match
                    }
                }
            }
            if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus) {
                readNextFile = false;  // force DecodeDir loop to exit
                break; // finish outer loop and find best match
            }
        }
    }

    if (!retStatus && (iFrameCountAll < MAXREADFRAMES) && ((iFrameCountAll > MAXREADFRAMES / 2) || (iFrameCountValid > 800))) {  // reached end of recording before we got 1000 valid frames
        dsyslog("cExtractLogo::SearchLogo(): end of recording reached at frame (%d), read (%d) iFrames and got (%d) valid iFrames, try anyway", iFrameNumber, iFrameCountAll, iFrameCountValid);
        retStatus = true;
    }
    else {
        if (iFrameCountValid < 1000) {
            dsyslog("cExtractLogo::SearchLogo(): read (%i) frames and could not get enough valid frames (%i)", iFrameCountAll, iFrameCountValid);
            retStatus = false;
        }
    }
    if (retStatus) {
        dsyslog("cExtractLogo::SearchLogo(): %d valid frames of %d frames read, got enough iFrames at frame (%d), start analyze", iFrameCountValid, iFrameCountAll, ptr_cDecoder->GetFrameNumber());
        logoInfoPacked actLogoInfoPacked[CORNERS] = {};
        logoInfo actLogoInfo[CORNERS] = {};
        for (int corner = 0; corner < CORNERS; corner++) {
            if (maContext->Config->autoLogo == 1) { // use packed logos
                actLogoInfoPacked[corner] = {};
                for (std::vector<logoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                    if (actLogoPacked->hits > actLogoInfoPacked[corner].hits) {
                        actLogoInfoPacked[corner] = *actLogoPacked;
                    }
                }
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %6d with %3d similars out of %3ld valid frames at %s", actLogoInfoPacked[corner].iFrameNumber, actLogoInfoPacked[corner].hits, logoInfoVectorPacked[corner].size(), aCorner[corner]);
            }
            if (maContext->Config->autoLogo == 2) { // use unpacked logos
                for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                    if (actLogo->hits > actLogoInfo[corner].hits) {
                        actLogoInfo[corner] = *actLogo;
                    }
                }
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %6d with %3d similars out of %3ld valid frames at %s", actLogoInfo[corner].iFrameNumber, actLogoInfo[corner].hits, logoInfoVector[corner].size(), aCorner[corner]);
            }
        }

        // find best and second best corner
        logoInfo bestLogoInfo = {};
        logoInfo secondBestLogoInfo = {};
        int bestLogoCorner = -1;
        int secondBestLogoCorner = -1;
        int sumHits = 0;

        if (maContext->Config->autoLogo == 1) { // use packed logos
            logoInfoPacked bestLogoInfoPacked = {};
            logoInfoPacked secondBestLogoInfoPacked = {};
            for (int corner = 0; corner < CORNERS; corner++) {  // search for the best hits of each corner
                sumHits += actLogoInfoPacked[corner].hits;
                if (actLogoInfoPacked[corner].hits > bestLogoInfoPacked.hits) {
                    bestLogoInfoPacked = actLogoInfoPacked[corner];
                    bestLogoCorner = corner;
                }
            }
            for (int corner = 0; corner < CORNERS; corner++) {  // search for second best hits of each corner
                if ((actLogoInfoPacked[corner].hits > secondBestLogoInfo.hits) && (actLogoInfoPacked[corner].hits < bestLogoInfoPacked.hits)) {
                    secondBestLogoInfoPacked = actLogoInfoPacked[corner];
                    secondBestLogoCorner = corner;
                }
            }
            UnpackLogoInfo(&bestLogoInfo, &bestLogoInfoPacked);
            UnpackLogoInfo(&secondBestLogoInfo, &secondBestLogoInfoPacked);
        }

        if (maContext->Config->autoLogo == 2) { // use unpacked logos
            for (int corner = 0; corner < CORNERS; corner++) {  // search for the best hits of each corner
                sumHits += actLogoInfo[corner].hits;
                if (actLogoInfo[corner].hits > bestLogoInfo.hits) {
                    bestLogoInfo = actLogoInfo[corner];
                    bestLogoCorner = corner;
                }
            }
            for (int corner = 0; corner < CORNERS; corner++) {  // search for second best hits of each corner
                if ((actLogoInfo[corner].hits > secondBestLogoInfo.hits) && (corner != bestLogoCorner)) {
                    secondBestLogoInfo = actLogoInfo[corner];
                    secondBestLogoCorner = corner;
                }
            }
        }

        if ((bestLogoInfo.hits >= 50) || ((bestLogoInfo.hits > 30) && (sumHits <= bestLogoInfo.hits + 3))) {  // if almost all hits are in the same corner than less are enough, increased from 25 to 30
            int secondLogoHeight = logoHeight;
            int secondLogoWidth = logoWidth;
            dsyslog("cExtractLogo::SearchLogo(): best corner is %s at frame %d with %d similars", aCorner[bestLogoCorner], bestLogoInfo.iFrameNumber, bestLogoInfo.hits);
            if (this->Resize(maContext, &bestLogoInfo, &logoHeight, &logoWidth, bestLogoCorner)) {
                if ((secondBestLogoInfo.hits > 50) && (secondBestLogoInfo.hits > (bestLogoInfo.hits * 0.8))) { // decreased from 0.9 to 0.8
                    dsyslog("cExtractLogo::SearchLogo(): try with second best corner %d at frame %d with %d similars", secondBestLogoCorner, secondBestLogoInfo.iFrameNumber, secondBestLogoInfo.hits);
                    if (this->Resize(maContext, &secondBestLogoInfo, &secondLogoHeight, &secondLogoWidth, secondBestLogoCorner)) {
                        dsyslog("cExtractLogo::SearchLogo(): resize logo from second best corner is valid, still no clear result");
                        retStatus=false;
                    }
                    else dsyslog("cExtractLogo::SearchLogo(): resize logo failed from second best corner, use best corner");
                }
            }
            else {
                dsyslog("cExtractLogo::SearchLogo(): resize logo from best corner failed");
                if (secondBestLogoInfo.hits >= 40) { // reduced from 50 to 40
                    dsyslog("cExtractLogo::SearchLogo(): try with second best corner %s at frame %d with %d similars", aCorner[secondBestLogoCorner], secondBestLogoInfo.iFrameNumber, secondBestLogoInfo.hits);
                    if (this->Resize(maContext, &secondBestLogoInfo, &logoHeight, &logoWidth, secondBestLogoCorner)) {
                        bestLogoInfo = secondBestLogoInfo;
                        bestLogoCorner = secondBestLogoCorner;
                    }
                    else {
                        dsyslog("cExtractLogo::SearchLogo(): resize logo from second best failed");
                        retStatus = false;
                    }
                }
                else retStatus = false;
            }
        }
        else {
            if (bestLogoCorner >= 0) dsyslog("cExtractLogo::SearchLogo(): no valid logo found, best logo at frame %i with %i similars at corner %s", bestLogoInfo.iFrameNumber, bestLogoInfo.hits, aCorner[bestLogoCorner]);
            else dsyslog("cExtractLogo::SearchLogo(): no logo found");
            retStatus = false;
        }

        if (retStatus) {
            if (!this->Save(maContext, &bestLogoInfo, logoHeight, logoWidth, bestLogoCorner)) {
                dsyslog("cExtractLogo::SearchLogo(): logo save failed");
                retStatus = false;
            }
        }
    }
    maContext->Video = maContextSaveState.Video;     // restore state of calling video context
    maContext->Audio = maContextSaveState.Audio;     // restore state of calling audio context
    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
    delete ptr_cDecoder;
    FREE(sizeof(*ptr_Logo), "ptr_Logo");
    delete ptr_Logo;
    FREE(sizeof(*hborder), "hborder");
    delete hborder;
    FREE(sizeof(*vborder), "vborder");
    delete vborder;
    if (retStatus) dsyslog("cExtractLogo::SearchLogo(): finished successfully, last frame %i", iFrameNumber);
    else dsyslog("cExtractLogo::SearchLogo(): failed, last frame %i", iFrameNumber);
    dsyslog("----------------------------------------------------------------------------");
    gettimeofday(&stopTime, NULL);
    time_t sec = stopTime.tv_sec - startTime.tv_sec;
    suseconds_t usec = stopTime.tv_usec - startTime.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        sec--;
    }
    logoSearchTime_ms += sec * 1000 + usec / 1000;
    if (retStatus) return 0;
    else {
        if (iFrameNumber > 0) return iFrameNumber;
        else return -1;
    }
}
