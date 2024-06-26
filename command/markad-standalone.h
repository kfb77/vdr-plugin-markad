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
#include "tools.h"
#include "global.h"
#include "video.h"
#include "audio.h"
#include "marks.h"
#include "encoder.h"
#include "evaluate.h"
#include "osd.h"
#include "criteria.h"
#include "vps.h"

/* forward declarations */
class cOSDMessage;

// valid lover border length
#define MIN_LOWER_BORDER  881
#define MAX_LOWER_BORDER 11000

// max distance for valid mark from assumed start/stop
#define MAX_ASSUMED 300

/**
 * markad main class
 */
class cMarkAdStandalone : protected cEvaluateChannel {
public:

    /**
     * markad main constructor
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
        ptitle                    = title;
        directory                 = origin.directory;
        video                     = nullptr;
        audio                     = nullptr;
        osd                       = nullptr;
        evaluateLogoStopStartPair = nullptr;
        duplicate                 = origin.duplicate,
        MaxFiles                  = origin.MaxFiles;
        framecnt                  = origin.framecnt;
        gotendmark                = origin.gotendmark;
        waittime                  = origin.waittime;
        iwaittime                 = origin.iwaittime;
        bLiveRecording            = origin.bLiveRecording;
        chkSTART                  = origin.chkSTART;
        chkSTOP                   = origin.chkSTOP;
        inBroadCast               = origin.inBroadCast;
        indexFile                 = origin.indexFile;
        ptr_cDecoderLogoChange    = origin.ptr_cDecoderLogoChange;
        iStopinBroadCast          = origin.iStopinBroadCast;
        endMarkPos                = origin.endMarkPos;
        iStopA                    = origin.iStopA;
        iStartA                   = origin.iStartA;
        iStop                     = origin.iStop;
        iStart                    = origin.iStart;
        length                    = origin.length;
        startTime                 = origin.startTime;
        macontext                 = origin.macontext;
        vps                       = nullptr;
        recordingIndexMark        = origin.recordingIndexMark;
        iFrameBefore              = origin.iFrameBefore;
        iFrameCurrent             = origin.iFrameCurrent;
        frameCurrent              = origin.frameCurrent;
        checkAudio                = origin.checkAudio;
        sleepcnt = origin.sleepcnt;
    };

    /**
     * operator=, not used, only for formal reason
     */
    cMarkAdStandalone &operator =(const cMarkAdStandalone *origin) {
        strcpy(title,origin->title);
        ptitle                    = title;
        directory                 = origin->directory;
        video                     = nullptr;
        audio                     = nullptr;
        osd                       = nullptr;
        duplicate                 = origin->duplicate,
        MaxFiles                  = origin->MaxFiles;
        framecnt                  = origin->framecnt;
        gotendmark                = origin->gotendmark;
        waittime                  = origin->waittime;
        iwaittime                 = origin->iwaittime;
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
        checkAudio                = origin->checkAudio;
        startTime                 = origin->startTime;
        iStopinBroadCast          = origin->iStopinBroadCast;
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

    /** logo mark optimization <br>
     * move logo marks:
     *     - if closing credits are detected after last logo stop mark
     *     - if silence was detected before start mark or after/before end mark
     *     - if black screen marks are direct before stop mark or direct after start mark
     */
    void LogoMarkOptimization();

    /**
     * optimize marks based on black screen
     */
    void BlackScreenOptimization();

    /**
     * optimize marks based on lower border
     */
    void LowerBorderOptimization();

    /**
     * optimize marks based on mute scene
     */
    void SilenceOptimization();

    /**
     * optimize marks based on schene changes
     */
    void SceneChangeOptimization();

    /**
     * cut recording based on detected marks
     */
    void MarkadCut();

#ifdef DEBUG_MARK_FRAMES
    void DebugMarkFrames();
#endif

private:
    /**
     * swap aspect ratio marks
     */
    void SwapAspectRatio();

    /**
     * check if there is a opening/closing logo sequence from kabel eins
     * @param mark check this mark
     * @return true if separator found
     */
    bool HaveInfoLogoSequence(const cMark *mark);

    /**
     * check if separator slience before logo start mark
     * @param mark check this mark
     * @return true if separator found
     */
    bool HaveSilenceSeparator(const cMark *mark);

    /**
     * check if closing credit with lower border form previous broadcast before logo start mark
     * @param mark check this mark
     * @return true if closing credits with lower border from broadcast before found
     */
    bool HaveLowerBorder(const cMark *mark);

    /**
     * check if separator black screen before logo start mark / around logo stop mark
     * @param mark check this mark
     * @return true if separator found
     */
    bool HaveBlackSeparator(const cMark *mark);

    /**
     * check for start mark
     */
    void CheckStart();

    /**
     * check final start mark for invalid short start/stop pairs
     */
    void CheckStartMark();

    /**
     * check for channel start mark
     */
    cMark *Check_CHANNELSTART();

    /**
     * check for logo start mark
     */
    cMark *Check_LOGOSTART();

    /**
     * check for hborder start mark
     */
    cMark *Check_HBORDERSTART();

    /**
     * check for vborder start mark
     */
    cMark *Check_VBORDERSTART(const int maxStart);

    /**
     * check for channel end mark
     */
    cMark *Check_CHANNELSTOP();

    /**
     * check for hborder end mark
     */
    cMark *Check_HBORDERSTOP();

    /**
     * check for vborder end mark
     */
    cMark *Check_VBORDERSTOP();

    /**
     * check for logo end mark
     */
    cMark *Check_LOGOSTOP();

    /**
     * cleanup undetected info logo before end mark
     */
    void CleanupUndetectedInfoLogo(const cMark *mark);

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
     * @param start time of the broadcast start
     * @param fd    stream pointer to VDR info file
     * @return time of recording start
     */
    time_t GetRecordingStart(time_t start, int fd);

#if defined(DEBUG_OVERLAP_FRAME_RANGE)
    /**
     * save frame, used for debug
     * @param frame  framenumber
     * @param path   target path
     * @param suffix fine name suffix
     */
    void SaveFrame(const int frame, const char *path = nullptr, const char *suffix = nullptr);
#endif

    /**
     * add a mark to marks object
     * @param mark to add to object
     */
    void AddMark(sMarkAdMark *mark);

    /**
     * add or replace marks by VPS events if we have not found stronger marks than black screen marks
     * @param offset  recording start offset of the VPS event
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
    void CheckMarks(const int endMarkPos);

    /**
     * write all curent detected mark to log file
     */
    void DebugMarks();

    /**
     * log VDR info file
     */
    void LoadInfo();

    /**
     * set user id of created files in recording directory
     * @param file filename
     * @return true if successful, false otherwise
     */
    bool SetFileUID(char *file);

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


    cMarkAdVideo *video = nullptr;                                    //!< detect video marks for current frame
    //!<
    cMarkAdAudio *audio = nullptr;                                    //!< detect audio marks for current frame
    //!<
    cOSDMessage *osd = nullptr;                                       //!< OSD message text
    //!<
    sMarkAdContext macontext = {};                                 //!< markad context
    //!<
    cCriteria criteria;                                           //!< status of possible mark types of the broadcast
    //!<
    cVPS *vps = nullptr;                                              //!< VPS events of the broadast
    //!<
    cIndex *recordingIndexMark = nullptr;                             //!< pointer to recording index class
    //!<
    const char *directory;                                         //!< recording directory
    //!<
    char title[80];                                                //!< recoring title from info file
    //!<
    char *ptitle = nullptr;                                           //!< title of OSD message
    //!<
    bool duplicate = false;                                        //!< true if another markad is running on the same recording
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
    int endMarkPos = 0;                                            //!< from checkStop calculated end position of the recording
    //!<
    bool iStopinBroadCast = false;                                 //!< true if we are in broadcast at iStop position, false otherwise
    //!<
    int chkSTART = 0;                                              //!< frame number to check for start mark
    //!<
    int chkSTOP = 0;                                               //!< frame number to check for end mark
    //!<
    bool inBroadCast = false;                                      //!< true if are we in a broadcast, false if we are in advertising
    //!<
    char *indexFile = nullptr;                                        //!< file name of the VDR index file
    //!<
    int sleepcnt = 0;                                              //!< count of sleeps to wait for new frames when decode during recording
    //!<
    cMarks marks;                                                  //!< objects with all strong marks
    //!<
    cMarks sceneMarks;                                             //!< objects with all scene change marks
    //!<
    cMarks silenceMarks;                                           //!< objects with all mute scene marks
    //!<
    cMarks blackMarks;                                             //!< objects with all black screen marks
    //!<
    cDecoder *ptr_cDecoderLogoChange = nullptr;                       //!< pointer to class cDecoder, used as second instance to detect logo changes
    //!<
    cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = nullptr;  //!< pointer to class cEvaluateLogoStopStartPair
    //!<
    bool checkAudio = false;                                       //!< set to true after each i-Frame, reset to false after audio channel check
    //!<
};
#endif
