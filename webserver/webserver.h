/* Copyright (c) 2012, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

/* standard includes */
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>

/* location where the configuration file is stored */
#define CONF_FILE "/etc/webserver.conf"

/* the default UID and GID to run the webserver as after
  binding to the default listening port */
#define DEFAULT_SAFE_UID 1
#define DEFAULT_SAFE_GID 1

/* internally used return values */
#define FAIL -1
#define OK 0

/* max size to read for headers */
#define MAX_HEADER_SIZE 1024

/* max size of default type in conf file */
#define MAX_TYPE_LENGTH MAX_CONF_LINE_SIZE

/* size to allocate for date strings */
#define MAX_DATE_SIZE 128

#define HTTP_PORT "80"
#define LISTEN_BACKLOG 10 //max unaccepted connections that listen() will queue

/*HTTP Status Codes*/
#define HTTP_OK                 200
#define HTTP_NOT_FOUND          404
#define HTTP_ACCESS_DENIED      403
#define HTTP_NOT_IMPLEMENTED    501
#define HTTP_SERVER_ERROR       500
#define HTTP_BAD_REQUEST        400

/*HTTP Status Reasons */
#define HTTP_OK_R                 "OK"
#define HTTP_NOT_FOUND_R          "Not Found"
#define HTTP_ACCESS_DENIED_R      "Forbidden"
#define HTTP_NOT_IMPLEMENTED_R    "Not Implemented"
#define HTTP_SERVER_ERROR_R       "Server Error"
#define HTTP_BAD_REQUEST_R        "Bad Request"

/* default http type for responses (ie text/html) */
#define DEFAULT_TYPE "text/html"

/* Configuration File Options */
#define CONF_HOME               "Home"
#define CONF_MAX_READ           "MaxReadSize"
#define CONF_DEFAULT_TYPE       "DefaultType"
#define CONF_DEFAULT_PAGE       "DefaultPage"
#define CONF_UID                "UID"
#define CONF_GID                "GID"
#define CONF_COMMENT            '#'
#define CONF_POST_OPTION        " " //delimiter between above options and argument




/* max size of a single line in the conf file */
#define MAX_CONF_LINE_SIZE 512

/* largest URL, including parameters, to accept */
#define MAX_URL_SIZE 512


/* reads in configuration file */
int read_conf(void);
/* receives and responds to requests */
int request_handler(int);
/* reads request header */
char * read_headers(int,int *);
/* reads request body */

char * read_body(int,int *);
/* responds to get requests */
int serve_get_request(int,char *,char *,int,char *,int);
/* verifies that the server is allowed to serve the requested URL */
int check_access(char *);
/* sends an error header to the client */
void send_error(int,int);
/* builds a header section with the given response code and type */
char * generate_headers(int,char *, int, int *);
/* builds the body as specified by the request URL */
char * generate_body(char *, int *);
/* finds size of file */
off_t fsize(char *);
/* reads one line from a file */
int get_line(FILE *,char *);
/* puts formatted date in passed buffer */
int get_date(char *);
/* turns a URL into a local filename */
char * get_filename(char *);
/* reclaims exited child processes */
void wait_children(int);


/* global variables */
extern int max_header_size; //the max number of chars to read as part of header
extern char default_type[]; //default type to send files as
extern char home_dir[]; //directory to serve files out of
extern int safe_uid; //nonroot user to run as after binding to port
extern int safe_gid; //nonroot group to run as after binding to port

#define HELP_STRING "Usage: %s\nConfiguration file should be located at %s\n"

#define CONF_HELP_STRING "\nHELP:\nConfiguration file should be at %s\n" \
    "Each line should be at most %d characters long.\n" \
    "Comments must be at the beginning of a line and start with %c\n" \
    "Valid options are:\n" \
    "\t%s%s<path> - Sets directory that files are allowed to be served from.\n\t\t Must be set and an absolute path in order for server to work.\n" \
    "\t%s%s<size> - Sets the largest allowed HTTP header size.\n\t\t Default is: %d\n" \
    "\t%s%s<type> - Sets the HTTP type to be used for serving files.\n\t\t Default is: %s\n" \
    "\t%s%s<filename> - File to serve if the \"/\" url is requested.\n\t\t If not specified, a simple diagnostic page will be returned.\n\t\t Can be absolute path or relative to Home.\n" \
    "\n"


/* This macro allows debug output to be enabled and disabled throughout
 * the entire program with one flag */
#ifdef DEBUG
#define debug_printf(args...) fprintf(stderr,"DEBUG: " args)
#else
#define debug_printf(args...) ;
#endif

#endif
