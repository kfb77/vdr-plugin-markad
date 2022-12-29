/*
 * osd.h: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include "global.h"
#if !defined(__osd_h_) && defined(POSIX)
#define __osd_h_

extern "C" {
    #include "debug.h"
}

#define trcs(c) bind_textdomain_codeset("markad",c)
#define tr(s) dgettext("markad",s)

/**
 * send OSD message to VDR
 */
class cOSDMessage {
    public:

/** constuctor
 * @param hostName   name or IP address of VDR
 * @param portNumber port number for OSD messages
 */
        cOSDMessage(const char *hostName, int portNumber);

        ~cOSDMessage();

/**
 * send message to VDR OSD
 * @param format message format
 * @return 0 for success, -1 otherwise
 */
        int Send(const char *format, ...);

    private:

/**
 * copy OSM object (not used)
 */
        cOSDMessage(const cOSDMessage &cOSDMessageCopy);

/**
 * = operator for OSM object (not used)
 */
        cOSDMessage &operator=(const cOSDMessage &foo);

        char *host;                                      //!< VDR host name or IP address
                                                         //!<
        int port;                                        //!< VDR port number to send OSD messages
                                                         //!<
        char *msg     = NULL;                            //!< OSD message
                                                         //!<
        pthread_t tid = 0;                               //!< thread id of the OSD message
                                                         //!<
        static void *SendMessage(void *osd);             //!< send OSD message
                                                         //!<
        bool ReadReply(int fd, char **reply = NULL);     //!< read reply from OSD
                                                         //!<
};
#endif
