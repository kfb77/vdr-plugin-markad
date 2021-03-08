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

#include "marks.h"
extern "C" {
    #include "debug.h"
}


clMark::clMark(const int Type, const int Position, const char *Comment, const bool InBroadCast) {
    type = Type;
    position = Position;
    inBroadCast = InBroadCast;
    if (Comment) {
        comment = strdup(Comment);
        ALLOC(strlen(comment)+1, "comment");
    }
    else comment = NULL;

    prev = NULL;
    next = NULL;
}


clMark::~clMark() {
    if (comment) {
        FREE(strlen(comment)+1, "comment");
        free(comment);
    }
}


clMarks::clMarks() {
    strcpy(filename, "marks");
    first = last = NULL;
    savedcount = 0;
    count = 0;
    indexfd = -1;
}


clMarks::~clMarks() {
    DelAll();
    if (indexfd != -1) close(indexfd);
}


int clMarks::Count(const int Type, const int Mask) {
    if (Type == 0xFF) return count;
    if (!first) return 0;

    int ret = 0;
    clMark *mark = first;
    while (mark) {
        if ((mark->type & Mask) == Type) ret++;
        mark = mark->Next();
    }
    return ret;
}


void clMarks::Del(const int Position) {
    if (!first) return; // no elements yet

    clMark *next, *mark = first;
    while (mark) {
        next = mark->Next();
        if (mark->position == Position) {
            Del(mark);
            return;
        }
        mark = next;
    }
}


void clMarks::Del(const unsigned char Type) {
    if (!first) return; // no elements yet

    clMark *next, *mark = first;
    while (mark) {
        next = mark->Next();
        if (mark->type == Type) Del(mark);
        mark = next;
    }
}


void clMarks::DelWeakFromTo(const int from, const int to, const short int type) {
    clMark *mark = first;
    while (mark) {
        if (mark->position >= to) return;
        if ((mark->position > from) && (mark->type < (type & 0xF0))) {
            clMark *tmpMark = mark->Next();
            Del(mark);
            mark = tmpMark;
        }
        else mark = mark->Next();
    }
}


// <FromStart> = true: delete all marks from start to <Position>
// <FromStart> = false: delete all marks from <Position> to end
// blackscreen marks were moved to blackscreen list
//
void clMarks::DelTill(const int Position, clMarks *blackMarks, const bool FromStart) {
    clMark *next, *mark = first;
    if (!FromStart) {
        while (mark) {
            if (mark->position > Position) break;
            mark = mark->Next();
        }
    }
    while (mark) {
        next = mark->Next();
        if (FromStart) {
            if (mark->position < Position) {
                if ((mark->type & 0xF0) == MT_BLACKCHANGE) blackMarks->Add(mark->type, mark->position, NULL, mark->inBroadCast); // add mark to blackscreen list
                Del(mark);
            }
        }
        else {
            if ((mark->type & 0xF0) == MT_BLACKCHANGE) blackMarks->Add(mark->type, mark->position, NULL, mark->inBroadCast); // add mark to blackscreen list
            Del(mark);
        }
        mark = next;
    }
}


void clMarks::DelFrom(const int Position) {
    clMark *mark = first;
    while (mark) {
        if (mark->position > Position) break;
        mark = mark->Next();
    }

    while (mark) {
        clMark * next = mark->Next();
        Del(mark);
        mark = next;
    }
}


void clMarks::DelAll() {
    clMark *next, *mark = first;
    while (mark) {
        next = mark->Next();
        Del(mark);
        mark=next;
    }
    first = NULL;
    last = NULL;
}


void clMarks::Del(clMark *Mark) {
    if (!Mark) return;

    if (first == Mark) {
        // we are the first mark
        first = Mark->Next();
        if (first) {
            first->SetPrev(NULL);
        }
        else {
            last = NULL;
        }
    }
    else {
        if (Mark->Next() && (Mark->Prev())) {
            // there is a next and prev object
            Mark->Prev()->SetNext(Mark->Next());
            Mark->Next()->SetPrev(Mark->Prev());
        }
        else {
            // we are the last
            Mark->Prev()->SetNext(NULL);
            last=Mark->Prev();
        }
    }
    FREE(sizeof(*Mark), "mark");
    delete Mark;
    count--;
}


