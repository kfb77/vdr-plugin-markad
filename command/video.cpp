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

#include "video.h"
#include "logo.h"

// global variables
extern bool abortNow;


cLogoDetect::cLogoDetect(cDecoder *decoderParam, cCriteria *criteriaParam, const char *recDirParam, const char *logoCacheDirParam) {
    decoder      = decoderParam;
    criteria     = criteriaParam;
    recDir       = recDirParam;
    logoCacheDir = logoCacheDirParam;

    // create object for sobel transformation
    sobel = new cSobel(decoder->GetVideoWidth(), decoder->GetVideoHeight(), 0);  // boundary = 0
    ALLOC(sizeof(*sobel), "sobel");
}


cLogoDetect::~cLogoDetect() {
    Clear(false); // free memory for sobel plane
    delete sobel;
    FREE(sizeof(*sobel), "sobel");

}


void cLogoDetect::Clear(const bool isRestart) {
    sobel->FreeAreaBuffer(&area);
    area = {};

    if (isRestart) area.status = LOGO_RESTART;
    else           area.status = LOGO_UNINITIALIZED;
}


sAreaT * cLogoDetect::GetArea() {
    return &area;
}


bool cLogoDetect::LoadLogo() {
    if (!logoCacheDir) {
        esyslog("logo cache directory not set");
        return false;
    }
    if (!recDir) {
        esyslog("recording directory not set");
        return false;
    }
    sobel->FreeAreaBuffer(&area);
    bool foundLogo = false;

    // logo name
    char *logoName=nullptr;
    sAspectRatio *aspectRatio = decoder->GetFrameAspectRatio();
    if (asprintf(&logoName,"%s-A%d_%d", criteria->GetChannelName(), aspectRatio->num, aspectRatio->den) < 0) {
        esyslog("cLogoDetect::LoadLogo(): asprintf failed");
        return false;
    }
    ALLOC(strlen(logoName) + 1, "logoName");
    dsyslog("cLogoDetect::LoadLogo(): try to find logo %s", logoName);

    // try logo cache directory
    dsyslog("cLogoDetect::LoadLogo(): search in logo cache path: %s", logoCacheDir);
    for (int plane = 0; plane < PLANES; plane++) {
        int foundPlane = LoadLogoPlane(logoCacheDir, logoName, plane);
        if (plane == 0) {            // we need at least plane 0
            foundLogo = foundPlane;
            if (!foundLogo) break;
        }
    }
    if (foundLogo) {
        isyslog("logo %s found in logo cache directory: %s", logoName, logoCacheDir);
    }

    // try recording directory
    dsyslog("cLogoDetect::LoadLogo(): search in recording directory: %s", recDir);
    for (int plane = 0; plane < PLANES; plane++) {
        bool foundPlane = LoadLogoPlane(recDir, logoName, plane);
        if (plane == 0) {            // we need at least plane 0
            foundLogo = foundPlane;
            if (!foundLogo) break;
        }
    }

    // check if we have a logo
    if (foundLogo) isyslog("logo %s found in recording directory: %s", logoName, recDir);
    else {
        isyslog("logo %s not found", logoName);
    }
    FREE(strlen(logoName) + 1, "logoName");
    free(logoName);

    return foundLogo;
}


bool cLogoDetect::LoadLogoPlane(const char *path, const char *logoName, const int plane) {
    if (!path) return false;
    if (!logoName) return false;
    if ((plane < 0) || (plane >= PLANES)) {
        dsyslog("cLogoDetect::LoadLogoPlane(): plane %d not valid", plane);
        return false;
    }

    // build full logo file name
    char *logoFileName;
    if (asprintf(&logoFileName, "%s/%s-P%d.pgm", path, logoName, plane) == -1) return false;
    ALLOC(strlen(logoFileName) + 1, "logoFileName");
    dsyslog("cLogoDetect::LoadLogoPlane(): search logo file name %s", logoFileName);

    // read logo file
    FILE *pFile = nullptr;
    pFile = fopen(logoFileName, "rb");
    FREE(strlen(logoFileName) + 1, "logoFileName");
    free(logoFileName);
    if (!pFile) {
        dsyslog("cLogoDetect::LoadLogoPlane(): file not found for logo %s plane %d in %s", logoName, plane, path);
        return false;
    }
    dsyslog("cLogoDetect::LoadLogoPlane(): file found for logo %s plane %d in %s", logoName, plane, path);

    // get logo size and corner
    int width, height;
    char c;
    if (fscanf(pFile, "P5\n#%1c%1i %4i\n%3d %3d\n255\n#", &c, &area.logoCorner, &area.mPixel[plane], &width, &height) != 5) {
        fclose(pFile);
        esyslog("format error in %s", logoFileName);
        return false;
    }

    if (height == 255) {
        height = width;
        width  = area.mPixel[plane];
        area.mPixel[plane] = 0;
    }
    if ((width <= 0) || (height <= 0) || (area.logoCorner < TOP_LEFT) || (area.logoCorner > BOTTOM_RIGHT)) {
        fclose(pFile);
        esyslog("format error in %s", logoFileName);
        return false;
    }

    // alloc buffer for logo and result
    if (plane == 0) {   // plane 0 is the largest, use this values
        area.logoSize.width  = width;
        area.logoSize.height = height;
        sobel->AllocAreaBuffer(&area);
        dsyslog("cLogoDetect::LoadLogoPlane(): logo size %dX%d in corner %s", area.logoSize.width, area.logoSize.height, aCorner[area.logoCorner]);
    }

    // read logo from file
    if (fread(area.logo[plane], 1, width * height, pFile) != (size_t)(width * height)) {
        fclose(pFile);
        esyslog("format error in %s", logoFileName);
        return false;
    }
    fclose(pFile);

    // calculate pixel for logo detection
    if (area.mPixel[plane] == 0) {
        for (int i = 0; i < width * height; i++) {
            if ((area.logo[plane][i]) == 0) area.mPixel[plane]++;
        }
        dsyslog("cLogoDetect::LoadLogoPlane(): logo plane %d has %d pixel", plane, area.mPixel[plane]);
    }
    area.valid[plane] = true;
    return true;
}

int cLogoDetect::GetLogoCorner() const {
    return area.logoCorner;
}


// reduce brightness and increase contrast
// return true if we now have a valid detection result
//
bool cLogoDetect::ReduceBrightness(const int logo_vmark, const int logo_imark) {  // frameNumber used only for debugging
    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cLogoDetect::ReduceBrightness picture not valid");
        return VBORDER_ERROR;
    }
    int frameNumber = decoder->GetVideoFrameNumber();
    int xstart, xend, ystart, yend;
    if (!sobel->SetCoordinates(&area, 0, &xstart, &xend, &ystart, &yend)) return false;   // plane 0

