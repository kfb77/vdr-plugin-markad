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

#define trcs(c) bind_textdomain_codeset("markad",c)
#define tr(s) dgettext("markad",s)

#define IGNORE_VIDEOINFO 1
#define IGNORE_AUDIOINFO 2
#define IGNORE_TIMERINFO 4

#define DELTATIME 20000 /* equals to 222ms (base is 90kHz PTS) */

#define MAXRANGE 120 /* range to search for start/stop marks in seconds */

class cOSDMessage
{
private:
    const char *host;
    int port;
    char *msg;
    pthread_t tid;
    static void *send(void *osd);
    bool readreply(int fd, char **reply=NULL);
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

    enum { mSTART=0x1, mBEFORE, mAFTER };

    static const char frametypes[8];
    const char *directory;

    cDemux *demux;
    cMarkAdDecoder *decoder;
    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cMarkAdStreamInfo *streaminfo;
    cOSDMessage *osd;

    AvPacket pkt;

    MarkAdContext macontext;

    char title[80],*ptitle;

    bool CreatePidfile();
    void RemovePidfile();
    bool duplicate; // are we a dup?

    bool isTS;
    bool isREEL;
    int MaxFiles;

    int lastiframe;
    int iframe;

    int framecnt;
    int framecnt2; // 2nd pass

    bool abort;
    bool gotendmark;
    int waittime;
    int iwaittime;
    struct timeval tv1,tv2;
    struct timezone tz;

    bool noticeVDR_VID;
    bool noticeVDR_AC3;
    bool noticeHEADER;
    bool noticeFILLER;

    bool bDecodeVideo;
    bool bDecodeAudio;
    bool bIgnoreTimerInfo;
    bool bLiveRecording;

    time_t startTime;  // starttime of broadcast
    int length;	       // length of broadcast in seconds
    int tStart;        // pretimer in seconds
    int iStart;        // pretimer in frames (negative if unset)
    int iStop;         // endposition in frames (negative if unset)
    int iStopA;        // assumed endposition in frames (negative if unset)
    bool iStopinBroadCast; // in broadcast @ iStop position?

    void CheckStop();
    void CheckStart();
    void CalculateCheckPositions(int startframe);
    int chkSTART;
    int chkSTOP;

    int skipped;       // skipped bytes in whole file
    bool inBroadCast;  // are we in a broadcast (or ad)?

    time_t GetBroadcastStart(time_t start, int fd);
    void CheckIndexGrowing();
    char *indexFile;
    int sleepcnt;

    void SaveFrame(int Frame);

    clMarks marks;
    char *IndexToHMSF(int Index);
    void AddMark(MarkAdMark *Mark);
    bool Reset(bool FirstPass=true);
    void ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos);

    bool CheckVDRHD();
    off_t SeekPATPMT();
    bool CheckPATPMT(off_t Offset=0);
    bool CheckTS();
    bool CheckLogo();
    void CheckLogoMarks();
    bool LoadInfo();
    bool SaveInfo();
    bool SetFileUID(char *File);
    bool RegenerateIndex();
    bool ProcessFile2ndPass(clMark **Mark1, clMark **Mark2, int Number, off_t Offset, int Frame, int Frames);
    bool ProcessFile(int Number);
    void ProcessFile();
public:
    cMarkAdStandalone(const char *Directory, const MarkAdConfig *config);
    ~cMarkAdStandalone();
    void SetAbort()
    {
        abort=true;
    }
    void Process2ndPass();
    void Process();
};

#endif
