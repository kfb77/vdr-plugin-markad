/*
 * markad-standalone.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include <syslog.h>
#include <stdio.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>
#include <signal.h>
#include <ctype.h>

#include "demux.h"
#include "global.h"
#include "decoder.h"
#include "video.h"
#include "audio.h"
#include "common.h"
#include "version.h"

int SysLogLevel=2;

class cMarkAdIndex
{
private:
    int index_fd; // index file descriptor
    int maxfiles;
    bool ts;

    int index;
    int iframe;
    off_t offset;

    bool Open(const char *Directory);
    void Close();
public:
    bool isTS()
    {
        return ts;
    }
    int MaxFiles()
    {
        return maxfiles;
    }
    int GetNext(off_t Offset);
    cMarkAdIndex(const char *Directory);
    ~cMarkAdIndex();
};

class cMarkAdStandalone
{
private:
    cMarkAdDemux *video_demux;
    cMarkAdDemux *ac3_demux;
    cMarkAdDemux *mp2_demux;
    cMarkAdDecoder *decoder;
    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cMarkAdCommon *common;

    MarkAdContext macontext;
    char *dir;

    cMarkAdIndex *index;

    void AddMark(MarkAdMark *Mark);
    int LastIFrame(int Number, off_t Offset);
    bool ProcessFile(int Number);

public:
    void Process();
    cMarkAdStandalone(const char *Directory);
    ~cMarkAdStandalone();
};

#endif
