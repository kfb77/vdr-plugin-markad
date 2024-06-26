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
#if defined(DEBUG_MARK_OPTIMIZATION) || defined(DEBUG_LOGO_RESIZE) || defined(DEBUG_LOGO_DETECT_FRAME_CORNER) || defined(DEBUG_LOGO_SAVE) || defined(DEBUG_FRAME_DETECTION)
#include <stdio.h>
#include <stdlib.h>
bool SaveSobel(const char *fileName, const uchar *picture, const int width, const int height) {
    if (!fileName) return false;
    if ((width == 0) || (height == 0)) {
        dsyslog("SaveSobel: logo width or logo height not set");
        return false;
    }

#ifdef DEBUG_SOBEL
    dsyslog("SaveSobel: logo size %dx%d", width, height);
#endif

    // Open file
    FILE *pFile = fopen(fileName, "wb");
    if (pFile == nullptr) return false;

    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", width, height);

    // Write pixel data
    if (fwrite(picture, 1, width * height, pFile)) {};

    // Close file
    fclose(pFile);
    return true;
}
#endif


// save currect content of the frame buffer (plane 0) to fileName
//
#if defined(DEBUG_MARK_FRAMES) || defined(DEBUG_LOGO_DETECT_FRAME_CORNER)
#include <stdio.h>
#include <stdlib.h>
void SaveFrameBuffer(const sMarkAdContext *maContext, const char *fileName) {
    if (!maContext->Video.Info.height) {
        dsyslog("SaveFrameBuffer(): maContext->Video.Info.height not set");
        return;
    }
    if (!maContext->Video.Info.width) {
        dsyslog("SaveFrameBuffer(): maContext->Video.Info.width not set");
        return;
    }
    if (!maContext->Video.Data.valid) {
        dsyslog("SaveFrameBuffer(): maContext->Video.Data.valid not set");
        return;
    }
    // Open file
    FILE *pFile = fopen(fileName, "wb");
    if (pFile == nullptr) {
        dsyslog("cMarkAdStandalone::SaveFrame(): open file %s failed", fileName);
        return;
    }
    // Write header
    fprintf(pFile, "P5\n%d %d\n255\n", maContext->Video.Info.width, maContext->Video.Info.height);
    // Write pixel data
    for (int line = 0; line < maContext->Video.Info.height; line++) {
        if (fwrite(&maContext->Video.Data.Plane[0][line * maContext->Video.Data.PlaneLinesize[0]], 1, maContext->Video.Info.width, pFile)) {};
    }
    // Close file
    fclose(pFile);
}
#endif
#ifdef DEBUG_MEM

#include <stdlib.h>
#include <cstring>
#include <vector>
#include <pthread.h>


long int memUseSum = 0;  // prevent int overflow
long int memUseMax = 0;
struct memUse {
    int size = 0;
    int line = 0;
    char *file = nullptr;
    char *var = nullptr;
    int count = 0;
};
std::vector<memUse> memUseVector;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void memAlloc(int size, int line, char *file, char *var) {
    pthread_mutex_lock(&mutex);
    memUseSum += size;
    if (memUseSum > memUseMax) memUseMax = memUseSum;
//    tsyslog("debugmem: alloc %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
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
//    tsyslog("debugmem:  free %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
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
    esyslog("debugmem: unmachted free %7d bytes, file %s, line %4d, variable: %s", size, file, line, var);
    pthread_mutex_unlock(&mutex);
    return;
}


void memList() {
    pthread_mutex_lock(&mutex);
    dsyslog("debugmem: unmachted alloc start ----------------------------------------------------------------");
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count > 0) {
            esyslog("debugmem unmachted alloc %6d times %7d bytes, file %s, line %4d, variable: %s", memLine->count, memLine->size, memLine->file, memLine->line, memLine->var);
        }
        free(memLine->file);
        free(memLine->var);
    }
    dsyslog("debugmem: unmachted alloc end ------------------------------------------------------------------");
    memUseVector.clear();

    dsyslog("debugmem: maximal heap memory usage: %ld B -> %ld MB", memUseMax, memUseMax / 1024 / 1024);
    pthread_mutex_unlock(&mutex);
}
#endif
