/**
 * @file evaluate.h
 * A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __evaluate_h_
#define __evaluate_h_

#include "debug.h"
#include "global.h"
#include "marks.h"
#include "video.h"
#include "tools.h"
#include "logo.h"


// max range of search after stop mark for closing credits
#define MAX_CLOSING_CREDITS_SEARCH 25 //!< max range of search after stop mark for closing credits

#define MAX_AD_IN_FRAME 60  //!< max range of search after logo start / before logo stop marks for ad in frame with logo
// sometimes advertising in frame has text in "e.g. Werbung"
// check longer range to prevent to detect text as second logo
// changed from 35 to 60


/**
 * class to evaluate logo stop/start pair
 */
class cEvaluateLogoStopStartPair {
public:
    /**
     * class to evaluate stop / start pair for special logos (info logo, logo change nd introduction logo"
     */
    cEvaluateLogoStopStartPair(cDecoder *decoderParam, cCriteria *criteriaParam);
    ~cEvaluateLogoStopStartPair();

    /**
     * set decoder to use in this object
     * @param decoderParam    pointer to current decoder to use in this object
     */
    void SetDecoder(cDecoder *decoderParam);

    /**
     * check logo stop/start pairs
     * @param marks           object with all marks
     * @param blackMarks      object with all black screen marks
     * @param iStart          assumed start frame position
     * @param chkSTART        frame position to check start part
     * @param packetEndPart   start of end part
     * @param iStopA          assumed end mark position
     */
    void CheckLogoStopStartPairs(cMarks *marks, cMarks *blackMarks, const int iStart, const int chkSTART, const int packetEndPart, const int iStopA);

    /**
     * check if logo stop/start pair can have ad in frame before/after
     * @param         marks             object with all marks
     * @param[in,out] logoStopStartPair structure of logo/stop start pair, result is stored here, isClosingCredits is set to -1 if the part is no logo change
     */
    void IsAdInFrame(cMarks *marks, sLogoStopStartPair *logoStopStartPair);

    /**
     * check if logo stop/start pair could be closing credits
     * @param[in]     marks             object with all marks
     * @param[in,out] logoStopStartPair structure of logo/stop start pair, result is stored here, isClosingCredits is set to -1 if the part is no logo change
     */
    static void IsClosingCredits(cMarks *marks, sLogoStopStartPair *logoStopStartPair);

    /**
     * check if logo stop/start pair could be a logo change
     * @param[in]     marks             object with all marks
     * @param[in,out] logoStopStartPair structure of logo/stop start pair, result is stored here, isLogoChange is set to -1 if the part is no logo change
     * @param[in]     iStart            assumed start mark position
     * @param[in]     chkSTART          search for start mark position
     */
    void IsLogoChange(cMarks *marks, sLogoStopStartPair *logoStopStartPair, const int iStart, const int chkSTART);

    /**
     * check if logo stop/start pair could be an info logo
     * @param marks             object with all marks
     * @param blackMarks        object with all black screen marks
     * @param logoStopStartPair structure of logo/stop start pair, result is stored here
     * @param iStopA            assumed stop frame number
     */
    void IsInfoLogo(cMarks *marks, cMarks *blackMarks, sLogoStopStartPair *logoStopStartPair, const int iStopA);

    /**
     * get next logo stop/start pair
     * @param logoStopStartPair  pointer to structure of logo stop/start pair
     * @return                   true if there is a next logo stop/start pair, false otherwise
     */
    bool GetNextPair(sLogoStopStartPair *logoStopStartPair);

    /**
     * get ad in frame status from logo stop position or logo start position
     * @param stopPosition   position of the logo stop mark
     * @param startPosition  position of the logo start mark
     * @return               state
     */
    int GetIsAdInFrame(const int stopPosition, const int startPosition);

    /**
     * set ad in frame status to state
     * @param stopPosition  frame number of the logo stop mark
     * @param state new state
     */
    void SetIsAdInFrameAroundStop(const int stopPosition, const int state);

    /**
     * set info logo status to STATUS_YES
     * @param stopPosition  frame number of the logo stop mark
     * @param startPosition frame number of the logo start mark
     */
    void SetIsInfoLogo(const int stopPosition, const int startPosition);

    /**
     * set closing credits status to STATUS_YES <br>
     * stopPosition / startPosition do not need exact match, they must be inbetween stop/start pair
     * @param stopPosition              packet number of the logo stop mark
     * @param startPosition             packet number of the logo start mark
     * @param endClosingCreditsPosition mark position of end of closing credits
     * @param endClosingCreditsPTS      mark PTS of end of closing credits
     * @param state                     new state
     */
    void SetIsClosingCredits(const int stopPosition, const int startPosition, const int endClosingCreditsPosition, const int64_t endClosingCreditsPTS, const eEvaluateStatus state);

