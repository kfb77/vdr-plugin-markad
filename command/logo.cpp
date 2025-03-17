/*
 * logo.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "logo.h"   // include global.h via logo.h first to define POSIX

#ifdef POSIX
#include <sys/stat.h>
#include <unistd.h>
#else
#include "win32/mingw64.h"
#endif

#include <sys/time.h>

// based on this idee to find the logo in a recording:
// 1. take 1000 iframes
// 2. compare each corner of the iframes with all other iframes of the same corner
// 3. take the iframe who has the most similar frame on the same corner, this hopefully should be the logo
// 4. remove the white frame from the logo
// 5. store the logo files in the recording directory for future use

// logo size limits
#define LOGO_MIN_LETTERING_H 38 // 41 for "DIE NEUEN FOLGEN" SAT_1
// 38 for "#wir bleiben zuhause" RTL2
#define LOGO_MAX_LETTERING_H 80 // ntv-hd logo has 79 pixel valiable banner above, changed from 56 to 80

// global variables
extern bool abortNow;
extern int logoSearchTime_ms;


cExtractLogo::cExtractLogo(const char *recDirParam, const char *channelNameParam, const int threads, const bool fullDecodeParam, char *hwaccel, const bool forceHW, const sAspectRatio requestedAspectRatio) {
    LogSeparator(true);
    recDir      = recDirParam;
    channelName = channelNameParam;
    fullDecode  = fullDecodeParam;

    // requested aspect ratio
    // requestedLogoAspectRatio = requestedAspectRatio;  avoid cppceck warning:
    requestedLogoAspectRatio.num = requestedAspectRatio.num;
    requestedLogoAspectRatio.den = requestedAspectRatio.den;

    // create all used objects
    decoder = new cDecoder(recDir, threads, fullDecode, hwaccel, forceHW, false, nullptr);    // recDir, threads, fullDecode, hwaccel, forceHW, forceInterlace, index
    ALLOC(sizeof(*decoder), "decoder");

    criteria = new cCriteria(channelName);
    ALLOC(sizeof(*criteria), "criteria");

    // open first file to init decoder
    if (decoder->ReadNextFile()) {
        // create object for sobel transformation
        sobel = new cSobel(decoder->GetVideoWidth(), decoder->GetVideoHeight(), 6);  // boundary 6
        ALLOC(sizeof(*sobel), "sobel");

        hBorder = new cHorizBorderDetect(decoder, nullptr, criteria);
        ALLOC(sizeof(*hBorder), "hBorder");

        vborder = new cVertBorderDetect(decoder, nullptr, criteria);  // no index
        ALLOC(sizeof(*vborder), "vborder");
    }
}


cExtractLogo::~cExtractLogo() {
    for (int corner = 0; corner < CORNERS; corner++) {  // free memory of all corners
#ifdef DEBUG_MEM
        int size = logoInfoVector[corner].size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(sLogoInfo), "logoInfoVector");
        }
#endif
        // free memory of sobel plane vector
        for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            for (int plane = 0; plane < PLANES; plane++) {
                delete[] actLogo->sobel[plane];
            }
            delete[] actLogo->sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area.logoSize.height * area.logoSize.width, "actLogoInfo.sobel");
        }
        logoInfoVector[corner].clear();
    }
    // cleanup used objects
    FREE(sizeof(*decoder), "decoder");
    delete decoder;

    FREE(sizeof(*criteria), "criteria");
    delete criteria;

    if ((area.logoSize.height > 0) && (area.logoSize.width > 0)) sobel->FreeAreaBuffer(&area);  // free memory for result buffer, for channel without logo, we have no buffer allocated
    FREE(sizeof(*sobel), "sobel");
    delete sobel;

    FREE(sizeof(*hBorder), "hBorder");
    delete hBorder;

    FREE(sizeof(*vborder), "vborder");
    delete vborder;

    LogSeparator(true);
}


bool cExtractLogo::IsWhitePlane(const sLogoInfo *actLogoInfo, const sLogoSize logoSizePlane, const int plane) {
    if (!actLogoInfo)                     return false;
    if (logoSizePlane.width  <= 0)        return false;
    if (logoSizePlane.height <= 0)        return false;
    if ((plane < 0) || (plane >= PLANES)) return false;

    int countBlack = 0;
    for (int line = 0; line < logoSizePlane.height; line++) {
        for (int column = 0; column < logoSizePlane.width; column++) {
            if (actLogoInfo->sobel[plane][line * (logoSizePlane.width) + column] == 0) {
                countBlack++;
                if (countBlack >= 60) {
                    return false;   // only if there are some pixel, changed from 5 to 60
                }
            }
        }
    }
    return true;
}


// check plane if the is a logo colour schnage
// calculate quote of white pictures
// return: true if only some frames have pixels in plane >=1, a channel with logo coulor change is detected
//         false if almost all frames have pixel in plane >=1, this is realy a coloured logo
//
bool cExtractLogo::IsLogoColourChange(const sLogoSize *logoSizeFinal, const int corner, const int plane) {
    if (!logoSizeFinal) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;

    sLogoSize logoSizePlane = area.logoSize;   // only find logo was resized, have to heck full sobel transformed sized
    logoSizePlane.height /= 2;  // we use plane > 1 to check
    logoSizePlane.width  /= 2;

    int count = 0;
    int countWhite = 0;

    for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
        count++;
        if (IsWhitePlane(&(*actLogo), logoSizePlane, plane)) {
            countWhite++;
        }
    }
    if (count > 0) {
        dsyslog("cExtractLogo::isLogoColourChange(): %4d valid frames in corner %d, plane %d: %3d are white, ratio %3d%%", count, corner, plane, countWhite, countWhite * 100 / count);
        if ((100 * countWhite / count) >= 20) return true;  // changed from 27 to 20, prevent to get colour planes for transparent logo
    }
    return false;
}


bool cExtractLogo::SaveLogo(const sLogoInfo *actLogoInfo, sLogoSize *logoSizeFinal, const sAspectRatio logoAspectRatio, const int corner) {
    if (!actLogoInfo)               return false;
    if (!logoSizeFinal)             return false;
    if (logoSizeFinal->height <= 0) return false;
    if (logoSizeFinal->width  <= 0) return false;
    if (corner <  0)                return false;
    if (corner >= CORNERS)          return false;
    if (!channelName)               return false;

    int blackPlane0 = 0;
    for (int plane = 0; plane < PLANES; plane++) {
        // pixel count of logo
        int height = logoSizeFinal->height;
        int width  = logoSizeFinal->width;
        if (plane > 0) {
            width  /= 2;
            height /= 2;
        }
        int black = 0;
        for (int i = 0; i < height * width; i++) {
            if (actLogoInfo->sobel[plane][i] == 0) black++;
        }
        if (plane == 0) blackPlane0 = black;
        dsyslog("cExtractLogo::Save(): %d pixel in plane %d", black, plane);
        // if we have transparent logo do not save colored plane, they are from background
        if (plane == 0) {
            if (black < 200) {
                dsyslog("cExtractLogo::Save(): not enough pixel (%d) in plane 0, logo invalid, continue search", black);  // invalid logo after resize, continue search
                return false;
            }
        }
        else {
            if (IsLogoColourChange(logoSizeFinal, corner, plane)) {
                dsyslog("cExtractLogo::Save(): logo is transparent or changed color, do not save plane %d", plane);
                continue;
            }
            // do not save planes with too less pixel, detection will not work
            if ((black < 200) || (black < (blackPlane0 / 10))) {  // do not increase, will loss red krone.tv logo
                dsyslog("cExtractLogo::Save(): not enough pixel (%d) in plane %d", black, plane);
                continue;
            }
            else dsyslog("cExtractLogo::Save(): got enough pixel (%d) in plane %d", black, plane);
        }

        // save logo
        char *buf = nullptr;
        if (asprintf(&buf, "%s/%s-A%d_%d-P%d.pgm", recDir, channelName, logoAspectRatio.num, logoAspectRatio.den, plane)==-1) return false;
        ALLOC(strlen(buf)+1, "buf");
        dsyslog("cExtractLogo::Save(): store logo plane %d in %s", plane, buf);
        if (plane == 0) isyslog("logo found for channel: %s %d:%d %dW %dH: %dW %dH %s", channelName, logoAspectRatio.num, logoAspectRatio.den, decoder->GetVideoWidth(), decoder->GetVideoHeight(), logoSizeFinal->width, logoSizeFinal->height, aCorner[corner]);
        // Open file
        FILE *pFile=fopen(buf, "wb");
        if (pFile==nullptr)
        {
            FREE(sizeof(buf), "buf");
            free(buf);
            dsyslog("cExtractLogo::Save(): open file failed");
            return false;
        }
        // Write header
        fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", corner, width, height);

        // Write pixel data
        if (!fwrite(actLogoInfo->sobel[plane], 1, width * height, pFile)) {
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


void cExtractLogo::CutOut(sLogoInfo *logoInfo, int cutPixelH, int cutPixelV, sLogoSize *logoSizeFinal, const int corner) const {
    if (!logoInfo)      return;
    if (!logoSizeFinal) return;
    if ((corner < 0) || (corner >= CORNERS)) return;
    if ((cutPixelH == 0) && (cutPixelV == 0)) {
        esyslog("cExtractLogo::CutOut(): cut out 0 pixel not valid");
        return;
    }
    if (cutPixelV >= logoSizeFinal->width) {
        esyslog("cExtractLogo::CutOut(): cut %d pixel from logo with width %d not valid", cutPixelV, logoSizeFinal->width);
        return;
    }
    if (cutPixelH >= logoSizeFinal->height) {
        esyslog("cExtractLogo::CutOut(): cut %d pixel from logo with height %d not valid", cutPixelH, logoSizeFinal->height);
        return;
    }

// plane 0 should have even pixel, we ve to calculate half of pixel for plane 1 and 2 without rest, accept one empty line
    if (cutPixelH % 2) cutPixelH--;
    if (cutPixelV % 2) cutPixelV--;

    int heightPlane_1_2 = logoSizeFinal->height / 2;
    int widthPlane_1_2  = logoSizeFinal->width  / 2;

    if (cutPixelH > 0) {
        dsyslog("cExtractLogo::CutOut(): cut out %3dp lines horizontal and %3dp column vertical", cutPixelH, cutPixelV);
        if (corner <= TOP_RIGHT) {  // top corners, cut from below
        }
        else { // bottom corners, cut from above
            for (int i = 0; i < (logoSizeFinal->height - cutPixelH) * logoSizeFinal->width; i++) {
                logoInfo->sobel[0][i] = logoInfo->sobel[0][i + cutPixelH * (logoSizeFinal->width)];
            }
        }
        logoSizeFinal->height -= cutPixelH;
    }

    if (cutPixelV > 0) {
        dsyslog("cExtractLogo::CutOut(): cut out %3dp lines horizontal and %3dp column vertical", cutPixelH, cutPixelV);
        if ((corner == TOP_RIGHT) || (corner == BOTTOM_RIGHT)) {  // right corners, cut from left
            for (int i = 0; i < logoSizeFinal->height * (logoSizeFinal->width - cutPixelV); i++) {
                logoInfo->sobel[0][i] =  logoInfo->sobel[0][i + cutPixelV * (1 + (i / (logoSizeFinal->width - cutPixelV)))];
            }
        }
        else { // left corners, cut from right
            for (int i = 0; i < logoSizeFinal->height * (logoSizeFinal->width - cutPixelV); i++) {
                logoInfo->sobel[0][i] =  logoInfo->sobel[0][i + cutPixelV * (i / (logoSizeFinal->width - cutPixelV))];
            }
        }
        logoSizeFinal->width -= cutPixelV;
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
#ifdef DEBUG_LOGO_RESIZE
    dsyslog("cExtractLogo::CutOut(): logo size after cut out:    %3d width %3d height on corner %12s", logoSizeFinal->width, logoSizeFinal->height, aCorner[corner]);
#endif
}


bool cExtractLogo::CheckLogoSize(sLogoSize *logoSizeFinal, const int logoCorner) {
    if (!logoSizeFinal) return false;
    struct logo_struct {
        int widthMin  =  0;
        int widthMax  =  0;
        int heightMin =  0;
        int heightMax =  0;
        int corner    = -1;
    } logo;
// define size of logos to prevent false logo detection
    switch (decoder->GetVideoWidth()) {

    case 544:
        // default values
        if (logo.widthMin  == 0) logo.widthMin  =  72; // TLC_Austria             16:9  544W  576H:->   72W  60H TOP_LEFT
        if (logo.widthMax  == 0) logo.widthMax  =  98; // DMAX_Austria            16:9  544W  576H:->   98W  90H TOP_LEFT
        if (logo.heightMin == 0) logo.heightMin =  60; // TLC_Austria             16:9  544W  576H:->   72W  60H TOP_LEFT
        if (logo.heightMax == 0) logo.heightMax =  91; // DMAX_Austria            16:9  544W  576H:->   98W  90H TOP_LEFT
        break;

    case 720:
        if (CompareChannelName(channelName, "arte", IGNORE_NOTHING)) {                 // arte                    16:9  720W  576H:->   50W 108H TOP_LEFT
            logo.widthMin  =  50;
            logo.heightMax = 108;
        }

        // ATV2                    16:9  720W  576H:->  118W  68H TOP_LEFT
        if (CompareChannelName(channelName, "ATV2", IGNORE_NOTHING)) {
            logo.widthMin  = 113;
            logo.widthMax  = 123;
            logo.heightMin =  63;
            logo.heightMax =  73;
        }
//   4 Comedy_Central          16:9  720W  576H:->  126W  74H TOP_LEFT
//   2 Comedy_Central          16:9  720W  576H:->  126W  76H TOP_LEFT
//  60 Comedy_Central          16:9  720W  576H:->  126W  78H TOP_LEFT
//  17 Comedy_Central          16:9  720W  576H:->  126W  80H TOP_LEFT
//   1 Comedy_Central          16:9  720W  576H:->  142W  80H TOP_LEFT
//   1 Comedy_Central          16:9  720W  576H:->  212W  78H TOP_LEFT
//   1 Comedy_Central          16:9  720W  576H:->  214W  80H TOP_LEFT
//   1 Comedy_Central          16:9  720W  576H:->  118W  86H TOP_RIGHT   -> logo from ad
        if (CompareChannelName(channelName, "Comedy_Central", IGNORE_NOTHING)) {
            logo.widthMin  = 119;
            logo.widthMax  = 224;
            logo.heightMin =  64;
            logo.heightMax =  90;
        }

        // Disney_Channel          16:9  720W  576H:->  110W  70H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  110W  72H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  110W  74H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  122W  76H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  122W  78H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  122W  80H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  122W  82H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  122W  88H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  152W 106H TOP_LEFT
        // Disney_Channel          16:9  720W  576H:->  152W 108H TOP_LEFT
        // Disney_Channel           4:3  720W  576H:->  124W  70H TOP_LEFT
        // Disney_Channel           4:3  720W  576H:->  124W  72H TOP_LEFT
        // Disney_Channel           4:3  720W  576H:->  124W  74H TOP_LEFT
        // Disney_Channel           4:3  720W  576H:->  126W  74H TOP_LEFT
        if (CompareChannelName(channelName, "Disney_Channel", IGNORE_NOTHING)) {
            logo.widthMin  = 110;
            logo.widthMax  = 152;
            logo.heightMin =  70;
            logo.heightMax = 108;
            logo.corner    = TOP_LEFT;                                           // "neue Folge" size matches as valid logo
        }

        // DMAX                    16:9  720W  576H:->  126W  74H TOP_LEFT
        // DMAX                    16:9  720W  576H:->  126W  76H TOP_LEFT
        // DMAX                    16:9  720W  576H:->  128W  76H TOP_LEFT
        if (CompareChannelName(channelName, "DMAX", IGNORE_NOTHING)) {
            logo.widthMin  = 126;
            logo.heightMax =  76;
            logo.corner    = TOP_LEFT;   // "neue Folge" has same size as logo
        }

//   1 HGTV                    16:9  720W  576H:->  110W  78H TOP_LEFT
        if (CompareChannelName(channelName, "HGTV", IGNORE_NOTHING)) {
            logo.widthMin  = 100;
            logo.widthMax  = 120;
            logo.heightMin =  68;
            logo.heightMax =  88;
        }

        //  K-TV                    16:9  720W  576H:->  122W  78H TOP_RIGHT
        if (CompareChannelName(channelName, "K-TV", IGNORE_NOTHING)) {
            logo.widthMin  =  122;
        }

        // kabel_eins              16:9  720W  576H:->   88W  72H TOP_RIGHT
        if (CompareChannelName(channelName, "kabel_eins", IGNORE_NOTHING)) {
            logo.widthMax  =  88;
            logo.heightMin =  72;
        }

        // kabel_eins_Doku         16:9  720W  576H:->  132W  64H TOP_RIGHT
        if (CompareChannelName(channelName, "kabel_eins_Doku", IGNORE_NOTHING)) {
            logo.widthMin  = 130;
            logo.widthMax  = 132;
            logo.heightMin =  64;
        }

        // krone_tv                16:9  720W  576H:->   64W  74H TOP_RIGHT
        if (CompareChannelName(channelName, "krone_tv", IGNORE_NOTHING)) {
            logo.widthMax  =  64;
            logo.heightMax =  74;
        }

//     Nickelodeon             16:9  720W  576H:    150W  94H TOP_LEFT   -> invalid ad logo
//
//   1 Nickelodeon             16:9  720W  576H:->  144W  90H TOP_LEFT
//   6 Nickelodeon             16:9  720W  576H:->  146W  88H TOP_LEFT
//   5 Nickelodeon             16:9  720W  576H:->  146W  90H TOP_LEFT
//   1 Nickelodeon             16:9  720W  576H:->  154W  84H TOP_LEFT
//   1 Nickelodeon             16:9  720W  576H:->  170W  80H TOP_LEFT
//   2 Nickelodeon             16:9  720W  576H:->  176W  80H TOP_LEFT
//   4 Nickelodeon             16:9  720W  576H:->  180W  78H TOP_LEFT
//  19 Nickelodeon             16:9  720W  576H:->  180W  80H TOP_LEFT
//   5 Nickelodeon             16:9  720W  576H:->  182W  78H TOP_LEFT
//   1 Nickelodeon             16:9  720W  576H:->  182W  80H TOP_LEFT
//   3 Nickelodeon             16:9  720W  576H:->  184W  78H TOP_LEFT
//   4 Nickelodeon             16:9  720W  576H:->  184W  80H TOP_LEFT
        if (CompareChannelName(channelName, "Nickelodeon", IGNORE_NOTHING)) {
            logo.widthMin  = 134;
            logo.widthMax  = 194;
            logo.heightMin =  68;
            logo.heightMax =  93;
            logo.corner    = TOP_LEFT;  // too much different logos, but all in same corner
        }

        // NICK_CC+1               16:9  720W  576H:->  146W  88H TOP_LEFT
        // NICK_CC+1               16:9  720W  576H:->  146W  92H TOP_LEFT
        // NICK_CC+1               16:9  720W  576H:->  148W  92H TOP_LEFT
        // NICK_MTV+               16:9  720W  576H:->  146W  88H TOP_LEFT
        // NICK_MTV+               16:9  720W  576H:->  146W  90H TOP_LEFT
        if (CompareChannelName(channelName, "NICK_CC+1", IGNORE_NOTHING) ||
                CompareChannelName(channelName, "NICK_MTV+", IGNORE_NOTHING)) {
            logo.widthMin  = 144;
            logo.widthMax  = 184;
            logo.heightMin =  78;
            logo.heightMax =  96;
        }

        // Nick_Comedy_Central+1   16:9  720W  576H:->  138W  80H TOP_LEFT
        // Nick_Comedy_Central+1   16:9  720W  576H:->  144W  94H TOP_LEFT
        // Nick_Comedy_Central+1   16:9  720W  576H:->  120W 116H TOP_RIGHT        -> special logo
        if (CompareChannelName(channelName, "Nick_Comedy_Central+1", IGNORE_NOTHING)) {
            logo.widthMin  = 110;
            logo.widthMax  = 154;
            logo.heightMin =  70;
            logo.heightMax = 126;
        }

//   2 NITRO                   16:9  720W  576H:->  110W  64H TOP_LEFT
//   3 NITRO                   16:9  720W  576H:->  112W  64H TOP_LEFT
//   1 NITRO                    4:3  720W  576H:->  130W  62H TOP_LEFT
        if (CompareChannelName(channelName, "NITRO", IGNORE_NOTHING)) {
            logo.widthMin  = 102;
            logo.widthMax  = 140;
            logo.heightMin =  52;
            logo.heightMax =  74;
        }

//   1 ntv                     16:9  720W  576H:->  234W  60H BOTTOM_LEFT
//   1 ntv                     16:9  720W  576H:->  234W  90H BOTTOM_LEFT
        if (    (CompareChannelName(channelName, "n-tv", IGNORE_COUNTRY)) ||
                (CompareChannelName(channelName, "ntv",  IGNORE_COUNTRY))) {
            logo.widthMax  = INT_MAX;  // news ticker
            logo.heightMin =  50;
            logo.heightMax = 100;
            logo.corner    = BOTTOM_LEFT;
        }

//   1 ProSieben               16:9  720W  576H:->   92W  60H TOP_RIGHT    -> logo from ad
//
//   1 ProSieben               16:9  720W  576H:->   80W  62H TOP_RIGHT
// 166 ProSieben               16:9  720W  576H:->   80W  64H TOP_RIGHT
//  35 ProSieben               16:9  720W  576H:->   84W  66H TOP_RIGHT
//   1 ProSieben                4:3  720W  576H:->   94W  64H TOP_RIGHT
        if (CompareChannelName(channelName, "ProSieben", IGNORE_COUNTRY)) {
            logo.widthMin  =  70;
            logo.widthMax  =  94;
            logo.heightMin =  61;
            logo.heightMax =  76;
        }

//  87 Pro7_MAXX               16:9  720W  576H:->  112W  64H TOP_RIGHT
//   2 Pro7_MAXX               16:9  720W  576H:->  114W  64H TOP_RIGHT
//  10 Pro7_MAXX                4:3  720W  576H:->  138W  64H TOP_RIGHT
        if (CompareChannelName(channelName, "Pro7_MAXX", IGNORE_NOTHING)) {
            logo.widthMin  = 102;
            logo.widthMax  = 148;
            logo.heightMin =  54;
            logo.heightMax =  74;
        }

        if (CompareChannelName(channelName, "RTL_Television", IGNORE_NOTHING)) {        // RTL_Television          16:9  720W  576H:->  104W  60H TOP_LEFT (before 09/2021)
            // RTL_Television          16:9  720W  576H:->  142W  60H TOP_LEFT (before 09/2021 RTL live logo)
            // RTL_Television          16:9  720W  576H:->  126W  68H TOP_LEFT (after  09/2021)
            // RTL_Television          16:9  720W  576H:->  146W  68H TOP_LEFT (after  09/2021 RTL live logo)
            //
            // no logo                 16:9  720W  576H:->  116W  66H TOP_LEFT ("neue Folge")
            logo.widthMax  = 146 ;
            logo.heightMax =  68 ;
            logo.corner    = TOP_LEFT;
        }
        if (CompareChannelName(channelName, "RTL2", IGNORE_NOTHING)) {                  // RTL2                    16:9  720W  576H:->   82W  78H BOTTOM_RIGHT (new logo)
            // RTL2                    16:9  720W  576H:->   82W  80H BOTTOM_RIGHT (new Logo)
            // RTL2                    16:9  720W  576H:->  108W 108H BOTTOM_RIGHT (old logo)
            // RTL2                    16:9  720W  576H:->  108W 110H BOTTOM_RIGHT
            logo.widthMin  =  82;
            logo.widthMax  = 108;
            logo.heightMin =  78;
            logo.heightMax = 110;
        }

//     RTLZWEI                 16:9  720W  576H:->   72W  84H TOP_RIGHT       -> wrong logo from ad
//
//   9 RTLZWEI                 16:9  720W  576H:->   82W  78H BOTTOM_RIGHT
//  27 RTLZWEI                 16:9  720W  576H:->   82W  80H BOTTOM_RIGHT
//   2 RTLZWEI                  4:3  720W  576H:->   98W  78H BOTTOM_RIGHT
//   6 RTLZWEI                  4:3  720W  576H:->   98W  80H BOTTOM_RIGHT
        if (CompareChannelName(channelName, "RTLZWEI", IGNORE_COUNTRY)) {
            const sAspectRatio *aspectRatio = decoder->GetFrameAspectRatio();
            if ((aspectRatio->num == 16) && (aspectRatio->den == 9)) {
                logo.widthMin  =  73;
                logo.widthMax  =  92;
                logo.heightMin =  68;
                logo.heightMax =  90;
            }
            else {
                logo.widthMin  =  88;
                logo.widthMax  = 108;
                logo.heightMin =  68;
                logo.heightMax =  90;
            }
        }

        // RTLplus                 16:9  720W  576H:->  168W  64H TOP_LEFT
        // RTLplus                  4:3  720W  576H:->  214W  66H TOP_LEFT
        if (CompareChannelName(channelName, "RTLplus", IGNORE_NOTHING)) {               // RTLplus                 16:9  720W  576H:->  168W  64H TOP_LEFT
            logo.widthMin  = 168;
            logo.widthMax  = 214;
        }

        // RTLup                   16:9  720W  576H:->  142W  68H TOP_LEFT
        // RTLup                    4:3  720W  576H:->  166W  66H TOP_LEFT
        if (CompareChannelName(channelName, "RTLup", IGNORE_NOTHING)) {
            logo.widthMin  = 142;
            logo.widthMax  = 166;
        }

//   1 SAT_1_Gold              16:9  720W  576H:->  114W  62H TOP_RIGHT    03.10.2024 new logo
//   5 SAT_1_Gold              16:9  720W  576H:->  128W  74H TOP_RIGHT
//   6 SAT_1_Gold              16:9  720W  576H:->  130W  74H TOP_RIGHT
//   1 SAT_1_Gold              16:9  720W  576H:->  130W  76H TOP_RIGHT
        if (CompareChannelName(channelName, "SAT_1_Gold", IGNORE_COUNTRY)) {
            logo.widthMin  = 104;
            logo.widthMax  = 140;
            logo.heightMin =  52;
            logo.heightMax =  86;
        }

        // sixx                    16:9  720W  576H:->  106W  54H TOP_RIGHT
        // SIXX                    16:9  720W  576H:->  106W  54H TOP_RIGHT
        // SIXX                    16:9  720W  576H:->  118W  56H TOP_RIGHT
        // SIXX                     4:3  720W  576H:->  130W  54H TOP_RIGHT
        if (CompareChannelName(channelName, "SIXX", IGNORE_NOTHING)) {
            logo.widthMin  = 106;
            logo.widthMax  = 130;
            logo.heightMin =  54;
            logo.heightMax =  56;
        }

        // SUPER_RTL               16:9  720W  576H:->  160W  66H TOP_LEFT     -> logo RTL SUPER
        // SUPER_RTL               16:9  720W  576H:->   98W  48H TOP_LEFT     -> logo SUPER RTL
        if (CompareChannelName(channelName, "SUPER_RTL", IGNORE_NOTHING)) {
            logo.widthMax  = 160;
            logo.heightMin =  48;
        }

        //   1 TELE_5                  16:9  720W  576H:->  108W  64H BOTTOM_RIGHT
        //   9 TELE_5                  16:9  720W  576H:->  108W  66H BOTTOM_RIGHT
        //   1 TELE_5                  16:9  720W  576H:->  108W  68H BOTTOM_RIGHT
        //   2 TELE_5                  16:9  720W  576H:->  108W  72H BOTTOM_RIGHT
        //   1 TELE_5                   4:3  720W  576H:->  144W  66H BOTTOM_RIGHT
        //
        //   2 TELE_5                  16:9  720W  576H:->   70W  76H TOP_LEFT
        if (CompareChannelName(channelName, "TELE_5", IGNORE_NOTHING)) {
            switch (logoCorner) {
            case TOP_LEFT:
                logo.heightMin =  64;
                logo.heightMax =  76;
                logo.widthMin  =  69;
                logo.widthMax  =  71;
                break;
            case BOTTOM_RIGHT:
                logo.heightMin =  54;
                logo.heightMax =  82;
                logo.widthMin  =  98;
                logo.widthMax  = 154;
                break;
            default:                   // no logo possible
                logo.heightMin =  INT_MAX;
                logo.heightMax =  0;
                logo.widthMin  =  INT_MAX;
                logo.widthMax  =  0;
            }
        }

        // TLC                     16:9  720W  576H:->   94W  60H TOP_LEFT
        // TLC                     16:9  720W  576H:->   98W  60H TOP_LEFT
        if (CompareChannelName(channelName, "TLC", IGNORE_NOTHING)) {
            logo.heightMin =  60;
            logo.heightMax =  65;
        }

        if (CompareChannelName(channelName, "TOGGO_plus", IGNORE_NOTHING)) {            // TOGGO_plus              16:9  720W  576H:->  104W  56H TOP_LEFT
            logo.heightMin =  56;
        }

        // VOX                     16:9  720W  576H:->  108W  70H TOP_LEFT
        // VOX                      4:3  720W  576H:->  126W  70H TOP_LEFT
        if (CompareChannelName(channelName, "VOX", IGNORE_NOTHING)) {
            logo.heightMin =  70;
            logo.heightMax =  70;
        }

//   2 VOXup                   16:9  720W  576H:->  108W  66H TOP_LEFT
//  49 VOXup                   16:9  720W  576H:->  110W  66H TOP_LEFT
        if (CompareChannelName(channelName, "VOXup", IGNORE_NOTHING)) {
            logo.widthMin  =  98;
            logo.widthMax  = 120;
            logo.heightMin =  56;
            logo.heightMax =  76;
        }

        // WELT                    16:9  720W  576H:->  226W  62H BOTTOM_LEFT
        if (CompareChannelName(channelName, "WELT", IGNORE_NOTHING)) {
            logo.heightMax = 64;
            logo.widthMax  = INT_MAX;      // news ticker
            logo.corner    = BOTTOM_LEFT;
        }

        if (CompareChannelName(channelName, "Welt_der_Wunder", IGNORE_NOTHING)) {       // Welt_der_Wunder         16:9  720W  576H:->   94W 108H TOP_LEFT
            // Welt_der_Wunder         16:9  720W  576H:->   96W 112H TOP_LEFT
            logo.widthMin  =  94;
            logo.heightMax = 112;
        }

        // default values
        if (logo.widthMin  == 0) logo.widthMin  =  50; // arte                    16:9  720W  576H:->   50W 108H TOP_LEFT
        if (logo.widthMax  == 0) logo.widthMax  = 214; // RTLplus                  4:3  720W  576H:->  214W  66H TOP_LEFT
        if (logo.heightMin == 0) logo.heightMin =  48; // SUPER_RTL               16:9  720W  576H:->   98W  48H TOP_LEFT
        if (logo.heightMax == 0) logo.heightMax = 116; // Nick_Comedy_Central+1   16:9  720W  576H:->  120W 116H TOP_RIGHT
        break;

    case 1280:
        // 3sat_HD                 16:9 1280W  720H:->  178W  94H TOP_LEFT
        if (CompareChannelName(channelName, "3sat_HD", IGNORE_NOTHING)) {
            logo.widthMax  = 185;
        }

//   1 ANIXE+                  16:9 1280W 1080H:->  290W 164H TOP_LEFT
//   1 ANIXE+                  16:9 1280W 1080H:->  294W 170H TOP_LEFT
        if (CompareChannelName(channelName, "ANIXE+", IGNORE_NOTHING)) {
            logo.widthMin  = 280;
            logo.widthMax  = 300;
            logo.heightMin = 154;
            logo.heightMax = 180;
        }

        // ARD_alpha_HD            16:9 1280W  720H:->  206W  78H TOP_LEFT
        if (CompareChannelName(channelName, "ARD_alpha_HD", IGNORE_NOTHING)) {
            logo.heightMax =  78;
            logo.widthMax  = 206;
        }

//   1 arte_HD                 16:9 1280W  720H:->  180W 134H TOP_LEFT    arte Thema logo
//   1 arte_HD                 16:9 1280W  720H:->   88W 134H TOP_LEFT
        if (CompareChannelName(channelName, "arte_HD", IGNORE_NOTHING)) {
            logo.widthMin  =  78;
            logo.widthMax  = 190;
            logo.heightMin = 124;
            logo.heightMax = 144;
        }

        // BR_Fernsehen_Süd_HD     16:9 1280W  720H:->  134W  84H TOP_LEFT
        if (CompareChannelName(channelName, "BR_Fernsehen_HD", IGNORE_CITY)) {
            logo.widthMax  = 135;
            logo.heightMax =  85;
        }

        // Das_Erste_HD            16:9 1280W  720H:->  146W 114H TOP_RIGHT
        // Das_Erste_HD            16:9 1280W  720H:->  148W 114H TOP_RIGHT
        // Das_Erste_HD            16:9 1280W  720H:->  148W 128H TOP_RIGHT
        // Das_Erste_HD            16:9 1280W  720H:->  244W 114H TOP_RIGHT    <- check eins Kinderprogramm Logo
        if (CompareChannelName(channelName, "Das_Erste_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 146;
            logo.widthMax  = 244;
            logo.heightMin = 114;
            logo.heightMax = 128;
            logo.corner    = TOP_RIGHT;   // too much same size info logos in other corner
        }

        if (CompareChannelName(channelName, "Einsfestival_HD", IGNORE_NOTHING)) {       // Einsfestival_HD         16:9 1280W  720H:->  300W  80H TOP_RIGHT
            logo.widthMax  = 300;
        }
        if (CompareChannelName(channelName, "EinsPlus_HD", IGNORE_NOTHING)) {           // EinsPlus_HD             16:9 1280W  720H:->  334W  86H TOP_RIGHT
            logo.widthMax  = 335;
        }

        // hr-fernsehen_HD         16:9 1280W  720H:->  196W  98H TOP_LEFT
        // hr-fernsehen_HD         16:9 1280W  720H:->  214W 100H TOP_LEFT
        if (CompareChannelName(channelName, "hr-fernsehen_HD", IGNORE_NOTHING)) {
            logo.widthMax  = 214;
            logo.heightMax = 100;
        }

//  13 KiKA_HD                 16:9 1280W  720H:->  228W  96H TOP_LEFT
//  17 KiKA_HD                 16:9 1280W  720H:->  230W  96H TOP_LEFT
//   4 KiKA_HD                 16:9 1280W  720H:->  230W  98H TOP_LEFT
//   1 KiKA_HD                 16:9 1280W  720H:->  298W 122H TOP_LEFT    -> special logo with rabbit left of logo

        if (CompareChannelName(channelName, "KiKA_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 218;
            logo.widthMax  = 308;
            logo.heightMin =  86;
            logo.heightMax = 132;
        }

        if (CompareChannelName(channelName, "MDR_HD", IGNORE_CITY)) {        // MDR_Sachsen_HD          16:9 1280W  720H:->  160W  70H TOP_LEFT
            logo.heightMax =  70;
        }

//   1 NDR_FS_HH_HD            16:9 1280W  720H:->  184W  86H TOP_LEFT
//   3 NDR_FS_HH_HD            16:9 1280W  720H:->  184W  88H TOP_LEFT
//   1 NDR_FS_NDS_HD           16:9 1280W  720H:->  144W  88H TOP_LEFT   -> new logo without HD
//   1 NDR_FS_NDS_HD           16:9 1280W  720H:->  184W  88H TOP_LEFT
        if (CompareChannelName(channelName, "NDR_FS_HD", IGNORE_CITY)) {
            logo.widthMin  = 134;
            logo.widthMax  = 194;
            logo.heightMin =  76;
            logo.heightMax =  98;
        }

        // ONE_HD                  16:9 1280W  720H:->  232W  80H TOP_RIGHT
        if (CompareChannelName(channelName, "ONE_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 222;
            logo.widthMax  = 242;
            logo.heightMin =  70;
            logo.heightMax =  90;
        }

//   1 ORF_2_EUROPE_HD         16:9 1280W  720H:->  172W  64H TOP_RIGHT
        if (CompareChannelName(channelName, "ORF_2_EUROPE_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 162;
            logo.widthMax  = 182;
            logo.heightMin =  54;
            logo.heightMax =  74;
        }

        // ORF2W_HD                16:9 1280W  720H:->  198W  80H TOP_RIGHT
        if (CompareChannelName(channelName, "ORF2W_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 188;
            logo.widthMax  = 208;
            logo.heightMin =  70;
            logo.heightMax =  90;
        }

        // phoenix_HD              16:9 1280W  720H:->  168W  72H TOP_LEFT
        if (CompareChannelName(channelName, "phoenix_HD", IGNORE_NOTHING)) {
            logo.widthMax =  168;
        }

//   1 MDR_Sachsen_HD          16:9 1280W  720H:->  160W  68H TOP_LEFT
//   1 MDR_Sachsen_HD          16:9 1280W  720H:->  162W  68H TOP_LEFT
        if (CompareChannelName(channelName, "MDR_HD", IGNORE_CITY)) {
            logo.widthMin  = 150;
            logo.widthMax  = 172;
            logo.heightMin =  58;
            logo.heightMax =  78;
        }

        // rbb_Berlin_HD           16:9 1280W  720H:->  178W  92H TOP_RIGHT
        if (CompareChannelName(channelName, "rbb_HD", IGNORE_CITY)) {
            logo.widthMin =  177;
            logo.widthMax =  179;
        }

        //  SRF_zwei_HD             16:9 1280W  720H:->  172W  66H TOP_RIGHT
        if (CompareChannelName(channelName, "SRF_zwei_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 162;
            logo.widthMax  = 182;
            logo.heightMin =  56;
            logo.heightMax =  76;
        }

        //   1 SWR_BW_HD               16:9 1280W  720H:->  228W  74H TOP_LEFT
        //   2 SWR_BW_HD               16:9 1280W  720H:->  228W  76H TOP_LEFT
        if (CompareChannelName(channelName, "SWR_HD", IGNORE_CITY)) {
            logo.widthMin  = 218;
            logo.widthMax  = 238;
            logo.heightMin =  64;
            logo.heightMax =  86;
        }

        if (CompareChannelName(channelName, "WDR_HD", IGNORE_CITY)) {         // WDR_HD_Köln             16:9 1280W  720H:->  224W  80H TOP_RIGHT
            logo.heightMax =  80;
        }

        //   1 ZDF_HD                  16:9 1280W  720H:->  146W  96H TOP_LEFT    -> logo without HD
        //   1 ZDF_HD                  16:9 1280W  720H:->  186W  84H TOP_LEFT
        //  11 ZDF_HD                  16:9 1280W  720H:->  186W  94H TOP_LEFT
        //  34 ZDF_HD                  16:9 1280W  720H:->  186W  96H TOP_LEFT
        //   8 ZDF_HD                  16:9 1280W  720H:->  188W  94H TOP_LEFT
        // 155 ZDF_HD                  16:9 1280W  720H:->  188W  96H TOP_LEFT
        if (CompareChannelName(channelName, "ZDF_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 136;
            logo.widthMax  = 198;
            logo.heightMin =  84;
            logo.heightMax = 105;   // prevent to accept "heute Xpress" as logo
        }

//   1 ZDFinfo_HD              16:9 1280W  720H:->  156W  82H TOP_LEFT  -> false logo, only "info"
//   1 ZDFinfo_HD              16:9 1280W  720H:->  160W  86H TOP_LEFT
        if (CompareChannelName(channelName, "ZDFinfo_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 150;
            logo.widthMax  = 170;
            logo.heightMin =  83;
            logo.heightMax =  96;
        }

        // zdf_neo_HD              16:9 1280W  720H:->  162W  96H TOP_LEFT
        // zdf_neo_HD              16:9 1280W  720H:->  180W  96H TOP_LEFT
        if (CompareChannelName(channelName, "zdf_neo_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 152;
            logo.widthMax  = 190;
            logo.heightMin =  86;
            logo.heightMax = 106;
        }


        if (CompareChannelName(channelName, "Deluxe_Music_HD", IGNORE_NOTHING)) {       // Deluxe_Music_HD         16:9 1280W 1080H:->  334W 124H TOP_RIGHT
            logo.widthMax  = 334;
        }

        // default values
        if (logo.widthMin  == 0) logo.widthMin  =   88; // arte_HD (vertical)      16:9 1280W  720H:->   88W 134H TOP_LEFT
        if (logo.widthMax  == 0) logo.widthMax  =  334; // EinsPlus_HD             16:9 1280W  720H:->  334W  86H TOP_RIGHT
        if (logo.heightMin == 0) logo.heightMin =   64; // ORF_2_EUROPE_HD         16:9 1280W  720H:->  172W  64H TOP_RIGHT
        if (logo.heightMax == 0) logo.heightMax =  172; // ANIXE+                  16:9 1280W 1080H:->  294W 172H TOP_LEFT
        break;

    case 1440:
        if (CompareChannelName(channelName, "WELT_HD", IGNORE_NOTHING)) {               // WELT_HD                 16:9 1440W 1080H:->  396W 116H BOTTOM_LEFT
            logo.widthMax  = INT_MAX;  // news ticker
        }

        // default values
        if (logo.widthMin  == 0) logo.widthMin  =  124; // BILD_HD                 16:9 1440W 1080H:->  124W 168H TOP_LEFT (normal logo)
        if (logo.widthMax  == 0) logo.widthMax  =  250; // DMAX_HD                 16:9 1440W 1080H:->  250W 140H TOP_LEFT
        if (logo.heightMin == 0) logo.heightMin =  112; // WELT_HD                 16:9 1440W 1080H:->  396W 116H BOTTOM_LEFT
        if (logo.heightMax == 0) logo.heightMax =  204; // BILD_HD                 16:9 1440W 1080H:->  124W 204H TOP_LEFT (doku logo)
        break;

    case 1920:
        // 13th_Street_HD          16:9 1920W 1080H:->  224W 198H TOP_LEFT
        if (CompareChannelName(channelName, "13th_Street_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 219;
            logo.widthMax  = 229;
            logo.heightMin = 193;
            logo.heightMax = 203;
        }

//    1 ANIXE_HD                16:9 1920W 1080H:->  438W 180H TOP_LEFT
        if (CompareChannelName(channelName, "ANIXE_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 428;
            logo.widthMax  = 448;
            logo.heightMin = 170;
            logo.heightMax = 190;
        }

        // arte_HD                 16:9 1920W 1080H:->  130W 200H TOP_LEFT
        if (CompareChannelName(channelName, "arte_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 120;
            logo.widthMax  = 140;
            logo.heightMin = 190;
            logo.heightMax = 210;
        }

//   1 a_tv_HD                 16:9 1920W 1080H:->  300W 172H BOTTOM_LEFT
        if (CompareChannelName(channelName, "a_tv", IGNORE_HD)) {
            logo.widthMin  = 290;
            logo.widthMax  = 310;
            logo.heightMin = 162;
            logo.heightMax = 182;
        }

//    1 C8                      16:9 1920W 1080H:->  218W 124H TOP_RIGHT
        if (CompareChannelName(channelName, "C8", IGNORE_NOTHING)) {
            logo.widthMin  = 208;
            logo.widthMax  = 228;
            logo.heightMin = 114;
            logo.heightMax = 134;
//            logo.corner    = TOP_RIGHT;
        }

        // Kutonen_HD              16:9 1920W 1080H:->  252W 142H TOP_RIGHT
        if (CompareChannelName(channelName, "Kutonen_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 242;
            logo.widthMax  = 262;
            logo.heightMin = 132;
            logo.heightMax = 152;
        }

        // mþnchen_tv_HD           16:9 1920W 1080H:->  428W  96H TOP_LEFT
        if (CompareChannelName(channelName, "mþnchen_tv_HD", IGNORE_NOTHING)) {  // char conversion error
            logo.widthMin  = 418;
            logo.widthMax  = 438;
            logo.heightMin =  86;
            logo.heightMax = 106;
        }

        // n-tv_HD                 16:9 1920W 1080H:->  406W 110H BOTTOM_RIGHT
        if (CompareChannelName(channelName, "n-tv_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 396;
            logo.widthMax  = 416;
            logo.heightMin = 100;
            logo.heightMax = 120;
            logo.corner    = BOTTOM_RIGHT;
        }

        // RTL_HD                  16:9 1920W 1080H:->  332W 110H TOP_LEFT
        if (CompareChannelName(channelName, "RTL_HD", IGNORE_NOTHING)) {
            logo.widthMin  = 322;
            logo.widthMax  = 342;
            logo.heightMin = 100;
            logo.heightMax = 120;
        }

//   1 TMC                     16:9 1920W 1080H:->  270W 116H TOP_RIGHT
        if (CompareChannelName(channelName, "TMC", IGNORE_NOTHING)) {
            logo.widthMin  = 260;
            logo.widthMax  = 280;
            logo.heightMin = 106;
            logo.heightMax = 126;
        }

        // TV5_HD                  16:9 1920W 1080H:->  260W 164H TOP_RIGHT
        if (CompareChannelName(channelName, "TV5_HD", IGNORE_NOTHING)) {
            logo.heightMin = 163;
            logo.heightMax = 165;
            logo.widthMin  = 259;
            logo.widthMax  = 261;
        }

        //  VOX_HD                  16:9 1920W 1080H:->  286W 124H TOP_LEFT
        if (CompareChannelName(channelName, "VOX_HD", IGNORE_NOTHING)) {
            logo.heightMax = 125;
        }

        // default values
        if (logo.widthMin  == 0) logo.widthMin  =  140; // arte_HD                 16:9 1920W 1080H:->  130W 200H TOP_LEFT
        if (logo.widthMax  == 0) logo.widthMax  =  448; // ANIXE_HD                16:9 1920W 1080H:->  438W 180H TOP_LEFT
        if (logo.heightMin == 0) logo.heightMin =   86; // münchen_tv_HD           16:9 1920W 1080H:->  336W  96H TOP_LEFT
        if (logo.heightMax == 0) logo.heightMax =  210; // arte_HD                 16:9 1920W 1080H:->  130W 200H TOP_LEFT
        break;

    case 3840:
        // default values
        if (logo.widthMin  == 0) logo.widthMin  = 696; // RTL_UHD                 16:9 3840W 2160H:->  706W 218H TOP_LEFT
        if (logo.widthMax  == 0) logo.widthMax  = 716; // RTL_UHD                 16:9 3840W 2160H:->  706W 218H TOP_LEFT
        if (logo.heightMin == 0) logo.heightMin = 208; // RTL_UHD                 16:9 3840W 2160H:->  706W 218H TOP_LEFT
        if (logo.heightMax == 0) logo.heightMax = 228; // RTL_UHD                 16:9 3840W 2160H:->  706W 218H TOP_LEFT
        break;

    default:
        dsyslog("cExtractLogo::CheckLogoSize(): no logo size rules for %dx%d",decoder->GetVideoWidth(), decoder->GetVideoHeight());
        return false;
        break;
    }
    dsyslog("cExtractLogo::CheckLogoSize(): channel logo size rule for %s: W %d->%d, H %d->%d", channelName, logo.widthMin, logo.widthMax, logo.heightMin, logo.heightMax);

    if (logoSizeFinal->width < logo.widthMin) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too narrow %d, expect min %d", logoSizeFinal->width, logo.widthMin);
        return false;
    }
    if (logoSizeFinal->width > logo.widthMax) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too wide %d, expect max %d", logoSizeFinal->width, logo.widthMax);
        return false;
    }
    if (logoSizeFinal->height < logo.heightMin) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too low %d, expect min %d", logoSizeFinal->height, logo.heightMin);
        return false;
    }
    if (logoSizeFinal->height > logo.heightMax) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too height %d, expect max %d", logoSizeFinal->height, logo.heightMax);
        return false;
    }
    if ((logo.corner >= 0) && (logo.corner != logoCorner)) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo in corner %d, expect %d", logoCorner, logo.corner);
        return false;
    }
    return true;
}


bool cExtractLogo::Resize(sLogoInfo *bestLogoInfo, sLogoSize *logoSizeFinal, const int bestLogoCorner) {
    if (!bestLogoInfo)             return false;
    if (!logoSizeFinal)            return false;
    if (bestLogoCorner < 0)        return false;
    if (bestLogoCorner >= CORNERS) return false;
    if ((logoSizeFinal->width <= 0) || (logoSizeFinal->height <= 0)) {
        dsyslog("cExtractLogo::Resize(): logo size %3d width %3d height on corner %12s not valid", logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
        return false;
    }

    dsyslog("cExtractLogo::Resize(): logo size before resize: %3dW X %3dH on corner %12s", logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
    int logoWidthBeforeResize  = logoSizeFinal->width;
    int logoHeightBeforeResize = logoSizeFinal->height;
    int acceptFalsePixelH = logoSizeFinal->width / 37;  // reduced from 60 to 20, increased to 30 for vertical logo of arte HD
    // increased to 37 to get full thin logos (e.g. arte HD)
    int acceptFalsePixelV;
    if (decoder->GetVideoWidth() < 3840) acceptFalsePixelV = logoSizeFinal->height / 33; // increase from 27 to 33 to get left start from SIXX logo
    else acceptFalsePixelV = logoSizeFinal->height / 30;                                 // UDH has very thin logo structure
#ifdef DEBUG_LOGO_RESIZE
    dsyslog("cExtractLogo::Resize(): accept false pixel horizontal %d, vertical %d", acceptFalsePixelH, acceptFalsePixelV);
#endif

    for (int repeat = 1; repeat <= 2; repeat++) {
        if ((logoSizeFinal->width <= 0) || (logoSizeFinal->height <= 0)) {
            esyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", decoder->GetVideoWidth(), decoder->GetVideoHeight(), logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
            logoSizeFinal->height = logoHeightBeforeResize; // restore logo size
            logoSizeFinal->width  = logoWidthBeforeResize;
            return false;
        }

#ifdef DEBUG_LOGO_RESIZE
        // save plane 0 of logo
        int cutStep = 0;
        char *fileName = nullptr;
        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dBefore.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
            ALLOC(strlen(fileName)+1, "fileName");
            sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
            FREE(strlen(fileName)+1, "fileName");
            free(fileName);
            cutStep++;
        }
#endif

// resize plane 0
        if (bestLogoCorner <= TOP_RIGHT) {  // top corners, calculate new height and cut from below
            int whiteLines = 0;
            for (int line = logoSizeFinal->height - 1; line > 0; line--) {
                int blackPixel = 0;
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelH) break;
                    }
                }
                if (blackPixel < acceptFalsePixelH) {  // accept false pixel
                    whiteLines++;
                }
                else break;
            }
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, top logo: %d white lines under logo", repeat, whiteLines);
#endif
            if (whiteLines > 0) {
                CutOut(bestLogoInfo, whiteLines, 0, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                // save plane 0 of logo
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutBottom.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                }
#endif
            }

// search for text under logo
// search for at least 2 (SD) or 4 (HD) white lines to cut logos with text addon (e.g. "Neue Folge" or "Live"), but do not cut out "HD"
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, top logo: search for text under logo", repeat);
#endif
#define MAX_FALSE_PIXEL 2
            int minWhiteLines = 2;
            if (decoder->GetVideoWidth() > 720) minWhiteLines = 4;
            // search for logo top line (first black pixel in line)
            int logoTopLine = -1;
            for (int line = 0; line < logoSizeFinal->height; line++) {
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {  // black pixel
                        logoTopLine = line;
                        break;
                    }
                }
                if (logoTopLine >= 0) break;
            }
            if (logoTopLine <= 10) {
                dsyslog("cExtractLogo::Resize(): no more white part above logo, logo is invalid");
                return false;
            }
            // search for logo buttom line (first line without black pixel)
            int logoButtomLine = -1;
            for (int line = logoTopLine; line < logoSizeFinal->height; line++) {
                logoButtomLine = line - 1;
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) { // black pixel
                        logoButtomLine = -1;
                        break;
                    }
                }
                if (logoButtomLine >= 0) break;
            }
            if (logoButtomLine < 0) logoButtomLine = logoSizeFinal->width - 1;  // logo end at buttom
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): logo top line %d, buttom line %d", logoTopLine, logoButtomLine);
#endif

            // search for white lines from buttom to top of logo (more than half can not be text)
            int topWhiteLine     = -1;
            int bottomWhiteLine  = -1;
            for (int line = logoSizeFinal->height - 1; line > logoButtomLine; line--) {  // check bottom half of the picture
                int countBlackPixel = 0;
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {
                        countBlackPixel++;
                        if (countBlackPixel > MAX_FALSE_PIXEL) break;   // no white line
                    }
                }
                if (countBlackPixel <= MAX_FALSE_PIXEL) {
                    if (bottomWhiteLine == -1) bottomWhiteLine = line;
                    topWhiteLine = line;
                }
                else {
                    if ((bottomWhiteLine - topWhiteLine + 1) >= minWhiteLines) break;  // we found enough white lines
                    topWhiteLine     = -1;
                    bottomWhiteLine  = -1;
                }
            }
            int countWhite = bottomWhiteLine - topWhiteLine + 1;
            int textHeight = logoSizeFinal->height - bottomWhiteLine - 1;
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, top logo: found white from line %d -> %d, height %d, text below from line %d -> %d, height %d", repeat, topWhiteLine, bottomWhiteLine, countWhite, bottomWhiteLine + 1, logoSizeFinal->height - 1, textHeight);
#endif
            if ((countWhite <= 22) && (textHeight < logoSizeFinal->height)) {    // too much white is not possible for text under logo, changed from 11 to 22
                // get width of text
                int leftColumn  = -1;
                int rightColumn = -1;
                int line = logoSizeFinal->height - (textHeight / 2) - 1;   // check in half of text
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {
                        leftColumn = column;
                        break;
                    }
                }
                for (int column = logoSizeFinal->width - 1; column >= 0; column--) {
                    if (bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {
                        rightColumn = column;
                        break;
                    }
                }
                int textWidth       = rightColumn - leftColumn + 1;
                int textWidthQuote  = 1000 * textWidth / decoder->GetVideoWidth();
                int textHeightQuote = 1000 * textHeight / decoder->GetVideoHeight();
#ifdef DEBUG_LOGO_RESIZE
                dsyslog("cExtractLogo::Resize(): repeat %d, top logo: found text under logo: line %3d -> %3d, height %d (%d), column %d -> %d, width %d (%d)", repeat, bottomWhiteLine + 1, logoSizeFinal->height - 1, textHeight, textHeightQuote, leftColumn, rightColumn, textWidth, textWidthQuote);
#endif
                // example of valid test to delete
                // line  86 ->  97, height 12 (20), column 185 -> 205, width 21 (29)   -> Pro7 MAXX "neu" under logo
                //
                // example of part of the logo, do nt delete
                // line 101 -> 113, height 13 (18), column 267 -> 289, width 23 (17)   -> Das Erste "HD" under logo
                if ((textHeight <= 2) || // pixel error
                        ((textHeightQuote > 18) && (textWidthQuote > 17))) {
#ifdef DEBUG_LOGO_RESIZE
                    dsyslog("cExtractLogo::Resize(): repeat %d, top logo: cut out valid text under logo", repeat);
#endif
                    if ((logoSizeFinal->height - topWhiteLine) > 0) {
                        CutOut(bestLogoInfo, logoSizeFinal->height - topWhiteLine, 0, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                        // save plane 0 of logo
                        char *fileName = nullptr;
                        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutBottomText.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                            ALLOC(strlen(fileName)+1, "fileName");
                            sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                            FREE(strlen(fileName)+1, "fileName");
                            free(fileName);
                        }
#endif
                    }
                }
#ifdef DEBUG_LOGO_RESIZE
                else dsyslog("cExtractLogo::Resize(): repeat %d, top logo: no valid text under logo found", repeat);
#endif
            }
#ifdef DEBUG_LOGO_RESIZE
            else dsyslog("cExtractLogo::Resize(): repeat %d, top logo: no valid text under logo found", repeat);
#endif
        }

        else { // bottom corners, calculate new height and cut from above
            int whiteLines = 0;
            for (int line = 0; line < logoSizeFinal->height; line++) {
                int blackPixel = 0;
                for (int column = 0; column < logoSizeFinal->width; column++) {
                    if ( bestLogoInfo->sobel[0][line * (logoSizeFinal->width) + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelH) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelH) {  // accept false pixel
                    whiteLines++;
                }
                else break;
            }
            if (whiteLines >= logoSizeFinal->height) {
                dsyslog("cExtractLogo::Resize(): logo invalid after removal of false pixel");
                return false;
            }
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, bottom logo: %d white lines found", repeat, whiteLines);
#endif
            if (whiteLines > 0) {
                CutOut(bestLogoInfo, whiteLines, 0, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                // save plane 0 of logo
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutTop.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                    cutStep++;
                }
#endif
            }

// search for text above logo
// search for at least 3 white lines to cut logos with text addon (e.g. "Neue Folge" or "Live")
            int countWhite = 0;
            int cutLine = 0;
            int topBlackLineOfLogo = 0;
            int leftBlackPixel = INT_MAX;
            int rightBlackPixel = 0;
            int minWhiteLines;
            if (decoder->GetVideoWidth() == 720) minWhiteLines = 2;
            else minWhiteLines = 4;
            for (int line = 0; line < logoSizeFinal->height; line++) {
                int countBlackPixel = 0;
                for (int column = logoSizeFinal->width - 1; column > 0; column--) {
                    if (bestLogoInfo->sobel[0][line * logoSizeFinal->width + column] == 0) {
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
            int quoteAfterCut = 100 * (logoSizeFinal->height - cutLine) / logoSizeFinal->height; // we may not cut off too much, this could not be text under logo, this is something on top of the logo e.g. RTL2
            if ((topBlackLineOfLogo > cutLine) && (quoteAfterCut > 52)) {  // changed from 48 to 52
                if ((cutLine >= LOGO_MIN_LETTERING_H) && (cutLine < LOGO_MAX_LETTERING_H)) {
                    dsyslog("cExtractLogo::Resize(): found text above logo, cut at line %d, size %dWx%dH, pixel before: left %d right %d, quote %d, width is valid", cutLine, rightBlackPixel - leftBlackPixel, cutLine, leftBlackPixel, rightBlackPixel, quoteAfterCut);
                    if (cutLine > 0) {
                        CutOut(bestLogoInfo, cutLine, 0, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                        // save plane 0 of logo
                        char *fileName = nullptr;
                        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutTopText.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                            ALLOC(strlen(fileName)+1, "fileName");
                            sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                            FREE(strlen(fileName)+1, "fileName");
                            free(fileName);
                            cutStep++;
                        }
#endif
                    }
                }
                else dsyslog("cExtractLogo::Resize(): cutline at %d not valid (expect >=%d and <%d)", cutLine, LOGO_MIN_LETTERING_H, LOGO_MAX_LETTERING_H);
            }
        }

        if ((bestLogoCorner == TOP_RIGHT) || (bestLogoCorner == BOTTOM_RIGHT)) {  // right corners, cut from left
            int whiteColumns = 0;
            for (int column = 0; column < logoSizeFinal->width - 1; column++) {
                int blackPixel = 0;
                for (int line = 0; line < logoSizeFinal->height - 1; line++) {
                    if (bestLogoInfo->sobel[0][line * logoSizeFinal->width + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelV) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelV ) {  // accept false pixel
                    whiteColumns++;
                }
                else break;
            }
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, right logo: %d white columns found", repeat, whiteColumns);
#endif
            if (whiteColumns > 0) {
                CutOut(bestLogoInfo, 0, whiteColumns, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                // save plane 0 of logo
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutRight.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                    cutStep++;
                }
#endif
            }

// check text left of logo, search for at least 2 white columns to cut logos with text addon (e.g. "Neue Folge")
            int countWhite = 0;
            int cutColumn = 0;
            int lastBlackColumn = 0;
            int topBlackPixel =  INT_MAX;
            int topBlackPixelBefore = INT_MAX;
            int bottomBlackPixelBefore = 0;
            int bottomBlackPixel = 0;
            for (int column = 0; column < logoSizeFinal->width; column++) {
                bool isAllWhite = true;
                topBlackPixel = topBlackPixelBefore;
                bottomBlackPixel = bottomBlackPixelBefore;
                for (int line = 0; line < logoSizeFinal->height; line++) {
                    if (bestLogoInfo->sobel[0][line * logoSizeFinal->width + column] == 0) {
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
                if (countWhite >= 2) {  // changed from 3 to 2 (text right of Pro7_MAXX logo)
                    cutColumn = column;
                }
            }
            if (lastBlackColumn > cutColumn) {
                if ((bottomBlackPixel - topBlackPixel) <= 13) {
                    dsyslog("cExtractLogo::Resize(): repeat %d, left logo: found text before logo, cut at column %d, pixel of text: top %d bottom %d, text height %d is valid", repeat, cutColumn, topBlackPixel, bottomBlackPixel, bottomBlackPixel - topBlackPixel);
                    if (cutColumn > 0) {
                        CutOut(bestLogoInfo, 0, cutColumn, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                        // save plane 0 of logo
                        char *fileName = nullptr;
                        if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dAfterCutLeftText.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                            ALLOC(strlen(fileName)+1, "fileName");
                            sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                            FREE(strlen(fileName)+1, "fileName");
                            free(fileName);
                            cutStep++;
                        }
#endif
                    }

                }
                else dsyslog("cExtractLogo::Resize(): repeat %d, left logo: found text before logo, cut at column %d, pixel text: top %d bottom %d, text height %d is not valid", repeat, cutColumn, topBlackPixel, bottomBlackPixel, bottomBlackPixel - topBlackPixel);
            }
        }
        else { // left corners, cut from right
            int whiteColumns = 0;
            for (int column = logoSizeFinal->width - 1; column > 0; column--) {
                int blackPixel = 0;
                for (int line = 0; line < logoSizeFinal->height; line++) {
                    if (bestLogoInfo->sobel[0][line * logoSizeFinal->width + column] == 0) {
                        blackPixel++;
                        if (blackPixel > acceptFalsePixelV) break;
                    }
                }
                if (blackPixel <= acceptFalsePixelV ) {  // accept false pixel
                    whiteColumns++;
                }
                else break;
            }
#ifdef DEBUG_LOGO_RESIZE
            dsyslog("cExtractLogo::Resize(): repeat %d, left logo: %d white columns found", repeat, whiteColumns);
#endif
            if (whiteColumns > 0) {
                CutOut(bestLogoInfo, 0, whiteColumns, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                // save plane 0 of logo
                char *fileName = nullptr;
                if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dAfterCutRightWhite.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                    ALLOC(strlen(fileName)+1, "fileName");
                    sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                    FREE(strlen(fileName)+1, "fileName");
                    free(fileName);
                    cutStep++;
                }
#endif
            }
            // search text right of logo (e.g. "Neue Folge")
            if (!CheckLogoSize(logoSizeFinal, bestLogoCorner)) {
                dsyslog("cExtractLogo::Resize(): repeat %d, left logo: search for text right of logo", repeat);
                int countWhite = 0;
                int cutColumn = 0;
                int topBlackPixel =  INT_MAX;
                int topBlackPixelBefore = INT_MAX;
                int bottomBlackPixelBefore = 0;
                int bottomBlackPixel = 0;
                for (int column = logoSizeFinal->width - 1; column > 0; column--) {
                    bool isAllWhite = true;
                    topBlackPixel = topBlackPixelBefore;
                    bottomBlackPixel = bottomBlackPixelBefore;
                    for (int line = 0; line < logoSizeFinal->height; line++) {
                        if (bestLogoInfo->sobel[0][line * logoSizeFinal->width + column] == 0) {
                            isAllWhite = false;
                            if (line < topBlackPixelBefore) topBlackPixelBefore = line;
                            if (line > bottomBlackPixelBefore) bottomBlackPixelBefore = line;
                        }
                    }
                    if (isAllWhite) {
                        countWhite++;
                    }
                    else {
                        if (cutColumn > 0) break;
                        countWhite = 0;
                    }
                    if (countWhite >= 3) {   // need at least 3 white column to detect as separator
                        cutColumn = column;  // cutColumn is last left white column searched from right
                    }
                }
                // check position and width of logo and text
                int logoStart   = 0;
                int logoEnd     = cutColumn - 1;
                int logoWidth   = logoEnd - logoStart + 1;
                int whiteStart  = cutColumn;
                int whiteEnd    = cutColumn + countWhite - 1;
                int whiteWidth  = whiteEnd - whiteStart + 1;
                int textStart   = cutColumn + countWhite;
                int textEnd     = logoSizeFinal->width - 1;
                int textWidth   = textEnd - textStart + 1;
                int textPortion = 1000 * textWidth / decoder->GetVideoWidth();  // we can not work with pixel, depends on resolution
                dsyslog("cExtractLogo::Resize(): repeat %d, left logo: text right of logo, logo %d->%d (%dp), white %d->%d (%dp), text %d->%d (%dp), portion %d", repeat, logoStart, logoEnd, logoWidth, whiteStart, whiteEnd, whiteWidth, textStart, textEnd, textWidth, textPortion);
                // example:
                // (+) text to remove right of logo, (-) no text right of logo, do not remove
                // (+) logo 0->68 (69p), white 69->76 (8p), text 77->219 (143p), portion 198 (MTV "name of episode")
                if (textPortion > 28) {  // keep very short text as part of logo (eg. "HD") or space in logo (eg. VOXup)
                    // check hight of text
                    if ((bottomBlackPixel - topBlackPixel) <= 35) {  // changed from 24 (ZDF HD "tivi") to 35 (phoenix HD "plus")
                        dsyslog("cExtractLogo::Resize(): repeat %d, left logo: found text right of logo, cut at column %d, pixel of text: top %d bottom %d, text height %d is valid", repeat, cutColumn, topBlackPixel, bottomBlackPixel, bottomBlackPixel - topBlackPixel);
                        if ((logoSizeFinal->width - cutColumn) > 0) {
                            CutOut(bestLogoInfo, 0, logoSizeFinal->width - cutColumn, logoSizeFinal, bestLogoCorner);
#ifdef DEBUG_LOGO_RESIZE
                            // save plane 0 of logo
                            char *fileName = nullptr;
                            if (asprintf(&fileName,"%s/F__%07d-P0-C%1d_LogoResize_Repeat%d_%dCutRightText.pgm", recDir, bestLogoInfo->frameNumber, bestLogoCorner, repeat, cutStep) >= 1) {
                                ALLOC(strlen(fileName)+1, "fileName");
                                sobel->SaveSobelPlane(fileName, bestLogoInfo->sobel[0], logoSizeFinal->width, logoSizeFinal->height);
                                FREE(strlen(fileName)+1, "fileName");
                                free(fileName);
                                cutStep++;
                            }
#endif
                        }
                    }
                }
            }
        }
        dsyslog("cExtractLogo::Resize(): repeat %d: logo size after resize: %3dW X %3dH on corner %12s", repeat, logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
        if ((logoSizeFinal->width <= 10) || (logoSizeFinal->height <= 10)) {
            dsyslog("cExtractLogo::Resize(): logo size after resize is invalid");
            return false;
        }
    }
    if (CheckLogoSize(logoSizeFinal, bestLogoCorner)) {
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is valid", decoder->GetVideoWidth(), decoder->GetVideoHeight(), logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
        return true;
    }
    else {
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", decoder->GetVideoWidth(), decoder->GetVideoHeight(), logoSizeFinal->width, logoSizeFinal->height, aCorner[bestLogoCorner]);
        logoSizeFinal->height = logoHeightBeforeResize; // restore original logo size
        logoSizeFinal->width  = logoWidthBeforeResize;
        return false;
    }
}


// check if extracted picture from the corner could be a logo
// used before picture is stored in logo cantidates list
// return: true if valid
//
bool cExtractLogo::CheckValid(const sLogoInfo *actLogoInfo, const int corner) {
    if ((area.logoSize.height <= 0) || (area.logoSize.width <= 0)) return false;
    if ((corner < 0) || (corner >= CORNERS))                       return false;
    if (!actLogoInfo)                                              return false;

    // check pixel count of plane 0
    int blackPixel1 = 0;
    for (int i = 0; i < (area.logoSize.height * area.logoSize.width); i++) {
        if (actLogoInfo->sobel[0][i] == 0) blackPixel1++;
    }
    if (blackPixel1 < 254) {  // ProSieben SD 254
#ifdef DEBUG_LOGO_CORNER
        if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has not enough pixel %d in plane 0", actLogoInfo->frameNumber, aCorner[corner], blackPixel1);
#endif
        return false;
    }
#ifdef DEBUG_LOGO_CORNER
    if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s plane 0 is valid", actLogoInfo->frameNumber, aCorner[corner]);
#endif

    // check white edge
#define WHITEHORIZONTAL_BIG   20
#define WHITEHORIZONTAL_SMALL 10
    if (corner <= TOP_RIGHT) {
        // check for big white space above logo
        for (int line = 0; line < WHITEHORIZONTAL_BIG; line++) {
            for (int column = 0; column < area.logoSize.width; column++) {
                if (actLogoInfo->sobel[0][line * area.logoSize.width + column] == 0) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no big white top part", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                    return false;
                }
            }
        }
        if ((corner != TOP_RIGHT) || !CompareChannelName(channelName, "SPORT1", IGNORE_NOTHING)) { // this channels have sometimes a big preview text below the logo on the right side
            // more general solution will be: make the possible logo size bigger
            // but this wll have a performence impact
            for (int i = (area.logoSize.height - WHITEHORIZONTAL_SMALL) * area.logoSize.width; i < (area.logoSize.height * area.logoSize.width); i++) { // a valid top logo should have at least a small white bottom part in plane 0
                if (actLogoInfo->sobel[0][i] == 0) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no small white bottom part in plane 0", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                    return false;
                }
            }
        }

    }
    else {
        for (int i = (area.logoSize.height - WHITEHORIZONTAL_BIG) * area.logoSize.width; i < (area.logoSize.height * area.logoSize.width); i++) { // a valid bottom logo should have a white bottom part in plane 0
            if (actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no big white bottom part in plane 0", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                return false;
            }
        }
        if (!CompareChannelName(channelName, "n-tv", IGNORE_HD)) {  // this channel has a news ticket with info banner above, we will not find a small white top part
            for (int i = 0 ; i < WHITEHORIZONTAL_SMALL * area.logoSize.width; i++) { // a valid bottom logo should have at least a small white top part in plane 0
                if (actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no small white top part in plane 0", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                    return false;
                }
            }
        }
    }

    // check left and right white part
    int leftWhite  = 0;
    int rightWhite = 0;
    if ((corner == TOP_LEFT) || (corner == BOTTOM_LEFT)) {   // a valid left logo should have big white left part and a small white right part in pane 0
        leftWhite  = 20;
        if (!criteria->LogoInNewsTicker()) rightWhite = 10;  // news ticker is direct after logo, no white space
    }
    else {                                                   // a valid right logo should have small white left part and a big white right part in pane 0
        if (!criteria->LogoInNewsTicker()) leftWhite  = 10;  // news ticker is direct after logo, no white space
        rightWhite = 20;
    }
    // left part
    for (int line = 0; line < area.logoSize.height; line++) {
        for (int column = 0; column < leftWhite; column++) {
            if (actLogoInfo->sobel[0][line * area.logoSize.width + column] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no big white left part in plane 0", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                return false;
            }
        }
    }
    // right part
    for (int line = 0; line < area.logoSize.height; line++) {
        for (int column = area.logoSize.width - rightWhite - 1; column < area.logoSize.width; column++) {
            if (actLogoInfo->sobel[0][line * area.logoSize.width + column] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CheckValid(): frame (%5d): logo %s has no big white right part in plane 0", actLogoInfo->frameNumber, aCorner[corner]);
#endif
                return false;
            }
        }
    }
    return true;
}


int cExtractLogo::Compare(sLogoInfo *actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!actLogoInfo)  return 0;
    if (logoHeight <= 0)   return 0;
    if (logoWidth <= 0)    return 0;
    if (corner < 0)        return 0;
    if (corner >= CORNERS) return 0;

    int hits = 0;

    for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
        if (criteria->IsLogoRotating()) {
            if (CompareLogoPairRotating(&(*actLogo), actLogoInfo, logoHeight, logoWidth, corner)) {
                hits++;
                actLogo->hits++;
            }
        }
        else {
            if (CompareLogoPair(&(*actLogo), actLogoInfo, logoHeight, logoWidth, corner)) {
                hits++;
                actLogo->hits++;
            }
        }
    }
    return hits;
}


/**
 * special detection for rotating logos
 */
