/*
 * criteria.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"
#include "criteria.h"

cMarkCriteria::cMarkCriteria() {
}


cMarkCriteria::~cMarkCriteria() {
}


int cMarkCriteria::GetMarkTypeState(const int type) {
    int state = CRITERIA_UNKNOWN;
    switch (type) {
        case MT_BLACKCHANGE:
            state = CRITERIA_UNKNOWN;
            break;
        case MT_LOGOCHANGE:
            state = logo;
            break;
        case MT_VBORDERCHANGE:
            state = vborder;
            break;
        case MT_HBORDERCHANGE:
            state = hborder;
            break;
        case MT_ASPECTCHANGE:
            state = CRITERIA_UNKNOWN;
            break;
        case MT_CHANNELCHANGE:
            state = channel;
            break;
        default:
            esyslog("cMarkCriteria::GetMarkTypeState(): type 0x%X not valid", type);
            return CRITERIA_UNKNOWN;
    }
    char *typeToText  = TypeToText(type);
    char *stateToText = StateToText(state);
    dsyslog("cMarkCriteria::GetMarkTypeState(): type: %s state: %s", typeToText, stateToText);
    FREE(strlen(typeToText)+1, "text");
    free(typeToText);
    FREE(strlen(stateToText)+1, "state");
    free(stateToText);
    return state;
}


void cMarkCriteria::SetMarkTypeState(const int type, const int state) {
    char *typeToText  = TypeToText(type);
    char *stateToText = StateToText(state);
    if (typeToText && stateToText) {
        dsyslog("cMarkCriteria::SetMarkTypeState(): set mark type for %-12s to %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
    switch (type) {
         case MT_VBORDERCHANGE:
            vborder = state;
            if (vborder == CRITERIA_AVAILABLE) {
                SetDetectionState(MT_SCENECHANGE, false);  // we do not need detection of we have border
                SetDetectionState(MT_BLACKCHANGE, false);  // we do not need detection of we have border
                SetDetectionState(MT_LOGOCHANGE,  false);  // we do not need detection of we have border
            }
            if (vborder == CRITERIA_UNAVAILABLE) {
                SetDetectionState(MT_VBORDERCHANGE, false);
            }
            break;
        case MT_HBORDERCHANGE:
            hborder = state;
            if (hborder == CRITERIA_AVAILABLE) {
                SetDetectionState(MT_SCENECHANGE, false);  // we do not need detection of we have border
                SetDetectionState(MT_BLACKCHANGE, false);  // we do not need detection of we have border
                SetDetectionState(MT_LOGOCHANGE,  false);  // we do not need detection of we have border
            }
            if (hborder == CRITERIA_UNAVAILABLE) {
                SetDetectionState(MT_HBORDERCHANGE, false);
            }
            break;
        case MT_CHANNELCHANGE:
            channel = state;
            if (channel == CRITERIA_AVAILABLE) {
                SetDetectionState(MT_SCENECHANGE,   false);
                SetDetectionState(MT_BLACKCHANGE,   false);
                SetDetectionState(MT_LOGOCHANGE,    false);
                SetDetectionState(MT_VBORDERCHANGE, false);
                SetDetectionState(MT_HBORDERCHANGE, false);
            }
            break;
        default:
            esyslog("cMarkCriteria::SetMarkTypeState(): type 0x%X not valid", type);
    }
}



int cMarkCriteria::GetClosingCreditsState() {
    dsyslog("cMarkCriteria::GetClosingCreditState(): closing credits state is %d", closingCredits);
    return closingCredits;
}


void cMarkCriteria::SetClosingCreditsState(const int state) {
    dsyslog("cMarkCriteria::SetClosingCreditState(): set closing credits state to %d", state);
    closingCredits = state;
}


// define text to mark status of broadcast
// return pointer to text, calling function has to free memory
//
char *cMarkCriteria::StateToText(const int state) {
    char *text = NULL;
    switch (state) {
        case CRITERIA_AVAILABLE:
            if (asprintf(&text, "available") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        case CRITERIA_UNKNOWN:
            if (asprintf(&text, "unknown") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        case CRITERIA_UNAVAILABLE:
            if (asprintf(&text, "unavailable") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        default:
            esyslog("cMarkCriteria::StateToText(): state %d not valid", state);
       }
    return text;
}


bool cMarkCriteria::GetDetectionState(const int type) {
    bool state = true;
   switch (type) {
       case MT_SCENECHANGE:
            state = sceneDetection;
            break;
       case MT_BLACKCHANGE:
            state = blackscreenDetection;
            break;
       case MT_LOGOCHANGE:
            state = logoDetection;
            break;
       case MT_VBORDERCHANGE:
            state = vborderDetection;
            break;
       case MT_HBORDERCHANGE:
            state = hborderDetection;
            break;
       case MT_VIDEO:
            state = videoDecoding;
            break;
       default:
            esyslog("cMarkCriteria::GetDetectionState(): type 0x%X not valid", type);
    }
    return state;
}


void cMarkCriteria::SetDetectionState(const int type, const bool state) {
    char *typeToText  = TypeToText(type);
    if (typeToText) {
        dsyslog("cMarkCriteria::SetDetectionState(): set detection state for %-16s to %d", typeToText, state);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
    }
    switch (type) {
        case MT_SCENECHANGE:
            sceneDetection = state;
            break;
        case MT_BLACKCHANGE:
            blackscreenDetection = state;
            break;
        case MT_LOGOCHANGE:
            logoDetection = state;
            break;
        case MT_VBORDERCHANGE:
            vborderDetection = state;
            break;
        case MT_HBORDERCHANGE:
            hborderDetection = state;
            break;
        case MT_ALL:
            sceneDetection       = state;
            blackscreenDetection = state;
            logoDetection        = state;
            vborderDetection     = state;
            hborderDetection     = state;
            channelDetection     = state;
            break;
        default:
            esyslog("cMarkCriteria::SetDetectionState(): type 0x%X not valid", type);
    }
    if (GetDetectionState(MT_SCENECHANGE) || GetDetectionState(MT_BLACKCHANGE) || GetDetectionState(MT_LOGOCHANGE) || GetDetectionState(MT_VBORDERCHANGE) || GetDetectionState(MT_HBORDERCHANGE)) {
        dsyslog("cMarkCriteria::SetDetectionState(): video decoding is on");
        videoDecoding = true;
    }
    else {
        dsyslog("cMarkCriteria::SetDetectionState(): video decoding is off");
        videoDecoding = false;
    }
}


void cMarkCriteria::ListDetection() {
    dsyslog("cMarkCriteria::ListDetectionState(): MT_SCENECHANGE:   %d", GetDetectionState(MT_SCENECHANGE));
    dsyslog("cMarkCriteria::ListDetectionState(): MT_BLACKCHANGE:   %d", GetDetectionState(MT_BLACKCHANGE));
    dsyslog("cMarkCriteria::ListDetectionState(): MT_LOGOCHANGE:    %d", GetDetectionState(MT_LOGOCHANGE));
    dsyslog("cMarkCriteria::ListDetectionState(): MT_VBORDERCHANGE: %d", GetDetectionState(MT_VBORDERCHANGE));
    dsyslog("cMarkCriteria::ListDetectionState(): MT_HBORDERCHANGE: %d", GetDetectionState(MT_HBORDERCHANGE));
}
