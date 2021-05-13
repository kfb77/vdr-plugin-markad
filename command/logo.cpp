/*
 * logo.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <sys/stat.h>
#include <unistd.h>

#include "logo.h"
#include "index.h"

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
#define LOGO_MIN_LETTERING_H 38 // 41 for "DIE NEUEN FOLGEN" SAT_1
                                // 38 for "#wir bleiben zuhause" RTL2
#define LOGO_MAX_LETTERING_H 56 // 56 for II over RTL (old RTL2 logo)

// global variables
extern bool abortNow;
extern int logoSearchTime_ms;



cExtractLogo::cExtractLogo(sMarkAdContext *maContext, const sAspectRatio AspectRatio, cIndex *recordingIndex) {
    logoAspectRatio.num = AspectRatio.num;
    logoAspectRatio.den = AspectRatio.den;
    recordingIndexLogo = recordingIndex;
    maContextLogoSize = maContext;
}


cExtractLogo::~cExtractLogo() {
#ifdef DEBUG_MEM
    int maxLogoPixel = GetMaxLogoPixel(maContextLogoSize->Video.Info.width);
#endif
    for (int corner = 0; corner < CORNERS; corner++) {  // free memory of all corners
#ifdef DEBUG_MEM
        int size = logoInfoVector[corner].size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(sLogoInfo), "logoInfoVector");
        }
        size = logoInfoVectorPacked[corner].size();
        for (int i = 0 ; i < size; i++) {
            FREE(sizeof(sLogoInfoPacked), "logoInfoVectorPacked");
        }
#endif
        // free memory of sobel plane vector
        for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            for (int plane = 0; plane < PLANES; plane++) {
                delete actLogo->sobel[plane];
            }
            delete actLogo->sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");
        }
        logoInfoVector[corner].clear();

        // free memory of packed sobel plane vector
        for (std::vector<sLogoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
            for (int plane = 0; plane < PLANES; plane++) {
                delete actLogoPacked->sobel[plane];
            }
            delete actLogoPacked->sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel / 8, "actLogoInfoPacked.sobel");
        }
        logoInfoVectorPacked[corner].clear();
    }
}


bool cExtractLogo::IsWhitePlane(const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane) {
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


void cExtractLogo::SetLogoSize(const sMarkAdContext *maContext, int *logoHeight, int *logoWidth) {
    if (!maContext) return;
    if (!logoHeight) return;
    if (!logoWidth) return;
    sLogoSize DefaultLogoSize = GetDefaultLogoSize(maContext->Video.Info.width);
    *logoHeight = DefaultLogoSize.height;
    *logoWidth = DefaultLogoSize.width;
}


// check plane 1 and 2, calculate quote of white pictures
// return: true if only some frames have have pixels in plane >=1, a channel with logo coulor change is detected
//         false if almost all frames have pixel in plane >=1, this is realy a coloured logo
//
bool cExtractLogo::IsLogoColourChange(const sMarkAdContext *maContext, const int corner) {
    if (!maContext) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;

    int logoHeight = 0;
    int logoWidth = 0;
    SetLogoSize(maContext, &logoHeight, &logoWidth);
    logoHeight /= 2;  // we use plane 1 to check
    logoWidth /= 2;
#ifdef DEBUG_MEM
    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
#endif

    int count = 0;
    int countWhite[2] = {};

    for (int plane = 1; plane < PLANES; plane++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            for (std::vector<sLogoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                if (plane == 1) count++;
                sLogoInfo actLogo = {};
                UnpackLogoInfo(&actLogo, &(*actLogoPacked));
                if (IsWhitePlane(&actLogo, logoHeight, logoWidth, plane)) countWhite[plane - 1]++;
                // free memory of unpacked sobel plane
                for (int planeTMP = 0; planeTMP < PLANES; planeTMP++) {
                    delete actLogo.sobel[planeTMP];
                }
                delete actLogo.sobel;
                FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");
           }
        }
        if (maContext->Config->autoLogo == 2){  // use unpacked logos
            for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                if (plane == 1) count++;
                if (IsWhitePlane(&(*actLogo), logoHeight, logoWidth, plane)) countWhite[plane - 1]++;
            }
        }
    }
    if (count > 0) {
        dsyslog("cExtractLogo::isLogoColourChange(): %4d valid frames in corner %d, plane 1: %3d are white, ratio %3d%%", count, corner, countWhite[0], countWhite[0] * 100 / count);
        dsyslog("cExtractLogo::isLogoColourChange():                                plane 2: %3d are white, ratio %3d%%", countWhite[1], countWhite[1] * 100 / count);
        if (((countWhite[0] * 100 / count) >= 40) && ((countWhite[1] * 100 / count) >= 40)) return true;
    }
    return false;
}


bool cExtractLogo::Save(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner, const int framenumber = -1, const char *debugText = NULL) { // framenumber >= 0: save from debug function
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

            if (asprintf(&buf, "%s/%s-A%i_%i-P%i.pgm", maContext->Config->recDir, maContext->Info.ChannelName, logoAspectRatio.num, logoAspectRatio.den, plane)==-1) return false;
            ALLOC(strlen(buf)+1, "buf");
            dsyslog("cExtractLogo::Save(): store logo in %s", buf);
            isyslog("Logo size for Channel: %s %d:%d %dW %dH: %dW %dH %s", maContext->Info.ChannelName, logoAspectRatio.num, logoAspectRatio.den, maContext->Video.Info.width, maContext->Video.Info.height, logoWidth, logoHeight, aCorner[corner]);
        }
        else {  // debug function, store logo to /tmp
            if (debugText) {
                if (asprintf(&buf, "%s/%06d-%s-A%i_%i-P%i_%s.pgm", "/tmp/",framenumber, maContext->Info.ChannelName, logoAspectRatio.num, logoAspectRatio.den, plane, debugText) == -1) return false;
            }
            else {
                if (asprintf(&buf, "%s/%06d-%s-A%i_%i-P%i.pgm", "/tmp/",framenumber, maContext->Info.ChannelName, logoAspectRatio.num, logoAspectRatio.den, plane) == -1) return false;
            }
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


void cExtractLogo::CutOut(sLogoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, const int corner) {
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


bool cExtractLogo::CheckLogoSize(const sMarkAdContext *maContext, const int logoHeight, const int logoWidth, const int logoCorner) {
    if (!maContext) return false;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return false;
    struct logo_struct {
       int widthMin  =  0;
       int widthMax  =  0;
       int heightMin =  0;
       int heightMax =  0;
       int corner    = -1;
    } logo;
// define size for special channels, special logos or to prevent false logo detection

// 720x576
    if (strcmp(maContext->Info.ChannelName, "ATV2") == 0) {             //
        logo.widthMax  = 110;
    }
    if (strcmp(maContext->Info.ChannelName, "Disney_Channel") == 0) {   // Disney_Channel:         16:9  720W  576H:->  110W  70-72H TOP_LEFT
        logo.widthMin  = 110;
        logo.heightMin =  70;
    }
    if (strcmp(maContext->Info.ChannelName, "DMAX") == 0) {             // DMAX                    16:9  720W  576H:->  126W  74H TOP_LEFT
        logo.widthMin  = 126;
    }
    if (strcmp(maContext->Info.ChannelName, "kabel_eins_Doku") == 0) {  // kabel_eins_Doku         16:9  720W  576H:->  130W  64H TOP_RIGHT
                                                                        // kabel_eins_Doku         16:9  720W  576H:->  132W  64H TOP_RIGHT
        logo.widthMax  = 132;
    }
    if (strcmp(maContext->Info.ChannelName, "Nickelodeon") == 0) {      // Nickelodeon old logo    16:9  720W  576H:->  180W  78H TOP_LEFT
                                                                        // Nickelodeon old logo    16:9  720W  576H:->  180W  80H TOP_LEFT
                                                                        // Nickelodeon old logo    16:9  720W  576H:->  184W  80H TOP_LEFT
                                                                        // Nickelodeon new logo    16:9  720W  576H:->  144W  88H TOP_LEFT
                                                                        // Nickelodeon new logo    16:9  720W  576H:->  146W  88H TOP_LEFT
        logo.widthMin  = 144;
        logo.widthMax  = 184;
        logo.heightMin =  78;
    }
    if (strcmp(maContext->Info.ChannelName, "n-tv") == 0) {             //  n-tv                    16:9  720W  576H:->  224W  60H BOTTOM_RIGHT
        logo.widthMax  = INT_MAX;  // news ticker
    }
    if (strcmp(maContext->Info.ChannelName, "ProSieben") == 0) {        // ProSieben               16:9  720W  576H:->   84W  66H TOP_RIGHT
                                                                        // ProSieben                4:3  720W  576H:->  100W  66H TOP_RIGHT
        logo.widthMax  =  100;
    }
    if (strcmp(maContext->Info.ChannelName, "RTL_Television") == 0) {   // RTL_Television          16:9  720W  576H:->  104W  60H TOP_LEFT (normal RTL logo)
                                                                        // RTL_Television          16:9  720W  576H:->  142W  60H TOP_LEFT ( RTL live logo)
        logo.widthMax  = 142 ;
    }
    if (strcmp(maContext->Info.ChannelName, "RTL2") == 0) {             // RTL2                    16:9  720W  576H:->   82W  78H BOTTOM_RIGHT (new logo)
                                                                        // RTL2                    16:9  720W  576H:->  108W 108H BOTTOM_RIGHT (old logo)
        logo.widthMin  =  81;
        logo.widthMax  = 109;
        logo.heightMin =  77;
        logo.heightMax = 109;
    }
    if (strcmp(maContext->Info.ChannelName, "RTLplus") == 0) {          // RTLplus                 16:9  720W  576H:->  168W  64H TOP_LEFT
        logo.widthMin  = 168;
        logo.widthMax  = 168;
    }
    if (strcmp(maContext->Info.ChannelName, "SIXX") == 0) {             // SIXX                    16:9  720W  576H:->   98W  54H TOP_RIGHT (new logo)
                                                                        // SIXX                    16:9  720W  576H:->  108W  56H TOP_RIGHT (old logo)
        logo.widthMin  =  98;
        logo.heightMin =  54;
    }
    if (strcmp(maContext->Info.ChannelName, "SUPER_RTL") == 0) {        // SUPER_RTL               16:9  720W  576H:-> 160W  54H TOP_LEFT
        logo.widthMin  = 160;
        logo.widthMax  = 160;
        logo.heightMin =  54;
    }
    if (strcmp(maContext->Info.ChannelName, "TELE_5") == 0) {           // TELE_5                  16:9  720W  576H:->   72W  76H BOTTOM_RIGHT (new logo)
                                                                        // TELE_5                   4:3  720W  576H:->   94W  76H BOTTOM_RIGHT (new logo)
                                                                        // TELE_5                  16:9  720W  576H:->  108W  64H BOTTOM_RIGHT (old logo)
        logo.widthMin  =  72;
        logo.widthMax  = 108;
        logo.heightMin =  64;
        logo.heightMax =  76;
        logo.corner    = BOTTOM_RIGHT;
    }
    if (strcmp(maContext->Info.ChannelName, "TOGGO_plus") == 0) {       // TOGGO_plus              16:9  720W  576H:->  104W  56H TOP_LEFT
        logo.heightMin =  56;
    }
    if (strcmp(maContext->Info.ChannelName, "VOX") == 0) {              // VOX                     16:9  720W  576H:->  108W  70H TOP_LEFT
                                                                        // VOX                      4:3  720W  576H:->  126W  70H TOP_LEFT
        logo.heightMin =  70;
    }
    if (strcmp(maContext->Info.ChannelName, "WELT") == 0) {             // WELT                    16:9  720W  576H:->  222W  60H BOTTOM_LEFT
        logo.widthMax  = INT_MAX;  // news ticker
    }
    if (strcmp(maContext->Info.ChannelName, "Welt_der_Wunder") == 0) {  // Welt_der_Wunder         16:9  720W  576H:->   94W 108H TOP_LEFT
                                                                        // Welt_der_Wunder         16:9  720W  576H:->   96W 112H TOP_LEFT
        logo.widthMin  =  94;
        logo.heightMax = 112;
    }

// 1280x720
    if (strcmp(maContext->Info.ChannelName, "arte_HD") == 0) {          // arte_HD                 16:9 1280W  720H:->   88W 134H TOP_LEFT
        logo.widthMin  = 88;
    }
    if (strcmp(maContext->Info.ChannelName, "EinsPlus_HD") == 0) {      // EinsPlus_HD             16:9 1280W  720H:->  334W  86H TOP_RIGHT
        logo.widthMax  = 335;
    }
    if (strcmp(maContext->Info.ChannelName, "hr-fernsehen_HD") == 0) {  // hr-fernsehen_HD         16:9 1280W  720H:->  198W  98H TOP_LEFT
        logo.heightMax  =  98;
    }

// 1280x1080
    if (strcmp(maContext->Info.ChannelName, "ANIXE+") == 0) {           // ANIXE+                  16:9 1280W 1080H:->  254W 170H TOP_LEFT
        logo.heightMax = 170;
    }
    if (strcmp(maContext->Info.ChannelName, "Deluxe_Music_HD") == 0) {  // Deluxe_Music_HD         16:9 1280W 1080H:->  334W 124H TOP_RIGHT
        logo.widthMax  = 334;
    }

// 1440x1080
    if (strcmp(maContext->Info.ChannelName, "WELT_HD") == 0) {          // WELT_HD                 16:9 1440W 1080H:->  396W 116H BOTTOM_LEFT
        logo.widthMax  = INT_MAX;  // news ticker
    }

// 1920x1080
    if (strcmp(maContext->Info.ChannelName, "13th_Street_HD") == 0) {   // 13th_Street_HD:         16:9 1920W 1080H:->  218W 194H TOP_LEFT
        logo.widthMin  = 217;
        logo.heightMax = 195;
    }
    if (strcmp(maContext->Info.ChannelName, "münchen_tv_HD") == 0) {
        logo.widthMax  = 396;
    }

// set default values
    switch (maContext->Video.Info.width) {
        case 544:
            if (logo.widthMin  == 0) logo.widthMin  =  95; // DMAX_Austria            16:9  544W  576H:->   96W  90H TOP_LEFT
            if (logo.widthMax  == 0) logo.widthMax  =  97; // DMAX_Austria            16:9  544W  576H:->   96W  90H TOP_LEFT
            if (logo.heightMin == 0) logo.heightMin =  89; // DMAX_Austria            16:9  544W  576H:->   96W  90H TOP_LEFT
            if (logo.heightMax == 0) logo.heightMax =  91; // DMAX_Austria            16:9  544W  576H:->   96W  90H TOP_LEFT
            break;
        case 720:
            if (logo.widthMin  == 0) logo.widthMin  =  76; // SAT_1                   16:9  720W  576H:->   76W  72H TOP_RIGHT
            if (logo.widthMax  == 0) logo.widthMax  = 146; // N24_DOKU                16:9  720W  576H:->  146W  82H TOP_RIGHT
                                                           // Nickelodeon             16:9  720W  576H:->  146W  88H TOP_LEFT
                                                           // NICK_MTV+               16:9  720W  576H:->  146W  88H TOP_LEFT
                                                           // SIXX                     4:3  720W  576H:->  146W  70H TOP_RIGHT
            if (logo.heightMin == 0) logo.heightMin =  60; // n-tv                    16:9  720W  576H:->  224W  60H BOTTOM_RIGHT
                                                           // RTL_Television          16:9  720W  576H:->  104W  60H TOP_LEFT
                                                           // SAT_1_Gold               4:3  720W  576H:->  140W  60H TOP_RIGHT
                                                           // TLC                     16:9  720W  576H:->   94W  60H TOP_LEFT
                                                           // WELT                    16:9  720W  576H:->  222W  60H BOTTOM_LEFT
            if (logo.heightMax == 0) logo.heightMax =  88; // Nickelodeon             16:9  720W  576H:->  144W  88H TOP_LEFT
                                                           // NICK_MTV+               16:9  720W  576H:->  146W  88H TOP_LEFT
            break;
        case 1280:
            if (logo.widthMin  == 0) logo.widthMin  =  132; // BR_Fernsehen_Süd_HD     16:9 1280W  720H:->  132W  84H TOP_LEFT
            if (logo.widthMax  == 0) logo.widthMax  =  254; // ANIXE+                  16:9 1280W 1080H:->  254W 170H TOP_LEFT
            if (logo.heightMin == 0) logo.heightMin =   66; // SRF_zwei_HD             16:9 1280W  720H:->  172W  66H TOP_RIGHT
            if (logo.heightMax == 0) logo.heightMax =  134; // arte_HD                 16:9 1280W  720H:->   88W 134H TOP_LEFT
            break;
        case 1440:
            if (logo.widthMin  == 0) logo.widthMin  =  184; // WELT_HD                 16:9 1440W 1080H:->  396W 116H BOTTOM_LEFT
            if (logo.widthMax  == 0) logo.widthMax  =  250; // DMAX_HD                 16:9 1440W 1080H:->  250W 140H TOP_LEFT
            if (logo.heightMin == 0) logo.heightMin =  112; // WELT_HD                 16:9 1440W 1080H:->  396W 116H BOTTOM_LEFT
            if (logo.heightMax == 0) logo.heightMax =  140; // DMAX_HD                 16:9 1440W 1080H:->  250W 140H TOP_LEFT
            break;
        case 1920:
            if (logo.widthMin  == 0) logo.widthMin  =  204; // SAT_1_HD                16:9 1920W 1080H:->  204W 132H TOP_RIGHT
            if (logo.widthMax  == 0) logo.widthMax  =  394; // n-tv_HD                 16:9 1920W 1080H:->  394W 110H BOTTOM_RIGHT
                                                            // n-tv_HD                 16:9 1920W 1080H:->  394W 122H BOTTOM_RIGHT
            if (logo.heightMin == 0) logo.heightMin =   96; // münchen_tv_HD           16:9 1920W 1080H:->  336W  96H TOP_LEFT
            if (logo.heightMax == 0) logo.heightMax =  180; // ANIXE_HD                16:9 1920W 1080H:->  336W 180H TOP_LEFT
            break;
        case 3840:
            if (logo.widthMin  == 0) logo.widthMin  = 1412; // RTL_UHD                 16:9 3840W 2160H:-> 1412W 218H TOP_LEFT
            if (logo.widthMax  == 0) logo.widthMax  = 1412; // RTL_UHD                 16:9 3840W 2160H:-> 1412W 218H TOP_LEFT
            if (logo.heightMin == 0) logo.heightMin =  218; // RTL_UHD                 16:9 3840W 2160H:-> 1412W 218H TOP_LEFT
            if (logo.heightMax == 0) logo.heightMax =  218; // RTL_UHD                 16:9 3840W 2160H:-> 1412W 218H TOP_LEFT
            break;
        default:
            dsyslog("cExtractLogo::CheckLogoSize(): no logo size rules for %dx%d", maContext->Video.Info.width, maContext->Video.Info.height);
            return false;
            break;
    }
    if (logoWidth < logo.widthMin) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too narrow %d, expect min %d", logoWidth, logo.widthMin);
        return false;
    }
    if (logoWidth > logo.widthMax) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too wide %d, expect max %d", logoWidth, logo.widthMax);
        return false;
    }
    if (logoHeight < logo.heightMin) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too low %d, expect min %d", logoHeight, logo.heightMin);
        return false;
    }
    if (logoHeight > logo.heightMax) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo too height %d, expect max %d", logoHeight, logo.heightMax);
        return false;
    }
    if ((logo.corner >= 0) && (logo.corner != logoCorner)) {
        dsyslog("cExtractLogo::CheckLogoSize(): logo in corner %d, expect %d", logoCorner, logo.corner);
        return false;
    }
    return true;
}


bool cExtractLogo::Resize(const sMarkAdContext *maContext, sLogoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner) {
    if (!maContext) return false;
    if (!bestLogoInfo) return false;
    if (!logoHeight) return false;
    if (!logoWidth) return false;
    if ((bestLogoCorner < 0) || (bestLogoCorner >= CORNERS)) return false;
#ifdef DEBUG_LOGO_RESIZE
    Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "0_before_resize");
#endif
    dsyslog("cExtractLogo::Resize(): logo size before resize:    %3d width %3d height on corner %12s", *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
    int logoHeightBeforeResize = *logoHeight;
    int logoWidthBeforeResize = *logoWidth;
    int acceptFalsePixelH = *logoWidth / 30;  // reduced from 60 to 20, increased to 30 for vertical logo of arte HD
    int acceptFalsePixelV;
    if (maContext->Video.Info.width < 3840) acceptFalsePixelV = *logoHeight / 20; // reduced from 30 to 20
    else acceptFalsePixelV = *logoHeight / 30; // UDH has thin logo structure

    for (int repeat = 1; repeat <= 2; repeat++) {
        if ((*logoWidth <= 0) || (*logoHeight <= 0)) {
            dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", maContext->Video.Info.width, maContext->Video.Info.height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
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
            if ((maContext->Video.Info.width) == 720) minWhiteLines = 2;
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
            int quoteAfterCut = 100 * cutLine / *logoHeight;
            if ((topBlackLineOfLogo < cutLine) && (quoteAfterCut > 64)) {  // we may not cut off too much, this could not be text under logo, this is something on top of the logo
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
#ifdef DEBUG_LOGO_RESIZE
            if (repeat == 1) Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "1_after_cut_top_1");
            else Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "4_after_cut_top_2");
#endif
// search for text above logo
// search for at least 3 white lines to cut logos with text addon (e.g. "Neue Folge" or "Live")
            int countWhite = 0;
            int cutLine = 0;
            int topBlackLineOfLogo = 0;
            int leftBlackPixel = INT_MAX;
            int rightBlackPixel = 0;
            int minWhiteLines;
            if ((maContext->Video.Info.width) == 720) minWhiteLines = 2;
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
                if ((cutLine >= LOGO_MIN_LETTERING_H) && (cutLine < LOGO_MAX_LETTERING_H)) {
                    dsyslog("cExtractLogo::Resize(): found text above logo, cut at line %d, size %dWx%dH, pixel before: left %d right %d, width is valid", cutLine, rightBlackPixel - leftBlackPixel, cutLine, leftBlackPixel, rightBlackPixel);
                    CutOut(bestLogoInfo, cutLine, 0, logoHeight, logoWidth, bestLogoCorner);
                }
                else dsyslog("cExtractLogo::Resize(): cutline at %d not valid (expect >=%d and <%d)", cutLine, LOGO_MIN_LETTERING_H, LOGO_MAX_LETTERING_H);
            }
        }
#ifdef DEBUG_LOGO_RESIZE
        if (repeat == 1) Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "2_after_cut_text_above_1");
        else Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "5_after_cut_text_above_2");
#endif

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
#ifdef DEBUG_LOGO_RESIZE
            if (repeat == 1) Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "3_after_cut_right_1");
            else Save(maContext, bestLogoInfo, *logoHeight, *logoWidth, bestLogoCorner, bestLogoInfo->iFrameNumber, "6_after_cut_right_2");
#endif
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
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is valid", maContext->Video.Info.width, maContext->Video.Info.height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
        return true;
    }
    else {
        dsyslog("cExtractLogo::Resize(): video %dx%d with logo size %3d width %3d height on corner %s is not valid", maContext->Video.Info.width, maContext->Video.Info.height, *logoWidth, *logoHeight, aCorner[bestLogoCorner]);
        *logoHeight = logoHeightBeforeResize; // restore logo size
        *logoWidth = logoWidthBeforeResize;
        return false;
    }
}


// check if extracted picture from the corner could be a logo
// used before picture is stored in logo cantidates list
// return: true if valid
//
bool cExtractLogo::CheckValid(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
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
        if (strcmp(maContext->Info.ChannelName, "n-tv_HD") != 0) {  // this channel has a news ticket with info banner above, we will not find a small white top part
            for (int i = 0 ; i < WHITEHORIZONTAL_SMALL * logoWidth; i++) { // a valid bottom logo should have at least a small white top part in plane 0
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
#ifdef DEBUG_LOGO_CORNER
                    if (corner == DEBUG_LOGO_CORNER) tsyslog("cExtractLogo::CheckValid(): logo %s has no small white top part in plane 0 at frame %i", aCorner[corner], ptr_actLogoInfo->iFrameNumber);
#endif
                    return false;
                }
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


int cExtractLogo::Compare(const sMarkAdContext *maContext, sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!maContext) return 0;
    if (!ptr_actLogoInfo) return 0;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return 0;
    if ((corner < 0) || (corner >= CORNERS)) return 0;
    if (!ptr_actLogoInfo->valid) {
        dsyslog("cExtractLogo::Compare(): invalid logo data at frame %i", ptr_actLogoInfo->iFrameNumber);
        return 0;
    }
    int hits=0;

#ifdef DEBUG_MEM
    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
#endif

    if (maContext->Config->autoLogo == 1) { // use packed logos
        for (std::vector<sLogoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
            sLogoInfo actLogo = {};
            UnpackLogoInfo(&actLogo, &(*actLogoPacked));
            if (maContext->Video.Logo.isRotating) {
                if (CompareLogoPairRotating(maContext, &actLogo, ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
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
            // free memory of unpacked sobel plane
            for (int plane = 0; plane < PLANES; plane++) {
                delete actLogo.sobel[plane];
            }
            delete actLogo.sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");

        }
    }
    if (maContext->Config->autoLogo == 2){  // use unpacked logos
        for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            if (maContext->Video.Logo.isRotating) {
                if (CompareLogoPairRotating(maContext, &(*actLogo), ptr_actLogoInfo, logoHeight, logoWidth, corner)) {
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


/**
 * special detection for rotating logos
 */
