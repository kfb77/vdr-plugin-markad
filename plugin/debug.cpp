/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <vdr/tools.h>
#include <stdarg.h>

#include "debug.h"

// 1 = error
// 2 = info
// 3 = debug  -> but log only if verbose plugin logging = ON
// 4 = trace
int logLevel = 3;

void DebugLog(const char *fmt, ...) {
    // Return immediately if log level is below 3 or format string is null
    if (logLevel < 3 || fmt == nullptr)
        return;

    va_list ap;
    va_start(ap, fmt);

    // Calculate required buffer size by first using vsnprintf with a small buffer
    int required_size = vsnprintf(nullptr, 0, fmt, ap) + 1;  // +1 for null terminator
    va_end(ap);  // Reset va_list before reusing

    // If vsnprintf failed to determine the size, return early
    if (required_size <= 0) {
        esyslog("markad: DebugLog(): error determining required buffer size");
        return;
    }

    // Dynamically allocate memory for the log message
    // We need space for "markad: " + formatted message + null terminator
    char* buffer = static_cast<char*>(malloc(required_size + 9)); // +9 for "markad: " and null terminator
    if (!buffer) {
        esyslog("markad: DebugLog(): memory allocation failed");
        return;
    }

    // Prepend "markad: " to the message
    snprintf(buffer, 9, "markad: ");  // "markad: " + null terminator

    // Format the original message after "markad: "
    va_start(ap, fmt);
    vsnprintf(buffer + 8, required_size, fmt, ap);  // Format the message starting after "markad: "
    va_end(ap);

    // Send the final message to VDR logging
    dsyslog("%s", buffer);

    // Free the dynamically allocated memory
    free(buffer);
}


#ifdef DEBUG_MEM
#include <stdlib.h>
#include <cstring>
#include <vector>
#include <pthread.h>
#include <vdr/plugin.h>

int memUseSum = 0;
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
        esyslog("markad: debugmem unmachted alloc %6d times %7d bytes, file %s, line %4d, variable: %s", memLine->count, memLine->size, memLine->file, memLine->line, memLine->var);
    }
    dsyslog("markad: debugmem unmachted alloc end ------------------------------------------------------------------");
    pthread_mutex_unlock(&mutex);
}


char *memListSVDR() {
    pthread_mutex_lock(&mutex);
    char *dump = nullptr;
    for (std::vector<memUse>::iterator memLine = memUseVector.begin(); memLine != memUseVector.end(); ++memLine) {
        if (memLine->count == 0) continue;
        char *line = nullptr;
        char *tmp = nullptr;
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
