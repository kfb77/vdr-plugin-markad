/*
 * status.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "status.h"

void cStatusMarkAd::Recording(const cDevice *Device, const char *UNUSED(Name), const char *FileName, bool On)
{
    if (!Device) return; // just to be safe
    if (!FileName) return; // we cannot operate without a filename
    if (!bindir) return; // we cannot operate without bindir
    if (!logodir) return; // we dont want to operate without logodir

    if (On)
    {
        // Start markad with (before) recording
        cString cmd = cString::sprintf("\"%s\"/markad --online=2 -l \"%s\" before \"%s\"",bindir,
                                       logodir,FileName);
        if (SystemExec(cmd)!=-1)
        {
            Add(FileName);
        }
    }
    else
    {
#if 0
        // Start markad after recording
        cString cmd = cString::sprintf("\"%s\"/markad -l \"%s\" after \"%s\"",bindir,
                                       logodir,FileName);
        if (SystemExec(cmd)!=-1)
        {
            Add(FileName);
        }
#endif
        // TODO: Start second pass?
    }

}

cStatusMarkAd::cStatusMarkAd(const char *BinDir, const char *LogoDir)
{
    bindir=BinDir;
    logodir=LogoDir;
    memset(&recs,0,sizeof(recs));
}

cStatusMarkAd::~cStatusMarkAd()
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (recs[i]) free(recs[i]);
    }
}

bool cStatusMarkAd::MarkAdRunning()
{
    bool running=false;
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (recs[i])
        {
            char *buf;
            if (asprintf(&buf,"%s/markad.pid",recs[i])==-1)
            {
                // this is crude, but if we fail to allocate memory something
                // is going really wrong!
                return false;
            }
            // check for running markad process
            FILE *fpid=fopen(buf,"r");
            free(buf);
            if (fpid)
            {
                int pid,ret;
                ret=fscanf(fpid,"%i\n",&pid);
                fclose(fpid);
                if (ret==1)
                {
                    char procname[256]="";
                    snprintf(procname,sizeof(procname),"/proc/%i",pid);
                    struct stat statbuf;
                    if (stat(procname,&statbuf)==0)
                    {
                        // found a running markad
                        running=true;
                    }
                    else
                    {
                        if (errno==ENOENT)
                        {
                            // no such file or directory -> markad crashed?
                            // remove filename from list
                            free(recs[i]);
                            recs[i]=NULL;
                        }
                        else
                        {
                            // serious error? -> let vdr close
                            return false;
                        }
                    }
                }
            }
            else
            {
                if (errno==ENOENT)
                {
                    // no such file or directory -> markad already finished
                    // remove filename from list
                    free(recs[i]);
                    recs[i]=NULL;
                }
                else
                {
                    // serious error? -> let vdr close
                    return false;
                }
            }
        }
    }
    return running;
}

void cStatusMarkAd::Add(const char *FileName)
{
    for (int i=0; i<(MAXDEVICES*MAXRECEIVERS); i++)
    {
        if (!recs[i])
        {
            recs[i]=strdup(FileName);
        }
    }
}
