/*
 * common.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __common_h_
#define __common_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "global.h"

class cMarkAdCommon
{
private:
    int recvnumber;
    MarkAdContext *macontext;
    MarkAdMark mark;

    void ResetMark();
    bool AddMark(int Position, const char *Comment);
    void SetTimerMarks(int LastIFrame);
public:
    MarkAdMark *Process(int LastIFrame);
    cMarkAdCommon(int RecvNumber,MarkAdContext *maContext);
    ~cMarkAdCommon();
};

#endif