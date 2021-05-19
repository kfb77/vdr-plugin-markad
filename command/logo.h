/*
 * logo.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __logo_h_
#define __logo_h_

#include "global.h"
#include "markad-standalone.h"
#include "decoder_new.h"
#include "video.h"
#include "audio.h"

#define MAXREADFRAMES 3000

#define TOP_LEFT 0
#define TOP_RIGHT 1
#define BOTTOM_LEFT 2
#define BOTTOM_RIGHT 3

/**
 * logo after sobel transformation
 */
struct sLogoInfo {
    int iFrameNumber = -1;   //!< frame number of the logo
                             //!<

    int hits = 0;            //!< number of similar other logos
                             //!<

    uchar **sobel = NULL;    //!< sobel transformed corner picture data
                             //!<

    bool valid[PLANES] = {}; //!< <b>true:</b> data planes contain valid data <br>
                             //!< <b>false:</b> data planes are not valid
                             //!<
};


/**
 * class to extract logo from recording
 */
class cExtractLogo : public cLogoSize {
    public:

/**
 * constuctor for class to search end extract logo from recording
 * @param maContext      markad context
 * @param aspectRatio    video aspect ratio for requested logo
 * @param recordingIndex recording index
 */
        explicit cExtractLogo(sMarkAdContext *maContext, const sAspectRatio aspectRatio, cIndex *recordingIndex);
        ~cExtractLogo();

/**
 * search and extract logo from recording
 * @param maContext  markad context
 * @param startFrame frame number to start search
 * @return last read frame during search
 */
        int SearchLogo(sMarkAdContext *maContext, int startFrame);

/**
 * get default logo size
 * @param maContext       markad context
 * @param[out] logoHeight default logo height
 * @param[out] logoWidth  default logo width
 */
        void GetLogoSize(const sMarkAdContext *maContext, int *logoHeight, int *logoWidth);

/**
 * compair logo pair
 * @param logo1      pixel map of logo 1
 * @param logo2      pixel map of logo 2
 * @param logoHeight logo height
 * @param logoWidth  logo width
 * @param corner     logo corner
 * @param match0     minimun requested rate of similars in pane 0 to thread as similar
 * @param match12    minimun requested rate of similars in pane 1 and 2 to thread as similar
 * @param[out] rate0 match rate of the two logos
 * @return true if logo pair is similar, false otherwise
 */
        bool CompareLogoPair(const sLogoInfo *logo1, const sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner, int match0 = 0, int match12 = 0, int *rate0 = NULL);

/**
 * request programm abort
 */
        void SetAbort() {
            abort = true;
        };