bool cExtractLogo::CompareLogoPairRotating(sLogoInfo *logo1, sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner) {
    if (!logo1)            return false;
    if (!logo2)            return false;
    if (corner < 0)        return false;
    if (corner >= CORNERS) return false;

// TODO do not hardcode the logo range
    int logoStartLine   = 0;
    int logoEndLine     = 0;
    int logoStartColumn = 0;
    int logoEndColumn   = 0;
    if (CompareChannelName(channelName, "SAT_1", IGNORE_COUNTRY) || CompareChannelName(channelName, "SAT_1_A", IGNORE_NOTHING)) {
        logoStartLine   =  18;
        logoEndLine     =  75;
        logoStartColumn = 143;
        logoEndColumn   = 192;
    }
    // SAT_1_HD                16:9 1920W 1080H:->  204W 132H TOP_RIGHT
    else if (CompareChannelName(channelName, "SAT_1_HD", IGNORE_NOTHING)) {
        logoStartLine   =  60;
        logoEndLine     = 133;
        logoStartColumn = logoWidth - 204 - 10;  // 10 pixel add space
        logoEndColumn   = logoWidth - 80;        // 80 = logo end distance from the right side
    }
    else {
        dsyslog("cExtractLogo::CompareLogoPairRotating(): channel unknown");
        return false;
    }
// check if pixel in both frames are only in the corner but let the pixel be different
    if (corner != TOP_RIGHT) return false; // to optimze performance, only check TOP_RIGHT (SAT.1)
// we use only logo with pixel in the expected logo range
    for (int line = 0; line < logoHeight; line++) {
        for (int column = 0; column < logoWidth; column++) {
            if ((line >= logoStartLine) && (line < logoEndLine) && (column >= logoStartColumn) && (column <= logoEndColumn)) continue;
            if (logo1->sobel[0][line * logoWidth + column] == 0) {
#ifdef DEBUG_LOGO_CORNER
                dsyslog("cExtractLogo::CompareLogoPairRotating(): packet logo1 (%5d) pixel out of valid range: line %3d (%d->%d), column %3i (%d->%d)", logo1->frameNumber, line, logoStartLine, logoEndLine, column, logoStartColumn, logoEndColumn);
#endif
                return false;
            }
            if (logo2->sobel[0][line * logoWidth + column] == 0) {
#ifdef DEBUG_LOGO_CORNER
                dsyslog("cExtractLogo::CompareLogoPairRotating(): packet logo2 (%5d) pixel out of valid range: line %3d (%d->%d), column %3d (%d->%d)", logo2->frameNumber, line, logoStartLine, logoEndLine, column, logoStartColumn, logoEndColumn);
#endif
                return false;
            }
        }
    }
#ifdef DEBUG_LOGO_CORNER
    dsyslog("cExtractLogo::CompareLogoPairRotating(): packet logo1 (%5d) valid", logo1->frameNumber);
    dsyslog("cExtractLogo::CompareLogoPairRotating(): packet logo2 (%5d) valid", logo2->frameNumber);
#endif
// merge pixel in logo range
    for (int line = logoStartLine; line <= logoEndLine; line++) {
        for (int column = logoStartColumn; column <= logoEndColumn; column++) {
            logo1->sobel[0][line * logoWidth + column] &= logo2->sobel[0][line * logoWidth + column];
            logo2->sobel[0][line * logoWidth + column] &= logo1->sobel[0][line * logoWidth + column];
        }
    }
    return true;
}


