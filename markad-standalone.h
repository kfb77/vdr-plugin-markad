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

class cMarkAdStandalone
{
private:

    struct PAT
    {
unsigned table_id:
        8;
unsigned section_length_H:
        4;
unsigned reserved1:
        2;
unsigned zero:
        1;
unsigned section_syntax_indicator:
        1;
unsigned section_length_L:
        8;
unsigned transport_stream_id_H:
        8;
unsigned transport_stream_id_L:
        8;
unsigned current_next_indicator:
        1;
unsigned version_number:
        5;
unsigned reserved2:
        2;
unsigned section_number:
        8;
unsigned last_section_number:
        8;
unsigned program_number_H:
        8;
unsigned program_number_L:
        8;
unsigned pid_H:
        5;
unsigned reserved3:
        3;
unsigned pid_L:
        8;
    };

    struct PMT
    {
unsigned table_id:
        8;
unsigned section_length_H:
        4;
unsigned reserved1:
        2;
unsigned zero:
        1;
unsigned section_syntax_indicator:
        1;
unsigned section_length_L:
        8;
unsigned program_number_H:
        8;
unsigned program_number_L:
        8;
unsigned current_next_indicator:
        1;
unsigned version_number:
        5;
unsigned reserved2:
        2;
unsigned section_number:
        8;
unsigned last_section_number:
        8;
unsigned PCR_PID_H:
        5;
unsigned reserved3:
        3;
unsigned PCR_PID_L:
        8;
unsigned program_info_length_H:
        4;
unsigned reserved4:
        4;
unsigned program_info_length_L:
        8;
    };

#pragma pack(1)
    struct STREAMINFO
    {
unsigned stream_type:
        8;
unsigned PID_H:
        5;
unsigned reserved1:
        3;
unsigned PID_L:
        8;
unsigned ES_info_length_H:
        4;
unsigned reserved2:
        4;
unsigned ES_info_length_L:
        8;
    };
#pragma pack()

    struct ES_DESCRIPTOR
    {
unsigned Descriptor_Tag:
        8;
unsigned Descriptor_Length:
        8;
    };


    cMarkAdDemux *video_demux;
    cMarkAdDemux *ac3_demux;
    cMarkAdDemux *mp2_demux;
    cMarkAdDecoder *decoder;
    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cMarkAdCommon *common;

    MarkAdContext macontext;
    int recvnumber;

    bool isTS;
    int MaxFiles;
    int framecnt;
    bool abort;

    void AddMark(MarkAdMark *Mark);
    bool CheckPATPMT(const char *Directory);
    bool CheckTS(const char *Directory);
    bool ProcessFile(const char *Directory, int Number);

public:
    void SetAbort()
    {
        abort=true;
    }
    void Process(const char *Directory);
    cMarkAdStandalone(const char *Directory);
    ~cMarkAdStandalone();
};

#endif
