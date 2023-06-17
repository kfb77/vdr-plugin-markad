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
            state = aspectratio;
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
    if (typeToText && stateToText) {
        dsyslog("cMarkCriteria::GetMarkTypeState(): %-17s: %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
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
         case MT_LOGOCHANGE:
            logo = state;
            if (logo == CRITERIA_USED) {
                SetDetectionState(MT_VBORDERCHANGE, false);
                SetDetectionState(MT_HBORDERCHANGE, false);
                SetDetectionState(MT_ASPECTCHANGE,  false);
                SetDetectionState(MT_CHANNELCHANGE, false);
            }
            if (logo == CRITERIA_UNKNOWN) {
                SetDetectionState(MT_LOGOCHANGE,    true);
            }
            if (logo == CRITERIA_DISABLED) {
                SetDetectionState(MT_LOGOCHANGE,    false);
            }
            break;
         case MT_VBORDERCHANGE:
            vborder = state;
            if (vborder == CRITERIA_USED) {
                SetDetectionState(MT_SCENECHANGE,   false);
                SetDetectionState(MT_SOUNDCHANGE,   false);
                SetDetectionState(MT_BLACKCHANGE,   false);
                SetDetectionState(MT_LOGOCHANGE,    false);
                SetDetectionState(MT_HBORDERCHANGE, false);
                SetDetectionState(MT_ASPECTCHANGE,  false);
                SetDetectionState(MT_CHANNELCHANGE, false);
            }
            if (vborder == CRITERIA_UNAVAILABLE) {
                SetDetectionState(MT_VBORDERCHANGE, false);
            }
            break;
        case MT_HBORDERCHANGE:
            hborder = state;
            if (hborder == CRITERIA_USED) {
                SetDetectionState(MT_SCENECHANGE,   false);
                SetDetectionState(MT_SOUNDCHANGE,   false);
                SetDetectionState(MT_BLACKCHANGE,   false);
                SetDetectionState(MT_LOGOCHANGE,    false);
                SetDetectionState(MT_VBORDERCHANGE, false);
                SetDetectionState(MT_ASPECTCHANGE,  false);
                SetDetectionState(MT_CHANNELCHANGE, false);
            }
            if (hborder == CRITERIA_UNAVAILABLE) {
                SetDetectionState(MT_HBORDERCHANGE, false);
            }
            break;
        case MT_ASPECTCHANGE:
            aspectratio = state;
            if (aspectratio == CRITERIA_USED) {
                SetDetectionState(MT_SCENECHANGE,   false);
                SetDetectionState(MT_SOUNDCHANGE,   false);
                SetDetectionState(MT_BLACKCHANGE,   false);
                SetDetectionState(MT_LOGOCHANGE,    false);
                SetDetectionState(MT_VBORDERCHANGE, false);
                SetDetectionState(MT_HBORDERCHANGE, false);
                SetDetectionState(MT_CHANNELCHANGE, false);
            }
            break;
        case MT_CHANNELCHANGE:
            channel = state;
            if (channel == CRITERIA_USED) {
                SetDetectionState(MT_SCENECHANGE,   false);
                SetDetectionState(MT_SOUNDCHANGE,   false);
                SetDetectionState(MT_BLACKCHANGE,   false);
                SetDetectionState(MT_LOGOCHANGE,    false);
                SetDetectionState(MT_VBORDERCHANGE, false);
                SetDetectionState(MT_HBORDERCHANGE, false);
                SetDetectionState(MT_ASPECTCHANGE,  false);
            }
            break;
        default:
            esyslog("cMarkCriteria::SetMarkTypeState(): type 0x%X not valid", type);
    }
}


void cMarkCriteria::ListMarkTypeState() {
    GetMarkTypeState(MT_LOGOCHANGE);
    GetMarkTypeState(MT_VBORDERCHANGE);
    GetMarkTypeState(MT_HBORDERCHANGE);
    GetMarkTypeState(MT_ASPECTCHANGE);
    GetMarkTypeState(MT_CHANNELCHANGE);
}


int cMarkCriteria::GetClosingCreditsState() {
    dsyslog("cMarkCriteria::GetClosingCreditState(): closing credits state is %d", closingCredits);
    return closingCredits;
}


