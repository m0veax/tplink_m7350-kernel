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


 /* Note: The handling of HTTP is based on RFC2616 - HTTP/1.1 */

#include "webserver.h"

/* global variable initialization */
char home_dir[MAX_CONF_LINE_SIZE] = "";
char default_type[MAX_TYPE_LENGTH] = DEFAULT_TYPE;
char default_page[2*MAX_CONF_LINE_SIZE] = "";
int max_header_size = MAX_HEADER_SIZE;
int safe_uid = DEFAULT_SAFE_UID;
int safe_gid = DEFAULT_SAFE_GID;


/* reads and responds to the HTTP requests recieved on the socket specified
 * by the conn_fd file descriptor
 * Inputs:
 *  int conn_fd: file descriptor of the connection to be handled
 * Outputs:
 *  Return value: returns OK on success and FAIL otherwise
 */
int request_handler(int conn_fd)
{
    int header_size; //size of the header (including CLRFs)
    int body_size; //size of body (if applicable)
    char * header_buf; //pointer to buffer that contains header.
    char * body_buf; //pointer to buffer that contains body (if applicable)
    char * content_length_loc; //pointer to where the Content-Length field is
    char * end_of_url; //pointer to the ? in the request string
    char request_type[32]; //storage for request type (GET,POST,etc)
    char request_url[MAX_URL_SIZE]; //storage for the request string/url
    char http_version[32]; //storage for the HTTP version fieldof the request line

    /*initialize body_buf to NULL to simplify free() special cases */
    header_buf = NULL;
    body_buf = NULL;
    body_size = 0;


    /****************************/
    /* Read request from socket */
    /****************************/


    /* read headers from socket */
    header_buf = read_headers(conn_fd,&header_size);
    if(header_buf == NULL) {
        debug_printf("request_handler: read_headers failed\n");
        send_error(conn_fd, HTTP_SERVER_ERROR);
        return FAIL;
    }

    /* The existence of a body is signaled by there being a Content-Length
     * or Transfer-Encoding header field */

    if(strstr(header_buf,"Transfer-Encoding") != NULL) {
        /* Transfer-Encoding message bodies are not supported at this time */
        debug_printf("request_handler: received message with Transfer-Encoding body. Ignoring\n");
        send_error(conn_fd, HTTP_NOT_IMPLEMENTED);
    } else {

        /* NOTE: at this time, the body of the header is not used, it is included
         * to allow easier modification and upgrades of this webserver */
        if((content_length_loc = strstr(header_buf,"Content-Length")) != NULL) {
            /* There is a body in this request so read it in */

            /* read the size of the body from the header*/
            content_length_loc += strlen("Content-Length:");

            if(sscanf(content_length_loc,"%6d",&body_size) != 1) {
                /* we expect only 1 pattern to be matched */
                debug_printf("request_handler: sscanf for body size failed\n");
            } else {

                /* NOTE: read_body may change body_size to actual amount read */
                body_buf = read_body(conn_fd,&body_size);
                if(body_buf == NULL) {
                    debug_printf("request_handler: read_body failed\n");
                }

            }
        }
    }


    /**************************************/
    /* generate a response to the request */
    /**************************************/

    /* determine if it is a GET, POST or HEAD request */
    if(sscanf(header_buf,"%32s %512s %32s",request_type,request_url,http_version) != 3) {
        debug_printf("request_handler: sscanf on request line failed\n");
    } else {
        if(strcmp("GET",request_type) == 0) {
            /* received a GET request */

            /* remove everything but the actual URL (everything before the ?)
                do this by replacing the ? with a null character */
            if((end_of_url = strchr(request_url,'?')) != NULL) {
                *end_of_url = '\0';
            }


            /* handle request */
            if(serve_get_request(conn_fd,request_url,header_buf,header_size,body_buf,body_size) == FAIL) {
                debug_printf("request_handler: serve_get_request failed\n");
                /* free the memory allocated by read_header and read_body */
                free(header_buf);
                header_buf = NULL;
                free(body_buf);
                body_buf = NULL;
                return FAIL;
            }

        } else if(strcmp("POST",request_type) == 0) {
            /* received a POST request - currently not implemented */
            debug_printf("request_handler: received POST, not implemented\n");
        } else if(strcmp("HEAD",request_type) == 0) {
            /* received a HEAD request - currently not implemented */
            debug_printf("request_handler: received HEAD, not implemented\n");
        } else {
            /* bad request type */
            debug_printf("request_handler: unknown request type\n");
            send_error(conn_fd,HTTP_BAD_REQUEST);
        }
    }


    /* free the memory allocated by read_header and read_body */
    free(header_buf);
    header_buf = NULL;
    free(body_buf);
    body_buf = NULL;
    return OK;
}