    /**
     * get closing credits status before start mark
     * @param startPosition frame number of the logo start mark
     * @return value of isClosingCredits
     */
    int GetIsClosingCreditsBefore(const int startPosition);

    /**
     * get closing credits status after mark
     * @param stopPosition frame number of the logo stop mark
     * @return value of isClosingCredits
     */
    int GetIsClosingCreditsAfter(const int stopPosition);

    /**
     * add adinframe pair
     * we need to add pair because it is not result of a logo stop/start pair
     * @param startPosition frame number of the last frame before adinframe
     * @param stopPosition  frame number of the last frame of adinframe
     */
    void AddAdInFrame(const int startPosition, const int stopPosition);


    /**
     * get closing credits status
     * @param stopPosition      frame number of logo stop mark
     * @param startPosition     frame number of logo start mark
     * @param endClosingCredits end position of closing credits
     * @return value of isClosingCredits
     */
    eEvaluateStatus GetIsClosingCredits(const int stopPosition, const int startPosition, sMarkPos *endClosingCredits);

    /** check if there is a info logo part between a logo stop/start pair
     * @param stopPosition  frame number of logo stop mark
     * @param startPosition frame number of logo start mark
     * @return true, if there is a info logo part between a logo stop/start pair
     */
    bool IncludesInfoLogo(const int stopPosition, const int startPosition);

private:
    cDecoder *decoder   = nullptr;                                  //!< pointer to decoder
    //!<
    cCriteria *criteria = nullptr;                                  //!< analyse criteria
    //!<
    std::vector<sLogoStopStartPair> logoPairVector;                 //!< logo stop/start pair vector
    //!<
    std::vector<sLogoStopStartPair>::iterator nextLogoPairIterator; //!< iterator for logo stop/start pair vector
    //!<
};


/**
 * class to calculate logo size
 */
class cDetectLogoStopStart {
public:
    /**
     * constructor for class to dectect special logo stop/start pair
     * @param decoderParam                   pointer to decoder
     * @param indexParam                     pointer to recording index
     * @param criteriaParam                  detection criteria
     * @param logoCornerParam                logo corner index
     * @param evaluateLogoStopStartPairParam class to evaluate logo stop/start pairs
     */
    cDetectLogoStopStart(cDecoder *decoderParam, cIndex *indexParam, cCriteria *criteriaParam, cEvaluateLogoStopStartPair *evaluateLogoStopStartPairParam, const int logoCornerParam);

    ~cDetectLogoStopStart();

    /**
    * copy constructor
    */
    cDetectLogoStopStart(const cDetectLogoStopStart &origin) {
        memcpy(aCorner, origin.aCorner, sizeof(origin.aCorner));
        evaluateLogoStopStartPair = origin.evaluateLogoStopStartPair;
        compareResult = origin.compareResult;
        sobel = nullptr;
        logoCorner = origin.logoCorner;
        criteria = origin.criteria;
        index = origin.index;
        decoder =origin.decoder;
    }

    /**
     * operator=
     */
    cDetectLogoStopStart &operator =(const cDetectLogoStopStart *origin) {
        memcpy(aCorner, origin->aCorner, sizeof(origin->aCorner));
        evaluateLogoStopStartPair = origin->evaluateLogoStopStartPair;
        compareResult = origin->compareResult;
        sobel = origin->sobel;
        logoCorner = origin->logoCorner;
        criteria = origin->criteria;
        index = origin->index;
        decoder = origin->decoder;
        return *this;
    }


    /**
     * find first pixel in a sobel transformed picture
     * @param      picture sobel transformed picture
     * @param      corner  source corner of picture
     * @param      width   picture width
     * @param      height  picture height
     * @param      startX  x position to start search
     * @param      startY  y position to start search
     * @param      offsetX x offset for each search step (usually +1 or -1)
     * @param      offsetY y offset for each search step (usually +1 or -1)
     */
    int FindFrameFirstPixel(const uchar *picture, const int corner, const int width, const int height, int startX, int startY, const int offsetX, const int offsetY);

    /**
     * find start position of a possible frame in a sobel transformed picture
     * @param         picture sobel transformed picture
     * @param         width   picture width
     * @param         height  picture height
     * @param[in,out] startX  in: x position to start search, out: x position of possible frame
     * @param[in,out] startY  in: y position to start search, out: y position of possible frame
     * @param         offsetX x offset for each search step (usually +1 or -1)
     * @param         offsetY y offset for each search step (usually +1 or -1)
     */
    int FindFrameStartPixel(const uchar *picture, const int width, const int height,  int startX, int startY, const int offsetX, const int offsetY);

