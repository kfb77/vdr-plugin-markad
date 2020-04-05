/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <signal.h>

#include "status.h"

cStatusMarkAd::cStatusMarkAd(const char *BinDir, const char *LogoDir, struct setup *Setup)
{
    setup=Setup;
    bindir=BinDir;
    logodir=LogoDir;
    actpos=0;
    memset(&recs,0,sizeof(recs));
}

cStatusMarkAd::~cStatusMarkAd()
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        Remove(i,true);
    }
}

int cStatusMarkAd::Recording()
{
    int cnt=0;
    for (int i=0; i<cDevice::NumDevices(); i++)
    {
        cDevice *dev=cDevice::GetDevice(i);
        if (dev)
        {
            if (dev->Receiving()) cnt++;
        }
    }
    return cnt;
}

bool cStatusMarkAd::Replaying()
{
    for (int i=0; i<cDevice::NumDevices(); i++)
    {
        cDevice *dev=cDevice::GetDevice(i);
        if (dev)
        {
            if (dev->Replaying()) return true;
        }
    }
    return false;
}

void cStatusMarkAd::Replaying(const cControl *UNUSED(Control), const char *UNUSED(Name),
                              const char *UNUSED(FileName), bool On)
{
    if (setup->ProcessDuring!=0) return;
    if (setup->whileReplaying) return;
    if (On)
    {
        Pause(NULL);
    }
    else
    {
        if (!Recording()) Continue(NULL);
    }
}

bool cStatusMarkAd::Start(const char *FileName, const char *Name, const bool Direct)
{
    if ((Direct) && (Get(FileName)!=-1)) return false;

    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
                                   bindir,
                                   setup->Verbose ? " -v " : "",
                                   setup->SaveInfo ? " -I " : "",
                                   setup->GenIndex ? " -G " : "",
#if VDRVERSNUM < 10715
                                   setup->OSDMessage ? " -O --svdrpport=2001 " : "",
#else
                                   setup->OSDMessage ? " -O --svdrpport=6419 " : "",
#endif
                                   setup->NoMargins ? " -i 4 " : "",
                                   setup->SecondPass ? "" : " --pass1only ",
                                   setup->Log2Rec ? " -R " : "",
                                   logodir,Direct ? "-O after" : "--online=2 before",
                                   FileName);
    usleep(1000000); // wait 1 second
    if (SystemExec(cmd)!=-1)
    {
        dsyslog("markad: executing %s",*cmd);
        usleep(200000);
        int pos=Add(FileName,Name);
        if (getPid(pos) && getStatus(pos))
        {
            if (setup->ProcessDuring==0)
            {
                if (!Direct)
                {
                    if (!setup->whileRecording)
                    {
                        Pause(NULL);
                    }
                    else
                    {
                        Pause(FileName);
                    }
                }
                else
                {
                    if (!setup->whileRecording && Recording())
                    {
                        Pause(FileName);
                    }
                    if (!setup->whileReplaying && Replaying())
                    {
                        Pause(FileName);
                    }
                }
            }
        } else {
            isyslog("markad: cannot find running process");
        }
        return true;
    }
    return false;
}

void cStatusMarkAd::TimerChange(const cTimer *Timer, eTimerChange Change)
{
    if (!Timer) return;
    if (Change!=tcDel) return;
    if (setup->ProcessDuring==2) return;
    if (time(NULL)>=Timer->StopTime()) return; // don't react on normal VDR timer deletion after recording
    Remove(Timer->File(),true);
}

