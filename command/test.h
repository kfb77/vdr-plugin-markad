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
    void Perf() const;

private:
    /**
     * performance test result structure
    */
    typedef struct sPerfResult {
        int pass        = 0;          //!< test run pass
        //!<
        int threads     = 0;          //!< count threads
        //!<
        char *hwaccel   = nullptr;    //!< hwaccel methode
        //!<
        double decode   = 0;          //!< avg time to read from file and decoding
        //!<
        double transfer = 0;          //!< avg time to transfert picture from GPU to CPU and convert pixel format
        //!<
        double read     = 0;          //!< avg time to read picture from memory
        //!<
        double test     = 0;          //!< time of whole test
        //!<

    } sPerfResult;

    /**
    * decoder performance test
    * @param result    performce test result
    */
    void PerfDecoder(sPerfResult *result) const;

    const char * recDir    = nullptr;  //!< recording directory
    //!<
    bool fullDecode        = false;    //!< true if full decoding, false if decoding only i-frames
    //!<
    char *hwaccel          = nullptr;  //!< hwaccel methode
    //!<
};