bool cExtractLogo::CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int match0, int match12, int *rate0) {
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
#define MIN_BLACK_PLANE_0 100
    if (oneBlack_0 > MIN_BLACK_PLANE_0) rate_0 = 1000 * similar_0 / oneBlack_0;   // accept only if we found some pixels
    else rate_0 = 0;
    if (oneBlack_0 == 0) rate_0 = -1;  // tell calling function, we found no pixel
    rate_1_2 = 1000 * similar_1_2 / (logoHeight * logoWidth) * 2;

    if (rate0) *rate0 = rate_0;

#define MINMATCH0 800   // reduced from 890 to 870 to 860 to 800
#define MINMATCH12 980  // reduced from 985 to 980
    if (match0 == 0) match0 = MINMATCH0;
    if (match12 == 0) match12 = MINMATCH12;

    if ((rate_0 > match0) && (rate_1_2 > match12)) {
#ifdef DEBUG_LOGO_CORNER
        if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ++++ frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->frameNumber, logo2->frameNumber, rate_0, match0, rate_1_2, match12, oneBlack_0, MIN_BLACK_PLANE_0);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_LOGO_CORNER
    if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ---- frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->frameNumber, logo2->frameNumber, rate_0, match0, rate_1_2, match12, oneBlack_0, MIN_BLACK_PLANE_0);
