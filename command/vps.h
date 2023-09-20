/*
 * vps.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


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
        int GetStart() {return vpsStart;};

/**
 * get VPS stop event offset
 * @return offset from start of recording in s
 */
        int GetStop() {return vpsStop;};

/**
 * set VPS stop event offset
 */
        void SetStop(const int state) {vpsStop = state;};

/**
 * get VPS pause start event offset
 * @return offset from start of recording in s
 */
        int GetPauseStart() {return vpsPauseStart;};

/**
 * get VPS pause stop event offset
 * @return offset from start of recording in s
 */
        int GetPauseStop()  {return vpsPauseStop;};

/**
 * get length of broadcast based on VPS events
 * @return length of recording in s
 */
        int Length();

    private:
        int vpsStart      = -1;  //!< VPS start event offset from recodering start in s
                                 //!<
        int vpsStop       = -1;  //!< VPS stop event offset from recodering start in s
                                 //!<
        int vpsPauseStart = -1;  //!< VPS pause start event offset from recodering start in s
                                 //!<
        int vpsPauseStop  = -1;  //!< VPS pause start event offset from recodering start in s
                                 //!<
};
