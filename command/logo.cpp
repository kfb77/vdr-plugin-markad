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

// #define DEBUG_CORNER TOP_RIGHT

// based on this idee to find the logo in a recording:
// 1. take 1000 ifarmes
// 2. compare each corner of the iframes with all other iframes of the same corner
// 3. take the iframe who has the most similar frame on the same corner, this hopefully should be the logo
// 4. remove the white frame from the logo
// 5. store the logo files in the recording directory for future use


extern bool abortNow;

cExtractLogo::cExtractLogo(MarkAdAspectRatio aspectRatio) {
    logoAspectRatio.Num = aspectRatio.Num;
    logoAspectRatio.Den = aspectRatio.Den;
}


cExtractLogo::~cExtractLogo() {
    for (int corner = 0; corner <= 3; corner++) {  // free memory of all corners
#ifdef DEBUGMEM
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


bool cExtractLogo::isWhitePlane(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane) {
    if (!ptr_actLogoInfo) return false;
    if (logoHeight < 1) return false;
    if (logoWidth < 1) return false;
    if ((plane < 0) || (plane >= PLANES)) return false;

    int countBlack = 0;
    for (int i = 0; i < logoHeight * logoWidth; i++) {
        if (ptr_actLogoInfo->sobel[plane][i] == 0) {
            countBlack++;
            if (countBlack >= 5) return false;   // only if there are same pixel
        }
    }
    return true;
}


bool cExtractLogo::Save(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!maContext) return false;
    if (!ptr_actLogoInfo) return false;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return false;
    if ((corner < 0) || (corner > 3)) return false;
    if (!maContext->Info.ChannelName) return false;

    for (int plane = 0; plane < PLANES; plane++) {
        char *buf=NULL;
        int height = logoHeight;
        int width = logoWidth;
        if (plane > 0) {
            width /= 2;
            height /= 2;
        }
        int black = 0;
        for (int i = 0; i < height*width; i++) {
            if (ptr_actLogoInfo->sobel[plane][i] == 0) black++;
        }
        if (plane > 0) {
            if (black <= 130) {  // increased from 80 to 100 to 110 to 115 to 130
                dsyslog("cExtractLogo::Save(): not enough pixel (%i) in plane %i", black, plane);
                continue;
            }
            else dsyslog("cExtractLogo::Save(): got enough pixel (%i) in plane %i", black, plane);
        }
        else dsyslog("cExtractLogo::Save(): %i pixel in plane %i", black, plane);

        if (this->isWhitePlane(ptr_actLogoInfo, height, width, plane)) continue;
        if (asprintf(&buf, "%s/%s-A%i_%i-P%i.pgm", maContext->Config->recDir, maContext->Info.ChannelName, logoAspectRatio.Num, logoAspectRatio.Den, plane)==-1) return false;
        ALLOC(strlen(buf)+1, "buf");
        dsyslog("cExtractLogo::Save(): store logo in %s", buf);
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
    isyslog("Logo size for Channel: %s %d:%d %dW %dH: %dW %dH %s", maContext->Info.ChannelName, logoAspectRatio.Num, logoAspectRatio.Den, maContext->Video.Info.Width, maContext->Video.Info.Height, logoWidth, logoHeight, aCorner[corner]);
    return true;
}


void cExtractLogo::CutOut(logoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, int corner) {
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
        dsyslog("cExtractLogo::CutOut(): logo size before cut out:   %3d width %3d height on corner %12s, cut out %3dp horizontal and %3dp vertical", *logoWidth, *logoHeight, aCorner[corner], cutPixelH, cutPixelV);
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
        dsyslog("cExtractLogo::CutOut(): logo size before cut out:   %3d width %3d height on corner %12s, cut out %3dp horizontal and %3dp vertical", *logoWidth, *logoHeight, aCorner[corner], cutPixelH, cutPixelV);
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
//    dsyslog("cExtractLogo::CutOut(): logo size after cut out:    %3d width %3d height on corner %d", *logoWidth, *logoHeight, corner);
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
// search for at least 3 white lines to cut logos with text addon (e.g. "Neue Folge" or "Live")
            int countWhite = 0;
            int cutLine = 0;
            int topBlackLine= 0;
            int leftBlackPixel = INT_MAX;
            int rightBlackPixel = 0;
            for (int line = *logoHeight - 1; line > 0; line--) {
                bool isAllWhite = true;
                for (int column = 0; column < *logoWidth; column++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        isAllWhite = false;
                        if (column < leftBlackPixel) leftBlackPixel = column;
                        if (column > rightBlackPixel) rightBlackPixel = column;
                    }
                }
                if (isAllWhite) {
                    countWhite++;
                }
                else {
                    countWhite = 0;
                    topBlackLine = line;
                    if (cutLine > 0) break;
                }
                if (countWhite >= 3) {
                    cutLine = line;
                }
            }
            if (topBlackLine < cutLine) {
                if ((rightBlackPixel - leftBlackPixel) >= 50) {
                    dsyslog("cExtractLogo::Resize(): found text under logo, cut at line %d,  pixel before: left %d right %d, width is valid", cutLine, leftBlackPixel, rightBlackPixel);
                    CutOut(bestLogoInfo, *logoHeight - cutLine, 0, logoHeight, logoWidth, bestLogoCorner);
                }
                else dsyslog("cExtractLogo::Resize(): found text under logo, cut at line %d,  pixel before: left %d right %d width is invalid", cutLine, leftBlackPixel, rightBlackPixel);
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
            int topBlackPixel = INT_MAX;
            int bottomBlackPixel = 0;
            for (int column = 0; column < *logoWidth; column++) {
                bool isAllWhite = true;
                for (int line = 0; line < *logoHeight; line++) {
                    if (bestLogoInfo->sobel[0][line * (*logoWidth) + column] == 0) {
                        isAllWhite = false;
                        if (line < topBlackPixel) topBlackPixel = line;
                        if (line > bottomBlackPixel) bottomBlackPixel = line;
                    }
                }
                if (isAllWhite) countWhite++;
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
                    dsyslog("cExtractLogo::Resize(): found text before logo, cut at column %d, pixel before: top %d bottom %d, height is valid", cutColumn, topBlackPixel, bottomBlackPixel);
                    CutOut(bestLogoInfo, 0, cutColumn, logoHeight, logoWidth, bestLogoCorner);
                }
                else dsyslog("cExtractLogo::Resize(): found text before logo, cut at column %d, pixel before: top %d bottom %d, height is not valid", cutColumn, topBlackPixel, bottomBlackPixel);
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
        dsyslog("cExtractLogo::Resize(): logo size after %d resize:   %3d width %3d height on corner %d", repeat, *logoWidth, *logoHeight, bestLogoCorner);
    }
    bool logoValid = true;
    switch (maContext->Video.Info.Width) {
        case 720:
            if (logoWidthBeforeResize == LOGO_DEFWIDTH) {
                if ((bestLogoCorner >= BOTTOM_LEFT) && (*logoHeight >= 60) && (*logoHeight <= 65) && (*logoWidth >= 185)) { // if logo size is low and wide on BOTTON, it is in a news ticker
                dsyslog("cExtractLogo::Resize(): found SD logo in a news ticker");
                }
                else {
                    if (strcmp(maContext->Info.ChannelName, "RTLplus") == 0) {
                        if (*logoWidth > 212) { // RTLplus
                            dsyslog("cExtractLogo::Resize(): SD logo for RTLPlus is too wide");
                            logoValid = false;
                        }
                    }
                    else {
                        if (*logoWidth >= 150) {
                            dsyslog("cExtractLogo::Resize(): SD logo is too wide");
                            logoValid = false;
                        }
                    }
                }
                if (*logoHeight <= 50) {
                    dsyslog("cExtractLogo::Resize(): SD logo is not heigh enough");
                    logoValid = false;
                }
                if (*logoHeight >= 114) {  // TLC with "love you" addon
                    dsyslog("cExtractLogo::Resize(): SD logo is too heigh");
                    logoValid = false;
                }
            }
            break;
        case 1280:
            if (maContext->Video.Info.Height == 1080) {  // ANIXE+
            }
            else {
                if (*logoWidth >= 256) {
                    dsyslog("cExtractLogo::Resize(): HD logo is too wide");
                    logoValid = false;
                }
                if (*logoHeight > 134) { // arte HD
                    dsyslog("cExtractLogo::Resize(): HD logo is too heigh");
                    logoValid = false;
                }
                if (*logoHeight >= 134) { // logo is vertical
                    if (*logoWidth < 84) { // arte HD
                        dsyslog("cExtractLogo::Resize(): HD logo is too narrow");
                        logoValid = false;
                    }
                }
                else { // logo is horizontal
                    if (*logoWidth <= 116) {
                        dsyslog("cExtractLogo::Resize(): HD logo is too narrow");
                        logoValid = false;
                    }
                }
            }
            break;
        case 1920:
            if (strcmp(maContext->Info.ChannelName, "münchen_tv_HD") == 0) {
                if (*logoHeight < 96) { // münchen_tv_HD
                    dsyslog("cExtractLogo::Resize(): HD logo for münchen_tv_HD is not heigh enough");
                    logoValid = false;
                }
            }
            else {
                if (*logoHeight <= 106) {
                    dsyslog("cExtractLogo::Resize(): HD logo is not heigh enough");
                    logoValid = false;
                }
            }
            break;
        default:
            dsyslog("cExtractLogo::Resize(): no logo size rules for %dx%d", maContext->Video.Info.Width, maContext->Video.Info.Height);
            break;
    }

    if (logoValid) {
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


bool cExtractLogo::CheckValid(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
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
#ifdef DEBUG_CORNER
                if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no big white top part at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }
        for (int i = (logoHeight - WHITEHORIZONTAL_SMALL) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid top logo should have at least a small white buttom part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0) {
#ifdef DEBUG_CORNER
                 if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no small white buttom part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }

    }
    else {
        for (int i = (logoHeight - WHITEHORIZONTAL_BIG) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid bottom logo should have a white bottom part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_CORNER
                 if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no big white buttom part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }
        for (int i = 0 ; i < WHITEHORIZONTAL_SMALL * logoWidth; i++) { // a valid bottom logo should have at least a small white top part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_CORNER
                 if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no small white top part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                return false;
            }
        }

    }

    if ((corner == TOP_LEFT) || (corner == BOTTOM_LEFT)) { // a valid left logo should have white left part in pane 0
        for (int column = 0; column <= WHITEVERTICAL_BIG; column++) {
            for (int i = column; i < logoHeight * logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_CORNER
                     if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no big white left part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
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
#ifdef DEBUG_CORNER
                    if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo %s and has no big white right part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
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
#ifdef DEBUG_CORNER
        if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CheckValid(): logo has no enough pixel %d plane 0 at frame %i", blackPixel1, ptr_actLogoInfo->iFrameNumber);
#endif
        return false;
    }
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
            if (CompareLogoPair(&actLogo, ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                hits++;
                actLogoPacked->hits++;
            }
        }
    }
    if (maContext->Config->autoLogo == 2){  // use unpacked logos
        for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            if (CompareLogoPair(&(*actLogo), ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
                hits++;
                actLogo->hits++;
            }
        }
    }
    return hits;
}


bool cExtractLogo::CompareLogoPair(const logoInfo *logo1, const logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner) {
    if (!logo1) return false;
    if (!logo2) return false;

    int similar_0=0;
    int similar_1_2=0;
    int oneBlack_0 = 0;
    int rate_0=0;
    int rate_1_2=0;
    for (int i = 0; i < logoHeight*logoWidth; i++) {    // compare all black pixel in plane 0
        if ((logo1->sobel[0][i] == 255) && (logo2->sobel[0][i] == 255)) continue;   // ignore white pixel
        else oneBlack_0 ++;
        if (logo1->sobel[0][i] == logo2->sobel[0][i]) {
            similar_0++;
        }
    }
    for (int i = 0; i < logoHeight/2*logoWidth/2; i++) {    // compare all pixel in plane 1 and 2
        for (int plane = 1; plane < PLANES; plane ++) {
            if (logo1->sobel[plane][i] == logo2->sobel[plane][i]) similar_1_2++;
        }
    }
    if (oneBlack_0 > 100) rate_0 = 1000 * similar_0 / oneBlack_0;   // accept only if we found some pixels
    else rate_0=0;
    rate_1_2 = 1000*similar_1_2/(logoHeight*logoWidth)*2;

#define MINMATCH_0 800  // reduced from 890 to 870 to 860 to 800
#define MINMATCH_1_2 980  // reduced from 985 to 980
    if ((rate_0 > MINMATCH_0) && (rate_1_2 > MINMATCH_1_2)) { // reduced from 890 to 870
#ifdef DEBUG_CORNER
        if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ++++ frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d)", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_CORNER
if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ---- frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d) ", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);
#endif
    return false;
}


