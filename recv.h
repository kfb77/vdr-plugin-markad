/*
 * recv.h: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef __recv_h_
#define __recv_h_

#include <vdr/receiver.h>
#include <vdr/filter.h>
#include <vdr/thread.h>
#include <vdr/ringbuffer.h>
#include <vdr/recording.h>

#include "demux.h"
#include "decoder.h"
#include "audio.h"
#include "video.h"
#include "common.h"

#if (APIVERSNUM >= 10700)
#include <linux/dvb/frontend.h>
#endif

#define MEGATS(n) ((n)*1024*1880)

class cMarkAdRingBuffer : public cRingBufferFrame
{
private:
    int pid;
public:
    cMarkAdRingBuffer(int Size) : cRingBufferFrame(Size, true) {};
    ~cMarkAdRingBuffer()
    {
        Clear();
    }
    void Wait(void)
    {
        WaitForGet();
    }
    void Signal(void)
    {
        EnableGet();
    }
    bool Check(int Size)
    {
        return (Free() >= Size);
    }
};

class cMarkAdReceiver : public cReceiver, public cThread
{
private:
    int recvnumber;
    char *filename;
    int lastiframe;

    char *strcatrealloc(char *dest, const char *src);
    cMarks marks;
    cIndexFile *Index;

    int LastIFrame();
    MarkAdContext macontext;

    cMarkAdDecoder *decoder;
    cMarkAdCommon *common;
    cMarkAdAudio *audio;
    cMarkAdVideo *video;

    cMarkAdDemux *video_demux;
    cMarkAdDemux *mp2_demux;
    cMarkAdDemux *ac3_demux;

    void AddMark(MarkAdMark *mark, int Priority);
protected:
    virtual void Activate(bool On);
    virtual void Receive(uchar *Data, int Length);
    void Action();
    cMarkAdRingBuffer buffer;
    bool running;
public:
    cMarkAdReceiver(int RecvNumber, const char *Filename, cTimer *Timer);
    const char *FileName()
    {
        return (const char *) filename;
    }
    virtual ~cMarkAdReceiver();
};

#endif
