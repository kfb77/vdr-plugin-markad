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

extern int SysLogLevel;
extern void syslog_with_tid(int priority, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

#define esyslog(a...) void( (SysLogLevel > 0) ? syslog_with_tid(LOG_ERR, a) : void() )
#define isyslog(a...) void( (SysLogLevel > 1) ? syslog_with_tid(LOG_INFO, a) : void() )
#define dsyslog(a...) void( (SysLogLevel > 2) ? syslog_with_tid(LOG_DEBUG, a) : void() )
#define tsyslog(a...) void( (SysLogLevel > 3) ? syslog_with_tid(LOG_TRACE, a) : void() )


#ifdef DEBUGMEM
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