// calculate coorginates for logo black pixel area in logo corner
    if ((logo_xstart == -1) && (logo_xend == -1) && (logo_ystart == -1) && (logo_yend == -1)) {  // have to init
        switch (area.logoCorner) {  // logo is usually in the inner part of the logo corner
#define LOGO_MIN_PIXEL 30  // big enough to get in the main part of the logo
        case TOP_LEFT: {
            // xend and yend from logo coordinates
            logo_xend = xend;
            logo_yend = yend;

            // xstart is first column with pixel in logo area
            int pixelCount = 0;
            int column;
            int line;
            for (column = 0; column < area.logoSize.width; column++) {
                for (line = 0; line < area.logoSize.height; line++) {
                    if (area.logo[0][line * area.logoSize.width + column] == 0) pixelCount++;
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                if (pixelCount > LOGO_MIN_PIXEL) break;
            }
            logo_xstart = column;

            // ystart is first line with pixel in logo area
            pixelCount = 0;
            for (line = 0; line < area.logoSize.height; line++) {
                for (column = 0; column < area.logoSize.width; column++) {
                    if (area.logo[0][line * area.logoSize.width + column] == 0) pixelCount++;
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
            for (column = area.logoSize.width - 1; column >= 0; column--) {
                for (line = 0; line < area.logoSize.height; line++) {
                    if (area.logo[0][line * area.logoSize.width + column] == 0) pixelCount++;
                    if (pixelCount > LOGO_MIN_PIXEL) break;
                }
                if (pixelCount > LOGO_MIN_PIXEL) break;
            }
            logo_xend = xend - (area.logoSize.width - column);

            // ystart is first line with pixel in logo area
            pixelCount = 0;
            for (line = 0; line < area.logoSize.height; line++) {
                for (column = 0; column < area.logoSize.width; column++) {
                    if (area.logo[0][line * area.logoSize.width + column] == 0) pixelCount++;
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
//        dsyslog("cLogoDetect::ReduceBrightness(): logo area: xstart %d xend %d, ystart %d yend %d", logo_xstart, logo_xend, logo_ystart, logo_yend);
    }

// detect contrast and brightness of logo part
    int minPixel = INT_MAX;
    int maxPixel = 0;
    int sumPixel = 0;
    for (int line = logo_ystart; line <= logo_yend; line++) {
        for (int column = logo_xstart; column <= logo_xend; column++) {
            int pixel = picture->plane[0][line * picture->planeLineSize[0] + column];
            if (pixel > maxPixel) maxPixel = pixel;
            if (pixel < minPixel) minPixel = pixel;
            sumPixel += pixel;
        }
    }
    int brightnessLogo = sumPixel / ((logo_yend - logo_ystart + 1) * (logo_xend - logo_xstart + 1));
    int contrastLogo = maxPixel - minPixel;
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): logo area before reduction: contrast %3d, brightness %3d", frameNumber, contrastLogo, brightnessLogo);
#endif

// check if contrast and brightness is valid
// build a curve from examples

    // very high contrast with not very high brightness in logo area, trust detection
    //
    // false negativ, logo is visible but not detected
    // contrast 202, brightness  85
    // contrast 200, brightness  85
    if ((contrastLogo > 202) && (brightnessLogo < 85)) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): very high contrast with not very high brightness in logo area, trust detection", frameNumber);
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
// contrast 139, brightness 180  -> bright background without logo
//
// contrast  54, brightness 181  -> no logo in frame
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
            ((contrastLogo  >  60) && (contrastLogo <= 140) && (brightnessLogo > 180)) ||
            ((contrastLogo  > 140) && (contrastLogo <= 180) && (brightnessLogo > 181)) ||
            ((contrastLogo  > 180) && (contrastLogo <= 194) && (brightnessLogo > 120)) ||
            ((contrastLogo  > 194) &&                          (brightnessLogo > 110))) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): contrast/brightness in logo area is invalid for brightness reduction", frameNumber);
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
        if (line > (picture->height - 1)) continue;
        for (int column = xstart - 1; column <= xend + 1; column++) {
            if (column < 0) continue;
            if (column > (picture->width - 1)) continue;
            int pixel = picture->plane[0][line * picture->planeLineSize[0] + column] - REDUCE_BRIGHTNESS;
            if (pixel < 0) pixel = 0;
            pixel = INCREASE_CONTRAST * (pixel - 128) + 128;
            if (pixel < 0) pixel = 0;
            if (pixel > 255) pixel = 255;
            picture->plane[0][line * picture->planeLineSize[0] + column] = pixel;
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
    dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): logo area after  reduction: contrast %3d, brightness %3d", frameNumber, contrastReduced, brightnessReduced);
#endif

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameNumber > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameNumber < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d_corrected.pgm", recDir, frameNumber) >= 1) {
            ALLOC(strlen(fileName) + 1, "fileName");
            SaveVideoPicture(fileName, decoder->GetVideoPicture());
            FREE(strlen(fileName) + 1, "fileName");
            free(fileName);
        }
    }
#endif

// if we have a comple white picture after brightness reduction, we can not decide if there is a logo or not
    if ((contrastReduced == 0) && (brightnessReduced == 255)) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): detection impossible on white picture", frameNumber);
#endif
        return false;
    }

// redo sobel transformation with reduced brightness and verfy result picture
    // redo sobel transformation
    int rPixelBefore = area.rPixel[0];
    area.rPixel[0] = 0;
    sobel->SobelPlane(picture, &area, 0);       // only plane 0
    int rPixel = area.rPixel[0];
    int mPixel = area.mPixel[0];
    int iPixel = area.iPixel[0];

#ifdef DEBUG_LOGO_DETECTION
    char detectStatus[] = "o";
    if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
    if (rPixel <= logo_imark) strcpy(detectStatus, "-");
    dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameNumber, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, 1, detectStatus);
#endif

#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    if ((frameNumber > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameNumber < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_3sobelCorrected.pgm", recDir, frameNumber, area.logoCorner) >= 1) {
            ALLOC(strlen(fileName) + 1, "fileName");
            SaveSobel(fileName, area.sobel[0], area.logoSize.width, area.logoSize.height);
            FREE(strlen(fileName) + 1, "fileName");
            free(fileName);
        }
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_4resultCorrected.pgm", recDir, frameNumber, area.logoCorner) >= 1) {
            ALLOC(strlen(fileName) + 1, "fileName");
            SaveSobel(fileName, area.result[0], area.logoSize.width, area.logoSize.height);
            FREE(strlen(fileName) + 1, "fileName");
            free(fileName);
        }
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_3inverseCorrected.pgm", recDir, frameNumber, area.logoCorner) >= 1) {
            ALLOC(strlen(fileName) + 1, "fileName");
            SaveSobel(fileName, area.inverse[0], area.logoSize.width, area.logoSize.height);
            FREE(strlen(fileName) + 1, "fileName");
            free(fileName);
        }
    }
#endif

    // check background pattern
    int black = 0;
    for (int i = 0; i < area.logoSize.height * area.logoSize.width; i++) {
        if (area.sobel[0][i] == 0) black++;
    }
    int quoteBlack   = 100 * black  /  (area.logoSize.height * area.logoSize.width);
    int quoteInverse = 100 * iPixel / ((area.logoSize.height * area.logoSize.width) - mPixel);
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): area intensity %3d, black quote: %d%%, inverse quote %d%%", frameNumber, area.intensity, quoteBlack, quoteInverse);
#endif

    // prevent to miss logo stop from background pattern
    // check if we have new matches after brightness reduction only from backbround patten
    // current state logo visable, no logo before brightness reduction, unclear result after, hight backgrund patten quote
    //
    // exmample no logo visible
    // area intensity 80, black quote: 32%, inverse quote 32%
    // area intensity 41, black quote: 27%, inverse quote 24%
    // area intensity 37, black quote: 25%, inverse quote 23%
    // area intensity 32, black quote: 25%, inverse quote 22%
    if ((area.status == LOGO_VISIBLE) && (area.intensity <= 86) && (rPixelBefore <= logo_imark) && (rPixel > logo_imark) && (rPixel < logo_vmark) &&
            (quoteBlack >= 25) && (quoteInverse >= 22)) {
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): inverse quote above limit, assume matches only from patten and logo is invisible", frameNumber);
        area.rPixel[0] = logo_imark;  // set logo machtes to invisible
        return true;
    }

    // prevent get false logo start detection from patten background
    // check if we have new matches after brightness reduction only from backbround patten
    // current state logo invisable, unclear result before brightness reduction, logo visable after, hight backgrund patten quote
    //
    // example no logo visibale
    // area intensity 122, black quote: 37%, inverse quote 35%
    // area intensity  93, black quote: 52%, inverse quote 51%
    // area intensity  31, black quote: 22%, inverse quote 19%
    // area intensity  30, black quote: 21%, inverse quote 18%
    // area intensity  30, black quote: 20%, inverse quote 18%
    // area intensity  29, black quote: 20%, inverse quote 18%
    // area intensity  29, black quote: 20%, inverse quote 17%
    // area intensity  27, black quote: 18%, inverse quote 15%
    // area intensity  27, black quote: 18%, inverse quote 15%
    if ((area.status == LOGO_INVISIBLE) && (rPixelBefore < logo_vmark) && (rPixel > logo_vmark) &&
            (area.intensity >= 27) && (quoteBlack >= 18) && (quoteInverse >= 15)) {
        return false; // there is a pattern on the backbround, no logo detection possible
    }

    // now we trust logo visible
    if (rPixel >= logo_vmark) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): valid logo visible after brightness reducation", frameNumber);
