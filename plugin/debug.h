/*
 * debug.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __debug_h_
#define __debug_h_

// debug pause und resume of markad process
// #define DEBUG_PAUSE_CONTINUE

#ifdef DEBUG_MEM
    #include <vdr/plugin.h>

    #define ALLOC(size, var) memAlloc(size, __LINE__, (char *) __FILE__, (char *) var)
    #define FREE(size, var) memFree(size, __LINE__, (char *) __FILE__, (char *) var)


    void memAlloc(int size, int line, char *file, char *var);
    void memFree(int size, int line, char *file, char *var);
    void memList();
    char *memListSVDR();
    void memClear();

#else
    #define ALLOC(size, var)
    #define FREE(size, var)
#endif
#endif
