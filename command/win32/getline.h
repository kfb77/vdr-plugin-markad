#pragma once

/*******************************************************************************
 * the C-function getline() is POSIX only.
 *
 * This replacement should work good enough.
 ******************************************************************************/

#include <cstdio> // type FILE

ssize_t getline(char** lineptr, size_t* n, FILE* stream);
