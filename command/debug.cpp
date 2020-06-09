/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include <stdlib.h>
#include <cstring>
#include <vector>
#ifdef DEBUGMEM
extern "C"{
    #include "debug.h"
}


int memUseSum = 0;
struct memUse {
    int size = 0;
    int line = 0;
    char *file = NULL;
    char *var = NULL;
    int count = 0;
};
std::vector<memUse> memUseVector;


void memAlloc(int size, int line, char *file, char *var) {
    memUseSum += size;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->file, file) == 0) && (strcmp(memLine->var, var) == 0)) {
            memLine->count++;
            return;
        }
    }
    memUseVector.push_back({size, line, strdup(file), strdup(var), 1});
    return;
}


void memFree(int size, int line, char *file, char *var) {
    memUseSum -= size;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->file, file) == 0) && (strcmp(memLine->var, var) == 0)) {  // try file match
            if (memLine->count <= 0) break;
            memLine->count--;
            return;
        }
    }
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->var, var) == 0)) {  // try all files
            if (memLine->count <= 0) continue;
            memLine->count--;
            return;
        }
    }
    dsyslog("debugmem unmachted free %7d bytes, line %4d, file %s, variable: %s", size, line, file, var);
    return;
}


void memList() {
    dsyslog("debugmem unmachted alloc start ----------------------------------------------------------------");
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count > 0) {
            dsyslog("debugmem unmachted alloc %6d times %7d bytes, line %4d, file %s, variable: %s", memLine->count, memLine->size, memLine->line, memLine->file, memLine->var);
        }
        free(memLine->file);
        free(memLine->var);
    }
    dsyslog("debugmem unmachted alloc end ------------------------------------------------------------------");
    memUseVector.clear();
}
#endif