/** parses request, builds and sends response
 * Is responsible for sending error codes to client
 * Inputs:
 *  int conn_fd: file descriptor for socket to client
 *  char * request_url: the URL that the client is asking for
 *  char * header_buf: the header of the request
 *  int header_size: size of the header
 *  char * body_buf: the request body (may be NULL)
 *  int body_size: size of body (0 if no body)
 * Outputs:
 *  Returns OK on success, FAIL otherwise
 */
int serve_get_request(int conn_fd, char * request_url, char * header_buf, int header_size, char * body_buf, int body_size)
{
    int access_result; //return value of check_access
    int response_headers_size; //size of the headers being sent to client
    int response_body_size; //size of body being sent to client (if applicable)
    char * response_headers; //headers being sent to client
    char * response_body; //body being sent to client (if applicable)
    char * filename; //local filename for URL
    char this_type[MAX_TYPE_LENGTH]; //the type to be used for this response

    /*initialize response_body to NULL to simplify free() special cases */
    response_headers = NULL;
    response_body = NULL;
    response_body_size = 0;

    /* get the local filename referred to by the URL */
    filename = get_filename(request_url);

    /*check filename to make sure it is a file we are allowed to serve */
    access_result = check_access(filename);
    if(access_result == OK) {

        /* create body for response */
        if((response_body = generate_body(filename,&response_body_size)) == NULL) {
            send_error(conn_fd,HTTP_SERVER_ERROR);
            /* response_headers was malloc'd so we have to free it before returning */
            free(response_headers);
            response_headers = NULL;
            free(filename);
            filename = NULL;
            debug_printf("serve_get_request: generate_body failed\n");
            return FAIL;
        }

        /* If we aren't sending the diagnostic page, use the default type
            otherwise use text/html */
        if(strcmp(filename,"/") == 0) {
            strcpy(this_type,"text/html");
        } else {
            strncpy(this_type,default_type,MAX_TYPE_LENGTH);
        }

        /* create headers for response */
        if((response_headers = generate_headers(HTTP_OK,this_type,response_body_size,&response_headers_size)) == NULL) {
            send_error(conn_fd,HTTP_SERVER_ERROR);
            debug_printf("serve_get_request: generate_headers failed\n");
            free(filename);
            filename=NULL;
            return FAIL;
        }

        /* send the headers */
        if(send(conn_fd,response_headers,response_headers_size,0) < 0) {
            debug_printf("serve_get_request: send headers failed\n");
            free(response_headers);
            response_headers = NULL;
            free(response_body);
            response_body = NULL;
            free(filename);
            filename = NULL;
            return FAIL;
        }

        /* send the body */
        if(send(conn_fd,response_body,response_body_size,0) < 0) {
            debug_printf("serve_get_request: send body failed\n");
            free(response_headers);
            response_headers = NULL;
            free(response_body);
            response_body = NULL;
            free(filename);
            filename = NULL;
            return FAIL;
        }

        /* terminate the connection with the client to indicate done sending */
        debug_printf("serve_get_request: closed connection\n");
        close(conn_fd);


        /* cleanup allocated memory */
        free(response_headers);
        response_headers = NULL;
        free(response_body);
        response_body = NULL;

    } else {
        /* the file was not found, it is not allowed to be served, or server error */
        send_error(conn_fd,access_result);
        if(access_result == HTTP_SERVER_ERROR) {
            /* this was an internal error and we should indicate this to the caller */
            debug_printf("serve_get_request: check_access failed\n");
            return FAIL;
        }
    }

    /* free filename memory */
    free(filename);
    response_body = NULL;

    return OK;
}

/* checks the request_url validity and access permissions based on the data
 *  read from the configuration file
 * Inputs:
 *  char * filename: filename to check
 * Outputs:
 *  returns OK on succes,
 *      HTTP_NOT_FOUND if the requested file is for a location within an allowed
 *           directory but does not exist.
 *      HTTP_ACCESS_DENIED if the requested url is in a directory where access
 *           is not allowed, even if the file does not exist.
 *      HTTP_BAD_REQUEST if the request is malformed
 *      HTTP_SERVER_ERROR otherwise
 */
