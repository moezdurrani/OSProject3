#define _REENTRANT
#include <pthread.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <unistd.h> 
#include <stdlib.h>

// Buffer size
#define BUFFER 15
#define SHMKEY_BUFFER ((key_t) 2700)
#define SHMKEY_COUNTER ((key_t) 2701)


// Circular Buffer Implementation
typedef struct {
    char buffer[BUFFER];  // Circular buffer
    int in;                   // position to produce
    int out;                  // position to consume
} sharedCircularBuffer;

sharedCircularBuffer *sharedMemory; // buffer shared memory
sem_t critical;                   
sem_t empty;                   
sem_t full;
int* counter; 


// Thread for producer
void* producer(void* arg) {
    char newChar;

    // Open the file for reading
    FILE* fp = fopen("mytest.dat", "r");
    if (fp == NULL) {
        perror("Error opening file");
        pthread_exit(NULL);
    }

    // Read characters from the file and produce them
    while (fscanf(fp, "%c", &newChar) != EOF) {
        sem_wait(&empty);      // Wait for an empty slot
        sem_wait(&critical);   // Enter critical section

        // Add character to the buffer
        sharedMemory->buffer[sharedMemory->in] = newChar;
        printf("Produced: %c\n", newChar);
        sharedMemory->in = (sharedMemory->in + 1) % BUFFER; // Circular behavior

        sem_post(&critical);   // Exit critical section
        sem_post(&full);       // Signal a filled slot
    }

    // Signal the end of production with a special character
    sem_wait(&empty);
    sem_wait(&critical);

    sharedMemory->buffer[sharedMemory->in] = '*'; // Special termination character
    sharedMemory->in = (sharedMemory->in + 1) % BUFFER;

    sem_post(&critical);
    sem_post(&full);

    fclose(fp); // Close the file
    pthread_exit(NULL);

}

// Thread for consumer
void* consumer(void* arg) {
    char value;

    while (1) {
        sem_wait(&full);       // Wait for a filled slot
        sem_wait(&critical);   // Enter critical section

        // Consume the character from the buffer
        value = sharedMemory->buffer[sharedMemory->out];
        sharedMemory->out = (sharedMemory->out + 1) % BUFFER; // Circular behavior

        // Check for termination character
        if (value != '*') {
            printf("Consumed: %c\n", value);
            (*counter)++; // Increment counter
        } else {
            sem_post(&critical);
            sem_post(&empty);
            break; // Exit the loop on termination character
        }

        sem_post(&critical);   // Exit critical section
        sem_post(&empty);      // Signal an empty slot

        sleep(1); // Slow down consumer
    }

    pthread_exit(NULL);
}


int main() {

    int shmid, shmidCounter;
    pthread_t producerThread;       // process id for producer thread 
    pthread_t consumerThread;       // process id for consumer thread 
    pthread_attr_t attr;

    // Create shared memory segment
    shmid = shmget(SHMKEY_BUFFER, sizeof(sharedCircularBuffer), IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget error");
        exit(1);
    }

    sharedMemory = (sharedCircularBuffer*) shmat(shmid, NULL, 0);
    if (sharedMemory == (sharedCircularBuffer*) -1) {
        perror("shmat error");
        exit(1);
    }

    // Create shared memory segment for the counter
    shmidCounter = shmget(SHMKEY_COUNTER, sizeof(int), IPC_CREAT | 0666);
    if (shmidCounter < 0) {
        perror("shmget for counter error");
        exit(1);
    }

    counter = (int*) shmat(shmidCounter, NULL, 0);
    if (counter == (int*) -1) {
        perror("shmat for counter error");
        exit(1);
    }

    // Initialize shared memory
    sharedMemory->in = 0;
    sharedMemory->out = 0;
    *counter = 0;

    // Initialize semaphores
    sem_init(&critical, 1, 1);  // Mutex for critical section
    sem_init(&empty, 1, BUFFER); // Tracks empty slots
    sem_init(&full, 1, 0);       // Tracks filled slots

    
    // Initialize thread attributes
    pthread_attr_init(&attr);
    pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);

    // Create producer and consumer threads
    pthread_create(&producerThread, &attr, producer, NULL);
    pthread_create(&consumerThread, &attr, consumer, NULL);

    // Wait for threads to complete
    pthread_join(producerThread, NULL);
    pthread_join(consumerThread, NULL);

    // Cleanup
    sem_destroy(&critical);
    sem_destroy(&empty);
    sem_destroy(&full);

    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl error for buffer");
        exit(1);
    }

    if (shmctl(shmidCounter, IPC_RMID, NULL) == -1) {
        perror("shmctl error for counter");
        exit(1);
    }

    printf("\nTotal Characters Consumed: %d\n", *counter);
    printf("\n\t\tEnd of Simulation\n");

    return 0;
}