int cExtractLogo::DeleteFrames(const MarkAdContext *maContext, const int from, const int to) {
    if (!maContext) return false;
    if (from >= to) return 0;
    int deleteCount=0;
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


bool cExtractLogo::WaitForFrames(const MarkAdContext *maContext, cDecoder *ptr_cDecoder) {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;

#define WAITTIME 60
    char *indexFile = NULL;
    if (recordingFrameCount > (ptr_cDecoder->GetFrameNumber()+200)) return true; // we have already found enougt frames

    if (asprintf(&indexFile,"%s/index",maContext->Config->recDir)==-1) {
        dsyslog("cExtractLogo::WaitForFrames: out of memory in asprintf");
        return false;
    }
    ALLOC(strlen(indexFile)+1, "indexFile");
    struct stat indexStatus;
    if (stat(indexFile,&indexStatus)==-1) {
        dsyslog("cExtractLogo::WaitForFrames: failed to stat %s",indexFile);
        FREE(strlen(indexFile)+1, "indexFile");
        free(indexFile);
        return false;
    }
    FREE(strlen(indexFile)+1, "indexFile");
    free(indexFile);
    dsyslog("cExtractLogo::WaitForFrames(): index file size %ld byte", indexStatus.st_size);
    int maxframes = indexStatus.st_size/8;
    recordingFrameCount = maxframes;
    if (maxframes>(ptr_cDecoder->GetFrameNumber()+200)) return true;  // recording has enough frames
    time_t now = time(NULL);
    char systemTime[50] = {0};
    char indexTime[50] = {0};
    strftime(systemTime,sizeof(systemTime),"%d-%m-%Y %H:%M:%S",localtime(&now));
    strftime(indexTime,sizeof(indexTime),"%d-%m-%Y %H:%M:%S",localtime(&indexStatus.st_mtime));
    dsyslog("cExtractLogo::WaitForFrames(): system time %s index time %s", systemTime, indexTime);
    dsyslog("cExtractLogo::WaitForFrames(): need more frames at frame (%d), frames recorded (%i)", ptr_cDecoder->GetFrameNumber(), maxframes);
    if ((difftime(now,indexStatus.st_mtime))>= 2*WAITTIME) {
        dsyslog("cExtractLogo::isRunningRecording(): index not growing at frame (%d), old or interrupted recording", ptr_cDecoder->GetFrameNumber());
        return false;
    }
    dsyslog("cExtractLogo::WaitForFrames(): waiting for new frames at frame (%d), frames recorded (%d)", ptr_cDecoder->GetFrameNumber(), maxframes);
    sleep(WAITTIME); // now we sleep and hopefully the index will grow
    return true;
}


void cExtractLogo::PackLogoInfo(const logoInfo *logoInfo, logoInfoPacked *logoInfoPacked) {
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;
    logoInfoPacked->iFrameNumber=logoInfo->iFrameNumber;
    logoInfoPacked->hits=logoInfo->hits;
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfoPacked->valid[plane]=logoInfo->valid[plane];
        for (int i = 0; i < MAXPIXEL/8; i++) {
            logoInfoPacked->sobel[plane][i]=0;
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
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;
    logoInfo->iFrameNumber=logoInfoPacked->iFrameNumber;
    logoInfo->hits=logoInfoPacked->hits;
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfo->valid[plane]=logoInfoPacked->valid[plane];
        for (int i = 0; i < MAXPIXEL/8; i++) {
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


int cExtractLogo::SearchLogo(MarkAdContext *maContext, int startFrame) {  // return -1 internal error, 0 ok, > 0 no logo found, return last framenumber of search
    dsyslog("----------------------------------------------------------------------------");
    dsyslog("cExtractLogo::SearchLogo(): start extract logo from frame %i with aspect ratio %d:%d", startFrame, logoAspectRatio.Num, logoAspectRatio.Den);

    if (!maContext) {
        dsyslog("cExtractLogo::SearchLogo(): maContext not valid");
        return -1;
    }
    if (startFrame < 0) return -1;

    int iFrameNumber = 0;
    int iFrameCountAll = 0;
    int logoHeight = 0;
    int logoWidth = 0;
    bool retStatus = true;

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

    while(ptr_cDecoder->DecodeDir(maContext->Config->recDir)) {
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
        dsyslog("cExtractLogo::SearchLogo(): video height: %i", maContext->Video.Info.Height);
        maContext->Video.Info.Width=ptr_cDecoder->GetVideoWidth();
        dsyslog("cExtractLogo::SearchLogo(): video width: %i", maContext->Video.Info.Width);
        if (maContext->Video.Info.Width > 720){
            logoHeight = LOGO_DEFHDHEIGHT;
            logoWidth = LOGO_DEFHDWIDTH;
        }
        else {
            logoHeight = LOGO_DEFHEIGHT;
            logoWidth = LOGO_DEFWIDTH;
        }
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
                        ptr_cDecoder->SeekToFrame(startFrame);
                        continue;
                    }
                    iFrameCountAll++;

                    bool is6ChannelBefore = is6Channel;
                    for (short int stream = 0; stream < MAXSTREAMS; stream++){
                        is6Channel = false;
                        if (maContext->Audio.Info.Channels[stream]>2) {
                            is6Channel = true;
                            has6Channel = true;
                            break;
                        }
                    }
                    if (has6Channel) {
                        if (is6Channel && !is6ChannelBefore) dsyslog("cExtractLogo::SearchLogo(): stop ignoring frame because of 6 channel at frame (%d)", iFrameNumber);
                        if (!is6Channel) {
                            if (is6ChannelBefore) dsyslog("cExtractLogo::SearchLogo(): start ignoring frame because of 2 channel at frame (%d)", iFrameNumber);
                            continue;
                        }
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
                    if ((vborder->Status() == VBORDER_VISIBLE) || (hborder->Status() == HBORDER_VISIBLE)) {  // if we run from start, we could start with a border from the previous recording
                        if ((startFrame > 0)) {
                            dsyslog("cExtractLogo::SearchLogo(): border frame detected, abort logo search");
                            retStatus = false;
                        }
                        else continue;
                    }

                    iFrameCountValid++;
                    if (!maContext->Video.Data.Valid) {
                        dsyslog("cExtractLogo::SearchLogo(): faild to get video data of frame (%d)", iFrameNumber);
                        continue;
                    }
                    for (int corner = 0; corner < CORNERS; corner++) {
                        int iFrameNumberNext = -1;  // flag for detect logo: -1: called by cExtractLogo, dont analyse, only fill area
                                                    //                       -2: called by cExtractLogo, dont analyse, only fill area, store logos in /tmp for debug
#ifdef DEBUG_CORNER
                        if (corner == DEBUG_CORNER) iFrameNumberNext = -2;   // TODO only for debug
#endif
                        area->corner=corner;
                        ptr_Logo->Detect(iFrameNumber,&iFrameNumberNext);
                        logoInfo actLogoInfo = {};
                        actLogoInfo.iFrameNumber = iFrameNumber;
                        memcpy(actLogoInfo.sobel,area->sobel, sizeof(area->sobel));
                        if (CheckValid(&actLogoInfo, logoHeight, logoWidth, corner)) {
                            actLogoInfo.hits = Compare(maContext, &actLogoInfo, logoHeight, logoWidth, corner);

                            if (maContext->Config->autoLogo == 1) { // use packed logos
                                logoInfoPacked actLogoInfoPacked = {};
                                PackLogoInfo(&actLogoInfo, &actLogoInfoPacked);
                                try { logoInfoVectorPacked[corner].push_back(actLogoInfoPacked); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus=false;
                                    break;
                                }
                                ALLOC((sizeof(logoInfoPacked)), "logoInfoVectorPacked");
                            }
                            if (maContext->Config->autoLogo == 2){  // use unpacked logos
                                try { logoInfoVector[corner].push_back(actLogoInfo); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus=false;
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
                    if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus)  break; // finish inner loop and find best match
                }
            }
            if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus)  break; // finish outer loop and find best match
        }
    }

    if (!retStatus && (iFrameCountAll < MAXREADFRAMES) && ((iFrameCountAll > MAXREADFRAMES / 2) || (iFrameCountValid > 800))) {  // reached end of recording before we got 1000 valid frames
        dsyslog("cExtractLogo::SearchLogo(): end of recording reached at frame (%d), read (%d) iFrames and got (%d) valid iFrames, try anyway",iFrameNumber , iFrameCountAll, iFrameCountValid);
        retStatus=true;
    }
    else {
        if (iFrameCountValid < 1000) {
            dsyslog("cExtractLogo::SearchLogo(): read (%i) frames and could not get enough valid frames (%i)", iFrameCountAll, iFrameCountValid);
            retStatus=false;
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
        logoInfo bestLogoInfo = {0};
        logoInfo secondBestLogoInfo = {0};
        int bestLogoCorner = -1;
        int secondBestLogoCorner = -1;
        int sumHits = 0;

        if (maContext->Config->autoLogo == 1) { // use packed logos
            logoInfoPacked bestLogoInfoPacked = {0};
            logoInfoPacked secondBestLogoInfoPacked = {0};
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
                if (secondBestLogoInfo.hits >= 50) {
                    dsyslog("cExtractLogo::SearchLogo(): try with second best corner %d at frame %d with %d similars", secondBestLogoCorner, secondBestLogoInfo.iFrameNumber, secondBestLogoInfo.hits);
                    if (this->Resize(maContext, &secondBestLogoInfo, &logoHeight, &logoWidth, secondBestLogoCorner)) {
                        bestLogoInfo = secondBestLogoInfo;
                        bestLogoCorner = secondBestLogoCorner;
                    }
                    else {
                        dsyslog("cExtractLogo::SearchLogo(): resize logo from second best failed");
                        retStatus=false;
                    }
                }
                else retStatus=false;
            }
        }
        else {
            dsyslog("cExtractLogo::SearchLogo(): no valid logo found, best logo at frame %i with %i similars at corner %i", bestLogoInfo.iFrameNumber, bestLogoInfo.hits, bestLogoCorner);
            retStatus=false;
        }

        if (retStatus) {
            if (! this->Save(maContext, &bestLogoInfo, logoHeight, logoWidth, bestLogoCorner)) {
                dsyslog("cExtractLogo::SearchLogo(): logo save failed");
                retStatus=false;
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
    if (retStatus) return 0;
    else {
        if (iFrameNumber > 0) return iFrameNumber;
        else return -1;
    }
}