clMark *clMarks::Get(const int Position) {
    if (!first) return NULL; // no elements yet

    clMark *mark = first;
    while (mark) {
        if (Position == mark->position) break;
        mark = mark->Next();
    }
    return mark;
}


clMark *clMarks::GetAround(const int Frames, const int Position, const int Type, const int Mask) {
    clMark *m0 = Get(Position);
    if (m0 && (m0->position == Position) && ((m0->type & Mask) == (Type & Mask))) return m0;

    clMark *m1 = GetPrev(Position, Type, Mask);
    clMark *m2 = GetNext(Position, Type, Mask);
    if (!m1 && !m2) return NULL;

    if (!m1 && m2) {
        if (abs(Position - m2->position) > Frames) return NULL;
        else return m2;
    }
    if (m1 && !m2) {
        if (abs(Position - m1->position) > Frames) return NULL;
        return m1;
    }
    if (m1 && m2) {
        if (abs(m1->position - Position) > abs(m2->position - Position)) {
            if (abs(Position - m2->position) > Frames) return NULL;
            else return m2;
        }
        else {
            if (abs(Position - m1->position) > Frames) return NULL;
            return m1;
        }
    }
    else {
        dsyslog("clMarks::GetAround(): invalid marks found");
        return NULL;
    }
}


clMark *clMarks::GetPrev(const int Position, const int Type, const int Mask) {
    if (!first) return NULL; // no elements yet

    // first advance
    clMark *mark = first;
    while (mark) {
        if (mark->position >= Position) break;
        mark = mark->Next();
    }
    if (Type == 0xFF) {
        if (mark) return mark->Prev();
        return last;
    }
    else {
        if (!mark) mark = last;
        else mark = mark->Prev();
        while (mark) {
            if ((mark->type & Mask) == Type) break;
            mark = mark->Prev();
        }
        return mark;
    }
}


clMark *clMarks::GetNext(const int Position, const int Type, const int Mask) {
    if (!first) return NULL; // no elements yet
    clMark *mark = first;
    while (mark) {
        if (Type == 0xFF) {
            if (mark->position > Position) break;
        }
        else {
            if ((mark->position > Position) && ((mark->type & Mask) == Type)) break;
        }
        mark = mark->Next();
    }
    if (mark) return mark;
    return NULL;
}


clMark *clMarks::Add(const int Type, const int Position, const char *Comment, const bool inBroadCast) {
    clMark *newmark;

    if ((newmark = Get(Position))) {
        dsyslog("duplicate mark on position %i type 0x%X and type 0x%x", Position, Type, newmark->type);
        if ((Type & 0xF0) == (newmark->type & 0xF0)) {  // start and stop mark of same type at same position, delete both
            Del(newmark->position);
            return NULL;
        }
        if (Type > newmark->type){   // keep the stronger mark
            if (newmark->comment && Comment) {
                FREE(strlen(newmark->comment)+1, "comment");
                free(newmark->comment);
                newmark->comment=strdup(Comment);
                ALLOC(strlen(newmark->comment)+1, "comment");
            }
            newmark->type = Type;
            newmark->inBroadCast = inBroadCast;
        }
        return newmark;
    }

    newmark = new clMark(Type, Position, Comment, inBroadCast);
    if (!newmark) return NULL;
    ALLOC(sizeof(*newmark), "mark");

    if (!first) {
        //first element
        first = last = newmark;
        count++;
        return newmark;
    }
    else {
        clMark *mark = first;
        while (mark) {
            if (!mark->Next()) {
                if (Position > mark->position) {
                    // add as last element
                    newmark->Set(mark, NULL);
                    mark->SetNext(newmark);
                    last = newmark;
                    break;
                }
                else {
                    // add before
                    if (!mark->Prev()) {
                        // add as first element
                        newmark->Set(NULL, mark);
                        mark->SetPrev(newmark);
                        first = newmark;
                        break;
                    }
                    else {
                        newmark->Set(mark->Prev(), mark);
                        mark->SetPrev(newmark);
                        break;
                    }
                }
            }
            else {
                if ((Position > mark->position) && (Position < mark->Next()->position)) {
                    // add between two marks
                    newmark->Set(mark, mark->Next());
                    mark->SetNext(newmark);
                    newmark->Next()->SetPrev(newmark);
                    break;
                }
                else {
                    if ((Position < mark->position) && (mark == first)) {
                        // add as first mark
                        first = newmark;
                        mark->SetPrev(newmark);
                        newmark->SetNext(mark);
                        break;
                    }
                }
            }
            mark = mark->Next();
        }
        if (!mark)return NULL;
        count++;
        return newmark;
    }
    return NULL;
}


