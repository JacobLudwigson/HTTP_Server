#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <error.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
// TO - DO
// Implement sig handler for graceful Ctrl + C exit (if dynamically allocating memory)
// 

#define ERROR -1
#define MAX_CLIENTS 4 //Maybe?
#define MAX_DATA 1024 //Maybe?
// struct dynMem { //Define a struct such that we can pass our dynamically allocated memory to an error wrapper and free prior to exiting on error

// }
typedef struct httpPacket{ 
    char* requestType;
    char* pageRequest;
    int status;
    char* statusMessage;
    char* httpVersion;
    char* host;
    char* connection;
    char* contentType;
    int contentLength;
    char* data;
};
void formulateHttpPacket(httpPacket* packet, char* buffer, size_t bufferSize){
    memset(buffer,0,bufferSize);
    snprintf(buffer, bufferSize,
        "%s %d %s\r\n"
        "Host: %s\r\n"
        "Connection: %s\r\n"
        "Content-Type: %s"
        "Content-Length: %s"
        "\r\n\r\n"
        "%s",
        packet->httpVersion, packet->statusCode, packet->statusMessage,
        packet->host,
        packet->connection,
        packet->contentType,
        packet->contentLength,
        packet->data ? packet->data : ""
    );
}
// int exitError(dynMem memory){

// }
int serveClient(int sock, struct sockaddr_in* client, int new){
    char buffer[MAX_DATA];
    printf("Handling Client Connected from port no %d and IP %s\n", ntohs(client->sin_port), inet_ntop(client->sin_addr));

    int data_len = 1;

    while(1){
        data_len = recv(new, buffer,MAX_DATA,0);

        if (data_len){
            send(new,buffer,data_len,0);
            buffer[data_len] = '\0';
            printf("Sent Message: %s", buffer);
        }
    }
}
int main(int argc, char **argv){
    struct sockaddr_in server;
    // struct sockaddr_in clients[MAX_CLIENTS];
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
    while (1){
        if ((new = accept(sock, (struct sockaddr *) &client, &socketaddr_len)) == ERROR){
            //input error handling
            perror("Error in accept : ");
            exit(-1);
        }
        serveClient(sock,&client, new);
    }
    close(sock);
}