#endif
    return false;
}


int cExtractLogo::DeleteFrames(const int from, const int to) {
    if (from >= to) return 0;
    int deleteCount = 0;
    dsyslog("cExtractLogo::DeleteFrames(): delete frames from %d to %d", from, to);
    for (int corner = 0; corner < CORNERS; corner++) {
        for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            if (abortNow) return 0;
            if (actLogo->frameNumber < from) continue;
            if (actLogo->frameNumber <= to) {
                // free memory of sobel planes
                for (int plane = 0; plane < PLANES; plane++) {
                    delete[] actLogo->sobel[plane];
                }
                delete[] actLogo->sobel;
                FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * area.logoSize.width * area.logoSize.height, "actLogoInfo.sobel");

                // delete vector element
                FREE(sizeof(*actLogo), "logoInfoVector");
                logoInfoVector[corner].erase(actLogo--);    // "erase" increments the iterator, "for" also does, that is 1 to much
                deleteCount++;
            }
            else break;
        }
    }
    return deleteCount/4;  // 4 corner
}


int cExtractLogo::GetFirstFrame() {
    int firstFrame = INT_MAX;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (!logoInfoVector[corner].empty() && logoInfoVector[corner].front().frameNumber < firstFrame) firstFrame = logoInfoVector[corner].front().frameNumber;
    }
    return firstFrame;
}


