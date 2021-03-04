/*
 * evaluate.cpp.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifndef __evaluate_h_
#define __evaluate_h_

extern "C" {
    #include "debug.h"
}
#include "global.h"
#include "marks.h"

class cEvaluateLogoStopStartPair {
    public:
        cEvaluateLogoStopStartPair(clMarks *marks, const int framesPerSecond, const int iStart, const int chkSTART, const int iStopA);
        ~cEvaluateLogoStopStartPair();
        bool GetNextPair(int *stopPosition, int *startPosition);
        int GetLastClosingCreditsStart();

    private:
        struct logoStopStartPair {
            int stopPosition = -1;
            int startPosition = -1;
            int isLogoChange = 0;            // -1 no logo change, 0 unknown, 1 is logo change
            int isAdvertising = 0;           // -1 pair is advertising, 0 unknown, 1 pair is advertising
            int isStartMarkInBroadcast = 0;  // -1 start mark does not contain to broadcast, 0 unknown, 1 start mark contains to broadcast
        };
        std::vector<logoStopStartPair> logoPairVector;
        std::vector<logoStopStartPair>::iterator nextLogoPairIterator;
};
#endif
