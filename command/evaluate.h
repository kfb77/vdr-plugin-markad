/**
 * @file evaluate.h
 * A program for the Video Disk Recorder
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
#include "video.h"

/**
 * evaluate stop/start pair status
 */
enum eEvaluateStatus {
    STATUS_NO      = -1,
    STATUS_UNKNOWN =  0,
    STATUS_YES     =  1
};



/**
 * class to evalute channel special logos types
 */
class cEvaluateChannel {
    public:

/**
 * check if channel could have info logos
 * @return true if channel could have info logos, false otherwise
 */
        bool IsInfoLogoChannel(char *channelName);

/**
 * check if channel could have logo changes
 * @return true if channel could have logo changes, false otherwise
 */
        bool IsLogoChangeChannel(char *channelName);

/**
 * check if channel could have closing credits without logo
 * @return true if channel could have closing credits without logo, false otherwise
 */
        bool ClosingCreditChannel(char *channelName);

/**
 * check if channel could have advertising in frame with logo
 * @return true if channel advertising in frame with logo, false otherwise
 */
        bool AdInFrameWithLogoChannel(char *channelName);

/**
 * check for introduction logo
 * @return true if introduction logo detected, false otherwise
 */
        bool IntroductionLogoChannel(char *channelName);
};


/**
 * class to evaluate logo stop/start pair
 */
class cEvaluateLogoStopStartPair : public cEvaluateChannel {
    public:

/**
 * logo stop / start pair
 */
        struct sLogoStopStartPair {
            int stopPosition = -1;                        //!< frame number of logo stop mark
                                                          //!<
            int startPosition = -1;                       //!< frame number of logo start mark
                                                          //!<
            int isLogoChange = STATUS_UNKNOWN;            //!< status of logo chnage, value #eEvaluateStatus
                                                          //!<
            int isAdvertising = STATUS_UNKNOWN;           //!< status of advertising, value #eEvaluateStatus
                                                          //!<
            int isStartMarkInBroadcast = STATUS_UNKNOWN;  //!< status of in broadacst, value #eEvaluateStatus
                                                          //!<
            int isInfoLogo = STATUS_UNKNOWN;              //!< status of info logo, value #eEvaluateStatus
                                                          //!<
            int isClosingCredits = STATUS_UNKNOWN;        //!< status of closing credits, value #eEvaluateStatus
                                                          //!<
        };

/**
 * contructor for class to evalualte logo stop/start pairs
 * @param maContext       markad context
 * @param marks           object with all marks
 * @param blackMarks      object with all black screen marks
 * @param iStart          assumed start frame position
 * @param chkSTART        frame postion to check start part
 * @param iStopA          assumed end mark position
 */
        cEvaluateLogoStopStartPair(sMarkAdContext *maContext, cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART, const int iStopA);

        ~cEvaluateLogoStopStartPair();

/**
 * check if logo stop/start pair could be a logo change
 * @param[in]     marks             object with all marks
 * @param[in,out] logoStopStartPair structure of logo/stop start pair, result is stored here, isLogoChange is set to -1 if the part is no logo change
 * @param[in]     framesPerSecond   video frame rate
 * @param[in]     iStart            assumed start mark position
 * @param[in]     chkSTART          search for start mark position
 * @param[in]     iStopA            assumed stop mark position
 */
     void IsLogoChange(cMarks *marks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond, const int iStart, const int chkSTART);

/**
 * check if logo stop/start pair could be an info logo
 * @param blackMarks        object with all black screen marks
 * @param logoStopStartPair structure of logo/stop start pair, result is stored here
 * @param framesPerSecond         video frame rate
 */
        void IsInfoLogo(cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int framesPerSecond);

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
 * set info logo status to STATUS_YES
 * @param stopPosition  frame number of the logo stop mark
 * @param startPosition frame number of the logo start mark
 */
        void SetIsInfoLogo(const int stopPosition, const int startPosition);

/**
 * set closing credits status to STATUS_YES <br>
 * stopPosition / startPosition do not need exact match, they must be inbetween stop/start pair
 * @param stopPosition  frame number of the logo stop mark
 * @param startPosition frame number of the logo start mark
 */
        void SetIsClosingCredits(const int stopPosition, const int startPosition);

/**
 * get closing credits status
 * @param startPosition frame number of the logo start mark
 * @return true if startPosition found and isClosingCredits is STATUS_YES, false otherwise
 */
        bool GetIsClosingCredits(const int startPosition);

/** check of there is a info logo part between a logo stop/start pair
 * @param stopPosition  frame number of logo stop mark
 * @param startPosition frame number of logo start mark
 * @return true, if there is a info logo part between a logo stop/start pair
 */
        bool IncludesInfoLogo(const int stopPosition, const int startPosition);

    private:
        std::vector<sLogoStopStartPair> logoPairVector;                 //!< logo stop/start pair vector
                                                                        //!<
        std::vector<sLogoStopStartPair>::iterator nextLogoPairIterator; //!< iterator for logo stop/start pair vector
                                                                        //!<
};


/**
 * class to calculate logo size
 */
class cDetectLogoStopStart : public cLogoSize, public cEvaluateChannel {
    public:
/**
 * contructor for class to dectect special logo stop/start pair
 * @param maContextParam                 markad context
 * @param ptr_cDecoderParam              decoder
 * @param recordingIndexParam            recording index
 * @param evaluateLogoStopStartPairParam class to evalute logo stop/start pairs
 */
        cDetectLogoStopStart(sMarkAdContext *maContextParam, cDecoder *ptr_cDecoderParam, cIndex *recordingIndexParam, cEvaluateLogoStopStartPair *evaluateLogoStopStartPairParam);

        ~cDetectLogoStopStart();

/**
 * compare all frames in range and calculate similar rate
 * @param startFrame start frame number
 * @param endFrame   end frame number
 * @param adInFrame true if compare for advertising in frame without logo, false otherwise
 * @return true if successful, false otherwise
 */
        bool Detect(int startFrame, int endFrame, const bool adInFrame);

/**
 * detect if current logo stop/start pair contains a info logo
 * @return true if part is info logo, false otherwise
 */
        bool IsInfoLogo();

/**
 * check for logo change
 * @return true if part contains a logo change, false otherwise
 */
        bool IsLogoChange();

/**
 * check for closing credits without logo
 * @return frame number of end of closinf credits
 */
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

        sMarkAdContext *maContext;                              //!< markad context
                                                                //!<
        cDecoder *ptr_cDecoder;                                 //!< decoder
                                                                //!<
        cIndex *recordingIndex;                                 //!< recording index
                                                                //!<
        cEvaluateLogoStopStartPair *evaluateLogoStopStartPair;  //!< class to evalute logo stop/start pairs
                                                                //!<
        int startPos = 0;                                       //!< frame number of start position to compare
                                                                //!<
        int endPos   = 0;                                       //!< frame number of end position to compare
                                                                //!<
/**
 * compare two frames
 */
        struct sCompareInfo {
            int frameNumber1 = 0;                //!< frame number 1
            int frameNumber2 = 0;                //!< frame number 2
                                                 //!<
            int rate[CORNERS] = {0};             //!< similar rate of frame pair per corner
                                                 //!<
        };
        std::vector<sCompareInfo> compareResult; //!< vector of frame compare results
                                                 //!<
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };  //!< array to convert corner anum to text
                                                                                                    //!<
};
#endif
