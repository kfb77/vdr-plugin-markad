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
#include "marks.h"
#include "encoder.h"
#include "osd.h"
#include "criteria.h"
#include "vps.h"
#include "version.h"
#include "logo.h"
#include "index.h"
#include "overlap.h"
#include "decoder.h"
#include "evaluate.h"
#include "video.h"

/* forward declarations */
class cOSDMessage;

// valid lover border length
#define MIN_LOWER_BORDER  1081    // changed from 881 to 1081
#define MAX_LOWER_BORDER 16400    // longest lower border found 16400

// max distance for valid mark from assumed start/stop
#define MAX_ASSUMED 300


/**
 * markad configuration structure
 */
typedef struct sMarkAdConfig {
    char logFile[20]               = {};       //!< name of the markad log file
    //!<
    char logoCacheDirectory[1024]  = {};       //!< logo cache directory (default /var/lib/markad)
    //!<
    char markFileName[255]         = {};       //!< name of the marks file (default marks)
    //!<
    char svdrphost[1024]           = {};       //!< ip or name of vdr server (default localhost)
    //!<
    int svdrpport                  = 0;        //!< vdr svdrp port number
    //!<
    int logoExtraction             = false;    //!< <b>true:</b> extract logo and store to /tmp <br>
    //!< <b>false:</b> normal markad operation
    //!<
    int logoWidth                  = 0;        //!< width for logo extractions
    //!
    int logoHeight                 = 0;        //!< height for logo extraction
    //!<
    int threads                    = 0;        //!< number of threads for decoder and encoder
    //!<
    bool useVPS                    = false;    //!< <b>true:</b> use information from vps file to optimize marks
    //!< <b>false:</b> do not use information from vps file to optimize marks
    bool MarkadCut                 = false;    //!< cut video after mark detection
    //!<
    bool ac3ReEncode               = false;    //!< re-encode AC3 stream and adapt audio volume
    //!<
    int autoLogo                   = 2;        //!< 0 = off, 1 = deprecated, 2 = on
    //!<
    const char *cmd                = nullptr;  //!< cmd parameter
    //!<
    const char *recDir             = nullptr;  //!< name of the recording directory
    //!<
    bool backupMarks               = false;    //!< <b>true:</b> backup marks file before override <br>
    //!< <b>false:</b> do not backup marks file
    //!<
    bool noPid                     = false;    //!< <b>true:</b> do not write a PID file <br>
    //!< <b>false:</b> write a PID file
    //!<
    bool osd                       = false;    //!< <b>true:</b> send screen messages to vdr <br>
    //!< <b>false:</b> do not send screen messages to vdr
    //!<
    int online                     = 0;        //!< start markad immediately when called with "before" as cmd
    //!< if online is 1, markad starts online for live-recordings
    //!< only, online=2 starts markad online for every recording
    //!< live-recordings are identified by having a '@' in the
    //!< filename so the entry 'Mark instant recording' in the menu
    //!< Setup - Recording of the vdr should be set to 'yes'
    //!<
    bool before                    = false;    //!< <b>true:</b> markad started by vdr before the recording is complete, only valid together with --online <br>
    //!<
    bool fullDecode                = false;    //!< <b>true:</b> decode all video frames <br>
    //!< <b>false:</b> decode only iFrames
    //!<
    bool forcedFullDecode          = false;    //!< true: full decoding was forced bacause of video codec
    //!<
    bool smartEncode               = false;    //!< <b>true:</b> re-encode only frames arounf cut position <br>
    //!<
    bool fullEncode                = false;    //!< <b>true:</b> full re-encode all frames, cut on all frame types <br>
    //!< <b>false:</b> copy frames without re-encode, cut on iframe position
    //!<
    bool bestEncode                = true;     //!< <b>true:</b> encode all video and audio streams <br>
    //!< <b>false:</b> encode all video and audio streams
    //!<
    bool pts                       = false;    //!< <b>true:</b> add pts based timestanp to marks<br>
    //!< <b>false:</b> otherwise
    char hwaccel[16]               = {0};      //!< hardware acceleration methode
    //!<
    bool forceHW                   = false;    //!< force hwaccel for MPEG2
    //!<
    bool forceInterlaced           = false;    //!< inform decoder who use hwaccel, the video is interlaced. In this case not possible to detect from decoder because HW deinterlaces
    //!
    bool perftest                  = false;    //!< <b>true:</b>  run decoder performance test before detect marks<br>
    //!< <b>false:</b> otherwise
} sMarkAdConfig;


/**
 * markad context structure
 */
