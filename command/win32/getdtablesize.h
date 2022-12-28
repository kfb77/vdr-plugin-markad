#pragma once

/*******************************************************************************
 * getdtablesize() is defined in posix header unistd.h
 *
 * getdtablesize() returns the maximum number of files a process can
 * have open, one more than the largest possible value for a file
 * descriptor.
 *
 * Well, doesnt work this way on WIN32.
 * Lets return the number of simultaneously open files permitted at the
 * stream I/O level on WIN32.
 ******************************************************************************/

int getdtablesize(void);
