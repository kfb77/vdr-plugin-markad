/*
 * marks.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include "global.h"
#ifdef WINDOWS
#include "win32/mingw64.h"
#endif

#include "marks.h"
#include "debug.h"


// global variable
extern bool abortNow;


cMark::cMark(const int typeParam, const int oldTypeParam, const int newTypeParam, const int positionParam, const char *commentParam, const bool inBroadCastParam) {
    type        = typeParam;
    newType     = newTypeParam;
    oldType     = oldTypeParam;
    position    = positionParam;
    inBroadCast = inBroadCastParam;
    if (commentParam) {
        comment = strdup(commentParam);
        ALLOC(strlen(comment)+1, "comment");
    }
    else comment = nullptr;

    prev          = nullptr;
    next          = nullptr;
    timeOffsetPTS = nullptr;
    secOffsetPTS  = -1;
}


cMark::~cMark() {
    if (comment) {
        FREE(strlen(comment)+1, "comment");
        free(comment);
    }
    if (timeOffsetPTS) {
        FREE(strlen(timeOffsetPTS)+1, "timeOffsetPTS");
        free(timeOffsetPTS);
    }
}


// set PTS based time offset text from mark position
void cMark::SetTime(char* time, int offset) {
    if (!time) {
        if (timeOffsetPTS) {
            FREE(strlen(timeOffsetPTS)+1, "timeOffsetPTS");
            free(timeOffsetPTS);
        }
    }
    else timeOffsetPTS = time;
    secOffsetPTS = offset;
}


// get PTS based time offset in seconds from mark position
int cMark::GetTimeSeconds() const {
    return secOffsetPTS;
}

// get PTS based time offset text from mark position
char *cMark::GetTime() {
    return timeOffsetPTS;
}


cMarks::cMarks() {
    strcpy(filename, "marks");
    first = last = nullptr;
    count = 0;
}


cMarks::~cMarks() {
    DelAll();
}


// write all current marks to log file
//
void cMarks::Debug() {           // write all marks to log file
    dsyslog("***********************************************************************************************************************");
    dsyslog("cMarkAdStandalone::DebugMarks(): current marks:");

    // strong marks
    cMark *mark = first;
    while (mark) {
        const char *indexToHMSF = GetTime(mark);
        if (indexToHMSF) {
            char *markType = TypeToText(mark->type);
            if (markType) {
                if ((mark->type & 0x0F) == MT_START) LogSeparator(false);
                if ((mark->type & 0xF0) == MT_MOVED) {
                    char *markOldType = TypeToText(mark->oldType);
                    char *markNewType = TypeToText(mark->newType);
                    if (markOldType && markNewType) {
                        dsyslog("mark at position %6d: %-5s %-18s at %s, inBroadCast %d, old type: %s %s, new type: %s %s", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast, markOldType, ((mark->oldType & 0x0F) == MT_START)? "start" : "stop", markNewType, ((mark->newType & 0x0F) == MT_START)? "start" : "stop");
                        FREE(strlen(markOldType)+1, "text");
                        free(markOldType);
                        FREE(strlen(markNewType)+1, "text");
                        free(markNewType);
                    }
                }
                else dsyslog("mark at position %6d: %-5s %-18s at %s, inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else dsyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        else esyslog("cMarkAdStandalone::DebugMarks(): could not get time to mark (%d) type %d", mark->position, mark->type);
        mark = mark->Next();
    }
#ifdef DEBUG_WEAK_MARKS
    // weak marks
    dsyslog("------------------------------------------------------------");
    dsyslog("cMarkAdStandalone::DebugMarks(): current black marks:");
    mark = blackMarks.GetFirst();
    while (mark) {
        const char *indexToHMSF = marks.GetTime(mark);
        if (indexToHMSF) {
            char *markType = marks.TypeToText(mark->type);
            if (markType) {
                dsyslog("mark at position %6d: %-5s %-18s at %s inBroadCast %d", mark->position, ((mark->type & 0x0F) == MT_START)? "start" : "stop", markType, indexToHMSF, mark->inBroadCast);
                FREE(strlen(markType)+1, "text");
                free(markType);
            }
            else esyslog("cMarkAdStandalone::DebugMarks(): could not get type to mark (%d) type %d", mark->position, mark->type);
        }
        mark=mark->Next();
    }
#endif
    dsyslog("***********************************************************************************************************************");
}


int cMarks::Count(const int type, const int mask) const {
    if (type == 0xFF) return count;
    if (!first) return 0;

    int ret = 0;
    cMark *mark = first;
    while (mark) {
        if ((mark->type & mask) == type) ret++;
        mark = mark->Next();
    }
    return ret;
}


void cMarks::Del(const int position) {
    if (!first) return; // no elements yet

    cMark *next, *mark = first;
    while (mark) {
        next = mark->Next();
        if (mark->position == position) {
            dsyslog("cMarks::Del(): delete mark (%d)", position);
            Del(mark);
            return;
        }
        mark = next;
    }
}


void cMarks::DelType(const int type, const int mask) {
    cMark *mark = first;
    while (mark) {
        if ((mark->type & mask) == type) {
            cMark *tmpMark = mark->Next();
            Del(mark);
            mark = tmpMark;
        }
        else mark = mark->Next();
    }
}


void cMarks::DelWeakFromTo(const int from, const int to, const short int type) {
    cMark *mark = first;
    while (mark) {
        if (mark->position >= to) return;
        if ((mark->position > from) && (mark->type < (type & 0xF0))) {
            cMark *tmpMark = mark->Next();
            Del(mark);
            mark = tmpMark;
        }
        else mark = mark->Next();
    }
}


// delete all marks <from> <to> of <type>
// include <from> and <to>
//
void cMarks::DelFromTo(const int from, const int to, const int type, const int mask) {
    cMark *mark = first;
    while (mark) {
        if (mark->position > to) return;
        if ((mark->position >= from) && ((type == MT_ALL) || ((mark->type & mask) == type))) {
            cMark *tmpMark = mark->Next();
            Del(mark);
            mark = tmpMark;
        }
        else mark = mark->Next();
    }
}


// <FromStart> = true: delete all marks from start to <Position>
// <FromStart> = false: delete all marks from <Position> to end
//
void cMarks::DelTill(const int position, const bool fromStart) {
    cMark *next, *mark = first;
    if (!fromStart) {
        while (mark) {
            if (mark->position > position) break;
            mark = mark->Next();
        }
    }
    while (mark) {
        next = mark->Next();
        if (fromStart) {
            if (mark->position < position) {
                dsyslog("cMarks::DelTill(): delete mark (%d)", mark->position);
                Del(mark);
            }
        }
        else {
            dsyslog("cMarks::DelTill(): delete mark (%d)", mark->position);
            Del(mark);
        }
        mark = next;
    }
}


// delete all marks after position to last mark
//
void cMarks::DelAfterFromToEnd(const int position) {
    cMark *mark = first;
    while (mark) {  // find first mark after position
        if (mark->position > position) break;
        mark = mark->Next();
    }
    while (mark) {
        cMark * next = mark->Next();
        Del(mark);
        mark = next;
    }
}


void cMarks::DelAll() {
    cMark *next, *mark = first;
    while (mark) {
        next = mark->Next();
        Del(mark);
        mark=next;
    }
    first = nullptr;
    last = nullptr;
}


void cMarks::DelInvalidSequence() {
    if (!first || !last) return;

    cMark *mark = first;
    while (mark) {

        // if first mark is a stop mark, remove it
        if (((mark->type & 0x0F) == MT_STOP) && (mark == first)) {
            dsyslog("cMarks::DelInvalidSequence(): Start with STOP mark (%d) type 0x%X, delete first mark", mark->position, mark->type);
            cMark *tmp = mark;
            mark = mark->Next();
            Del(tmp);
            continue;
        }

        // cleanup of start mark followed by start mark or stop mark followed by stop mark
        cMark *markNext = mark->Next();
        if (markNext) {
            if ((((mark->type & 0x0F) == MT_STOP)  && ((markNext->type & 0x0F) == MT_STOP)) || // two stop or start marks, keep strong marks, delete weak
                    (((mark->type & 0x0F) == MT_START) && ((markNext->type & 0x0F) == MT_START))) {
                dsyslog("cMarks::DelInvalidSequence(): mark (%d) type 0x%X, followed by same mark (%d) type 0x%X", mark->position, mark->type, markNext->position, markNext->type);

                // first mark is stronger or equal, delete second mark of pair, but never delete start mark
                if (((mark->position == first->position) || (mark->type >= markNext->type)) && (markNext->position != last->position)) {
                    dsyslog("cMarks::DelInvalidSequence() delete second mark (%d)", markNext->position);
                    Del(markNext);
                }
                // second mark is stronger, delete first mark of pair, but never delete end mark
                else if ((markNext->position == last->position) || (mark->type < markNext->type)) {
                    dsyslog(" cMarks::DelInvalidSequence() delete first mark (%d)", mark->position);
                    cMark *tmp = mark;
                    mark = markNext;
                    Del(tmp);
                    continue;
                }
            }
        }
        mark = mark->Next();
    }
}



void cMarks::Del(cMark *mark) {
    if (!mark) return;

    if (first == mark) {
        // we are the first mark
        first = mark->Next();
        if (first) {
            first->SetPrev(nullptr);
        }
        else {
            last = nullptr;
        }
    }
    else {
        if (mark->Next() && (mark->Prev())) {
            // there is a next and prev object
            mark->Prev()->SetNext(mark->Next());
            mark->Next()->SetPrev(mark->Prev());
        }
        else {
            // we are the last
            mark->Prev()->SetNext(nullptr);
            last=mark->Prev();
        }
    }
    FREE(sizeof(*mark), "mark");
    delete mark;
    count--;
}


cMark *cMarks::First() {
    return first;
}


cMark *cMarks::Get(const int position) {
    if (!first) return nullptr; // no elements yet

    cMark *mark = first;
    while (mark) {
        if (position == mark->position) break;
        mark = mark->Next();
    }
    return mark;
}


cMark *cMarks::GetAround(const int frames, const int position, const int type, const int mask) {
    cMark *m0 = Get(position);
    if (m0 && (m0->position == position) && ((m0->type & mask) == (type & mask))) return m0;

    cMark *m1 = GetPrev(position, type, mask);
    cMark *m2 = GetNext(position, type, mask);
    if (!m1 && !m2) return nullptr;

    if (!m1 && m2) {
        if (abs(position - m2->position) > frames) return nullptr;
        else return m2;
    }
    if (m1 && !m2) {
        if (abs(position - m1->position) > frames) return nullptr;
        return m1;
    }
    if (m1 && m2) {
        if (abs(m1->position - position) > abs(m2->position - position)) {
            if (abs(position - m2->position) > frames) return nullptr;
            else return m2;
        }
        else {
            if (abs(position - m1->position) > frames) return nullptr;
            return m1;
        }
    }
    else {
        dsyslog("cMarks::GetAround(): invalid marks found");
        return nullptr;
    }
}


cMark *cMarks::GetPrev(const int position, const int type, const int mask) {
    if (!first) return nullptr; // no elements yet

    // first advance
    cMark *mark = first;
    while (mark) {
        if (mark->position >= position) break;
        mark = mark->Next();
    }
    if (type == 0xFF) {
        if (mark) return mark->Prev();
        return last;
    }
    else {
        if (!mark) mark = last;
        else mark = mark->Prev();
        while (mark) {
            if ((mark->type & mask) == type) break;
            mark = mark->Prev();
        }
        return mark;
    }
}


cMark *cMarks::GetNext(const int position, const int type, const int mask) {
    if (!first) return nullptr; // no elements yet
    cMark *mark = first;
    while (mark) {
        if (type == 0xFF) {
            if (mark->position > position) break;
        }
        else {
            if ((mark->position > position) && ((mark->type & mask) == type)) break;
        }
        mark = mark->Next();
    }
    if (mark) return mark;
    return nullptr;
}


cMark *cMarks::Add(const int type, const int oldType, const int newType, const int position, const char *comment, const bool inBroadCast) {

    cMark *dupMark;
    if ((dupMark = Get(position))) {
        if ((type & 0xF0) != MT_SCENECHANGE) dsyslog("cMarks::Add(): duplicate mark on position (%d) type 0x%X and type 0x%X", position, type, dupMark->type); // duplicate scene changes happens frequently
#ifdef DEBUG_SCENECHANGE
        dsyslog("cMarks::Add(): duplicate mark on position (%d) type 0x%X and type 0x%X", position, type, dupMark->type); // duplicate scene changes happens frequently
#endif
        if (type == dupMark->type) return dupMark;      // same type at same position, ignore add
        if ((type & 0xF0) == (dupMark->type & 0xF0)) {  // start and stop mark of same type at same position, delete both
            Del(dupMark->position);
            return nullptr;
        }
        if (type > dupMark->type) {  // keep the stronger mark
            if (dupMark->comment && comment) {
                FREE(strlen(dupMark->comment)+1, "comment");
                free(dupMark->comment);
                dupMark->comment = strdup(comment);
                ALLOC(strlen(dupMark->comment)+1, "comment");
            }
            dupMark->type        = type;
            dupMark->oldType     = oldType;
            dupMark->newType     = newType;
            dupMark->inBroadCast = inBroadCast;
        }
        return dupMark;
    }

    cMark *newMark = new cMark(type, oldType, newType, position, comment, inBroadCast);
    if (!newMark) return nullptr;
    ALLOC(sizeof(*newMark), "mark");

    if (!first) {
        //first element
        first = last = newMark;
        count++;
        return newMark;
    }
    else {
        cMark *mark = first;
        while (mark) {
            if (!mark->Next()) {
                if (position > mark->position) {
                    // add as last element
                    newMark->Set(mark, nullptr);
                    mark->SetNext(newMark);
                    last = newMark;
                    break;
                }
                else {
                    // add before
                    if (!mark->Prev()) {
                        // add as first element
                        newMark->Set(nullptr, mark);
                        mark->SetPrev(newMark);
                        first = newMark;
                        break;
                    }
                    else {
                        newMark->Set(mark->Prev(), mark);
                        mark->SetPrev(newMark);
                        break;
                    }
                }
            }
            else {
                if ((position > mark->position) && (position < mark->Next()->position)) {
                    // add between two marks
                    newMark->Set(mark, mark->Next());
                    mark->SetNext(newMark);
                    newMark->Next()->SetPrev(newMark);
                    break;
                }
                else {
                    if ((position < mark->position) && (mark == first)) {
                        // add as first mark
                        first = newMark;
                        mark->SetPrev(newMark);
                        newMark->SetNext(mark);
                        break;
                    }
                }
            }
            mark = mark->Next();
        }
        if (!mark)return nullptr;
        count++;
        return newMark;
    }
    return nullptr;
}


// define text to mark type, used in marks file and log messages
// return pointer to text, calling function has to free memory
//
char *cMarks::TypeToText(const int type) {
    char *text = nullptr;
    switch (type & 0xFF0) {
    case MT_UNDEFINED:
        if (asprintf(&text, "undefined") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_ASSUMED:
        if (asprintf(&text, "assumed") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_SCENECHANGE:
        if (asprintf(&text, "scene") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_SOUNDCHANGE:
        if (asprintf(&text, "sound") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_LOWERBORDERCHANGE:
        if (asprintf(&text, "lower border") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_BLACKCHANGE:
        if (asprintf(&text, "black screen") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_LOGOCHANGE:
        if (asprintf(&text, "logo") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_CHANNELCHANGE:
        if (asprintf(&text, "channel") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_VBORDERCHANGE:
        if (asprintf(&text, "vertical border") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_HBORDERCHANGE:
        if (asprintf(&text, "horizontal border") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_ASPECTCHANGE:
        if (asprintf(&text, "aspect ratio") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_MOVED:
        if (asprintf(&text, "moved") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_TYPECHANGE:
        if (asprintf(&text, "type changed") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_VPSCHANGE:
        if (asprintf(&text, "VPS event") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_RECORDINGCHANGE:
        if (asprintf(&text, "recording") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_OVERLAPCHANGE:
        if (asprintf(&text, "overlap") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_INTRODUCTIONCHANGE:
        if (asprintf(&text, "introduction") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_CLOSINGCREDITSCHANGE:
        if (asprintf(&text, "closing credits") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    case MT_ADINFRAMECHANGE:
        if (asprintf(&text, "ad in frame") != -1) {
            ALLOC(strlen(text)+1, "text");
        }
        else esyslog("cMarks::TypeToText(): asprintf failed");
        break;
    default:
        // special type
        if (type == MT_ALL) {
            if (asprintf(&text, "all") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            else esyslog("cMarks::TypeToText(): asprintf failed");
        }
        else {
            esyslog("cMarks::TypeToText(): type 0x%X unknown", type);
            if (asprintf(&text, "unknown") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            else esyslog("cMarks::TypeToText(): asprintf failed");
        }
        break;
    }
    return text;
}


cMark *cMarks::ChangeType(cMark *mark, const int newType) {
    if (!mark) return nullptr;
    if ((newType != MT_START) && (newType != MT_STOP)) return nullptr;
    mark->type = MT_TYPECHANGE | newType;
    char *comment = nullptr;
    if (asprintf(&comment,"%s used as %s mark",mark->comment, (newType == MT_START)? "START" : "STOP") == -1) return nullptr;
    ALLOC(strlen(comment)+1, "comment");

    FREE(strlen(mark->comment)+1, "comment");
    free(mark->comment);
    mark->comment = comment;
    return mark;
}


// move mark to new posiition
// return pointer to new mark
//
cMark *cMarks::Move(cMark *mark, const int newPosition, const int newType) {
    if (!mark) return nullptr;
    // check if old and new type is valid
    if ((mark->type & 0x0F) != (newType & 0x0F)) {
        esyslog("cMarks::Move(): old mark (%d) type 0x%X and new mark (%d) type 0x%X is invalid", mark->position, mark->type, newPosition, newType);
        return nullptr;
    }
    // prevent move to position on with a mark exists with different base type (START/STOP)
    const cMark *checkPos = Get(newPosition);
    if (checkPos && ((checkPos->type & 0x0F) != (newType & 0x0F))) {  // will result in invalid sequence
        esyslog("cMarks::Move(): move failed, mark with different type exists on new position (%d) type 0x%X, new type 0x%X", newPosition, checkPos->type, newType);
        return nullptr;
    }
    // prevent invalid sequence after move
    const cMark *beforeNewPos = GetPrev(newPosition, MT_ALL);
    if (beforeNewPos && (beforeNewPos->position == mark->position)) beforeNewPos = GetPrev(mark->position, MT_ALL);
    if (beforeNewPos && ((beforeNewPos->type & 0x0F) == (newType & 0x0F))) {
        esyslog("cMarks::Move(): move to (%d) will result in invalid sequence, mark before (%d) has same type", newPosition, beforeNewPos->position);
        return nullptr;
    }
    const cMark *afterNewPos = GetNext(newPosition, MT_ALL);
    if (afterNewPos && (afterNewPos->position == mark->position)) afterNewPos = GetNext(mark->position, MT_ALL);
    if (afterNewPos && ((afterNewPos->type & 0x0F) == (newType & 0x0F))) {
        esyslog("cMarks::Move(): move to (%d) will result in invalid sequence, mark after (%d) has same type", newPosition, afterNewPos->position);
        return nullptr;
    }

    // delete old mark, add new mark
    char *comment = nullptr;
    const char *indexToHMSF = GetTime(mark);
    cMark *newMark = nullptr;
    char* typeText = TypeToText(newType);

    if (indexToHMSF && typeText) {
        char suffix[10] = {0};
        if (newPosition > mark->position)      strcpy(suffix,"after ");
        else if (newPosition < mark->position) strcpy(suffix,"before");
        if (asprintf(&comment,"%s %s -> %s %s (%d)", mark->comment, indexToHMSF, typeText, suffix, newPosition)) {
            ALLOC(strlen(comment)+1, "comment");
            dsyslog("cMarks::Move(): %s",comment);

            int oldType = MT_UNDEFINED;
            if (((mark->type & 0xF0) == MT_MOVED) && (mark->newType != MT_UNDEFINED)) oldType = mark->newType;
            else oldType = mark->type;
            int type = ((mark->type & 0x0F) == MT_START) ? MT_MOVEDSTART : MT_MOVEDSTOP;
            Del(mark);
            newMark = Add(type, oldType, newType, newPosition, comment);

            FREE(strlen(typeText)+1, "text");
            free(typeText);
            FREE(strlen(comment)+1, "comment");
            free(comment);
        }
    }
    return newMark;
}


char *cMarks::IndexToHMSF(const int frameNumber, const bool isVDR, int *offsetSeconds) {
    // offsetSeconds may be nullptr
    char *indexToHMSF = nullptr;
    double Seconds    = 0;
    int f             = 0;
    int time_ms       = -1;

    if (index) time_ms = index->GetTimeFromFrame(frameNumber, isVDR);
    else { // called by logo search, we have no index
        dsyslog("cMarks::IndexToHMSF(): no index available, use frame rate %d", frameRate);
        if (frameRate <= 0) {
            esyslog("cMarks::IndexToHMSF(): no frame rate set");
            return nullptr;
        }
        else time_ms = frameNumber * 1000 / frameRate;
    }

#ifdef DEBUG_SAVEMARKS
    dsyslog("cMarks::IndexToHMSF():      frame (%d), offset from start %d, isVDR %d", frameNumber, time_ms, isVDR);
#endif

    if (time_ms >= 0) f = int(modf(float(time_ms) / 1000, &Seconds) * 100);                 // convert ms to 1/100 s
    else {
        esyslog("cMarks::IndexToHMSF(): failed to get time from frame (%d)", frameNumber);
        return nullptr;
    }
    int s = int(Seconds);
    if (offsetSeconds) *offsetSeconds = s;
    int m = s / 60 % 60;
    int h = s / 3600;
    s %= 60;
    if (asprintf(&indexToHMSF, "%d:%02d:%02d.%02d", h, m, s, f) == -1) return nullptr;  // memory debug managed by calling function
    return indexToHMSF;
}


/**
 * get PTS based time offset of mark position
 * @param mark pointer to mark
 * @return char array of time stamp with format HH:MM:SS.FF
 */
