/*
 * debug.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __debug_h_
#define __debug_h_

#ifndef LOG_ERR
#define LOG_ERR 3
#endif

extern int SysLogLevel;
extern void syslog_with_tid(int priority, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

#define esyslog(a...) void( (SysLogLevel > 0) ? syslog_with_tid(LOG_ERR, a) : void() )
#define isyslog(a...) void( (SysLogLevel > 1) ? syslog_with_tid(LOG_ERR, a) : void() )
#define dsyslog(a...) void( (SysLogLevel > 2) ? syslog_with_tid(LOG_ERR, a) : void() )
#define tsyslog(a...) void( (SysLogLevel > 3) ? syslog_with_tid(LOG_ERR, a) : void() )

#endif
