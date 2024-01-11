/*
 * tools.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "tools.h"
#include "debug.h"
#include <string>
#include <algorithm>

cTools::cTools() {
}

cTools::~cTools() {
}

bool cTools::CompareChannelName(const char *nameA, const char *nameB, const int flags) {
    std::string name1(nameA);
    std::string name2(nameB);

    // remove "_HD"
    if (flags & IGNORE_HD) {
        size_t pos = name1.find( "_HD" );
        if (pos != std::string::npos) name1.replace(pos, 3, "");
        pos = name2.find( "_HD" );
        if (pos != std::string::npos) name2.replace(pos, 3, "");
    }

    // change to uppercase
    std::transform(name1.begin(), name1.end(), name1.begin(), ::toupper);
    std::transform(name2.begin(), name2.end(), name2.begin(), ::toupper);

    // remove _Austria
    if (flags & IGNORE_COUNTRY) {
        size_t pos = name1.find( "_AUSTRIA" );
        if (pos != std::string::npos) name1.replace(pos, 8, "");
        pos = name2.find( "_AUSTRIA" );
        if (pos != std::string::npos) name2.replace(pos, 8, "");
    }

    // remove fill character "_"
    while (true) {
        size_t pos = name1.find( "_" );
        if (pos == std::string::npos) break;
        name1.replace(pos, 1, "");
    }
    while (true) {
        size_t pos = name2.find( "_" );
        if (pos == std::string::npos) break;
        name2.replace(pos, 1, "");
    }

    // compare names
    if (name1.compare(name2) == 0) {
        dsyslog("cTools::CompareChannelName(): identical -> nameA %s, nameB %s, name1 %s, name2 %s, flags %d", nameA, nameB, name1.c_str(), name2.c_str(), flags);
        return true;  // we have an exact match
    }
    else {
#ifdef DEBUG_CHANNEL_NAME
        dsyslog("cTools::CompareChannelName(): different -> nameA %s, nameB %s, name1 %s, name2 %s, flags %d", nameA, nameB, name1.c_str(), name2.c_str(), flags);
#endif
        return false;
    }
    return false;
}
