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
    return true;
}


bool cExtractLogo::Resize(logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner) {
    if (!bestLogoInfo) return false;
    if (!logoHeight) return false;
    if (!logoWidth) return false;
    if ((bestLogoCorner < 0) || (bestLogoCorner > 3)) return false;

    int acceptFalsePixelH = *logoWidth / 60;
    int acceptFalsePixelV = *logoHeight / 30;

// resize plane 0
    dsyslog("cExtractLogo::Resize(): logo size before resize: %d height %d width on corner %d", *logoHeight, *logoWidth, bestLogoCorner);
    bool allWhite = true;
    int whiteLines = 0;
    int whiteColumns = 0;
    int logoHeightBeforeResize = *logoHeight;
    int logoWidthBeforeResize = *logoWidth;
    int heightPlane_1_2 = *logoHeight/2;
    int widthPlane_1_2 = *logoWidth/2;


    if (bestLogoCorner <= TOP_RIGHT) {  // top corners, calculate new height and cut from below
        while (allWhite) {
            int isLineWhite = 0;
            for (int i = (*logoHeight -1) * *logoWidth; i < *logoHeight * *logoWidth; i++) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isLineWhite++;
                }
            }
            if (isLineWhite <= acceptFalsePixelH ) {  // accept false pixel
                (*logoHeight)--;
            }
            else allWhite=false;
        }
        if (*logoHeight % 2) (*logoHeight)++;
    }
    else { // bottom corners, calculate new height and cut from above
        while (allWhite) {
            int isLineWhite = 0;
            for (int i = whiteLines; i < (whiteLines+1) * *logoWidth; i++) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isLineWhite++;
                }
            }
            if (isLineWhite <= acceptFalsePixelH ) {  // accept false pixel
                whiteLines++;
                if (whiteLines >= *logoHeight) allWhite=false;
            }
            else allWhite=false;
        }
        if (whiteLines % 2) whiteLines--;
        for (int i = 0; i < (*logoHeight-whiteLines) * *logoWidth; i++) {
             bestLogoInfo->sobel[0][i] =  bestLogoInfo->sobel[0][i + whiteLines* *logoWidth];
        }
        *logoHeight -= whiteLines;
    }

    allWhite=true;
    if ((bestLogoCorner == TOP_RIGHT) || (bestLogoCorner == BOTTOM_RIGHT)) {  // right corners, cut from left
        while ((allWhite) && (whiteColumns < *logoWidth)) {
            int isColumnWhite = 0;
            for (int i = whiteColumns; i < *logoHeight * *logoWidth; i = i + *logoWidth) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isColumnWhite++;
                }
            }
            if (isColumnWhite <= acceptFalsePixelV ) {  // accept false pixel
                whiteColumns++;
            }
            else allWhite=false;
        }
        if (whiteColumns % 2) whiteColumns--;
        for (int i = 0; i < *logoHeight * (*logoWidth - whiteColumns); i++) {
             bestLogoInfo->sobel[0][i] =  bestLogoInfo->sobel[0][i + whiteColumns*(1 + (i / (*logoWidth - whiteColumns)))];
        }
        *logoWidth -= whiteColumns;
    }
    else { // left corners, cut from right
        whiteColumns = *logoWidth;
        while ((allWhite) && (whiteColumns > 0)) {
            int isColumnWhite = 0;
            for (int i = whiteColumns; i < *logoHeight * *logoWidth; i = i + *logoWidth) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isColumnWhite++;
                }
            }
            if (isColumnWhite <= acceptFalsePixelV ) {  // accept false pixel
                whiteColumns--;
            }
            else allWhite=false;
        }
        if (whiteColumns % 2) whiteColumns--;
        whiteColumns = *logoWidth - whiteColumns;
        for (int i = 0; i < *logoHeight * (*logoWidth - whiteColumns); i++) {
             bestLogoInfo->sobel[0][i] =  bestLogoInfo->sobel[0][i + whiteColumns*(i / (*logoWidth - whiteColumns))];
        }
        *logoWidth -= whiteColumns;

    }

