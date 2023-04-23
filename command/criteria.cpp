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


int cMarkCriteria::GetState(const int type) {
    int state = MARK_UNKNOWN;
    switch (type) {
        case MT_HBORDERCHANGE:
            state = hborder;
            break;
        default:
            dsyslog("cMarkCriteria::SetState(): type %d not valid", type);
    }
    dsyslog("cMarkCriteria::GetState(): type: %d state: %d", type, state);
    return state;
}


void cMarkCriteria::SetState(const int type, const int state) {
    char *typeToText  = TypeToText(type);
    char *stateToText = StateToText(state);
    if (typeToText && stateToText) {
        dsyslog("cMarkCriteria::SetState(): set mark type for %s to %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
    switch (type) {
        case MT_HBORDERCHANGE:
            hborder = state;
            break;
        default:
            esyslog("cMarkCriteria::SetState(): type %d not valid", type);
    }

}


// define text to mark status of broadcast
// return pointer to text, calling function has to free memory
//
char *cMarkCriteria::StateToText(const int state) {
    char *text = NULL;
    switch (state) {
        case MARK_AVAILABLE:
            if (asprintf(&text, "available") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        case MARK_UNKNOWN:
            if (asprintf(&text, "unknown") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        case MARK_UNAVAILABLE:
            if (asprintf(&text, "unavailable") != -1) {
                ALLOC(strlen(text)+1, "state");
            }
            break;
        default:
            esyslog("cMarkCriteria::StateToText(): state %d not valid", state);
       }
    return text;
}