int check_access(char * filename)
{
    FILE * f; //file structure to test if file exists
    char c; //for test read

    /* filename of / will not be literally opened so we don't have to check it */
    if(strcmp(filename,"/") == 0) {
        return OK;
    }

    /* all URLs should start with '/' */
    if(filename[0] != '/') {
        return HTTP_BAD_REQUEST;
    }

    /* make sure that the filename is within the home_dir directory by
     stripping out "..", "~", etc*/
    if(strstr(filename,"/../") != NULL) {
        return HTTP_ACCESS_DENIED;
    }


    /* test to see if file exists and we can open it and it is readable*/
    if((f = fopen(filename,"r")) == NULL) {
        return HTTP_NOT_FOUND;
    } else {
        if(fread(&c,1,1,f) != 1) {
            fclose(f);
            return HTTP_NOT_FOUND;
        }
        fclose(f);
    }

    return OK;
}

/* sends the given error code to the client connected to conn_fd
 * Inputs:
 *  int conn_fd: file descriptor of socket to client
 *  int code: HTTP status code of error to send
 * Outputs:
 *  None
 */
void send_error(int conn_fd, int code)
{
    char * buffer; //buffer to be filled with data to send
    char code_str[16]; //string version of the error code
    char body[256] = "";
    char type[] = "Content-Type: text/html; charset=ISO-8859-4";
    int header_len;

    /* buffer starts out unallocated */
    buffer = NULL;

    /* convert the status code to string */
    sprintf(code_str,"%d ",code);

    /* generate a simple error message page as the body of the response */
    strcat(body,"<!DOCTYPE html><html><body><h2>Error ");
    strcat(body,code_str);
    strcat(body," - ");
    switch(code) {
        case HTTP_OK: strcat(body,HTTP_OK_R);
            break;
        case HTTP_NOT_FOUND: strcat(body,HTTP_NOT_FOUND_R);
            break;
        case HTTP_ACCESS_DENIED: strcat(body,HTTP_ACCESS_DENIED_R);
            break;
        case HTTP_NOT_IMPLEMENTED: strcat(body,HTTP_NOT_IMPLEMENTED_R);
            break;
        case HTTP_SERVER_ERROR: strcat(body,HTTP_SERVER_ERROR_R);
            break;
        case HTTP_BAD_REQUEST: strcat(body,HTTP_BAD_REQUEST_R);
            break;
        default:
            strcat(body,"Unknown");
    };
    strcat(body,"</h2></body></html>");

    /* add the length of the body to the headers */
    buffer = generate_headers(code,type, strlen(body),&header_len);
    if(buffer == NULL) {
        debug_printf("send_error: generate_headers failed\n");
        return;
    }

    /* send the message to client */
    send(conn_fd, buffer, header_len,0);
    send(conn_fd, body, strlen(body),0);

    /* close socket to indicate message complete */
    debug_printf("send_error: closed connection\n");
    close(conn_fd);

    /* cleanup the memory allocated by generate_header */
    free(buffer);
    buffer = NULL;

}

/* fills buffer in with time string
 * Inputs:
 *  char * date: string to be filled with date
 * Outputs:
 *  Returns OK on success, FAIL otherwise
 */
int get_date(char * date)
{
    time_t curr_time; //timer for date generation
    struct tm * gmt; //struct to store GMT time data

    /* generate and add the date */
    /* get current time */
    time(&curr_time);
    /* convert time to GMT */
    gmt = gmtime(&curr_time);
    /* Format the time into HTTP format */
    strftime(date,MAX_DATE_SIZE,"Date: %a, %d %b %Y %H:%M:%S %Z\r\n",gmt);

    return OK;
}


/* builds a header section with the given response code and type
 * Inputs:
 *  int code: HTTP status code for the response line
 *  char * type: Content-Type to use in message (NULL will cause the
 *      Content-Type header to be omitted)
 *  int body_length: Length of the body to put in header (a negative value
 *      will cause the Content-Length header to be omitted)
 * Outputs:
 *  int * size: Size of buffer allocated and returned
 *  Returns buffer with header or NULL on error
 */
