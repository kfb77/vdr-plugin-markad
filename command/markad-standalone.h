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
#include "evaluate.h"

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


class cMarkAdStandalone {
    public:
        cMarkAdStandalone(const char *Directory, MarkAdConfig *config, cIndex *recordingIndex);
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
            return *this;
        }
        void ProcessFiles();
        void Process2ndPass();
        void Process3ndPass();
        void MarkadCut();
#ifdef DEBUG_MARK_FRAMES
        void DebugMarkFrames();
#endif

    private:
        void CheckStop();
        bool MoveLastStopAfterClosingCredits(clMark *stopMark);
        void RemoveLogoChangeMarks();
        void CheckStart();
        void CalculateCheckPositions(int startframe);
        bool isVPSTimer();
        time_t GetBroadcastStart(time_t start, int fd);
#if defined(DEBUG_LOGO_DETECT_FRAME_CORNER) || defined(DEBUG_MARK_FRAMES)
        void SaveFrame(const int frame, const char *path = NULL, const char *suffix = NULL);
#endif
        char *IndexToHMSF(int Index);
        void AddMark(MarkAdMark *Mark);
        void AddMarkVPS(const int offset, const int type, const bool isPause);
        bool Reset(bool FirstPass=true);
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
        bool ProcessFrame(cDecoder *ptr_cDecoder);

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
        int iFrameBefore = -1;
        int iFrameCurrent = -1;
        int frameCurrent = -1;
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
        clMarks blackMarks;
        cDecoder *ptr_cDecoderLogoChange = NULL;
        cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = NULL;
};
#endif
