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
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);   // line:netp:tiny:doit
    Close(connfd);  // line:netp:tiny:close
  }
}

//반환 값이 없고, 소켓을 받음.
void doit(int fd){
  
    // 콘텐츠가 정적인지, 동적인지.
    int is_static;
    // 구조체 선언 시스템 콜이 채워넣는 파일 메타데이터 구조체. (파일 타입,크기, 권한, 수정시간)
    struct stat sbuf;
    // 소켓에서 한 줄 읽어온 내용을 저장하는 버퍼.(GET /index.html HTTP/1.0)
    char buf[MAXLINE];
    // 메서드, uri, version 선언.
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    // filename, cgiargs(동적 요소) 
    char filename[MAXLINE], cgiargs[MAXLINE]; 
    // rio_t rio TCP가 메시지 단위가 아닌 바이트 스트림이기 때문에.
    rio_t rio;
    
    //rio를 소켓 fd에 연결된 읽기 버퍼로 초기화(받아올 데이터 fd, 바이트 크기, 해당 데이터가 들어 있는 주소)
    Rio_readinitb(&rio, fd);
    //소켓에서 한줄 읽음 (\n 기준) 어떤 fd에서 가져올지, 어떤 변수에 담으면 될지, 해당 변수의 크기는 얼마인지
    //초기화 하지 않아도 되는 이유는 \n기준으로 받아고기 때문이고, 많은 용량을 한번에 할당받아서 쓰는 이유는 속도와 단순함.
    //메모리 할당은 컴퓨터에게 꽤나 부담이 많이 가는 작업.
    Rio_readlineb(&rio, buf, MAXLINE);

    printf("Request headers:\n");
    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    // 대소문자 무시 문자열 비교 함수. 같으면 0 아니면 다른값.
    if (strcasecmp(method, "GET")) {
      clienterror(fd, method, "501", "Not implemented",
        "Tiny does not implement this method");
        return;
    }

    // 요청한 헤더 읽기
    /*
      연결 유지 여부 결정(Connection)
      keep-alive

      body 데이터 읽기 준비
      post 요청에는 body 본문 데이터가 따라옴

      신원 확인과 상태유지
      coolie나 Auth 토큰 처리

      맞춤형 데이터 제공
      다국어 지원 페이지에서 한국어페이지를 꺼내 줌.
      혹은 html이 아닌 json 데이터만 응답으로 내려주기도 함

      목적지 호스트 라우팅
      하나의 서버 컴퓨터에 두 개의 웹사이트가 같이 돌아가고 있을 수 있음(가상 호스팅)

    */
    read_requesthdrs(&rio);

    // uri 파싱
    is_static = parse_uri(uri, filename, cgiargs);

    // 파일 존재 여부 확인  stat 는 해당 파일의 메타데이터를 읽어오고 성공하면, 앞에서 선언한
    // sbuf구조체에 해당 파일의 메타데이터를 할당함.
    // stat 실패가 항상 파일이 없다는 것은 아님, 접근 권한이 없거나 파일 시스템 오류거나 경로가 비정상적일수도 있음.
    if (stat(filename, &sbuf) < 0) {
      clienterror(fd, filename, "404", "Not found",
                    "Tiny couldn't find this file");
      return;
    }

    // 콘텐츠 분기 처리
    if (is_static) {
      //권한 검사. 일반 파일인가? 기본적으로 해당 파일을 읽어서 보내기 때문, 읽기 권한이 있는가
      if(!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
        // 404는 해당 파일이 없는 것, 403은 파일은 있지만, 허용되지 않음.
        clienterror(fd, filename, "403", "Forbidden",
                    "Tiny couldn't read the file"
        );
        return;
      }
      serve_static(fd, filename, sbuf.st_size);
      // 응답 줄 전송 200ok
      // 헤더 전송
      // 파일 내용 읽어서 소켓으로 전송
      // st_size를 넘기는 건 해당 Content-Length를 작성이 가능하기 때문.
    }
    else {
      // 정적일 때는 읽어야 하므로 read권한 체크, cgi는 실행해야 하므로 execute권한 체크
      if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
        clienterror(fd, filename, "403", "Forbidden",
        "Tiny couldn't run the CGI program");
        return;
      }
      serve_dynamic(fd,filename,cgiargs);
      // 실행할 파일에 인자를 넣고 돌린 결과물을 fd에 씀. 
      // 동적 콘텐츠이므로 size를 미리 알 수 없음.
    }
}

// 해당 소켓에 원인이 되는 메시지, 상태코드, 짧은 메시지 ,긴메시지
void clienterror(int fd, char *cause, char *errnum,
  char *shortmsg, char *longmsg)
{
char buf[MAXLINE], body[MAXBUF];

//HTML 문서 본문을 문자열로 조립. 한 번에 완성하지 않고 body 뒤에 덧붙임.
// 가독성, 유지보수 보기에 깔끔하고 이해하기 쉽기에 쪼개 놓은 것이지만, c문법적으로는 낡은 방식
sprintf(body, "<html><title>Tiny Error</title>");
sprintf(body, "%s<body bgcolor=\"\"ffffff\"\">\r\n", body);
sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

/* HTTP 응답 헤더  교육용이라 여러번 쪼개서 작성해 놓은것.*/
sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
Rio_writen(fd, buf, strlen(buf));
sprintf(buf, "Content-type: text/html\r\n");
Rio_writen(fd, buf, strlen(buf));
sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
Rio_writen(fd, buf, strlen(buf));
Rio_writen(fd, body, strlen(body));
}


void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  // 첫 줄을 먼저 빼서 하는 이유는, 읽고 검사 구조를 만들기 위해.
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

// 클라이언트 요청이 정적인지, 동적인지 판단하여 static에 정수를 반환해줘야 하므로 int형 선언
int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  //정적,동적 콘텐츠 분기
  if(!strstr(uri, "cgi-bin")){
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else {
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else strcpy(cgiargs, "");

    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}


void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
    sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
    Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

    //읽어야 할 파일의 fd를 할당.
    srcfd = Open(filename, O_RDONLY, 0);
    
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
}