// resize plane 1 and 2
    whiteLines /= 2;
    whiteColumns /= 2;
    for (int plane = 1; plane < PLANES; plane++) {
        if (bestLogoCorner <= TOP_RIGHT) { // top corners, cut from below
        }
        else { // bottom corners, cut from above
            for (int i = 0; i < (heightPlane_1_2 - whiteLines) * widthPlane_1_2; i++) {
                bestLogoInfo->sobel[plane][i] =  bestLogoInfo->sobel[plane][i + whiteLines* widthPlane_1_2];
            }
        }
        if ((bestLogoCorner == TOP_RIGHT) || (bestLogoCorner == BOTTOM_RIGHT)) {  // right corners, cut from left
            for (int i = 0; i < heightPlane_1_2 * (widthPlane_1_2 - whiteColumns); i++) {
                bestLogoInfo->sobel[plane][i] =  bestLogoInfo->sobel[plane][i + whiteColumns*(1 + (i / (widthPlane_1_2 - whiteColumns)))];
            }
        }
        else { // left corners, cut from right
            for (int i = 0; i < heightPlane_1_2 * (widthPlane_1_2 - whiteColumns); i++) {
                bestLogoInfo->sobel[plane][i] =  bestLogoInfo->sobel[plane][i + whiteColumns*(i / (widthPlane_1_2 - whiteColumns))];
            }
        }
    }
    dsyslog("cExtractLogo::Resize(): logo size after resize: height %d and width %d on corner %d", *logoHeight, *logoWidth, bestLogoCorner);
    if ((*logoWidth > logoWidthBeforeResize * 0.9) && (bestLogoCorner != BOTTOM_LEFT)) {  // if logo is too wide, it maybe is a lettering, but not bottom left, this could be a news ticker after the logo
        dsyslog("cExtractLogo::Resize(): logo size not valid after resize");
        *logoHeight = logoHeightBeforeResize; // restore logo size
        *logoWidth = logoWidthBeforeResize;
        return false;
    }
    return true;
}


int cExtractLogo::Compare(const MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner) {
    if (!maContext) return 0;
    if (!ptr_actLogoInfo) return 0;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return 0;
    if ((corner < 0) || (corner > 3)) return 0;
    if (!ptr_actLogoInfo->valid) {
        dsyslog("cExtractLogo::Compare(): invalid logo data at frame %i", ptr_actLogoInfo->iFrameNumber);
        return 0;
    }
    int hits=0;
    if (corner <= TOP_RIGHT) {
        for (int i = 0 ; i < 10 * logoWidth; i++) { // a valid top logo should have a white top part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }
        for (int i = (logoHeight-8) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid top logo should have at least a small white buttom part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }

    }
    else {
        for (int i = (logoHeight-10) * logoWidth; i < logoHeight*logoWidth; i++) { // a valid bottom logo should have a white bottom part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }
        for (int i = 0 ; i < 8 * logoWidth; i++) { // a valid bottom logo should have at least a small white top part in plane 0
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }

    }
#define WHITEVERTICAL 10
    if ((corner == TOP_LEFT) || (corner == BOTTOM_LEFT)) { // a valid left logo should have white left part in pane 0
        for (int column = 0; column <= WHITEVERTICAL; column++) {
            for (int i = column; i < logoHeight * logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                    return 0;
                }
            }
        }
    }
    else { // a valid right logo should have white right part in pane 0
        for (int column = 0; column <= WHITEVERTICAL; column++) {
            for (int i = logoWidth - column; i < logoHeight * logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                    return 0;
                }
            }
        }
    }

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
    int black_0 = 0;
    int rate_0=0;
    int rate_1_2=0;
    for (int i = 0; i < logoHeight*logoWidth; i++) {    // compare all black pixel in plane 0
        if ((logo1->sobel[0][i] == 255) && (logo2->sobel[0][i] == 255)) continue;   // ignore white pixel
        else black_0 ++;
        if (logo1->sobel[0][i] == logo2->sobel[0][i]) {
            similar_0++;
        }
    }
    for (int i = 0; i < logoHeight/2*logoWidth/2; i++) {    // compare all pixel in plane 1 and 2
        for (int plane = 1; plane < PLANES; plane ++) {
            if (logo1->sobel[plane][i] == logo2->sobel[plane][i]) similar_1_2++;
        }
    }
    if (black_0 > 100) rate_0=1000*similar_0/black_0;   // accept only if we found some pixels
    else rate_0=0;
    rate_1_2 = 1000*similar_1_2/(logoHeight*logoWidth)*2;

#define MINMATCH_0 860  // reduced from 890 to 870 to 860
#define MINMATCH_1_2 985
// #define DEBUG_CORNER TOP_LEFT
    if ((rate_0 > MINMATCH_0) && (rate_1_2 > MINMATCH_1_2)) { // reduced from 890 to 870
#ifdef DEBUG_CORNER
        if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo ======== frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d)", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);  // only for debug
#endif
        return true;
    }
