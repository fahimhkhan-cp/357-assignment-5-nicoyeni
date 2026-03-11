#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>

void sigchld_handler(int s) {
   while (waitpid(-1, NULL, WNOHANG) > 0);
}

void send_error(FILE *out, int code, const char *msg){
   char body[256];
   snprintf(body, sizeof(body), "<html><body><h1>%d %s</h1></body></html>", code, msg);
   int len = strlen(body);

   fprintf(out, "HTTP/1.0 %d %s\r\n", code, msg);
   fprintf(out, "Content-Type: text/html\r\n");
   fprintf(out, "Content-Length: %d\r\n", len);
   fprintf(out, "\r\n");
   fprintf(out, "%s", body);
   fflush(out);
}

int has_dotdot(const char *path){
   if(strstr(path, "/../") != NULL) return 1;
   if(strncmp(path, "../", 3) == 0) return 1;
   if(strlen(path) >= 3 && strcmp(path + strlen(path) - 3, "/..") == 0) return 1;
   if(strcmp(path, "..") == 0) return 1;
   return 0;
}

void handle_request(int nfd)
{
   FILE *network_in = fdopen(nfd, "r");
   FILE *network_out = fdopen(dup(nfd), "w");
   char *line = NULL;
   size_t size;
   ssize_t num;

   if (network_in == NULL || network_out == NULL)
   {
      perror("fdopen");
      close(nfd);
      return;
   }

   if ((num = getline(&line, &size, network_in)) > 0)
   {
      char method[16];
      char path[1024];
      char version[16];
      int parsed = sscanf(line, "%s %s %s", method, path, version);
      if(parsed < 2){
         send_error(network_out, 400, "Bad Request");
         free(line);
         fclose(network_in);
         fclose(network_out);
         return;
      }
      while(getline(&line, &size, network_in) > 0){
         if(strcmp(line, "\r\n") == 0 || strcmp(line, "\n") == 0){
            break;
         }
      }

      if(has_dotdot(path)){
         send_error(network_out, 403, "Permission Denied");
         free(line);
         fclose(network_in);
         fclose(network_out);
         return;
      }

      if(strcmp(method, "GET") == 0){
         char body[256];
         snprintf(body, sizeof(body), "<html><body><h1>%s</h1></body></html>", msg);
         int len = strlen(body);

         fprintf(network_out, "HTTP/1.0 200 OK\r\n");
         fprintf(network_out, "Content-Type: text/html\r\n");
         fprintf(network_out, "Content-Length: %d\r\n", len);
         fprintf(network_out, "\r\n");
         fprintf(network_out, "%s", body);
         fflush(network_out);

      } else if(strcmp(method, "HEAD") == 0){

      } else{
         send_error(network_out, 501, "Not Implemented");
         free(line);
         fclose(network_in);
         fclose(network_out);
         return;
      }
   }

   free(line);
   fclose(network_in);
   fclose(network_out);
}

void run_service(int fd)
{
   while (1)
   {
      int nfd = accept_connection(fd);
      if (nfd != -1)
      {
         pid_t pid = fork();
         if(pid < 0){
            perror("fork");
            close(nfd);
            continue;            
         } else if(pid == 0){
            close(fd);
            printf("Connection established\n");
            handle_request(nfd);
            printf("Connection closed\n");
            exit(0);
         } else{
            close(nfd);
            continue;
         }
         
      }
   }
}

int main(int argc, char *argv[])
{
   if(argc != 2){
      printf("invalid command-line args");
      exit(1);
   }

   int port = atoi(argv[1]);
   if(port <= 0){
      printf("invalid port number");
      exit(1);
   }
   int fd = create_service(port);

   if (fd == -1)
   {
      perror("creat_service");
      exit(1);
   }

   struct sigaction sa;
   sa.sa_handler = sigchld_handler;
   sigemptyset(&sa.sa_mask);
   sa.sa_flags = SA_RESTART;
   sigaction(SIGCHLD, &sa, NULL);

   printf("listening on port: %d\n", port);
   run_service(fd);
   close(fd);

   return 0;
}
