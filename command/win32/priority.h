#pragma once

/* None of those below is supported on WIN32.
 *
 * As no functionality is expected and not an error,
 * zero is returned and errno set to zero.
 *
 * Otherwise, these functions are just dummies. 
 */

#define PRIO_PROCESS 0 /* any integer, not evaluated. */

int getpriority(int which, int who);
int setpriority(int which, int who, int prio);
int ioprio_get(int which, int who);
int ioprio_set(int which, int who, int ioprio);
