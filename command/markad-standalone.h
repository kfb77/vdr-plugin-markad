/*
 * markad-standalone.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include "global.h"
#include "demux.h"
#include "decoder.h"
#include "video.h"
#include "audio.h"
#include "streaminfo.h"
#include "marks.h"

#define tr(s) dgettext("markad",s)

#define IGNORE_VIDEOINFO 1
#define IGNORE_AUDIOINFO 2
#define IGNORE_TIMERINFO 4

#define DELTATIME 20000 /* equals to 222ms (base is 90kHz PTS) */

#define MAXRANGE 420 /* range to search for start/stop marks in seconds */


class cOSDMessage
{
private:
    const char *host;
    int port;
    char *msg;
    pthread_t tid;
    static void *send(void *osd);
    bool readreply(int fd);
public:
    int Send(const char *format, ...);
    cOSDMessage(const char *Host, int Port);
    ~cOSDMessage();
};

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

    static const char frametypes[8];
    const char *directory;

    cMarkAdDemux *video_demux;
    cMarkAdDemux *ac3_demux;
    cMarkAdDemux *mp2_demux;
    cMarkAdDecoder *decoder;
    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cMarkAdStreamInfo *streaminfo;
    cOSDMessage *osd;

    MarkAdPacket vpkt,apkt;

    MarkAdContext macontext;

    char title[80],*ptitle;

    bool CreatePidfile();
    void RemovePidfile();
    bool duplicate; // are we a dup?

    bool isTS;
    int MaxFiles;

    int lastiframe;
    int iframe;

    unsigned int iframetime;
    unsigned int lastiframetime;
    unsigned int audiotime;

    int framecnt;
    int framecnt2; // 2nd pass

    bool abort;
    bool gotendmark;
    bool reprocess;
    int waittime;
    struct timeval tv1,tv2;
    struct timezone tz;

    bool noticeVDR_MP2;
    bool noticeVDR_AC3;
    bool noticeHEADER;

    bool bDecodeVideo;
    bool bDecodeAudio;
    bool bIgnoreAudioInfo;
    bool bIgnoreVideoInfo;
    bool bIgnoreTimerInfo;

    int tStart; // pretimer in seconds
    int iStart; // pretimer as index value
    int iStartCheck; // check position for iStart
    int iStop;  // posttimer as index value
    int iStopCheck; // check position for iStop

    bool setAudio51;  // set audio to 5.1 in info
    bool setAudio20;  // set audio to 2.0 in info
    bool setVideo43;  // set video to 4:3 in info
    bool setVideo169; // set video to 16:9 in info

    int nextPictType;

    int chkLEFT;
    int chkRIGHT;

    void CheckBroadcastLength();
    bool CheckIndexGrowing();
    char *indexFile;
    int sleepcnt;

    void SaveFrame(int Frame);

    bool aspectChecked;
    bool marksAligned;

    clMarks marks;
    char *IndexToHMSF(int Index);
    void CalculateStopPosition(int startframe, int delta);
    void CheckFirstMark();
    void CheckLastMark();
    bool CheckDolbyDigital51();
    void CheckStartStop(int frame, bool checkend=false);
    void CheckInfoAspectRatio();
    void CheckLogoMarks(clMark *last=NULL);
    void AddStartMark();
    void AddMark(MarkAdMark *Mark);
    bool Reset(bool FirstPass=true);
    void ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos);

    bool CheckVDRHD();
    bool CheckPATPMT();
    bool CheckTS();
    bool LoadInfo();
    bool SaveInfo();
    bool RegenerateIndex();
    bool ProcessFile2ndPass(clMark **Mark1, clMark **Mark2, int Number, off_t Offset, int Frame, int Frames);
    bool ProcessFile(int Number);

    char *Timestamp2HMS(unsigned int Timestamp);
    void ProcessFile();
public:
    void SetAbort()
    {
        abort=true;
    }
    void Process2ndPass();
    void Process();
    cMarkAdStandalone(const char *Directory, const MarkAdConfig *config);

    ~cMarkAdStandalone();
};

#endif
