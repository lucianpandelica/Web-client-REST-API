#include <stdio.h>      /* printf, sprintf */
#include <stdlib.h>     /* exit, atoi, malloc, free */
#include <unistd.h>     /* read, write, close */
#include <string.h>     /* memcpy, memset */
#include <sys/socket.h> /* socket, connect */
#include <netinet/in.h> /* struct sockaddr_in, struct sockaddr */
#include <netdb.h>      /* struct hostent, gethostbyname */
#include <arpa/inet.h>
#include "helpers.h"
#include "requests.h"

#include <ctype.h>
#include <sys/epoll.h>
#include "w_epoll.h"
#include "util.h"
#include "parson/parson.h"

#define MAXBUFSIZE 4096
#define TOKEN_SIZE 1024
#define COOKIE_SIZE 1024

#define HTTPVERS "HTTP/1.1"
#define SET_COOKIE "\r\nSet-Cookie: "

/* starea conexiunii clientului */
enum connection_state {
	STATE_CONNECTED,
    STATE_DISCONNECTED
};

typedef struct client {
    int sockfd;
    char* jwt_token; // token acces biblioteca
    char** cookies; // cookie aferent sesiunii
    int cookies_count;
    enum connection_state state;
    uint8_t has_token;
} Client;

Client *my_client;
static int epollfd;

void init_client() {
    my_client = (Client*) malloc(sizeof(Client));
    DIE(my_client == NULL, "malloc");

    my_client->sockfd = -1;

    my_client->jwt_token = (char*) malloc(TOKEN_SIZE * sizeof(char));
    memset(my_client->jwt_token, 0, TOKEN_SIZE);
    
    /* vom aloca spatiu pentru cookies pe masura ce le primim */
    my_client->cookies = NULL;
    my_client->cookies_count = 0;

    my_client->state = STATE_DISCONNECTED;
    my_client->has_token = 0;
}

void connect_to_server() {
    int res;

    my_client->sockfd = open_connection("34.254.242.81",
                                        8080,
                                        AF_INET,
                                        SOCK_STREAM,
                                        0);
    DIE(my_client->sockfd < 0, "socket");

    my_client->state = STATE_CONNECTED;

    /*
     * adaugam noul socket la epoll pentru a
     * sti cand se inchide din nou conexiunea
     */
    res = w_epoll_add_fd_in(epollfd, my_client->sockfd);
    DIE(res < 0, "w_epoll_add_fd_in");
}

void maintain_conn() {
    /* reconectare la server */
    if (my_client->state == STATE_DISCONNECTED) {
        connect_to_server();
    }
}

void handle_conn_closed() {
    int res;

    /* stergem file descriptor-ul din epoll */
    res = w_epoll_remove_fd(epollfd, my_client->sockfd);
    DIE(res < 0, "w_epoll_remove_fd");

    /* inchidem socket-ul */
    close_connection(my_client->sockfd);

    /* setam clientul drept deconectat de la server (indiferent de cont) */
    my_client->sockfd = -1;
    my_client->state = STATE_DISCONNECTED;
}

int get_user_and_pass(char** user, char** pass) {
    size_t user_len = 0;
    size_t pass_len = 0;
    ssize_t total_size = 0;
    char* space_check = NULL;
    char space = ' ';
    int res = 0;

    /*
     * functia citeste username-ul si parola date de utilizator si
     * aloca buffere pentru retinerea acestora
     */
    printf("username=");
    total_size = getline(user, &user_len, stdin);
    DIE(total_size < 0, "getline");

    (*user)[total_size - 1] = '\0';

    /* verificam daca input-ul contine spatii (adica input invalid) */
    space_check = strstr((*user), &space);
    if (space_check != NULL) {
        res = -1;
    }

    printf("password=");
    total_size = getline(pass, &pass_len, stdin);
    DIE(total_size < 0, "getline");

    (*pass)[total_size - 1] = '\0';

    space_check = strstr((*pass), &space);
    if (space_check != NULL) {
        res = -1;
    }

    return res;
}

