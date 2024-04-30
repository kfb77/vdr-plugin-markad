/*
 * criteria.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __criteria_h_
#define __criteria_h_

#include "marks.h"
#include "tools.h"

enum eCriteria {
    CRITERIA_USED        =  2,
    CRITERIA_AVAILABLE   =  1,
    CRITERIA_UNKNOWN     =  0,
    CRITERIA_UNAVAILABLE = -1,
    CRITERIA_DISABLED    = -2,
};

enum eFadeInOut {
    FADE_ERROR = -1,
    FADE_NONE  =  0,
    FADE_IN    =  1,
    FADE_OUT   =  2,
};



/**
 * store valid mark creteria for broadcast
 */
class cCriteria : public cMarks, cTools {
public:

    /**
     * cCriteria constructor
     */
    cCriteria();
    ~cCriteria();

    /**
     * get status of channel have exact VPS events
     * @param channelName name of the channel
     * @return status
     */
    bool GoodVPS(const char *channelName);

    /**
     * get status of channel uses fade in/out/fade logo
     * @param channelName name of the channel
     * @return status
     */
    int LogoFadeInOut(const char *channelName);

    /**
     * get status of channel if logo is in hborder or vborder
     * @param channelName name of the channel
     * @return status
     */
    bool LogoInBorder(const char *channelName);

    /**
     * get status of channel if infos are in hborder or vborder
     * @param channelName name of the channel
     * @return status
     */
    bool InfoInBorder(const char *channelName);

    /**
     * get status of channel has logo rotating
     * @param channelName name of the channel
     * @return status
     */
    bool LogoRotating(const char *channelName);

    /**
     * get status of channel logo rotating
     * @param channelName name of the channel
     * @return status
     */
    bool LogoTransparent(const char *channelName);


    /**
     * get status of a mark type
     * @return status
     */
    int GetMarkTypeState(const int type) const;


    /**
     * set status of a mark type
     * @param type   mark type
     * @param state  set to this state
     */
    void SetMarkTypeState(const int type, const int state);


    /**
     * list mark type stati
     */
    void ListMarkTypeState() const;

    /**
     *  get detection state of marks from type
     */
    bool GetDetectionState(const int type) const;


    /**
     * get status of possible closing credits without logo
     * @return status
     */
    int GetClosingCreditsState(const int position) const;


    /**
     * set status of closing credits without logo
     * @param position  frame number
     * @param state     set to this state
     */
    void SetClosingCreditsState(const int position, const int state);


    /**
     * turn on/of detection of marks from type
     */
    void SetDetectionState(const int type, const bool state);


    /**
     * list detection stati
     */
    void ListDetection() const;


private:
    /**
     * convert state to printable text
     * @param state
     * @return state in printable text
     */
    static char *StateToText(const int state);

    int logo                  = CRITERIA_UNKNOWN;  //!< status of logo in broadcast
    //!<
    int hborder               = CRITERIA_UNKNOWN;  //!< status of hborder in broadcast
    //!<
    int vborder               = CRITERIA_UNKNOWN;  //!< status of vborder in broadcast
    //!<
    int aspectratio           = CRITERIA_UNKNOWN;  //!< status of aspact ration in broadcast
    //!<
    int channel               = CRITERIA_UNKNOWN;  //!< status of channel changes in broadcast
    //!<

    int closingCreditsState   = CRITERIA_UNKNOWN;  //!< status of closing credits after end mark
    //!<
    int closingCreditsPos     = -1;                //!< mark position from status
    //!<

    bool sceneDetection       = true;              //!< true if we have to detect scene changes, false otherwise
    //!<
    bool soundDetection       = true;              //!< true if we have to detect silence, false otherwise
    //!<
    bool blackscreenDetection = true;              //!< true if we have to detect black screens, false otherwise
    //!<
    bool logoDetection        = true;              //!< true if we have to detect logo, false otherwise
    //!<
    bool vborderDetection     = true;              //!< true if we have to detect vertical border, false otherwise
    //!<
    bool hborderDetection     = true;              //!< true if we have to detect horizontal border, false otherwise
    //!<
    bool aspectratioDetection = true;              //!< true if we have to detect video aspect ratio changes, false otherwise
    //!<
    bool channelDetection     = true;              //!< true if we have to detect channel changes, false otherwise
    //!<

    bool videoDecoding        = true;              //!< true if we have do decode video stream, false otherwise
    //!<
    bool audioDecoding        = true;              //!< true if we have do decode audio stream, false otherwise
    //!<
};
#endif
