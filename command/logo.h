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

/**
 * logo after sobel transformation
 */
struct sLogoInfo {
    int iFrameNumber = -1;   //!< frame number of the logo
                             //!<

    int hits = 0;            //!< number of similar other logos
                             //!<

    uchar **sobel = NULL;    //!< sobel transformed corner picture data
                             //!<

    bool valid[PLANES] = {}; //!< <b>true:</b> data planes contain valid data <br>
                             //!< <b>false:</b> data planes are not valid
                             //!<
};


class cExtractLogo : public cLogoSize {
    public:
        explicit cExtractLogo(sMarkAdContext *maContext, const sAspectRatio aspectRatio, cIndex *recordingIndex);
        ~cExtractLogo();
        int SearchLogo(sMarkAdContext *maContext, int startFrame);
        void SetLogoSize(const sMarkAdContext *maContext, int *logoHeight, int *logoWidth);
        bool CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int match0 = 0, int match12 = 0, int *rate0 = NULL);

        bool abort = false;
        void SetAbort() {
            abort = true;
        };
    private:
        struct compareInfoType {
            int frameNumber1 = 0;
            int frameNumber2 = 0;
            int rate[CORNERS] = {0};
        };
        typedef std::vector<compareInfoType> compareResultType;
        struct sLogoInfoPacked {
            int iFrameNumber = -1;         //!< frame number of the logo
                                           //!<

            int hits = 0;                  //!< number of similar other logos
                                           //!<

            uchar **sobel = NULL;          //!< sobel transformed corner picture data, 8 pixel in one byte
                                           //!<

            bool valid[PLANES] = {};       //!< <b>true:</b> data planes contain valid data <br>
                                           //!< <b>false:</b> data planes are not valid
                                           //!<

            sAspectRatio aspectratio = {}; //!< video aspect ratio
                                           //!<

        };

        bool Save(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner, const int framenumber,  const char *debugText);
        bool CheckValid(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        int Compare(const sMarkAdContext *maContext, sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);
        bool CompareLogoPairRotating(const sMarkAdContext *maContext, sLogoInfo *logo1, sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner);
        void CutOut(sLogoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, const int corner);

/**
 * check if found logo size and corner is valid
 * @param maContext  markad context
 * @param logoHeight logo height
 * @param logoWidth  logo width
 * @logoCorner       corner of logo
 * @return true if logo size and corner is valid, flase otherwise
 */
        bool CheckLogoSize(const sMarkAdContext *maContext, const int logoHeight, const int logoWidth, const int logoCorner);

        bool Resize(const sMarkAdContext *maContext, sLogoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner);
        bool IsWhitePlane(const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane);
        bool IsLogoColourChange(const sMarkAdContext *maContext, const int corner);
        int DeleteFrames(const sMarkAdContext *maContext, const int from, const int to);
        bool WaitForFrames(sMarkAdContext *maContext, cDecoder *ptr_cDecoder, const int minFrame);
        void PackLogoInfo(const sLogoInfo *logoInfo, sLogoInfoPacked *logoInfoPacked);
        void UnpackLogoInfo(sLogoInfo *logoInfo, const sLogoInfoPacked *logoInfoPacked);
        int GetFirstFrame(const sMarkAdContext *maContext);
        int GetLastFrame(const sMarkAdContext *maContext);
        int CountFrames(const sMarkAdContext *maContext);
        void RemovePixelDefects(const sMarkAdContext *maContext, sLogoInfo *logoInfo, const int logoHeight, const int logoWidth, const int corner);
        int AudioInBroadcast(const sMarkAdContext *maContext, const int iFrameNumber);   // 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel

        sMarkAdContext *maContextLogoSize = NULL;
        cIndex *recordingIndexLogo = NULL;
        std::vector<sLogoInfo> logoInfoVector[CORNERS];
        std::vector<sLogoInfoPacked> logoInfoVectorPacked[CORNERS];
        int recordingFrameCount = 0;
        sAspectRatio logoAspectRatio = {};
        int AudioState = 0;  // 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
        int iFrameCountValid = 0;
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };
};
#endif