    /**
     * find start position of a possible frame in a sobel transformed picture
     * @param         picture sobel transformed picture
     * @param         width   picture width
     * @param         height  picture height
     * @param[in]     startX  x position to start search (start pixel)
     * @param[in]     startY  y position to start search (start pixel)
     * @param         offsetX x offset for each search step (usually +1 or -1)
     * @param         offsetY y offset for each search step (usually +1 or -1)
     * @param[in,out] endX    x position from end of the frame
     * @param[in,out] endY    y position from end of the frame
     */
    int FindFrameEndPixel(const uchar *picture, const int width, const int height, const int startX, const int startY, const int offsetX, const int offsetY, int *endX, int *endY);

    /**
     * detect a frame in a sobel transformed picture
     * @param picture     plane 0 of sobel transformed picture
     * @param width       picture width in pixel
     * @param height      picture height in pixel
     * @param corner      source corner of picture
     */
    int DetectFrame(const uchar *picture, const int width, const int height, const int corner);

    /**
     * compare all frames in range and calculate similar rate
     * @param startFrame start frame number
     * @param endFrame   end frame number
     * @return true if successful, false otherwise
     */
    bool Detect(int startFrame, int endFrame);

/// detect if current logo stop/start pair contains a info logo
    /**
     * a info logo is a static alternate logo (e.g. telexext info) <br>
     * fade in/out is possible
     * @param startPos  start position of possible info logo
     * @param endPos    end position of possible info logo
     * @param hasBorder true if there is a vborder or hborder from startPos to endPos
     * @return true if part is info logo, false otherwise
     */
    bool IsInfoLogo(int startPos, int endPos, const bool hasBorder);

/// check for logo change
    /**
     * logo change parts contains dynamic changing alternative logos (used only by TELE5)
     * @return true if part contains a logo change, false otherwise
     */
    bool IsLogoChange(int startPos, int endPos);

    /**
     * check for closing credits without logo and for closing still image
     * @param startPos           start position for search (stop mark)
     * @param endPos             end position for search
     * @param endClosingCredits  new end mark position after closing credits
     * @param noLogoCornerCheck  if true we not check logo corner because there is an info logo
     */
    void ClosingCredit(int startPos, int endPos, sMarkPos *endClosingCredits, const bool noLogoCornerCheck = false);

    /**
     * check if current range is an advertising in frame with logo
     * @param startPos  start frame position
     * @param endPos    end   frame position
     * @param adInFrame position of ad in frame
     * @param isStartMark true if called to check after start mark, false if called to check before stop mark
     * @param isEndMark   true if called to check after stop mark, false if called to check before stop mark
     */
    void AdInFrameWithLogo(int startPos, int endPos,  sMarkPos *adInFrame, const bool isStartMark, const bool isEndMark);

    /**
     * check if current range is a introduction logo
     * @param startPos          search start position
     * @param endPos            end position of search
     * @param introductionStart start position of introducion logo if found
     */
    void IntroductionLogo(int startPos, int endPos, sMarkPos *introductionStart);

private:
    /**
     * compair two logos
     * @param logo1      first logo
     * @param logo2      second logo
     * @param logoHeight logo height
     * @param logoWidth  logo width
     * @param corner     logo corner
     * @param rate0      similar rate
     * @return true if compair was successful
     */
    bool CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int *rate0);

    cDecoder *decoder         = nullptr;  //!< decoder
    //!<
    cIndex *index             = nullptr;  //!< decoder
    //!<
    cCriteria *criteria       = nullptr;  //!< class for mark detection criteria
    //!<
    sAreaT area               = {};       //!< result area
    //!<
    int logoCorner            = -1;       //!< index of logo corner
    //!<
    cSobel *sobel             = nullptr;  //!< class for sobel transformation
    //!<
    /**
     * compare two frames
     */
    struct sCompareInfo {
        int frameNumber1          = -1;       //!< frame number 1
        //!<
        int64_t pts1              = -1;       //!< pts of frame number 1
        //!<
        int frameNumber2          = -1;       //!< frame number 2
        //!<
        int64_t pts2              = -1;       //!< pts of frame number 2
        //!<
        int rate[CORNERS]         = {0};     //!< similar rate of frame pair per corner
        //!<
        int framePortion[CORNERS] = {0};     //!< portion of frame pixels of corner
        //!<
    };
    std::vector<sCompareInfo> compareResult;                //!< vector of frame compare results
    //!<
    cEvaluateLogoStopStartPair *evaluateLogoStopStartPair;  //!< class to evaluate logo stop/start pairs
    //!<
    const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" };  //!< array to convert corner anum to text
    //!<
};
#endif
