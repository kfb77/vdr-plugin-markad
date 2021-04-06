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

#define MAXREADFRAMES 3000

#define TOP_LEFT 0
#define TOP_RIGHT 1
#define BOTTOM_LEFT 2
#define BOTTOM_RIGHT 3


struct logoInfo {
    int iFrameNumber = -1;
    int hits = 0;
    uchar sobel[PLANES][MAXPIXEL] = {};
    bool valid[PLANES] = {};
};


class cExtractLogo {
    public:
        explicit cExtractLogo(const MarkAdAspectRatio aspectRatio, cIndex *recordingIndex);
        ~cExtractLogo();
        int SearchLogo(MarkAdContext *maContext, int startFrame);
        void SetLogoSize(const MarkAdContext *maContext, int *logoHeight, int *logoWidth);
        bool CompareLogoPair(const logoInfo *logo1, const logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int match0 = 0, int match12 = 0, int *rate0 = NULL);

        bool abort = false;
        void SetAbort() {
            abort = true;
        };
    private:
        struct compareInfoType {  // TODO remove
            int frameNumber1 = 0;
            int frameNumber2 = 0;
            int rate[CORNERS] = {0};
        };
        typedef std::vector<compareInfoType> compareResultType;
        struct logoInfoPacked {
            int iFrameNumber = 0;
            int hits = 0;
            uchar sobel[PLANES][MAXPIXEL / 8] = {};
            bool valid[PLANES] = {};
            MarkAdAspectRatio aspectratio = {};
        };

        bool Save(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner, const int framenumber,  const char *debugText);
        bool CheckValid(const MarkAdContext *maContext, const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        int Compare(const MarkAdContext *maContext, logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        bool CompareLogoPairRotating(logoInfo *logo1, logoInfo *logo2, const int logoHeight, const int logoWidth, const int corner);
        void CutOut(logoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, const int corner);
        bool CheckLogoSize(const MarkAdContext *maContext, const int logoHeight, const int logoWidth, const int logoCorner);
        bool Resize(const MarkAdContext *maContext, logoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner);
        bool IsWhitePlane(const logoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane);
        bool IsLogoColourChange(const MarkAdContext *maContext, const int corner);
        int DeleteFrames(const MarkAdContext *maContext, const int from, const int to);
        bool WaitForFrames(MarkAdContext *maContext, cDecoder *ptr_cDecoder, const int minFrame);
        void PackLogoInfo(const logoInfo *logoInfo, logoInfoPacked *logoInfoPacked);
        void UnpackLogoInfo(logoInfo *logoInfo, const logoInfoPacked *logoInfoPacked);
        int GetFirstFrame(const MarkAdContext *maContext);
        int GetLastFrame(const MarkAdContext *maContext);
        int CountFrames(const MarkAdContext *maContext);
        void RemovePixelDefects(const MarkAdContext *maContext, logoInfo *logoInfo, const int logoHeight, const int logoWidth, const int corner);
        int AudioInBroadcast(const MarkAdContext *maContext, const int iFrameNumber);   // 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel

        cIndex *recordingIndexLogo = NULL;
        std::vector<logoInfo> logoInfoVector[CORNERS];
        std::vector<logoInfoPacked> logoInfoVectorPacked[CORNERS];
        int recordingFrameCount = 0;
        MarkAdAspectRatio logoAspectRatio = {};
        int AudioState = 0;  // 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
        int iFrameCountValid = 0;
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };
};
#endif