char * generate_headers(int code ,char * type, int body_length, int * size) {
    char * buffer; //buffer to be filled with data to send
    char code_str[16]; //string version of the error code
    char date[MAX_DATE_SIZE]; //string to store formatted date in
    char temp[64];

    /* allocate memory for the header */
    buffer = malloc(max_header_size+1);
    if(buffer == NULL) {
        debug_printf("generate_headers: malloc failed\n");
        return NULL;
    }

    /* start buffer off empty so the first strcat will work correctly */
    buffer[0] = '\0';

    /* convert the status code to string */
    sprintf(code_str,"%d ",code);

    /* add the HTTP version to the response line */
    strcat(buffer,"HTTP/1.1 ");

    /* add the status code */
    strcat(buffer,code_str);

    /* based off the status code, add the correct reason */
    switch(code) {
        case HTTP_OK: strcat(buffer,HTTP_OK_R);
            break;
        case HTTP_NOT_FOUND: strcat(buffer,HTTP_NOT_FOUND_R);
            break;
        case HTTP_ACCESS_DENIED: strcat(buffer,HTTP_ACCESS_DENIED_R);
            break;
        case HTTP_NOT_IMPLEMENTED: strcat(buffer,HTTP_NOT_IMPLEMENTED_R);
            break;
        case HTTP_SERVER_ERROR: strcat(buffer,HTTP_SERVER_ERROR_R);
            break;
        case HTTP_BAD_REQUEST: strcat(buffer,HTTP_BAD_REQUEST_R);
            break;
        default:
            strcat(buffer,"Unknown");
    }

    /* finish off the response line with a CRLF*/
    strcat(buffer,"\r\n");

    /* generate and add the date */
    if(get_date(date) == FAIL) {
        debug_printf("generate_headers: get_date failed\n");
    }

    /* add the date to the header */
    strcat(buffer, date);

    /* add the length of the body to the headers */
    if(body_length >= 0) {
        sprintf(temp, "Content-Length: %d\r\n", body_length);
        strcat(buffer,temp);
    }

    /* add content type header */
    if(strlen(type) > 0) {
        strcat(buffer, "Content-Type: ");
        strcat(buffer, type);
        strcat(buffer, "\r\n");
    }

    /* tell the client that the server is closing the connection after
     * sending this message */
    strcat(buffer,"Connection: close\r\n");

    /* end the header with final CRLF */
    strcat(buffer,"\r\n");

    *size = strlen(buffer);
    return buffer;
}


/* takes a URL and finds the local filename for it
 * Inputs:
 *  char * url: URL from the HTTP request
 * Outputs:
 *  Returns pointer to filename string. Caller should free it
 */
char * get_filename(char * url) {
    char * filename; //holds the path+filename of the file on disk
    /* allocate enough memory to hold both the home path and the url path */
    /* +2 is to give room for null termination */
    /* MAX_CONF_LINE_SIZE in case the default path is needed */
    filename = malloc(MAX_CONF_LINE_SIZE + strlen(home_dir) + 2);
    if(filename == NULL) {
        perror("get_filename: malloc failed");
        return NULL;
    }

    /* start filename out empty */
    filename[0] = '\0';

    /* if the url is "/" we need to find another file to serve */
    if(strcmp(url,"/") == 0) {
        /* if a default page is set serve that */
        if(strlen(default_page) > 0) {
            strncpy(filename,default_page,MAX_CONF_LINE_SIZE);
        } else {
            strcpy(filename,"/");
        }
    } else {
        /* put the home dir read from the conf file as the start of the path */
        /* Note: home_dir should not end with '/'. This is handled in read_conf */
        strcat(filename,home_dir);

        /* add on the relative URL path */
        strcat(filename,url);
    }

    return filename;
}


/* builds the body as specified by the request URL
 * Assumes that access to & existence of the file has been checked
 * Caller repsonsible for freeing returned buffer
 * Inputs:
 *  char * filename: absolute address of file to be served
 * Outputs:
 *  int * size: size of body buffer that is returned (Limit: 2GB)
 *  Returns pointer to buffer containing body or NULL on error
 */
