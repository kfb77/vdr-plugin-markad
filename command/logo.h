/*
 * logo.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __logo_h_
#define __logo_h_

#include "global.h"
#include "debug.h"
#include "tools.h"
#include "video.h"
#include "decoder.h"
#include "audio.h"
#include "sobel.h"
#include "index.h"
#include "sobel.h"

#define MAXREADFRAMES 3000

#define TOP_LEFT     0
#define TOP_RIGHT    1
#define BOTTOM_LEFT  2
#define BOTTOM_RIGHT 3

#define LOGO_ERROR  -1
#define LOGO_FOUND   0

/**
 * logo after sobel transformation
 */
struct sLogoInfo {
    int frameNumber = -1;   //!< frame number of the logo
    //!<

    int hits = 0;            //!< number of similar other logos
    //!<

    uchar **sobel = nullptr;    //!< sobel transformed corner picture data
    //!<
};


/**
 * class to extract logo from recording
 */
class cExtractLogo : protected cTools {
public:

    /**
     * constructor for class to search end extract logo from recording
     * @param recDirParam          recording directory
     * @param channelNameParam     channel name
     * @param threads              count of FFmpeg threads
     * @param hwaccel              device type of hwaccel
     * @param forceHW              force hwaccel for MPEG2 codec
     * @param requestedAspectRatio video aspect ratio for requested logo
     */
    explicit cExtractLogo(const char *recDirParam, const char *channelNameParam, const int threads, char *hwaccel, const bool forceHW, const sAspectRatio requestedAspectRatio);
    ~cExtractLogo();

    /**
     * copy constructor
     */
    cExtractLogo(const cExtractLogo &origin) {
        recDir              = nullptr;
        channelName         = nullptr;
        decoder             = nullptr;
        criteria            = nullptr;
        sobel               = nullptr;
        hBorder             = nullptr;
        vborder             = nullptr;
        recordingFrameCount = origin.recordingFrameCount;
        audioState          = origin.audioState;
        iFrameCountValid    = origin.iFrameCountValid;
        memcpy(aCorner, origin.aCorner, sizeof(origin.aCorner));
        for (int i = 0; i < CORNERS; i++) {
            logoInfoVector[i]   = origin.logoInfoVector[i];
        }
    }

    /**
     * operator=
     */
    cExtractLogo &operator =(const cExtractLogo *origin) {
        recDir              = nullptr;
        channelName         = nullptr;
        decoder             = nullptr;
        criteria            = nullptr;
        sobel               = nullptr;
        hBorder             = nullptr;
        vborder             = nullptr;
        recordingFrameCount = origin->recordingFrameCount;
        audioState          = origin->audioState;
        iFrameCountValid    = origin->iFrameCountValid;
        memcpy(aCorner, origin->aCorner, sizeof(origin->aCorner));
        for (int i = 0; i < CORNERS; i++) {
            logoInfoVector[i]   = origin->logoInfoVector[i];
        }
        return *this;
    }


    /**
     * get video frame rate from decoder
     * @return frame rate
     */
    int GetFrameRate();

    /**
     * search and extract logo from recording
     * @param startPacket   frame number to start search
     * @param force         finding a logo, even on weak matches
     * @return last read frame during search
     */
    int SearchLogo(int startPacket, const bool force);

    /**
     * compare logo pair
     * @param logo1      pixel map of logo 1
     * @param logo2      pixel map of logo 2
     * @param logoHeight logo height
     * @param logoWidth  logo width
     * @param corner     logo corner
     * @param match0     minimum requested rate of similars in pane 0 to thread as similar
     * @param match12    minimum requested rate of similars in pane 1 and 2 to thread as similar
     * @param[out] rate0 match rate of the two logos
     * @return true if logo pair is similar, false otherwise
     */
    bool CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int match0 = 0, int match12 = 0, int *rate0 = nullptr);

    /**
     * manually extrct logo from recording
     * @param corner  video picture corner
     * @param width   logo width
     * @param height  logo height
     */
    void ManuallyExtractLogo(const int corner, const int width, const int height);

