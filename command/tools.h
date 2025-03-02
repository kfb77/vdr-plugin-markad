/*
 * tools.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __tools_h_
#define __tools_h_

#include <chrono>


// flags for CompareChannelName()
#define IGNORE_NOTHING 0   // exact match
#define IGNORE_HD      1   // ignore HD
#define IGNORE_COUNTRY 2   // ignore e.g. _Austria
#define IGNORE_CITY    4   // ignore e.g. _Berlin


/**
 * common tools class
 */
class cTools {
public:
    cTools();
    ~cTools();

    /**
    * log a separator line
    * @param main      true logs double line, single line otherwise
    */
    static void LogSeparator(const bool main = false);

    /**
    * log start of section and store timestamp
    * @param name section name
    */
    void StartSection(const char* name);

    /**
    * log end of section and calculate elapsed time
    * @param name section name
    * return elapsed time in ms
    */
    int EndSection(const char* name) const;

    /**
    * compare channel name pair
    * @param nameA      first name
    * @param nameB      second name
    * @param flags      compaire criteria
    */
    bool CompareChannelName(const char *nameA, const char *nameB, const int flags);

private:
    /**
    * list of states and counties in channel name to ignore
    */
    const char* countries[3] = {
        "_AUSTRIA",
        "_ÖSTERREICH",
        "_BAYERN"
    };


    /**
    * list of cities in channel name to ignore
    */
    const char* cities[21] = {
        "_BERLIN",
        "_BRANDENBURG",
        "_HH",
        "_NDS",
        "_BW",
        "_RP",
        "_KöLN",
        "_AACHEN",
        "_BIELEFELD",
        "_BONN",
        "_DORTMUND",
        "_DUISBURG",
        "_DüSSELDORF",
        "_ESSEN",
        "_MüNSTER",
        "_SACHSEN",
        "_S-ANHALT",
        "_SIEGEN",
        "_WUPPERTAG",
        "_SüD",
        "_NORD"
    };


    std::chrono::high_resolution_clock::time_point startSectionTime = {};  //!< start time of section
    //!<
};
#endif
