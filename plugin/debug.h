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

// debug vdr locks
// #define DEBUG_LOCKS

// debug all VPS EIT events with runningStatus > 0 from <channel>
// #define DEBUG_VPS_EIT "<channel>"

// debug all VPS VDR events with runningStatus > 0 from <channel>
// #define DEBUG_VPS_VDR "<channel>"


#ifdef DEBUG_MEM
    #include <vdr/plugin.h>

    #define ALLOC(size, var) memAlloc(size, __LINE__, const_cast<char *>(__FILE__), const_cast<char *>(var))
    #define FREE(size, var) memFree(size, __LINE__, const_cast<char *>(__FILE__),const_cast<char *>(var))


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
