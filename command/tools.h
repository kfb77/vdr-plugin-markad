/*
 * tools.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __tools_h_
#define __tools_h_


// flags for CompareChannelName()
#define IGNORE_NOTHING 0   // exact match
#define IGNORE_HD      1   // ignore HD
#define IGNORE_COUNTRY 2   // ignore _Austria


/**
 * common tools class
 */
class cTools {
public:
    cTools();
    ~cTools();

    bool CompareChannelName(const char *nameA, const char *nameB, const int flags);
};
#endif
