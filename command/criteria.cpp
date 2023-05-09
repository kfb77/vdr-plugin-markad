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
        case MT_HBORDERCHANGE:
            state = hborder;
            break;
        case MT_VBORDERCHANGE:
            state = vborder;
            break;
        default:
            dsyslog("cMarkCriteria::GetMarkTypeState(): type %d not valid", type);
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
        dsyslog("cMarkCriteria::SetMarkTypeState(): set mark type for %s to %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
    switch (type) {
        case MT_HBORDERCHANGE:
            hborder = state;
            break;
        case MT_VBORDERCHANGE:
            vborder = state;
            break;
        default:
            esyslog("cMarkCriteria::SetMarkTypeState(): type %d not valid", type);
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
