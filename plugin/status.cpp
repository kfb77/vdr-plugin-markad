/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <signal.h>

#include "debug.h"
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
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++) {
        Remove(i,true);
    }
}


int cStatusMarkAd::Recording() {
    int cnt=0;
    for (int i=0; i<cDevice::NumDevices(); i++) {
        cDevice *dev=cDevice::GetDevice(i);
        if (dev) {
            if (dev->Receiving()) cnt++;
        }
    }
    return cnt;
}


bool cStatusMarkAd::Replaying() {
    for (int i=0; i<cDevice::NumDevices(); i++) {
        cDevice *dev=cDevice::GetDevice(i);
        if (dev) {
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


bool cStatusMarkAd::Start(const char *FileName, const char *Name, const bool Direct) {
    if ((Direct) && (Get(FileName)!=-1)) return false;

    char *autoLogoOption = NULL;
    if (setup->autoLogoConf > 0) {
        if(! asprintf(&autoLogoOption," --autologo=%i ",setup->autoLogoConf)) {
            esyslog("markad: asprintf ouf of memory");
            return false;
        }
        ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
    }
    else {
        if (setup->autoLogoMenue > 0) {
            if(! asprintf(&autoLogoOption," --autologo=%i ",setup->autoLogoMenue)) {
                esyslog("markad: asprintf ouf of memory");
                return false;
            }
            ALLOC(strlen(autoLogoOption)+1, "autoLogoOption");
        }
    }
    cString cmd = cString::sprintf("\"%s\"/markad %s%s%s%s%s%s%s%s%s%s%s%s%s -l \"%s\" %s \"%s\"",
                                   bindir,
                                   setup->Verbose ? " -v " : "",
                                   setup->SaveInfo ? " -I " : "",
                                   setup->GenIndex ? " -G " : "",
                                   setup->OSDMessage ? " -O --svdrpport=6419 " : "",
                                   setup->NoMargins ? " -i 4 " : "",
                                   setup->SecondPass ? "" : " --pass1only ",
                                   setup->Log2Rec ? " -R " : "",
                                   setup->LogLevel ? setup->LogLevel : "",
                                   setup->aStopOffs ? setup->aStopOffs : "",
                                   setup->cDecoder ? " --cDecoder " : "",
                                   setup->MarkadCut ? " --cut " : "",
                                   setup->ac3ReEncode ? " --ac3reencode " : "",
                                   autoLogoOption ? autoLogoOption : "",
                                   logodir,
                                   Direct ? "-O after" : "--online=2 before",
                                   FileName);
    FREE(strlen(autoLogoOption)+1, "autoLogoOption");
    free(autoLogoOption);

    usleep(1000000); // wait 1 second
    if (SystemExec(cmd)!=-1) {
        dsyslog("markad: executing %s",*cmd);
        usleep(200000);
        int pos=Add(FileName,Name);
        if (getPid(pos) && getStatus(pos)) {
            if (setup->ProcessDuring==0) {
                if (!Direct) {
                    if (!setup->whileRecording) Pause(NULL);
                    else Pause(FileName);
                }
                else {
                    if (!setup->whileRecording && Recording()) Pause(FileName);
                    if (!setup->whileReplaying && Replaying()) Pause(FileName);
                }
            }
        }
        else isyslog("markad: cannot find running process");
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
#ifdef DEBUGMEM
    memList();
#endif
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


bool cStatusMarkAd::getPid(int Position) {
    if (Position<0) return false;
    if (!recs[Position].FileName) return false;
    if (recs[Position].Pid) return true;
    int ret=0;
    char *buf;
    if (asprintf(&buf,"%s/markad.pid",recs[Position].FileName)==-1) return false;
    ALLOC(strlen(buf)+1, "buf");

    usleep(500*1000);   // wait 500ms to give markad time to create pid file
    FILE *fpid=fopen(buf,"r");
    if (fpid) {
        FREE(strlen(buf)+1, "buf");
        free(buf);
        int pid;
        ret=fscanf(fpid,"%10i\n",&pid);
        if (ret==1) recs[Position].Pid=pid;
        fclose(fpid);
    }
    else {
        esyslog("markad: failed to open pid file %s with errno %i", buf, errno);
        if (errno==ENOENT) {
            // no such file or directory -> markad done or crashed
            // remove entry from list
            Remove(Position);
        }
        FREE(strlen(buf)+1, "buf");
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

void cStatusMarkAd::Remove(int pos, bool Kill)
{
    if (recs[pos].FileName) {
        FREE(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
        free(recs[pos].FileName);
    }
    recs[pos].FileName=NULL;
    if (recs[pos].Name) {
        FREE(strlen(recs[pos].Name)+1, "recs[pos].Name");
        free(recs[pos].Name);
    }
    recs[pos].Name=NULL;

    if ((Kill) && (recs[pos].Pid))
    {
        if (getStatus(pos))
        {
            if ((recs[pos].Status=='R') || (recs[pos].Status=='S'))
            {
                dsyslog("markad: terminating pid %i",recs[pos].Pid);
                kill(recs[pos].Pid,SIGTERM);
            }
            else
            {
                dsyslog("markad: killing pid %i",recs[pos].Pid);
                kill(recs[pos].Pid,SIGKILL);
            }
        }
    }
    recs[pos].Status=0;
    recs[pos].Pid=0;
    recs[pos].ChangedbyUser=false;
}

int cStatusMarkAd::Add(const char *FileName, const char *Name)
{
    for (int pos = 0; pos < (MAXDEVICES*MAXRECEIVERS); pos++)
    {
        if (!recs[pos].FileName)
        {
            recs[pos].FileName=strdup(FileName);
            ALLOC(strlen(recs[pos].FileName)+1, "recs[pos].FileName");
            if (Name)
            {
                recs[pos].Name=strdup(Name);
                ALLOC(strlen(recs[pos].Name)+1, "recs[pos].Name");
            }
            else
            {
                recs[pos].Name=NULL;
            }
            recs[pos].Status=0;
            recs[pos].Pid=0;
            recs[pos].ChangedbyUser=false;
            return pos;
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