int cExtractLogo::GetLastFrame() {
    int lastFrame = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (!logoInfoVector[corner].empty() && logoInfoVector[corner].back().frameNumber > lastFrame) lastFrame = logoInfoVector[corner].back().frameNumber;
    }
    return lastFrame;
}


int cExtractLogo::CountFrames() {
    long unsigned int count = 0;
    for (int corner = 0; corner < CORNERS; corner++) {
        if (!logoInfoVector[corner].empty() && logoInfoVector[corner].size() > count) count = logoInfoVector[corner].size();
    }
    return count;
}


bool cExtractLogo::WaitForFrames(const cDecoder *decoder, const int minFrame = 0) {
    if (!decoder) return false;

    if ((recordingFrameCount > (decoder->GetPacketNumber() + 200)) && (recordingFrameCount > minFrame)) return true; // we have already found enough frames

#define WAITTIME 60
    char *indexFile = nullptr;
    if (asprintf(&indexFile, "%s/index", recDir) == -1) {
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
        dsyslog("cExtractLogo::WaitForFrames(): frames recorded (%d) read frames (%d) minFrame (%d)", recordingFrameCount, decoder->GetPacketNumber(), minFrame);
        if ((recordingFrameCount > (decoder->GetPacketNumber() + 200)) && (recordingFrameCount > minFrame)) {
            ret = true;  // recording has enough frames
            break;
        }
        time_t now = time(nullptr);
        char systemTime[50] = {0};
        char indexTime[50] = {0};
        strftime(systemTime, sizeof(systemTime), "%d-%m-%Y %H:%M:%S", localtime(&now));
        strftime(indexTime, sizeof(indexTime), "%d-%m-%Y %H:%M:%S", localtime(&indexStatus.st_mtime));
        dsyslog("cExtractLogo::WaitForFrames(): index file size %" PRId64 " bytes, system time %s index time %s, wait %ds", indexStatus.st_size, systemTime, indexTime, WAITTIME);
        if ((difftime(now, indexStatus.st_mtime)) >= 2 * WAITTIME) {
            dsyslog("cExtractLogo::WaitForFrames(): index not growing at frame (%d), old or interrupted recording", decoder->GetPacketNumber());
            ret = false;
            break;
        }
        sleep(WAITTIME); // now we sleep and hopefully the index will grow
    }
    FREE(strlen(indexFile)+1, "indexFile");
    free(indexFile);
    return ret;
}


