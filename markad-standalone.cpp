/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "markad-standalone.h"

cMarkAdStandalone *cmasta=NULL;

void syslog_with_tid(int priority, const char *format, ...)
{
    va_list ap;
#ifdef SYSLOG
    char fmt[255];
    snprintf(fmt, sizeof(fmt), "[%d] %s", getpid(), format);
    va_start(ap, format);
    vsyslog(priority, fmt, ap);
    va_end(ap);
#else
    va_start(ap, format);
    vprintf(format,ap);
    va_end(ap);
    printf("\n");
#endif
}

void cMarkAdStandalone::AddMark(MarkAdMark *Mark)
{
// TODO: Implement this!
}

bool cMarkAdStandalone::ProcessFile(const char *Directory, int Number)
{
    if (!Directory) return false;
    if (!Number) return false;

    int datalen=385024;
    uchar data[datalen];

    char *fbuf;
    if (isTS)
    {
        if (asprintf(&fbuf,"%s/%05i.ts",Directory,Number)==-1) return false;
    }
    else
    {
        if (asprintf(&fbuf,"%s/%03i.vdr",Directory,Number)==-1) return false;
    }

    int f=open(fbuf,O_RDONLY);
    free(fbuf);
    if (f==-1) return false;

    int dataread,lastiframe=0;
    dsyslog("markad [%i]: processing file %05i",recvnumber,Number);

    while ((dataread=read(f,data,datalen))>0)
    {
        if (abort) break;
        MarkAdMark *mark;

        if (common)
        {
            mark=common->Process(lastiframe);
            AddMark(mark);
        }

        if ((video_demux) && (decoder) && (video))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=video_demux->Process(macontext.General.VPid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (decoder->DecodeVideo(&macontext,pkt,pktlen))
                        {
                            if (macontext.Video.Info.Pict_Type==MA_I_TYPE)
                            {
                                lastiframe=framecnt;
                            }
                            mark=video->Process(framecnt);
                            AddMark(mark);

                            framecnt++;
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if ((mp2_demux) && (decoder) && (audio))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=mp2_demux->Process(macontext.General.APid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (decoder->DecodeMP2(&macontext,pkt,pktlen))
                        {
                            mark=audio->Process(lastiframe);
                            AddMark(mark);
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }

        if ((ac3_demux) && (decoder) && (audio))
        {
            uchar *pkt;
            int pktlen;

            uchar *tspkt = data;
            int tslen = dataread;

            while (tslen>0)
            {
                int len=ac3_demux->Process(macontext.General.DPid,tspkt,tslen,&pkt,&pktlen);
                if (len<0)
                {
                    break;
                }
                else
                {
                    if (pkt)
                    {
                        if (decoder->DecodeAC3(&macontext,pkt,pktlen))
                        {
                            mark=audio->Process(lastiframe);
                            AddMark(mark);
                        }
                    }
                    tspkt+=len;
                    tslen-=len;
                }
            }
        }
    }
    close(f);
    return true;
}

void cMarkAdStandalone::Process(const char *Directory)
{
    if (abort) return;
    for (int i=1; i<=MaxFiles; i++)
    {
        if (abort) break;
        if (!ProcessFile(Directory,i))
        {
            break;
        }
    }
    if (abort)
    {
        isyslog("markad [%i]: aborted",recvnumber);
    }
}

bool cMarkAdStandalone::CheckTS(const char *Directory)
{
    MaxFiles=0;
    isTS=false;
    if (!Directory) return false;
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",Directory)==-1) return false;
    struct stat statbuf;
    if (stat(buf,&statbuf)==-1)
    {
        if (errno!=ENOENT)
        {
            free(buf);
            return false;
        }
        free(buf);
        if (asprintf(&buf,"%s/001.vdr",Directory)==-1) return false;
        if (stat(buf,&statbuf)==-1)
        {
            free(buf);
            return false;
        }
        free(buf);
        // .VDR detected
        isTS=false;
        MaxFiles=999;
        return true;
    }
    free(buf);
    // .TS detected
    isTS=true;
    MaxFiles=65535;
    return true;
}

bool cMarkAdStandalone::CheckPATPMT(const char *Directory)
{
    char *buf;
    if (asprintf(&buf,"%s/00001.ts",Directory)==-1) return false;

    int fd=open(buf,O_RDONLY);
    free(buf);
    if (fd==-1) return false;

    uchar patpmt_buf[564];
    uchar *patpmt;

    if (read(fd,patpmt_buf,sizeof(patpmt_buf))!=sizeof(patpmt_buf))
    {
        close(fd);
        return false;
    }
    close(fd);
    patpmt=patpmt_buf;

    if ((patpmt[0]==0x47) && ((patpmt[1] & 0x5F)==0x40) && (patpmt[2]==0x11) &&
            ((patpmt[3] & 0x10)==0x10)) patpmt+=188; // skip SDT

    // some checks
    if ((patpmt[0]!=0x47) || (patpmt[188]!=0x47)) return false; // no TS-Sync
    if (((patpmt[1] & 0x5F)!=0x40) && (patpmt[2]!=0)) return false; // no PAT
    if ((patpmt[3] & 0x10)!=0x10) return false; // PAT not without AFC
    if ((patpmt[191] & 0x10)!=0x10) return false; // PMT not without AFC
    struct PAT *pat = (struct PAT *) &patpmt[5];

    // more checks
    if (pat->reserved1!=3) return false; // is always 11
    if (pat->reserved3!=7) return false; // is always 111

    int pid=pat->pid_L+(pat->pid_H<<8);
    int pmtpid=((patpmt[189] & 0x1f)<<8)+patpmt[190];
    if (pid!=pmtpid) return false; // pid in PAT differs from pid in PMT

    struct PMT *pmt = (struct PMT *) &patpmt[193];

    // still more checks
    if (pmt->reserved1!=3) return false; // is always 11
    if (pmt->reserved2!=3) return false; // is always 11
    if (pmt->reserved3!=7) return false; // is always 111
    if (pmt->reserved4!=15) return false; // is always 1111

    if ((pmt->program_number_H!=pat->program_number_H) ||
            (pmt->program_number_L!=pat->program_number_L)) return false;

    int desc_len=(pmt->program_info_length_H<<8)+pmt->program_info_length_L;
    if (desc_len>166) return false; // beyond patpmt buffer

    int section_end = 196+(pmt->section_length_H<<8)+pmt->section_length_L;
    section_end-=4; // we don't care about the CRC32
    if (section_end>376) return false; //beyond patpmt buffer

    int i=205+desc_len;

    while (i<section_end)
    {
        struct ES_DESCRIPTOR *es=NULL;
        struct STREAMINFO *si = (struct STREAMINFO *) &patpmt[i];
        int esinfo_len=(si->ES_info_length_H<<8)+si->ES_info_length_L;
        if (esinfo_len)
        {
            es = (struct ES_DESCRIPTOR *) &patpmt[i+sizeof(struct STREAMINFO)];
        }

        // oh no -> more checks!
        if (si->reserved1!=7) return false;
        if (si->reserved2!=15) return false;

        int pid=(si->PID_H<<8)+si->PID_L;

        switch (si->stream_type)
        {
        case 0x1:
        case 0x2:
            macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
            // just use the first pid
            if (!macontext.General.VPid.Num) macontext.General.VPid.Num=pid;
            break;

        case 0x3:
        case 0x4:
            // just use the first pid
            if (!macontext.General.APid.Num) macontext.General.APid.Num=pid;
            break;

        case 0x6:
            if (es)
            {
                if (es->Descriptor_Tag==0x6A) macontext.General.DPid.Num=pid;
            }
            break;

        case 0x1b:
            macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
            // just use the first pid
            if (!macontext.General.VPid.Num) macontext.General.VPid.Num=pid;
            break;
        }

        i+=(sizeof(struct STREAMINFO)+esinfo_len);
    }

    return true;
}

cMarkAdStandalone::cMarkAdStandalone(const char *Directory)
{
    recvnumber=255;
    abort=false;

    memset(&macontext,0,sizeof(macontext));
    macontext.General.StartTime=0;
    macontext.General.EndTime=time(NULL)+(7*86400);
    macontext.General.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;
    macontext.General.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    if (!CheckTS(Directory))
    {
        video_demux=NULL;
        ac3_demux=NULL;
        mp2_demux=NULL;
        decoder=NULL;
        video=NULL;
        audio=NULL;
        common=NULL;
        return;
    }

    if (isTS)
    {
        if (!CheckPATPMT(Directory))
        {
            esyslog("markad [%i]: no PAT/PMT found -> nothing to process",recvnumber);
            abort=true;
        }
        macontext.General.APid.Num=0;
    }
    else
    {
        macontext.General.DPid.Num=-1;
        macontext.General.VPid.Num=-1;
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
    }

    if (macontext.General.VPid.Num)
    {
        dsyslog("markad [%i]: using %s-video (0x%04x)",recvnumber,macontext.General.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264 ? "H264": "H262",
                macontext.General.VPid.Num);
        video_demux = new cMarkAdDemux(recvnumber);
    }
    else
    {
        video_demux=NULL;
    }

    if (macontext.General.APid.Num)
    {
        dsyslog("markad [%i]: using MP2 (0x%04x)",recvnumber,macontext.General.APid.Num);
        mp2_demux = new cMarkAdDemux(recvnumber);
    }
    else
    {
        mp2_demux=NULL;
    }

    if (macontext.General.DPid.Num)
    {
        dsyslog("markad [%i]: using AC3 (0x%04x)",recvnumber,macontext.General.DPid.Num);
        ac3_demux = new cMarkAdDemux(recvnumber);
    }
    else
    {
        ac3_demux=NULL;
    }

    if (!abort)
    {
        decoder = new cMarkAdDecoder(recvnumber,macontext.General.VPid.Type==MARKAD_PIDTYPE_VIDEO_H264,
                                     macontext.General.DPid.Num!=0);
        video = new cMarkAdVideo(recvnumber,&macontext);
        audio = new cMarkAdAudio(recvnumber,&macontext);
        common = new cMarkAdCommon(recvnumber,&macontext);
    }
    else
    {
        decoder=NULL;
        video=NULL;
        audio=NULL;
        common=NULL;
    }

    framecnt=0;
}

cMarkAdStandalone::~cMarkAdStandalone()
{
    if (video_demux) delete video_demux;
    if (ac3_demux) delete ac3_demux;
    if (mp2_demux) delete mp2_demux;
    if (decoder) delete decoder;
    if (video) delete video;
    if (audio) delete audio;
    if (common) delete common;
}

bool isnumber(const char *s)
{
    while (*s)
    {
        if (!isdigit(*s))
            return false;
        s++;
    }
    return true;
}

int usage()
{
    // nothing done, give the user some help
    printf("Usage: markad [options] cmd <record>\n"
           "options:\n"
           "-b              --background\n"
           "                  markad runs as a background-process\n"
           "                  this will be automatically set if called with \"after\"\n"
           "-p,             --priority\n"
           "                  priority-level of markad when running in background\n"
           "                  [19...-19] default 19\n"
           "-v,             --verbose\n"
           "                  increments loglevel by one, can be given multiple times\n"
           "-B              --backupmarks\n"
           "                  make a backup of the existing marks\n"
           "-O,             --OSD\n"
           "                  markad sends an OSD-Message for start and end\n"
           "-V              --version\n"
           "                  print version-info and exit\n"
           "--markfile=<markfilename>\n"
           "  set a different markfile-name\n"
           "\ncmd: one of\n"
           "-                            dummy-parameter if called directly\n"
           "after                        markad starts to analyze the recording\n"
           "before                       markad exits immediately if called with \"before\"\n"
           "edited                       markad exits immediately if called with \"edited\"\n"
           "nice                         runs markad with nice(19)\n"
           "\n<record>                     is the name of the directory where the recording\n"
           "                             is stored\n\n"
          );
    return -1;
}

void signal_handler(int sig)
{
    if (sig==SIGUSR1)
    {
        // TODO: what we are supposed to do?
    }
    else
    {
        if (cmasta)
        {
            cmasta->SetAbort();
        }
    }
}

int main(int argc, char *argv[])
{
    int c;
    bool bAfter=false,bBefore=false,bEdited=false,bFork=false,bNice=false,bImmediateCall=false;
    int niceLevel = 19;
    char *recDir=NULL;

    while (1)
    {
        int option_index = 0;
        static struct option long_options[] =
        {
            {"statisticfile",1,0,'s'
            },
            {"logocachedir", 1, 0, 'l'},
            {"verbose", 0, 0, 'v'},
            {"background", 0, 0, 'b'},
            {"priority",1,0,'p'},
            {"comments", 0, 0, 'c'},
            {"jumplogo",0,0,'j'},
            {"overlap",0,0,'o' },
            {"ac3",0,0,'a' },
            {"OSD",0,0,'O' },
            {"savelogo", 0, 0, 'S'},
            {"backupmarks", 0, 0, 'B'},
            {"scenechangedetection", 0, 0, 'C'},
            {"version", 0, 0, 'V'},
            {"nelonen",0,0,'n'},
            {"markfile",1,0,1},
            {"loglevel",1,0,2},
            {"testmode",0,0,3},
            {"online",2,0,4},
            {"nopid",0,0,5},
            {"asd",0,0,6},
            {"pass3only",0,0,7},
            {0, 0, 0, 0}
        };

        c = getopt_long  (argc, argv, "s:l:vbp:cjoaOSBCV",
                          long_options, &option_index);
        if (c == -1)
            break;

        switch (c)
        {

        case 'v':
            SysLogLevel++;
            if (SysLogLevel>10) SysLogLevel=10;
            break;

        case 'b':
            bFork = true;
            break;

        case 'p':
            if (isnumber(optarg) || *optarg=='-')
                niceLevel = atoi(optarg);
            else
            {
                fprintf(stderr, "markad: invalid priority level: %s\n", optarg);
                return 2;
            }
            bNice = true;
            break;

        case 'O':
            // --OSD
            break;

        case 'o':
            // --overlap
            break;

        case 's':
        case 'l':
        case 'c':
        case 'j':
        case 'a':
        case 'S':
        case 'B':
        case 'n':
        case 'C':
            break;

        case 'V':
            printf("markad %s - marks advertisements in VDR recordings\n",VERSION);
            return 0;

        case '?':
            printf("unknow option ?\n");
            break;

        case 0:
            printf ("option %s", long_options[option_index].name);
            if (optarg)
                printf (" with arg %s", optarg);
            printf ("\n");
            break;

        case 1: // --markfile
            //setMarkfileName(optarg); // TODO: implement this
            break;

        case 2: // --loglevel
            SysLogLevel=atoi(optarg);
            if (SysLogLevel>10) SysLogLevel=10;
            if (SysLogLevel<0) SysLogLevel=2;
            break;

        case 3: // --testmode
            break;

        case 4: // --online
            break;

        case 5: // --nopid
            break;

        case 6: // --asd
            break;

        case 7: // --pass3only
            break;

        default:
            printf ("? getopt returned character code 0%o ? (option_index %d)\n", c,option_index);
        }
    }

    if (optind < argc)
    {
        while (optind < argc)
        {
            if (strcmp(argv[optind], "after" ) == 0 )
            {
                bAfter = bFork = bNice = true;
            }
            else if (strcmp(argv[optind], "before" ) == 0 )
            {
                bBefore = true;
            }
            else if (strcmp(argv[optind], "edited" ) == 0 )
            {
                bEdited = true;
            }
            else if (strcmp(argv[optind], "nice" ) == 0 )
            {
                bNice = true;
            }
            else if (strcmp(argv[optind], "-" ) == 0 )
            {
                bImmediateCall = true;
            }
            else
            {
                if ( strstr(argv[optind],".rec") != NULL )
                    recDir = argv[optind];
            }
            optind++;
        }
    }

    // do nothing if called from vdr before/after the video is cutted
    if (bBefore) return 0;
    if (bEdited) return 0;

    // we can run, if one of bImmediateCall, bAfter, bBefore or bNice is true
    // and recDir is given
    if ( (bImmediateCall || bAfter || bNice) && recDir )
    {
        // if bFork is given go in background
        if ( bFork )
        {
            (void)umask((mode_t)0011);
            //close_files();
            pid_t pid = fork();
            if (pid < 0)
            {
                fprintf(stderr, "%m\n");
                esyslog("fork ERROR: %m");
                return 2;
            }
            if (pid != 0)
            {
                isyslog("markad forked to pid %d",pid);
                return 0; // initial program immediately returns
            }
        }
        if ( bFork )
        {
            isyslog("markad (forked) pid: %d", getpid());
            if (chdir("/")==-1)
            {
                perror("chdir");
                exit(EXIT_FAILURE);
            }
            if (setsid() == (pid_t)(-1))
            {
                perror("setsid");
                exit(EXIT_FAILURE);
            }
            if (signal(SIGHUP, SIG_IGN) == SIG_ERR)
            {
                perror("signal(SIGHUP, SIG_IGN)");
                errno = 0;
            }
            int f;

            f = open("/dev/null", O_RDONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdin)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }

            f = open("/dev/null", O_WRONLY);
            if (f == -1)
            {
                perror("/dev/null");
                errno = 0;
            }
            else
            {
                if (dup2(f, fileno(stdout)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                if (dup2(f, fileno(stderr)) == -1)
                {
                    perror("dup2");
                    errno = 0;
                }
                (void)close(f);
            }
        }

        int MaxPossibleFileDescriptors = getdtablesize();
        for (int i = STDERR_FILENO + 1; i < MaxPossibleFileDescriptors; i++)
            close(i); //close all dup'ed filedescriptors

        // should we renice ?
        if ( bNice )
        {
            int niceErr = nice(niceLevel);
            int oldErrno = errno;
            if ( errno == EPERM || errno == EACCES )
            {
                esyslog("ERROR: nice %d: no super-user rights",niceLevel);
                errno = oldErrno;
                fprintf(stderr, "nice %d: no super-user rights\n",niceLevel);
            }
            else if ( niceErr != niceLevel )
            {
                esyslog("nice ERROR(%d,%d): %m",niceLevel,niceErr);
                errno = oldErrno;
                fprintf(stderr, "%d %d %m\n",niceErr,errno);
            }
        }

        // now do the work...
        struct stat statbuf;
        if (stat(recDir,&statbuf)==-1)
        {
            fprintf(stderr,"%s not found\n",recDir);
            return -1;
        }

        if (!S_ISDIR(statbuf.st_mode))
        {
            fprintf(stderr,"%s is not a directory\n",recDir);
            return -1;
        }

        cmasta = new cMarkAdStandalone(recDir);
        if (!cmasta) return -1;

        // ignore some signals
        signal(SIGHUP, SIG_IGN);

        // catch some signals
        signal(SIGINT, signal_handler);
        signal(SIGTERM, signal_handler);
        signal(SIGUSR1, signal_handler);

        cmasta->Process(recDir);
        delete cmasta;
        return 0;
    }

    return usage();
}
