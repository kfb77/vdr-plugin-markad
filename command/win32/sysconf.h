#pragma once

/*******************************************************************************
 * sysconf() is POSIX only.
 * Put a dummy here, which only answers about just one question; _SC_LONG_BIT;
 ******************************************************************************/
#define _SC_LONG_BIT 1001

long sysconf(int name);
