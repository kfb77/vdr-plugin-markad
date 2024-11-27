/*
 * tools.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "tools.h"
#include "debug.h"

#include <string>
#include <cstring>
#include <algorithm>
#include <math.h>

cTools::cTools() {
}

cTools::~cTools() {
}


void cTools::LogSeparator(const bool main) {
    if (main) dsyslog("=======================================================================================================================");
    else      dsyslog("-----------------------------------------------------------------------------------------------------------------------");
}


void cTools::StartSection(const char* name) {
    startSectionTime = std::chrono::high_resolution_clock::now();
    dsyslog("<<<<<<<<<< start section: %s <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<", name);
}


int cTools::EndSection(const char* name) const {
    std::chrono::high_resolution_clock::time_point stopSectionTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> durationSection = stopSectionTime - startSectionTime;
    int msElapsed = round(durationSection.count());
    dsyslog(">>>>>>>>>> end  section: %s: %5ds %3dms >>>>>>>>>>>>>>>>>", name, static_cast<int>(msElapsed / 1000), msElapsed % 1000);
    return msElapsed;
}


bool cTools::CompareChannelName(const char *nameA, const char *nameB, const int flags) {
    std::string name1(nameA);
    std::string name2(nameB);

    // remove "_HD"
    if (flags & IGNORE_HD) {
        size_t pos = name1.find("_HD");
        if (pos != std::string::npos) name1.replace(pos, 3, "");
        pos = name2.find("_HD");
        if (pos != std::string::npos) name2.replace(pos, 3, "");
    }

    // change to uppercase
    std::transform(name1.begin(), name1.end(), name1.begin(), ::toupper);
    std::transform(name2.begin(), name2.end(), name2.begin(), ::toupper);

    // remove state and country
    if (flags & IGNORE_COUNTRY) {
        for (const char *country : countries) {
            size_t pos    = name1.find(country);
            size_t length = strlen(country);
            if (pos != std::string::npos) name1.replace(pos, length, "");
            pos = name2.find(country);
            if (pos != std::string::npos) name2.replace(pos, length, "");
        }

    }

    // remove cities
    if (flags & IGNORE_CITY) {
        for (const char *city : cities) {
            size_t pos    = name1.find(city);
            size_t length = strlen(city);
            if (pos != std::string::npos) name1.replace(pos, length, "");
            pos = name2.find(city);
            if (pos != std::string::npos) name2.replace(pos, length, "");
        }
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
#ifdef DEBUG_CHANNEL_NAME
        dsyslog("cTools::CompareChannelName(): identical -> nameA %s, nameB %s, name1 %s, name2 %s, flags %d", nameA, nameB, name1.c_str(), name2.c_str(), flags);
#endif
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