private:
    /**
     * save logo picture, used for debugging
     * @param actLogoInfo logo pixel map
     * @param logoSizeFinal   logo size of final selected logo
     * @param logoAspectRatio logo from this video aspect ratio
     * @param corner          logo corner
     * @return                true if saved successful, false otherwise
     */
    bool SaveLogo(const sLogoInfo *actLogoInfo, sLogoSize *logoSizeFinal, const sAspectRatio logoAspectRatio, const int corner);

    /**
     * check if logo is valid
     * @param actLogoInfo logo pixel map
     * @param corner          logo corner
     * @return                true if logo is valid
     */
    bool CheckValid(const sLogoInfo *actLogoInfo, const int corner);

    /**
     * compare logo with all other in list
     * @param actLogoInfo pixel map of logo
     * @param logoHeight      logo height
     * @param logoWidth       logo width
     * @param corner          logo corner
     * @return                true if logo pair is similar, false otherwise
     */
    int Compare(sLogoInfo *actLogoInfo, const int logoHeight, const int logoWidth, const int corner);

    /**
     * compare rotating logo pair
     * @param logo1      pixel map of logo 1
     * @param logo2      pixel map of logo 2
     * @param logoHeight logo height
     * @param logoWidth  logo width
     * @param corner     logo corner
     * @return true if logo pair is similar, false otherwise
     */
    bool CompareLogoPairRotating(sLogoInfo *logo1, sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner);

    /**
     * cut logo picture
     * @param logoInfo              logo pixel map
     * @param cutPixelH             number of pixel to cut off horizontal
     * @param cutPixelV             number of pixel ro cut off vertical
     * @param[in,out] logoSizeFinal logo size of final setected logo
     * @param corner                logo corner
     */
    void CutOut(sLogoInfo *logoInfo, int cutPixelH, int cutPixelV, sLogoSize *logoSizeFinal, const int corner) const;

    /**
     * check if found logo size and corner is valid
     * @param logoSizeFinal final logo size
     * @param logoCorner    corner of logo
     * @return              true if logo size and corner is valid, false otherwise
     */
    bool CheckLogoSize(sLogoSize *logoSizeFinal, const int logoCorner);

    /**
     * remove white frame and resize logo
     * @param bestLogoInfo          logo pixel map
     * @param[in,out] logoSizeFinal size of final setected logo
     * @param bestLogoCorner        logo corner
     * @return                      true if successful, false otherwise
     */
    bool Resize(sLogoInfo *bestLogoInfo, sLogoSize *logoSizeFinal, const int bestLogoCorner);

    /**
     * check of plane has pixel
     * @param actLogoInfo logo pixel
     * @param logoSizePlane   logo size of this plane
     * @param plane           pixel plane number
     * @return                true if there are no pixel, false otherwise
     */
    static bool IsWhitePlane(const sLogoInfo *actLogoInfo, const sLogoSize logoSizePlane, const int plane);

    /**
     * check of logo had a changed colour
     * @param logoSizeFinal  final size of selected logo
     * @param corner         logo corner
     * @param plane          number of plane
     * @return               true if logo changed colour, false otherwise
     */
    bool IsLogoColourChange(sLogoSize *logoSizeFinal, const int corner, const int plane);

    /**
     * delete frames from logo list
     * @param from      start frame to delete from
     * @param to        end frame
     * @return          number of deleted frames
     */
    int DeleteFrames(const int from, const int to);

    /**
     * wait for more frames if markad runs during recording
     * @param decoder   pointer to decoder
     * @param minFrame  minimum framenumber we need
     * @return          true if we have enough frames, false otherwise
     */
    bool WaitForFrames(cDecoder *decoder, const int minFrame);

    /**
     * get first frame number of stored logos
     * @return first frame number of stored logos
     */
    int GetFirstFrame();

    /**
     * get last frame number of stored logos
     * @return last frame number of stored logos
     */
    int GetLastFrame();

    /**
     * count of stored logo frames
     * @return count of stored logo frames
     */
    int CountFrames();

    /**
     * remove single pixel defect in logo
     * @param [in,out] logoInfo logo pixel map
     * @param corner            logo corner
     */
    void RemovePixelDefects(sLogoInfo *logoInfo, const int corner);

    /**
     * check audio channel status
     * @return  0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
     */
    int AudioInBroadcast();

    const char *recDir                    = nullptr;      //!< recording directory
    //!<
    const char *channelName               = nullptr;      //!< channel name, used for logo file name
    //!<
    cDecoder *decoder                     = nullptr;      //!< pointer to decoder
    //!<
    cCriteria *criteria                   = nullptr;      //!< channel criteria for logo detection
    //!<
    sAreaT area                           = {};           //!< sobel transformed pixels of logo area
    //!<
    cSobel *sobel                         = nullptr;      //!< pointer to sobel transformation
    //!<
    cHorizBorderDetect *hBorder           = nullptr;      //!< pointer to hBorder detection
    //!<
    cVertBorderDetect *vborder            = nullptr;      //!< pointer to hBorder detection
    //!<
    int recordingFrameCount               = 0;            //!< frame count of the recording
    //!<
    sAspectRatio requestedLogoAspectRatio = {0};          //!< aspect ratio of requested logo
    //!<
    int audioState                        = 0;            //!< 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
    //!<
    int iFrameCountValid                  = 0;            //!< number of valid i-frames
    //!<
    const char *aCorner[CORNERS]          = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" }; //!< array to transform enum corner to text
    //!<
    std::vector<sLogoInfo> logoInfoVector[CORNERS];   //!< infos of all proccessed logos
    //!<


};
#endif
