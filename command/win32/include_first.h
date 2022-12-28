#pragma once

/*******************************************************************************
 * These defines need to be included before any windows api header.
 ******************************************************************************/

/* just to be shure */
#ifndef _WIN32
   #define _WIN32 1
#endif

#ifdef WINVER
   #undef WINVER
#endif

#ifdef _WIN32_WINNT
   #undef _WIN32_WINNT
#endif

/* 0x0601 -> Win7 or better */
#define WINVER       0x0601
#define _WIN32_WINNT 0x0601

/* __cplusplus for C++20 */
#define CPLUSPLUS_20 202002L