char *cMarks::GetTime(cMark *mark) {
    if (!mark) return nullptr;
    char *timeChar = mark->GetTime();
    if (!timeChar) {
        int timeSec = -1;
        timeChar = IndexToHMSF(mark->position, false, &timeSec);
        if (timeChar) {
            ALLOC(strlen(timeChar)+1, "timeOffsetPTS");
        }
        mark->SetTime(timeChar, timeSec);
    }
    if (!timeChar) esyslog("cMarks::GetTime(): frame (%d): failed to get time from index", mark->position);
    return timeChar;
}


bool cMarks::Backup(const char *directory) {
    char *fpath = nullptr;
    if (asprintf(&fpath, "%s/%s", directory, filename) == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    // make backup of old marks, filename convention taken from noad
    char *bpath = nullptr;
    if (asprintf(&bpath, "%s/%s0", directory, filename) == -1) {
        FREE(strlen(fpath)+1, "fpath");
        free(fpath);
        return false;
    }
    ALLOC(strlen(bpath)+1, "bpath");

    int ret = rename(fpath,bpath);
    FREE(strlen(bpath)+1, "bpath");
    free(bpath);
    FREE(strlen(fpath)+1, "fpath");
    free(fpath);
    return (ret == 0);
}


int cMarks::Length() const {
    if (!first) {
        esyslog("cMarks::Length(): no marks found");
        return 0;
    }
    int length = 0;
    cMark *startMark = first;
    cMark *stopMark  = nullptr;
    while (startMark) {
        if ((startMark->type & 0x0F) == MT_START) {
            stopMark = startMark->Next();
            if ((stopMark && (stopMark->type & 0x0F) == MT_STOP)) {
                length += stopMark->position - startMark->position;
            }
            else {  // invalid sequence
                esyslog("cMarks::Length(): invalid mark type sequence, expect a stop mark");
                return 0;
            }
        }
        else {  // invalid sequence
            esyslog("cMarks::Length(): invalid mark type sequence, expect a start mark");
            return 0;
        }
        startMark = stopMark->Next();
    }
    return length;
}


bool cMarks::Save(const char *directory, const sMarkAdContext *maContext, const bool force) {
    if (!directory) return false;
    if (!maContext) return false;
    if (abortNow) return false;  // do not save marks if aborted

    if (!maContext->Info.isRunningRecording && !force) {
//        dsyslog("cMarks::Save(): save marks later, isRunningRecording=%d force=%d", maContext->Info.isRunningRecording, force);
        return false;
    }
    dsyslog("cMarks::Save(): save marks, isRunningRecording=%d force=%d", maContext->Info.isRunningRecording, force);

    char *fpath = nullptr;
    if (asprintf(&fpath, "%s/%s", directory, filename) == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    FILE *mf;
    mf = fopen(fpath,"w+");
    if (!mf) {
        FREE(strlen(fpath)+1, "fpath");
        free(fpath);
        return false;
    }
    cMark *mark = first;
    while (mark) {
        // for stop marks adjust timestamp from iFrame before to prevent short ad pictures, vdr cut only on iFrames
        int vdrMarkPosition = mark->position;
        if ((mark->type & 0x0F) == MT_STOP) {
            if (!index) {  // called by logo search, we have no index
                dsyslog("cMarks::Save(): no index available, use frame rate %d", frameRate);
                if (frameRate <= 0) {
                    esyslog("cMarks::Save(): no frame rate set");
                    return false;
                }
                else vdrMarkPosition = mark->position * 1000 / frameRate;
            }
            else vdrMarkPosition = index->GetIFrameBefore(mark->position);
        }

#ifdef DEBUG_SAVEMARKS
        dsyslog("-----------------------------------------------------------------------------");
        dsyslog("cMarks::Save(): mark frame number     (%d)", mark->position);
        dsyslog("cMarks::Save(): vdr mark frame number (%d)", vdrMarkPosition);
#endif
        const char *indexToHMSF_PTS = GetTime(mark);                    // PTS based timestamp
        if (!indexToHMSF_PTS) esyslog("cMarks::Save(): failed to get PTS timestamp for (%d)",  mark->position);

        char *indexToHMSF_VDR = IndexToHMSF(vdrMarkPosition, true);     // vdr based timestamp
        if (indexToHMSF_VDR) {
            ALLOC(strlen(indexToHMSF_VDR)+1, "indexToHMSF_VDR");
        }
        else esyslog("cMarks::Save(): failed to get VDR timestamp for (%d)", vdrMarkPosition);

#ifdef DEBUG_SAVEMARKS
        dsyslog("cMarks::Save(): mark frame number     (%d): indexToHMSF_PTS %s", mark->position,  indexToHMSF_PTS);
        dsyslog("cMarks::Save(): vdr mark frame number (%d): indexToHMSF_VDR %s", vdrMarkPosition, indexToHMSF_VDR);
#endif

        if (indexToHMSF_VDR && indexToHMSF_PTS) {
            if (maContext->Config->pts) fprintf(mf, "%s (%6d)%s %s <- %s\n", indexToHMSF_VDR, mark->position, ((mark->type & 0x0F) == MT_START) ? "*" : " ", indexToHMSF_PTS, mark->comment ? mark->comment : "");
            else fprintf(mf, "%s (%6d)%s %s\n", indexToHMSF_VDR, mark->position, ((mark->type & 0x0F) == MT_START) ? "*" : " ", mark->comment ? mark->comment : "");
        }
        if (indexToHMSF_VDR) {
            FREE(strlen(indexToHMSF_VDR)+1, "indexToHMSF_VDR");
            free(indexToHMSF_VDR);
        }
        mark = mark->Next();
    }
    fclose(mf);

    if (geteuid() == 0) {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath = nullptr;
        if (asprintf(&spath, "%s/00001.ts", directory) != -1) {
            ALLOC(strlen(spath)+1, "spath");
            struct stat statbuf;
            if (!stat(spath, &statbuf)) {
                if (chown(fpath, statbuf.st_uid, statbuf.st_gid)) {};
            }
            FREE(strlen(spath)+1, "spath");
            free(spath);
        }
    }
    FREE(strlen(fpath)+1, "fpath");
    free(fpath);
    return true;
}