// define text to mark status of broadcast
// return pointer to text, calling function has to free memory
//
char *cMarkCriteria::StateToText(const int state) {
    char *text = NULL;
    switch (state) {
        case CRITERIA_USED:
            if (asprintf(&text, "used") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
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
        case CRITERIA_DISABLED:
            if (asprintf(&text, "DISBALED") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        default:
            if (asprintf(&text, "state invalid") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            esyslog("cMarkCriteria::StateToText(): state %d not valid", state);
            break;
       }
    return text;
}


void cMarkCriteria::SetClosingCreditsState(const int state) {
    dsyslog("cMarkCriteria::SetClosingCreditState(): set closing credits state to %d", state);
    closingCredits = state;
}


bool cMarkCriteria::GetDetectionState(const int type) {
    bool state = true;
   switch (type) {
       case MT_SCENECHANGE:
            state = sceneDetection;
            break;
       case MT_SOUNDCHANGE:
            state = soundDetection;
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
       case MT_ASPECTCHANGE:
            state = aspectratioDetection;
            break;
       case MT_CHANNELCHANGE:
            state = channelDetection;
            break;
       case MT_VIDEO:
            state = videoDecoding;
            break;
       case MT_AUDIO:
            state = audioDecoding;
            break;
       default:
            esyslog("cMarkCriteria::GetDetectionState(): type 0x%X not valid", type);
    }
    return state;
}


void cMarkCriteria::SetDetectionState(const int type, const bool state) {
   switch (type) {
        case MT_SCENECHANGE:
            sceneDetection = state;
            break;
        case MT_SOUNDCHANGE:
            soundDetection = state;
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
        case MT_ASPECTCHANGE:
            aspectratioDetection = state;
            break;
        case MT_CHANNELCHANGE:
            channelDetection = state;
            break;
        case MT_ALL:
            sceneDetection       = state;
            soundDetection       = state;
            blackscreenDetection = state;
            logoDetection        = state;
            vborderDetection     = state;
            hborderDetection     = state;
            aspectratioDetection = state;
            channelDetection     = state;
            break;
        default:
            esyslog("cMarkCriteria::SetDetectionState(): type 0x%X not valid", type);
            break;
    }
    if (GetDetectionState(MT_SCENECHANGE) || GetDetectionState(MT_BLACKCHANGE) || GetDetectionState(MT_LOGOCHANGE) || GetDetectionState(MT_VBORDERCHANGE) || GetDetectionState(MT_HBORDERCHANGE) || GetDetectionState(MT_ASPECTCHANGE)) videoDecoding = true;
    else videoDecoding = false;

    if (GetDetectionState(MT_SOUNDCHANGE) || GetDetectionState(MT_CHANNELCHANGE)) audioDecoding = true;
    else audioDecoding = false;

    char *typeToText  = TypeToText(type);
    if (typeToText) {
        dsyslog("cMarkCriteria::SetDetectionState(): set detection state for %-17s to %s, decoding: video is %s, audio is %s", typeToText, (state) ? "on" : "off", (videoDecoding) ? "on" : "off", (audioDecoding) ? "on" : "off");
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
    }
}


void cMarkCriteria::ListDetection() {
    dsyslog("cMarkCriteria::ListDetectionState(): MT_SCENECHANGE:     %s", GetDetectionState(MT_SCENECHANGE)   ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_SOUNDCHANGE:     %s", GetDetectionState(MT_SOUNDCHANGE)   ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_BLACKCHANGE:     %s", GetDetectionState(MT_BLACKCHANGE)   ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_LOGOCHANGE:      %s", GetDetectionState(MT_LOGOCHANGE)    ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_VBORDERCHANGE:   %s", GetDetectionState(MT_VBORDERCHANGE) ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_HBORDERCHANGE:   %s", GetDetectionState(MT_HBORDERCHANGE) ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_ASPECTCHANGE:    %s", GetDetectionState(MT_ASPECTCHANGE)  ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_CHANNELCHANGE:   %s", GetDetectionState(MT_CHANNELCHANGE) ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_VIDEO:           %s", GetDetectionState(MT_VIDEO)         ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_AUDIO:           %s", GetDetectionState(MT_AUDIO)         ? "on" : "off");
}
