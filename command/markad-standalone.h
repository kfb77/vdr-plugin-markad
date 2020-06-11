/*
 * markad-standalone.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include <sys/time.h>

#include "global.h"
#include "demux.h"
#include "decoder.h"
#include "video.h"
#include "audio.h"
#include "streaminfo.h"
#include "marks.h"
#include "encoder_new.h"

#define trcs(c) bind_textdomain_codeset("markad",c)
#define tr(s) dgettext("markad",s)

#define IGNORE_VIDEOINFO 1
#define IGNORE_AUDIOINFO 2
#define IGNORE_TIMERINFO 4

#define DELTATIME 20000 /* equals to 222ms (base is 90kHz PTS) */

#define MAXRANGE 120 /* range to search for start/stop marks in seconds */


class cOSDMessage {
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

class cMarkAdStandalone {
    private:
        struct PAT {
            unsigned table_id: 8;
            unsigned section_length_H: 4;
            unsigned reserved1: 2;
            unsigned zero: 1;
            unsigned section_syntax_indicator: 1;
            unsigned section_length_L: 8;
            unsigned transport_stream_id_H: 8;
            unsigned transport_stream_id_L: 8;
            unsigned current_next_indicator: 1;
            unsigned version_number: 5;
            unsigned reserved2: 2;
            unsigned section_number: 8;
            unsigned last_section_number: 8;
            unsigned program_number_H: 8;
            unsigned program_number_L: 8;
            unsigned pid_H: 5;
            unsigned reserved3: 3;
            unsigned pid_L: 8;
        };

    struct PMT {
        unsigned table_id: 8;
        unsigned section_length_H: 4;
        unsigned reserved1: 2;
        unsigned zero: 1;
        unsigned section_syntax_indicator: 1;
        unsigned section_length_L: 8;
        unsigned program_number_H: 8;
        unsigned program_number_L: 8;
        unsigned current_next_indicator: 1;
        unsigned version_number: 5;
        unsigned reserved2: 2;
        unsigned section_number: 8;
        unsigned last_section_number: 8;
        unsigned PCR_PID_H: 5;
        unsigned reserved3: 3;
        unsigned PCR_PID_L: 8;
        unsigned program_info_length_H: 4;
        unsigned reserved4: 4;
        unsigned program_info_length_L: 8;
    };

#pragma pack(1)
    struct STREAMINFO {
        unsigned stream_type: 8;
        unsigned PID_H: 5;
        unsigned reserved1: 3;
        unsigned PID_L: 8;
        unsigned ES_info_length_H: 4;
        unsigned reserved2: 4;
        unsigned ES_info_length_L: 8;
    };
#pragma pack()

    struct ES_DESCRIPTOR {
        unsigned Descriptor_Tag: 8;
        unsigned Descriptor_Length: 8;
    };

    enum { mSTART=0x1, mBEFORE, mAFTER };

    static const char frametypes[8];
    const char *directory;

    cMarkAdVideo *video;
    cMarkAdAudio *audio;
    cOSDMessage *osd;
    AvPacket pkt;
    MarkAdContext macontext;
    char title[80];
    char *ptitle;
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
    time_t startTime = 0;  // starttime of broadcast
    int length = 0;        // length of broadcast in seconds
    int tStart = 0;        // pretimer in seconds
    int iStart = 0;        // pretimer in frames (negative if unset)
    int iStop = 0;         // endposition in frames (negative if unset)
    int iStartA = 0;       // assumed startposition in frames
    int iStopA = 0;        // assumed endposition in frames (negative if unset)
    bool iStopinBroadCast = false;    // in broadcast @ iStop position?
    int chkSTART;
    int chkSTOP;
    bool inBroadCast;  // are we in a broadcast (or ad)?
    int skipped;       // skipped bytes in whole file
    char *indexFile;
    int sleepcnt;
    clMarks marks;

