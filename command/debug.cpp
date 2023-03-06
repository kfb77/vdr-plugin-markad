/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"


// save picture to recording directory
// return: true if successful
//
#ifdef DEBUG_MARK_OPTIMIZATION
#include <stdio.h>
#include <stdlib.h>
bool SavePicture(const char *fileName, uchar *picture, const int width, const int height) {
    if (!fileName) return false;
    if ((width == 0) || (height == 0)) {
        dsyslog("SavePicture: logo width or logo height not set");
        return false;
    }

    // Open file
    FILE *pFile = fopen(fileName, "wb");
    if (pFile == NULL) return false;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", width, height);

    // Write pixel data
    if (fwrite(picture, 1, width * height, pFile)) {};

    // Close file
    fclose(pFile);
    return true;
}
#endif


#ifdef DEBUG_MEM

#include <stdlib.h>
#include <cstring>
#include <vector>
#include <pthread.h>


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
    pthread_mutex_lock(&mutex);
    memUseSum += size;
    tsyslog("debugmem alloc %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
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
    pthread_mutex_lock(&mutex);
    memUseSum -= size;
    tsyslog("debugmem  free %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
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
    dsyslog("debugmem unmachted free %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
    pthread_mutex_unlock(&mutex);
    return;
}


void memList() {
    pthread_mutex_lock(&mutex);
    dsyslog("debugmem unmachted alloc start ----------------------------------------------------------------");
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count > 0) {
            dsyslog("debugmem unmachted alloc %6d times %7d bytes, file %s, line %4d, variable: %s", memLine->count, memLine->size, memLine->file, memLine->line, memLine->var);
        }
        free(memLine->file);
        free(memLine->var);
    }
    dsyslog("debugmem unmachted alloc end ------------------------------------------------------------------");
    memUseVector.clear();
    pthread_mutex_unlock(&mutex);
}
#endif
