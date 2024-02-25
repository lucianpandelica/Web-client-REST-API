#ifndef _REQUESTS_
#define _REQUESTS_

#define GET_TYPE 1
#define DELETE_TYPE 2

// computes and returns a GET/DELETE request string (query_params
// and cookies can be set to NULL if not needed)
char *compute_request(uint8_t type, char *host, char *url, char* auth, char *query_params,
                            char **cookies, int cookies_count);

// computes and returns a POST request string (cookies can be NULL if not needed)
char *compute_post_request(char *host, char *url, char* auth, char* content_type, char* content,
                            int content_len, char **cookies, int cookies_count);

#endif
