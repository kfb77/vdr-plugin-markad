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

/**
 * class to do sobel transformation
 */
class cSobel {
public:
    /**
     * constructor of sobel transformation
     */
    cSobel(const int videoWidthParam, const int videoHeightParam, const int logoWidthParam, const int logoHeightParam, const int boundaryParam);
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
        sobelPicture = nullptr;
        sobelResult  = nullptr;
        sobelInverse = nullptr;
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
        sobelPicture = nullptr;
        sobelResult  = nullptr;
        sobelInverse = nullptr;
        return *this;
    }

    /**
    * sobel transformation of a single plane from input picture
    * @param picture    input picture
    * @param logo       picture of logo
    * @param corner     index of corner
    * @param plane      number of plane
    * @return true if successful, false otherwise
    */
    bool SobelPlane(sVideoPicture *picture, uchar **logo, const int corner, const int plane);

    /**
    * get size of logo
    * @return logo width and heigth
    */
    sLogoSize GetLogoSize() const;

    /**
    * set coordinates from logo area in picture
    * @return true if successful, false otherwise
    */
    bool SetCoordinates(const int corner, const int plane, int *xstart, int *xend, int *ystart, int *yend) const;

    /**
    * get all planes of result from sobels transformation
    * @return pointer to result plane
    */
    uchar **GetSobelPlanes();

    int GX[3][3]         = {0};      //!< GX Sobel mask
    //!<
    int GY[3][3]         = {0};      //!< GY Sobel mask
    //!<
    int videoWidth       = 0;        //!< video width
    //!<
    int videoHeight      = 0;        //!< videoHeight
    //!<
    sLogoSize logoSize;              //!< logo size in pixel
    //!<
    int boundary         = 0;        //!< pixel to ignore in edge
    //!<
    int rPixel           = 0;        //!< pixel match result
    //!<
    int iPixel           = 0;        //!< inverse pixel match result
    //!<
    int intensity        = 0;        //!< brightness of plane 0 picture
    //!<
    uchar **sobelPicture = nullptr;  //!< monochrome picture from edge after sobel transformation
    //!<
    uchar **sobelResult  = nullptr;  //!< monochrome picture after logo mask applied (sobel + mask)
    //!<
    uchar **sobelInverse = nullptr;  //!< monochrome picture of inverse result of sobel + mask
    //!<

private:
    /**
    * get max logo size for video resolution
    * @return log size
    */
    sLogoSize GetMaxLogoSize() const;

};
#endif
