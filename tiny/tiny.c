/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    //getnameinfo:converts socket address structure to host and service name string
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers*/
  Rio_readinitb(&rio, fd); // system book 10.5
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if(strcasecmp(method, "GET")){ //strcasecmp는 대소문자 구분없이 비교
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);
  /* Parse URI from GET request  */
  
  is_static = parse_uri(uri, filename, cgiargs);
  if(stat(filename, &sbuf) < 0){
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  if (is_static){ /*Serve static content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)){
      //S_ISREG: 정규 파일인지 판별
      //S_IRURT: 사용자 파일을 읽을 수 있음
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size);
  }
  else{/*Serve dynamic content*/
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)){
      //S_ISREG: 정규 파일인지 판별
      //S_IXURT: 사용자가 파일을 실행할 수 있음
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
int parse_uri(char *uri, char *filename, char *cgiargs){
  char *ptr;
  if(!strstr(uri, "cgi-bin")){/*static content*/
    //strstr(str1, str2): str1에서 str2가 시작하는 시점을 찾는다, 이때 string2가 없는 경우 NULL return
    strcpy(cgiargs, "");
    //strcpy(arr, str): arr에 str을 복사
    strcpy(filename, ".");
    strcat(filename, uri);
    //strcat(arr, str): arr뒤에 str을 붙인다.
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }else{//dynamic content
    ptr = index(uri, '?');
    if (ptr){
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    }
    else{
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void serve_static(int fd, char *filename, int filesize){
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];
  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  /*파일이나 디바이스를 응용 프로그램의 주소 공간 메모리에 대응시킨다.
    1인자 => 시작 포인터 주소 (아래의 예제 참조)
    2인자 => 파일이나 주소공간의 메모리 크기
    3인자 => PROT 설정 (읽기, 쓰기, 접근권한, 실행)
    4인자 => flags는 다른 프로세스와 공유할지 안할지를 결정한다.
    5인자 => fd는 쓰거나 읽기용으로 열린 fd값을 넣어준다.
    6인자 => offset은 0으로 하던지 알아서 조절한다.*/
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

/*
 * get_filetype - Derive file type from filename
 */
void get_filetype(char *filename, char *filetype){
  if(strstr(filename, ".html")){
    strcpy(filetype, "text/html");
  }else if(strstr(filename, ".gif")){
    strcpy(filetype, "image/gif");
  }else if(strstr(filename, ".png")){
    strcpy(filetype, "image/png");
  }else if(strstr(filename, ".jpg")){
    strcpy(filetype, "image/jpg");
  }else{
    strcpy(filetype, "image/plain");
  // /* 11.7 mpg video files */
  // }else{
  //   strcpy(filetype, "video/MPG");
  // }
}
}


void serve_dynamic(int fd, char *filename, char *cgiargs){
  char buf[MAXLINE], *emptylist[] = { NULL };
  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
      if (Fork() == 0) { /* Child */
          /* Real server would set all CGI vars here */
          setenv("QUERY_STRING", cgiargs, 1);
          Dup2(fd, STDOUT_FILENO);         /* Redirect stdout to client */
          Execve(filename, emptylist, environ); /* Run CGI program */
  }
      Wait(NULL); /* Parent waits for and reaps child */
}