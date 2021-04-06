/*
 * evaluate.cpp.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __evaluate_h_
#define __evaluate_h_

extern "C" {
    #include "debug.h"
}
#include "global.h"
#include "marks.h"


class cEvaluateLogoStopStartPair {
    public:
        struct logoStopStartPairType {
            int stopPosition = -1;
            int startPosition = -1;
            int isLogoChange = 0;            // -1 no logo change, 0 unknown, 1 is logo change
            int isAdvertising = 0;           // -1 pair is no advertising, 0 unknown, 1 pair is advertising
            int isStartMarkInBroadcast = 0;  // -1 start mark does not contain to broadcast, 0 unknown, 1 start mark contains to broadcast
            int isInfoLogo = 0;           // -1 pair is no introduction sequence, 0 unknown, 1 pair is introduction sequence
        };

        cEvaluateLogoStopStartPair(clMarks *marks, clMarks *blackMarks, const int framesPerSecond, const int iStart, const int chkSTART, const int iStopA);
        ~cEvaluateLogoStopStartPair();
        void isInfoLogo(clMarks *blackMarks, logoStopStartPairType *logoStopStartPair, const int framesPerSecond);
        bool GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo);
        int GetLastClosingCreditsStart();

    private:
        std::vector<logoStopStartPairType> logoPairVector;
        std::vector<logoStopStartPairType>::iterator nextLogoPairIterator;
};


class cDetectLogoStopStart {
    public:
        cDetectLogoStopStart(MarkAdContext *maContext_, cDecoder *ptr_cDecoder_, cIndex *recordingIndex_);
        ~cDetectLogoStopStart();
        bool Detect(int startFrame, int endFrame, const bool adInFrame);
        bool isInfoLogo();
        bool isLogoChange();
        int ClosingCredit();
        int AdInFrameWithLogo(const bool isStartMark);
        int IntroductionLogo();
    private:
        bool isInfoLogoChannel();
        bool isLogoChangeChannel();
        bool ClosingCreditChannel();
        bool AdInFrameWithLogoChannel();
        bool IntroductionLogoChannel();
        MarkAdContext *maContext;
        cDecoder *ptr_cDecoder;
        cIndex *recordingIndex;
        int startPos = 0;
        int endPos   = 0;
        struct compareInfoType {
            int frameNumber1 = 0;
            int frameNumber2 = 0;
            int rate[CORNERS] = {0};
        };
        typedef std::vector<compareInfoType> compareResultType;
        compareResultType compareResult;
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };
};
#endif
