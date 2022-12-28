#pragma once

/* None of those below is supported on WIN32.
 *
 * As no functionality is expected and not an error,
 * zero is returned and errno set to zero.
 *
 * Otherwise, these functions are just dummies. 
 */

#define PRIO_PROCESS 0 /* any integer, not evaluated. */

int getpriority(int, int);
int setpriority(int, int, int);
int ioprio_get(int, int);
int ioprio_set(int, int, int);