    void CheckStop();
    void CheckStart();
    void CalculateCheckPositions(int startframe);
    time_t GetBroadcastStart(time_t start, int fd);
    void SaveFrame(int Frame);
    char *IndexToHMSF(int Index);
    void AddMark(MarkAdMark *Mark);
    bool Reset(bool FirstPass=true);
    void ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos);
    void CheckIndexGrowing();
    bool CheckVDRHD();
    off_t SeekPATPMT();
    bool CheckPATPMT(off_t Offset=0);
    bool CheckTS();
    bool CheckLogo();
    void CheckMarks();
    void LogSeparator();
    void DebugMarks();
    bool LoadInfo();
    bool SaveInfo();
    bool SetFileUID(char *File);
#if !defined ONLY_WITH_CDECODER
    cDemux *demux;
    cMarkAdDecoder *decoder;
    cMarkAdStreamInfo *streaminfo;
    bool RegenerateIndex();
    bool ProcessFile2ndPass(clMark **Mark1, clMark **Mark2, int Number, off_t Offset, int Frame, int Frames);
    bool ProcessFile(int Number);
    void ProcessFile();
#endif
    bool ProcessMark2ndPass(clMark **Mark1, clMark **Mark2);
    void ProcessFile_cDecoder();
    bool ProcessFrame(cDecoder *ptr_cDecoder);
public:
    cMarkAdStandalone(const char *Directory, const MarkAdConfig *config);
    ~cMarkAdStandalone();
    cMarkAdStandalone(const cMarkAdStandalone &origin) {
        strcpy(title,origin.title);
        ptitle = title;
        directory = origin.directory;
        video = NULL;
        audio = NULL;
        osd = NULL;
        duplicate = origin.duplicate,
        isTS = origin.isTS;
        isREEL = origin.isREEL;
        MaxFiles = origin.MaxFiles;
        lastiframe = origin.lastiframe;
        iframe = origin.iframe;
        framecnt = origin.framecnt;
        framecnt2 = origin.framecnt2;
        gotendmark = origin.gotendmark;
        waittime = origin.waittime;
        iwaittime = origin.iwaittime;
        noticeVDR_VID = origin.noticeVDR_VID;
        noticeVDR_AC3 = origin.noticeVDR_AC3;
        noticeHEADER = origin.noticeHEADER;
        noticeFILLER = origin.noticeFILLER;
        bDecodeVideo = origin.bDecodeVideo;
        bDecodeAudio = origin.bDecodeAudio;
        bIgnoreTimerInfo = origin.bIgnoreTimerInfo;
        bLiveRecording = origin.bLiveRecording;
        chkSTART = origin.chkSTART;
        chkSTOP = origin.chkSTOP;
        inBroadCast = origin.inBroadCast;
        skipped = origin.skipped;
        indexFile = origin.indexFile;
        sleepcnt = origin.sleepcnt;
    };
    cMarkAdStandalone &operator =(const cMarkAdStandalone *origin) {
        strcpy(title,origin->title);
        ptitle = title;
        directory = origin->directory;
        video = NULL;
        audio = NULL;
        osd = NULL;
        duplicate = origin->duplicate,
        isTS = origin->isTS;
        isREEL = origin->isREEL;
        MaxFiles = origin->MaxFiles;
        lastiframe = origin->lastiframe;
        iframe = origin->iframe;
        framecnt = origin->framecnt;
        framecnt2 = origin->framecnt2;
        gotendmark = origin->gotendmark;
        waittime = origin->waittime;
        iwaittime = origin->iwaittime;
        noticeVDR_VID = origin->noticeVDR_VID;
        noticeVDR_AC3 = origin->noticeVDR_AC3;
        noticeHEADER = origin->noticeHEADER;
        noticeFILLER = origin->noticeFILLER;
        bDecodeVideo = origin->bDecodeVideo;
        bDecodeAudio = origin->bDecodeAudio;
        bIgnoreTimerInfo = origin->bIgnoreTimerInfo;
        bLiveRecording = origin->bLiveRecording;
        chkSTART = origin->chkSTART;
        chkSTOP = origin->chkSTOP;
        inBroadCast = origin->inBroadCast;
        skipped = origin->skipped;
        indexFile = origin->indexFile;
        sleepcnt = origin->sleepcnt;
        return *this;
    }
    void Process_cDecoder();
    void Process2ndPass();
    void MarkadCut();
#if !defined ONLY_WITH_CDECODER
    void Process();
#endif
};
#endif