// define text to mark type, used in marks file
// return pointer to text
//
char *clMarks::TypeToText(const int type) {
    char *text = NULL;
    switch (type & 0xF0) {
        case MT_LOGOCHANGE:
            if (asprintf(&text, "logo") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        case MT_CHANNELCHANGE:
            if (asprintf(&text, "channel") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        case MT_VBORDERCHANGE:
            if (asprintf(&text, "vertical border") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        case MT_HBORDERCHANGE:
            if (asprintf(&text, "horizontal border") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        case MT_ASPECTCHANGE:
            if (asprintf(&text, "aspectratio") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        case MT_MOVEDCHANGE:
            if (asprintf(&text, "moved") != -1) {
                ALLOC(strlen(text)+1, "text");
            }
            break;
        default:
           if (asprintf(&text, "unknown") != -1) {
               ALLOC(strlen(text)+1, "text");
           }
           break;
    }
    return text;
}


// move mark to new posiition
// return pointer to new mark
//
clMark *clMarks::Move(MarkAdContext *maContext, clMark *mark, const int newPosition, const char* reason) {
    if (!mark) return NULL;
    if (!reason) return NULL;

    char *comment = NULL;
    char *indexToHMSF = IndexToHMSF(mark->position, maContext);
    clMark *newMark = NULL;
    char* typeText = TypeToText(mark->type);

    if (indexToHMSF && typeText) {
       if (asprintf(&comment,"moved %s mark (%d) %s %s %s mark (%d) at %s, %s detected%s",
                                    ((mark->type & 0x0F) == MT_START) ? "start" : "stop",
                                             newPosition,
                                                  (newPosition > mark->position) ? "after" : "before",
                                                     typeText,
                                                        ((mark->type & 0x0F) == MT_START) ? "start" : "stop",
                                                                 mark->position,
                                                                       indexToHMSF,
                                                                           reason,
                                                                                      ((mark->type & 0x0F) == MT_START) ? "*" : "") != -1) {
           ALLOC(strlen(comment)+1, "comment");
           isyslog("%s",comment);

           int newType = ((mark->type & 0x0F) == MT_START) ? MT_MOVEDSTART : MT_MOVEDSTOP;
           Del(mark);
           newMark = Add(newType, newPosition, comment);

           FREE(strlen(typeText)+1, "text");
           free(typeText);
           FREE(strlen(comment)+1, "comment");
           free(comment);
           FREE(strlen(indexToHMSF)+1, "indexToHMSF");
           free(indexToHMSF);
       }
   }
   return newMark;
}


void clMarks::RegisterIndex(cIndex *recordingIndex) {
    recordingIndexMarks = recordingIndex;
}


char *clMarks::IndexToHMSF(const int Index, const MarkAdContext *maContext) {
    double FramesPerSecond = maContext->Video.Info.FramesPerSecond;
    if (FramesPerSecond == 0.0) return NULL;
    char *indexToHMSF = NULL;
    double Seconds;
    int f = 0;
    if (recordingIndexMarks && ((maContext->Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H264) || (maContext->Info.VPid.Type == MARKAD_PIDTYPE_VIDEO_H265))) {
        int64_t pts_time_ms = recordingIndexMarks->GetTimeFromFrame(Index);
        if (pts_time_ms >= 0) {
            f = int(modf(float(pts_time_ms) / 1000, &Seconds) * 100); // convert ms to 1/100 s
        }
        else {
            dsyslog("clMarks::IndexToHMSF(): failed to get time from frame (%d)", Index);
        }
    }
    else {
        f = int(modf((Index + 0.5) / FramesPerSecond, &Seconds) * FramesPerSecond + 1);
    }
    int s = int(Seconds);
    int m = s / 60 % 60;
    int h = s / 3600;
    s %= 60;
    if (asprintf(&indexToHMSF, "%d:%02d:%02d.%02d", h, m, s, f) == -1) return NULL;   // this has to be freed in the calling function
    ALLOC(strlen(indexToHMSF)+1, "indexToHMSF");
    return indexToHMSF;
}


bool clMarks::Backup(const char *Directory, const bool isTS) {
    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s%s", Directory, filename, isTS ? "" : ".vdr") == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    // make backup of old marks, filename convention taken from noad
    char *bpath = NULL;
    if (asprintf(&bpath, "%s/%s0%s", Directory, filename, isTS ? "" : ".vdr") == -1) {
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


int clMarks::LoadVPS(const char *Directory, const char *type) {
    if (!Directory) return -1;
    if (!type) return -1;

    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s", Directory, "markad.vps") == -1) return -1;
    FILE *mf;
    mf = fopen(fpath, "r+");
    if (!mf) {
        dsyslog("clMarks::LoadVPS(): %s not found", fpath);
        free(fpath);
        return -1;
    }
    free(fpath);

    char *line = NULL;
    size_t length;
    char typeVPS[15] = "";
    char timeVPS[20] = "";
    int offsetVPS = 0;
    while (getline(&line, &length,mf) != -1) {
        sscanf(line, "%15s %20s %d", (char *) &typeVPS, (char *)&timeVPS, &offsetVPS);
        if (strcmp(type, typeVPS) == 0) break;
        offsetVPS = -1;
    }
    if (line) free(line);
    fclose(mf);
    return offsetVPS;
}


bool clMarks::Load(const char *Directory, const double FrameRate, const bool isTS) {
    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s%s", Directory, filename, isTS ? "" : ".vdr") == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    FILE *mf;
    mf = fopen(fpath, "r+");
    FREE(strlen(fpath)+1, "fpath");
    free(fpath);
    if (!mf) return false;

    char *line = NULL;
    size_t length;
    int h, m, s, f;

    while (getline(&line, &length, mf) != -1) {
        char descr[256] = "";
        f = 1;
        int n = sscanf(line, "%3d:%02d:%02d.%02d %80c", &h, &m, &s, &f, (char *) &descr);
        if (n == 1) {
            Add(0, h);
        }
        if (n >= 3) {
            int pos = int(round((h * 3600 + m * 60 + s) * FrameRate)) + f - 1;
            if (n <= 4) {
                Add(0, pos);
            }
            else {
                char *lf = strchr(descr, 10);
                if (lf) *lf = 0;
                char *cr = strchr(descr, 13);
                if (cr) *cr = 0;
                Add(0, pos, descr);
            }
        }
    }
    if (line) {
        FREE(strlen(line)+1, "line");
        free(line);
    }
    fclose(mf);
    return true;
}


bool clMarks::Save(const char *Directory, const MarkAdContext *maContext, const bool isTS, const bool force) {
    if (!Directory) return false;
    if (!maContext) return false;
    if (!first) return false;  // no marks to save
    if (!maContext->Info.isRunningRecording && !force) {
//        dsyslog("clMarks::Save(): save marks later, isRunningRecording=%d force=%d", maContext->Info.isRunningRecording, force);
        return false;
    }
    dsyslog("clMarks::Save(): save marks, isRunningRecording=%d force=%d", maContext->Info.isRunningRecording, force);

    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s%s", Directory, filename, isTS ? "" : ".vdr") == -1) return false;
    ALLOC(strlen(fpath)+1, "fpath");

    FILE *mf;
    mf = fopen(fpath,"w+");

    if (!mf) {
        FREE(strlen(fpath)+1, "fpath");
        free(fpath);
        return false;
    }

    clMark *mark = first;
    while (mark) {
        if (((mark->type & 0xF0) == MT_BLACKCHANGE) && (mark->position != first->position) && (mark->position != last->position)) { // do not save blackscreen marks expect start and end mark
            mark = mark->Next();
            continue;
        }
        char *indexToHMSF = IndexToHMSF(mark->position, maContext);
        if (indexToHMSF) {
            fprintf(mf, "%s %s\n", indexToHMSF, mark->comment ? mark->comment : "");
            FREE(strlen(indexToHMSF)+1, "indexToHMSF");
            free(indexToHMSF);
        }
        mark = mark->Next();
    }
    fclose(mf);

    if (getuid() == 0 || geteuid() != 0) {
        // if we are root, set fileowner to owner of 001.vdr/00001.ts file
        char *spath = NULL;
        if (asprintf(&spath, "%s/%s", Directory, isTS ? "00001.ts" : "001.vdr") != -1) {
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
