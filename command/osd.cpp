/*
 * osd.cpp: A program for the Video Disk Recorder
 *
 * See the README file for copyright information and how to reach the author.
 *
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <libintl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>

#include "osd.h"
#include "debug.h"


cOSDMessage::cOSDMessage(const char *hostName, int portNumber) {
    msg  = NULL;
    host = strdup(hostName);
    ALLOC(strlen(host)+1, "host");
    port = portNumber;
    SendMessage(this);
}


cOSDMessage::~cOSDMessage() {
    if (tid) pthread_join(tid, NULL);
    if (msg) {
        FREE(strlen(msg)+1, "msg");
        free(msg);
    }
    if (host) {
        FREE(strlen(host)+1, "host");
	free(host);
    }
}


bool cOSDMessage::ReadReply(int fd, char **reply) {
    usleep(400000);
    char c = ' ';
    int repsize = 0;
    int msgsize = 0;
    if (reply) *reply = NULL;
    do {
        struct pollfd fds;
        fds.fd = fd;
        fds.events = POLLIN;
        fds.revents = 0;
        int ret = poll(&fds, 1, 600);

        if (ret <= 0) return false;
        if (fds.revents != POLLIN) return false;
        if (read(fd, &c, 1) < 0) return false;
        if ((reply) && (c != 10) && (c != 13)) {
            msgsize++;
            while ((msgsize + 5) > repsize) {
                repsize += 80;
                char *tmp = static_cast<char *>(realloc(*reply, repsize));
                if (!tmp) {
                    free(*reply);
                    *reply = NULL;
                    return false;
                }
                else {
                    *reply = tmp;
                }
            }
            (*reply)[msgsize - 1] = c;
            (*reply)[msgsize] = 0;
        }
    }
    while (c != '\n');
    return true;
}


void *cOSDMessage::SendMessage(void *posd) {
    cOSDMessage *osd = static_cast<cOSDMessage *>(posd);

    const struct hostent *host = gethostbyname(osd->host);
    if (!host) {
        osd->tid = 0;
        return NULL;
    }

    struct sockaddr_in name;
    name.sin_family = AF_INET;
    name.sin_port = htons(osd->port);
    memcpy(&name.sin_addr.s_addr, host->h_addr, host->h_length);
    uint size = sizeof(name);

    int sock;
    sock=socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) return NULL;

    if (connect(sock, reinterpret_cast<struct sockaddr *>(&name), size) != 0 ) {
        close(sock);
        return NULL;
    }

    char *reply = NULL;
    if (!osd->ReadReply(sock, &reply)) {
        if (reply) free(reply);
        close(sock);
        return NULL;
    }

    ssize_t ret;
    if (osd->msg) {
        if (reply) free(reply);
        ret=write(sock, "MESG ", 5);
        if (ret != (ssize_t) - 1) ret = write(sock,osd->msg,strlen(osd->msg));
        if (ret != (ssize_t) - 1) ret = write(sock, "\r\n", 2);

        if (!osd->ReadReply(sock) || (ret == (ssize_t) - 1)) {
            close(sock);
            return NULL;
        }
    }
    else {
        if (reply) {
            char *cs = strrchr(reply, ';');
            if (cs) {
                cs += 2;
                trcs(cs);
            }
            else {
                trcs("UTF-8"); // just a guess
            }
            free(reply);
        }
        else {
            trcs("UTF-8"); // just a guess
        }
    }
    ret=write(sock, "QUIT\r\n", 6);
    if (ret != (ssize_t) - 1) osd->ReadReply(sock);
    close(sock);
    return NULL;
}


int cOSDMessage::Send(const char *format, ...) {
    if (tid) pthread_join(tid, NULL);
    if (msg) free(msg);
    va_list ap;
    va_start(ap, format);
    if (vasprintf(&msg, format, ap) == -1) {
        va_end(ap);
        return -1;
    }
    ALLOC(strlen(msg)+1, "msg");
    va_end(ap);

    if (pthread_create(&tid, NULL, reinterpret_cast<void *(*) (void *)>(&SendMessage), reinterpret_cast<void *>(this)) != 0 ) return -1;
    return 0;
}