#endif
        return true;  // we have a clear result
    }

    // ignore matches on still bright picture
    if ((area.intensity > 160) && (rPixel >= logo_imark / 5)) { // still too bright, trust only very low matches
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): logo area still too bright", frameNumber);
#endif
        return false;
    }

    // now we trust logo invisible
    if (rPixel <= logo_imark) {
#ifdef DEBUG_LOGO_DETECTION
        dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d): valid logo invisible after brightness reducation", frameNumber);
#endif
        return true;  // we have a clear result
    }

    // still no clear result
#ifdef DEBUG_LOGO_DETECTION
    dsyslog("cLogoDetect::ReduceBrightness(): frame (%6d) no valid result after brightness reducation", frameNumber);
#endif
    return false;
}


// copy all black pixels from logo pane 0 into plan 1 and plane 2
// we need this for channels with usually grey logos, but at start and end they can be red (DMAX)
//
void cLogoDetect::LogoGreyToColour() {
    for (int line = 0; line < area.logoSize.height; line++) {
        for (int column = 0; column < area.logoSize.width; column++) {
            if (area.logo[0][line * area.logoSize.width + column] == 0 ) {
                area.logo[1][line / 2 * area.logoSize.width / 2 + column / 2] = 0;
                area.logo[2][line / 2 * area.logoSize.width / 2 + column / 2] = 0;
            }
            else {
                area.logo[1][line / 2 * area.logoSize.width / 2 + column / 2] = 255;
                area.logo[2][line / 2 * area.logoSize.width / 2 + column / 2] = 255;
            }
        }
    }
}


// notice: if we are called by logo detection, <framenumber> is last iFrame before, otherwise it is current frame
int cLogoDetect::Detect(const int frameBefore, const int frameCurrent, int *logoFrameNumber) {
    int rPixel       =  0;
    int mPixel       =  0;
    int iPixel       =  0;
    int processed    =  0;
    *logoFrameNumber = -1;

    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cVertBorderDetect::Process(): picture not valid");
        return VBORDER_ERROR;
    }
    if(!picture->plane[0]) {
        dsyslog("cVertBorderDetect::Process(): picture plane 0 not valid");
        return VBORDER_ERROR;
    }
    if(picture->planeLineSize[0] <= 0) {
        dsyslog("cVertBorderDetect::Process(): picture planeLineSize[0] valid");
        return VBORDER_ERROR;
    }

    // apply sobel transformation to all planes
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    processed = sobel->SobelPicture(recDir, picture, &area, false);  // don't ignore logo
    if ((frameCurrent > DEBUG_LOGO_DETECT_FRAME_CORNER - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE) && (frameCurrent < DEBUG_LOGO_DETECT_FRAME_CORNER + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE)) {
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d.pgm", recDir, frameCurrent) >= 1) {
            ALLOC(strlen(fileName) + 1, "fileName");
            SaveVideoPicture(fileName, decoder->GetVideoPicture());
            FREE(strlen(fileName) + 1, "fileName");
            free(fileName);
        }
    }
#else
    processed = sobel->SobelPicture(picture, &area, false);  // don't ignore logo
#endif
    for (int plane = 0; plane < PLANES; plane++) {
        rPixel += area.rPixel[plane];
        mPixel += area.mPixel[plane];
        iPixel += area.iPixel[plane];
    }

    if (processed == 0) return LOGO_ERROR;  // we have no plane processed

    // set logo visible and invisible limits
    int logo_vmark = LOGO_VMARK * mPixel;
    int logo_imark = LOGO_IMARK * mPixel;
    if (criteria->LogoRotating()) {  // reduce if we have a rotating logo (e.g. SAT_1), changed from 0.9 to 0.8
        logo_vmark *= 0.8;
        logo_imark *= 0.8;
    }
    if (criteria->LogoTransparent()) { // reduce if we have a transparent logo (e.g. SRF_zwei_HD)
        logo_vmark *= 0.9;
        logo_imark *= 0.9;
    }

    bool logoStatus     = false;

    // in dark scene we can use stronger detection
    // don't miss logo invisible for:
    // - down shiftet logo in add (Pro7_MAXX), will only work on dark background
    // - part of logo in black screen as stop mark instead of no logo (Comedy_Central)
#define AREA_INTENSITY_TRUST    54 // we trust detection, use higher invisible value
#define QUOTE_TRUST              2 // uplift factor for logo invisible threshold
    if (area.intensity <= AREA_INTENSITY_TRUST) logo_imark *= QUOTE_TRUST;

