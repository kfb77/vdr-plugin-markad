/*
 * markad-standalone.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "markad-standalone.h"

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

int cMarkAdIndex::GetNext(off_t Offset)
{
    if (index_fd==-1) return 0;

    struct tIndexPes
    {
        uint32_t offset;
        uchar type;
        uchar number;
        uint16_t reserved;
    };

    struct tIndexTs
    {
uint64_t offset:
        40;
unsigned reserved:
        7;
unsigned independent:
        1;
uint16_t number:
        16;
    };

    if (offset>Offset) return iframe;
    if (ts)
    {
        struct tIndexTs its;
        if (read(index_fd,&its,sizeof(its))==-1) return 0;
        offset=(off_t) its.offset;
        if (its.independent==1) iframe=index;
    }
    else
    {
        struct tIndexPes ipes;
        if (read(index_fd,&ipes,sizeof(ipes))==-1) return 0;
        offset=ipes.offset;
        if (ipes.type==1) iframe=index;
    }
    index++;
    return iframe;
}

bool cMarkAdIndex::Open(const char *Directory)
{
    if (!Directory) return false;
    char *ibuf;
    asprintf(&ibuf,"%s/index.vdr",Directory);
    if (!ibuf) return false;
    index_fd = open(ibuf,O_RDONLY);
    free(ibuf);
    maxfiles=999;
    if (index_fd==-1)
    {
        // second try just index -> ts format
        asprintf(&ibuf,"%s/index",Directory);
        if (!ibuf) return false;
        index_fd = open(ibuf,O_RDONLY);
        free(ibuf);
        if (index_fd==-1)
        {
            fprintf(stderr,"Cannot open index file in %s\n",Directory);
            return false;
        }
        ts=true;
        maxfiles=65535;
    }
    return true;
}

void cMarkAdIndex::Close()
{
    if (index_fd!=-1) close(index_fd);
}

cMarkAdIndex::cMarkAdIndex(const char *Directory)
{
    if (!Directory) return;
    iframe=0;
    offset=0;
    index=0;
    ts=false;
    Open(Directory);
}

cMarkAdIndex::~cMarkAdIndex()
{
    Close();
}

void cMarkAdStandalone::AddMark(MarkAdMark *Mark)
{
// TODO: Implement this!
}

bool cMarkAdStandalone::ProcessFile(int Number)
{
    if (!dir) return false;
    if (!Number) return false;

    uchar *data;
    int datalen;
    int dataread;

    char *fbuf;
    if (index->isTS())
    {
        datalen=70688; // multiple of 188
        data=(uchar *) malloc(datalen);
        if (!data) return false;
        asprintf(&fbuf,"%s/%05i.ts",dir,Number);
    }
    else
    {
        datalen=69632; // VDR paket size
        data=(uchar *) malloc(datalen);
        if (!data)return false;
        asprintf(&fbuf,"%s/%03i.vdr",dir,Number);
    }
    if (!fbuf) return false;

    int f=open(fbuf,O_RDONLY);
    free(fbuf);
    if (f==-1) return false;

    int lastiframe;
    while ((dataread=read(f,data,datalen))>0)
    {

        lastiframe=LastIFrame(Number,lseek(f,0,SEEK_CUR)-dataread);
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
                        decoder->FindH262VideoInfos(&macontext,pkt,pktlen);
                        if (decoder->DecodeVideo(&macontext,pkt,pktlen))
                        {
                            mark=video->Process(lastiframe);
                            AddMark(mark);
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
                        decoder->FindAC3AudioInfos(&macontext,pkt,pktlen);
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
    free(data);
    close(f);
    return true;
}

int cMarkAdStandalone::LastIFrame(int Number, off_t Offset)
{
    return index->GetNext(Offset);
}


void cMarkAdStandalone::Process()
{
    if (!index) return;

    for (int i=1; i<=index->MaxFiles(); i++)
    {
        if (!ProcessFile(i))
        {
            break;
        }
    }
}

cMarkAdStandalone::cMarkAdStandalone(const char *Directory)
{
    dir=strdup(Directory);

    index = new cMarkAdIndex(Directory);
    if (!index)
    {
        video_demux=ac3_demux=mp2_demux=NULL;
        decoder=NULL;
        video=NULL;
        audio=NULL;
        common=NULL;
        return;
    }

    memset(&macontext,0,sizeof(macontext));
    macontext.General.StartTime=0;
    macontext.General.EndTime=time(NULL)+(7*86400);
    macontext.General.DPid.Type=MARKAD_PIDTYPE_AUDIO_AC3;
    macontext.General.APid.Type=MARKAD_PIDTYPE_AUDIO_MP2;

    if (index->isTS())
    {
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H264;
    }
    else
    {
        macontext.General.VPid.Type=MARKAD_PIDTYPE_VIDEO_H262;
    }

    macontext.General.VPid.Num=0xa5;
    //macontext.General.DPid.Num=0x403;
    macontext.General.APid.Num=0x78;

    video_demux = new cMarkAdDemux(255);

    if (macontext.General.APid.Num)
    {
        mp2_demux = new cMarkAdDemux(255);
    }

    if (macontext.General.DPid.Num)
    {
        ac3_demux = new cMarkAdDemux(255);
    }

    decoder = new cMarkAdDecoder(255,index->isTS(),
                                 macontext.General.DPid.Num!=0);
    video = new cMarkAdVideo(255,&macontext);
    audio = new cMarkAdAudio(255,&macontext);
    common = new cMarkAdCommon(255,&macontext);
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
    if (index) delete index;

    if (dir) free(dir);
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
//            osdMsg = 1;
            break;

        case 's':
        case 'l':
        case 'c':
        case 'j':
        case 'o':
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

        case 2: // --verbose
            //if (isnumber(optarg))
            //    verbosity = atoi(optarg);
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
            chdir("/");
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

        // catch some signals
        /*
                signal(SIGINT, signal_handler);
                signal(SIGTERM, signal_handler);
                signal(SIGABRT, signal_handler);
                signal(SIGSEGV, signal_handler);
                signal(SIGUSR1, signal_handler);
        */

        // do cleanup at exit...
//        atexit(cleanUp);

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

        cMarkAdStandalone *cmasta = new cMarkAdStandalone(recDir);
        if (cmasta)
        {
            cmasta->Process();
            delete cmasta;
        }

        return 0;
    }

    return usage();
}