char * generate_body(char * filename, int * size)
{
    FILE * f; //FILE object for file manipulations
    char * read_buf; //holds contents of the file
    off_t filesize; //size of the buffer being returned
    size_t ret; //stores return value to be checked later
    char * date; //temporarily stores the date string, if necessary

    /* the filename will be "/" when there is no default page and the URL is "/" */
    if(strcmp(filename,"/") == 0) {
        /* serve a very simple diagnostic page */
        if((date = malloc(MAX_DATE_SIZE)) == NULL) {
            debug_printf("generate_body: malloc failed\n");
            return NULL;
        }

        /* allocate memory for the page data */
        if((read_buf = malloc(MAX_DATE_SIZE*2)) == NULL) {
            debug_printf("generate_body: malloc failed\n");
            free(date);
            date=NULL;
            return NULL;
        }

        /* add beginning of page */
        read_buf[0] = '\0';
        strcat(read_buf,"<html><body>");

        /* generate and add the date */
        if(get_date(date) == FAIL) {
            debug_printf("generate_headers: get_date failed\n");
        }
        strncat(read_buf,date,MAX_DATE_SIZE);

        /* add end of page */
        strcat(read_buf,"</body></html>");


        /* cleanup allocated memory */
        free(date);
        date = NULL;

        *size = strlen(read_buf);
        return read_buf;
    }

    /* open the file for reading */
    f = fopen(filename,"r");
    if(f == NULL) {
        debug_printf("generate_body: failed to open %s\n",filename);
        perror("generate_body: fopen failed");
        return NULL;
    }

    /* get size of file */
    if((filesize = fsize(filename)) == FAIL) {
        debug_printf("generate_body: failed to get filesize\n");
        fclose(f);
        return NULL;
    }

    /* allocate a buffer to hold the file */
    read_buf = malloc(filesize+1);
    if(read_buf == NULL) {
        perror("generate_body: malloc failed");
        fclose(f);
        return NULL;
    }

    /* read the file into the buffer */
    if((ret = fread(read_buf,1,filesize,f)) != filesize) {
        /* if the return value is less than filesize, there could be an error */
        if(ferror(f)) {
            debug_printf("generate_body: fread failed\n");
            free(read_buf);
            read_buf = NULL;
            return NULL;
        } else {
            /* if no error, adjust the file size and continue */
            debug_printf("WARNING generate_body: read less than expected\n");
            filesize = (off_t) ret;
        }
    }

    /* we are done with the file, so close it */
    fclose(f);

    /* output */
    *size = (int) filesize;
    return read_buf;
}

/* gets the size of a file
 * Inputs:
 *  char * filename: filename of file to return size of
 * Outputs:
 *  Returns size of file
 */
off_t fsize(char * filename)
{
    struct stat st; //struct that stat will file with file info

    /* fill the structure for the file at filename */
    if(stat(filename,&st) != 0) {
        debug_printf("fsize: stat failed %s\n",strerror(errno));
        return FAIL;
    }
    return st.st_size;
}

/* reads just the header of the request. mallocs the memory needed for it
 * Caller is responsible for freeing the memory allocated here
 * Inputs:
 *  int conn_fd: file descriptor of the socket to read from
 *  int * read_chars: pointer to an int to store the number of read characters
 * Outputs:
 *  Number of read characters output via dereferencing read_chars.
 *  Returns pointer to header data, NULL on failure.
 *      Note: caller is responsible for freeing this pointer
 */
char * read_headers(int conn_fd, int * read_chars)
{
    int read_len; //holds the amount read be each recv call
    int total_len; //total number of chars read into the buffer
    char * read_buf; //stores the read chars
    char ended; //set true when CRLF is encountered

    /* initialize read_buf to be NULL */
    read_buf = NULL;

    /* allocate a buffer of size MAX_SOCKET_READ for reading */
    if((read_buf = malloc(sizeof(char)*max_header_size+1)) == NULL) {
        fprintf(stderr, "read_header: malloc failed\n");
        return NULL;
    }

    total_len = 0;
    ended = 0;

    /* read the headers char-by-char checking for the two CRLFs that signal
     * the end of the headers */
    while((read_len = recv(conn_fd,&read_buf[total_len],1,0)) == 1) {
        total_len += read_len;


        /* if total_len is not yet 4, no point in checking for a 4 character sequence */
        if(total_len >= 4) {
            /* check for the last char being CR and this char LF.
             * Note: total_len-1 is the most recent character because we have
             * incremented total_len in the previous line */
            if(read_buf[total_len-2] == '\r' && read_buf[total_len-1] == '\n') {
                /* check for the 3rd and 4th to last characters being a CRLF */
                if(read_buf[total_len-4] == '\r' && read_buf[total_len-3] == '\n') {
                    /* this means that we have read "\r\n\r\n" which signals the
                     * end of the headers section */
                    ended = 1;
                    break;
                }
            }
        }

        /* ensure that it does not overflow the buffer */
        if(total_len >= max_header_size) {
            break;
        }

    }

#ifdef DEBUG
    /* The above code assumes that read_len will always be either 1 or 0,
     * this just ensures that is the case during DEBUG mode testing */
    if(read_len > 1) {
        debug_printf("read_header: unexpected return value from recv: %d\n",read_len);
        return NULL;
    }
#endif

    /*check for recv errors */
    if(read_len == -1) {
        perror("read_header: recv");
        return NULL;
    }

    /* check to make sure something was read */
    if(total_len <= 0) {
        debug_printf("read_header: read nothing\n");
        return NULL;
    }

#ifdef DEBUG
    if(ended != 1) {
        debug_printf("read_header: did not find CRLF in header\n");
        return NULL;
    }
#endif

    *read_chars = total_len;
    return read_buf;
}


