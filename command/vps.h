/*
 * vps.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */


/**
 * load VPS events
 */
class cVPS {
    public:

/**
 * cDecoder constructor
 */
        explicit cVPS(const char *directory);
        ~cVPS();

        int GetStart()      {return vpsStart;};
        int GetStop()       {return vpsStop;};
        int GetPauseStart() {return vpsPauseStart;};
        int GetPauseStop()  {return vpsPauseStop;};

    private:
        int vpsStart      = -1;
        int vpsStop       = -1;
        int vpsPauseStart = -1;
        int vpsPauseStop  = -1;

};