int get_book_info(char** title,
                  char** author,
                  char** genre,
                  char** publisher,
                  int* page_count) {
    int i;
    size_t title_len = 0, auth_len = 0, genre_len = 0, pub_len = 0;
    ssize_t total_size = 0;

    /* titlu */
    printf("title=");
    total_size = getline(title, &title_len, stdin);
    DIE(total_size < 0, "getline");

    (*title)[total_size - 1] = '\0';

    /* autor */
    printf("author=");
    total_size = getline(author, &auth_len, stdin);
    DIE(total_size < 0, "getline");

    (*author)[total_size - 1] = '\0';

    /* gen */
    printf("genre=");
    total_size = getline(genre, &genre_len, stdin);
    DIE(total_size < 0, "getline");

    (*genre)[total_size - 1] = '\0';

    /* editura */
    printf("publisher=");
    total_size = getline(publisher, &pub_len, stdin);
    DIE(total_size < 0, "getline");

    (*publisher)[total_size - 1] = '\0';

    /* numar de pagini */
    char* page_count_str = NULL;
    size_t page_count_len = 0;

    printf("page_count=");
    total_size = getline(&page_count_str, &page_count_len, stdin);
    DIE(total_size < 0, "getline");

    page_count_str[total_size - 1] = '\0';

    /* verificam validitatea input-ului pentru numarul de pagini */
    for (i = 0; i < strlen(page_count_str); i++) {
        if (isalpha(page_count_str[i])) {
            free(page_count_str);
            return -1;
        }
    }

    /* avem un numar valid, il retinem */
    (*page_count) = atoi(page_count_str);
    free(page_count_str);
    return 0;
}

int get_book_id(char** book_id) {
    int i;
    size_t id_len = 0;
    ssize_t total_size = 0;

    printf("id=");
    total_size = getline(book_id, &id_len, stdin);
    DIE(total_size < 0, "getline");

    (*book_id)[total_size - 1] = '\0';

    /* verificam validitatea id-ului */
    for (i = 0; i < strlen((*book_id)); i++) {
        if (isalpha((*book_id)[i])) {
            return -1;
        }
    }

    return 0;
}

void print_error(char* response) {
    char* json_resp;

    /* printam eroarea descrisa in corpul raspunsului primit de la server */
    json_resp = basic_extract_json_response(response);
    if (json_resp != NULL) {
        JSON_Value *jvalue;
        JSON_Object *jobj;

        jvalue = json_parse_string(json_resp);
        jobj = json_value_get_object(jvalue);

        printf("%s\n", json_object_get_string(jobj, "error"));

        json_value_free(jvalue);
    }
}

int give_feedback(char* response, char* positive) {
    char* status_code = NULL;

    status_code = strstr(response, HTTPVERS);
    if(status_code == NULL) {
        printf("ERROR: HTTP/1.1 not present!\n");
        return -1;
    }

    /* extragem status code-ul din raspuns */
    status_code = status_code + strlen(HTTPVERS) + 1;

    char* feedback = (char*) malloc(MAXBUFSIZE * sizeof(char));
    DIE(feedback == NULL, "malloc");

    memset(feedback, 0, MAXBUFSIZE);

    /* il retinem */
    int i = 0;
    char c = status_code[i];
    while (c != '\r') {
        feedback[i] = c;
        i++;
        c = status_code[i];
    }

    if (strlen(feedback) == 0) {
        printf("ERROR: No feedback!\n");
        free(feedback);
        return -1;
    }

    int ret = 0;

    /* in functie de categoria codului, interpretam raspunsul */
    switch (feedback[0]) {
        case '2':
            /* operatie realizata cu succes, intoarcem feedback pozitiv */
            printf("%s - %s\n", feedback, positive);
            ret = 0;
            break;
        case '4':
            /* eroare, afisam mesajul transmis de server */
            printf("%s - ", feedback);
            print_error(response);
            ret = -1;
            break;
        default:
            /* in mod implicit afisam mesajul transmis de server */
            printf("%s - ", feedback);
            print_error(response);
            ret = -1;
            break;
    }

    free(feedback);
    return ret;
}

