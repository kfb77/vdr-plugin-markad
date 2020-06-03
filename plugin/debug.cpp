/*
 * debug.cpp: A plugin for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#ifdef DEBUGMEM
#include "debug.h"
#include <vdr/plugin.h>

int memUse = 0;
void memAlloc(int size, int line, char *file, char *var) {
    memUse += size;
    dsyslog("markad: memdebug alloc %5d bytes, sum %5d bytes, line %4d, file %s, variable: %s", size, memUse, line, file, var);
    return;
}
void memFree(int size, int line, char *file, char *var) {
    memUse -= size;
    dsyslog("markad: memdebug  free %5d bytes, sum %5d bytes, line %4d, file %s, variable: %s", size, memUse, line, file, var);
    return;
}
#endif
