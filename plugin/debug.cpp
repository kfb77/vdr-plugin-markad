/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#ifdef DEBUG_MEM
#include <stdlib.h>
#include <cstring>
#include <vector>
#include <pthread.h>

#include "debug.h"
#include <vdr/plugin.h>


int memUseSum = 0;
struct memUse {
    int size = 0;
    int line = 0;
    char *file = NULL;
    char *var = NULL;
    int count = 0;
};
std::vector<memUse> memUseVector;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void memAlloc(int size, int line, char *file, char *var) {
    if (!file) return;
    if (!var) return;
    pthread_mutex_lock(&mutex);
    memUseSum += size;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->file, file) == 0) && (strcmp(memLine->var, var) == 0)) {
            memLine->count++;
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    memUseVector.push_back({size, line, strdup(file), strdup(var), 1});
    pthread_mutex_unlock(&mutex);
    return;
}


void memFree(int size, int line, char *file, char *var) {
    if (!file) return;
    if (!var) return;
    pthread_mutex_lock(&mutex);
    memUseSum -= size;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->file, file) == 0) && (strcmp(memLine->var, var) == 0)) {  // try file match
            if (memLine->count <= 0) break;
            memLine->count--;
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if ((memLine->size == size) && (strcmp(memLine->var, var) == 0)) {  // try all files
            if (memLine->count <= 0) continue;
            memLine->count--;
            pthread_mutex_unlock(&mutex);
            return;
        }
    }
    dsyslog("markad: debugmem unmachted free %5d bytes, file %s, line %4d, variable: %s", size, file, line, var);
    pthread_mutex_unlock(&mutex);
    return;
}


void memList() {
    pthread_mutex_lock(&mutex);
    dsyslog("markad: debugmem unmachted alloc start ----------------------------------------------------------------");
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count == 0) continue;
        dsyslog("markad: debugmem unmachted alloc %6d times %7d bytes, file %s, line %4d, variable: %s", memLine->count, memLine->size, memLine->file, memLine->line, memLine->var);
    }
    dsyslog("markad: debugmem unmachted alloc end ------------------------------------------------------------------");
    pthread_mutex_unlock(&mutex);
}


char *memListSVDR() {
    pthread_mutex_lock(&mutex);
    char *dump = NULL;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count == 0) continue;
        char *line = NULL;
        char *tmp = NULL;
        if (asprintf(&line, "markad: unmachted alloc %3d times %7d bytes, file %s, line %4d, variable: %s\n", memLine->count, memLine->size, memLine->file, memLine->line, memLine->var) != -1) {
            if (asprintf(&tmp, "%s%s", (dump) ? dump : "", line) != -1) {
                free(dump);
                free(line);
                dump = tmp;
            }
        }
    }
    pthread_mutex_unlock(&mutex);
    return dump;
}


void memClear() {
    pthread_mutex_lock(&mutex);
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        free(memLine->file);
        free(memLine->var);
    }
    memUseVector.clear();
    pthread_mutex_unlock(&mutex);
}
#endif