char* build_account_content(char* user, char* pass) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    JSON_Status res;

    /* construim un obiect json cu datele contului date ca parametri */
    res = json_object_set_string(root_object, "username", user);
    DIE(res == JSONFailure, "json_object_set_string");

    res = json_object_set_string(root_object, "password", pass);
    DIE(res == JSONFailure, "json_object_set_string");

    char* content;
    content = json_serialize_to_string(root_value);

    json_value_free(root_value);
    return content;
}

char* build_book_content(char* title,
                         char* author,
                         char* genre,
                         char* publisher,
                         int page_count) {
    JSON_Value *root_value = json_value_init_object();
    JSON_Object *root_object = json_value_get_object(root_value);
    JSON_Status res;

    /* construim un obiect json cu datele cartii date ca parametri */
    res = json_object_set_string(root_object, "title", title);
    DIE(res == JSONFailure, "json_object_set_string");

    res = json_object_set_string(root_object, "author", author);
    DIE(res == JSONFailure, "json_object_set_string");

    res = json_object_set_string(root_object, "genre", genre);
    DIE(res == JSONFailure, "json_object_set_string");

    res = json_object_set_number(root_object, "page_count", (double) page_count);
    DIE(res == JSONFailure, "json_object_set_number");

    res = json_object_set_string(root_object, "publisher", publisher);
    DIE(res == JSONFailure, "json_object_set_string");

    char* content;
    content = json_serialize_to_string(root_value);

    json_value_free(root_value);
    return content;
}

int store_cookies(char* response) {
    /*
     * cum strstr() gaseste prima aparitie a unui sir,
     * aceasta va gasi cuvantul cheie in header;
     * in plus, raspunsul lui login in acest caz
     * nu contine payload
     */
    int cnt;
    char* cookie;
    char* crt_poz = response;
    int num_cookies = 0;
    
    /*
     * retinem toate cookie-urile prezente in
     * header-ul raspunsului dat de server
     */
    while (1) {
        cookie = strstr(crt_poz, SET_COOKIE);

        if (cookie == NULL)
            break;

        cnt = my_client->cookies_count;

        /* adaugam un nou element in vectorul de pointeri la cookie-uri */
        if (my_client->cookies == NULL) {
            my_client->cookies = (char**) malloc(sizeof(char*));
            DIE(my_client->cookies == NULL, "malloc");
        } else {
            my_client->cookies = (char**) realloc(my_client->cookies,
                                                  (cnt + 1) * sizeof(char*));
            DIE(my_client->cookies == NULL, "realloc");
        }
        
        /* alocam resurse pentru cookie-ul curent */
        my_client->cookies[cnt] = (char*) malloc(COOKIE_SIZE * sizeof(char));
        DIE(my_client->cookies[cnt] == NULL, "malloc");

        memset(my_client->cookies[cnt], 0, COOKIE_SIZE);

        cookie = cookie + strlen(SET_COOKIE);

        /* copiem cookie-ul curent din header */
        int i = 0;
        char c = cookie[i];
        while (c != '\r') {
            my_client->cookies[cnt][i] = c;
            i++;
            c = cookie[i];
        }

        my_client->cookies_count++;
        crt_poz = cookie + strlen(my_client->cookies[cnt]) - 1;
        num_cookies++;
    }

    return num_cookies;
}

void free_cookies() {
    int i;

    /* stergem cookie-urile si eliberam resursele alocate retinerii acestora */
    for (i = 0; i < my_client->cookies_count; i++) {
        free(my_client->cookies[i]);
    }
    free(my_client->cookies);

    my_client->cookies_count = 0;
    my_client->cookies = NULL;
}

