/*
 * debug.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __debug_h_
#define __debug_h_

#include "global.h"

#ifdef POSIX
#include <syslog.h> // only part of posix systems.
#endif

#define LOG_TRACE 8


// write full picture from recording
// from <framenumber> - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE to <framenumber> + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE
// #define DEBUG_PICTURE <framenumber>
// #define DEBUG_PICTURE_RANGE <count frames>

// write full picture from recording and all sobel pictures from logo corner to recording directory
// from <framenumber> - DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE to <framenumber> + DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE
// #define DEBUG_LOGO_DETECT_FRAME_CORNER <framenumber>
// #define DEBUG_LOGO_DETECT_FRAME_CORNER_RANGE <count frames>

// write logo detection information in log file
// #define DEBUG_LOGO_DETECTION

// debug sobel transformation
// #define DEBUG_SOBEL

// debug overlap detection
// #define DEBUG_OVERLAP

// write overlap frame pairs
// #define DEBUG_OVERLAP_FRAME_RANGE 2
// #define DEBUG_OVERLAP_FRAME_BEFORE <framenumber>
// #define DEBUG_OVERLAP_FRAME_AFTER  <framenumber>

// debug logo extraction of logo.cpp
// #define DEBUG_LOGO_CORNER TOP_LEFT
// #define DEBUG_LOGO_CORNER TOP_RIGHT
// #define DEBUG_LOGO_CORNER BOTTOM_LEFT
// #define DEBUG_LOGO_CORNER BOTTOM_RIGHT

// save logos from search logo function (only works if corner is defined by DEBUG_LOGO_CORNER
// #define DEBUG_LOGO_SAVE 0   // save all logos before CheckValid to recording directory
// #define DEBUG_LOGO_SAVE 1   // save valid logos after before and after RemovePixelDefects to recording directory

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

// show all weak marks
// #define DEBUG_WEAK_MARKS

// debug scene change detection
// #define DEBUG_SCENE_CHANGE

// debug blackscreen and lower border detection
// #define DEBUG_BLACKSCREEN

// debug volume detection
// #define DEBUG_VOLUME

// debug marks frames, write mark frame picture (and some before and after) to recording directory
// #define DEBUG_MARK_FRAMES <count frames before and after>
// #define DEBUG_MARK_FRAMES 2

// debug framenumber pts index build
// #define DEBUG_INDEX

// debug input packet PTS
// #define DEBUG_PACKET_PTS

// debug PTS ring buffer build
// #define DEBUG_RING_PTS_ADD

// debug PTS ring buffer PTS lookup
// #define DEBUG_RING_PTS_LOOKUP

// debug video cut position, write <count> frames after start mark to recording directory
// #define DEBUG_CUT 5

// debug cut offset
// #define DEBUG_CUT_OFFSET

// debug cut write
// #define DEBUG_CUT_WRITE <stream number>  (-1 for all streams)
// #define DEBUG_CUT_WRITE 0

// debug PTS and DTS of video cut
// #define DEBUG_PTS_DTS_CUT <stream number>  (-1 for all streams)
// #define DEBUG_PTS_DTS_CUT 0

// debug decoder send/receive
// #define DEBUG_DECODER <stream index>
// #define DEBUG_DECODER 0

// debug DecodeNextFrame()
// #define DEBUG_DECODE_NEXT_FRAME

// debug seek from decoder
// #define DEBUG_DECODER_SEEK

// debug encoder
// #define DEBUG_ENCODER

// debug mark optimization
// #define DEBUG_MARK_OPTIMIZATION

// debug info logo detection
// #define DEBUG_INFOLOGO

// debug ad in frame detection
// #define DEBUG_ADINFRAME

// debug introduction logo detection
// #define DEBUG_INTRODUCTION

// debug closing credits detection
// #define DEBUG_CLOSINGCREDITS

// debug frame detection for closing credits or ad in frame
// #define DEBUG_FRAME_DETECTION

// write sobel transformed pictures from frame detection to recording directory
// writes 10 picture before to 10 picture after frame number
// #define DEBUG_FRAME_DETECTION_PICTURE <frame number>

// debug timestamps from final marks to save
// #define DEBUG_SAVEMARKS

// debug channel name compare
// #define DEBUG_CHANNEL_NAME

// debug hw_device_ctx reference count
// #define DEBUG_HW_DEVICE_CTX_REF

extern int SysLogLevel;
extern void syslog_with_tid(int priority, const char *format, ...) __attribute__ ((format (printf, 2, 3)));

#ifdef POSIX
#define esyslog(a...) void( (SysLogLevel > 0) ? syslog_with_tid(LOG_ERR, a) : void() )
#define isyslog(a...) void( (SysLogLevel > 1) ? syslog_with_tid(LOG_INFO, a) : void() )
#define dsyslog(a...) void( (SysLogLevel > 2) ? syslog_with_tid(LOG_DEBUG, a) : void() )
#define tsyslog(a...) void( (SysLogLevel > 3) ? syslog_with_tid(LOG_TRACE, a) : void() )
#else
/* no POSIX, no syslog */
#include <cstdio>
#define esyslog(a...) if (SysLogLevel > 0) fprintf(stderr, a)
#define isyslog(a...) if (SysLogLevel > 1) fprintf(stdout, a)
#define dsyslog(a...) if (SysLogLevel > 2) fprintf(stdout, a)
#define tsyslog(a...) if (SysLogLevel > 3) fprintf(stdout, a)
#endif

#if defined(DEBUG_PICTURE)
void SaveVideoPicture(const char *baseName, const sVideoPicture *picture);
#endif

#if defined(DEBUG_MARK_FRAMES) || defined(DEBUG_LOGO_DETECT_FRAME_CORNER) || defined(DEBUG_DECODER_SEEK) || defined(DEBUG_CUT)
void SaveVideoPlane0(const char *fileName, const sVideoPicture *picture);
#endif

#ifdef DEBUG_MEM
#define ALLOC(size, var) memAlloc(size, __LINE__, const_cast<char *>(__FILE__), const_cast<char *>(var))
#define FREE(size, var) memFree(size, __LINE__, const_cast<char *>(__FILE__), const_cast<char *>(var))
void memAlloc(int size, int line, char *file, char *var);
void memFree(int size, int line, char *file, char *var);
void memList();
#else
#define ALLOC(size, var)
#define FREE(size, var)
#endif
#endif
