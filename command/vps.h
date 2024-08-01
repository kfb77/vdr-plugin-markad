/*
 * vps.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __vps_h_
#define __vps_h_

#include "marks.h"

/**
 * class for VPS events
 */
class cVPS {
public:

    /**
     * cVPS constructor
     */
    explicit cVPS(const char *directory);

    /**
     * cVPS destructor
     */
    ~cVPS();

    /**
     * get VPS start event offset
     * @return offset from start of recording in s
     */
    int GetStart() const {
        return vpsStart;
    };

    /**
     * get VPS stop event offset
     * @return offset from start of recording in s
     */
    int GetStop() const {
        return vpsStop;
    };

    /**
     * set VPS stop event offset
     */
    void SetStop(const int state) {
        vpsStop = state;
    };

    /**
     * status of VPD timer recording
     * @return true if recorded was controlled by VPS timer
     */
    bool IsVPSTimer() const {
        return isVPStimer;
    }

    /**
     * get length of broadcast based on VPS events
     * @return length of recording in s
     */
    int Length() const;

    /**
     * log match of start and end mark with VPS events
     * @param channel name of channel
     * @param marks object with all marks
     */
    void LogMatch(char *channel, cMarks *marks) const;

private:
    int vpsStart      = -1;    //!< VPS start event offset from recodering start in s
    //!<
    int vpsStop       = -1;    //!< VPS stop event offset from recodering start in s
    //!<
    int vpsPauseStart = -1;    //!< VPS pause start event offset from recodering start in s
    //!<
    int vpsPauseStop  = -1;    //!< VPS pause start event offset from recodering start in s
    //!<
    bool isVPStimer   = false; //!< recorded by VPS controlled timer
    //!<
};
#endif