void store_token(char* response) {
    char* json_resp;

    /*
     * extragem token-ul din raspunsul dat de server
     * si il retinem in structura clientului
     */
    json_resp = basic_extract_json_response(response);
    if (json_resp != NULL) {
        JSON_Value *jvalue;
        JSON_Object *jobj;

        jvalue = json_parse_string(json_resp);
        jobj = json_value_get_object(jvalue);

        sprintf(my_client->jwt_token, "%s", json_object_get_string(jobj,
                                                                   "token"));
        my_client->has_token = 1;

        json_value_free(jvalue);
    }
}

void erase_token() {
    /* stergem token-ul anterior retinut in buffer */
    memset(my_client->jwt_token, 0, TOKEN_SIZE);
    my_client->has_token = 0;
}

void print_all_books(char* response) {
    int i;
    char* json_resp;

    /*
     * parsam vectorul de obiecte din body-ul json dat
     * ca raspuns de server si afisam toate elementele
     */
    json_resp = strstr(response, "[");
    if (json_resp != NULL) {
        JSON_Value *jvalue;
        JSON_Array *jarray;
        JSON_Object *book;

        jvalue = json_parse_string(json_resp);
        jarray = json_value_get_array(jvalue);

        for (i = 0; i < json_array_get_count(jarray); i++) {
            book = json_array_get_object(jarray, i);
            
            printf("id: %d\n", (int) json_object_get_number(book, "id"));
            printf("title: %s\n", json_object_get_string(book, "title"));
            printf("\n");
        }

        json_value_free(jvalue);
    }
}

void print_book(char* response) {
    char* json_resp;

    /* parsam body-ul json din raspunsul dat de server si afisam fiecare camp */
    json_resp = basic_extract_json_response(response);
    if (json_resp != NULL) {
        JSON_Value *jvalue;
        JSON_Object *book;

        jvalue = json_parse_string(json_resp);
        book = json_value_get_object(jvalue);

        printf("id = %d\n", (int) json_object_get_number(book, "id"));
        printf("title = %s\n", json_object_get_string(book, "title"));
        printf("author = %s\n", json_object_get_string(book, "author"));
        printf("genre = %s\n", json_object_get_string(book, "genre"));
        printf("publisher = %s\n", json_object_get_string(book, "publisher"));
        printf("page_count = %d\n", (int) json_object_get_number(book,
                                                                 "page_count"));
        printf("\n");

        json_value_free(jvalue);
    }
}

char* build_book_url(char* book_id) {
    char* url;

    /* compunem URL-ul */
    url = malloc(MAXBUFSIZE * sizeof(char));
    DIE(url == NULL, "malloc");
    memset(url, 0, MAXBUFSIZE);

    strcat(url, "/api/v1/tema/library/books/");
    strcat(url, book_id);

    return url;
}

void free_resources() {
    int res;

    /* eliberam resursele clientului */

    /* cookie-uri */
    if (my_client->cookies != NULL) {
        free_cookies();
    }

    /* token */
    if (my_client->has_token == 1) {
        erase_token();
    }
    free(my_client->jwt_token);
    
    /* inchidem socket-ul */
    if (my_client->state == STATE_CONNECTED)
        close(my_client->sockfd);
    
    /* eliberam structura asociata clientului */
    free(my_client);
    
    /* inchidem epoll */
    res = close(epollfd);
    DIE(res < 0, "close");
}

void handle_login(char* user, char* pass) {
    int res;
    char* content;
    char* message;
    char* recv_mess;
    int content_len;

    /* restabilim conexiunea la server daca este cazul */
    maintain_conn();
    /* construim body-ul json al cererii */
    content = build_account_content(user, pass);
    content_len = (int) strlen(content);

    /* construim cererea */
    message = compute_post_request("34.254.242.81:8080", 
                                   "/api/v1/tema/auth/login",
                                   NULL, 
                                   "application/json", 
                                   content, 
                                   content_len, 
                                   NULL, 
                                   0);

    /* trimitem cererea la server si preluam raspunsul */
    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);
    
    /*
     * daca autentificarea s-a realizat cu succes,
     * retinem / suprascriem cookie-ul / cookie-urile de sesiune
     */
    res = give_feedback(recv_mess, "Utilizator autentificat cu succes!");
    if (res == 0) {
        free_cookies();
        store_cookies(recv_mess);
    }

    handle_conn_closed();

    free(recv_mess);
    free(message);
    json_free_serialized_string(content);
}