bool cStatusMarkAd::LogoExists(const cDevice *Device,const char *FileName)
{
    if (!FileName) return false;
    char *cname=NULL;
#if APIVERSNUM>=20301
    const cTimer *timer=NULL;
    cStateKey StateKey;
    if (const cTimers *Timers = cTimers::GetTimersRead(StateKey)) {
        for (const cTimer *Timer=Timers->First(); Timer; Timer=Timers->Next(Timer))
#else
    cTimer *timer=NULL;
    for (cTimer *Timer = Timers.First(); Timer; Timer=Timers.Next(Timer))
#endif
        {
#if APIVERSNUM>=10722
            if (Timer->Recording() && const_cast<cDevice *>(Device)->IsTunedToTransponder(Timer->Channel()) &&
            (difftime(time(NULL),Timer->StartTime())<60))
            {
                timer=Timer;
                break;
            }
#else
            if (Timer->Recording() && Device->IsTunedToTransponder(Timer->Channel()) &&
                    (difftime(time(NULL),Timer->StartTime())<60))
            {
                timer=Timer;
                break;
            }
#endif
        }

        if (!timer) {
            esyslog("markad: cannot find timer for '%s'",FileName);
        } else {
            const cChannel *chan=timer->Channel();
            if (chan) cname=strdup(chan->Name());
        }

#if APIVERSNUM>=20301
        StateKey.Remove();
    }
#endif

    if (!timer) return false;
    if (!cname) return false;

    for (int i=0; i<(int) strlen(cname); i++)
    {
        if (cname[i]==' ') cname[i]='_';
        if (cname[i]=='.') cname[i]='_';
        if (cname[i]=='/') cname[i]='_';
    }

    char *fname=NULL;
    if (asprintf(&fname,"%s/%s-A16_9-P0.pgm",logodir,cname)==-1)
    {
        free(cname);
        return false;
    }

    struct stat statbuf;
    if (stat(fname,&statbuf)==-1)
    {
        free(fname);
        fname=NULL;
        if (asprintf(&fname,"%s/%s-A4_3-P0.pgm",logodir,cname)==-1)
        {
            free(cname);
            return false;
        }

        if (stat(fname,&statbuf)==-1)
        {
            free(cname);
            free(fname);
            return false;
        }
    }
    free(fname);
    free(cname);
    return true;
}

void cStatusMarkAd::Recording(const cDevice *Device, const char *Name,
                              const char *FileName, bool On)
{
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir) return; // we cannot operate without bindir
    if (!logodir) return; // we dont want to operate without logodir

    if (setup->ProcessDuring==2) {
        dsyslog("markad: deactivated by user");
        return; // markad deactivated
    }

    if (On)
    {
        if (setup->LogoOnly && !LogoExists(Device,FileName)) {
            dsyslog("markad: no logo found for %s",Name);
            return;
        }
        // Start markad with recording
        if (!Start(FileName,Name,false)) {
            esyslog("markad: failed starting on %s",FileName);
        }
    }
    else
    {
        if (!setup->ProcessDuring)
        {
            if (!setup->whileRecording)
            {
                if (!setup->whileReplaying)
                {
                    if (!Recording() && !Replaying()) Continue(NULL);
                }
                else
                {
                    if (!Recording()) Continue(NULL);
                }
            }
            else
            {
                Continue(FileName);
            }
        }
    }
}

bool cStatusMarkAd::getStatus(int Position)
{
    if (Position<0) return false;
    if (!recs[Position].Pid) return false;
    int ret=0;
    char procname[256]="";
    snprintf(procname,sizeof(procname),"/proc/%i/stat",recs[Position].Pid);
    FILE *fstat=fopen(procname,"r");
    if (fstat)
    {
        // found a running markad
        ret=fscanf(fstat,"%*10d %*255s %c",&recs[Position].Status);
        fclose(fstat);
    }
    else
    {
        if (errno==ENOENT)
        {
            // no such file or directory -> markad done or crashed
            // remove filename from list
            Remove(Position);
        }
    }
    return (ret==1);
}

bool cStatusMarkAd::getPid(int Position)
{
    if (Position<0) return false;
    if (!recs[Position].FileName) return false;
    if (recs[Position].Pid) return true;
    int ret=0;
    char *buf;
    if (asprintf(&buf,"%s/markad.pid",recs[Position].FileName)==-1) return false;

    FILE *fpid=fopen(buf,"r");
    if (fpid)
    {
        free(buf);
        int pid;
        ret=fscanf(fpid,"%10i\n",&pid);
        if (ret==1) recs[Position].Pid=pid;
        fclose(fpid);
    }
    else
    {
        if (errno==ENOENT)
        {
            // no such file or directory -> markad done or crashed
            // remove entry from list
            Remove(Position);
        }
        free(buf);
    }
    return (ret==1);
}

