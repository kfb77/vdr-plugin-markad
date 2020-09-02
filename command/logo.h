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
#include "audio.h"

#define CORNERS 4
#define MAXREADFRAMES 3000

#define TOP_LEFT 0
#define TOP_RIGHT 1
#define BOTTOM_LEFT 2
#define BOTTOM_RIGHT 3


class cExtractLogo {
    public:
        explicit cExtractLogo(MarkAdAspectRatio aspectRatio);
        ~cExtractLogo();
        int SearchLogo(MarkAdContext *maContext, int startFrame);
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
        MarkAdAspectRatio logoAspectRatio = {};
        bool is6Channel = false;
        bool has6Channel = false;
        int iFrameCountValid = 0;
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };

        bool Save(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        bool CheckValid(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        int Compare(const MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        bool CompareLogoPair(const logoInfo *logo1, const logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner);
        bool Resize(const MarkAdContext *maContext, logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner);
        bool isWhitePlane(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane);
        int DeleteFrames(const MarkAdContext *maContext, const int from, const int to);
        bool WaitForFrames(const MarkAdContext *maContext, cDecoder *ptr_cDecoder);
        void PackLogoInfo(const logoInfo *logoInfo, logoInfoPacked *logoInfoPacked);
        void UnpackLogoInfo(logoInfo *logoInfo, const logoInfoPacked *logoInfoPacked);
        int GetFirstFrame(const MarkAdContext *maContext);
        int GetLastFrame(const MarkAdContext *maContext);
        int CountFrames(const MarkAdContext *maContext);
};
#endif
