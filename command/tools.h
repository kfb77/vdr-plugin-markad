/*
 * tools.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __tools_h_
#define __tools_h_


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
    * compare channel name pair
    * @param nameA      first name
    * @param nameB      second name
    * @param flags      compaire criteria
    */
    bool CompareChannelName(const char *nameA, const char *nameB, const int flags);

private:
    const char* cities[15] = {
        "_BERLIN",
        "_BRANDENBURG",
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
        "_SIEGEN",
        "_WUPPERTAG"
    };
};
#endif