bool cStatusMarkAd::GetNextActive(struct recs **RecEntry)
{
    if (!RecEntry) return false;
    *RecEntry=NULL;

    if (actpos>=(MAXDEVICES*MAXRECEIVERS)) return false;

    do
    {
        if ((recs[actpos].FileName) && (recs[actpos].Pid))
        {
            if (getStatus(actpos))
            {
                /* check if recording directory still exists */
                if (access(recs[actpos].FileName,R_OK)==-1) {
                    Remove(actpos,true);
                } else {
                    *RecEntry=&recs[actpos++];
                    return true;
                }
            }
        }
        actpos++;
    }
    while (actpos<(MAXDEVICES*MAXRECEIVERS));

    return false;
}

void cStatusMarkAd::Check()
{
    struct recs *tmpRecs=NULL;
    ResetActPos();
    while (GetNextActive(&tmpRecs)) ;
}

bool cStatusMarkAd::MarkAdRunning()
{
    struct recs *tmpRecs=NULL;
    ResetActPos();
    GetNextActive(&tmpRecs);
    return (tmpRecs!=NULL);
}

int cStatusMarkAd::Get(const char *FileName, const char *Name)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (Name && recs[i].Name && !strcmp(recs[i].Name,Name)) return i;
        if (FileName && recs[i].FileName && !strcmp(recs[i].FileName,FileName)) return i;
    }
    return -1;
}

void cStatusMarkAd::Remove(const char *Name, bool Kill)
{
    if (!Name) return;
    int pos=Get(NULL,Name);
    if (pos==-1) return;
    Remove(pos,Kill);
}

void cStatusMarkAd::Remove(int Position, bool Kill)
{
    if (recs[Position].FileName) free(recs[Position].FileName);
    recs[Position].FileName=NULL;
    if (recs[Position].Name) free(recs[Position].Name);
    recs[Position].Name=NULL;

    if ((Kill) && (recs[Position].Pid))
    {
        if (getStatus(Position))
        {
            if ((recs[Position].Status=='R') || (recs[Position].Status=='S'))
            {
                dsyslog("markad: terminating pid %i",recs[Position].Pid);
                kill(recs[Position].Pid,SIGTERM);
            }
            else
            {
                dsyslog("markad: killing pid %i",recs[Position].Pid);
                kill(recs[Position].Pid,SIGKILL);
            }
        }
    }
    recs[Position].Status=0;
    recs[Position].Pid=0;
    recs[Position].ChangedbyUser=false;
}

int cStatusMarkAd::Add(const char *FileName, const char *Name)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (!recs[i].FileName)
        {
            recs[i].FileName=strdup(FileName);
            if (Name)
            {
                recs[i].Name=strdup(Name);
            }
            else
            {
                recs[i].Name=NULL;
            }
            recs[i].Status=0;
            recs[i].Pid=0;
            recs[i].ChangedbyUser=false;
            return i;
        }
    }
    return -1;
}

void cStatusMarkAd::Pause(const char *FileName)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (FileName)
        {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) &&
                    (recs[i].Pid) && (!recs[i].ChangedbyUser))
            {
                dsyslog("markad: pausing pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGTSTP);
            }
        }
        else
        {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser))
            {
                dsyslog("markad: pausing pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGTSTP);
            }
        }
    }
}

void cStatusMarkAd::Continue(const char *FileName)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (FileName)
        {
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) &&
                    (recs[i].Pid) && (!recs[i].ChangedbyUser) )
            {
                dsyslog("markad: resume pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGCONT);
            }
        }
        else
        {
            if ((recs[i].Pid) && (!recs[i].ChangedbyUser))
            {
                dsyslog("markad: resume pid %i",recs[i].Pid);
                kill(recs[i].Pid,SIGCONT);
            }
        }
    }
}