void handle_register(char* user, char* pass) {
    char* content;
    char* message;
    char* recv_mess;
    int content_len;

    /* restabilim conexiunea la server daca este cazul */
    maintain_conn();
    /* construim body-ul json al cererii */
    content = build_account_content(user, pass);
    content_len = (int) strlen(content);

    /* construim cererea */
    message = compute_post_request("34.254.242.81:8080", 
                                   "/api/v1/tema/auth/register",
                                   NULL, 
                                   "application/json", 
                                   content, 
                                   content_len, 
                                   NULL, 
                                   0);

    /* trimitem cererea la server */
    send_to_server(my_client->sockfd, message);
    /* preluam raspunsul serverului la cerere */
    recv_mess = receive_from_server(my_client->sockfd);
    /* parsam raspunsul pentru a oferi feedback utilizatorului */
    give_feedback(recv_mess, "Utilizator inregistrat cu succes!");

    handle_conn_closed();

    free(recv_mess);
    free(message);
    json_free_serialized_string(content);
}

void handle_access() {
    int res;
    char* message;
    char* recv_mess;

    maintain_conn();
    /* fiind o cerere de tip GET, nu mai avem body */
    message = compute_request(GET_TYPE,
                              "34.254.242.81:8080",
                              "/api/v1/tema/library/access",
                              NULL,
                              NULL,
                              my_client->cookies,
                              my_client->cookies_count);

    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);

    /*
     * daca accesul la biblioteca s-a realizat cu succes,
     * retinem / surprascriem token-ul
     */
    res = give_feedback(recv_mess, "Acces la biblioteca permis!");
    if (res == 0) {
        erase_token();
        store_token(recv_mess);
    }

    handle_conn_closed();

    free(recv_mess);
    free(message);
}

void handle_get_books() {
    int res;
    char* message;
    char* recv_mess;

    maintain_conn();
    message = compute_request(GET_TYPE,
                              "34.254.242.81:8080",
                              "/api/v1/tema/library/books",
                              my_client->jwt_token,
                              NULL,
                              my_client->cookies,
                              my_client->cookies_count);

    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);

    /* daca cererea a reusit, afisam biblioteca */
    res = give_feedback(recv_mess, "Operatie reusita!");
    if (res == 0) {
        print_all_books(recv_mess);
    }

    handle_conn_closed();
    
    free(recv_mess);
    free(message);
}

void handle_get_book_info(char* book_id) {
    int res;
    char* message;
    char* recv_mess;
    char* url;

    maintain_conn();
    /* construim URL-ul pe baza id-ului dat ca parametru */
    url = build_book_url(book_id);
    message = compute_request(GET_TYPE,
                              "34.254.242.81:8080",
                              url,
                              my_client->jwt_token,
                              NULL,
                              my_client->cookies,
                              my_client->cookies_count);

    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);

    /*
     * daca am introdus un ID valid (prezent in biblioteca),
     * afisam datele cartii din corpul mesajului trimis ca raspuns
     */
    res = give_feedback(recv_mess, "Operatie reusita!");
    if (res == 0) {
        print_book(recv_mess);
    }

    handle_conn_closed();

    free(url);
    free(recv_mess);
    free(message);
}

void handle_add_book(char* title,
                     char* author,
                     char* genre,
                     char* publisher,
                     int page_count) {
    char* content;
    char* message;
    char* recv_mess;
    int content_len;

    maintain_conn();
    /* construim body-ul json cu datele cartii */
    content = build_book_content(title, author, genre, publisher, page_count);
    content_len = (int) strlen(content);

    message = compute_post_request("34.254.242.81:8080", 
                                   "/api/v1/tema/library/books",
                                   my_client->jwt_token, 
                                   "application/json", 
                                   content, 
                                   content_len, 
                                   my_client->cookies,
                                   my_client->cookies_count);
    
    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);
    give_feedback(recv_mess, "Operatie reusita!");

    handle_conn_closed();

    free(recv_mess);
    free(message);
    json_free_serialized_string(content);
}

