#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <stdio.h>
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"

char *compute_request(uint8_t type, char *host, char *url, char* auth, char *query_params,
                            char **cookies, int cookies_count)
{   
    int i;
    char *message = calloc(BUFLEN, sizeof(char));
    char *line = calloc(LINELEN, sizeof(char));

    if (type == GET_TYPE) {
        // Step 1: write the method name, URL, request params (if any) and protocol type
        if (query_params != NULL) {
            sprintf(line, "GET %s?%s HTTP/1.1", url, query_params);
        } else {
            sprintf(line, "GET %s HTTP/1.1", url);
        }
    } else if (type == DELETE_TYPE) {
        sprintf(line, "DELETE %s HTTP/1.1", url);
    } else {
        /* invalid type */
        return NULL;
    }
    
    compute_message(message, line);

    // Step 2: add the host
    memset(line, 0, LINELEN);
    sprintf(line, "Host: %s", host);
    compute_message(message, line);

    if (auth != NULL) {
        memset(line, 0, LINELEN);
        sprintf(line, "Authorization: Bearer %s", auth);
        compute_message(message, line);
    }

    // Step 3 (optional): add headers and/or cookies, according to the protocol format
    if (cookies != NULL) {
        memset(line, 0, LINELEN);

        strcat(line, "Cookie: ");
        for (i = 0; i < cookies_count; i++) {
            strcat(line, cookies[i]);

            if(i != (cookies_count - 1)) {
                strcat(line, "; ");
            }
        }

        compute_message(message, line);
    }

    // Step 4: add final new line
    compute_message(message, "");
    free(line);
    return message;
}

/* added auth parameter */
char *compute_post_request(char *host, char *url, char* auth, char* content_type, char* content,
                            int content_len, char **cookies, int cookies_count)
{
    int i;
    char *message = calloc(BUFLEN, sizeof(char));
    char *line = calloc(LINELEN, sizeof(char));

    // Step 1: write the method name, URL and protocol type
    sprintf(line, "POST %s HTTP/1.1", url);
    compute_message(message, line);
    
    // Step 2: add the host
    memset(line, 0, LINELEN);
    sprintf(line, "Host: %s", host);
    compute_message(message, line);

    /* Step 3: add necessary headers (Content-Type and Content-Length are mandatory)
            in order to write Content-Length you must first compute the message size
    */
    if (auth != NULL) {
        memset(line, 0, LINELEN);
        sprintf(line, "Authorization: Bearer %s", auth);
        compute_message(message, line);
    }

    memset(line, 0, LINELEN);
    sprintf(line, "Content-Type: %s", content_type);
    compute_message(message, line);

    memset(line, 0, LINELEN);
    sprintf(line, "Content-Length: %d", content_len);
    compute_message(message, line);

    // Step 4 (optional): add cookies
    if (cookies != NULL) {
        memset(line, 0, LINELEN);

        strcat(line, "Cookie: ");
        for (i = 0; i < cookies_count; i++) {
            strcat(line, cookies[i]);

            if(i != (cookies_count - 1)) {
                strcat(line, "; ");
            }
        }

        compute_message(message, line);
    }

    // Step 5: add new line at end of header
    compute_message(message, "");

    // Step 6: add the actual payload data
    memset(line, 0, LINELEN);
    strcat(message, content);

    free(line);
    return message;
}
