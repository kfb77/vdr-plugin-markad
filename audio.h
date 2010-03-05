/*
 * audio.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __audio_h_
#define __audio_h_

#include <vdr/tools.h> // needed for (d/e/i)syslog
#include <netinet/in.h> // for htonl

#include "global.h"

class cMarkAdAudio
{
private:
    int lastiframe;
    int recvnumber;
    MarkAdContext *macontext;

    MarkAdMark mark;
    void ResetMark();
    bool AddMark(int Position, const char *Comment);

    int channels;
    bool ChannelChange(int a, int b);
public:
    cMarkAdAudio(int RecvNumber,MarkAdContext *maContext);
    ~cMarkAdAudio();
    MarkAdMark *Process(int LastIFrame);
};


#endif
