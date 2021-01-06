/*
 * markad-standalone.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include <sys/time.h>

extern "C" {
    #include "debug.h"
}
#include "global.h"
#include "video.h"
#include "audio.h"
#include "marks.h"
#include "encoder_new.h"

#if defined CLASSIC_DECODER
    #include "demux.h"
    #include "decoder.h"
    #include "streaminfo.h"
#endif

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
        pthread_t tid = 0;
        static void *send(void *osd);
        bool readreply(int fd, char **reply=NULL);
    public:
        int Send(const char *format, ...);
        cOSDMessage(const char *Host, int Port);
        ~cOSDMessage();
};


class cEvaluateLogoStopStartPair {
    public:
        cEvaluateLogoStopStartPair(clMarks *marks, const int framesPerSecond, const int iStart);
        ~cEvaluateLogoStopStartPair();
        bool GetNextPair(int *stopPosition, int *startPosition);
        void SetClosingCredits(const int stopPosition, const int isClosingCredits);
        int GetLastClosingCreditsStart();
    private:
        struct logoStopStartPair {
            int stopPosition = -1;
            int startPosition = -1;
            int isLogoChange = 0;            // -1 no logo change, 0 unknown, 1 is logo change
            int isAdvertising = 0;           // -1 pair is advertising, 0 unknown, 1 pair is advertising
            int isStartMarkInBroadcast = 0;  // -1 start mark does not contain to broadcast, 0 unknown, 1 start mark contains to broadcast
            int isClosingCredits = 0;            // -1 no closing credits, 0 unknown, 1 is closing credits
        };
        std::vector<logoStopStartPair> logoPairVector;
        std::vector<logoStopStartPair>::iterator nextLogoPairIterator;
};


class cMarkAdStandalone {
    private:
        cMarkAdVideo *video = NULL;
        cMarkAdAudio *audio = NULL;
        cOSDMessage *osd = NULL;
        MarkAdContext macontext = {};
	cIndex *recordingIndexMark = NULL;

        enum { mSTART = 0x1, mBEFORE, mAFTER };
        const char *directory;
        char title[80];
        char *ptitle = NULL;
        bool CreatePidfile();
        void RemovePidfile();
        bool duplicate = false; // are we a dup?
        bool isTS = false;
        bool isREEL = false;
        int MaxFiles = 0;
        int lastiframe = 0;
        int iframe = 0;
        int framecnt1 = 0; // 1nd pass (detect marks)
        int framecnt2 = 0; // 2nd pass (overlap)
        int framecnt3 = 0; // 3nd pass (silence)
        int framecnt4 = 0; // 3nd pass (cut)
        bool gotendmark = false;
        int waittime = 0;
        int iwaittime = 0;
        bool noticeVDR_VID = false;
        bool noticeVDR_AC3 = false;
        bool noticeHEADER = false;
        bool noticeFILLER = false ;
        bool bDecodeVideo = false;
        bool bDecodeAudio = false;
        bool bIgnoreTimerInfo = false;
        bool bLiveRecording = false;
        time_t startTime = 0;  // starttime of broadcast
        int length = 0;        // length of broadcast in seconds
        int tStart = 0;        // pretimer in seconds
        int iStart = 0;        // pretimer in frames (negative if unset)
        int iStop = 0;         // endposition in frames (negative if unset)
        int iStartA = 0;       // assumed startposition in frames
        int iStopA = 0;        // assumed endposition in frames (negative if unset)
        bool iStopinBroadCast = false;    // in broadcast @ iStop position?
        int chkSTART = 0;
        int chkSTOP = 0;
        bool inBroadCast = false;  // are we in a broadcast (or ad)?
        char *indexFile = NULL;
        int sleepcnt = 0;
        clMarks marks;
	cDecoder *ptr_cDecoderLogoChange = NULL;

        void CheckStop();
        bool MoveLastLogoStopAfterClosingCredits(clMark *stopMark);
        int RemoveLogoChangeMarks();
        void CheckStart();
        void CalculateCheckPositions(int startframe);
        bool isVPSTimer();
        time_t GetBroadcastStart(time_t start, int fd);
#if defined(DEBUG_FRAME) || defined(DEBUG_MARK_FRAMES)
        void SaveFrame(const int frame, const char *path = NULL, const char *suffix = NULL);
#endif
        char *IndexToHMSF(int Index);
        void AddMark(MarkAdMark *Mark);
        void AddMarkVPS(const int offset, const int type, const bool isPause);
        bool Reset(bool FirstPass=true);
        void ChangeMarks(clMark **Mark1, clMark **Mark2, MarkAdPos *NewPos);
        void CheckIndexGrowing();
        bool CheckTS();
        bool CheckLogo();
        void CheckMarks();
        void LogSeparator(const bool main = false);
        void DebugMarks();
        bool LoadInfo();
        bool SaveInfo();
        bool SetFileUID(char *File);
        bool ProcessMark2ndPass(clMark **Mark1, clMark **Mark2);
        void ProcessFile_cDecoder();
        bool ProcessFrame(cDecoder *ptr_cDecoder);

#if defined CLASSIC_DECODER
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

        AvPacket pkt = {};
        int skipped;       // skipped bytes in whole file
        struct ES_DESCRIPTOR {
            unsigned Descriptor_Tag: 8;
            unsigned Descriptor_Length: 8;
        };
        bool CheckVDRHD();
        off_t SeekPATPMT();
        bool CheckPATPMT(off_t Offset=0);
        cDemux *demux = NULL;
        cMarkAdDecoder *decoder;
        cMarkAdStreamInfo *streaminfo;
        bool RegenerateIndex();
        bool ProcessFile2ndPass(clMark **Mark1, clMark **Mark2, int Number, off_t Offset, int Frame, int Frames);
        bool ProcessFile(int Number);
        void ProcessFile();
#endif
    public:
        cMarkAdStandalone(const char *Directory, const MarkAdConfig *config, cIndex *recordingIndex);
        ~cMarkAdStandalone();
        cMarkAdStandalone(const cMarkAdStandalone &origin) {   //  copy constructor, not used, only for formal reason
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
            framecnt1 = origin.framecnt1;
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
            indexFile = origin.indexFile;
            sleepcnt = origin.sleepcnt;
#if defined CLASSIC_DECODER
            skipped = origin.skipped;
            demux = NULL;
            decoder = NULL;
            streaminfo = NULL;
#endif
        };
        cMarkAdStandalone &operator =(const cMarkAdStandalone *origin) {   // operator=, not used, only for formal reason
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
            framecnt1 = origin->framecnt1;
            framecnt2 = origin->framecnt2;
            framecnt3 = origin->framecnt3;
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
            indexFile = origin->indexFile;
            sleepcnt = origin->sleepcnt;
#if defined CLASSIC_DECODER
            skipped = origin->skipped;
            demux = NULL;
            decoder = NULL;
            streaminfo = NULL;
#endif
            return *this;
        }
        void Process_cDecoder();
        void Process2ndPass();
        void Process3ndPass();
        void MarkadCut();
#ifdef DEBUG_MARK_FRAMES
        void DebugMarkFrames();
#endif
#if defined CLASSIC_DECODER
        void Process();
#endif
};
#endif
