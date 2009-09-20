/*
 * ts2pes.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __ts2pes_h_
#define __ts2pes_h_

#ifndef TS_SIZE
#define TS_SIZE 188
#endif

#ifndef uchar
typedef unsigned char uchar;
#endif

#include <stdlib.h>
#include <string.h>

class cMarkAdTS2PES
{
private:
    struct TSHDR
    {
unsigned Sync:
        8;
unsigned PidH:
        5;
unsigned Priority:
        1;
unsigned PayloadStart:
        1;
unsigned TError:
        1;
unsigned PidL:
        8;
unsigned Counter:
        4;
unsigned AFC:
        2;
unsigned TSC:
        2;
    };

    struct TSADAPT
    {
unsigned Len:
        8;
unsigned Flags:
        8;
    };

    uchar *pesdatalast;
    uchar *pesdata;
    int pessize;
    int streamsize;
    bool data_left;
    int counter;
    bool sync;

    void Reset();
    int FindPESHeader(uchar *TSData, int TSSize, int *StreamSize);
public:
    cMarkAdTS2PES();
    ~cMarkAdTS2PES();
    int Process(int Pid,uchar *TSData, int TSSize, uchar **PESData, int *PESSize);
};

#endif
