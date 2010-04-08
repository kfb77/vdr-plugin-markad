/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

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
        Remove(i);
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
    if (setup->whilePlaying) return;
    if (On)
    {
        Pause(NULL);
    }
    else
    {
        if (!Recording()) Continue(NULL);
    }
}

void cStatusMarkAd::Recording(const cDevice *UNUSED(Device), const char *Name,
                              const char *FileName, bool On)
{
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir) return; // we cannot operate without bindir
    if (!logodir) return; // we dont want to operate without logodir

    if (On)
    {
        // Start markad with recording
        cString cmd = cString::sprintf("\"%s\"/markad --online=2 -l \"%s\" before \"%s\"",bindir,
                                       logodir,FileName);
        if (SystemExec(cmd)!=-1)
        {
            usleep(200000);
            int pos=Add(FileName,Name);
            if (getPid(pos) && getStatus(pos))
            {
                if (!setup->ProcessDuring)
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
            }
        }
    }
    else
    {
        if (!setup->ProcessDuring)
        {
            if (!setup->whileRecording)
            {
                if (!setup->whilePlaying)
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
        ret=fscanf(fstat,"%*d %*s %c",&recs[Position].Status);
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
        ret=fscanf(fpid,"%i\n",&pid);
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

    if (actpos>=(MAXDEVICES*MAXRECEIVERS)) return true;

    do
    {
        if ((recs[actpos].FileName) && (recs[actpos].Pid))
        {
            if (getStatus(actpos))
            {
                *RecEntry=&recs[actpos++];
                break;
            }
        }
        actpos++;
    }
    while (actpos<(MAXDEVICES*MAXRECEIVERS));

    return true;
}

bool cStatusMarkAd::MarkAdRunning()
{
    struct recs *tmpRecs=NULL;
    ResetActPos();
    GetNextActive(&tmpRecs);
    return (tmpRecs!=NULL);
}

void cStatusMarkAd::Remove(int Position)
{
    if (recs[Position].FileName)
    {
        dsyslog("markad: removing %s at position %i",recs[Position].FileName,Position);
        free(recs[Position].FileName);
        recs[Position].FileName=NULL;
    }
    if (recs[Position].Name)
    {
        free(recs[Position].Name);
        recs[Position].Name=NULL;
    }
    recs[Position].Status=0;
    recs[Position].Pid=0;
}

int cStatusMarkAd::Add(const char *FileName, const char *Name)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (!recs[i].FileName)
        {
            dsyslog("markad: adding %s at position %i",FileName,i);
            recs[i].FileName=strdup(FileName);
            recs[i].Name=strdup(Name);
            recs[i].Status=0;
            recs[i].Pid=0;
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
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid))
            {
                kill(recs[i].Pid,SIGTSTP);
            }
        }
        else
        {
            if (recs[i].Pid)
            {
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
            if ((recs[i].FileName) && (!strcmp(recs[i].FileName,FileName)) && (recs[i].Pid))
            {
                kill(recs[i].Pid,SIGCONT);
            }
        }
        else
        {
            if (recs[i].Pid)
            {
                kill(recs[i].Pid,SIGCONT);
            }
        }
    }
}
