/*
 * markad-standalone.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __markad_standalone_h_
#define __markad_standalone_h_

#include <sys/time.h>

#include "debug.h"
#include "global.h"
#include "video.h"
#include "audio.h"
#include "marks.h"
#include "encoder.h"
#include "evaluate.h"
#include "osd.h"
#include "criteria.h"
#include "vps.h"


#define IGNORE_VIDEOINFO 1
#define IGNORE_AUDIOINFO 2
#define IGNORE_TIMERINFO 4


/* forward declarations */
class cOSDMessage;

/**
 * markad main class
 */
class cMarkAdStandalone : public cEvaluateChannel {
    public:

/**
 * markad main constuctor
 * @param directoryParam recording directory
 * @param config         markad context configuration
 * @param recordingIndex recording index
 */
        cMarkAdStandalone(const char *directoryParam, sMarkAdConfig *config, cIndex *recordingIndex);
        ~cMarkAdStandalone();

/**
 * copy constructor, not used, only for formal reason
 */
        cMarkAdStandalone(const cMarkAdStandalone &origin) {   //  copy constructor, not used, only for formal reason
            strcpy(title,origin.title);
            ptitle = title;
            directory = origin.directory;
            video = NULL;
            audio = NULL;
            osd = NULL;
            duplicate = origin.duplicate,
            isREEL = origin.isREEL;
            MaxFiles = origin.MaxFiles;
            framecnt = origin.framecnt;
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

/**
 * operator=, not used, only for formal reason
 */
        cMarkAdStandalone &operator =(const cMarkAdStandalone *origin) {
            strcpy(title,origin->title);
            ptitle                    = title;
            directory                 = origin->directory;
            video                     = NULL;
            audio                     = NULL;
            osd                       = NULL;
            duplicate                 = origin->duplicate,
            isREEL                    = origin->isREEL;
            MaxFiles                  = origin->MaxFiles;
            framecnt                  = origin->framecnt;
            gotendmark                = origin->gotendmark;
            waittime                  = origin->waittime;
            iwaittime                 = origin->iwaittime;
            bDecodeVideo              = origin->bDecodeVideo;
            bDecodeAudio              = origin->bDecodeAudio;
            bIgnoreTimerInfo          = origin->bIgnoreTimerInfo;
            bLiveRecording            = origin->bLiveRecording;
            chkSTART                  = origin->chkSTART;
            chkSTOP                   = origin->chkSTOP;
            inBroadCast               = origin->inBroadCast;
            indexFile                 = origin->indexFile;
            sleepcnt                  = origin->sleepcnt;
            macontext                 = origin->macontext;
            recordingIndexMark        = origin->recordingIndexMark;
            iFrameBefore              = origin->iFrameBefore;
            iFrameCurrent             = origin->iFrameCurrent;
            frameCurrent              = origin->frameCurrent;
            length                    = origin->length;
            iStart                    = origin->iStart;
            iStop                     = origin->iStop;
            iStartA                   = origin->iStartA;
            iStopA                    = origin->iStopA;
            endMarkPos                = origin->endMarkPos;
            ptr_cDecoderLogoChange    = origin->ptr_cDecoderLogoChange;
            evaluateLogoStopStartPair = origin->evaluateLogoStopStartPair;
            vps                       = origin->vps;
            return *this;
        }

/**
 * process all ts files and detect marks
 */
        void ProcessFiles();

/**
 * process second pass, detect overlaps
 */
        void ProcessOverlap();

/** boder mark optimization <br>
 * move border marks:
 *     - if black screen marks are direct after broadcast start mark
 */
        void BorderMarkOptimization();

/** logo mark optimization <br>
 * move logo marks:
 *     - if closing credits are detected after last logo stop mark
 *     - if silence was detected before start mark or after/before end mark
 *     - if black screen marks are direct before stop mark or direct after start mark
 */
        void LogoMarkOptimization();


/**
 * cut recording based on detected marks
 */
        void MarkadCut();

#ifdef DEBUG_MARK_FRAMES
        void DebugMarkFrames();
#endif

    private:

/**
 * check for start mark
 */
        void CheckStart();

/**
 * check final start mark for invalid short start/stop pairs
 */
        void CheckStartMark();

/**
 * check for hborder end mark
 */
        cMark *Check_HBORDERSTOP();

/**
 * check for vborder end mark
 */
        cMark *Check_VBORDERSTOP();

/**
 * check for end mark
 */
        int CheckStop();

/**
 * move search for closing credits and move last logo stop mark after this
 * @return true if closing credits are found, false otherwise
 */
        bool MoveLastStopAfterClosingCredits(cMark *stopMark);

/**
 * remove all logo marks based on logo changes
 */
        void RemoveLogoChangeMarks();

/**
 * calculate position to check for start and end mark
 * @param startframe frame position of pre-timer
 */
        void CalculateCheckPositions(int startframe);

/**
 * check if timer is VPS controlled
 * @return true if timer is VPS controlled, false otherwise
 */
        bool IsVPSTimer();

/**
 * get start time of the recording from: <br>
 * -# file access time (atime) of recording directory, if the volume is mounted with noatime (no change of atime after creation)
 * -# file modification time (mtime) from VDR info file
 * @param start start time of the broadcast
 * @param fd    stream pointer to VDR info file
 * @return time of recording start
 */
        time_t GetRecordingStart(time_t start, int fd);

#if defined(DEBUG_OVERLAP_FRAME_RANGE)
/**
 * save frame, used for debug
 * @param frame  frame number
 * @param path   target path
 * @param suffix fine name suffix
 */
        void SaveFrame(const int frame, const char *path = NULL, const char *suffix = NULL);
#endif

/**
 * add a mark to marks object
 * @param mark to add to object
 */
        void AddMark(sMarkAdMark *mark);

/**
 * add or replace marks by VPS events if we have not found stronger marks than black screen marks
 * @param offset  offset from recording start of the VPS event
 * @param type    MT_START or MT_STOP
 * @param isPause true if event is VPS pause, false otherwise
 */
        void AddMarkVPS(const int offset, const int type, const bool isPause);

/**
 * reset frame counter, video and audio status
 */
        void Reset();

/**
 * check if the index is more advanced than our framecounter <br>
 * If not we wait. If we wait too much, we discard this check.
 */
        void CheckIndexGrowing();

/**
 * check if 00001.ts exists in recording directory
 * @return true if 00001.ts exists in recording directory, false otherwise
 */
        bool CheckTS();

/**
 * check if we have a logo <br>
 * - in the logo cache directory <br>
 * - in the recording directory <br>
 * - extract self from the recording
 * @return true if we found a logo, false otherwise
 */
        bool CheckLogo();

/**
 * cleanup marks that make no sense
 */
        void CheckMarks(const int endPos);

/**
 * write a separator line to log file
 * @param main true write "=", false write "-"
 */
        void LogSeparator(const bool main = false);

/**
 * write all curent detected mark to log file
 */
        void DebugMarks();

/**
 * log VDR info file
 * @return true if successful, false otherwise
 */
        bool LoadInfo();

/**
 * set user id of created files in recording directory
 * @param file filename
 * @return true if successful, false otherwise
 */
        bool SetFileUID(char *file);

/**
 * process overlap detection with stop/start pair
 * @param[in]      overlap overlap detection object
 * @param[in, out] mark1   stop mark before advertising, set to start position of detected overlap
 * @param[in, out] mark2   start mark after advertising, set to end position of detected overlap
 * @return true if overlap was detected, false otherwise
 */
        bool ProcessMarkOverlap(cMarkAdOverlap *overlap, cMark **mark1, cMark **mark2);

/**
 * process next frame
 * @param ptr_cDecoder pointer to decoder class
 * @return true if successful, false otherwise
 */
        bool ProcessFrame(cDecoder *ptr_cDecoder);

/**
 * create markad.pid file
 */
        bool CreatePidfile();

/**
 * remove markad.pid file
 */
        void RemovePidfile();


        cMarkAdVideo *video = NULL;                                    //!< detect video marks for current frame
                                                                       //!<
        cMarkAdAudio *audio = NULL;                                    //!< detect audio marks for current frame
                                                                       //!<
        cOSDMessage *osd = NULL;                                       //!< OSD message text
                                                                       //!<
        sMarkAdContext macontext = {};                                 //!< markad context
                                                                       //!<
        cMarkCriteria markCriteria;                                    //!< status of possible mark types of the broadcast
                                                                       //!<
        cVPS *vps = NULL;                                              //!< VPS events of the broadast
                                                                       //!<
        cIndex *recordingIndexMark = NULL;                             //!< pointer to recording index class
                                                                       //!<
        const char *directory;                                         //!< recording directory
                                                                       //!<
        char title[80];                                                //!< recoring title from info file
                                                                       //!<
        char *ptitle = NULL;                                           //!< title of OSD message
                                                                       //!<
        bool duplicate = false;                                        //!< true if another markad is running on the same recording
                                                                       //!<
        bool isREEL = false;                                           //!< true if markad runs on a Reelbox VDR BM2LTS (VDR info file is info.txt), false otherwise
                                                                       //!<
        int MaxFiles = 65535;                                          //!< maximum number of ts files
                                                                       //!<
        int iFrameBefore = -1;                                         //!< i-frame number before last processed i-frame number
                                                                       //!<
        int iFrameCurrent = -1;                                        //!< last processed i-frame number
                                                                       //!<
        int frameCurrent = -1;                                         //!< current processed frame number
                                                                       //!<
        int framecnt = 0;                                             //!< processed frames of 1nd pass (detect marks)
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
        int endMarkPos = 0;                                            //!< from checkStop calulated end position of the recording
                                                                       //!<
        bool iStopinBroadCast = false;                                 //!< true if we are in broadcast at iStop position, false otherwise
                                                                       //!<
        int chkSTART = 0;                                              //!< frame number to check for start mark
                                                                       //!<
        int chkSTOP = 0;                                               //!< frame number to check for end mark
                                                                       //!<
        bool inBroadCast = false;                                      //!< true if are we in a broadcast, false if we are in advertising
                                                                       //!<
        char *indexFile = NULL;                                        //!< file name of the VDR index file
                                                                       //!<
        int sleepcnt = 0;                                              //!< count of sleeps to wait for new frames when decode during recording
                                                                       //!<
        cMarks marks;                                                  //!< objects with all marks
                                                                       //!<
        cMarks blackMarks;                                             //!< objects with all blackscreen marks
                                                                       //!<
        cDecoder *ptr_cDecoderLogoChange = NULL;                       //!< pointer to class cDecoder, used as second instance to detect logo changes
                                                                       //!<
        cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = NULL;  //!< pointer to class cEvaluateLogoStopStartPair
                                                                       //!<
        bool checkAudio = false;                                       //!< set to true after each i-Frame, reset to false after audio channel check
                                                                       //!<
};
#endif
