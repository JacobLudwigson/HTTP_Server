#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <error.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <pthread.h>
#include <arpa/inet.h>
// #include <POSIX.h>
// TO - DO
// Implement sig handler for graceful Ctrl + C exit (if dynamically allocating memory)
// Persistant connections using setsockop for 10 seconds
// 

#define ERROR -1
#define MAX_CLIENTS 1000 //Maybe?
#define MAX_DATA 4096 //Maybe?
#define LINETERMINATOR "\r\n"
#define HEADERSEPARATOR "\r\n\r\n"
int countActiveThreads;
typedef struct httpPacket{ 
    unsigned char* data;
    int status;
    int contentLength;
    char requestType[20];
    char pageRequest[50];
    char statusMessage[20];
    char httpVersion[50];
    char host[50];
    char connection[100];
    char contentType[150];
}httpPacket;

const char* get_content_type(const char* filename) {
    if (strstr(filename, ".html") != NULL || strstr(filename, ".htm") != NULL) {
        return "text/html";
    } else if (strstr(filename, ".css") != NULL) {
        return "text/css";
    } else if (strstr(filename, ".js") != NULL) {
        return "application/javascript";
    } else if (strstr(filename, ".jpg") != NULL ) {
        return "image/jpg";
    } else if (strstr(filename, ".png") != NULL) {
        return "image/png";
    } else if (strstr(filename, ".gif") != NULL) {
        return "image/gif";
    } else if (strstr(filename, ".txt") != NULL) {
        return "text/plain";
    } else if (strstr(filename, ".ico") != NULL) {
        return "image/x-icon";
    } else {
        return "application/octet-stream";
    }
}
void printPacket(struct httpPacket* packet){
    printf(
        "%s %d %s\r\n"
        "Host: %s\r\n"
        "Connection: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        packet->httpVersion, packet->status, packet->statusMessage,
        packet->host,
        packet->connection,
        packet->contentType,
        packet->contentLength
    );
}
int formulateHttpPacket(struct httpPacket* packet, char* buffer, size_t bufferSize){
    int len = snprintf(buffer, bufferSize,
        "%s %d %s\r\n"
        "Connection: %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "\r\n",
        packet->httpVersion ? packet->httpVersion : "", 
        packet->status ? packet->status : 100, 
        packet->statusMessage ? packet->statusMessage : "",
        packet->connection ? packet->connection : "",
        packet->contentType ? packet->contentType : "",
        packet->contentLength ? packet->contentLength : 0
    );
    memcpy(buffer + len, packet->data, packet->contentLength);
    return len + packet->contentLength;
}
int decodeHttpPacket(struct httpPacket* packet, char* buffer, size_t bufferLength ){
    int count = 0;
    char* line = strtok(buffer, "\r\n");
    int status;
    char first[50];
    char second[1024];

    sscanf(line, "%s %s %s",packet->requestType, packet->pageRequest, packet->httpVersion);
    // line = strtok(NULL, "\r\n");
    while ((line = strtok(NULL, "\r\n")) != NULL){
        if (strcmp(line,"") == 0 || line == NULL){
            break;
        }
        sscanf(line, "%s %s", first,second);
        first[strlen(first)-1] = '\0';
        if (strcmp(first, "Host") == 0){
            count += 1;
            strcpy(packet->host, second);
        }
        else if (strcmp(first, "Connection") == 0){
            count += 1;
            strcpy(packet->connection, second);
        }
        else if (strcmp(first, "Accept") == 0){
            count += 1;
            strcpy(packet->contentType, second);
        }
    }
    return count == 3;
}
void errorPacket(int errorCode, struct httpPacket* responsePacket){
    strcpy(responsePacket->contentType, "text/html");
    responsePacket->status = errorCode;
    return;
}
void buildResponsePacket(struct httpPacket* requestPacket, struct httpPacket* responsePacket, int decode){
    strcpy(responsePacket->httpVersion,requestPacket->httpVersion);
    if (decode != 1){
        errorPacket(400, responsePacket);
        return;
    }
    if (strcmp(requestPacket->requestType,"GET") != 0){
        errorPacket(405, responsePacket);
        return;
    }
    if (strcmp(requestPacket->httpVersion, "HTTP/1.1") != 0 && strcmp(requestPacket->httpVersion,"HTTP/1.0") != 0){
        errorPacket(500, responsePacket);
    }
    if (strcmp(requestPacket->pageRequest,"/") == 0){
        strcpy(requestPacket->pageRequest,"/index.html");
    }
    //Might want to change this? Filenames probably shouldnt be over 100 characters but this could overflow buffer as is 
    char filename[100]= "www";
    strcat(filename,requestPacket->pageRequest);
    if (access(filename, F_OK) != 0){
        errorPacket(404, responsePacket);
        return;
    }
    FILE* fptr = fopen(filename, "rb");
    if (!fptr){
        errorPacket(403, responsePacket);
        return;
    }
    if (strcmp(requestPacket->connection, "keep-alive") != 0){
        strcpy(responsePacket->connection,"Close");
    }
    else{
        strcpy(responsePacket->connection,"keep-alive");
    }
    fseek(fptr,0, SEEK_END);
    long int fileSize = ftell(fptr);
    rewind(fptr);
    unsigned char* buffer = (unsigned char*) malloc(fileSize+1);
    if (buffer == NULL){
        perror("Error in memory allocation: ");
        exit(-1);
    }
    long int bytesRead = fread(buffer,sizeof(char), fileSize,fptr);
    if (fileSize != bytesRead){
        printf("File read, supposed to read %ld bytes and actually read %ld\n",fileSize, bytesRead);
        errorPacket(403, responsePacket);
        return;
    }
    strcpy(responsePacket->contentType, get_content_type(requestPacket->pageRequest));
    responsePacket->contentLength = fileSize;
    buffer[bytesRead] = '\0';
    responsePacket->data = buffer;
    fclose(fptr);

    responsePacket->status = 200;
    return;
}
// int exitError(dynMem memory){

