#include <bits/stdc++.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>

/////////////////////////////
#include <iostream>
#include <assert.h>
#include <tuple>
using namespace std;
/////////////////////////////

// Regular bold text
#define BBLK "\e[1;30m"
#define BRED "\e[1;31m"
#define BGRN "\e[1;32m"
#define BYEL "\e[1;33m"
#define BBLU "\e[1;34m"
#define BMAG "\e[1;35m"
#define BCYN "\e[1;36m"
#define ANSI_RESET "\x1b[0m"

typedef long long LL;

#define pb push_back
#define debug(x) cout << #x << " : " << x << endl
#define part cout << "-----------------------------------" << endl;

///////////////////////////////
#define MAX_CLIENTS 15
#define PORT_ARG 8001

const int initial_msg_len = 256;

////////////////////////////////////

const LL buff_sz = 1048576;
///////////////////////////////////////////////////

pthread_mutex_t queue_lock;
pthread_mutex_t dictionary_key_lock[101];
sem_t queue_sem;

queue<int> fd_queue;
map<int, string> dictionary;

///////////////////////////////////////////////////
pair<string, int> read_string_from_socket(const int &fd, int bytes)
{
    std::string output;
    output.resize(bytes);

    int bytes_received = read(fd, &output[0], bytes - 1);
    // debug(bytes_received);
    if (bytes_received <= 0)
    {
        cerr << "Failed to read data from socket. \n";
    }

    output[bytes_received] = 0;
    output.resize(bytes_received);
    // debug(output);
    return {output, bytes_received};
}

int send_string_on_socket(int fd, const string &s)
{
    // debug(s.length());
    int bytes_sent = write(fd, s.c_str(), s.length());
    if (bytes_sent < 0)
    {
        cerr << "Failed to SEND DATA via socket.\n";
    }

    return bytes_sent;
}

///////////////////////////////

void handle_connection(int client_socket_fd)
{
    // int client_socket_fd = *((int *)client_socket_fd_ptr);
    //####################################################

    string thread_id = to_string(pthread_self());
    int received_num, sent_num;
    int sent_to_client;
    /* read message from client */
    int ret_val = 1;
    string cmd;
    tie(cmd, received_num) = read_string_from_socket(client_socket_fd, buff_sz);
    ret_val = received_num;
    // debug(ret_val);
    // printf("Read something\n");

    // tokenize cmd string
    stringstream ss(cmd);
    vector<string> tokens;
    string token;
    while (getline(ss, token, ' '))
    {
        tokens.push_back(token);
    }

    string msg_to_send_back = thread_id + ":";
    if (ret_val <= 0)
    {
        // perror("Error read()");
        printf("Server could not read msg sent from client\n");
        goto close_client_socket_ceremony;
    }
    // execute command
    if (tokens[1] == "insert")
    {
        int key = stoi(tokens[2]);
        string value = tokens[3];
        pthread_mutex_lock(&dictionary_key_lock[key]);
        if (dictionary.find(key) == dictionary.end())
        {
            dictionary[key] = value;
            msg_to_send_back += "Insertion successful";
        }
        else
        {
            msg_to_send_back += "Key already exists";
        }
        pthread_mutex_unlock(&dictionary_key_lock[key]);
    }
    else if (tokens[1] == "delete")
    {
        int key = stoi(tokens[2]);
        pthread_mutex_lock(&dictionary_key_lock[key]);
        if (dictionary.find(key) != dictionary.end())
        {
            dictionary.erase(key);
            msg_to_send_back += "Deletion successful";
        }
        else
        {
            msg_to_send_back += "No such key exists";
        }
        pthread_mutex_unlock(&dictionary_key_lock[key]);
    }
    else if (tokens[1] == "update")
    {
        int key = stoi(tokens[2]);
        string value = tokens[3];
        pthread_mutex_lock(&dictionary_key_lock[key]);
        if (dictionary.find(key) != dictionary.end())
        {
            dictionary[key] = value;
            msg_to_send_back += value;
        }
        else
        {
            msg_to_send_back += "Key does not exist";
        }
        pthread_mutex_unlock(&dictionary_key_lock[key]);
    }
    else if (tokens[1] == "concat")
    {
        int key1 = stoi(tokens[2]);
        int key2 = stoi(tokens[3]);
        int err = 0;
        string value1, value2;
        pthread_mutex_lock(&dictionary_key_lock[key1]);
        pthread_mutex_lock(&dictionary_key_lock[key2]);
        if (dictionary.find(key1) != dictionary.end())
        {
            value1 = dictionary[key1];
        }
        else
        {
            err = 1;
        }

        if (dictionary.find(key2) != dictionary.end())
        {
            value2 = dictionary[key2];
        }
        else
        {
            err = 1;
        }
        pthread_mutex_unlock(&dictionary_key_lock[key2]);
        pthread_mutex_unlock(&dictionary_key_lock[key1]);
        if (err == 0)
        {
            msg_to_send_back += value2 + value1;
            dictionary[key1] = value1 + value2;
            dictionary[key2] = value2 + value1;
        }
        else
        {
            msg_to_send_back += "Concat failed as at least one of the keys does not exist";
        }
    }
    else if (tokens[1] == "fetch")
    {
        int key = stoi(tokens[2]);
        pthread_mutex_lock(&dictionary_key_lock[key]);
        if (dictionary.find(key) != dictionary.end())
        {
            msg_to_send_back += dictionary[key];
        }
        else
        {
            msg_to_send_back += "Key does not exist";
        }
        pthread_mutex_unlock(&dictionary_key_lock[key]);
    }
    sleep(2);

    ////////////////////////////////////////
    // "If the server write a message on the socket and then close it before the client's read. Will the client be able to read the message?"
    // Yes. The client will get the data that was sent before the FIN packet that closes the socket.

    sent_to_client = send_string_on_socket(client_socket_fd, msg_to_send_back);
    // debug(sent_to_client);
    if (sent_to_client == -1)
    {
        perror("Error while writing to client. Seems socket has been closed");
        goto close_client_socket_ceremony;
    }

close_client_socket_ceremony:
    close(client_socket_fd);
    printf(BRED "Disconnected from client" ANSI_RESET "\n");
    // return NULL;
}