/* reads the body of a message
 * Caller responsible for freeing the buffer that is returned
 * Inputs:
 *  int conn_fd: file descriptor for socket connected to client
 *  int * body_size: size of the body to be read (also an output)
 * Outputs:
 *  Stores the number of chars read through body_size (also an input)
 *  Returns a pointer to the buffer containing the body, NULL on error
 */
char * read_body(int conn_fd, int * body_size)
{
    char * read_buf; //pointer to location storing the body of the message
    int read_len;

    /* allocate memory for the body */
    errno = 0; //not all malloc implementations set errno
    if((read_buf = malloc(sizeof(char)*(*body_size)+1)) == NULL) {
        perror("read_body: malloc error");
        return NULL;
    }

    /* read up to *body_size bytes from the socket */
    if((read_len = recv(conn_fd,read_buf,*body_size,0)) <= 0) {
        perror("read_body: recv failed");
        return NULL;
    }

#ifdef DEBUG
    if(read_len < *body_size) {
        debug_printf("read_body: read less than content-length\n");
    }
#endif

    /* store the number of bytes read into the buffer */
    *body_size = read_len;

    return read_buf;
}

/* reads in the configuration file
 * Inputs:
 *  (reads from file)
 * Outputs:
 *  (changes global variables)
 *  Returns OK on success and FAIL otherwise
 */
int read_conf()
{
    FILE * f; //file structure for conf file
    FILE * g; //file structure for testing default page file
    char templine[MAX_CONF_LINE_SIZE]; //holds each line as it is being parsed
    int line_len; //size of the current line
    char option[MAX_CONF_LINE_SIZE]; //temporarily holds the option parsed out of the line
    char home_done = 0; //flag to make sure home directory gets set
    char * argstart; //pointer to the start of the argument

    /* open the configuration file */
    if((f = fopen(CONF_FILE,"r")) == NULL) {
        debug_printf("read_conf: could not open conf file\n");
        return FAIL;
    }


    /* parse each line in the file */
    while((line_len = get_line(f,templine)) > 0) {
        /* if this line is a comment, ignore it */
        if(templine[0] == CONF_COMMENT) {
            continue;
        }

        /*determine which option this line specifies */
        if(sscanf(templine,"%s",option) != 1) {
            debug_printf("read_conf: failed to parse conf line %s\n",templine);
            fclose(f);
            return FAIL;
        }

        /* copy the argument to the appropriate location, starting at the
           offset just past the "<option>: " bit*/
        if(strcmp(CONF_HOME,option) == 0) {
            strncpy(home_dir,
                    &templine[strlen(CONF_HOME)+strlen(CONF_POST_OPTION)]
                    ,MAX_CONF_LINE_SIZE);

            /* make sure the home directory does not end with '/' */
            if(home_dir[strlen(home_dir)-1] == '/') {
                /* cut off the '/' */
                home_dir[strlen(home_dir)-1] = '\0';
            }
            home_done = 1;

        } else if(strcmp(CONF_MAX_READ,option) == 0) {
            sscanf(&templine[strlen(CONF_MAX_READ)],CONF_POST_OPTION "%d", &max_header_size);

        } else if(strcmp(CONF_DEFAULT_TYPE,option) == 0) {
            strncpy(default_type,
                    &templine[strlen(CONF_DEFAULT_TYPE)+strlen(CONF_POST_OPTION)]
                    ,MAX_CONF_LINE_SIZE);
        } else if(strcmp(CONF_DEFAULT_PAGE,option) == 0) {
            /*check to see if the given path is absolute or relative */
            argstart = &templine[strlen(CONF_DEFAULT_PAGE)+strlen(CONF_POST_OPTION)];
            if(argstart[0] == '/') {
                /* it is an absolute address, so leave it be */
                strncpy(default_page,argstart,MAX_CONF_LINE_SIZE);
            } else {
                /* it is a relative path, so add on the home directory to it */
                strncat(default_page,home_dir,MAX_CONF_LINE_SIZE);
                strcat(default_page,"/");
                strncat(default_page,argstart,MAX_CONF_LINE_SIZE);
            }

            /* ensure that the default page file really exists */
            if((g = fopen(default_page,"r")) == NULL) {
                fprintf(stderr, "Configuration: Unable to open default page %s\n",default_page);
                fclose(f);
                return FAIL;
            } else {
                fclose(g);
            }

        } else if(strcmp(CONF_UID, option) == 0) {
            argstart = &templine[strlen(CONF_UID)+strlen(CONF_POST_OPTION)];
            if(sscanf(argstart,"%d",&safe_uid) != 1) {
                fprintf(stderr, "Configuration: Unable to parse CONF_UID argument: %s\n",argstart);
                fclose(f);
                return FAIL;
            }

        } else if(strcmp(CONF_GID, option) == 0) {
            argstart = &templine[strlen(CONF_UID)+strlen(CONF_POST_OPTION)];
            if(sscanf(argstart,"%s",&safe_gid) != 1) {
                fprintf(stderr, "Configuration: Unable to parse CONF_GID argument: %s\n",argstart);
                fclose(f);
                return FAIL;
            }
        } else {
            fprintf(stderr, "Configuration: Unknown configuration option: %s\n", option);
            fclose(f);
            return FAIL;
        }
    }

    /* close the conf file */
    fclose(f);

    /* check for get_line returning FAIL */
    if(line_len == FAIL) {
        debug_printf("read_conf: get_line failed\n");
        return FAIL;
    }

    /* make sure the home directory was set */
    if(home_done == 0) {
        fprintf(stderr,"No home directory set in the configuration file\n");
        return FAIL;
    }

    return OK;
}

