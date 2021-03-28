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
        struct logoStopStartPairType {
            int stopPosition = -1;
            int startPosition = -1;
            int isLogoChange = 0;            // -1 no logo change, 0 unknown, 1 is logo change
            int isAdvertising = 0;           // -1 pair is no advertising, 0 unknown, 1 pair is advertising
            int isStartMarkInBroadcast = 0;  // -1 start mark does not contain to broadcast, 0 unknown, 1 start mark contains to broadcast
            int isInfoLogo = 0;           // -1 pair is no introduction sequence, 0 unknown, 1 pair is introduction sequence
        };

        cEvaluateLogoStopStartPair(clMarks *marks, clMarks *blackMarks, const int framesPerSecond, const int iStart, const int chkSTART, const int iStopA);
        ~cEvaluateLogoStopStartPair();
        void isInfoLogo(clMarks *marks, clMarks *blackMarks, logoStopStartPairType *logoStopStartPair, const int framesPerSecond);
        bool GetNextPair(int *stopPosition, int *startPosition, int *isLogoChange, int *isInfoLogo);
        int GetLastClosingCreditsStart();

    private:
        std::vector<logoStopStartPairType> logoPairVector;
        std::vector<logoStopStartPairType>::iterator nextLogoPairIterator;
};
#endif
