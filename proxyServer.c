
#include "threadpool.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <netinet/in.h>


#define usage "Usage: proxyServer <port> <pool size> <max number of request> <filter>\n"
#define BUFFER_SZ 128

/********** filtered hosts list********/
static int filter_size = 1; 
static char **filter;
/*************************************/

int clientHandler(void *arg);
char *errorResponse(int code);
int create_socket(int);
char *check_input(char *str, int client_sd);
int buildFilter(char *);
char *sendRequestToServer(char *host, int port, char *request, int client_sd);

int main(int argc, char **argv)
{

    if (argc != 5)
    {
        printf("%s", usage);
        exit(EXIT_FAILURE);
    }

    //these args (1 to 3) should be integers
    for (int i = 1; i <= 3; i++)
    {
        int x = atoi(argv[i]);
        if (x <= 0)
        {
            printf("%s", usage);
            exit(EXIT_FAILURE);
        }
    }
    // and are now safe to convert
    int proxy_port = atoi(argv[1]);
    int pool_size = atoi(argv[2]);
    int max_requests = atoi(argv[3]);
    char *filterFilePath = argv[4];

    // create the main socket
    int sockfd = create_socket(proxy_port);
    if (sockfd < 0)
        exit(1);

    threadpool *pool = create_threadpool(pool_size);
    if (pool == NULL)
    {
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    // try to build the filtered hosts array from the file
    if (buildFilter(filterFilePath) < 0)
    {
        destroy_threadpool(pool);
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    int connections[max_requests];
    for (int i = 0; i < max_requests; i++)
    {
        connections[i] = accept(sockfd, NULL, NULL);
        if (connections[i] < 0)
        {
            perror("error: accept\n");
            continue;
        }
        dispatch(pool, clientHandler, (void *)&connections[i]);
    }

    destroy_threadpool(pool);
    for (int i = 0; i < filter_size; i++)
    {
        free(filter[i]);
    }
    free(filter);

    return 0;
}

// Create the main socket of the proxy server
int create_socket(int port)
{
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("error: socket\n");
        return -1;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("error: bind\n");
        close(sockfd);
        return -1;
    }
    if (listen(sockfd, 5) < 0)
    {
        perror("error: listen\n");
        close(sockfd);
        return -1;
    }

    return sockfd;
}

//create the corresponding response for each error code
char *errorResponse(int code)
{
    char *ht = "HTTP/1.0";
    char *server = "Server: webserver/1.0";
    char *type = "Content-Type: text/html";
    char *connection = "Connection: close";
    char *what, *contentLen, *explanation;

    char *responseMsg = (char *)malloc(sizeof(char));
    if (responseMsg == NULL)
    {
        perror("error: malloc\n");
        return NULL;
    }

    if (code == 400)
    {
        what = "400 Bad Request";
        contentLen = "Content-Length: 113";
        explanation = "Bad Request.";
        responseMsg = realloc(responseMsg, strlen(ht) + strlen(server) + strlen(type) + strlen(connection) + strlen(what) + strlen(contentLen) + strlen(explanation) + 113);
        if (responseMsg == NULL)
        {
            perror("error: realloc\n");
            return NULL;
        }
    }
    else if (code == 403)
    {
        what = "403 Forbidden";
        contentLen = "Content-Length: 111";
        explanation = "Access denied.";
        responseMsg = realloc(responseMsg, strlen(ht) + strlen(server) + strlen(type) + strlen(connection) + strlen(what) + strlen(contentLen) + strlen(explanation) + 111);
        if (responseMsg == NULL)
        {
            perror("error: realloc\n");
            return NULL;
        }
    }
    else if (code == 404)
    {
        what = "404 Not Found";
        contentLen = "Content-Length: 112";
        explanation = "File not found.";
        responseMsg = realloc(responseMsg, strlen(ht) + strlen(server) + strlen(type) + strlen(connection) + strlen(what) + strlen(contentLen) + strlen(explanation) + 112);
        if (responseMsg == NULL)
        {
            perror("error: realloc\n");
            return NULL;
        }
    }
    else if (code == 500)
    {
        what = "500 Internal Server Error";
        contentLen = "Content-Length: 144";
        explanation = "Some server side error.";
        responseMsg = realloc(responseMsg, strlen(ht) + strlen(server) + strlen(type) + strlen(connection) + strlen(what) + strlen(contentLen) + strlen(explanation) + 144);
        if (responseMsg == NULL)
        {
            perror("error: realloc\n");
            return NULL;
        }
    }
    else if (code == 501)
    {
        what = "501 Not supported";
        contentLen = "Content-Length: 129";
        explanation = "Method is not supported.";
        responseMsg = realloc(responseMsg, strlen(ht) + strlen(server) + strlen(type) + strlen(connection) + strlen(what) + strlen(contentLen) + strlen(explanation) + 129);
        if (responseMsg == NULL)
        {
            perror("error: realloc\n");
            return NULL;
        }
    }
    else
        return NULL;

    sprintf(responseMsg, "%s %s\n%s\n%s\n%s\n%s\n\n<HTML><HEAD><TITLE>%s</TITLE></HEAD>\n<BODY><H4>%s</H4>\n%s\n</BODY></HTML>", ht, what, server, type, contentLen, connection, what, what, explanation);
    return responseMsg;
}

/*this function for handle the client request*/
int clientHandler(void *arg)
{
    if (arg == NULL)
        return -1;

    //casting and dereferencing
    int *pointer_for_cast = (int *)arg;
    int socket_fd = *pointer_for_cast;

    char *request = (char *)malloc(sizeof(char));
    if (request == NULL)
    {
        perror("error: malloc\n");
        return -1;
    }

    char buffer[BUFFER_SZ];
    int bytes_read, total_chars = 1;
    buffer[0] = '\0';
    request[0] = '\0';

    //read the request from the client socket to the local buffer.
    // in chunks (pieces) of length BUFFER_SZ chars = bytes
    while ((bytes_read = read(socket_fd, buffer, BUFFER_SZ)) != 0) //read the request from the client to buffer[].
    {
        if (bytes_read < 0)
        {
            close(socket_fd);
            free(request);
            return -1;
        }
        if (bytes_read == 0)
            break; //empty message , not interesting

        total_chars += bytes_read;

        request = realloc(request, total_chars);
        if (request == NULL)
        {
            perror("error: realloc\n");
            return -1;
        }
        buffer[bytes_read] = '\0';

        //concatenate the current data from the buffer to the request buffer
        strncat(request, buffer, bytes_read);

        // end of the request should be "\r\n\r\n", if we reached there,stop
        if (strstr(request, "\r\n\r\n") != NULL)
            break;
    }

    // process the request
    char *response = check_input(request, socket_fd);
    if (strcmp(response, "ok") != 0)
    {
        int w = write(socket_fd, response, strlen(response) + 1);
        if (w < 0)
        {
            perror("error: write\n");
            close(socket_fd);
            free(request);
            free(response);
            return -1;
        }
        free(response);
    }
    close(socket_fd);
    free(request);
    return 0;
}

//process the query and return the result
char *check_input(char *str, int client_sd)
{
    if (str == NULL)
        return NULL;

    char *real_req = (char *)malloc(sizeof(char) * (strlen(str) + 1));
    if (real_req == NULL)
    {
        perror("error: malloc\n");
        return errorResponse(400); // query is too long
    }
    strncpy(real_req, str, strlen(str));
    real_req[strlen(str)] = '\0';

    // "\r\n" = end of first line
    char *first_line = strtok(str, "\r\n");
    if (first_line == NULL)
    {
        free(real_req);
        return errorResponse(400); // invalid request
    }
    char *request = (char *)malloc(sizeof(char) * (strlen(str) + 1));
    if (request == NULL)
    {
        perror("error: malloc\n");
        free(real_req);
        return NULL;
    }
    strncpy(request, first_line, strlen(first_line));
    request[strlen(str)] = '\0';

    char *method = NULL, *path = NULL, *version = NULL;
    // "METHOD PATH VERSION"
    method = strtok(first_line, " ");
    path = strtok(NULL, " ");
    version = strtok(NULL, " ");
    int port;

    /*check if one of this tokens not exist*/
    if (method == NULL || path == NULL || version == NULL)
    {
        free(request);
        free(real_req);
        return errorResponse(400);
    }

    //Only GET method is supported
    if (strcmp(method, "GET") != 0)
    {
        free(request);
        free(real_req);
        return errorResponse(501);
    }

    //Only these versions are supported
    if (strcmp(version, "HTTP/1.0") != 0 && strcmp(version, "HTTP/1.1") != 0)
    {
        free(request);
        free(real_req);
        return errorResponse(501);
    }

    // search for the Host header
    char *host = strstr(real_req, "Host:");
    if (host == NULL)
    {
        free(request);
        free(real_req);
        return errorResponse(400);
    }
    host += 5; //advance the pointer by the the strlen of "Host:"
    while (host[0] == ' ')
        host++; //skip whitespaces

    host = strtok(host, "\r");
    char *look_for_port = strstr(host, ":");
    if (look_for_port != NULL)
    {
        look_for_port++;
        port = atoi(look_for_port);
        if (port <= 0)
        {
            free(request);
            free(real_req);
            return errorResponse(400);
        }
        host = strtok(host, ":");
    }
    else
    {
        port = 80; //default value in case no port number is supplied
        host = strtok(host, "/");
    }

    // check if host is in the filter list
    for (int i = 0; i < filter_size - 1; i++)
    {
        char *temp = strtok(filter[i], "\r");
        if (temp != NULL)
            if (strcmp(host, temp) == 0)
            {
                free(request);
                free(real_req);
                return errorResponse(403);
            }
    }

    char *response = (char *)malloc(sizeof(char) * (strlen(request) + strlen(host) + 36));
    if (response == NULL)
    {
        perror("error: malloc\n");
        free(real_req);
        free(request);
        return errorResponse(500);
    }
    sprintf(response, "%s\r\nConnection: close\r\nHost: %s\r\n\r\n", request, host);

    char *answer = sendRequestToServer(host, port, response, client_sd);

    //done
    free(request);
    free(response);
    free(real_req);
    return answer;
}

// Generate array of filtered hosts
int buildFilter(char *path)
{
    size_t lineLength = 0;
    FILE *fp = fopen(path, "r"); // open in read mode
    if (fp == NULL)
    {
        perror("error: fopen\n");
        return -1;
    }
    filter = (char **)malloc(sizeof(char *)); //array of strings
    if (filter == NULL)
    {
        perror("error: malloc\n");
        return -1;
    }

    // read each line in the filter file */
    //last element will always be set to NULL to indicate end of filtered sites array
    // (unless we use a field of size / global variable)    int i = 0;
    filter[0] = NULL;

    /* 
        ssize_t getline(char **lineptr, size_t *n, FILE *stream);
        getline() reads an entire line from stream, storing the address of
       the buffer containing the text into *lineptr.  The buffer is null-
       terminated and includes the newline character, if one was found.

       TODO: If *lineptr is set to NULL and *n is set 0 before the call, then
       getline() will allocate a buffer for storing the line.  This buffer
       >>>should be freed<<< by the user program even if getline() failed. */
    for (int i = 0; getline(&filter[i], &lineLength, fp) != -1; i++)
    {
        filter_size++;

        //TODO: can be optimized by using treshold ,capacity = 2*capacity
        filter = realloc(filter, sizeof(char *) * filter_size);
        if (filter == NULL)
        {
            perror("error: malloc\n");
            return -1;
        }

        //last element will always be set to NULL to indicate end of filtered sites array
        filter[filter_size - 1] = NULL;
    }
    //no more lines to read from filter file
    fclose(fp);
    return 0;
}

// connect to the host server and get return the response
char *sendRequestToServer(char *host, int port, char *request, int client_sd)
{
    struct sockaddr_in serv_addr;
    struct hostent *server; //Description of data base entry for a single host.

    //create the socket.
    int new_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (new_socket < 0)
    {
        perror("error: socket\n");
        return errorResponse(500);
    }

    // set ip and port.
    server = gethostbyname(host);
    if (server == NULL)
    {
        herror("gethostbyname:");
        return errorResponse(404);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr_list[0], server->h_length);

    //connect to the server (host) through the new socket
    //Open a connection on socket FD to peer at ADDR (which LEN bytes long)
    int ret_code = connect(new_socket, (const struct sockaddr *)&serv_addr, sizeof(serv_addr));
    if (ret_code < 0)
    {
        perror("error: connect\n");
        return errorResponse(500);
    }

    //write the request for the server.
    int bytes_written = write(new_socket, request, strlen(request) + 1);
    if (bytes_written < 0)
    {
        perror("error: write\n");
        close(new_socket);
        return errorResponse(500);
    }

    unsigned char buffer[BUFFER_SZ];
    buffer[0] = '\0';

    //read the server's response to the buffer and send it to the client.
    while (1)
    {
        int bytes_recieved = read(new_socket, buffer, BUFFER_SZ);
        if (bytes_recieved < 0)
        {
            close(new_socket);
            perror("error: read\n");
            return errorResponse(500);
        }
        if (bytes_recieved == 0)
            break; //end of response

        size_t bytes_sent = send(client_sd, buffer, bytes_recieved, MSG_NOSIGNAL);
        if (bytes_sent < 0)
        {
            close(new_socket);
            perror("error: write\n");
            return errorResponse(500);
        }
    }

    //end of response
    close(new_socket);
    return "ok";
}