// }
//ALL FIELDS OF PACKETS ARE THIS FUNCTIONS JOB TO FREE (I.E. A BUFFER WILL BE ALLOCATED FOR "data" SECTION OF HTTP PACKET THAT WILL NOT HAVE A COORESPONDING FREE)
void* serveClient(void* data){
    countActiveThreads+=1;
    int socket = *(int*)data;
    // printf("New thread with socket %d\n", socket);

    char buffer[MAX_DATA];
    char* responseBuffer;

    int data_len = 1;
    int persistant = 0;
    int decodeStatus;
    int length = 0;
    int bytesSent;
    struct httpPacket* requestPacket = (httpPacket*) calloc(1, sizeof(httpPacket));
    struct httpPacket* responsePacket= (httpPacket*) calloc(1, sizeof(httpPacket));
    struct timeval timeout;      
    timeout.tv_sec = 10;
    timeout.tv_usec = 0;
    int op = 1;
    if (setsockopt(socket, SOL_SOCKET,SO_REUSEADDR, &op, sizeof(op)) < 0){
        perror("Error setting sock op: ");
        free(responsePacket);
        free(requestPacket);
        return NULL;
    }
    if (setsockopt (socket, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                sizeof timeout) < 0)
        perror("setsockopt failed\n");
        free(responsePacket);
        free(requestPacket);
        return NULL;
    do {
        data_len = recv(socket, buffer,MAX_DATA,0);
        if (data_len < 0) {
            printf("Client timeout, returning...\n");
            break;
        }
        decodeStatus = decodeHttpPacket(requestPacket, buffer, data_len);
        buildResponsePacket(requestPacket, responsePacket, decodeStatus);
        length = 1024 + responsePacket->contentLength + 1;
        responseBuffer = (unsigned char*)calloc(1,length);
        length = formulateHttpPacket(responsePacket,responseBuffer, length);
        bytesSent = send(socket, responseBuffer, length,0);
        if (bytesSent != length){
            printf("Error in thread with socket %d\n\n", socket);

            perror("Error in send ");
            return NULL;
        }
        else{
            printf("Served: %s\n", requestPacket->pageRequest);
        }
        if (strcmp(responsePacket->connection,"keep-alive")){
            persistant = 1;
        }
        else{
            persistant = 0;
        }
        if (requestPacket->data){
            free(requestPacket->data);
        }
        if (responsePacket->data){
            free(responsePacket->data);
        }
        free(responseBuffer);
        memset(buffer,0,MAX_DATA);
    }while(persistant);
    if (requestPacket->data){
        free(requestPacket->data);
    }
    if (responsePacket->data){
        free(responsePacket->data);
    }
    free(responsePacket);
    free(requestPacket);
    close(socket);
    countActiveThreads -=1;
    // printf("Exiting thread with socket %d\n\n", socket);
    return NULL;
}
int main(int argc, char **argv){
    if (argc < 2){
        printf("Incorrect Number of Args! Run with ./server [Port Number]\n");
        exit(-1);
    }
    struct sockaddr_in server;
    struct sockaddr_in client;
    int sock;
    int new;
    int socketaddr_len = sizeof(struct sockaddr_in);
    int data_len;
    char data[MAX_DATA];


    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == ERROR){
        //Error handling if socket cant open
        perror("Error in socket : ");
        exit(-1);
    }
    int op = 1;
    if (setsockopt(sock, SOL_SOCKET,SO_REUSEADDR, &op, sizeof(op)) < 0){
        perror("Error setting sock op: ");
        exit(-1);
    }
    server.sin_family = AF_INET;
    server.sin_port = htons(atoi(argv[1]));
    server.sin_addr.s_addr = INADDR_ANY;
    bzero(&server.sin_zero,8);
    if ((bind(sock, (struct sockaddr* )&server, socketaddr_len)) == ERROR){
        //Error binding to socket
        perror("Error in bind : ");
        exit(-1);
    }

    if ((listen(sock, MAX_CLIENTS)) == -1){
        //Error in listen
        perror("Error in listen : ");
        exit(-1);
    }
    char ip_str[INET_ADDRSTRLEN];
    countActiveThreads = 0;
    while (1){
        if ((new = accept(sock, (struct sockaddr *) &client, &socketaddr_len)) == ERROR){
            //input error handling
            perror("Error in accept : ");
            exit(-1);
        }
        if (inet_ntop(AF_INET, &client.sin_addr, ip_str, sizeof(ip_str)) == NULL) {
            perror("inet_ntop error");
            // handle error as needed
        }
        printf("Handling Client Connected from port no %d and IP %s\n",ntohs(client.sin_port), ip_str);
        printf("Accepted connection, client_fd = %d\n", new);
        printf("Current Active Threads %d\n",countActiveThreads);

        void* data = &new;
        pthread_t ptid;
        pthread_create(&ptid, NULL, &serveClient, data);
        pthread_detach(ptid);
    }
    close(sock);
}