bool cExtractLogo::CompareLogoPairRotating(const sMarkAdContext *maContext, sLogoInfo *logo1, sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner) {
    if (!logo1) return false;
    if (!logo2) return false;
    if ((corner < 0) || (corner >= CORNERS)) return false;
// TODO do not hardcode the logo range
    int logoStartLine   = 0;
    int logoEndLine     = 0;
    int logoStartColumn = 0;
    int logoEndColumn   = 0;
    if (strcmp(maContext->Info.ChannelName, "SAT_1") == 0) {
        logoStartLine   =  18;
        logoEndLine     =  75;
        logoStartColumn = 143;
        logoEndColumn   = 185;
    }
    else {
        if (strcmp(maContext->Info.ChannelName, "SAT_1_HD") == 0) { // TODO
            logoStartLine   =  60;
            logoEndLine     = 133;
            logoStartColumn = 196;
            logoEndColumn   = 318;
        }
        else {
            dsyslog("cExtractLogo::CompareLogoPairRotating(): channel unknown");
            return false;
        }
    }
// check if pixel in both frames are only in the corner but let the pixel be different
    if (corner != TOP_RIGHT) return false; // to optimze performance, only check TOP_RIGHT (SAT.1)
// we use only logo with pixel in the expected logo range
    for (int line = 0; line < logoHeight; line++) {
        for (int column = 0; column < logoWidth; column++) {
            if ((line >= logoStartLine) && (line < logoEndLine) && (column >= logoStartColumn) && (column < logoEndColumn)) continue;
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
        if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ++++ frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, match0, rate_1_2, match12, oneBlack_0, MIN_BLACK_PLANE_0);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_LOGO_CORNER
if (corner == DEBUG_LOGO_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ---- frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d), black %d (%d)", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, match0, rate_1_2, match12, oneBlack_0, MIN_BLACK_PLANE_0);
#endif
    return false;
}


int cExtractLogo::DeleteFrames(const sMarkAdContext *maContext, const int from, const int to) {
    if (!maContext) return 0;
    if (from >= to) return 0;
    int deleteCount = 0;
    dsyslog("cExtractLogo::DeleteFrames(): delete frames from %d to %d", from, to);
    for (int corner = 0; corner < CORNERS; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            for (std::vector<sLogoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                if (abortNow) return 0;
                if (actLogoPacked->iFrameNumber < from) continue;
                if (actLogoPacked->iFrameNumber <= to) {
                    // free memory of sobel planes
                    for (int plane = 0; plane < PLANES; plane++) {
                        delete actLogoPacked->sobel[plane];
                    }
                    delete actLogoPacked->sobel;
#ifdef DEBUG_MEM
                    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
#endif
                    FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel / 8, "actLogoInfoPacked.sobel");

                    // delete vector element
                    FREE(sizeof(*actLogoPacked), "logoInfoVectorPacked");
                    logoInfoVectorPacked[corner].erase(actLogoPacked--);  // "erase" increments the iterator, "for" also does, that is 1 to much
                    deleteCount++;
                }
                else break;
            }
        }
        if (maContext->Config->autoLogo == 2) {  // use unpacked logos
            for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                if (abortNow) return 0;
                if (actLogo->iFrameNumber < from) continue;
                if (actLogo->iFrameNumber <= to) {
                    // free memory of sobel planes
                    for (int plane = 0; plane < PLANES; plane++) {
                        delete actLogo->sobel[plane];
                    }
                    delete actLogo->sobel;
#ifdef DEBUG_MEM
                    int maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);
#endif
                    FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");

                    // delete vector element
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


int cExtractLogo::GetFirstFrame(const sMarkAdContext *maContext) {
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


int cExtractLogo::GetLastFrame(const sMarkAdContext *maContext) {
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


int cExtractLogo::CountFrames(const sMarkAdContext *maContext) {
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


bool cExtractLogo::WaitForFrames(sMarkAdContext *maContext, cDecoder *ptr_cDecoder, const int minFrame = 0) {
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
        maContext->Info.isRunningRecording = true;
        sleep(WAITTIME); // now we sleep and hopefully the index will grow
    }
    FREE(strlen(indexFile)+1, "indexFile");
    free(indexFile);
    return ret;
}


void cExtractLogo::PackLogoInfo(const sLogoInfo *logoInfo, sLogoInfoPacked *logoInfoPacked) {
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;

    int maxLogoPixel = GetMaxLogoPixel(maContextLogoSize->Video.Info.width);
    logoInfoPacked->iFrameNumber = logoInfo->iFrameNumber;
    logoInfoPacked->hits = logoInfo->hits;
    logoInfoPacked->sobel = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfoPacked->valid[plane] = logoInfo->valid[plane];
        logoInfoPacked->sobel[plane] = new uchar[maxLogoPixel / 8];  // alloc memory and copy planes
        for (int i = 0; i < maxLogoPixel / 8; i++) {
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
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel / 8, "actLogoInfoPacked.sobel");
}


void cExtractLogo::UnpackLogoInfo(sLogoInfo *logoInfo, const sLogoInfoPacked *logoInfoPacked) {
    if (!logoInfo) return;
    if (!logoInfoPacked) return;
    if (!logoInfoPacked->sobel) return; // nothing to unpack

    int maxLogoPixel = GetMaxLogoPixel(maContextLogoSize->Video.Info.width);
    logoInfo->iFrameNumber = logoInfoPacked->iFrameNumber;
    logoInfo->hits = logoInfoPacked->hits;
    logoInfo->sobel = new uchar*[PLANES];
    for (int plane = 0; plane < PLANES; plane++) {
        logoInfo->sobel[plane] = new uchar[maxLogoPixel];  // alloc memory and copy planes
        logoInfo->valid[plane] = logoInfoPacked->valid[plane];
        for (int i = 0; i < maxLogoPixel / 8; i++) {
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
    ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");
}


void cExtractLogo::RemovePixelDefects(const sMarkAdContext *maContext, sLogoInfo *logoInfo, const int logoHeight, const int logoWidth, const int corner) {
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


int cExtractLogo::AudioInBroadcast(const sMarkAdContext *maContext, const int iFrameNumber) {
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


int cExtractLogo::SearchLogo(sMarkAdContext *maContext, int startFrame) {  // return -1 internal error, 0 ok, > 0 no logo found, return last framenumber of search
    dsyslog("----------------------------------------------------------------------------");
    dsyslog("cExtractLogo::SearchLogo(): start extract logo from frame %i with aspect ratio %d:%d", startFrame, logoAspectRatio.num, logoAspectRatio.den);

    if (!maContext) {
        dsyslog("cExtractLogo::SearchLogo(): maContext not valid");
        return -1;
    }
    if (!recordingIndexLogo) return -1;
    if (startFrame < 0) return -1;

    struct timeval startTime, stopTime;
    int iFrameNumber = 0;
    int iFrameCountAll = 0;
    int logoHeight = 0;
    int logoWidth = 0;
    bool retStatus = true;
    bool readNextFile = true;
    int maxLogoPixel = 0;


    gettimeofday(&startTime, NULL);
    sMarkAdContext maContextSaveState = {};
    maContextSaveState.Video = maContext->Video;     // save state of calling video context
    maContextSaveState.Audio = maContext->Audio;     // save state of calling audio context

    cDecoder *ptr_cDecoder = new cDecoder(maContext->Config->threads, recordingIndexLogo);
    ALLOC(sizeof(*ptr_cDecoder), "ptr_cDecoder");

    cMarkAdLogo *ptr_Logo = new cMarkAdLogo(maContext, recordingIndexLogo);
    ALLOC(sizeof(*ptr_Logo), "SearchLogo-ptr_Logo");

    cMarkAdBlackBordersHoriz *hborder = new cMarkAdBlackBordersHoriz(maContext);
    ALLOC(sizeof(*hborder), "hborder");

    cMarkAdBlackBordersVert *vborder = new cMarkAdBlackBordersVert(maContext);
    ALLOC(sizeof(*vborder), "vborder");

    sAreaT *area = ptr_Logo->GetArea();

    if (!WaitForFrames(maContext, ptr_cDecoder)) {
        dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed");
        FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
        delete ptr_cDecoder;
        FREE(sizeof(*ptr_Logo), "SearchLogo-ptr_Logo");
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
        maContext->Info.vPidType = ptr_cDecoder->GetVideoType();
        if (maContext->Info.vPidType == 0) {
            dsyslog("cExtractLogo::SearchLogo(): video type not set");
            FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
            delete ptr_cDecoder;
            FREE(sizeof(*ptr_Logo), "SearchLogo-ptr_Logo");
            delete ptr_Logo;
            FREE(sizeof(*hborder), "hborder");
            delete hborder;
            FREE(sizeof(*vborder), "vborder");
            delete vborder;
            return -1;
        }
        maContext->Video.Info.height = ptr_cDecoder->GetVideoHeight();
        maContext->Video.Info.width = ptr_cDecoder->GetVideoWidth();
        dsyslog("cExtractLogo::SearchLogo(): video resolution %dx%d", maContext->Video.Info.width, maContext->Video.Info.height);
        SetLogoSize(maContext, &logoHeight, &logoWidth);
        dsyslog("cExtractLogo::SearchLogo(): logo size %dx%d", logoWidth, logoHeight);

        while(ptr_cDecoder->GetNextFrame()) {
            if (abortNow) return -1;

            // write an early start mark for running recordings
            if (maContext->Info.isRunningRecording && !maContext->Info.isStartMarkSaved && (iFrameNumber >= (maContext->Info.tStart * maContext->Video.Info.framesPerSecond))) {
                dsyslog("cExtractLogo::SearchLogo(): recording is aktive, read frame (%d), now save dummy start mark at pre timer position %ds", iFrameNumber, maContext->Info.tStart);
                clMarks marksTMP;
                marksTMP.Add(MT_ASSUMEDSTART, iFrameNumber, "timer start", true);
                marksTMP.Save(maContext->Config->recDir, maContext, true, true);
                maContext->Info.isStartMarkSaved = true;
            }
            if (!WaitForFrames(maContext, ptr_cDecoder)) {
                dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed at frame (%d), got %d valid frames of %d frames read", ptr_cDecoder->GetFrameNumber(), iFrameCountValid, iFrameCountAll);
                retStatus=false;
            }
            if ((ptr_cDecoder->GetFrameInfo(maContext, false) && retStatus)) {
                if (ptr_cDecoder->IsVideoPacket()) {
                    iFrameNumber = ptr_cDecoder->GetFrameNumber();
                    if (maxLogoPixel == 0) maxLogoPixel = GetMaxLogoPixel(maContext->Video.Info.width);

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

                    if ((logoAspectRatio.num == 0) || (logoAspectRatio.den == 0)) {
                        logoAspectRatio.num = maContext->Video.Info.AspectRatio.num;
                        logoAspectRatio.den = maContext->Video.Info.AspectRatio.den;
                        dsyslog("cExtractLogo::SearchLogo(): aspect ratio set to %d:%d", logoAspectRatio.num, logoAspectRatio.den);
                    }
                    if ((logoAspectRatio.num != maContext->Video.Info.AspectRatio.num) || (logoAspectRatio.den != maContext->Video.Info.AspectRatio.den)) {
                        continue;
                    }

                    int hBorderIFrame = -1;
                    int vBorderIFrame = -1;
                    int isHBorder = hborder->Process(iFrameNumber, &hBorderIFrame);
                    int isVBorder = vborder->Process(iFrameNumber, &vBorderIFrame);
                    if (isHBorder) {  // -1 invisible, 1 visible
                        if (hBorderIFrame >= 0) {  // we had a change
                            if (isHBorder == HBORDER_VISIBLE){
                                dsyslog("cExtractLogo::SearchLogo(): detect new horizontal border from frame (%d) to frame (%d)", hBorderIFrame, iFrameNumber);
                                iFrameCountValid-=DeleteFrames(maContext, hBorderIFrame, iFrameNumber);
                            }
                            else {
                                dsyslog("cExtractLogo::SearchLogo(): no horizontal border from frame (%d)", iFrameNumber);
                            }
                        }
                    }
                    if (isVBorder) { // -1 invisible, 1 visible
                        if (vBorderIFrame >= 0) {  // we had a change
                            if (isVBorder == VBORDER_VISIBLE) {
                                dsyslog("cExtractLogo::SearchLogo(): detect new vertical border from frame (%d) to frame (%d)", vBorderIFrame, iFrameNumber);
                                iFrameCountValid-=DeleteFrames(maContext, vBorderIFrame, iFrameNumber);
                            }
                            else {
                                dsyslog("cExtractLogo::SearchLogo(): no vertical border from frame (%d)", iFrameNumber);
                            }
                        }
                    }
                    if ((isHBorder == HBORDER_VISIBLE) || (isVBorder == VBORDER_VISIBLE)) {
                        dsyslog("cExtractLogo::SearchLogo(): border frame detected, abort logo search");
                        retStatus = false;
                    }

                    iFrameCountValid++;
                    if (!maContext->Video.Data.valid) {
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
                        ptr_Logo->Detect(0, iFrameNumber, &iFrameNumberNext);  // we do not take care if we detect the logo, we only fill the area
                        sLogoInfo actLogoInfo = {};
                        actLogoInfo.iFrameNumber = iFrameNumber;

                        // alloc memory and copy planes
                        actLogoInfo.sobel = new uchar*[PLANES];
                        for (int plane = 0; plane < PLANES; plane++) {
                            actLogoInfo.sobel[plane] = new uchar[maxLogoPixel];
                            memcpy(actLogoInfo.sobel[plane], area->sobel[plane], sizeof(uchar) * maxLogoPixel);
                        }
                        ALLOC(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");

                        if (CheckValid(maContext, &actLogoInfo, logoHeight, logoWidth, corner)) {
                            RemovePixelDefects(maContext, &actLogoInfo, logoHeight, logoWidth, corner);
                            actLogoInfo.hits = Compare(maContext, &actLogoInfo, logoHeight, logoWidth, corner);

                            if (maContext->Config->autoLogo == 1) { // use packed logos
                                sLogoInfoPacked actLogoInfoPacked = {};
                                PackLogoInfo(&actLogoInfo, &actLogoInfoPacked);
                                try { logoInfoVectorPacked[corner].push_back(actLogoInfoPacked); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus = false;
                                    break;
                                }
                                ALLOC((sizeof(sLogoInfoPacked)), "logoInfoVectorPacked");
                                // free memory of unpacked sobel plane
                                for (int plane = 0; plane < PLANES; plane++) {
                                    delete actLogoInfo.sobel[plane];
                                }
                                delete actLogoInfo.sobel;
                                FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");
                            }
                            if (maContext->Config->autoLogo == 2){  // use unpacked logos
                                try { logoInfoVector[corner].push_back(actLogoInfo); }  // this allocates a lot of memory
                                catch(std::bad_alloc &e) {
                                    dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %d", iFrameNumber);
                                    retStatus = false;
                                    break;
                                }
                                ALLOC((sizeof(sLogoInfo)), "logoInfoVector");
                            }
                        }
                        else {  // corner sobel transformed picture not valid
                            // free memory of sobel planes
                            for (int plane = 0; plane < PLANES; plane++) {
                                delete actLogoInfo.sobel[plane];
                            }
                            delete actLogoInfo.sobel;
                            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");
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

    if (!retStatus && (iFrameCountAll < MAXREADFRAMES) && ((iFrameCountAll > MAXREADFRAMES / 2) || (iFrameCountValid > 390))) {  // reached end of recording before we got 1000 valid frames, changed from 700 to 390
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
        sLogoInfoPacked actLogoInfoPacked[CORNERS] = {};
        sLogoInfo actLogoInfo[CORNERS] = {};
        for (int corner = 0; corner < CORNERS; corner++) {
            if (maContext->Config->autoLogo == 1) { // use packed logos
                actLogoInfoPacked[corner] = {};
                for (std::vector<sLogoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                    if (actLogoPacked->hits > actLogoInfoPacked[corner].hits) {
                        actLogoInfoPacked[corner] = *actLogoPacked;
                    }
                }
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %6d with %3d similars out of %3ld valid frames at %s", actLogoInfoPacked[corner].iFrameNumber, actLogoInfoPacked[corner].hits, logoInfoVectorPacked[corner].size(), aCorner[corner]);
            }
            if (maContext->Config->autoLogo == 2) { // use unpacked logos
                for (std::vector<sLogoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                    if (actLogo->hits > actLogoInfo[corner].hits) {
                        actLogoInfo[corner] = *actLogo;
                    }
                }
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %6d with %3d similars out of %3ld valid frames at %s", actLogoInfo[corner].iFrameNumber, actLogoInfo[corner].hits, logoInfoVector[corner].size(), aCorner[corner]);
            }
        }

        // find best and second best corner
        sLogoInfo bestLogoInfo = {};
        sLogoInfo secondBestLogoInfo = {};
        int bestLogoCorner = -1;
        int secondBestLogoCorner = -1;
        int sumHits = 0;


        if (maContext->Config->autoLogo == 1) { // use packed logos
            sLogoInfoPacked bestLogoInfoPacked = {};
            sLogoInfoPacked secondBestLogoInfoPacked = {};
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
            // unpack best logo
            UnpackLogoInfo(&bestLogoInfo, &bestLogoInfoPacked);
            // free memory of sobel planes, we not need them now
            for (int plane = 0; plane < PLANES; plane++) {
                delete bestLogoInfo.sobel[plane];
            }
            delete bestLogoInfo.sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");

            // unpack second best logo
            UnpackLogoInfo(&secondBestLogoInfo, &secondBestLogoInfoPacked);
            // free memory of sobel planes, we not need them now
            for (int plane = 0; plane < PLANES; plane++) {
                delete secondBestLogoInfo.sobel[plane];
            }
            delete secondBestLogoInfo.sobel;
            FREE(sizeof(uchar*) * PLANES * sizeof(uchar) * maxLogoPixel, "actLogoInfo.sobel");

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

        if ((bestLogoCorner >= 0) &&
           (((startFrame == 0) && (bestLogoInfo.hits >= 14)) ||  // this is the very last try, use what we have, bettet than nothing
             (bestLogoInfo.hits >= 50) || // we have a good result
            ((bestLogoInfo.hits >= 40) && (sumHits <= bestLogoInfo.hits + 10)) ||  // if most hits are in the same corner than less are enough
            ((bestLogoInfo.hits >= 30) && (sumHits <= bestLogoInfo.hits + 3)) ||  // if almost all hits are in the same corner than less are enough
            ((bestLogoInfo.hits >= 10) && (sumHits == bestLogoInfo.hits)))) {  // if all hits are in the same corner than less are enough
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
            if (!Save(maContext, &bestLogoInfo, logoHeight, logoWidth, bestLogoCorner)) {
                dsyslog("cExtractLogo::SearchLogo(): logo save failed");
                retStatus = false;
            }
        }
    }
    FREE(sizeof(*ptr_Logo), "SearchLogo-ptr_Logo");  // new cMarkAdLogo(maContext, recordingIndexLogo);
    delete ptr_Logo;

    // restore maContext
    maContext->Video = maContextSaveState.Video;     // restore state of calling video context
    maContext->Audio = maContextSaveState.Audio;     // restore state of calling audio context

    // delete all used classes
    FREE(sizeof(*ptr_cDecoder), "ptr_cDecoder");
    delete ptr_cDecoder;
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
