/*
 * vps.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vps.h"
#include "debug.h"

cVPS::cVPS(const char *directory) {
    if (!directory) return;

    char *fpath = NULL;
    if (asprintf(&fpath, "%s/%s", directory, "markad.vps") == -1) return;
    FILE *mf;
    mf = fopen(fpath, "r+");
    if (!mf) {
        dsyslog("cVPS::cVPS(): %s not found", fpath);
        free(fpath);
        return;
    }
    free(fpath);

    char   *line       = NULL;
    size_t length      = 0;
    char   typeVPS[16] = "";
    char   timeVPS[21] = "";
    int    offsetVPS   = 0;
    while (getline(&line, &length,mf) != -1) {
        sscanf(line, "%15s %20s %d", reinterpret_cast<char *>(&typeVPS), reinterpret_cast<char *>(&timeVPS), &offsetVPS);
        if (strcmp(typeVPS, "START:") == 0) {
            vpsStart = offsetVPS;
            dsyslog("cVPS::cVPS(): VPS START       event at offset %5ds", vpsStart);
        };
        if (strcmp(typeVPS, "STOP:") == 0) {
            vpsStop = offsetVPS;
            dsyslog("cVPS::cVPS(): VPS STOP        event at offset %5ds", vpsStop);
        };
        if (strcmp(typeVPS, "PAUSE_START:") == 0) {
            vpsPauseStart = offsetVPS;
            dsyslog("cVPS::cVPS(): VPS PAUSE START event at offset %5ds", vpsPauseStart);
        };
        if (strcmp(typeVPS, "PAUSE_STOP:") == 0) {
            vpsPauseStop = offsetVPS;
            dsyslog("cVPS::cVPS(): VPS PAUSE STOP  event at offset %5ds", vpsPauseStop);
        };
    }
    if (line) free(line);
    fclose(mf);
}


cVPS::~cVPS() {
}