        bool abort = false;  //!< true if programm abort is requestet, false otherwise
                             //!<
    private:
/**
 * save logo picture, used for debugging
 * @param maContext       markad context
 * @param ptr_actLogoInfo logo pixel map
 * @param logoHeight      logo height
 * @param logoWidth       logo width
 * @param corner          logo corner
 * @param framenumber     frame number
 * @param debugText       debug messaged appended to file name
 * @return true if saved successful, false otherwise
 */
        bool Save(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner, const int framenumber,  const char *debugText);

/**
 * check if logo is valid
 * @param maContext       markad context
 * @param ptr_actLogoInfo logo pixel map
 * @param logoHeight      logo heigth
 * @param logoWidth       logo width
 * @param corner          logo corner
 * @return true if logo is valif
 */
        bool CheckValid(const sMarkAdContext *maContext, const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);

/**
 * compair logo with all other in list
 * @param maContext       markad context
 * @param ptr_actLogoInfo pixel map of logo
 * @param logoHeight      logo height
 * @param logoWidth       logo width
 * @param corner          logo corner
 * @return true if logo pair is similar, false otherwise
 */
        int Compare(const sMarkAdContext *maContext, sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int corner);

/**
 * compair rotating logo pair
 * @param maContext  markad context
 * @param logo1      pixel map of logo 1
 * @param logo2      pixel map of logo 2
 * @param logoHeight logo height
 * @param logoWidth  logo width
 * @param corner     logo corner
 * @return true if logo pair is similar, false otherwise
 */
        bool CompareLogoPairRotating(const sMarkAdContext *maContext, sLogoInfo *logo1, sLogoInfo *logo2, const int logoHeight, const int logoWidth, const int corner);

/**
 * cut logo picture
 * @param logoInfo           logo pixel map
 * @param cutPixelH          number of pixel to cut off horizontal
 * @param cutPixelV          number of pixel ro cut off vertical
 * @param[in,out] logoHeight logo height
 * @param[in,out] logoWidth  logo width
 * @param corner             logo corner
 */
        void CutOut(sLogoInfo *logoInfo, int cutPixelH, int cutPixelV, int *logoHeight, int *logoWidth, const int corner);

/**
 * check if found logo size and corner is valid
 * @param maContext  markad context
 * @param logoHeight logo height
 * @param logoWidth  logo width
 * @param logoCorner corner of logo
 * @return true if logo size and corner is valid, false otherwise
 */
        bool CheckLogoSize(const sMarkAdContext *maContext, const int logoHeight, const int logoWidth, const int logoCorner);

/**
 * remove white frame and resize logo
 * @param maContext          markad context
 * @param bestLogoInfo       logo pixel map
 * @param[in,out] logoHeight logo height
 * @param[in,out] logoWidth  logo width
 * @param bestLogoCorner     logo corner
 * @return true if successful, false otherwise
 */
        bool Resize(const sMarkAdContext *maContext, sLogoInfo *bestLogoInfo, int *logoHeight, int *logoWidth, const int bestLogoCorner);

/**
 * check of plane has pixel
 * @param ptr_actLogoInfo logo pixel
 * @param logoHeight      logo heigth
 * @param logoWidth       logo width
 * @param plane           pixel plane number
 * @return true if there are no pixel, false otherwise
 */
        bool IsWhitePlane(const sLogoInfo *ptr_actLogoInfo, const int logoHeight, const int logoWidth, const int plane);

/**
 * check of logo changed colour
 * @param maContext markad context
 * @param corner    logo corner
 * @return true if logo changed colour, false otherwise
 */
        bool IsLogoColourChange(const sMarkAdContext *maContext, const int corner);

/**
 * delete frames from logo list
 * @param maContext markad context
 * @param from      start frame to delete from
 * @param to        end frame
 * @return          number of deleted frames
 */
        int DeleteFrames(const sMarkAdContext *maContext, const int from, const int to);

/**
 * wait for more frames if markad runs during recording
 * @param maContext    markad context
 * @param ptr_cDecoder decoder
 * @param minFrame     minimum framenumber we need
 * @return true if we have enought frames, false otherwise
 */
        bool WaitForFrames(sMarkAdContext *maContext, cDecoder *ptr_cDecoder, const int minFrame);

/**
 * get first frame number of stored logos
 * @param maContext markad context
 * @return first frame number of stored logos
 */
        int GetFirstFrame(const sMarkAdContext *maContext);

/**
 * get last frame number of stored logos
 * @param maContext markad context
 * @return last frame number of stored logos
 */
        int GetLastFrame(const sMarkAdContext *maContext);

/**
 * count of stored logo frames
 * @param maContext markad context
 * @return count of stored logo frames
 */
        int CountFrames(const sMarkAdContext *maContext);

/**
 * remove single pixel defect in logo
 * @param maContext         markad context
 * @param [in,out] logoInfo logo pixel map
 * @param logoHeight        logo height
 * @param logoWidth         logo width
 * @param corner            logo corner
 */
        void RemovePixelDefects(const sMarkAdContext *maContext, sLogoInfo *logoInfo, const int logoHeight, const int logoWidth, const int corner);

/**
 * check audio channel status
 * @param maContext markad context
 * @param iFrameNumber   i-frame number
 * @return  0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
 */
        int AudioInBroadcast(const sMarkAdContext *maContext, const int iFrameNumber);

        sMarkAdContext *maContextLogoSize = NULL;         //!< markad context
                                                          //!<
        cIndex *recordingIndexLogo = NULL;                //!< recording index
                                                          //!<
        std::vector<sLogoInfo> logoInfoVector[CORNERS];   //!< infos of all proccessed logos
                                                          //!<
        int recordingFrameCount = 0;                      //!< frame count of the recording
                                                          //!<
        sAspectRatio logoAspectRatio = {};                //!< video aspect ratio
                                                          //!<
        int AudioState = 0;                               //!< 0 = undefined, 1 = got first 2 channel, 2 = now 6 channel, 3 now 2 channel
                                                          //!<
        int iFrameCountValid = 0;                         //!< number of valid i-frames
                                                          //!<
        const char *aCorner[CORNERS] = { "TOP_LEFT", "TOP_RIGHT", "BOTTOM_LEFT", "BOTTOM_RIGHT" }; //!< array to transform enum corner to text
                                                                                                   //!<


};
#endif
