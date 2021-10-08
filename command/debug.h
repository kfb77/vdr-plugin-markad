/*
 * debug.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <syslog.h>

#ifndef __debug_h_
#define __debug_h_

#define LOG_TRACE 8


// write picture from full recording recording and logo corner to /tmp from <framenumber> - 200 to <framenumber> + 200
// #define DEBUG_LOGO_DETECT_FRAME_CORNER <framenumber>

// write logo detection informations in log file
// #define DEBUG_LOGO_DETECTION

// debug overlap detection
// #define DEBUG_OVERLAP

// write overlap frame pairs
// #define DEBUG_OVERLAP_FRAME_RANGE 2
// #define DEBUG_OVERLAP_FRAME_BEFORE <framenumber>
// #define DEBUG_OVERLAP_FRAME_AFTER  <framenumber>

// debug logo detection of logo.cpp
// #define DEBUG_LOGO_CORNER TOP_LEFT
// #define DEBUG_LOGO_CORNER TOP_RIGHT
// #define DEBUG_LOGO_CORNER BOTTOM_LEFT
// #define DEBUG_LOGO_CORNER BOTTOM_RIGHT

// save logos from search logo function
// #define DEBUG_LOGO_SAVE 0   // save all logos before CheckValid to /tmp
// #define DEBUG_LOGO_SAVE 1   // save valid logos after CheckValid and before RemovePixelDefects to /tmp
// #define DEBUG_LOGO_SAVE 2   // save valid logos after RemovePixelDefects /tmp

// debug logo resize function
// #define DEBUG_LOGO_RESIZE

// debug compare frame range
// #define DEBUG_COMPARE_FRAME_RANGE TOP_LEFT
// #define DEBUG_COMPARE_FRAME_RANGE TOP_RIGHT
// #define DEBUG_COMPARE_FRAME_RANGE BOTTOM_LEFT
// #define DEBUG_COMPARE_FRAME_RANGE BOTTOM_RIGHT

// debug vborder detection
// #define DEBUG_VBORDER

// debug hborder detection
// #define DEBUG_HBORDER

// debug blackscreen detection
// #define DEBUG_BLACKSCREEN

// debug silence detection
// #define DEBUG_SILENCE

// debug marks frames, write mark frame picture (and some before and after) to recording directory
// #define DEBUG_MARK_FRAMES <count frames before and after>
// #define DEBUG_MARK_FRAMES 2

// debug index build
// #define DEBUG_INDEX_BUILD

// debug PTS ring buffer
// #define DEBUG_PTS

// debug video cut
// #define DEBUG_CUT <streamindex>
// #define DEBUG_CUT 0

// debug encoder
// #define DEBUG_ENCODER

extern int SysLogLevel;
extern void syslog_with_tid(int priority, const char *format, ...) __attribute__ ((format (printf, 2, 3)));


#define esyslog(a...) void( (SysLogLevel > 0) ? syslog_with_tid(LOG_ERR, a) : void() )
#define isyslog(a...) void( (SysLogLevel > 1) ? syslog_with_tid(LOG_INFO, a) : void() )
#define dsyslog(a...) void( (SysLogLevel > 2) ? syslog_with_tid(LOG_DEBUG, a) : void() )
#define tsyslog(a...) void( (SysLogLevel > 3) ? syslog_with_tid(LOG_TRACE, a) : void() )


#ifdef DEBUG_MEM
    #define ALLOC(size, var) memAlloc(size, __LINE__, (char *) __FILE__, (char *) var)
    #define FREE(size, var) memFree(size, __LINE__, (char *) __FILE__, (char *) var)
    void memAlloc(int size, int line, char *file, char *var);
    void memFree(int size, int line, char *file, char *var);
    void memList();
#else
    #define ALLOC(size, var)
    #define FREE(size, var)
#endif
#endif
