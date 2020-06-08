/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#ifdef DEBUGMEM
#include <stdlib.h>
#include <cstring>
#include <vector>

#include "debug.h"
#include <vdr/plugin.h>

int memUseSum = 0;
struct memUse {
    int size = 0;
    int line = 0;
    char *file = NULL;
    char *var = NULL;
};
std::vector<memUse> memUseVector;


void memAlloc(int size, int line, char *file, char *var) {
    memUseSum += size;
    dsyslog("markad: debugmem alloc %5d bytes, sum %5d bytes, line %4d, file %s, variable: %s", size, memUseSum, line, file, var);
    memUseVector.push_back({size, line, strdup(file), strdup(var)});
    return;
}


void memFree(int size, int line, char *file, char *var) {
    memUseSum -= size;
    dsyslog("markad: debugmem  free %5d bytes, sum %5d bytes, line %4d, file %s, variable: %s", size, memUseSum, line, file, var);
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
       if ((memLine->size == size) && (strcmp(memLine->file, file) == 0) && (strcmp(memLine->var, var) == 0)) {  // try file match
           memUseVector.erase(memLine);
           return;
       }
       if ((memLine->size == size) && (strcmp(memLine->var, var) == 0)) {  // try all files
           memUseVector.erase(memLine);
           return;
       }
    }
    dsyslog("markad: debugmem unmachted  free %5d bytes, line %4d, file %s, variable: %s", size, line, file, var);
    return;
}


void memList() {
     dsyslog("markad: debugmem unmachted alloc start ----------------------------------------------------------------");
     for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
         dsyslog("markad: debugmem unmachted alloc %7d bytes, line %4d, file %s, variable: %s", memLine->size, memLine->line, memLine->file, memLine->var);
     }
     dsyslog("markad: debugmem unmachted alloc end ------------------------------------------------------------------");
}


char *memListSVDR() {
    char *dump = NULL;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        char *line = NULL;
        char *tmp = NULL;
        if (asprintf(&line, "markad: debugmem unmachted alloc %7d bytes, line %4d, file %s, variable: %s\n", memLine->size, memLine->line, memLine->file, memLine->var) != -1) {
            if (asprintf(&tmp, "%s%s", (dump) ? dump : "", line) != -1) {
                free(dump);
                free(line);
                dump = tmp;
            }
        }
     }
     return dump;
}


void memClear() {
    memUseVector.clear();
}


#endif