void handle_delete_book(char* book_id) {
    char* message;
    char* recv_mess;
    char* url;

    maintain_conn();
    /* construim URL-ul pe baza id-ului dat ca parametru */
    url = build_book_url(book_id);
    message = compute_request(DELETE_TYPE,
                              "34.254.242.81:8080",
                              url,
                              my_client->jwt_token,
                              NULL,
                              my_client->cookies,
                              my_client->cookies_count);
    
    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);
    give_feedback(recv_mess, "Operatie reusita!");

    handle_conn_closed();

    free(url);
    free(recv_mess);
    free(message);
}

void handle_logout() {
    int res;
    char* message;
    char* recv_mess;

    maintain_conn();
    message = compute_request(GET_TYPE,
                              "34.254.242.81:8080",
                              "/api/v1/tema/auth/logout",
                              NULL,
                              NULL,
                              my_client->cookies,
                              my_client->cookies_count);

    send_to_server(my_client->sockfd, message);
    recv_mess = receive_from_server(my_client->sockfd);

    /*
     * daca deautentificarea s-a realizat cu succes, stergem
     * cookie-urile de sesiune si token-ul, pentru a nu fi 
     * folosite de un eventual alt utilizator
     */
    res = give_feedback(recv_mess, "Deautentificare realizata!");
    if (res == 0) {
        free_cookies();
        erase_token();
    }

    /* inchidem conexiunea */
    handle_conn_closed();

    free(recv_mess);
    free(message);
}

