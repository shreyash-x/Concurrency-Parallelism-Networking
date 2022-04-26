# Concurrency, Networking and Parallelism

These code snippets are used to simulate real life problems with the help of threads, mutex , locks , semaphores and conditional variable.


## Execution instructions

### Question 1 : An alternate course alocation portal

- Compile using `gcc q1.c -lpthread -o q1`
- then run using `./q1`

### Question 2 : The Clasico Experience

- Compile using `gcc q2.c -lpthread -o q2`
- then run using `./q2`

### Question 3 : Multithreaded client and server

**Server**

- Compile using `g++ server.cpp -lpthread -o server`
- then run using `./server <number of working threads>` eg : `./server 7`

**Client**

- Compile using `g++ client.cpp -lpthread -o client`
- then run using `./client`
