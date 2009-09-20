/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "markad-standalone.h"

#define MAXSYSLOGBUF 255
void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
    char fmt[MAXSYSLOGBUF];
    snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
    va_start(ap, format);
    vsyslog(priority, fmt, ap);
    va_end(ap);
}

int main(int argc, char *argv[])
{
    uchar data[35344];
    int datalen;

    //int f=open("/var/lib/video/031.ts",O_RDWR);
    //int f=open("/root/frames_515.dat",O_RDWR);
    //int f=open("/root/VDR/PLUGINS/markad/test/ANIXEHD-Spiderman3-h.264.ts",O_RDWR);
    int f=open("/tmp/input0.ts",O_RDWR);
    if (f==-1) return -1;

    MarkAdContext macontext;
    memset(&macontext,0,sizeof(macontext));

    macontext.General.StartTime=0;
    macontext.General.EndTime=0xFFFFFFFF;
    macontext.General.VPid=0x100;
    //macontext.General.DPid=0x403;
//    macontext.General.DPid=0x203;
    //macontext.General.APid=0x101;

    cMarkAdDemux *demux = new cMarkAdDemux();
    cMarkAdDecoder *decoder = new cMarkAdDecoder(255,false,false);
    cMarkAdVideo *video = new cMarkAdVideo(255,&macontext);
    MarkAdMark *mark;

    int lastiframe=1;
    while (datalen=read(f,&data,sizeof(data)))
    {
        uchar *pkt;
        int pktlen;

        uchar *tspkt = data;
        int tslen = datalen;

        while (tslen>0)
        {
            int len=demux->Process(macontext.General.VPid,tspkt,tslen,&pkt,&pktlen);
            if (len<0)
            {
                break;
            }
            else
            {

                if (pkt)
                {

                    decoder->FindH262VideoInfos(&macontext,pkt,pktlen);
                    if (decoder->DecodeVideo(&macontext,pkt,pktlen))
                    {
                        mark=video->Process(lastiframe++);
                        //   AddMark(mark);

                    }

                    printf("Pktlen=%i\n", pktlen);
                }

                tspkt+=len;
                tslen-=len;
            }
        }
    }
    delete demux;
    delete decoder;
    delete video;
    close(f);


    return 0;

}
