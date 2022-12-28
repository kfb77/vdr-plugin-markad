#pragma once
extern "C" { /* C-Linkage */
/*******************************************************************************
 * get parts of winsock2 without the whole stuff
 * in winsock2.h and windows.h
 ******************************************************************************/

struct servent {
  char*  s_name;
  char** s_aliases;
  #ifdef _WIN64
  char* s_proto;
  short s_port;
  #else
  short s_port;
  char* s_proto;
  #endif
};

struct servent* getservbyname(const char* name, const char* proto);
unsigned short htons(unsigned short hostshort);

} /* C-Linkage */
