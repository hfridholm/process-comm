#include "debug.h"
#include "socket.h"
#include "thread.h"

#include <stdlib.h>
#include <signal.h>
#include <pthread.h>

pthread_t stdinThread;
pthread_t stdoutThread;

bool stdinThreadRunning = false;
bool stdoutThreadRunning = false;

int serverfd = -1;
int sockfd = -1;

// Settings
bool debug = false;

char address[64] = "127.0.0.1";
int port = 5555;

void* stdout_routine(void* arg)
{
  stdoutThreadRunning = true;

  if(debug) info_print("Redirecting socket -> stdout");

  char buffer[1024];
  memset(buffer, '\0', sizeof(buffer));

  int status;

  while((status = socket_read(sockfd, buffer, sizeof(buffer))) > 0)
  {
    fputs(buffer, stdout);

    memset(buffer, '\0', sizeof(buffer));
  }
  if(debug) info_print("Stopped socket -> stdout");

  // If stdin thread has interrupted stdout thread
  if(status == -1 && errno == EINTR)
  {
    if(debug) info_print("stdout routine interrupted"); 
  }

  // Interrupt stdin thread
  pthread_kill(stdinThread, SIGUSR1);

  stdoutThreadRunning = false;

  return NULL;
}

void* stdin_routine(void* arg)
{
  stdinThreadRunning = true;

  if(debug) info_print("Redirecting stdin -> socket");

  char buffer[1024];
  memset(buffer, '\0', sizeof(buffer));

  while(fgets(buffer, sizeof(buffer), stdin) != NULL)
  {
    if(socket_write(sockfd, buffer, sizeof(buffer)) == -1) break;
  
    memset(buffer, '\0', sizeof(buffer));
  }
  if(debug) info_print("Stopped stdin -> socket");

  // If stdout thread has interrupted stdin thread
  if(errno == EINTR)
  {
    if(debug) info_print("stdin routine interrupted"); 
  }

  // Interrupt stdout thread
  pthread_kill(stdoutThread, SIGUSR1);

  stdinThreadRunning = false;

  return NULL;
}

/*
 * Keyboard interrupt - close the program (the threads)
 */
void sigint_handler(int signum)
{
  if(debug) info_print("Keyboard interrupt");

  if(stdinThreadRunning) pthread_kill(stdinThread, SIGUSR1);
  if(stdoutThreadRunning) pthread_kill(stdoutThread, SIGUSR1);
}

void sigint_handler_setup(void)
{
  struct sigaction sigAction;

  sigAction.sa_handler = sigint_handler;
  sigAction.sa_flags = 0;
  sigemptyset(&sigAction.sa_mask);

  sigaction(SIGINT, &sigAction, NULL);
}

void sigusr1_handler(int signum) {}

void sigusr1_handler_setup(void)
{
  struct sigaction sigAction;

  sigAction.sa_handler = sigusr1_handler;
  sigAction.sa_flags = 0;
  sigemptyset(&sigAction.sa_mask);

  sigaction(SIGUSR1, &sigAction, NULL);
}

void signals_handler_setup(void)
{
  signal(SIGPIPE, SIG_IGN); // Ignores SIGPIPE
  
  sigint_handler_setup();

  sigusr1_handler_setup();
}

/*
 * Accept socket client
 *
 * RETURN
 * - 0 | Success!
 * - 1 | Failed to accept client socket
 * - 2 | Failed to start stdin and stdout threads
 */
int server_process_step2(void)
{
  sockfd = socket_accept(serverfd, address, port, debug);

  if(sockfd == -1) return 1;

  int status = stdin_stdout_thread_start(&stdinThread, &stdin_routine, &stdoutThread, &stdout_routine, debug);

  socket_close(&sockfd, debug);

  return (status != 0) ? 2 : 0;
}

/*
 * Create server socket
 *
 * RETURN
 * - 0 | Success!
 * - 1 | Failed to create server socket
 * - 2 | Failed server_process_step2
 */
int server_process(void)
{
  serverfd = server_socket_create(address, port, 1, debug);

  if(serverfd == -1) return 1;

  int status = server_process_step2();

  socket_close(&serverfd, debug);

  return (status != 0) ? 2 : 0;
}

/*
 * Create client socket
 *
 * RETURN
 * - 0 | Success!
 * - 1 | Failed to create client socket
 * - 2 | Failed to start stdin and stdout threads
 */
int client_process(void)
{
  sockfd = client_socket_create(address, port, debug);

  if(sockfd == -1) return 1;

  int status = stdin_stdout_thread_start(&stdinThread, &stdin_routine, &stdoutThread, &stdout_routine, debug);

  socket_close(&sockfd, debug);

  return (status != 0) ? 2 : 0;
}

/*
 * Parse the current passed flag
 *
 * FLAGS
 * --debug             | Output debug messages
 * --address=<address> | The server address
 * --port=<port>       | The server port
 */
void flag_parse(char flag[])
{
  if(!strcmp(flag, "--debug"))
  {
    debug = true;
  }
  else if(!strncmp(flag, "--address=", 10))
  {
    strcpy(address, flag + 10);
  }
  else if(!strncmp(flag, "--port=", 7))
  {
    port = atoi(flag + 7);
  }
}

/*
 * Parse every passed flag
 */
void flags_parse(int argc, char* argv[])
{
  for(int index = 1; index < argc; index += 1)
  {
    flag_parse(argv[index]);
  }
}

int main(int argc, char* argv[])
{
  flags_parse(argc, argv);

  signals_handler_setup();

  if(argc >= 2 && !strcmp(argv[1], "server"))
  {
    return server_process();
  }
  else return client_process();
}