typedef struct sMarkAdContext {
    sMarkAdConfig *Config; //!< markad configuration
    //!<

    /**
     * global markad state structure
     */
    struct sInfo {
        bool isRunningRecording        = false;    //!< <b>true:</b> markad is running during recording <br>
        //!< <b>false:</b>  markad is running after recording
        //!<
        int tStart                     = -1;       //!< offset of timer start to recording start (pre timer)
        //!<
        bool startVPS                  = false;    //!< tStart is from VPS start event
        //!<
        sAspectRatio AspectRatio       = {0};      //!< set from info file and checked after chkSTART, valid for the broadcast
        //!<
        char *ChannelName              = nullptr;  //!< name of the channel
        //!<
    } Info; //!< global markad state infos
    //!<
} sMarkAdContext;


/**
 * markad main class
 */
class cMarkAdStandalone : private cTools {
public:

    /**
     * markad main constructor
     * @param directoryParam recording directory
     * @param config         markad context configuration
     */
    cMarkAdStandalone(const char *directoryParam, sMarkAdConfig *config);
    ~cMarkAdStandalone();

    /**
     * copy constructor
     */
    cMarkAdStandalone(const cMarkAdStandalone &origin) {   //  copy constructor
        strcpy(title, origin.title);
        decoder                   = nullptr;
        index                     = nullptr;
        criteria                  = nullptr;
        startA                    = origin.startA;
        stopA                     = origin.stopA;
        packetCheckStart          = origin.packetCheckStart;
        ptitle                    = title;
        directory                 = origin.directory;
        video                     = nullptr;
        audio                     = nullptr;
        osd                       = nullptr;
        evaluateLogoStopStartPair = nullptr;
        duplicate                 = origin.duplicate,
        framecnt                  = origin.framecnt;
        waittime                  = origin.waittime;
        iwaittime                 = origin.iwaittime;
        bLiveRecording            = origin.bLiveRecording;
        inBroadCast               = origin.inBroadCast;
        indexFile                 = origin.indexFile;
        iStopinBroadCast          = origin.iStopinBroadCast;
        length                    = origin.length;
        startTime                 = origin.startTime;
        macontext                 = origin.macontext;
        vps                       = nullptr;
        checkAudio                = origin.checkAudio;
        detectLogoStopStart       = origin.detectLogoStopStart;
        doneCheckStop             = origin.doneCheckStop;
        doneCheckStart            = origin.doneCheckStart;
        packetEndPart             = origin.packetEndPart;
        packetCheckStop           = origin.packetCheckStop;
        extractLogo               = nullptr;
        sleepcnt                  = origin.sleepcnt;
    };

    /**
     * operator=
     */
    cMarkAdStandalone &operator =(const cMarkAdStandalone *origin) {
        strcpy(title,origin->title);
        decoder                   = nullptr;
        index                     = nullptr;
        criteria                  = nullptr;
        startA                    = origin->startA;
        stopA                     = origin->stopA;
        packetCheckStart          = origin->packetCheckStart;
        ptitle                    = title;
        directory                 = origin->directory;
        video                     = nullptr;
        audio                     = nullptr;
        osd                       = nullptr;
        duplicate                 = origin->duplicate,
        framecnt                  = origin->framecnt;
        waittime                  = origin->waittime;
        iwaittime                 = origin->iwaittime;
        bLiveRecording            = origin->bLiveRecording;
        inBroadCast               = origin->inBroadCast;
        indexFile                 = origin->indexFile;
        sleepcnt                  = origin->sleepcnt;
        macontext                 = origin->macontext;
        length                    = origin->length;
        evaluateLogoStopStartPair = origin->evaluateLogoStopStartPair;
        vps                       = origin->vps;
        checkAudio                = origin->checkAudio;
        startTime                 = origin->startTime;
        iStopinBroadCast          = origin->iStopinBroadCast;
        detectLogoStopStart       = origin->detectLogoStopStart;
        doneCheckStop             = origin->doneCheckStop;
        doneCheckStart            = origin->doneCheckStart;
        packetCheckStop           = origin->packetCheckStop;
        packetEndPart             = origin->packetEndPart;
        extractLogo               = origin->extractLogo;
        return *this;
    }

    /**
     * process all ts files and detect marks
     */
    void Recording();

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
     * @return true, if later start mark was set
     */
    bool CheckStartMark();

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
     * check for aspect ratio end mark
     */
    cMark *Check_ASPECTSTOP();

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
    void CheckStop();

    /**
     * move search for closing credits and move last logo stop mark after this
     * @return true if closing credits are found, false otherwise
     */
    bool MoveLastStopAfterClosingCredits(cMark *stopMark);

    /**
     * remove all logo marks based on logo changes
     */
    void RemoveLogoChangeMarks(const bool checkStart);

