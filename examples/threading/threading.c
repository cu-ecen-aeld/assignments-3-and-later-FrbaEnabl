#include "threading.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>

// Optional: use these functions to add debug or error prints to your application
#define DEBUG_LOG(msg,...)
//#define DEBUG_LOG(msg,...) printf("threading: " msg "\n" , ##__VA_ARGS__)
#define ERROR_LOG(msg,...) printf("threading ERROR: " msg "\n" , ##__VA_ARGS__)

void wait_ms(long milliseconds) {
    struct timespec req;
    req.tv_sec = milliseconds / 1000;  // Convert milliseconds to seconds
    req.tv_nsec = (milliseconds % 1000) * 1000000L;  // Convert remaining milliseconds to nanoseconds

    nanosleep(&req, NULL);
}

void* threadfunc(void* thread_param)
{
    // Dynamically allocate memory for a struct thread_data
    struct thread_data* thread_dat_ptr = (struct thread_data *) thread_param;
    if (thread_dat_ptr == NULL) {
        fprintf(stderr, "Error allocating memory for thread_data\n");
        return NULL;
    }

    if (pthread_mutex_init(&(thread_dat_ptr->mutex), NULL) != 0) {
        // Error handling for mutex initialization failure
        fprintf(stderr, "Error initializing mutex\n");
        return NULL;
    }

    wait_ms(thread_dat_ptr->wait_to_obtain_ms);
    if (pthread_mutex_lock(&(thread_dat_ptr->mutex)) != 0) {
        fprintf(stderr, "Error locking mutex\n");
        return NULL;
    }
    wait_ms(thread_dat_ptr->wait_to_release_ms);
    if (pthread_mutex_unlock(&(thread_dat_ptr->mutex)) != 0) {
        fprintf(stderr, "Error unlocking mutex\n");
        return NULL;
    }
    
    // TODO: wait, obtain mutex, wait, release mutex as described by thread_data structure
    // hint: use a cast like the one below to obtain thread arguments from your parameter
    //struct thread_data* thread_func_args = (struct thread_data *) thread_param;

    // Destroy the mutex and free memory when done
    pthread_mutex_destroy(&(thread_dat_ptr->mutex));
    free(thread_dat_ptr);
    return thread_param;
}


bool start_thread_obtaining_mutex(pthread_t *thread, pthread_mutex_t *mutex,int wait_to_obtain_ms, int wait_to_release_ms)
{
    /**
     * TODO: allocate memory for thread_data, setup mutex and wait arguments, pass thread_data to created thread
     * using threadfunc() as entry point.
    */
    struct thread_data* thread_dat_ptr = (struct thread_data*)malloc(sizeof(struct thread_data));
    thread_dat_ptr->wait_to_obtain_ms = (long)wait_to_obtain_ms;
    thread_dat_ptr->wait_to_release_ms = (long)wait_to_release_ms;
    thread_dat_ptr->thread_complete_success = 0;
    thread_dat_ptr->mutex = mutex;
    if (pthread_mutex_init(&(thread_dat_ptr->mutex), NULL) != 0) {
        // Error handling for mutex initialization failure
        fprintf(stderr, "Error initializing mutex\n");
        return false;
    }
    int rc = pthread_create(thread, NULL, threadfunc, thread_dat_ptr);
    /*
     *
     * return true if successful.
     * See implementation details in threading.h file comment block
     */
    if (rc == 0) {
        pthread_join(my_thread, NULL); // Optionally join the thread if needed.
        return true;
    }
    return false;
}

