/*
 * sobel.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __sobel_h_
#define __sobel_h_

#include <cstring>

#include "global.h"
#include "debug.h"

/**
 * class to do sobel transformation
 */
class cSobel {
public:
    /**
     * constructor of sobel transformation
     */
    cSobel(const int videoWidthParam, const int videoHeightParam, const int boundaryParam);
    ~cSobel();

    /**
     * copy constructor
     */
    cSobel(const cSobel &origin) {
        memcpy(GX, origin.GX, 3 * sizeof(int));
        memcpy(GY, origin.GY, 3 * sizeof(int));
        videoWidth   = origin.videoWidth;
        videoHeight  = origin.videoHeight;
        boundary     = origin.boundary;
        rPixel       = origin.rPixel;
        iPixel       = origin.iPixel;
        intensity    = origin.intensity;
    }

    /**
     * operator=
     */
    cSobel &operator =(const cSobel *origin) {
        memcpy(GX, origin->GX, 3 * sizeof(int));
        memcpy(GY, origin->GY, 3 * sizeof(int));
        videoWidth   = origin->videoWidth;
        videoHeight  = origin->videoHeight;
        boundary     = origin->boundary;
        rPixel       = origin->rPixel;
        iPixel       = origin->iPixel;
        intensity    = origin->intensity;
        return *this;
    }

    /**
    * allocate buffer for sobel transformation result
    * @param area result area
    * @return true if sucessful, false otherwise
    */
    bool AllocAreaBuffer(sAreaT *area) const;

    /**
    * free buffer for sobel transformation result
    * @param area result area
    * @return true if sucessful, false otherwise
    */
    bool FreeAreaBuffer(sAreaT *area);

    /**
    * sobel transformation of a all planes with a logo plane from input picture
    * @param picture    input picture
    * @param area       logo mask and result of transformation and machtes
    * @param ignoreLogo ignore missing logo, use by logo search and logo mark optimization
    * @return number of planes processed
    */
#ifdef DEBUG_LOGO_DETECT_FRAME_CORNER
    int SobelPicture(const char *recDir, sVideoPicture *picture, sAreaT *area, const bool ignoreLogo);
#else
    int SobelPicture(sVideoPicture *picture, sAreaT *area, const bool ignoreLogo);
#endif

    /**
    * sobel transformation of a single plane from input picture
    * @param picture    input video picture
    * @param area       result area
    * @param plane      number of video plane
    * @return           true if successful, false otherwise
    */
    bool SobelPlane(sVideoPicture *picture, sAreaT *area, const int plane);

    /**
     * calculate coordinates of logo position (values for array index, from 0 to (Video.Info.width - 1) or (Video.Info.height)
     * @param[out] area   result area
     * @param[out] plane  video plane number
     * @param[out] xstart x position of logo start
     * @param[out] xend   x position of logo end
     * @param[out] ystart y position of logo start
     * @param[out] yend   y position of logo end
     * @return true if successful, false otherwise
     */
    bool SetCoordinates(sAreaT *area, const int plane, int *xstart, int *xend, int *ystart, int *yend) const;

private:
    /**
    * get max logo size for video resolution
    * @return log size
    */
    sLogoSize GetMaxLogoSize() const;

    int GX[3][3]         = {0};      //!< GX Sobel mask
    //!<
    int GY[3][3]         = {0};      //!< GY Sobel mask
    //!<
    int videoWidth       = 0;        //!< video width
    //!<
    int videoHeight      = 0;        //!< videoHeight
    //!<
    int boundary         = 0;        //!< pixel to ignore in edge
    //!<
    int rPixel           = 0;        //!< pixel match result
    //!<
    int iPixel           = 0;        //!< inverse pixel match result
    //!<
    int intensity        = 0;        //!< brightness of plane 0 picture
    //!<
};
#endif