#ifdef DEBUG_CORNER
if (corner == DEBUG_CORNER) dsyslog("cExtractLogo::CompareLogoPair(): logo !=!=!=!= frame (%5d) and (%5d), rate_0: %4d (%d), rate_1_2: %4d (%d) ", logo1->iFrameNumber, logo2->iFrameNumber, rate_0, MINMATCH_0, rate_1_2, MINMATCH_1_2);
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
        if (maContext->Config->autoLogo == 2){  // use unpacked logos
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


int cExtractLogo::SearchLogo(MarkAdContext *maContext, const int startFrame) {  // return -1 internal error, 0 ok, > 0 no logo found, return last framenumber of search
    dsyslog("----------------------------------------------------------------------------");
    dsyslog("cExtractLogo::SearchLogo(): start extract logo from frame %i with aspect ratio %d:%d", startFrame, logoAspectRatio.Num, logoAspectRatio.Den);

    if (!maContext) {
        dsyslog("cExtractLogo::SearchLogo(): maContext not valid");
        return -1;
    }
    if (startFrame < 0) return -1;

    int iFrameNumber = 0;
    int iFrameCountValid = 0;
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
// remove frames before start frame
    DeleteFrames(maContext, 0, startFrame);
    while(ptr_cDecoder->DecodeDir(maContext->Config->recDir)) {
// skip frames we have already read
        if ((maContext->Config->autoLogo == 1) && (! logoInfoVectorPacked[0].empty())) { // use packed logos
            logoInfoPacked lastInfo = logoInfoVectorPacked[0].back();
            if (ptr_cDecoder->GetFrameNumber() < lastInfo.iFrameNumber) {
                ptr_cDecoder->SeekToFrame(lastInfo.iFrameNumber);
                iFrameCountValid = logoInfoVectorPacked[0].size();
                if (iFrameCountValid > 1000) {
                    dsyslog("cExtractLogo::SearchLogo(): we will get no new frames, give up");
                }
                iFrameCountAll = iFrameCountValid;
                dsyslog("cExtractLogo::SearchLogo(): already have %d frames from (%d)to frame (%d)", iFrameCountValid, logoInfoVectorPacked[0].front().iFrameNumber, lastInfo.iFrameNumber);
            }
        }
        if ((maContext->Config->autoLogo == 2) && (! logoInfoVector[0].empty())) { // use unpacked logos
            logoInfo lastInfo = logoInfoVector[0].back();
            if (ptr_cDecoder->GetFrameNumber() < lastInfo.iFrameNumber) {
                ptr_cDecoder->SeekToFrame(lastInfo.iFrameNumber);
                iFrameCountValid = logoInfoVector[0].size();
                if (iFrameCountValid > 1000) {
                    dsyslog("cExtractLogo::SearchLogo(): we will get no new frames, give up");
                }
                iFrameCountAll = iFrameCountValid;
                dsyslog("cExtractLogo::SearchLogo(): already have %d frames from (%d) to (%d)", iFrameCountValid, logoInfoVector[0].front().iFrameNumber, lastInfo.iFrameNumber);
            }
        }

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
                    if ((logoAspectRatio.Num == 0) || (logoAspectRatio.Den == 0)) {
                        logoAspectRatio.Num = maContext->Video.Info.AspectRatio.Num;
                        logoAspectRatio.Den = maContext->Video.Info.AspectRatio.Den;
                        dsyslog("cExtractLogo::SearchLogo(): aspect ratio set to %d:%d", logoAspectRatio.Num, logoAspectRatio.Den);
                    }
                    if ((logoAspectRatio.Num != maContext->Video.Info.AspectRatio.Num) || (logoAspectRatio.Den != maContext->Video.Info.AspectRatio.Den)) {
                        continue;
                    }
                    if ((iFrameNumber >= firstBorderStart) && (iFrameNumber < lastBorderEnd)) {
                        dsyslog("cExtractLogo::SearchLogo(): frame (%d) is in border area from (%d) to (%d) seek to end of area", iFrameNumber, firstBorderStart, lastBorderEnd);
                        ptr_cDecoder->SeekToFrame(lastBorderEnd);
                        continue;
                    }
                    int hBorderIFrame = 0;
                    int vBorderIFrame = 0;
                    int isHBorder = hborder->Process(iFrameNumber, &hBorderIFrame);
                    int isVBorder = vborder->Process(iFrameNumber, &vBorderIFrame);
                    if (isHBorder) {  // -1 invisible, 1 visible
                        if (hborder->Status() == HBORDER_VISIBLE) {
                            if (firstBorderStart == -1) firstBorderStart = hBorderIFrame;
                            dsyslog("cExtractLogo::SearchLogo(): detect new horizontal border from frame (%d) to frame (%d), first border start at frame (%d)", hBorderIFrame, iFrameNumber, firstBorderStart);
                            iFrameCountValid-=DeleteFrames(maContext, hBorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no horizontal border from frame (%d)", iFrameNumber);
                        }
                    }
                    if (isVBorder) { // -1 invisible, 1 visible
                        if (vborder->Status() == VBORDER_VISIBLE) {
                            if (firstBorderStart == -1) firstBorderStart = vBorderIFrame;
                            dsyslog("cExtractLogo::SearchLogo(): detect new vertical border from frame (%d) to frame (%d), first border start at frame (%d)", vBorderIFrame, iFrameNumber, firstBorderStart);
                            iFrameCountValid-=DeleteFrames(maContext, vBorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no vertical border from frame (%d)", iFrameNumber);
                        }
                    }
                    if ((vborder->Status() == VBORDER_VISIBLE) || (hborder->Status() == HBORDER_VISIBLE)) {
                        lastBorderEnd = iFrameNumber;
                        continue;
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
                        actLogoInfo.hits = this->Compare(maContext, &actLogoInfo, logoHeight, logoWidth, corner);

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
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %i with %i similars at corner %i", actLogoInfoPacked[corner].iFrameNumber, actLogoInfoPacked[corner].hits, corner);
            }
            if (maContext->Config->autoLogo == 2) { // use unpacked logos
                for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
                    if (actLogo->hits > actLogoInfo[corner].hits) {
                        actLogoInfo[corner] = *actLogo;
                    }
                }
                dsyslog("cExtractLogo::SearchLogo(): best guess found at frame %i with %i similars at corner %i", actLogoInfo[corner].iFrameNumber, actLogoInfo[corner].hits, corner);
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

        if ((bestLogoInfo.hits >= 50) || ((bestLogoInfo.hits >= 25) && (sumHits <= bestLogoInfo.hits + 3))) {  // if almost all hits are in the same corner than 25 are enough
            int secondLogoHeight = logoHeight;
            int secondLogoWidth = logoWidth;
            dsyslog("cExtractLogo::SearchLogo(): best corner %d found at frame %d with %d similars", bestLogoCorner, bestLogoInfo.iFrameNumber, bestLogoInfo.hits);
            if (this->Resize(&bestLogoInfo, &logoHeight, &logoWidth, bestLogoCorner)) {
                if ((secondBestLogoInfo.hits > 50) || (secondBestLogoInfo.hits > (bestLogoInfo.hits * 0.7))) { // decreased from 0.9 to 0.8 to 0.7
                    dsyslog("cExtractLogo::SearchLogo(): no clear corner detected, second best corner has %d hits", secondBestLogoInfo.hits);
                    if (secondBestLogoInfo.hits >= 50) {
                        dsyslog("cExtractLogo::SearchLogo(): try with second best corner %d at frame %d with %d similars", secondBestLogoCorner, secondBestLogoInfo.iFrameNumber, secondBestLogoInfo.hits);
                        if (this->Resize(&secondBestLogoInfo, &secondLogoHeight, &secondLogoWidth, secondBestLogoCorner)) {
                            if (secondLogoWidth < logoWidth) {  // smaller is the logo, the wider is a lettering
                                dsyslog("cExtractLogo::SearchLogo(): second best corner is narrower, use this");
                                bestLogoInfo = secondBestLogoInfo;
                                bestLogoCorner = secondBestLogoCorner;
                                logoHeight = secondLogoHeight;
                                logoWidth = secondLogoWidth;
                            }
                        }
                        else dsyslog("cExtractLogo::SearchLogo(): resize logo failed from second best corner failed");
                    }
                    else retStatus=false;
                }
            }
            else {
                dsyslog("cExtractLogo::SearchLogo(): resize logo failed from best corner failed");
                if (secondBestLogoInfo.hits >= 50) {
                    dsyslog("cExtractLogo::SearchLogo(): try with second best corner %d at frame %d with %d similars", secondBestLogoCorner, secondBestLogoInfo.iFrameNumber, secondBestLogoInfo.hits);
                    if (this->Resize(&secondBestLogoInfo, &logoHeight, &logoWidth, secondBestLogoCorner)) {
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