int handle_new_command() {
    int res;
    char *command = NULL;
    size_t comm_len = 0;
    ssize_t comm_size = 0;

    /* comanda va fi terminata cu '\n' */
    comm_size = getline(&command, &comm_len, stdin);
    if (comm_size < 0)
        printf("ERROR: Failed reading from STDIN.\n");

    if (strcmp(command, "register\n") == 0) {
        /* preluam username si parola */
        char* user = NULL;
        char* pass = NULL;

        res = get_user_and_pass(&user, &pass);

        if (res == 0) {
            /*
             * daca avem date valide, trimitem
             * cererea de inregistrare la server
             */
            handle_register(user, pass);
        } else {
            /* altfel, afisam un mesaj de eroare */
            printf("ERROR: Username and password can't "
                   "contain space characters!\n");
        }

        if(user != NULL)
            free(user);
        if(pass != NULL)
            free(pass);

    } else if (strcmp(command, "login\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->cookies != NULL) {
            printf("You're already logged in. Please logout first!\n");
            free(command);
            return 0;
        }

        /* daca este valida, preluam datele de conectare */
        char* user = NULL;
        char* pass = NULL;
        
        res = get_user_and_pass(&user, &pass);

        if (res == 0) {
            /* daca avem date valide, trimitem cererea */
            handle_login(user, pass);
        } else {
            /* altfel, afisam un mesaj de eroare */
            printf("ERROR: Username and password can't "
                   "contain space characters!\n");
        }

        if(user != NULL)
            free(user);
        if(pass != NULL)
            free(pass);

    } else if (strcmp(command, "enter_library\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->cookies == NULL) {
            printf("You're not logged in. Please log in first!\n");
            free(command);
            return 0;
        }

        handle_access();

    } else if (strcmp(command, "get_books\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->has_token == 0) {
            printf("You don't have access to the library!\n");
            free(command);
            return 0;
        }

        handle_get_books();

    } else if (strcmp(command, "get_book\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->has_token == 0) {
            printf("You don't have access to the library!\n");
            free(command);
            return 0;
        }

        /*
         * preluam id-ul cartii de la utilizator,
         * verificand daca s-a introdus un id valid
         */
        char* book_id = NULL;
        res = get_book_id(&book_id);

        if (res == 0) {
            /* daca avem un id valid, trimitem cererea */
            handle_get_book_info(book_id);
        } else {
            /* altfel, afisam un mesaj de eroare */
            printf("ERROR: 'id' must be an integer! Please retry!\n");
        }

        if(book_id != NULL)
            free(book_id);

    } else if (strcmp(command, "add_book\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->has_token == 0) {
            printf("You don't have access to the library!\n");
            free(command);
            return 0;
        }
        
        /* preluam datele cartii */
        char* title = NULL;
        char* author = NULL;
        char* genre = NULL;
        char* publisher = NULL;
        int page_count;

        res = get_book_info(&title, &author, &genre, &publisher, &page_count);

        if (res == 0) {
            /* daca datele introduse sunt valide, trimitem cererea */
            handle_add_book(title, author, genre, publisher, page_count);
        } else {
            /* altfel, afisam un mesaj de eroare */
            printf("ERROR: 'page_count' must be an integer! Please retry!\n");
        }

        /* sirurile de caractere se elibereaza in orice caz */
        if(title != NULL)
            free(title);
        if(author != NULL)
            free(author);
        if(genre != NULL)
            free(genre);
        if(publisher != NULL)
            free(publisher);

    } else if (strcmp(command, "delete_book\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->has_token == 0) {
            printf("You don't have acces to the library!\n");
            free(command);
            return 0;
        }

        /* preluam id-ul cartii */
        char* book_id = NULL;
        res = get_book_id(&book_id);

        if (res == 0) {
            /* daca id-ul este valid (numar intreg), trimitem cererea */
            handle_delete_book(book_id);
        } else {
            printf("ERROR: 'id' must be an integer! Please retry!\n");
        }

        if(book_id != NULL)
            free(book_id);

    } else if (strcmp(command, "logout\n") == 0) {
        /* verificam validitatea comenzii */
        if (my_client->cookies == NULL) {
            printf("You're not logged in!\n");
            free(command);
            return 0;
        }

        handle_logout();

    } else if (strcmp(command, "exit\n") == 0) {
        /* eliberam resursele clientului inainte de inchidere */
        free_resources();
        free(command);
        return -1;
    } else
        printf("ERROR: Don't recognize command.\n");

    free(command);
    return 0;
}

void handle_server_mess() {
    char buf[MAXBUFSIZE];
    int buf_len;

    /*
     * functia ar trebui sa fie apelata doar in cazul inchiderii conexiunii
     * din partea serverului, deoarece in rest citim toti octetii trimisi de
     * server ca raspuns la cererile clientului
     */
    buf_len = recv(my_client->sockfd, buf, MAXBUFSIZE, 0);

    if (buf_len < 0) {
        printf("ERROR: Error in communication from server.\n");
        return;
    }
    if (buf_len == 0) {
        printf("Connection closed from server.\n");

        handle_conn_closed();
        return;
    }

    printf("ERROR: There should be no bytes left to read!\n");
}

int main(int argc, char *argv[])
{
    int res;

    init_client();

    /* folosim epoll pentru multiplexare */
	epollfd = w_epoll_create();
	DIE(epollfd < 0, "w_epoll_create");

    res = w_epoll_add_fd_in(epollfd, STDIN_FILENO);
	DIE(res < 0, "w_epoll_add_fd_in");

    while (1) {
        struct epoll_event ev;

        /* asteptam eventimente */
		res = w_epoll_wait_infinite(epollfd, &ev);
		DIE(res < 0, "w_epoll_wait_infinite");

        if (ev.data.fd == STDIN_FILENO) {
            if (ev.events & EPOLLIN) {
                /* comanda de la tastatura */
                res = handle_new_command();

                if (res == -1) {
                    printf("Client closed.\n");
                    return 0;
                }
            }
        } else if (ev.data.fd == my_client->sockfd) {
            /* mesaj de la server */
            handle_server_mess();
        }
    }

    return 0;
}
