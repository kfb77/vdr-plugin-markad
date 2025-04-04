/*
 * overlap.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __overlap_h_
#define __overlap_h_

#include "tools.h"
#include "marks.h"
#include "decoder.h"




/**
 * class to detect overlapping scenes before and after a single advertising
 */
class cOverlapAroundAd {
public:

    /**
     * constructor of overlap detection
     * @param decoderParam pointer to decoder
     */
    explicit cOverlapAroundAd(cDecoder *decoderParam);

    ~cOverlapAroundAd();

/// process overlap detection
    /**
     * if beforeAd == true preload frames before stop mark in histogram buffer array, otherwise preload frames after start mark <br>
     * if we got frameCount, start compare
     * @param[in]     picture      video picture
     * @param[in]     frameCount   number of frames to process
     * @param[in]     beforeAd     true if called with a frame before advertising, false otherwise
     * @param[in]     h264         true if HD video, false otherwise
     */
    void Process(const sVideoPicture *picture, const int frameCount, const bool beforeAd, const bool h264);

    /**
     * detect overlaps before and after advertising
     * @param[in,out] overlapPos new stop and start mark pair after overlap detection, -1 if no overlap was found
     */
    void Detect(sOverlapPos *overlapPos);

private:

    enum {
        OV_BEFORE = 0,
        OV_AFTER  = 1
    };

    typedef int simpleHistogram[256];     //!< histogram array
    //!<

    /**
     * check if two histogram are similar
     * @param hist1 histogram 1
     * @param hist2 histogram 2
     * @return different pixels if similar, <0 otherwise
     */
    int AreSimilar(const simpleHistogram &hist1, const simpleHistogram &hist2) const;

    /**
     * get a simple histogram of current frame
     * @param[in]     picture    video picture
     * @param[in,out] dest       histogram
     */
    void GetHistogram(const sVideoPicture *picture, simpleHistogram &dest) const;

    /**
     * histogram buffer for overlap detection
     */
    typedef struct sHistBuffer {
        int frameNumber = -1;      //!< frame number
        //!<
        int64_t pts     = -1;      //!< pts of picture
        //!<
        bool valid      = false;   //!< true if buffer is valid
        //!<
        simpleHistogram histogram; //!< simple frame histogram
        //!<
    } sHistBuffer;

    cDecoder *decoder         = nullptr;    //!< pointer to decoder
    //!<
    sHistBuffer *histbuf[2]   = {nullptr};  //!< simple frame histogram with frame number
    //!<
    int histcnt[2]            = {0};        //!< count of prcessed frame histograms
    //!<
    int histframes[2]         = {0};        //!< frame number of histogram buffer content
    //!<
    int lastFrameNumber       = 0;          //!< last processed frame number
    //!<
    int similarCutOff         = 0;          //!< maximum different pixel to treat picture as similar, depends on resolution
    //!<
    int similarMinLength      = 0;          //!< minimum similar frames for a overlap
    //!<
};


/**
 * class to detect overlapping scenes and closing credits in recording
 */
class cOverlap : private cTools {
public:
    /**
     * process overlap detection with all ads
     * @param  decoderParam  pointer to decoder
     * @param  indexParam    pointer to index
     */
    cOverlap(cDecoder *decoderParam, cIndex *indexParam);
    ~cOverlap();

    /**
     * detect overlap
     * @param marksParam      current marks
     */
    bool DetectOverlap(cMarks *marksParam);

    /**
     * process overlap detection with stop/start pair
     * @param[in]      overlapAroundAd detection object
     * @param[in, out] mark1           stop mark before advertising, set to start position of detected overlap
     * @param[in, out] mark2           start mark after advertising, set to end position of detected overlap
     * @return true if overlap was detected, false otherwise
     */
    bool ProcessMarksOverlap(cOverlapAroundAd *overlapAroundAd, cMark **mark1, cMark **mark2);

private:
    cDecoder *decoder         = nullptr;   //!< decoder
    //!<
    cIndex   *index           = nullptr;   //!< recording index
    //!<
    cMarks   *marks           = nullptr;   //!< marks
    //!<
};
#endif