/* reads an entire line in a file
 * Inputs:
 *  FILE * f: file stream to read from
 *  char * output: preallocated buffer to store characters in
 * Outputs:
 *  Returns number of characters stored (up to MAX_CONF_LINE_SIZE)
 *      or FAIL on failure
 */
int get_line(FILE * f, char * output) {
    char c; //stores char read from file
    int counter = 0;

    /* while there are still characters to be read in the file */
    while((c = getc(f)) != EOF) {
        /* this is a getline function, so only read up to a newline */
        if(c == '\n') {
            break;
        }
        /* only store up to max_conf_line_size -1 characters in the buffer */
        /* -1 because we need room for null termination */
        if(counter < MAX_CONF_LINE_SIZE - 1) {
            output[counter] = c;
            counter++;
        } else {
            /* read (but ignore) the rest of the line so future reads start at
               beginning of next line */
            ;
        }
    }
    /* check to see if EOF was because of an error */
    if(ferror(f)) {
        debug_printf("get_line: getc failed\n");
        return -1;
    }

    /* null terminate string */
    output[counter] = '\0';

    /* return length of line (up to max)*/
    return counter;
}

/* This will be registered as a signal handler for SIGCHLD
 * It simply calls wait to reclaim the exited child's resources
 * Inputs:
 *  int sig: provided by system
 * Outputs:
 *  None
 */
void wait_children(int sig) {
    int status;
    wait(&status);
}



