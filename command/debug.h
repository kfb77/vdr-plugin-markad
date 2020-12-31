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


// write picture from recording to /tmp from <framenumber>-200 to <framenumber>+200
// #define DEBUG_FRAME <framenumber>

// write picture from recording of logo corner to /tmp from <framenumber>-200 to <framenumber>+200
// #define DEBUG_FRAME_CORNER <framenumber>

// write logo detection informations in log file
// #define DEBUG_LOGO_DETECTION

// debug overlap detection
// #define DEBUG_OVERLAP

// debug logo detection
// #define DEBUG_LOGO_CORNER TOP_LEFT
// #define DEBUG_LOGO_CORNER TOP_RIGHT
// #define DEBUG_LOGO_CORNER BOTTOM_LEFT
// #define DEBUG_LOGO_CORNER BOTTOM_RIGHT

// save logos from search logo function
// #define DEBUG_LOGO_SAVE 0   // save all logos before CheckValid to /tmp
// #define DEBUG_LOGO_SAVE 1   // save valid logos after CheckValid and before RemovePixelDefects to /tmp
// #define DEBUG_LOGO_SAVE 2   // save valid logos after RemovePixelDefects /tmp

// debug temporary logo change detection
// #define DEBUG_LOGO_CHANGE

// debug vborder detection
// #define DEBUG_VBORDER

// debug hborder detection
// #define DEBUG_HBORDER

// debug silence detection
// #define DEBUG_SILENCE

// debug marks frames, write mark frame picture (and some before and after) to recording directory
// #define DEBUG_MARK_FRAMES <count frames before and after>

// debug index build
// #define DEBUG_INDEX_BUILD


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
