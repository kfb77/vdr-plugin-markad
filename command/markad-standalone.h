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


/**
 * send OSD message to VDR
 */
class cOSDMessage {
    public:

/** constuctor
 * @param hostName   name or IP address of VDR
 * @param portNumber port number for OSD messages
 */
        cOSDMessage(const char *hostName, int portNumber);

        ~cOSDMessage();

/**
 * send message to VDR OSD
 * @param format message format
 * @return 0 for success, -1 otherwise
 */
        int Send(const char *format, ...);

    private:
        const char *host;                            //!< VDR host name or IP address
                                                     //!<
        int port;                                    //!< VDR port number to send OSD messages
                                                     //!<
        char *msg = NULL;                            //!< OSD message
                                                     //!<
        pthread_t tid = 0;                           //!< thread id of the OSD message
                                                     //!<
        static void *SendMessage(void *osd);         //!< send OSD message
                                                     //!<
        bool ReadReply(int fd, char **reply = NULL); //!< read reply from OSD
                                                     //!<
};


class cMarkAdStandalone {
    public:
        cMarkAdStandalone(const char *Directory, sMarkAdConfig *config, cIndex *recordingIndex);
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
        void AddMark(sMarkAdMark *Mark);
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
        sMarkAdContext macontext = {};
        cIndex *recordingIndexMark = NULL;
        enum { mSTART = 0x1, mBEFORE, mAFTER };
        const char *directory;
        char title[80];
        char *ptitle = NULL;
        bool CreatePidfile();
        void RemovePidfile();
        bool duplicate = false; // are we a dup?
        bool isTS = false;
        bool isREEL = false;                                           //!< true if markad runs on a Reelbox VDR (VDR info file is info.txt), false otherwise
                                                                       //!<
        int MaxFiles = 0;                                              //!< maximum number of ts files
        int iFrameBefore = -1;                                         //!< i-frame number before last processed i-frame number
                                                                       //!<
        int iFrameCurrent = -1;                                        //!< last processed i-frame number
                                                                       //!<
        int frameCurrent = -1;                                         //!< current processed frame number
                                                                       //!<
        int framecnt1 = 0;                                             //!< processed frames of 1nd pass (detect marks)
                                                                       //!<
        int framecnt2 = 0;                                             //!< processed frames of 2nd pass (overlap)
                                                                       //!<
        int framecnt3 = 0;                                             //!< processed frames of 3nd pass (silence)
                                                                       //!<
        int framecnt4 = 0;                                             //!< processed frames of 4nd pass (cut)
                                                                       //!<
        bool gotendmark = false;                                       //!< true if a valid end mark was found, false otherwise
                                                                       //!<
        int waittime = 0;                                              //!< time waited for more frames if markad runs during recording
                                                                       //!<
        int iwaittime = 0;                                             //!< time waited for continuation of interrupted recording
                                                                       //!<
        bool bDecodeVideo = false;                                     //!< true if configured to decode video, false otherwise
                                                                       //!<
        bool bDecodeAudio = false;                                     //!< true if configured to decode audio, false otherwise
                                                                       //!<
        bool bIgnoreTimerInfo = false;                                 //!< true if confugured to ignore timer infos from info file, false otherwise
                                                                       //!<
        bool bLiveRecording = false;                                   //!< true if markad was started during recording, false otherwise
                                                                       //!<
        time_t startTime = 0;                                          //!< start time of the broadcast
                                                                       //!<
        int length = 0;                                                //!< length of broadcast in seconds
                                                                       //!<
        int iStart = 0;                                                //!< pretimer (recording start before bradcast start) in frames (negative if unset)
                                                                       //!<
        int iStop = 0;                                                 //!< end frame position (negative if unset)
                                                                       //!<
        int iStartA = 0;                                               //!< assumed start frame position
                                                                       //!<
        int iStopA = 0;                                                //!< assumed end frame position (negative if unset)
                                                                       //!<
        bool iStopinBroadCast = false;                                 //!< true if we are in broadcast at iStop position, false otherwise
                                                                       //!<
        int chkSTART = 0;                                              //!< frame number to check for start mark
                                                                       //!<
        int chkSTOP = 0;                                               //!< frame number to check for end mark
                                                                       //!<
        bool inBroadCast = false;                                      //!< true if are we in a broadcast, false if we are in advertising
                                                                       //!<
        char *indexFile = NULL;                                        //!< file name of the vdr index file
                                                                       //!<
        int sleepcnt = 0;                                              //!< count of sleeps to wait for new frames when decode during recording
                                                                       //!<
        clMarks marks;                                                 //!< objects with all marks
                                                                       //!<
        clMarks blackMarks;                                            //!< objects with all blackscreen marks
                                                                       //!<
        cDecoder *ptr_cDecoderLogoChange = NULL;                       //!< pointer to class cDecoder, used as second instance to detect logo changes
                                                                       //!<
        cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = NULL;  //!< pointer to class cEvaluateLogoStopStartPair
                                                                       //!<
};
#endif