void cExtractLogo::RemovePixelDefects(sLogoInfo *logoInfo, const int corner) {
    if (!logoInfo)         return;
    if (corner < 0)        return;
    if (corner >= CORNERS) return;

    for (int plane = 0; plane < PLANES; plane++) {
        int height;
        int width;
        if (plane == 0) {
            width  = area.logoSize.width;
            height = area.logoSize.height;
        }
        else {
            width  = area.logoSize.width  / 2;
            height = area.logoSize.height / 2;
        }
#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 1
        if (corner == DEBUG_LOGO_CORNER) {
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d-P%1d-C%1d_RemovePixelDefects_1before.pgm", recDir, logoInfo->frameNumber, plane, corner) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                sobel->SaveSobelPlane(fileName, logoInfo->sobel[plane], width, height);
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
        }
#endif
        // remove single separate pixel, add single missing pixel
        for (int line = height - 2; line >= 1; line--) {  // elements are from 0 to height -1 but we check neighbor pixel
            for (int column = 0; column < width; column++) {
                if ( logoInfo->sobel[plane][line * width + column] == 0) {  // remove single separate pixel
                    if (( logoInfo->sobel[plane][(line + 1) * width + column] == 255) &&
                            (logoInfo->sobel[plane][(line - 1) * width + column] == 255) &&
                            (logoInfo->sobel[plane][line * width + (column + 1)] == 255) &&
                            (logoInfo->sobel[plane][line * width + (column - 1)] == 255) &&
                            (logoInfo->sobel[plane][(line + 1) * width + (column + 1)] == 255) &&
                            (logoInfo->sobel[plane][(line - 1) * width + (column - 1)] == 255)) {
                        logoInfo->sobel[plane][line * width + column] = 255;
#if defined(DEBUG_LOGO_CORNER)
                        dsyslog("cExtractLogo::RemovePixelDefects(): fix single separate pixel found at line %d column %d at frame %d in plane %d", line, column, logoInfo->frameNumber, plane);
#endif
                    }
                }
                else if ( logoInfo->sobel[plane][line * width + column] == 255) {  //  add single missing pixel
                    if (( logoInfo->sobel[plane][(line + 1) * width + column] == 0) &&
                            (logoInfo->sobel[plane][(line - 1) * width + column] == 0) &&
                            (logoInfo->sobel[plane][line * width + (column + 1)] == 0) &&
                            (logoInfo->sobel[plane][line * width + (column - 1)] == 0) &&
                            (logoInfo->sobel[plane][(line + 1) * width + (column + 1)] == 0) &&
                            (logoInfo->sobel[plane][(line - 1) * width + (column - 1)] == 0)) {
                        logoInfo->sobel[plane][line * width + column] = 0;
                    }
                }
            }
        }

        // logos does not have a lot of white lines, remove false pixel below big white space of top logos
        if ((corner == TOP_LEFT) || (corner == TOP_RIGHT)) {
            // search for first line of logo
            int topLogoLine = -1;
            for (int line = 0; line < height; line++) {
                int blackPixel = 0;
                for (int column = 0; column < width; column++) {
                    if (logoInfo->sobel[plane][(line) * width + column] == 0) {
                        blackPixel++;
                        if (blackPixel > 10) {   // be sure we found logo and not single false pixel
                            topLogoLine = line;
                            break;
                        }
                    }
                }
                if (topLogoLine >= 0) break;
            }
            if (topLogoLine >= 0) {
                // search for end of logo
                int whiteLines     = 0;
                int bottomLogoLine = -1;
                for (int line = topLogoLine; line < height; line++) {
                    bool haveBlack = false;
                    int blackPixel = 0;
                    for (int column = 0; column < width; column++) {
                        if (logoInfo->sobel[plane][(line) * width + column] == 0) {
                            blackPixel++;
                            if (blackPixel > 10) {
                                haveBlack  = true;
                                whiteLines = 0;
                                break;
                            }
                        }
                    }
                    if (!haveBlack) whiteLines++;
                    else bottomLogoLine = line;
                    if (whiteLines >= 30) break;    // we are now under logo
                }
#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 1
                dsyslog("cExtractLogo::RemovePixelDefects(): frame (%d), plane %d: logo top line %d, bottom line %d, white lines after %d", logoInfo->frameNumber, plane, topLogoLine, bottomLogoLine, whiteLines);
#endif
                if ((bottomLogoLine >= 0) && (whiteLines >= 30)) {
                    // clean pixel below logo
                    for (int line = bottomLogoLine + whiteLines - 1; line < height; line++) {
                        for (int column = 0; column < width; column++) {
                            logoInfo->sobel[plane][(line) * width + column] = 255;
                        }
                    }
                }
            }
        }

#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 1
        if (corner == DEBUG_LOGO_CORNER) {
            char *fileName = nullptr;
            if (asprintf(&fileName,"%s/F__%07d-P%1d-C%1d_RemovePixelDefects_2after.pgm", recDir, logoInfo->frameNumber, plane, corner) >= 1) {
                ALLOC(strlen(fileName)+1, "fileName");
                sobel->SaveSobelPlane(fileName, logoInfo->sobel[plane], width, height);
                FREE(strlen(fileName)+1, "fileName");
                free(fileName);
            }
        }
#endif
    }
}


