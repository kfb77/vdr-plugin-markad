/*
 * criteria.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "debug.h"
#include "criteria.h"


cCriteria::cCriteria(const char *channelNameParam) {
    channelName = channelNameParam;
}


cCriteria::~cCriteria() {
}


const char *cCriteria::GetChannelName() const {
    return channelName;
}


bool cCriteria::GoodVPS() {
    if (!channelName) return false;

//    if (CompareChannelName(channelName, "arte",         IGNORE_HD))               return true;    // VPS start event before preview
    if (CompareChannelName(channelName, "Das_Erste",    IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "hr-fernsehen", IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "KiKA",         IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "NDR_FS",       IGNORE_HD | IGNORE_CITY)) return true;
    if (CompareChannelName(channelName, "rbb",          IGNORE_HD | IGNORE_CITY)) return true;
    if (CompareChannelName(channelName, "SR_Fernsehen", IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "tagesschau24", IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "WDR",          IGNORE_HD | IGNORE_CITY)) return true;
    if (CompareChannelName(channelName, "ZDF",          IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "ZDFinfo",      IGNORE_HD))               return true;
    if (CompareChannelName(channelName, "zdf_neo",      IGNORE_HD))               return true;

    return false;
}


int cCriteria::LogoFadeInOut() {
    if (!channelName) return FADE_ERROR;
    if (CompareChannelName(channelName, "BR_Fernsehen",   IGNORE_HD | IGNORE_CITY))    return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "Comedy_Central", IGNORE_HD))                  return FADE_OUT;           // logo stop before broadcast end
    if (CompareChannelName(channelName, "Das_Erste",      IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "Disney_Channel", IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "DMAX",           IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "FOX_Channel",    IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "kabel_eins",     IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "KiKA",           IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "N24_DOKU",       IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "Nickelodeon",    IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "NICK_MTV+",      IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "NRJ12",          IGNORE_NOTHING))             return FADE_IN;
    if (CompareChannelName(channelName, "Pro7_MAXX",      IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "ProSieben_MAXX", IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "RTLNITRO",       IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "RTLZWEI",        IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "SAT_1_Gold",     IGNORE_HD | IGNORE_COUNTRY)) return FADE_IN;
    if (CompareChannelName(channelName, "ServusTV",       IGNORE_HD))                  return FADE_OUT;
    if (CompareChannelName(channelName, "sixx",           IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "SUPER_RTL",      IGNORE_HD))                  return FADE_OUT;
    if (CompareChannelName(channelName, "SPORT1",         IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "SWR",            IGNORE_HD | IGNORE_CITY))    return FADE_IN;              // FADE_OUT only very short
    if (CompareChannelName(channelName, "TELE_5",         IGNORE_HD))                  return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "TLC",            IGNORE_HD | IGNORE_COUNTRY)) return FADE_IN | FADE_OUT;
    if (CompareChannelName(channelName, "VOXup",          IGNORE_HD))                  return FADE_OUT;
    if (CompareChannelName(channelName, "WELT",           IGNORE_HD))                  return FADE_IN;
    if (CompareChannelName(channelName, "ZDF",            IGNORE_HD))                  return FADE_IN | FADE_OUT;
    return FADE_NONE;
}


bool cCriteria::LogoInBorder() {
    if (!channelName) return false;
//    if (CompareChannelName(channelName, "ARD_alpha",      IGNORE_HD)) return true;       // in border with very small vertical border, out of border otherwise
    if (CompareChannelName(channelName, "arte",           IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "Bibel_TV",       IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "C8",             IGNORE_NOTHING)) return true;
    if (CompareChannelName(channelName, "Comedy_Central", IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "Disney_Channel", IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "N24_DOKU",       IGNORE_HD))      return true;
//    if (CompareChannelName(channelName, "NITRO",          IGNORE_HD)) return true;       // not in border
    if (CompareChannelName(channelName, "ONE_HD",         IGNORE_HD))      return true;
//    if (CompareChannelName(channelName, "RTLZWEI",        IGNORE_HD))      return true;  // sometimes in border, sometimes not
    if (CompareChannelName(channelName, "SUPER_RTL",      IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "TELE_5",         IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "TMC",            IGNORE_NOTHING)) return true;
    if (CompareChannelName(channelName, "ZDF",            IGNORE_HD))      return true;
    return false;
}


bool cCriteria::NoLogo() {   // channel were logo detection is not possible
    if (!channelName) return false;

    if (CompareChannelName(channelName, "Sky_Cinema_Special_HD", IGNORE_NOTHING)) return true;  // channel have no continuous logo
    if (CompareChannelName(channelName, "TF1",                   IGNORE_NOTHING)) return true;  // channel have no logo

    return false;
}


bool cCriteria::LogoInNewsTicker() {
    if (!channelName) return false;

    if (CompareChannelName(channelName, "n-tv", IGNORE_HD)) return true;
    if (CompareChannelName(channelName, "ntv", IGNORE_HD))  return true;
    if (CompareChannelName(channelName, "WELT", IGNORE_HD)) return true;

    return false;
}


bool cCriteria::InfoInBorder() {   // info logo or closing banner in one of the border
    if (!channelName) return false;
    if (CompareChannelName(channelName, "Disney_Channel",   IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "N24_DOKU",         IGNORE_HD))      return true;  // closing credits in border
    if (CompareChannelName(channelName, "NITRO",            IGNORE_HD))      return true;  // closing credits banner in lower border
    if (CompareChannelName(channelName, "RTLZWEI",          IGNORE_HD))      return true;
//  if (CompareChannelName(channelName, "SUPER_RTL",        IGNORE_HD)) return true;  // channel has black framed ad
    if (CompareChannelName(channelName, "SAT_1_Gold",       IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "TELE_5",           IGNORE_HD))      return true;
    if (CompareChannelName(channelName, "TF1",              IGNORE_NOTHING)) return true;  // closing banner in border
    if (CompareChannelName(channelName, "TF1_Séries_Films", IGNORE_NOTHING)) return true;  // closing banner in border
    if (CompareChannelName(channelName, "TV5MONDE_EUROPE",  IGNORE_HD))      return true;  // blue/white banner in border
    if (CompareChannelName(channelName, "VOX",              IGNORE_HD))      return true;  // closing credits banner in lower border
    if (CompareChannelName(channelName, "zdf_neo",          IGNORE_HD))      return true;
    return false;
}


// logo change color during broadcast
bool cCriteria::LogoColorChange() {
    if (!channelName) return false;
    if (CompareChannelName(channelName, "Disney_Channel", IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "DMAX",           IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "FOX_Channel",    IGNORE_HD))                  return true;   // changes from white to red at recording start
    if (CompareChannelName(channelName, "kabel_eins",     IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "NRJ12",          IGNORE_NOTHING))             return true;
    if (CompareChannelName(channelName, "RTL_Television", IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "TELE_5",         IGNORE_HD))                  return true;
    return false;
}


bool cCriteria::IsLogoRotating() {
    if (!channelName) return false;
    if (CompareChannelName(channelName, "SAT_1",   IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "SAT_1_A", IGNORE_NOTHING))             return true;
    return false;
}


bool cCriteria::LogoTransparent() {
    if (!channelName) return false;
    if (CompareChannelName(channelName, "NITRO", IGNORE_HD)) return true;
    if (CompareChannelName(channelName, "SRF_zwei", IGNORE_HD)) return true;
    return false;

}

bool cCriteria::IsInfoLogoChannel() {
    if (!channelName) return false;
    // for performance reason only known and tested channels
    if (CompareChannelName(channelName, "C8",              IGNORE_NOTHING))             return true;
    if (CompareChannelName(channelName, "Comedy_Central",  IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "DMAX",            IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "kabel_eins",      IGNORE_HD))                  return true;   // only for info (e.g. Teletext)
    if (CompareChannelName(channelName, "Kabel_1_Austria", IGNORE_HD | IGNORE_COUNTRY)) return true;   // only for info (e.g. Teletext)
    if (CompareChannelName(channelName, "SIXX",            IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "SAT_1",           IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "WELT",            IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "RTL2",            IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "TMC",             IGNORE_NOTHING))             return true;
    return false;
}


bool cCriteria::IsLogoInterruptionChannel() {
    if (!channelName) return false;
    // for performance reason only known and tested channels
    if (CompareChannelName(channelName, "Comedy_Central",  IGNORE_HD))                  return true;  // has short logo blend out

    return false;
}


bool cCriteria::IsLogoChangeChannel() {
    if (!channelName) return false;
    // for performance reason only known and tested channels
    if (CompareChannelName(channelName, "TELE_5",          IGNORE_HD | IGNORE_COUNTRY)) return true;  // has special logo changes

    return false;
}


bool cCriteria::IsClosingCreditsChannel() {
    if (!channelName) return false;
    // for performance reason only known and tested channels
    if (CompareChannelName(channelName, "Kabel_1_Austria", IGNORE_HD) ||
            CompareChannelName(channelName, "kabel_eins",  IGNORE_HD) ||
            CompareChannelName(channelName, "krone_tv",    IGNORE_HD) ||
            CompareChannelName(channelName, "SAT_1",       IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "SAT_1_Gold",  IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "SIXX",        IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "DMAX",        IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "Pro7_MAXX",   IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "RTL2",        IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "ProSieben",   IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "ZDF",         IGNORE_HD)) {
        return true;
    }
    return false;
}


bool cCriteria::IsAdInFrameWithLogoChannel() {
    if (!channelName) return false;
// for performance reason only for known and tested channels
    if (CompareChannelName(channelName, "DMF",             IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "Kabel_1_Austria", IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "kabel_eins",      IGNORE_HD))                  return true;
    if (CompareChannelName(channelName, "Pro7_MAXX",       IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "ProSieben",       IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "ProSieben_MAXX",  IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "RTL2",            IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "RTLZWEI",         IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "RTL_Television",  IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "SAT_1",           IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "SIXX",            IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "VOX",             IGNORE_HD | IGNORE_COUNTRY)) return true;
    if (CompareChannelName(channelName, "VOXup",           IGNORE_HD | IGNORE_COUNTRY)) return true;
//          if (CompareChannelName(channelName, "WELT",           IGNORE_HD | IGNORE_COUNTRY)) return true;  // too much false positiv because of news ticker
    return false;
}


bool cCriteria::IsIntroductionLogoChannel() {
    if (!channelName) return false;
// for performance reason only for known and tested channels for now
    if (CompareChannelName(channelName, "Kabel_1_Austria", IGNORE_HD) ||
            CompareChannelName(channelName, "kabel_eins",  IGNORE_HD) ||
            CompareChannelName(channelName, "SIXX",        IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "SAT_1",       IGNORE_HD | IGNORE_COUNTRY) ||
            CompareChannelName(channelName, "RTL2",        IGNORE_HD | IGNORE_COUNTRY)) {
        return true;
    }
    return false;
}


int cCriteria::GetMarkTypeState(const int type) const {
    int state = CRITERIA_UNKNOWN;
    switch (type) {
    case MT_LOGOCHANGE:
        state = logo;
        break;
    case MT_VBORDERCHANGE:
        state = vborder;
        break;
    case MT_HBORDERCHANGE:
        state = hborder;
        break;
    case MT_ASPECTCHANGE:
        state = aspectratio;
        break;
    case MT_CHANNELCHANGE:
        state = channel;
        break;
    default:
        esyslog("cMarkCriteria::GetMarkTypeState(): type 0x%X not valid", type);
        return CRITERIA_UNKNOWN;
    }
    char *typeToText  = TypeToText(type);
    char *stateToText = StateToText(state);
    if (typeToText && stateToText) {
        dsyslog("cMarkCriteria::GetMarkTypeState(): %-17s: %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
    return state;
}


void cCriteria::SetMarkTypeState(const int type, const int state, const bool fullDecode) {
    char *typeToText  = TypeToText(type);
    char *stateToText = StateToText(state);
    if (typeToText && stateToText) {
        dsyslog("cMarkCriteria::SetMarkTypeState(): set mark type for %-12s to %s", typeToText, stateToText);
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
    }
    switch (type) {
    case MT_LOGOCHANGE:
        logo = state;
        if (logo == CRITERIA_USED) {
            SetDetectionState(MT_VBORDERCHANGE, false);
            SetDetectionState(MT_HBORDERCHANGE, false);
            SetDetectionState(MT_ASPECTCHANGE,  false);
            if (GetMarkTypeState(MT_CHANNELCHANGE) < CRITERIA_AVAILABLE) SetDetectionState(MT_CHANNELCHANGE, false); // if we have 6 channel, but no channel change, keep channel detection
        }
        if (logo == CRITERIA_UNKNOWN) {
            SetDetectionState(MT_LOGOCHANGE,    true);
        }
        if (logo == CRITERIA_DISABLED) {
            SetDetectionState(MT_LOGOCHANGE,    false);
        }
        break;
    case MT_VBORDERCHANGE:
        vborder = state;
        if (vborder == CRITERIA_USED) {
            SetDetectionState(MT_SCENECHANGE,   false);
            SetDetectionState(MT_SOUNDCHANGE,   true);   // use silence to select black screen mark
            SetDetectionState(MT_BLACKCHANGE,   true);   // use black screen to correct vborder marks
            SetDetectionState(MT_LOGOCHANGE,    false);
            SetDetectionState(MT_HBORDERCHANGE, false);
            SetDetectionState(MT_ASPECTCHANGE,  false);
            SetDetectionState(MT_CHANNELCHANGE, false);
        }
        if (vborder == CRITERIA_UNAVAILABLE) {
            SetDetectionState(MT_VBORDERCHANGE, false);
        }
        break;
    case MT_HBORDERCHANGE:
        if ((GetMarkTypeState(MT_ASPECTCHANGE) == CRITERIA_USED) && (state == CRITERIA_USED)) hborder = CRITERIA_AVAILABLE; // do not override use of aspect ratio
        else hborder = state;
        if (hborder == CRITERIA_USED) {
            SetDetectionState(MT_SCENECHANGE,   false);
            SetDetectionState(MT_SOUNDCHANGE,   false);
            SetDetectionState(MT_BLACKCHANGE,   false);
            SetDetectionState(MT_LOGOCHANGE,    false);
            SetDetectionState(MT_VBORDERCHANGE, false);
            SetDetectionState(MT_ASPECTCHANGE,  false);
            if (GetMarkTypeState(MT_CHANNELCHANGE) < CRITERIA_AVAILABLE) SetDetectionState(MT_CHANNELCHANGE, false); // if we have 6 channel, but no channel change, keep channel detection
        }
        if (hborder == CRITERIA_UNAVAILABLE) {
            SetDetectionState(MT_HBORDERCHANGE, false);
        }
        break;
    case MT_ASPECTCHANGE:
        aspectratio = state;
        switch (aspectratio) {
        case CRITERIA_USED:
            if (fullDecode) SetDetectionState(MT_SCENECHANGE, true);   // aspect ratio changes are not frame exact, use scene change to optimize
            SetDetectionState(MT_SOUNDCHANGE,   false);
            SetDetectionState(MT_BLACKCHANGE,    true);   // aspect ratio changes are not frame exact, use black screen to optimize
            SetDetectionState(MT_LOGOCHANGE,    false);
            SetDetectionState(MT_VBORDERCHANGE, false);
            SetDetectionState(MT_HBORDERCHANGE, false);
            SetDetectionState(MT_CHANNELCHANGE, false);
            break;
        case CRITERIA_UNAVAILABLE:
            SetDetectionState(MT_ASPECTCHANGE,  false);
            break;
        default:
            break;
        }
        break;
    case MT_CHANNELCHANGE:
        channel = state;
        switch (channel) {
        case CRITERIA_USED:
            if (fullDecode) SetDetectionState(MT_SCENECHANGE, true);   // channel change give not exact video frame mark
            SetDetectionState(MT_SOUNDCHANGE,   false);
            SetDetectionState(MT_BLACKCHANGE,    true);   // channel change give not exact video frame mark
            SetDetectionState(MT_LOGOCHANGE,    false);
            SetDetectionState(MT_VBORDERCHANGE, false);
            SetDetectionState(MT_HBORDERCHANGE, false);
            SetDetectionState(MT_ASPECTCHANGE,  false);
            break;
        case CRITERIA_UNAVAILABLE:
            SetDetectionState(MT_CHANNELCHANGE,  false);
            break;
        default:
            break;
        }
        break;
    default:
        esyslog("cMarkCriteria::SetMarkTypeState(): type 0x%X not valid", type);
    }
}


void cCriteria::ListMarkTypeState() const {
    GetMarkTypeState(MT_LOGOCHANGE);
    GetMarkTypeState(MT_VBORDERCHANGE);
    GetMarkTypeState(MT_HBORDERCHANGE);
    GetMarkTypeState(MT_ASPECTCHANGE);
    GetMarkTypeState(MT_CHANNELCHANGE);
}


int cCriteria::GetClosingCreditsState(const int position) const {
    if (position == closingCreditsPos) {
        char *stateToText = StateToText(closingCreditsState);
        dsyslog("cMarkCriteria::GetClosingCreditState(): closing credits state for (%d) is %s", position, stateToText);
        FREE(strlen(stateToText)+1, "state");
        free(stateToText);
        return closingCreditsState;
    }
    else return CRITERIA_UNKNOWN;
}


void cCriteria::SetClosingCreditsState(const int position, const int state) {
    char *stateToText = StateToText(state);
    dsyslog("cMarkCriteria::SetClosingCreditState(): set closing credits state for (%d) to %s", position, stateToText);
    FREE(strlen(stateToText)+1, "state");
    free(stateToText);
    closingCreditsState = state;
    closingCreditsPos   = position;
}


// define text to mark status of broadcast
// return pointer to text, calling function has to free memory
//
char *cCriteria::StateToText(const int state) {
    char *text = nullptr;
    switch (state) {
    case CRITERIA_USED:
        if (asprintf(&text, "used") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        break;
    case CRITERIA_AVAILABLE:
        if (asprintf(&text, "available") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        break;
    case CRITERIA_UNKNOWN:
        if (asprintf(&text, "unknown") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        break;
    case CRITERIA_UNAVAILABLE:
        if (asprintf(&text, "unavailable") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        break;
    case CRITERIA_DISABLED:
        if (asprintf(&text, "DISABLED") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        break;
    default:
        if (asprintf(&text, "state invalid") != -1) {
            ALLOC(strlen(text)+1, "state");
        }
        esyslog("cMarkCriteria::StateToText(): state %d not valid", state);
        break;
    }
    return text;
}


bool cCriteria::GetDetectionState(const int type) const {
    bool state = true;
    switch (type) {
    case MT_SCENECHANGE:
        state = sceneDetection;
        break;
    case MT_SOUNDCHANGE:
        state = soundDetection;
        break;
    case MT_LOWERBORDERCHANGE:
        state = lowerBorderDetection;
        break;
    case MT_BLACKCHANGE:
        state = blackscreenDetection;
        break;
    case MT_LOGOCHANGE:
        state = logoDetection;
        break;
    case MT_VBORDERCHANGE:
        state = vborderDetection;
        break;
    case MT_HBORDERCHANGE:
        state = hborderDetection;
        break;
    case MT_ASPECTCHANGE:
        state = aspectratioDetection;
        break;
    case MT_CHANNELCHANGE:
        state = channelDetection;
        break;
    case MT_VIDEO:
        state = videoDecoding;
        break;
    case MT_AUDIO:
        state = audioDecoding;
        break;
    default:
        esyslog("cMarkCriteria::GetDetectionState(): type 0x%X not valid", type);
    }
    return state;
}


void cCriteria::SetDetectionState(const int type, const bool state) {
    switch (type) {
    case MT_SCENECHANGE:
        sceneDetection = state;
        break;
    case MT_SOUNDCHANGE:
        soundDetection = state;
        break;
    case MT_LOWERBORDERCHANGE:
        lowerBorderDetection = state;
        break;
    case MT_BLACKCHANGE:
        blackscreenDetection = state;
        break;
    case MT_LOGOCHANGE:
        logoDetection = state;
        break;
    case MT_VBORDERCHANGE:
        vborderDetection = state;
        break;
    case MT_HBORDERCHANGE:
        hborderDetection = state;
        break;
    case MT_ASPECTCHANGE:
        aspectratioDetection = state;
        break;
    case MT_CHANNELCHANGE:
        channelDetection = state;
        break;
    case MT_ALL:
        sceneDetection       = state;
        soundDetection       = state;
        lowerBorderDetection = state;
        blackscreenDetection = state;
        logoDetection        = state;
        vborderDetection     = state;
        hborderDetection     = state;
        aspectratioDetection = state;
        channelDetection     = state;
        break;
    default:
        esyslog("cMarkCriteria::SetDetectionState(): type 0x%X not valid", type);
        break;
    }
    if (GetDetectionState(MT_SCENECHANGE) || GetDetectionState(MT_LOWERBORDERCHANGE) || GetDetectionState(MT_BLACKCHANGE) || GetDetectionState(MT_LOGOCHANGE) || GetDetectionState(MT_VBORDERCHANGE) || GetDetectionState(MT_HBORDERCHANGE) || GetDetectionState(MT_ASPECTCHANGE)) videoDecoding = true;
    else videoDecoding = false;

    if (GetDetectionState(MT_SOUNDCHANGE) || GetDetectionState(MT_CHANNELCHANGE)) audioDecoding = true;
    else audioDecoding = false;

    char *typeToText  = TypeToText(type);
    if (typeToText) {
        dsyslog("cMarkCriteria::SetDetectionState(): set detection state for %-17s to %s, decoding: video is %s, audio is %s", typeToText, (state) ? "on" : "off", (videoDecoding) ? "on" : "off", (audioDecoding) ? "on" : "off");
        FREE(strlen(typeToText)+1, "text");
        free(typeToText);
    }
}


void cCriteria::ListDetection() const {
    dsyslog("cMarkCriteria::ListDetectionState(): MT_SCENECHANGE:       %s", GetDetectionState(MT_SCENECHANGE)       ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_SOUNDCHANGE:       %s", GetDetectionState(MT_SOUNDCHANGE)       ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_LOWERBORDERCHANGE: %s", GetDetectionState(MT_LOWERBORDERCHANGE) ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_BLACKCHANGE:       %s", GetDetectionState(MT_BLACKCHANGE)       ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_LOGOCHANGE:        %s", GetDetectionState(MT_LOGOCHANGE)        ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_VBORDERCHANGE:     %s", GetDetectionState(MT_VBORDERCHANGE)     ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_HBORDERCHANGE:     %s", GetDetectionState(MT_HBORDERCHANGE)     ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_ASPECTCHANGE:      %s", GetDetectionState(MT_ASPECTCHANGE)      ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_CHANNELCHANGE:     %s", GetDetectionState(MT_CHANNELCHANGE)     ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_VIDEO:             %s", GetDetectionState(MT_VIDEO)             ? "on" : "off");
    dsyslog("cMarkCriteria::ListDetectionState(): MT_AUDIO:             %s", GetDetectionState(MT_AUDIO)             ? "on" : "off");
}
