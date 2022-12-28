#pragma once
#include <cstddef> // size_t

/*******************************************************************************
 * sleep() is part of POSIX only, file unistd.h and therefore not available
 * on WIN32.
 *
 * Wrap C++11 to get similar, but returns always zero.
 ******************************************************************************/

unsigned sleep(size_t s);