void *worker_routine(void *arg)
{
    while (true)
    {
        sem_wait(&queue_sem);
        pthread_mutex_lock(&queue_lock);
        int client_socket_fd = fd_queue.front();
        fd_queue.pop();
        pthread_mutex_unlock(&queue_lock);
        handle_connection(client_socket_fd);
    }
}

int main(int argc, char *argv[])
{

    int num_workers = atoi(argv[1]);
    pthread_t workers[num_workers];

    for (int i = 0; i <= 100; i++)
    {
        pthread_mutex_init(&dictionary_key_lock[i], NULL);
    }
    sem_init(&queue_sem, 0, 0);
    for (int i = 0; i < num_workers; i++)
    {
        pthread_create(&workers[i], NULL, worker_routine, NULL);
    }

    int i, j, k, t, n;

    int wel_socket_fd, client_socket_fd, port_number;
    socklen_t clilen;

    struct sockaddr_in serv_addr_obj, client_addr_obj;
    /////////////////////////////////////////////////////////////////////////
    /* create socket */
    /*
    The server program must have a special door—more precisely,
    a special socket—that welcomes some initial contact
    from a client process running on an arbitrary host
    */
    // get welcoming socket
    // get ip,port
    /////////////////////////
    wel_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (wel_socket_fd < 0)
    {
        perror("ERROR creating welcoming socket");
        exit(-1);
    }

    //////////////////////////////////////////////////////////////////////
    /* IP address can be anything (INADDR_ANY) */
    bzero((char *)&serv_addr_obj, sizeof(serv_addr_obj));
    port_number = PORT_ARG;
    serv_addr_obj.sin_family = AF_INET;
    // On the server side I understand that INADDR_ANY will bind the port to all available interfaces,
    serv_addr_obj.sin_addr.s_addr = INADDR_ANY;
    serv_addr_obj.sin_port = htons(port_number); // process specifies port

    /////////////////////////////////////////////////////////////////////////////////////////////////////////
    /* bind socket to this port number on this machine */
    /*When a socket is created with socket(2), it exists in a name space
       (address family) but has no address assigned to it.  bind() assigns
       the address specified by addr to the socket referred to by the file
       descriptor wel_sock_fd.  addrlen specifies the size, in bytes, of the
       address structure pointed to by addr.  */

    // CHECK WHY THE CASTING IS REQUIRED
    if (bind(wel_socket_fd, (struct sockaddr *)&serv_addr_obj, sizeof(serv_addr_obj)) < 0)
    {
        perror("Error on bind on welcome socket: ");
        exit(-1);
    }
    //////////////////////////////////////////////////////////////////////////////////////

    /* listen for incoming connection requests */

    listen(wel_socket_fd, MAX_CLIENTS);
    cout << "Server has started listening on the LISTEN PORT" << endl;
    clilen = sizeof(client_addr_obj);

    while (1)
    {
        /* accept a new request, create a client_socket_fd */
        /*
        During the three-way handshake, the client process knocks on the welcoming door
of the server process. When the server “hears” the knocking, it creates a new door—
more precisely, a new socket that is dedicated to that particular client.
        */
        // accept is a blocking call
        printf("Waiting for a new client to request for a connection\n");
        client_socket_fd = accept(wel_socket_fd, (struct sockaddr *)&client_addr_obj, &clilen);
        if (client_socket_fd < 0)
        {
            perror("ERROR while accept() system call occurred in SERVER");
            exit(-1);
        }

        printf(BGRN "New client connected from port number %d and IP %s \n" ANSI_RESET, ntohs(client_addr_obj.sin_port), inet_ntoa(client_addr_obj.sin_addr));

        pthread_mutex_lock(&queue_lock);
        fd_queue.push(client_socket_fd);
        pthread_mutex_unlock(&queue_lock);
        sem_post(&queue_sem);

        // handle_connection(client_socket_fd);
    }

    close(wel_socket_fd);
    return 0;
}