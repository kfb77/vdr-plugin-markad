/*
 * test.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


#include "tools.h"


/**
* performance test class
*/
class cTest : protected cTools {
public:

    /**
     * constructor for performance test class
     * @param recDirParam      recording directory
     * @param fullDecodeParam  true if full decoding, false if decoding only i-frames
     * @param hwaccelParam     hwaccel methode
     */
    explicit cTest(const char *recDirParam, const bool fullDecodeParam, char *hwaccelParam);
    ~cTest();

    /**
     * all decoder performance test
     */
    void Perf();

    /**
    * decoder performance test
    * @param testFrames count of test rames for performance test
    * @param threads    count of FFmpeg threads
    * @param hwaccel    string of hwaccel methode
    */
    int PerfDecoder(const int testFrames, const int threads, char *hwaccel) const;

private:
    const char * recDir = nullptr;  //!< recording directory
    //!<
    bool fullDecode     = false;    //!< true if full decoding, false if decoding only i-frames
    //!<
    char *hwaccel       = nullptr;  //!< hwaccel methode
    //!<
};