int cExtractLogo::AudioInBroadcast() {
    // AudioState 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
    if (decoder->GetAC3ChannelCount() > 2) {
        if (audioState == 1) {
            dsyslog("cExtractLogo::AudioInBroadcast(): got first time 6 channel at frame (%d)", decoder->GetPacketNumber());
            audioState = 2;
            return audioState;
        }
        if (audioState == 3) {
            dsyslog("cExtractLogo::AudioInBroadcast(): 6 channel start at frame (%d)", decoder->GetPacketNumber());
            audioState = 2;
            return audioState;
        }
    }
    else {
        if (audioState == 0) {
            dsyslog("cExtractLogo::AudioInBroadcast(): got first time 2 channel at frame (%d)", decoder->GetPacketNumber());
            audioState = 1;
            return audioState;
        }
        if (audioState == 2) {
            dsyslog("cExtractLogo::AudioInBroadcast(): 2 channel start at frame (%d)", decoder->GetPacketNumber());
            audioState = 3;
            return audioState;
        }
    }
    return audioState;
}


void cExtractLogo::ManuallyExtractLogo(const int corner, const int width, const int height) {
    // allocate area result buffer
    area.logoSize.width  = width;
    area.logoSize.height = height;
    area.logoCorner      = corner;
    sobel->AllocAreaBuffer(&area);              // allocate memory for result buffer

    while (decoder->DecodeNextFrame(false)) {
        if (abortNow) return;
        int frameNumber = decoder->GetPacketNumber();
        const sVideoPicture *picture = decoder->GetVideoPicture();
        if (picture) {
            // logo name
            char *logoName=nullptr;
            sAspectRatio *aspectRatio = decoder->GetFrameAspectRatio();
            if (asprintf(&logoName,"%s-A%d_%d", criteria->GetChannelName(), aspectRatio->num, aspectRatio->den) < 0) {
                esyslog("cExtractLogo::ManuallyExtractLogo(): asprintf failed");
                return;
            }
            ALLOC(strlen(logoName) + 1, "logoName");
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
            if (sobel->SobelPicture(recDir, picture, &area, true) > 0)
#else
            if (sobel->SobelPicture(picture, &area, true) > 0)
#endif
            {
                for (int plane = 0; plane < PLANES; plane++) {
                    char *fileName = nullptr;
                    if (asprintf(&fileName,"%s/F__%07d-%s-P%d.pgm", recDir, frameNumber, logoName, plane) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        if (plane == 0) sobel->SaveSobelPlane(fileName, area.sobel[plane], area.logoSize.width, area.logoSize.height);
                        else sobel->SaveSobelPlane(fileName, area.sobel[plane], area.logoSize.width / 2, area.logoSize.height / 2);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                }

            }
            FREE(strlen(logoName) + 1, "logoName");
            free (logoName);
        }
        if (frameNumber > 2000) break;
    }
}


// return -1 internal error, 0 ok, > 0 no logo found, return last framenumber of search
int cExtractLogo::SearchLogo(int startPacket, const bool force) {
    LogSeparator(true);
    dsyslog("cExtractLogo::SearchLogo(): extract logo from packet %d requested aspect ratio %d:%d, force = %d", startPacket, requestedLogoAspectRatio.num, requestedLogoAspectRatio.den, force);
    if (startPacket < 0) return LOGO_SEARCH_ERROR;
    if (criteria->NoLogo()) {
        dsyslog("cExtractLogo::SearchLogo(): channel have no continuous logo");
        return LOGO_SEARCH_ERROR;
    }

    // set start time for statistics
    struct timeval startTime;
    gettimeofday(&startTime, nullptr);

#define MAX_READ_PACKETS 3000
#define MIN_VALID_FRAMES 1000
    int packetsRead         = 0;
    bool logoFound          = false;
    sLogoSize logoSizeFinal = {0};          // logo size after logo found
    int frameCountValid     = 0;            // number of valid possible logo frames
    // allocate area result buffer
    if ((area.logoSize.height == 0) || (area.logoSize.width == 0)) sobel->AllocAreaBuffer(&area);  // allocate memory for result buffer with max logo size for this video resolution

    if (!WaitForFrames(decoder)) {
        dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed");
        return LOGO_SEARCH_ERROR;
    }

// set start point
    DeleteFrames(0, startPacket - 1);
    int firstFrame = GetFirstFrame();
    int lastFrame  = GetLastFrame();
    int countFrame = CountFrames();
    if (firstFrame == INT_MAX) dsyslog("cExtractLogo::SearchLogo(): we have no frames already stored");
    else dsyslog("cExtractLogo::SearchLogo(): already have %d frames from (%d) to frame (%d)", countFrame, firstFrame, lastFrame);
    frameCountValid = countFrame;
    if (lastFrame > startPacket) startPacket = lastFrame;

    // seek to start position
    int packetNumber = decoder->GetPacketNumber();
    if (packetNumber < startPacket) {
        dsyslog("cExtractLogo::SearchLogo(): frame (%d): seek to packet (%d)", packetNumber, startPacket);
        if (!WaitForFrames(decoder, startPacket)) {
            dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() for start packet (%d) failed", startPacket);
            return LOGO_SEARCH_ERROR;
        }
        if (!decoder->SeekToPacket(startPacket)) {
            dsyslog("cExtractLogo::SearchLogo(): seek to start packet (%d) failed", startPacket);
            return LOGO_SEARCH_ERROR;
        }
    }

    // if no aspect ratio requestet, use current from video
    decoder->DecodeNextFrame(false);   // decode one video frame to get current aspect ratio
    sAspectRatio logoAspectRatio = requestedLogoAspectRatio;
    if ((logoAspectRatio.num == 0) || (logoAspectRatio.den == 0))  {
        const sAspectRatio *aspectRatio = nullptr;
        while (!aspectRatio) {   // read and decode until we got a valid frame
            if (abortNow) return LOGO_SEARCH_ERROR;
            if (!decoder->DecodeNextFrame(false)) break;
            aspectRatio = decoder->GetFrameAspectRatio();
        }
        if (aspectRatio) {
            logoAspectRatio = *aspectRatio;
            dsyslog("cExtractLogo::SearchLogo(): packet (%d): no aspect ratio requested, set to aspect ratio of current video position %d:%d", decoder->GetPacketNumber(), logoAspectRatio.num, logoAspectRatio.den);
        }
        else {
            esyslog("cExtractLogo::SearchLogo(): no valid aspect ratio in video found");
            return LOGO_SEARCH_ERROR;
        }
    }

    while (decoder->DecodeNextFrame(false)) {  // no audio decode
        if (abortNow) return LOGO_SEARCH_ERROR;

        if (!WaitForFrames(decoder)) {
            dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed at packet (%d), got %d valid frames of %d packets read", decoder->GetPacketNumber(), frameCountValid, packetsRead);
            break;
        }
        packetNumber = decoder->GetPacketNumber();

        packetsRead++;

        if (AudioInBroadcast()  == 3) {  // we are in advertising
            continue;
        }

        // stop search if different aspect ratio
        sAspectRatio *videoAspectRatio = decoder->GetFrameAspectRatio();
        if (!videoAspectRatio) {
            esyslog("cExtractLogo::SearchLogo(): frame (%d): aspect ratio invalid", packetNumber);
            break;
        }
        if (logoAspectRatio != *videoAspectRatio) {
            dsyslog("cExtractLogo::SearchLogo(): frame (%d): aspect ratio requested %d:%d but video aspect ration %d:%d", packetNumber, logoAspectRatio.num, logoAspectRatio.den, videoAspectRatio->num, videoAspectRatio->den);
            break;
        }

        // get next video picture
        const sVideoPicture *picture = decoder->GetVideoPicture();
        if (!picture) {
            dsyslog("cExtractLogo::SearchLogo(): frame (%d): failed to get video data", decoder->GetPacketNumber());
            continue;
        }

        if (!criteria->LogoInBorder()) {
            int hBorderPacketNumber = -1;
            int64_t hBorderFramePTS = -1;
            int vBorderPacketNumber = -1;
            int64_t vBorderFramePTS = -1;
            int isHBorder = hBorder->Process(&hBorderPacketNumber, &hBorderFramePTS);
            int isVBorder = vborder->Process(&vBorderPacketNumber, &vBorderFramePTS);
            if (isHBorder != HBORDER_ERROR) {
                if (hBorderPacketNumber >= 0) {  // we had a change
                    if (isHBorder == HBORDER_VISIBLE) {
                        dsyslog("cExtractLogo::SearchLogo(): detect new horizontal border from frame (%d) to frame (%d)", hBorderPacketNumber, packetNumber);
                        frameCountValid -= DeleteFrames(hBorderPacketNumber, packetNumber);
                    }
                    else {
                        dsyslog("cExtractLogo::SearchLogo(): no horizontal border from frame (%d)", packetNumber);
                    }
                }
            }
            if (isVBorder != VBORDER_ERROR) {
                if (vBorderPacketNumber >= 0) {  // we had a change
                    if (isVBorder == VBORDER_VISIBLE) {
                        dsyslog("cExtractLogo::SearchLogo(): detect new vertical border from frame (%d) to frame (%d)", vBorderPacketNumber, packetNumber);
                        frameCountValid -= DeleteFrames(vBorderPacketNumber, packetNumber);
                    }
                    else {
                        dsyslog("cExtractLogo::SearchLogo(): no vertical border from frame (%d)", packetNumber);
                    }
                }
            }
            if (isHBorder == HBORDER_VISIBLE) {
                if (hBorderPacketNumber >= 0) dsyslog("cExtractLogo::SearchLogo(): hborder start (%d) detected, abort logo search", hBorderPacketNumber);
                else dsyslog("cExtractLogo::SearchLogo(): hborder still activ, abort logo search");
                break;
            }
            if (isVBorder == VBORDER_VISIBLE) {
                if (vBorderPacketNumber >= 0) dsyslog("cExtractLogo::SearchLogo(): vborder start (%d) detected, abort logo search", vBorderPacketNumber);
                else dsyslog("cExtractLogo::SearchLogo(): vborder still activ, abort logo search");
                break;
            }
        }
        frameCountValid++;

        // do sobbel transformation of plane 0 of all corners
        for (int corner = 0; corner < CORNERS; corner++) {
            area.logoCorner = corner;
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
            if (sobel->SobelPicture(recDir, picture, &area, true) <= 0) {
                esyslog("cExtractLogo::SearchLogo(): sobel transformation failed");
                continue; // call with no logo mask
            }
#else
            if (sobel->SobelPicture(picture, &area, true) <= 0) {
                esyslog("cExtractLogo::SearchLogo(): sobel transformation failed");
                continue; // ignoreLogo = true, call with no logo mask
            }
#endif

#if defined(DEBUG_LOGO_CORNER) && defined(DEBUG_LOGO_SAVE) && DEBUG_LOGO_SAVE == 0
            if (corner == DEBUG_LOGO_CORNER) {
                for (int plane = 0; plane < PLANES; plane++) {
                    char *fileName = nullptr;
                    if (asprintf(&fileName,"%s/F__%07d-P%1d-C%1d_SearchLogo.pgm", recDir, packetNumber, plane, corner) >= 1) {
                        ALLOC(strlen(fileName)+1, "fileName");
                        if (plane == 0) sobel->SaveSobelPlane(fileName, area.sobel[plane], area.logoSize.width, area.logoSize.height);
                        else sobel->SaveSobelPlane(fileName, area.sobel[plane], area.logoSize.width / 2, area.logoSize.height / 2);
                        FREE(strlen(fileName)+1, "fileName");
                        free(fileName);
                    }
                }
            }
#endif

            sLogoInfo actLogoInfo = {};
            actLogoInfo.frameNumber = packetNumber;

            // alloc memory and copy planes
            int logoPixel     = area.logoSize.height * area.logoSize.width;
            actLogoInfo.sobel = new uchar*[PLANES];
            for (int plane = 0; plane < PLANES; plane++) {
                actLogoInfo.sobel[plane] = new uchar[logoPixel];
                memcpy(actLogoInfo.sobel[plane], area.sobel[plane], sizeof(uchar) * logoPixel);
            }
            ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "actLogoInfo.sobel");

            if (CheckValid(&actLogoInfo, corner)) {
                RemovePixelDefects(&actLogoInfo, corner);
                actLogoInfo.hits = Compare(&actLogoInfo, area.logoSize.height, area.logoSize.width, corner);

                try {
                    logoInfoVector[corner].push_back(actLogoInfo);    // this allocates a lot of memory
                }
                catch(std::bad_alloc &e) {
                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", packetNumber);
                    break;
                }
                ALLOC((sizeof(sLogoInfo)), "logoInfoVector");
            }
            else {  // corner sobel transformed picture not valid
                // free memory of sobel planes
                for (int plane = 0; plane < PLANES; plane++) {
                    delete[] actLogoInfo.sobel[plane];
                }
                delete[] actLogoInfo.sobel;
                FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * logoPixel, "actLogoInfo.sobel");
            }
        }
        if (frameCountValid > MIN_VALID_FRAMES) {
            int firstBorder = hBorder->GetFirstBorderFrame();
            if (firstBorder > 0) {
                dsyslog("cExtractLogo::SearchLogo(): detect unprocessed horizontal border from frame (%d) to frame (%d)", firstBorder, packetNumber);
                frameCountValid -= DeleteFrames(firstBorder, packetNumber);
            }
            firstBorder = vborder->GetFirstBorderFrame();
            if (firstBorder > 0) {
                dsyslog("cExtractLogo::SearchLogo(): detect unprocessed vertical border from frame (%d) to frame (%d)", firstBorder, packetNumber);
                frameCountValid -= DeleteFrames(firstBorder, packetNumber);
            }
        }
        if ((frameCountValid >= MIN_VALID_FRAMES) || (packetsRead >= MAX_READ_PACKETS)) {
            break; // finish read frames and find best match
        }
        // skip some packets to prevent to get logo from ad scene or wrong coloured logo from background
        if (fullDecode) {
            int skipPackets = decoder->GetVideoFrameRate() / 10;
            if (force) {
                if (criteria->IsLogoRotating()) skipPackets = 1;  // for rotating logo skip at least one frame to catch the full logo
                else                            skipPackets = 0;
            }
            for (int i = 1; i <= skipPackets; i++) {
                decoder->DecodeNextFrame(false);
                decoder->DropFrame();  // frame not used, drop frame buffer
            }
        }
    }

    bool doSearch = false;
    if ((packetsRead >= MAX_READ_PACKETS) || (frameCountValid >= MIN_VALID_FRAMES)) {
        dsyslog("cExtractLogo::SearchLogo(): %d valid frames of %d packets read, got enough frames at packet (%d), start analyze", frameCountValid, packetsRead, decoder->GetPacketNumber());
        doSearch = true;
    }
    else if ((packetsRead < MAX_READ_PACKETS) && ((packetsRead > MAX_READ_PACKETS / 2) || (frameCountValid > 390))) {
        // reached end of recording (or part without border) before we got 1000 valid frames out of MAXREADFRAMES decoded
        // but we got at least 390 valid frames out of MAXREADFRAMES / 2 decoded, we can work with that
        dsyslog("cExtractLogo::SearchLogo(): end of recording reached at packet (%d), read (%d) packets and got (%d) valid packets, try anyway", packetNumber, packetsRead, frameCountValid);
        doSearch = true;
    }
    else dsyslog("cExtractLogo::SearchLogo(): read (%d) packets and could not get enough valid frames (%i)", packetsRead, frameCountValid);

