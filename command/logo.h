/*
 * logo.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __logo_h_
#define __logo_h_

#include "global.h"
#include "markad-standalone.h"
#include "decoder_new.h"
#include "video.h"

#define CORNERS 4
#define MAXREADFRAMES 3000

#define TOP_LEFT 0
#define TOP_RIGHT 1
#define BOTTOM_LEFT 2
#define BOTTOM_RIGHT 3


class cExtractLogo {
    public:
        cExtractLogo();
        ~cExtractLogo();
        bool SearchLogo(MarkAdContext *maContext, int startFrame);
        bool abort = false;
        void SetAbort() {
            abort = true;
       };
    private:
        struct logoInfo {
            int iFrameNumber = 0;
            int hits = 0;
            uchar sobel[PLANES][MAXPIXEL] = {};
            bool valid[PLANES] = {};
            MarkAdAspectRatio aspectratio = {};
        };
        struct logoInfoPacked {
            int iFrameNumber = 0;
            int hits = 0;
            uchar sobel[PLANES][MAXPIXEL/8] = {};
            bool valid[PLANES] = {};
            MarkAdAspectRatio aspectratio = {};
        };
        std::vector<logoInfo> logoInfoVector[CORNERS];
        std::vector<logoInfoPacked> logoInfoVectorPacked[CORNERS];
        int recordingFrameCount = 0;

        bool Save(MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int corner);
        int Compare(MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int corner);
        bool CompareLogoPair(logoInfo *logo1, logoInfo *logo2, int logoHeight, int logoWidth);
        bool Resize(logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, int bestLogoCorner);
        bool isWhitePlane(logoInfo *ptr_actLogoInfo, int logoHeight, int logoWidth, int plane);
        int DeleteBorderFrames(MarkAdContext *maContext, int from, int to);
        bool WaitForFrames(MarkAdContext *maContext, cDecoder *ptr_cDecoder);
        void PackLogoInfo(logoInfo *logoInfo, logoInfoPacked *logoInfoPacked);
        void UnpackLogoInfo(logoInfo *logoInfo, logoInfoPacked *logoInfoPacked);
};
#endif
