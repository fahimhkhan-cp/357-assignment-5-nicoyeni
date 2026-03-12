#define _GNU_SOURCE
#include "net.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>

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

void handle_cgi(FILE *out, const char *method, const char *path){
   char *line = strdup(path + 1);
   char *args[100];
   int i = 0;
   char *token = strtok(line, "?");

   if (token == NULL) {
      send_error(out, 400, "Bad Request");
      free(line);
      return;
   }

   while (token != NULL) {
      args[i++] = token;
      token = strtok(NULL, "&");
   }
   args[i] = NULL;

   char tmpfile[64];
   snprintf(tmpfile, sizeof(tmpfile), "/tmp/cgi_%d.txt", getpid());
   pid_t pid = fork();
   if(pid < 0){
      perror("fork");
      send_error(out, 500, "Internal Error");
      free(line);
      return;
   }else if(pid == 0){
      int fd = open(tmpfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
      if (fd >= 0) {
         dup2(fd, STDOUT_FILENO);
         close(fd);
      }
      execv(args[0], args);
      perror("exec");
      exit(1);
   } else{
      waitpid(pid, NULL, 0);
   }
   
   struct stat st;
   if(stat(tmpfile, &st) == -1){
      send_error(out, 500, "Internal Error");
      unlink(tmpfile);
      free(line);
      return;
   }
   
   fprintf(out, "HTTP/1.0 200 OK\r\n");
   fprintf(out, "Content-Type: text/html\r\n");
   fprintf(out, "Content-Length: %ld\r\n", (long)st.st_size);
   fprintf(out, "\r\n");

   if(strcmp(method, "GET") == 0){
      FILE *f = fopen(tmpfile, "r");
      if (f != NULL) {
         char buf[4096];
         size_t num;
         while((num = fread(buf, 1, sizeof(buf), f)) > 0){
            fwrite(buf, 1, num, out);
         }
         fclose(f);
      }
   } 
   fflush(out);

   unlink(tmpfile);
   free(line);
}

void handle_file(FILE *out, const char *method, const char *path){
   const char *filepath = path + 1;

   struct stat st;
   if(stat(filepath, &st) == -1){
      send_error(out, 404, "Not Found");
      return;
   }

   if(S_ISDIR(st.st_mode)){
      send_error(out, 403, "Permission Denied");
      return;
   }

   if (!(st.st_mode & S_IRUSR)){
      send_error(out, 403, "Permission Denied");
      return;
   }

   fprintf(out, "HTTP/1.0 200 OK\r\n");
   fprintf(out, "Content-Type: text/html\r\n");
   fprintf(out, "Content-Length: %ld\r\n", (long)st.st_size);
   fprintf(out, "\r\n");

   if(strcmp(method, "GET") == 0){
      FILE *f = fopen(filepath, "r");
      if (f == NULL) return;
      char buf[4096];
      size_t num;
      while((num = fread(buf, 1, sizeof(buf), f)) > 0){
         fwrite(buf, 1, num, out);
      }
      fclose(f);
   } else if(strcmp(method, "HEAD") == 0){
   } else{
      send_error(out, 501, "Not Implemented");
      return;
   }
   fflush(out);
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
      
      int parsed = sscanf(line, "%15s %1023s %15s", method, path, version);
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

      if(strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0){
         send_error(network_out, 501, "Not Implemented");
      } else if(strncmp(path, "/cgi-like/", 10) == 0){
         handle_cgi(network_out, method, path);
      } else {
         handle_file(network_out, method, path);
      }
   }

   char dummy[1024];
   while (fread(dummy, 1, sizeof(dummy), network_in) > 0) {
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
            
            struct sigaction sa;
            sa.sa_handler = SIG_DFL;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            sigaction(SIGCHLD, &sa, NULL);

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
      fprintf(stderr, "Usage: %s <port>\n", argv[0]);
      exit(1);
   }

   int port = atoi(argv[1]);
   if(port <= 0){
      fprintf(stderr, "invalid port number\n");
      exit(1);
   }
   
   signal(SIGPIPE, SIG_IGN);

   int fd = create_service(port);
   if (fd == -1)
   {
      perror("create_service");
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
