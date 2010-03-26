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

#define tsyslog(fmt,...) void( (SysLogLevel > 3) ? syslog_with_tid(LOG_ERR, fmt, __VA_ARGS__) : void() )

#endif