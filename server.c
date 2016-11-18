#include "cs537.h"
#include "request.h"
#include <pthread.h>

//
// server.c: A very, very simple web server
//
// To run:
//  server <portnum (above 2000)>
//
// Repeatedly handles HTTP requests sent to this port number.
// Most of the work is done within routines written in request.c
//

// CS537: Parse the new arguments too
typedef struct connection_information {
  int conn_fd; // connection file descriptor.
  int in_use;
} conn_info_t;

typedef struct shared_buffer {
  pthread_mutex_t lock;
  conn_info_t *buffer;
  int fill_count;
} shared_buf_t;

int port = 0, n_threads = 0, n_buffers = 0;
/* All these threads are dynamically allocated */
pthread_t **cnsmr_threads;
pthread_mutex_t main_lock;
shared_buf_t shared_buffer;
pthread_cond_t full, empty;

int put_buffer() {
  int i = 0, index = -1;
  pthread_mutex_lock(&shared_buffer.lock);
    for (i = 0; i < n_buffers; ++i) {
        if (shared_buffer.buffer[i].in_use == 0) {
          shared_buffer.buffer[i].in_use = 1;
          shared_buffer.fill_count++;
          index = i;
          break;
        }
    }
  pthread_mutex_unlock(&shared_buffer.lock);
  return index;
}

int get_buffer() {
  int index = -1, i = 0;
  pthread_mutex_lock(&shared_buffer.lock);
    for (i = 0; i < n_buffers; ++i) {
        if (shared_buffer.buffer[i].in_use == 1) {
          shared_buffer.buffer[i].in_use = 0;
          shared_buffer.fill_count--;
          index = i;
          break;
        }
    }
  pthread_mutex_unlock(&shared_buffer.lock);
  return index;
}

void* connection_consumer(void *arg) {
  int i = 0;
  while (1) {
    pthread_mutex_lock(&main_lock);
    {
      while (shared_buffer.fill_count == 0) {
        pthread_cond_wait(&full, &main_lock);
      }
      i = get_buffer();

      pthread_cond_signal(&empty);
    }
    pthread_mutex_unlock(&main_lock);

    if (i == -1) {
      perror("Invalid state of Producer-Consumer relationship ! i = -1");
      exit(1);
    }
    printf("Thread %p consumed buffer %d\n", arg, i);
    requestHandle(shared_buffer.buffer[i].conn_fd);
    Close(shared_buffer.buffer[i].conn_fd);
  }
}

void getargs(int argc, char *argv[], int *port, int *n_threads, int *n_buffers)
{
    if (argc != 4) {
      fprintf(stderr, "Usage: %s <port> <threads> <buffers>\n", argv[0]);
      exit(1);
    }
    *port = atoi(argv[1]);
    *n_threads = atoi(argv[2]);
    *n_buffers = atoi(argv[3]);

    if (!(*port && *n_threads && *n_buffers)) {
      fprintf(stderr, "Usage: %s <port> <threads> <buffers>\n", argv[0]);
      exit(1);
    }
}

int main(int argc, char *argv[])
{
    int listenfd, connfd, clientlen;
    struct sockaddr_in clientaddr;
    int i = 0;

    getargs(argc, argv, &port, &n_threads, &n_buffers);

    pthread_cond_init(&empty, NULL);
    pthread_cond_init(&full, NULL);

    pthread_mutex_init(&main_lock, NULL);

    cnsmr_threads = (pthread_t **) malloc(sizeof(pthread_t *) * n_threads);
    if (!cnsmr_threads) {
      goto exit;
    }

    shared_buffer.buffer = (conn_info_t *)
      malloc(sizeof(conn_info_t) * n_buffers);
    if (!shared_buffer.buffer)
      goto buffer_error;

    pthread_mutex_init(&shared_buffer.lock, NULL);
    for (i = 0; i < n_buffers; ++i) {
      shared_buffer.buffer[i].conn_fd = -1;
      shared_buffer.buffer[i].in_use = 0;
    }

    for (i = 0; i < n_threads; ++i) {
      cnsmr_threads[i] = (pthread_t *) malloc(sizeof(pthread_t));
      if (!cnsmr_threads[i]) {
        pthread_create(cnsmr_threads[i], NULL, connection_consumer, (void *) i);
      } else {
        goto free_cnsmr_threads;
      }
    }

    listenfd = Open_listenfd(port);
    while (1) {
      int i = 0;
      clientlen = sizeof(clientaddr);
      connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);

      //
      // CS537: In general, don't handle the request in the main thread.
      // Save the relevant info in a buffer and have one of the worker threads
      // do the work.
      //
      // requestHandle(connfd);
      pthread_mutex_lock(&main_lock);
      {
        while (shared_buffer.fill_count == n_buffers) {
          pthread_cond_wait(&empty, &main_lock);
        }
        i = put_buffer();
        if (i == -1) {
          perror("Invalid state of Producer-Consumer relationship ! i = %d\n");
          exit(1);
        }

        shared_buffer.buffer[i].conn_fd = connfd;
        printf("main Thread produced buffer %d\n", i);
        
        pthread_cond_signal(&full);
      }
      pthread_mutex_unlock(&main_lock);
    }
    return 0;

free_cnsmr_threads:
    i--;
    for (; i>= 0; --i) {
      free(cnsmr_threads[i]);
    }
    free(cnsmr_threads);
buffer_error:
    for (i = 0; i < n_threads; ++i) {
      free(cnsmr_threads[i]);
    }
exit:
    return 1;
}