// search for valid logo matches
    int logoCorner[CORNERS]     = {-1, -1, -1, -1};
    sLogoInfo logoInfo[CORNERS] = {};
    int rankResult              = -1;

    if (doSearch) {
        sLogoInfo actLogoInfo[CORNERS] = {};
        for (int corner = 0; corner < CORNERS; corner++) {
            for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                if (actLogo->hits > actLogoInfo[corner].hits) {
                    actLogoInfo[corner] = *actLogo;
                }
            }
            dsyslog("cExtractLogo::SearchLogo(): best guess found at packet %6d with %3d similars out of %3zu valid packets at %s", actLogoInfo[corner].frameNumber, actLogoInfo[corner].hits, logoInfoVector[corner].size(), aCorner[corner]);

        }

        // order corner matches
        int sumHits = 0;
        for (int corner = 0; corner < CORNERS; corner++) {  // search for the best hits of each corner
            sumHits += actLogoInfo[corner].hits;
            if ((actLogoInfo[corner].hits > 0) && (actLogoInfo[corner].hits > logoInfo[0].hits)) {
                logoInfo[0]   = actLogoInfo[corner];
                logoCorner[0] = corner;
            }
        }
        for (int corner = 0; corner < CORNERS; corner++) {  // search for second best hits of each corner
            if ((actLogoInfo[corner].hits > 0) && (actLogoInfo[corner].hits > logoInfo[1].hits) && (corner != logoCorner[0])) {
                logoInfo[1]   = actLogoInfo[corner];
                logoCorner[1] = corner;
            }
        }
        for (int corner = 0; corner < CORNERS; corner++) {  // search for third best hits of each corner
            if ((actLogoInfo[corner].hits > 0) && (actLogoInfo[corner].hits > logoInfo[2].hits) && (corner != logoCorner[0]) && (corner != logoCorner[1])) {
                logoInfo[2]   = actLogoInfo[corner];
                logoCorner[2] = corner;
            }
        }
        for (int corner = 0; corner < CORNERS; corner++) {  // search for third best hits of each corner
            if ((actLogoInfo[corner].hits > 0) && (actLogoInfo[corner].hits > logoInfo[3].hits) && (corner != logoCorner[0]) && (corner != logoCorner[1]) && (corner != logoCorner[2])) {
                logoInfo[3]   = actLogoInfo[corner];
                logoCorner[3] = corner;
            }
        }
        dsyslog("cExtractLogo::SearchLogo(): corner rank:");
        for (int rank = 0; rank < CORNERS; rank++) {
            if (logoCorner[rank] < 0) break;
            dsyslog("cExtractLogo::SearchLogo(): %d.: %-12s %3d similars", rank, aCorner[logoCorner[rank]], logoInfo[rank].hits);
        }

        // try good matches, use max 3 best corners
        int done = -1;
        for (int rank = 0; rank < CORNERS - 1; rank++) {
            dsyslog("cExtractLogo::SearchLogo(): check %d. best corner ---------------------------------------------------------------", rank);
            if (logoCorner[rank] < 0) break;    // no more matches
            if ((logoInfo[rank].hits >= 40) ||                                                 // we have a good result, changed from 50 to 46 to 40
                    ((logoInfo[rank].hits >= 30) && (sumHits <= logoInfo[rank].hits + 8)) ||   // if almost all hits are in the same corner than less are enough
                    ((logoInfo[rank].hits >= 20) && (sumHits <= logoInfo[rank].hits + 7)) ||   // if almost all hits are in the same corner than less are enough
                    ((logoInfo[rank].hits >= 10) && (sumHits <= logoInfo[rank].hits + 6)) ||   // if almost all hits are in the same corner than less are enough
                    ((logoInfo[rank].hits >=  5) && (sumHits == logoInfo[rank].hits))) {       // if all hits are in the same corner than less are enough
                dsyslog("cExtractLogo::SearchLogo(): %d. best corner is %s at packet %d with %d similars", rank, aCorner[logoCorner[rank]], logoInfo[rank].frameNumber, logoInfo[rank].hits);
                // check possible logo
                logoSizeFinal = area.logoSize;
                logoInfo[rank].resized = true;
                if (Resize(&logoInfo[rank], &logoSizeFinal, logoCorner[rank])) {  // logo can be valid
                    done = rank;
                    rankResult = rank;
                    dsyslog("cExtractLogo::SearchLogo(): resize logo from %d. best corner %s was successful, %dW x %dH", rank, aCorner[logoCorner[rank]], logoSizeFinal.width, logoSizeFinal.height);
                    // check next best possible logo corner, it is valid too, we can not decide
                    if ((logoInfo[rank + 1].hits >= 40) || (logoInfo[rank + 1].hits > (logoInfo[rank].hits * 0.8))) { // next best logo corner has high matches
                        dsyslog("cExtractLogo::SearchLogo(): %d. best corner %d at packet %d with %d similars", rank + 1, logoCorner[rank + 1], logoInfo[rank + 1].frameNumber, logoInfo[rank + 1].hits);
                        sLogoSize secondLogoSize = area.logoSize;
                        logoInfo[rank + 1].resized = true;
                        if (Resize(&logoInfo[rank + 1], &secondLogoSize, logoCorner[rank + 1])) { // second best logo can be valid
                            dsyslog("cExtractLogo::SearchLogo(): resize logo from %d. and %d. best corner is valid, still no clear result", rank, rank + 1);
                            rankResult = -1;
                            break;
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): resize logo failed from %d. best corner, use %d. best corner", rank + 1, rank);
                            break;
                        }
                    }
                    else break;
                }
                else dsyslog("cExtractLogo::SearchLogo(): resize logo from %d. best corner failed", rank + 1);
            }
        }

        // try low matches
        if ((rankResult == -1) && force) {  // last try to get a logo, use all corners
            for (int rank = done + 1; rank < CORNERS; rank++) {
                dsyslog("cExtractLogo::SearchLogo(): check %d. best corner for all corners -------------------------------------------------------------------", rank);
                if (logoCorner[rank] < 0) break;    // no more matches
                if ((logoInfo[rank].hits >= 4) ||  // this is the very last try, use what we have, bettet than nothing, changed from 6 to 4
                        ((logoInfo[rank].hits >=  2) && (sumHits == logoInfo[rank].hits))) {  // all machtes in one corner
                    dsyslog("cExtractLogo::SearchLogo(): try low match with %d best corner %s at frame %d with %d similars", rank, aCorner[logoCorner[rank]], logoInfo[rank].frameNumber, logoInfo[rank].hits);
                    logoSizeFinal = area.logoSize;
                    if (!logoInfo[rank].resized && Resize(&logoInfo[rank], &logoSizeFinal, logoCorner[rank])) {
                        rankResult = rank;
                        break;
                    }
                }
            }
        }
    }
    else dsyslog("cExtractLogo::SearchLogo(): no similar frames for logo detection found");

    if (rankResult < 0) {
        dsyslog("cExtractLogo::SearchLogo(): no valid logo found");
    }
    else {
        dsyslog("cExtractLogo::SearchLogo(): save corner %s as logo, %dW x %dH", aCorner[logoCorner[rankResult]], logoSizeFinal.width, logoSizeFinal.height);
        if (!SaveLogo(&logoInfo[rankResult], &logoSizeFinal, logoAspectRatio, logoCorner[rankResult])) {
            dsyslog("cExtractLogo::SearchLogo(): logo save failed");
        }
        else logoFound = true;
    }

    struct timeval stopTime;
    gettimeofday(&stopTime, nullptr);
    time_t sec = stopTime.tv_sec - startTime.tv_sec;
    suseconds_t usec = stopTime.tv_usec - startTime.tv_usec;
    if (usec < 0) {
        usec += 1000000;
        sec--;
    }
    logoSearchTime_ms += sec * 1000 + usec / 1000;

    if (abortNow) {
        dsyslog("cExtractLogo::SearchLogo(): aborted by user");
        return LOGO_SEARCH_ERROR;
    }

    if (logoFound) {
        dsyslog("cExtractLogo::SearchLogo(): finished successfully, last frame %d", packetNumber);
        return LOGO_SEARCH_FOUND;
    }
    else {
        dsyslog("cExtractLogo::SearchLogo(): failed, last frame %d", packetNumber);
        if (packetNumber > 0) return packetNumber;   // return last frame from search to setup new search after this
        else return LOGO_SEARCH_ERROR;                // nothing read, retry makes no sense

    }
}