#ifdef DEBUG_LOGO_DETECTION
    char detectStatus[] = "o";
    if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
    if (rPixel <= logo_imark) strcpy(detectStatus, "-");
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
    dsyslog("cLogoDetect::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
#endif

    // we have only 1 plane (no coloured logo)
    // if we only have one plane we are "vulnerable"
    // to very bright pictures, so ignore them...
    if (processed == 1) {
        // special cases where detection is not possible:
        // prevent to detect logo start on very bright background, this is not possible
        if ((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark) && area.intensity >= 218) {  // possible state change from invisible to visible
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cLogoDetect::Detect(): frame (%6d) too bright %d for logo start", frameCurrent, area.intensity);
#endif
            return LOGO_NOCHANGE;
        }

        // transparent logo decetion on bright backbround is imposible, changed from 189 to 173
        if (criteria->LogoTransparent() && (area.intensity >= 161)) {  // changed from 173 to 161
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cLogoDetect::Detect(): frame (%6d) with transparent logo too bright %d for detection", frameCurrent, area.intensity);
#endif
            return LOGO_NOCHANGE;
        }

        // background pattern can mess up soble transformation result
        if (((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark)) || // logo state was invisible, new logo state visible
                // prevent to detect background pattern as new logo start
                // logo state was visible, new state unclear result
                // ignore very bright pictures, we can have low logo result even on pattern background, better do brighntness reduction before to get a clear result
                ((area.status == LOGO_VISIBLE) && (rPixel > logo_imark) && (rPixel < logo_vmark) && area.intensity <= 141)) {
            int black   = 0;
            for (int i = 0; i < area.logoSize.height * area.logoSize.width; i++) {
                if (area.sobel[0][i]     == 0) black++;
            }
            int quoteBlack   = 100 * black  /  (area.logoSize.height * area.logoSize.width);
            int quoteInverse = 100 * iPixel / ((area.logoSize.height * area.logoSize.width) - mPixel);
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cLogoDetect::Detect(): frame (%6d): area intensity %3d, black quote: %d%%, inverse quote %d%%", frameCurrent, area.intensity, quoteBlack, quoteInverse);
#endif
            // state logo invisible and rPixel > logo_vmark
            if ((area.status == LOGO_INVISIBLE) && (rPixel >= logo_vmark)) {
                // example logo visible and (rPixel >= logo_vmark)
                // tbd
                //
                // example logo invisible but (rPixel >= logo_vmark) from pattern in background
                // area intensity 154, black quote: 47%, inverse quote 46%
                // area intensity 154, black quote: 47%, inverse quote 46%
                // area intensity 154, black quote: 46%, inverse quote 45%
                // area intensity 153, black quote: 47%, inverse quote 46%
                //
                // area intensity 141, black quote: 23%, inverse quote 20%
                // area intensity 136, black quote: 23%, inverse quote 20%
                // area intensity 126, black quote: 26%, inverse quote 23%
                // area intensity  90, black quote: 19%, inverse quote 16%
                // area intensity  89, black quote: 18%, inverse quote 16%
                // area intensity  77, black quote: 23%, inverse quote 21%
                // area intensity  75, black quote: 24%, inverse quote 21%
                // area intensity  73, black quote: 24%, inverse quote 21%
                // area intensity  73, black quote: 22%, inverse quote 20%
                // area intensity  71, black quote: 24%, inverse quote 22%
                // area intensity  68, black quote: 24%, inverse quote 22%
                // area intensity  67, black quote: 26%, inverse quote 24%
                // area intensity  66, black quote: 24%, inverse quote 22%
                // area intensity  65, black quote: 25%, inverse quote 23%
                // area intensity  63, black quote: 24%, inverse quote 22%

                // asume uniformly background pattern, quoteInverse matches are from background pattern
                // if still logo visible without this trust detection
                int rPixelWithout = rPixel * (100 - quoteInverse) / 100;
                if (rPixelWithout >= logo_vmark) {
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cLogoDetect::Detect(): frame (%6d): rPixel without pattern quote %d, trust logo visible", frameCurrent, rPixelWithout);
#endif
                    logoStatus = true;  // now we trust a positiv logo detection
                }

                else {
                    if (area.intensity < 153) { // on very bright backgrund logo visible result is not possible
                        if ((quoteBlack >= 18) && (quoteInverse >= 16)) {  // with very bright backgrund logo visible is not possible
#ifdef DEBUG_LOGO_DETECTION
                            dsyslog("cLogoDetect::Detect(): frame (%6d): quote above limit, assume matches only from patten and logo is still invisible", frameCurrent);
#endif
                            return LOGO_NOCHANGE;
                        }
                    }
                    else { // pattern in bright background, no current logo visible but detected
                        if ((quoteBlack >= 46) && (quoteInverse >= 45)) {
                            area.counter--;       // assume only pattern, no logo
                            if (area.counter <= 0) area.counter = 0;
#ifdef DEBUG_LOGO_DETECTION
                            dsyslog("cLogoDetect::Detect(): frame (%6d): quote above limit with bright background, assume matches only from patten and logo is still invisible", frameCurrent);
#endif
                            return LOGO_NOCHANGE;
                        }
                    }
                    logoStatus = true;  // now we trust a positiv logo detection
                }
            }
            else {
                // prevent to miss logo stop from background pattern
                // state logo visible and logo_imark < rPixel < logo_vmark
                //
                // logo visible example
                // area intensity 120, black quote: 37%, inverse quote 37%
                // area intensity 119, black quote: 38%, inverse quote 38%
                // area intensity 119, black quote: 22%, inverse quote 21%
                // area intensity 118, black quote: 37%, inverse quote 36%
                // area intensity 108, black quote: 25%, inverse quote 24%
                // area intensity 104, black quote: 28%, inverse quote 27%
                // area intensity 102, black quote: 25%, inverse quote 25%
                // area intensity  99, black quote: 24%, inverse quote 18%
                //
                // logo invisible example
                // area intensity 103, black quote: 35%, inverse quote 34%  -> no logo, tree and sky in backbground
                // area intensity  76, black quote: 15%, inverse quote 14%
                //
                // check logo state visible, new unclear result, trust less bright picture more than bright pictures
                if (
                    (                           (area.intensity <= 76) && (quoteBlack >= 15) && (quoteInverse >= 14)) ||
                    ((area.intensity >= 103) && (area.intensity < 118) && (quoteBlack >= 35) && (quoteInverse >= 34)) ||
                    ((area.intensity >= 118) &&                           (quoteBlack >= 38) && (quoteInverse >= 38))) {
                    rPixel = logo_imark; // set to invisible
                    logoStatus = true;
#ifdef DEBUG_LOGO_DETECTION
                    dsyslog("cLogoDetect::Detect(): frame (%6d) quote above limit, assume matches only from patten and logo is invisible", frameCurrent);
#endif
                }
            }
        }

// if current state is logo visible or uninitialized (to get an early logo start) and we have a lot of matches, now we trust logo is still there
        if (!logoStatus &&
                ((area.status == LOGO_VISIBLE) ||  (area.status == LOGO_UNINITIALIZED)) &&
                (rPixel > logo_imark)) {
#ifdef DEBUG_LOGO_DETECTION
            dsyslog("cLogoDetect::Detect(): frame (%6d) high machtes, trust logo still visible", frameCurrent);
#endif
            logoStatus = true;
        }

        // check if we have a valid logo visible/invisible result
#define MAX_AREA_INTENSITY  56    // limit to reduce brightness
        if (!logoStatus && (area.intensity <= MAX_AREA_INTENSITY) && (rPixel <= logo_imark)) logoStatus = true;  // we have no bright picture so we have a valid logo invisible result

        // if we have still no match, try to copy colour planes into grey planes
        // some channel use coloured logo at broadcast start
        // for performance reason we do this only for the known channel
        if (!logoStatus && criteria->LogoColorChange()) {
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
                sobel->SobelPlane(picture, &area, 0);  // plane 0
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
            dsyslog("cLogoDetect::Detect(): frame (%6d): maybe colour change, try plane 1 and plan 2", frameCurrent);
            char detectStatus[] = "o";
            if (rPixel >= logo_vmark) strcpy(detectStatus, "+");
            if (rPixel <= logo_imark) strcpy(detectStatus, "-");
            dsyslog("cLogoDetect::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
#endif
            if (rPixel <= logo_imark) { // we found no coloured logo here, restore old state
                area.intensity = intensityOld;
                rPixel = rPixelOld;
            }
            if (rPixel >= logo_vmark) {
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cLogoDetect::Detect(): frame (%6d): logo visible in plane 1 and plan 2", frameCurrent);
#endif
                logoStatus = true;  // we found colored logo
            }
            // change from very low match to unclear result
            if ((rPixelOld <= (logo_imark / 10)) && (rPixel > logo_imark)) {
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cLogoDetect::Detect(): frame (%6d): unclear result in plane 1 and plan 2", frameCurrent);
#endif
                return LOGO_NOCHANGE;
            }
        }

        // try to reduce brightness and increase contrast
        // check area intensitiy
        // notice: there can be very bright logo parts in dark areas, this will result in a lower brightness, we handle this cases in ReduceBrightness() when we detect contrast
        // check if area is bright
        // changed max area.intensity from 221 to 234 t detect logo invisible on white separator
        if (!logoStatus && (area.intensity > MAX_AREA_INTENSITY) && (area.intensity <= 234)) {  //  only if we don't have a valid result yet
            // reduce brightness and increase contrast
            logoStatus = ReduceBrightness(logo_vmark, logo_imark);
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
                if (area.mPixel[i] == 0) continue;   // plane has no logo
#ifdef DEBUG_LOGO_DETECTION
                dsyslog("cLogoDetect::Detect():       plane %d: rp=%5d | ip=%5d | mp=%5d | mpV=%5.f | mpI=%5.f |", i, area.rPixel[i], area.iPixel[i], area.mPixel[i], area.mPixel[i] * LOGO_VMARK, area.mPixel[i] * LOGO_IMARK);
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
        dsyslog("cLogoDetect::Detect(): frame (%6d) no valid result", frameCurrent);
#endif
        return LOGO_NOCHANGE;
    }

// set logo visible/unvisible status
// set initial start status
    if (area.status == LOGO_UNINITIALIZED) {
        if (rPixel >= logo_vmark) area.status = LOGO_VISIBLE;
        if (rPixel <= logo_imark) area.status = LOGO_INVISIBLE;  // wait for a clear result
        if (area.stateFrameNumber == -1) area.stateFrameNumber = frameCurrent;
        *logoFrameNumber = area.stateFrameNumber;
        return area.status;
    }
    if (area.status == LOGO_RESTART) {
        if (rPixel >= logo_vmark) area.status = LOGO_VISIBLE;
        if (rPixel <= logo_imark) area.status = LOGO_INVISIBLE;  // wait for a clear result
        *logoFrameNumber = -1;   // no logo change report after detection restart
        area.stateFrameNumber = frameCurrent;
        return area.status;
    }


    int ret = LOGO_NOCHANGE;
    if (rPixel >= logo_vmark) {
        if (area.status == LOGO_INVISIBLE) {
            if (area.counter >= LOGO_VMAXCOUNT) {
                area.status = ret = LOGO_VISIBLE;
                *logoFrameNumber = area.stateFrameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.stateFrameNumber = frameCurrent;
                area.counter++;
            }
        }
        else {
            area.stateFrameNumber = frameCurrent;
            area.counter = 0;
        }
    }

    if (rPixel <=logo_imark) {
        if (area.status == LOGO_VISIBLE) {
            if (area.counter >= LOGO_IMAXCOUNT) {
                area.status = ret = LOGO_INVISIBLE;
                *logoFrameNumber = area.stateFrameNumber;
                area.counter = 0;
            }
            else {
                if (!area.counter) area.stateFrameNumber = frameBefore;
                area.counter++;
                if (rPixel <= (logo_imark / 2)) area.counter++;   // good detect for logo invisible
                if (rPixel <= (logo_imark / 4)) area.counter++;   // good detect for logo invisible
                if (rPixel == 0) {
                    area.counter++;   // very good detect for logo invisible
                    if (area.intensity <= 80) { // best detect, blackscreen without logo, increased from 30 to 70 to 80
                        dsyslog("cLogoDetect::Detect(): black screen without logo detected at frame (%d)", frameCurrent);
                        area.status = ret = LOGO_INVISIBLE;
                        *logoFrameNumber = area.stateFrameNumber;
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
    dsyslog("cLogoDetect::Detect():           frame (%6d): rp=%5d | ip=%5d | mp=%5d | mpV=%5d | mpI=%5d | i=%3d | c=%d | s=%d | p=%d | v=%s", frameCurrent, rPixel, iPixel, mPixel, logo_vmark, logo_imark, area.intensity, area.counter, area.status, processed, detectStatus);
    dsyslog("----------------------------------------------------------------------------------------------------------------------------------------------");
#endif

    return ret;
}


int cLogoDetect::Process(int *logoFrameNumber) {
    int frameNumber = decoder->GetVideoFrameNumber();

    if (area.logoAspectRatio != *decoder->GetFrameAspectRatio()) {
        dsyslog("cLogoDetect::Process(): aspect ratio changed from %d:%d to %d:%d, reload logo", area.logoAspectRatio.num, area.logoAspectRatio.den, decoder->GetFrameAspectRatio()->num, decoder->GetFrameAspectRatio()->den);
        if (!LoadLogo()) {
            isyslog("no valid logo found for %s %d:%d, disable logo detection", criteria->GetChannelName(), decoder->GetFrameAspectRatio()->num, decoder->GetFrameAspectRatio()->den);
            criteria->SetMarkTypeState(MT_LOGOCHANGE, CRITERIA_DISABLED);
        }
        area.logoAspectRatio = *decoder->GetFrameAspectRatio();
    }
    return Detect(frameNumber - 1, frameNumber, logoFrameNumber);
}


// detect scene change
cSceneChangeDetect::cSceneChangeDetect(cDecoder *decoderParam, cCriteria *criteriaParam) {
    decoder  = decoderParam;
    criteria = criteriaParam;
}


cSceneChangeDetect::~cSceneChangeDetect() {
    if (prevHistogram) {  // in case constructor called but never Process()
        FREE(sizeof(*prevHistogram), "SceneChangeHistogramm");
        free(prevHistogram);
    }
}


int cSceneChangeDetect::Process(int *changeFrameNumber) {
    if (!changeFrameNumber) return SCENE_ERROR;

    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cVertBorderDetect::Process(): picture not valid");
        return VBORDER_ERROR;
    }
    if(!picture->plane[0]) {
        dsyslog("cVertBorderDetect::Process(): picture plane 0 not valid");
        return VBORDER_ERROR;
    }
    if(picture->planeLineSize[0] <= 0) {
        dsyslog("cVertBorderDetect::Process(): picture planeLineSize[0] valid");
        return VBORDER_ERROR;
    }
    int frameNumber = decoder->GetVideoFrameNumber();

    // get simple histogramm from current frame
    int *currentHistogram = nullptr;
    currentHistogram = static_cast<int *>(malloc(sizeof(int) * 256));
    ALLOC(sizeof(*currentHistogram), "SceneChangeHistogramm");
    memset(currentHistogram, 0, sizeof(int[256]));
    for (int Y = 0; Y < picture->height; Y++) {
        for (int X = 0; X < picture->width; X++) {
            uchar val = picture->plane[0][X + (Y * picture->planeLineSize[0])];
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
    int diffQuote = 1000 * difference / (picture->height * picture->width * 2);
#ifdef DEBUG_SCENE_CHANGE
    dsyslog("cSceneChangeDetect::Process(): previous frame (%7d) and current frame (%7d): status %2d, blendCount %2d, blendFrame %7d, difference %7ld, diffQute %4d", prevFrameNumber, currentFrameNumber, sceneStatus, blendCount, blendFrame, difference, diffQuote);
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
        dsyslog("cSceneChangeDetect::Process(): frame (%7d) end of scene during active blend", prevFrameNumber);
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
            dsyslog("cSceneChangeDetect::Process(): frame (%7d) end of scene", prevFrameNumber);
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
            dsyslog("cSceneChangeDetect::Process(): frame (%7d) scene blend start at frame (%d)", prevFrameNumber, blendFrame);
#endif
        }
        else sceneStatus = SCENE_BLEND;
    }
// unclear result, keep state
    else if ((diffQuote < DIFF_SCENE_BLEND_START) && (diffQuote > DIFF_SCENE_BLEND_STOP)) {
#ifdef DEBUG_SCENE_CHANGE
        if (sceneStatus == SCENE_BLEND) dsyslog("cSceneChangeDetect::Process(): frame (%7d) scene blend continue at frame (%d)", prevFrameNumber, blendFrame);
#endif
    }
// start of next scene
    else {
        if ((sceneStatus == SCENE_STOP) || ((sceneStatus == SCENE_BLEND) && (blendCount >= SCENE_BLEND_FRAMES))) {
            *changeFrameNumber = prevFrameNumber;
            sceneStatus        = SCENE_START;
#ifdef DEBUG_SCENE_CHANGE
            dsyslog("cSceneChangeDetect::Process(): frame (%7d) start of scene", prevFrameNumber);
#endif
        }
        else sceneStatus = SCENE_NOCHANGE;
        blendFrame = -1;
        blendCount =  0;
    }

    prevHistogram   = currentHistogram;
    prevFrameNumber = frameNumber;

#ifdef DEBUG_SCENE_CHANGE
    if (*changeFrameNumber >= 0) {
        if (sceneStatus == SCENE_START) dsyslog("cSceneChangeDetect::Process(): new mark: MT_SCENESTART at frame (%7d)", *changeFrameNumber);
        if (sceneStatus == SCENE_STOP)  dsyslog("cSceneChangeDetect::Process(): new mark: MT_SCENESTOP  at frame (%7d)", *changeFrameNumber);
    }
#endif

    if (*changeFrameNumber >= 0) return sceneStatus;
    else return SCENE_NOCHANGE;
}


// detect blackscreen
cBlackScreenDetect::cBlackScreenDetect(cDecoder *decoderParam, cCriteria *criteriaParam) {
    decoder  = decoderParam;
    criteria = criteriaParam;
    Clear();
}


void cBlackScreenDetect::Clear() {
    blackScreenStatus = BLACKSCREEN_UNINITIALIZED;
    lowerBorderStatus = BLACKSCREEN_UNINITIALIZED;
}


// check if current frame is a blackscreen
// return: -1 blackscreen start (notice: this is a STOP mark)
//          0 no status change
//          1 blackscreen end (notice: this is a START mark)
//
int cBlackScreenDetect::Process() {
#define BLACKNESS          19  // maximum brightness to detect a blackscreen, +1 to detect end of blackscreen, changed from 17 to 19 because of undetected black screen
#define WHITE_LOWER       220  // minimum brightness to detect white lower border
#define PIXEL_COUNT_LOWER  25  // count pixel from bottom for detetion of lower border, changed from 40 to 25
    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cVertBorderDetect::Process(): picture not valid");
        return VBORDER_ERROR;
    }
    if(!picture->plane[0]) {
        dsyslog("cVertBorderDetect::Process(): picture plane 0 not valid");
        return VBORDER_ERROR;
    }
    if(picture->planeLineSize[0] <= 0) {
        dsyslog("cVertBorderDetect::Process(): picture planeLineSize[0] valid");
        return VBORDER_ERROR;
    }

    int maxBrightnessAll;
    int maxBrightnessLower;   // for detetion of black lower border
    int minBrightnessLower;   // for detetion of white lower border

    // calculate limit with hysteresis
    if (blackScreenStatus == BLACKSCREEN_INVISIBLE) maxBrightnessAll = BLACKNESS * picture->width * picture->height;
    else maxBrightnessAll = (BLACKNESS + 1) * picture->width * picture->height;

    // limit for black lower border
    if (lowerBorderStatus == BLACKLOWER_INVISIBLE) maxBrightnessLower = BLACKNESS * picture->width * PIXEL_COUNT_LOWER;
    else maxBrightnessLower = (BLACKNESS + 1) * picture->width * PIXEL_COUNT_LOWER;

    // limit for white lower border
    if (lowerBorderStatus == BLACKLOWER_INVISIBLE) minBrightnessLower = WHITE_LOWER * picture->width * PIXEL_COUNT_LOWER;
    else minBrightnessLower = (WHITE_LOWER - 1) * picture->width * PIXEL_COUNT_LOWER;

    int maxBrightnessGrey = 28 * picture->width *picture->height;

    int valAll   = 0;
    int valLower = 0;
    int maxPixel = 0;
    // calculate blackness
    for (int x = 0; x < picture->width; x++) {
        for (int y = 0; y < picture->height; y++) {
            int pixel = picture->plane[0][x + y * picture->planeLineSize[0]];
            valAll += pixel;
            if (y > (picture->height - PIXEL_COUNT_LOWER)) valLower += pixel;
            if (pixel > maxPixel) maxPixel = pixel;
        }
    }

#ifdef DEBUG_BLACKSCREEN
    int debugValAll   = valAll   / (picture.width * maContext->Video.Info.height);
    int debugValLower = valLower / (picture.width * PIXEL_COUNT_LOWER);
    dsyslog("cBlackScreenDetect::Process(): frame (%d): blackScreenStatus %d, blackness %3d (expect <%d for start, >%d for end), lowerBorderStatus %d, lower %3d", frameCurrent, blackScreenStatus, debugValAll, BLACKNESS, BLACKNESS, lowerBorderStatus, debugValLower);
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


cHorizBorderDetect::cHorizBorderDetect(cDecoder *decoderParam, cCriteria *criteriaParam) {
    decoder      = decoderParam;
    criteria     = criteriaParam;
    frameRate    = decoder->GetVideoFrameRate();
    logoInBorder = criteria->LogoInBorder();
    infoInBorder = criteria->InfoInBorder();
    Clear();
}


cHorizBorderDetect::~cHorizBorderDetect() {
}


int cHorizBorderDetect::GetFirstBorderFrame() const {
    if (borderstatus != HBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cHorizBorderDetect::State() const {
    return borderstatus;
}


void cHorizBorderDetect::Clear(const bool isRestart) {
    dsyslog("cHorizBorderDetect::Clear():  clear hborder state");
    if (isRestart) borderstatus = HBORDER_RESTART;
    else           borderstatus = HBORDER_UNINITIALIZED;
    borderframenumber = -1;
}


int cHorizBorderDetect::Process(int *borderFrame) {
#define CHECKHEIGHT           5  // changed from 8 to 5
#define BRIGHTNESS_H_SURE    22
#define BRIGHTNESS_H_MAYBE  137  // some channel have logo or infos in border, so we will detect a higher value, changed from 131 to 137
#define NO_HBORDER          200  // internal limit for early loop exit, must be more than BRIGHTNESS_H_MAYBE

    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cVertBorderDetect::Process(): picture not valid");
        return VBORDER_ERROR;
    }
    if(!picture->plane[0]) {
        dsyslog("cVertBorderDetect::Process(): picture plane 0 not valid");
        return VBORDER_ERROR;
    }
    if(picture->planeLineSize[0] <= 0) {
        dsyslog("cVertBorderDetect::Process(): picture planeLineSize[0] valid");
        return VBORDER_ERROR;
    }

    // set limits
    int brightnessSure  = BRIGHTNESS_H_SURE;
    if (logoInBorder) brightnessSure = BRIGHTNESS_H_SURE + 1;  // for pixel from logo
    int brightnessMaybe = BRIGHTNESS_H_SURE;
    if (infoInBorder) brightnessMaybe = BRIGHTNESS_H_MAYBE;    // for pixel from info in border

    *borderFrame = -1;   // framenumber of first hborder, otherwise -1
    int height = picture->height;

    int start     = (height - CHECKHEIGHT) * picture->planeLineSize[0];
    int end       = height * picture->planeLineSize[0];
    int valTop    = 0;
    int valBottom = 0;
    int cnt       = 0;
    int xz        = 0;

    for (int x = start; x < end; x++) {
        if (xz < picture->width) {
            valBottom += picture->plane[0][x];
            cnt++;
        }
        xz++;
        if (xz >= picture->planeLineSize[0]) xz = 0;
    }
    valBottom /= cnt;

    // if we have a bottom border, test top border
    if (valBottom <= brightnessMaybe) {
        start = picture->planeLineSize[0];
        end = picture->planeLineSize[0] * CHECKHEIGHT;
        cnt = 0;
        xz  = 0;
        for (int x = start; x < end; x++) {
            if (xz < picture->width) {
                valTop += picture->plane[0][x];
                cnt++;
            }
            xz++;
            if (xz >= picture->planeLineSize[0]) xz = 0;
        }
        valTop /= cnt;
    }
    else valTop = NO_HBORDER;   // we have no botton border, so we do not have to calculate top border

#ifdef DEBUG_HBORDER
    dsyslog("cHorizBorderDetect::Process(): frame (%7d) hborder brightness top %4d bottom %4d (expect one <=%d and one <= %d)", FrameNumber, valTop, valBottom, brightnessSure, brightnessMaybe);
#endif

    if ((valTop <= brightnessMaybe) && (valBottom <= brightnessSure) || (valTop <= brightnessSure) && (valBottom <= brightnessMaybe)) {
        // hborder detected
#ifdef DEBUG_HBORDER
        int duration = (FrameNumber - borderframenumber) / decoder->GetVideoFrameRate();
        dsyslog("cHorizBorderDetect::Process(): frame (%7d) hborder ++++++: borderstatus %d, borderframenumber (%d), duration %ds", picture->frameNumber, borderstatus, borderframenumber, duration);
#endif
        if (borderframenumber == -1) {  // got first frame with hborder
            borderframenumber = picture->frameNumber;
        }
        if (borderstatus != HBORDER_VISIBLE) {
            if (picture->frameNumber > (borderframenumber + frameRate * MIN_H_BORDER_SECS)) {
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
        dsyslog("cHorizBorderDetect::Process(): frame (%7d) hborder ------: borderstatus %d, borderframenumber (%d)", picture->frameNumber, borderstatus, borderframenumber);
#endif
        if (borderstatus != HBORDER_INVISIBLE) {
            if ((borderstatus == HBORDER_UNINITIALIZED) || (borderstatus == HBORDER_RESTART)) *borderFrame = -1;  // do not report back a border change after detection restart, only set internal state
            else *borderFrame = borderframenumber;
            borderstatus = HBORDER_INVISIBLE; // detected stop of black border
        }
        borderframenumber = -1; // restart from scratch
    }
#ifdef DEBUG_HBORDER
    dsyslog("cHorizBorderDetect::Process(): frame (%7d) hborder return: borderstatus %d, borderframenumber (%d), borderFrame (%d)", FrameNumber, borderstatus, borderframenumber, *borderFrame);
#endif
    return borderstatus;
}


cVertBorderDetect::cVertBorderDetect(cDecoder *decoderParam, cCriteria *criteriaParam) {
    decoder      = decoderParam;
    criteria     = criteriaParam;
    frameRate    = decoder->GetVideoFrameRate();
    logoInBorder = criteria->LogoInBorder();
    infoInBorder = criteria->InfoInBorder();
    Clear();
}


void cVertBorderDetect::Clear(const bool isRestart) {
    if (isRestart) borderstatus = VBORDER_RESTART;
    else           borderstatus = VBORDER_UNINITIALIZED;
    borderframenumber = -1;
    darkFrameNumber   = INT_MAX;
}


int cVertBorderDetect::GetFirstBorderFrame() const {
    if (borderstatus != VBORDER_VISIBLE) return borderframenumber;
    else return -1;
}


int cVertBorderDetect::Process(int *borderFrame) {
#define CHECKWIDTH 10           // do not reduce, very small vborder are unreliable to detect, better use logo in this case
#define BRIGHTNESS_V_SURE   27  // changed from 33 to 27, some channels has dark separator before vborder start
#define BRIGHTNESS_V_MAYBE 101  // some channel have logo or infos in one border, so we must accept a higher value, changed from 100 to 101
    sVideoPicture *picture = decoder->GetVideoPicture();
    if (!picture) {
        dsyslog("cVertBorderDetect::Process(): picture not valid");
        return VBORDER_ERROR;
    }
    if(!picture->plane[0]) {
        dsyslog("cVertBorderDetect::Process(): picture plane 0 not valid");
        return VBORDER_ERROR;
    }
    if(picture->planeLineSize[0] <= 0) {
        dsyslog("cVertBorderDetect::Process(): picture planeLineSize[0] valid");
        return VBORDER_ERROR;
    }
    if (!borderFrame) {
        dsyslog("cVertBorderDetect::Process(): borderFrame not valid");
        return VBORDER_ERROR;
    }
    if (picture->width == 0) {
        dsyslog("cVertBorderDetect::Process(): picture width %d not valid", picture->width);
    }
    if (picture->height == 0) {
        dsyslog("cVertBorderDetect::Process(): picture height %d not valid", picture->height);
    }
    if (frameRate == 0) {
        dsyslog("cVertBorderDetect::Process(): video frames per second  not valid");
        return VBORDER_ERROR;
    }

    // set limits
    int brightnessSure  = BRIGHTNESS_V_SURE;
    if (logoInBorder) brightnessSure = BRIGHTNESS_V_SURE + 1;  // for pixel from logo
    int brightnessMaybe = BRIGHTNESS_V_SURE;
    if (infoInBorder) brightnessMaybe = BRIGHTNESS_V_MAYBE;    // for pixel from info in border

    *borderFrame = -1;
    int valLeft  =  0;
    int valRight =  0;
    int cnt      =  0;


    // check left border
    for (int y = 0; y < picture->height; y++) {
        for (int x = 0; x < CHECKWIDTH; x++) {
            valLeft += picture->plane[0][x + (y * picture->planeLineSize[0])];
            cnt++;
        }
    }
    valLeft /= cnt;

    // check right border
    if (valLeft <= brightnessMaybe) {
        cnt = 0;
        for (int y = 0; y < picture->height; y++) {
            for (int x = picture->width - CHECKWIDTH; x < picture->width; x++) {
                valRight += picture->plane[0][x + (y * picture->planeLineSize[0])];
                cnt++;
            }
        }
        valRight /= cnt;
    }
    else valRight = INT_MAX;  // left side has no border, so we have not to check right side

#ifdef DEBUG_VBORDER
    dsyslog("cVertBorderDetect::Process(): frame (%7d): vborder status: %d, valLeft: %10d, valRight: %10d", frameNumber, borderstatus, valLeft, valRight);
    if (darkFrameNumber < INT_MAX) dsyslog("cVertBorderDetect::Process(): frame (%7d):  frist vborder in dark picture: (%5d)", frameNumber, darkFrameNumber);
    if (borderframenumber >= 0) dsyslog("cVertBorderDetect::Process(): frame (%7d):  frist vborder: [bright (%5d), dark (%5d)], duration: %ds", frameNumber, borderframenumber, darkFrameNumber, static_cast<int> ((frameNumber - borderframenumber) / frameRate));
#endif
#define BRIGHTNESS_MIN (38 * picture->height * picture->width)  // changed from 51 to 38, dark part with vborder
    if (((valLeft <= brightnessMaybe) && (valRight <= brightnessSure)) || ((valLeft <= brightnessSure) && (valRight <= brightnessMaybe))) {
        // vborder detected
        if (borderframenumber == -1) {   // first vborder detected
            // prevent false detection in a very dark scene, we have to get at least one vborder in a bright picture to accept start frame
#ifndef DEBUG_VBORDER
            bool end_loop   = false;
#endif
            int  brightness = 0;
            for (int y = 0; y < picture->height ; y++) {
                for (int x = 0; x < picture->width; x++) {
                    brightness += picture->plane[0][x + (y * picture->planeLineSize[0])];
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
                darkFrameNumber = std::min(darkFrameNumber, picture->frameNumber);  // set to first frame with vborder
#ifdef DEBUG_VBORDER
                minBrightness = std::min(brightness, minBrightness);
                maxBrightness = std::max(brightness, maxBrightness);
                dsyslog("cVertBorderDetect::Process(): frame (%7d) has a dark picture: %d, delay vborder start at (%7d), minBrightness %d, maxBrightness %d", picture->frameNumber, brightness, darkFrameNumber, minBrightness, maxBrightness);
#endif
            }
            else {
                borderframenumber = std::min(picture->frameNumber, darkFrameNumber);      // use first vborder
#ifdef DEBUG_VBORDER
                minBrightness = INT_MAX;
                maxBrightness = 0;
                dsyslog("cVertBorderDetect::Process(): frame (%7d) has a bright picture %d, accept vborder start at (%7d)", picture->frameNumber, brightness, borderframenumber);
#endif
                darkFrameNumber = INT_MAX;
            }
        }
        if (borderstatus != VBORDER_VISIBLE) {
            if ((borderframenumber >= 0) && (picture->frameNumber > (borderframenumber + frameRate * MIN_V_BORDER_SECS))) {
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
            else *borderFrame = picture->frameNumber;
            borderstatus = VBORDER_INVISIBLE; // detected stop of black border
        }
        borderframenumber = -1; // restart from scratch
        darkFrameNumber = INT_MAX;
    }
    return borderstatus;
}


cVideo::cVideo(cDecoder *decoderParam, cCriteria *criteriaParam, const char *recDirParam, const char *logoCacheDirParam) {
    dsyslog("cVideo::cVideo(): new object");
    decoder      = decoderParam;
    criteria     = criteriaParam;
    recDir       = recDirParam;
    logoCacheDir = logoCacheDirParam;

    sceneChangeDetect = new cSceneChangeDetect(decoder, criteria);
    ALLOC(sizeof(*sceneChangeDetect), "sceneChangeDetect");

    blackScreenDetect = new cBlackScreenDetect(decoder, criteria);
    ALLOC(sizeof(*blackScreenDetect), "blackScreenDetect");

    hBorderDetect = new cHorizBorderDetect(decoder, criteria);
    ALLOC(sizeof(*hBorderDetect), "hBorderDetect");

    vBorderDetect = new cVertBorderDetect(decoder, criteria);
    ALLOC(sizeof(*vBorderDetect), "vBorderDetect");

    logoDetect = new cLogoDetect(decoder, criteria, recDir, logoCacheDir);
    ALLOC(sizeof(*logoDetect), "logoDetect");
}


cVideo::~cVideo() {
    dsyslog("cVideo::cVideo(): delete object");
    if (sceneChangeDetect) {
        FREE(sizeof(*sceneChangeDetect), "sceneChangeDetect");
        delete sceneChangeDetect;
    }
    if (blackScreenDetect) {
        FREE(sizeof(*blackScreenDetect), "blackScreenDetect");
        delete blackScreenDetect;
    }
    if (hBorderDetect) {
        FREE(sizeof(*hBorderDetect), "hBorderDetect");
        delete hBorderDetect;
    }
    if (vBorderDetect) {
        FREE(sizeof(*vBorderDetect), "vBorderDetect");
        delete vBorderDetect;
    }
    if (logoDetect) {
        FREE(sizeof(*logoDetect), "logoDetect");
        delete logoDetect;
    }
}


int cVideo::GetLogoCorner () const {
    return logoDetect->GetLogoCorner();
}

void cVideo::Clear(const bool isRestart) {
    dsyslog("cVideo::Clear(): reset detection status, isRestart = %d", isRestart);
    if (!isRestart) {
        aspectRatioFrameBefore = {0};
    }
    if (isRestart) {  // only clear if detection is disabled
        if (blackScreenDetect && !criteria->GetDetectionState(MT_BLACKCHANGE))   blackScreenDetect->Clear();
        if (logoDetect        && !criteria->GetDetectionState(MT_LOGOCHANGE))    logoDetect->Clear(true);
        if (vBorderDetect     && !criteria->GetDetectionState(MT_VBORDERCHANGE)) vBorderDetect->Clear(true);
        if (hBorderDetect     && !criteria->GetDetectionState(MT_HBORDERCHANGE)) hBorderDetect->Clear(true);
    }
    else {
        if (blackScreenDetect) blackScreenDetect->Clear();
        if (logoDetect)        logoDetect->Clear(false);
        if (vBorderDetect)     vBorderDetect->Clear(false);
        if (hBorderDetect)     hBorderDetect->Clear(false);
    }
}


void cVideo::ClearBorder() {
    dsyslog("cVideo::ClearBorder(): reset border detection status");
    if (vBorderDetect) vBorderDetect->Clear();
    if (hBorderDetect) hBorderDetect->Clear();
}


bool cVideo::AddMark(int type, int position, const sAspectRatio *before, const sAspectRatio *after) {
    if (videoMarks.Count >= videoMarks.maxCount) {  // array start with 0
        esyslog("cVideo::AddMark(): too much marks %d at once detected", videoMarks.Count);
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


void cVideo::SetAspectRatioBroadcast(sAspectRatio aspectRatio) {
    dsyslog("cVideo::SetAspectRatioBroadcast(): set assumed broadcast aspect ratio to %d:%d", aspectRatio.num, aspectRatio.den);
    aspectRatioBroadcast = aspectRatio;
}


sMarkAdMarks *cVideo::Process() {
    int frameNumber = decoder->GetVideoFrameNumber();
    videoMarks = {};   // reset array of new marks

    // scene change detection
    if (criteria->GetDetectionState(MT_SCENECHANGE)) {
        int changeFrame = -1;
        int sceneRet = sceneChangeDetect->Process(&changeFrame);
        if (sceneRet == SCENE_START) AddMark(MT_SCENESTART, changeFrame);
        if (sceneRet == SCENE_STOP)  AddMark(MT_SCENESTOP,  changeFrame);
    }

    // black screen change detection
    if ((frameNumber > 0) && criteria->GetDetectionState(MT_BLACKCHANGE)) { // first frame can be invalid result
        int blackret = blackScreenDetect->Process();
        switch (blackret) {
        case BLACKSCREEN_INVISIBLE:
            AddMark(MT_NOBLACKSTART, frameNumber);  // first frame without blackscreen is start mark position
            break;
        case BLACKSCREEN_VISIBLE:
            AddMark(MT_NOBLACKSTOP,  frameNumber);
            break;
        case BLACKLOWER_INVISIBLE:
            AddMark(MT_NOLOWERBORDERSTART, frameNumber);  // first frame without lower border is start mark position
            break;
        case BLACKLOWER_VISIBLE:
            AddMark(MT_NOLOWERBORDERSTOP,  frameNumber);
            break;
        default:
            break;
        }
    }

    // hborder change detection
    if (criteria->GetDetectionState(MT_HBORDERCHANGE)) {
        int hborderframenumber;
        int hret = hBorderDetect->Process(&hborderframenumber);  // we get start frame of hborder back
        if ((hret == HBORDER_VISIBLE) && (hborderframenumber >= 0)) {
            AddMark(MT_HBORDERSTART, hborderframenumber);
        }
        if ((hret == HBORDER_INVISIBLE) && (hborderframenumber >= 0)) {
            AddMark(MT_HBORDERSTOP, frameNumber - 1);
        }
    }
    else {
        if (hBorderDetect && (hBorderDetect->State() != HBORDER_UNINITIALIZED)) hBorderDetect->Clear();
    }

    // vborder change detection
    if (criteria->GetDetectionState(MT_VBORDERCHANGE)) {
        int vborderframenumber;

        int vret = vBorderDetect->Process(&vborderframenumber);
        if ((vret == VBORDER_VISIBLE) && (vborderframenumber >= 0)) AddMark(MT_VBORDERSTART, vborderframenumber);
        if ((vret == VBORDER_INVISIBLE) && (vborderframenumber >= 0)) AddMark(MT_VBORDERSTOP, frameNumber - 1);
    }
    else if (vBorderDetect) vBorderDetect->Clear();

    // aspect ratio change detection
    if (criteria->GetDetectionState(MT_ASPECTCHANGE)) {
        // get aspect ratio from frame
        sAspectRatio *aspectRatioFrame = decoder->GetFrameAspectRatio();
        if (!aspectRatioFrame) {
            esyslog("cVideo::Process(): frame (%d): get aspect ratio failed", frameNumber);
            return nullptr;
        }
        if (aspectRatioFrameBefore != *aspectRatioFrame) {
            if ((aspectRatioBroadcast.num == 4) && (aspectRatioBroadcast.den == 3)) {
                if ((aspectRatioFrame->num == 4) && (aspectRatioFrame->den == 3)) AddMark(MT_ASPECTSTART, frameNumber,     &aspectRatioFrameBefore, aspectRatioFrame);
                else                                                              AddMark(MT_ASPECTSTOP,  frameNumber - 1, &aspectRatioFrameBefore, aspectRatioFrame);
            }
            else {
                if ((aspectRatioBroadcast.num == 16) && (aspectRatioBroadcast.den == 9)) {
                    // do not set a initial 16:9 aspect ratio start mark for 16:9 videos, not useful
                    if ((aspectRatioFrameBefore.num != 0) || (aspectRatioFrameBefore.den != 0)) AddMark(MT_ASPECTSTART, aspectRatioBeforeFrame, &aspectRatioFrameBefore, aspectRatioFrame);
                }
                else {
                    AddMark(MT_ASPECTSTOP, frameNumber - 1, &aspectRatioFrameBefore, aspectRatioFrame);  // stop is one frame before aspect ratio change
                    // 16:9 -> 4:3, this is end of broadcast (16:9) and start of next broadcast (4:3)
                    // if we have activ hborder add hborder stop mark, because hborder state will be cleared
                    if (hBorderDetect->State() == HBORDER_VISIBLE) {
                        dsyslog("cVideo::Process(): hborder activ during aspect ratio change from 16:9 to 4:3, add hborder stop mark");
                        AddMark(MT_HBORDERSTOP, frameNumber - 2);
                    }
                }
            }
            aspectRatioFrameBefore = *aspectRatioFrame;   // store new aspect ratio
        }
    }

    // logo change detection
    if (criteria->GetDetectionState(MT_LOGOCHANGE)) {
        int logoframenumber = 0;
        int lret=logoDetect->Process(&logoframenumber);
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
        logoDetect->SetStatusUninitialized();
    }

    if (videoMarks.Count > 0) {
        return &videoMarks;
    }
    else {
        return nullptr;
    }
}


// disable colored planes
bool cVideo::ReducePlanes() {
    if (!logoDetect) return false;
    sAreaT *area = logoDetect->GetArea();
    bool ret = false;
    for (int plane = 1; plane < PLANES; plane++) {
        area->logo[plane] = nullptr;
        area->mPixel[plane] = 0;
        area->rPixel[plane] = 0;
        area->iPixel[plane] = 0;
        ret = true;
    }
    return ret;
}
