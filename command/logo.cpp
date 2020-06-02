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

cExtractLogo::cExtractLogo() {
}


cExtractLogo::~cExtractLogo() {
}


bool cExtractLogo::isWhitePlane(logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int plane) {
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


bool cExtractLogo::Save(MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int corner) {
    if (!maContext) return false;
    if (!ptr_actLogoInfo) return false;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return false;
    if ((corner < 0) || (corner > 3)) return false;
    if (!maContext->Info.ChannelName) return false;

    for (int plane=0; plane < PLANES; plane++) {
        char *buf=NULL;
        int height = logoHeight;
        int width = logoWidth;
        if (plane>0) {
            width/=2;
            height/=2;
        }
        int black = 0;
        for (int i = 0; i < height*width; i++) {
            if (ptr_actLogoInfo->sobel[plane][i] == 0) black++;
        }
        if (plane > 0) {
            if (black < 80) {
                dsyslog("cExtractLogo::Save(): not enough pixel (%i) in plane %i", black, plane);
                continue;
            }
            else dsyslog("cExtractLogo::Save(): got enough pixel (%i) in plane %i", black, plane);
        }
        else dsyslog("cExtractLogo::Save(): %i pixel in plane %i", black, plane);

        if (this->isWhitePlane(ptr_actLogoInfo, height, width, plane)) continue;
        if (asprintf(&buf,"%s/%s-A%i_%i-P%i.pgm",maContext->Config->recDir, maContext->Info.ChannelName, ptr_actLogoInfo->aspectratio.Num,ptr_actLogoInfo->aspectratio.Den,plane)==-1) return false;
        dsyslog("cExtractLogo::Save(): store logo in %s", buf);
        // Open file
        FILE *pFile=fopen(buf, "wb");
        if (pFile==NULL)
        {
            free(buf);
            dsyslog("cExtractLogo::Save(): open file failed");
            return false;
        }
       // Write header
        fprintf(pFile, "P5\n#C%i\n%d %d\n255\n", corner, width, height);

        // Write pixel data
        if (!fwrite(ptr_actLogoInfo->sobel[plane],1,width*height,pFile)) {
            dsyslog("cExtractLogo::Save(): write data failed");
            fclose(pFile);
            free(buf);
            return false;
        }
        // Close file
        fclose(pFile);
        free(buf);
    }
    return true;
}


bool cExtractLogo::Resize(logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, int bestLogoCorner) {
    if (!bestLogoInfo) return false;
    if (!logoHeight) return false;
    if (!logoWidth) return false;
    if ((bestLogoCorner < 0) || (bestLogoCorner > 3)) return false;

// resize plane 0
    dsyslog("cExtractLogo::Resize(): logo size before resize: %d height %d width on corner %d", *logoHeight, *logoWidth, bestLogoCorner);
    bool allWhite = true;
    int whiteLines = 0;
    int whiteColumns = 0;
    int heightPlane_1_2 = *logoHeight/2;
    int widthPlane_1_2 = *logoWidth/2;

    if (bestLogoCorner <= 1) {  // top corners, calculate new height and cut from below
        while (allWhite) {
            int isLineWhite = 0;
            for (int i = (*logoHeight -1) * *logoWidth; i < *logoHeight * *logoWidth; i++) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isLineWhite++;
                }
            }
            if (isLineWhite < 2 ) {  // accept 1 false pixel
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
            if (isLineWhite < 2 ) {  // accept 1 false pixel
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
    if ((bestLogoCorner == 1) || (bestLogoCorner == 3)) {  // right corners, cut from left
        while (allWhite) {
            int isColumnWhite = 0;
            for (int i = whiteColumns; i < *logoHeight * *logoWidth; i = i + *logoWidth) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isColumnWhite++;
                }
            }
            if (isColumnWhite < 2 ) {  // accept 1 false pixel
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
        while (allWhite) {
            int isColumnWhite = 0;
            for (int i = whiteColumns; i < *logoHeight * *logoWidth; i = i + *logoWidth) {
                if ( bestLogoInfo->sobel[0][i] == 0) {
                    isColumnWhite++;
                }
            }
            if (isColumnWhite < 2 ) {  // accept 1 false pixel
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

// resize plane 1 - 2
    whiteLines/= 2;
    whiteColumns/=2;
    for (int plane = 1; plane <= 2; plane++) {
        if (bestLogoCorner <= 1) { // top corners, cut from below
            heightPlane_1_2 = *logoHeight/2;
        }
        else { // bottom corners, cut from above
            for (int i = 0; i < (heightPlane_1_2-whiteLines) * widthPlane_1_2; i++) {
                bestLogoInfo->sobel[plane][i] =  bestLogoInfo->sobel[plane][i + whiteLines* widthPlane_1_2];
            }
            heightPlane_1_2 = *logoHeight/2;
        }
        if ((bestLogoCorner == 1) || (bestLogoCorner == 3)) {  // right corners, cut from left
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
    dsyslog("cExtractLogo::Resize(): logo size after resize: %d height %d width on corner %d", *logoHeight, *logoWidth, bestLogoCorner);
    return true;
}


int cExtractLogo::Compare(MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int corner) {
    if (!maContext) return 0;
    if (!ptr_actLogoInfo) return 0;
    if ((logoHeight <= 0) || (logoWidth <= 0)) return 0;
    if ((corner < 0) || (corner > 3)) return 0;
    if (!ptr_actLogoInfo->valid) {
        dsyslog("cExtractLogo::Compare(): invalid logo data at frame %i", ptr_actLogoInfo->iFrameNumber);
        return 0;
    }
    int hits=0;
    if (corner <= 1) { // a valid top logo should have a white top part in plane 0
        for (int i = 0 ; i < 10 * logoWidth; i++) {
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }
    }
    else { // a valid bottom logo should have a white bottom  part in plane 0
        for (int i = (logoHeight-10) * logoWidth; i < logoHeight*logoWidth; i++) {
            if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                return 0;
            }
        }
    }
    if ((corner == 0) || (corner == 2)) { // a valid left logo should have white left part in pane 0
        for (int column = 0; column < 10; column++) {
            for (int i = column; i < logoHeight*logoWidth; i = i + logoWidth) {
                if (ptr_actLogoInfo->sobel[0][i] == 0 ) {
                    return 0;
                }
            }
        }
    }
    else { // a valid right logo should have white right part in pane 0
        for (int column = 0; column < 10; column++) {
            for (int i = logoWidth-column; i < logoHeight*logoWidth; i = i + logoWidth) {
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
            if (CompareLogoPair(&actLogo, ptr_actLogoInfo, logoHeight, logoWidth)) {
                hits++;
                actLogoPacked->hits++;
            }
        }
    }
    if (maContext->Config->autoLogo == 2){  // use unpacked logos
        for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
            if (CompareLogoPair(&(*actLogo), ptr_actLogoInfo, logoHeight, logoWidth)) {
                hits++;
                actLogo->hits++;
            }
        }
    }
    return hits;
}


bool cExtractLogo::CompareLogoPair(logoInfo *logo1, logoInfo *logo2, int logoHeight, int logoWidth) {
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
    if ((rate_0 > 900) && (rate_1_2 > 990)) return true;
    return false;
}


int cExtractLogo::DeleteBorderFrames(MarkAdContext *maContext, int from, int to) {
    if (!maContext) return false;
    if (from >= to) return 0;
    int deleteCount=0;
    dsyslog("cExtractLogo::DeleteBorderFrames(): delete border frames from %d to %d", from, to);
    for (int corner = 0; corner <= 3; corner++) {
        if (maContext->Config->autoLogo == 1) { // use packed logos
            for (std::vector<logoInfoPacked>::iterator actLogoPacked = logoInfoVectorPacked[corner].begin(); actLogoPacked != logoInfoVectorPacked[corner].end(); ++actLogoPacked) {
                if (abortNow) return deleteCount/4;
                if (( actLogoPacked->iFrameNumber >= from) && ( actLogoPacked->iFrameNumber <= to)) {
                    logoInfoVectorPacked[corner].erase(actLogoPacked);
                    deleteCount++;
                    actLogoPacked--;  // "erase" increments the iterator, "for" also does, that is 1 to much
                }
            }
       }
       if (maContext->Config->autoLogo == 2){  // use unpacked logos
           for (std::vector<logoInfo>::iterator actLogo = logoInfoVector[corner].begin(); actLogo != logoInfoVector[corner].end(); ++actLogo) {
               if (( actLogo->iFrameNumber >= from) && ( actLogo->iFrameNumber <= to)) {
                   logoInfoVector[corner].erase(actLogo);
                   deleteCount++;
                   actLogo--;  // "erase" increments the iterator, "for" also does, that is 1 to much
               }
           }
        }
   }
   return deleteCount/4;  // 4 corner
}


bool cExtractLogo::WaitForFrames(MarkAdContext *maContext, cDecoder *ptr_cDecoder) {
    if (!maContext) return false;
    if (!ptr_cDecoder) return false;

#define WAITTIME 60
    char *indexFile = NULL;
    if (recordingFrameCount > (ptr_cDecoder->GetFrameNumber()+200)) return true; // we have already found enougt frames

    if (asprintf(&indexFile,"%s/index",maContext->Config->recDir)==-1) indexFile=NULL;
    if (!indexFile) {
        dsyslog("cExtractLogo::isRunningRecording(): no index file info");
        return false;
    }
    struct stat indexStatus;
    if (stat(indexFile,&indexStatus)==-1) {
        dsyslog("cExtractLogo::isRunningRecording(): failed to stat %s",indexFile);
        return false;
    }
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
    dsyslog("cExtractLogo::WaitForFrames(): need more frames at frame (%ld), frames recorded (%i)", ptr_cDecoder->GetFrameNumber(), maxframes);
    if ((difftime(now,indexStatus.st_mtime))>= 2*WAITTIME) {
        dsyslog("cExtractLogo::isRunningRecording(): index not growing at frame (%ld), old or interrupted recording", ptr_cDecoder->GetFrameNumber());
        return false;
    }
    dsyslog("cExtractLogo::WaitForFrames(): waiting for new frames at frame (%ld), frames recorded (%i)", ptr_cDecoder->GetFrameNumber(), maxframes);
    sleep(WAITTIME); // now we sleep and hopefully the index will grow
    return true;
}


void cExtractLogo::PackLogoInfo(logoInfo *logoInfo, logoInfoPacked *logoInfoPacked) {
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;
    logoInfoPacked->iFrameNumber=logoInfo->iFrameNumber;
    logoInfoPacked->hits=logoInfo->hits;
    logoInfoPacked->aspectratio=logoInfo->aspectratio;
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


void cExtractLogo::UnpackLogoInfo(logoInfo *logoInfo, logoInfoPacked *logoInfoPacked) {
    if ( !logoInfo ) return;
    if ( !logoInfoPacked) return;
    logoInfo->iFrameNumber=logoInfoPacked->iFrameNumber;
    logoInfo->hits=logoInfoPacked->hits;
    logoInfo->aspectratio=logoInfoPacked->aspectratio;
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


bool cExtractLogo::SearchLogo(MarkAdContext *maContext, int startFrame) {
    dsyslog("----------------------------------------------------------------------------");
    dsyslog("cExtractLogo::SearchLogo(): start extract logo from frame %i", startFrame);

    if (!maContext) {
        dsyslog("cExtractLogo::SearchLogo(): maContext not valid");
        return false;
    }
    if (startFrame < 0) return false;

    cMarkAdLogo *logo = NULL;
    long int iFrameNumber = 0;
    int iFrameCountValid = 0;
    int iFrameCountAll = 0;
    int logoHeight = 0;
    int logoWidth = 0;
    bool retStatus=true;

    MarkAdContext maContextSaveState = {};
    maContextSaveState.Video = maContext->Video;     // save state of calling video context
    maContextSaveState.Audio = maContext->Audio;     // save state of calling audio context

    cDecoder *ptr_cDecoder = new cDecoder(maContext->Config->threads);
    logo = new cMarkAdLogo(maContext);
    cMarkAdBlackBordersHoriz *hborder=new cMarkAdBlackBordersHoriz(maContext);
    cMarkAdBlackBordersVert *vborder=new cMarkAdBlackBordersVert(maContext);
    areaT *area = logo->GetArea();

    if (!WaitForFrames(maContext, ptr_cDecoder)) {
        dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed");
        return false;
    }
    while(ptr_cDecoder->DecodeDir(maContext->Config->recDir)) {
        maContext->Video.Info.Height=ptr_cDecoder->GetVideoHeight();
        dsyslog("cExtractLogo::SearchLogo(): video height: %i", maContext->Video.Info.Height);
        maContext->Video.Info.Width=ptr_cDecoder->GetVideoWidth();
        dsyslog("cExtractLogo::SearchLogo(): video width: %i", maContext->Video.Info.Width);

        if (maContext->Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264) {
        logoHeight=LOGO_DEFHDHEIGHT;
        logoWidth=LOGO_DEFHDWIDTH;
        }
        else if (maContext->Info.VPid.Type==MARKAD_PIDTYPE_VIDEO_H262) {
        logoHeight=LOGO_DEFHEIGHT;
        logoWidth=LOGO_DEFWIDTH;
        }
        else dsyslog("cExtractLogo::SearchLogo(): maContext->Info.VPid.Type %i not valid", maContext->Info.VPid.Type);

        bool firstFrame = true;
        MarkAdAspectRatio aspectRatio = {};
        while(ptr_cDecoder->GetNextFrame()) {
            if (abortNow) return false;
            if (!WaitForFrames(maContext, ptr_cDecoder)) {
                dsyslog("cExtractLogo::SearchLogo(): WaitForFrames() failed");
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
                    if (firstFrame) {
                        aspectRatio.Num = maContext->Video.Info.AspectRatio.Num;
                        aspectRatio.Den = maContext->Video.Info.AspectRatio.Den;
                        dsyslog("cExtractLogo::SearchLogo(): aspect ratio at start frame (%ld) %i:%i", iFrameNumber, aspectRatio.Num, aspectRatio.Den);
                        firstFrame=false;
                    }
                    else {
                        if ((aspectRatio.Num != maContext->Video.Info.AspectRatio.Num) || (aspectRatio.Den != maContext->Video.Info.AspectRatio.Den)) {
                            continue;
                        }
                    }
                    int BorderIFrame = 0;
                    int isVBorder = vborder->Process(iFrameNumber, &BorderIFrame);
                    int isHBorder = hborder->Process(iFrameNumber, &BorderIFrame);
                    if (isVBorder) {
                        if (vborder->Status() == VBORDER_VISIBLE) {
                            dsyslog("cExtractLogo::SearchLogo(): detect new vertical border from frame (%d) to frame (%ld)", BorderIFrame, iFrameNumber);
                            iFrameCountValid-=DeleteBorderFrames(maContext, BorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no vertical border from frame (%ld)", iFrameNumber);
                        }
                    }
                    if (isHBorder) {
                        if (hborder->Status() == HBORDER_VISIBLE) {
                            dsyslog("cExtractLogo::SearchLogo(): detect new horizontal border from frame (%d) to frame (%ld)", BorderIFrame, iFrameNumber);
                            iFrameCountValid-=DeleteBorderFrames(maContext, BorderIFrame, iFrameNumber);
                        }
                        else {
                            dsyslog("cExtractLogo::SearchLogo(): no horizontal border from frame (%ld)", iFrameNumber);
                        }
                    }
                    if ((vborder->Status() == VBORDER_VISIBLE) || (hborder->Status() == HBORDER_VISIBLE)) {
                        continue;
                    }
                    iFrameCountValid++;
                    if (!maContext->Video.Data.Valid) {
                        dsyslog("cExtractLogo::SearchLogo(): faild to get video data of frame (%ld)", iFrameNumber);
                        continue;
                    }
                    for (int corner = 0; corner < CORNERS; corner++) {
                        int iFrameNumberNext = -1;  // flag for detect logo: -1: called by cExtractLogo, dont analyse, only fill area
                                                    //                       -2: called by cExtractLogo, dont analyse, only fill area, store logos in /tmp for debug
//                        if (corner == 1) iFrameNumberNext = -2;   // TODO only for debug
                        area->corner=corner;
                        logo->Detect(iFrameNumber,&iFrameNumberNext);
                        logoInfo actLogoInfo = {};
                        actLogoInfo.iFrameNumber = iFrameNumber;
                        actLogoInfo.aspectratio.Den = maContext->Video.Info.AspectRatio.Den;
                        actLogoInfo.aspectratio.Num = maContext->Video.Info.AspectRatio.Num;
                        memcpy(actLogoInfo.sobel,area->sobel, sizeof(area->sobel));
                        actLogoInfo.hits = this->Compare(maContext, &actLogoInfo, logoHeight, logoWidth, corner);

                        if (maContext->Config->autoLogo == 1) { // use packed logos
                            logoInfoPacked actLogoInfoPacked = {};
                            PackLogoInfo(&actLogoInfo, &actLogoInfoPacked);

                            try { logoInfoVectorPacked[corner].push_back(actLogoInfoPacked); }  // this allocates a lot of memory
                            catch(std::bad_alloc &e) {
                                dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %ld", iFrameNumber);
                                retStatus=false;
                                break;
                            }
                        }
                        if (maContext->Config->autoLogo == 2){  // use unpacked logos
                            try { logoInfoVector[corner].push_back(actLogoInfo); }  // this allocates a lot of memory
                            catch(std::bad_alloc &e) {
                                dsyslog("cExtractLogo::SearchLogo(): out of memory in pushback vector at frame %ld", iFrameNumber);
                                retStatus=false;
                                break;
                            }
                        }
                    }
                    if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus)  break; // finish inner loop and find best match
                }
            }
            if ((iFrameCountValid > 1000) || (iFrameCountAll >= MAXREADFRAMES) || !retStatus)  break; // finish outer loop and find best match
        }
    }
    if (!retStatus && (iFrameCountAll < MAXREADFRAMES)) {  // reached end of recording before we got 1000 valid frames
        dsyslog("cExtractLogo::SearchLogo(): end of recording reached at frame (%ld), read (%i) iFrames and got (%i) valid iFrames, try anyway", ptr_cDecoder->GetFrameNumber(), iFrameCountAll, iFrameCountValid);
        retStatus=true;
    }
    else {
        if (iFrameCountValid < 1000) {
            dsyslog("cExtractLogo::SearchLogo(): read (%i) frames and could not get enough valid frames (%i)", iFrameCountAll, iFrameCountValid);
            retStatus=false;
        }
    }
    if (retStatus) {
        dsyslog("cExtractLogo::SearchLogo(): got enough frames, start analyze");
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

        // find best corner
        logoInfo bestLogoInfo = {};
        int bestLogoCorner=-1;
        if (maContext->Config->autoLogo == 1) { // use packed logos
            logoInfoPacked bestLogoInfoPacked = {};
            for (int corner = 0; corner < CORNERS; corner++) {  // search for the best hits of each corner
                if (actLogoInfoPacked[corner].hits > bestLogoInfoPacked.hits) {
                    bestLogoInfoPacked = actLogoInfoPacked[corner];
                    bestLogoCorner=corner;
                }
            }
            UnpackLogoInfo(&bestLogoInfo, &bestLogoInfoPacked);
        }
        if (maContext->Config->autoLogo == 2) { // use unpacked logos
            for (int corner = 0; corner <= 3; corner++) {  // search for the best hits of each corner
                if (actLogoInfo[corner].hits > bestLogoInfo.hits) {
                    bestLogoInfo = actLogoInfo[corner];
                    bestLogoCorner=corner;
                }
            }
        }
        for (int corner = 0; corner <= 3; corner++) {  // free memory of the corners who are not selected
            if (corner == bestLogoCorner) continue;
            logoInfoVector[corner].clear();
            logoInfoVectorPacked[corner].clear();
        }

        if (bestLogoInfo.hits < 2) {
            dsyslog("cExtractLogo::SearchLogo(): no valid logo found, best logo at frame %i with %i similars at corner %i", bestLogoInfo.iFrameNumber, bestLogoInfo.hits, bestLogoCorner);
            retStatus=false;
        }
        else {
            dsyslog("cExtractLogo::SearchLogo(): best logo found at frame %i with %i similars at corner %i", bestLogoInfo.iFrameNumber, bestLogoInfo.hits, bestLogoCorner);
            if (! this->Resize(&bestLogoInfo, &logoHeight, &logoWidth, bestLogoCorner)) {
                dsyslog("cExtractLogo::SearchLogo(): resize logo failed");
                retStatus=false;
            }
            else {
                if (! this->Save(maContext, &bestLogoInfo, logoHeight, logoWidth, bestLogoCorner)) {
                    dsyslog("cExtractLogo::SearchLogo(): logo save failed");
                    retStatus=false;
                }
            }
        }
    }
    for (int corner = 0; corner <= 3; corner++) {  // free memory of all corners
        logoInfoVector[corner].clear();
        logoInfoVectorPacked[corner].clear();
    }
    ptr_cDecoder->~cDecoder();
    hborder->~cMarkAdBlackBordersHoriz();
    vborder->~cMarkAdBlackBordersVert();
    logo->~cMarkAdLogo();
    maContext->Video = maContextSaveState.Video;     // restore state of calling video context
    maContext->Audio = maContextSaveState.Audio;     // restore state of calling audio context
    if (retStatus) dsyslog("cExtractLogo::SearchLogo(): finished successfully");
    else dsyslog("cExtractLogo::SearchLogo(): failed");
    dsyslog("----------------------------------------------------------------------------");
    return retStatus;
}