    /**
     * calculate position to check for start and end mark
     * @param startFrame frame position of pre-timer or VPS start event
     */
    void CalculateCheckPositions(int startFrame);

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
     */
    void AddMarkVPS(const int offset, const int type);

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
    bool CheckLogo(const int frameRate);

    /**
     * cleanup marks that make no sense
     */
    void CheckMarks();

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
     * @return true if successful, false otherwise
     */
    bool ProcessFrame();

    /**
     * create markad.pid file
     */
    bool CreatePidfile();

    /**
     * remove markad.pid file
     */
    void RemovePidfile();

    cIndex *index                    = nullptr;  //!< pointer to index object
    //!
    cDecoder *decoder                = nullptr;  //!< pointer to main decoder
    //!
    cCriteria *criteria              = nullptr;  //!< status of possible mark types of the broadcast
    //!<
    cExtractLogo *extractLogo        = nullptr;  //!< pointer to class to extract logo from recording
    //!<
    cVideo *video                    = nullptr;  //!< detect video marks for current frame
    //!<
    cAudio *audio                    = nullptr;  //!< detect audio marks for current frame
    //!<
    cOSDMessage *osd                 = nullptr;  //!< OSD message text
    //!<
    sMarkAdContext macontext         = {};       //!< markad context
    //!<
    cVPS *vps                        = nullptr;  //!< VPS events of the broadast
    //!<
    const char *directory            = nullptr;  //!< recording directory
    //!<
    char title[80]                   = {0};      //!< recoring title from info file
    //!<
    char *ptitle                     = nullptr;  //!< title of OSD message
    //!<
    bool duplicate                   = false;    //!< true if another markad is running on the same recording
    //!<
    int framecnt                     = 0;        //!< processed frames of 1nd pass (detect marks)
    //!<
    int waittime                     = 0;        //!< time waited for more frames if markad runs during recording
    //!<
    int iwaittime                    = 0;        //!< time waited for continuation of interrupted recording
    //!<
    bool bLiveRecording              = false;    //!< true if markad was started during recording, false otherwise
    //!<
    time_t startTime                 = 0;        //!< start time of the broadcast
    //!<
    int length                       = 0;        //!< length of broadcast in seconds
    //!<
    int startA                       = 0;        //!< assumed start frame position
    //!<
    int stopA                        = 0;        //!< assumed end frame position
    //!<
    int packetCheckStart             = 0;        //!< packet number to check for start mark
    //!<
    int packetCheckStop              = 0;        //!< packet number to check for end mark
    //!<
    int packetEndPart                = 0;        //!< packet number of start of end part
    //!<
    bool doneCheckStart              = false;    //!< true, if CheckStart() was called
    //!<
    bool doneCheckStop               = false;    //!< true, if CheckStop() was called
    //!<
    bool iStopinBroadCast            = false;    //!< true if we are in broadcast at iStop position, false otherwise
    //!<
    bool inBroadCast                 = false;    //!< true if are we in a broadcast, false if we are in advertising
    //!<
    char *indexFile                  = nullptr;  //!< file name of the VDR index file
    //!<
    int sleepcnt                     = 0;        //!< count of sleeps to wait for new frames when decode during recording
    //!<
    cMarks marks                     = {};       //!< objects with all strong marks
    //!<
    cMarks sceneMarks                = {};       //!< objects with all scene change marks
    //!<
    cMarks silenceMarks              = {};       //!< objects with all mute scene marks
    //!<
    cMarks blackMarks                = {};       //!< objects with all black screen marks
    //!<
    bool checkAudio                  = false;    //!< set to true after each i-Frame, reset to false after audio channel check
    //!<
    cEvaluateLogoStopStartPair *evaluateLogoStopStartPair = nullptr;  //!< pointer to class cEvaluateLogoStopStartPair
    //!<
    cDetectLogoStopStart *detectLogoStopStart             = nullptr;  //!< pointer to class cDetectLogoStopStart
    //!<

    /**
     * elapsed time of section
     */
    struct sElapsedTime {
        int markDetection    = 0;    //!< elapsed time in ms for mark detection
        //!<
        int markOptimization = 0;    //!< elapsed time in ms for mark optimization
        //!<
        int logoSearch       = 0;    //!< elapsed time in ms for initial logo search and extraxtion from cache/recording directory or recording
        //!<
        int overlap          = 0;    //!< elapsed time in ms for overlap detection
        //!<
        int cut              = 0;    //!< elapsed time in ms for video cut
        //!<
        int markPictures     = 0;    //!< elapsed time in ms for debug mark position picture
        //!<
    } elapsedTime; //!< elapsed time statistics
};
#endif
