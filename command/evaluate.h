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

/**
 * logo stop / start pair
 */
        struct sLogoStopStartPair {
            int stopPosition = -1;           //!< frame number of logo stop mark
                                             //!<

            int startPosition = -1;          //!< frame number of logo start mark
                                             //!<

            int isLogoChange = 0;            //!< -1 no logo change, 0 unknown, 1 is logo change
                                             //!<

            int isAdvertising = 0;           //!< -1 pair is no advertising, 0 unknown, 1 pair is advertising
                                             //!<

            int isStartMarkInBroadcast = 0;  //!< -1 start mark does not contain to broadcast, 0 unknown, 1 start mark contains to broadcast
                                             //!<

            int isInfoLogo = 0;              //!< -1 pair is no introduction sequence, 0 unknown, 1 pair is introduction sequence
                                             //!<

        };

/**
 * contructor for class to evalualte logo stop/start pairs
 * @param marks           object with all marks
 * @param blackMarks      object with all black screen marks
 * @param framesPerSecond video frame rate
 * @param iStart          assumed start frame position
 * @param chkSTART        frame postion to check start part
 * @param iStopA          assumed end mark position
 */
        cEvaluateLogoStopStartPair(clMarks *marks, clMarks *blackMarks, const int framesPerSecond, const int iStart, const int chkSTART, const int iStopA);

        ~cEvaluateLogoStopStartPair();

/**
 * check if logo stop/start pair could be an info logo part
 * @param blackMarks        object with all black screen marks
 * @param logoStopStartPair structure of logo/stop start pair, result is stored here
 * @framesPerSecond         video frame rate
 */
        void IsInfoLogo(clMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond);

/**
 * get next logo stop/start pair
 * @param stopPosition  frame number of logo stop mark
 * @param startPosition frame number of logo start mark
 * @param isLogoChange  -1 no logo change, 0 unknown, 1 is logo change
 * @param isInfoLogo    -1 pair is no introduction sequence, 0 unknown, 1 pair is introduction sequence
 * @return true if there is a next logo stop/start pair, false otherwise
 */
        bool GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo);

/**
 * set info logo status to "1 is logo change"
 * @param stopPosition  frame number of the logo stop mark
 * @param startPosition frame number of the logo start mark
 */
        void SetIsInfoLogo(const int stopPosition, const int startPosition);

/** check of there is a info logo part between a logo stop/start pair
 * @param stopPosition  frame number of logo stop mark
 * @param startPosition frame number of logo start mark
 * @return true, if there is a info logo part between a logo stop/start pair
 */
        bool IncludesInfoLogo(const int stopPosition, const int startPosition);

        int GetLastClosingCreditsStart();

    private:
        std::vector<sLogoStopStartPair> logoPairVector;
        std::vector<sLogoStopStartPair>::iterator nextLogoPairIterator;
};


class cDetectLogoStopStart {
    public:
        cDetectLogoStopStart(sMarkAdContext *maContext_, cDecoder *ptr_cDecoder_, cIndex *recordingIndex_);
        ~cDetectLogoStopStart();
        bool Detect(int startFrame, int endFrame, const bool adInFrame);

/**
 * detect if current logo stop/start pair contains a info logo
 * @return true if part is info logo, false otherwise
 */
        bool IsInfoLogo();

        bool isLogoChange();
        int ClosingCredit();

/**
 * check if current range is an advertising in frame with logo
 * @param isStartMark true if called to check after start mark, false if called to check before stop mark
 * @return first frame of advertising in frame with logo before logo stop mark or last frame of advertising in frame with logo after logo start mark <br>
 *         -1 if no advertising in frame with logo was found
 */
        int AdInFrameWithLogo(const bool isStartMark);

/**
 * check if current range is a introduction logo
 * @return last frame of the introduction logo after logo start mark
 */
        int IntroductionLogo();
    private:

/**
 * check if channel could have info logos
 * @return true if channel could have info logos, false otherwise
 */
        bool IsInfoLogoChannel();

/**
 * check if channel could have logo changes
 * @return true if channel could have logo changes, false otherwise
 */
        bool IsLogoChangeChannel();

/**
 * check if channel could have closing credits without logo
 * @return true if channel could have closing credits without logo, false otherwise
 */
        bool ClosingCreditChannel();

/**
 * check if channel could have advertising in frame with logo
 * @return true if channel advertising in frame with logo, false otherwise
 */
        bool AdInFrameWithLogoChannel();


        bool IntroductionLogoChannel();
        sMarkAdContext *maContext;
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