int main(int argc, char ** argv)
{
    int listen_fd; //file descriptor for listening TCP socket
    int connection_fd; //file descriptor for newly accepted connections

    int ret; //used to temporarily store various return values

    struct addrinfo addr_specs; //used to specify parameters for this connections
    struct addrinfo *server_info_list; //head of the results list from getaddrinfo
    struct addrinfo *curr_info; //current result in above list
    struct sockaddr_storage client_addr; //information about connected client
    socklen_t client_addr_size; //stores size of above structure

    struct sigaction child_action; //holds info needed to register SIGCHLD handler


    const int int_one = 1; //used to get a pointer to int 1

    /******************************/
    /* read in configuration file */
    /******************************/

    if(argc == 2) {
        if(strcmp("--help",argv[2]) == 0) {
            printf(HELP_STRING,argv[1],CONF_FILE);
        }
    }

    if(read_conf() == FAIL) {
        fprintf(stderr, "Error while reading configuration file.\n");
        /* fill in the configuration file help string */
        fprintf(stderr, CONF_HELP_STRING, CONF_FILE, MAX_CONF_LINE_SIZE,
                CONF_COMMENT,
                CONF_HOME, CONF_POST_OPTION,
                CONF_MAX_READ, CONF_POST_OPTION, MAX_HEADER_SIZE,
                CONF_DEFAULT_TYPE, CONF_POST_OPTION, DEFAULT_TYPE,
                CONF_DEFAULT_PAGE, CONF_POST_OPTION);

        return -1;
    }


    /*****************************************/
    /* set up the a TCP socket for listening */
    /*****************************************/

    /*convert our local address and port binding for later use */
    memset(&addr_specs, 0,sizeof(addr_specs));


    /* fill out desired specs for this server listening connection */
    addr_specs.ai_family = AF_UNSPEC; //use IPv4 or IPv6 (IPv6 not tested)
    addr_specs.ai_socktype = SOCK_STREAM; //use TCP
    addr_specs.ai_flags = AI_PASSIVE; //accept connections on any interface

    if((ret = getaddrinfo(NULL, HTTP_PORT, &addr_specs, &server_info_list)) != 0) {
        //Error - terminate
        fprintf(stderr, "main: getaddrinfo failed: %s. exiting\n", gai_strerror(ret));
        exit(1);
    }

    /* loop through possible addresses returned by getaddrinfo and bind to
     * first available address */
    for(curr_info = server_info_list; curr_info != NULL; curr_info = curr_info->ai_next) {
        /* create a socket with this address */
        if((listen_fd = socket(curr_info->ai_family, curr_info->ai_socktype, curr_info->ai_protocol)) == -1) {
            perror("main: socket()");
            continue; //try on the next address, if possible
        }

        /* this allows the socket to be port to be reused if the server crashes */
        if(setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &int_one, sizeof(int)) == -1) {
            perror("main: setsockopt()");
            exit(1);
        }

        /* try to bind this socket */
        if(bind(listen_fd,curr_info->ai_addr,curr_info->ai_addrlen) == -1) {
            /* bindinf failed for some reason. close the socket and try on the
             * address, if one exists */
            close(listen_fd);
            perror("main: bind()");
            continue;
        }

        /* if everything succeeded up to here, we are ready to go */
        break;
    }

    /* done with the list of addresses, so free it */
    freeaddrinfo(server_info_list);

    /* if no suitable address was found or other errors */
    if(curr_info == NULL) {
        fprintf(stderr, "main: was unable to create/bind socket. exiting\n");
        exit(1);
    }


    /* drop root priviledge */
    setuid(safe_uid);
    setgid(safe_gid);

    debug_printf("DEBUG: userid: %d\n",safe_uid);
    debug_printf("DEBUG: groupid: %d\n",safe_gid);

    /* register the SIGCHLD handler */
    child_action.sa_handler = wait_children;
    sigemptyset(&child_action.sa_mask);
    child_action.sa_flags = 0;
    sigaction(SIGCHLD,&child_action,NULL);

    /*******************************************/
    /* listen to socket and accept connections */
    /*******************************************/

    /* start listening for connections from clients (does not block)
     * Enqueue up to LISTEN_BACKLOG connections waiting to be accepted */
    if(listen(listen_fd, LISTEN_BACKLOG) == -1) {
        perror("main: listen()");
        exit(1);
    }

    debug_printf("main: server is up and listening\n");

    /* we need the address of client_addr's size */
    client_addr_size = sizeof(client_addr);

    /* this loop accepts incoming connections and then forks a process
     * to handle each connection */
    while(1) {


        /*accept connection, stores info in client_addr */
        if((connection_fd = accept(listen_fd, (struct sockaddr *) &client_addr, &client_addr_size)) == -1) {
            perror("main: accept()");
            continue; //we do not want to exit because one bad connection would kill the server
        }

        debug_printf("main: accepted connection\n");

        /* fork the process to handle the new connections requests */
        if((ret = fork()) == 0) {
            /* this is executed on the child process only */
            //close(listen_fd); //the child process has no need for this socket

            /* start handling requests */
            if(request_handler(connection_fd) == FAIL) {
                debug_printf("main (child): request_handler failed\n");
                exit(1);
            }

            /* we are done with this client, exit this forked process */
            debug_printf("main (child): _exit 0\n");
            _exit(0); //_exit is needed to exit from fork

        } else {
            /* this is executed on the parent process only */

            close(connection_fd); //the parent process no longer needs this socket

            if(ret == -1) {
                perror("main: fork()");
            }

            //continue with the accept loop
        }

    } //end while(1)

    close(listen_fd);

    return 0;
}
