#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

struct {
  char *ext;
  char *filetype;
} extensions [] = {
  {"gif", "image/gif" },
  {"jpg", "image/jpg" },
  {"jpeg","image/jpeg"},
  {"png", "image/png" },
  {"ico", "image/ico" },
  {"zip", "image/zip" },
  {"gz",  "image/gz"  },
  {"tar", "image/tar" },
  {"htm", "text/html" },
  {"html","text/html" },
  {0,0} };

typedef struct
{
	int fd;
	int hit;
}webparam;

/**存放每个处理时间的数据结构**/
typedef struct  
{
	int count;
	double totalTime;
}runTime;

runTime dealRequest,readSocket,writeSocket,readHtml,writeLog;//分别为完成各个操作的时间结构
pthread_mutex_t mutex;

void logger(int type, char *s1, char *s2, int socket_fd)
{
  int fd ;
  char logbuffer[BUFSIZE*2];	
  char timebuffer[BUFSIZE];

  time_t timep;
  time(&timep);
  struct tm *p=gmtime(&timep);
  sprintf(timebuffer,"%d/%d/%d %d:%d:%d ",1900+p->tm_year,1+p->tm_mon,p->tm_mday,8+p->tm_hour,p->tm_min,p->tm_sec);
  
  switch (type) {
  case ERROR: (void)sprintf(logbuffer,"ERROR: %s:%s Errno=%d exiting pid=%d",s1, s2, errno,getpid());
    break;
  case FORBIDDEN:
    (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
    (void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
    break;
  case NOTFOUND:
    (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
    (void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
    break;
  case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
  }
  /* No checks here, nothing can be done with a failure anyway */

  //writeLog
  struct timeval logbegin;
  gettimeofday(&logbegin,NULL);
  if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
	  write(fd,timebuffer,strlen(timebuffer));
    (void)write(fd,logbuffer,strlen(logbuffer));
    (void)write(fd,"\n",1);
    (void)close(fd);
  }
  struct timeval logend;
	gettimeofday(&logend,NULL);
  pthread_mutex_lock(&mutex);
  writeLog.totalTime+=(logend.tv_sec-logbegin.tv_sec)*1000+(logend.tv_usec-logbegin.tv_usec)/1000;
  writeLog.count++;
  pthread_mutex_unlock(&mutex);
}

void printResult(double totaltimes)
{
	int sum=dealRequest.count;
	printf("\n共用%fs成功处理%d个客户端请求，其中\n",totaltimes,dealRequest.count);
	printf("客户端请求处理总时间%fms;\n客户端完成读Socket总时间%fms;\n客户端完成写Socket总时间%fms;\n客户端完成读网页数据总时间%fms;\n客户端完成写日志数据总时间%fms;\n",dealRequest.totalTime,readSocket.totalTime,writeSocket.totalTime,readHtml.totalTime,writeLog.totalTime);
	printf("平均每个客户端完成请求处理时间为%fms;\n平均每个客户端完成读Socket时间为%fms;读Socket次数为:%d;\n平均每个客户端完成写Socket时间为%fms;写Socket次数为%d\n平均每个客户端完成读网页数据时间为%fms;读网页数据次数为%d\n平均每个客户端完成写日志数据时间为%fms;写日志次数为%d\n",(dealRequest.totalTime/sum),(readSocket.totalTime/sum),readSocket.count,(writeSocket.totalTime/sum),writeSocket.count,(readHtml.totalTime/sum),readHtml.count,(writeLog.totalTime/sum),writeLog.count);
}
/* this is a child web server process, so we can exit on errors */
void* web(void* data)
{
 
  struct timeval webbegin;
	gettimeofday(&webbegin,NULL);
	
  int fd;
  int hit;

  int j,file_fd,buflen;
  long i,ret,len;
  char* fstr;
  char buffer[BUFSIZE+1];
  webparam *param=(webparam*)data;
  fd=param->fd;
  hit=param->hit;
  
  //read
  struct timeval readbegin;
	gettimeofday(&readbegin,NULL);
  ret=read(fd,buffer,BUFSIZE);
  struct timeval readend;
	gettimeofday(&readend,NULL);
  pthread_mutex_lock(&mutex);
  readSocket.totalTime+=(readend.tv_sec-readbegin.tv_sec)*1000+(readend.tv_usec-readbegin.tv_usec)/1000;
  readSocket.count++;
  pthread_mutex_unlock(&mutex);
  //read
  
  if(ret == 0 || ret == -1) {  /* read failure stop now */
    logger(FORBIDDEN,"failed to read browser request","",fd);
  }
  
  if(ret > 0 && ret < BUFSIZE)  /* return code is valid chars */
    buffer[ret]=0;    /* terminate the buffer */
  else buffer[0]=0;
  for(i=0;i<ret;i++)  /* remove CF and LF characters */
    if(buffer[i] == '\r' || buffer[i] == '\n')
      buffer[i]='*';
  logger(LOG,"request",buffer,hit);
  if( strncmp(buffer,"GET ",4) && strncmp(buffer,"get ",4) ) {
    logger(FORBIDDEN,"Only simple GET operation supported",buffer,fd);
  }
  for(i=4;i<BUFSIZE;i++) { /* null terminate after the second space to ignore extra stuff */
    if(buffer[i] == ' ') { /* string is "GET URL " +lots of other stuff */
      buffer[i] = 0;
      break;
    }
  }
  for(j=0;j<i-1;j++)   /* check for illegal parent directory use .. */
    if(buffer[j] == '.' && buffer[j+1] == '.') {
      logger(FORBIDDEN,"Parent directory (..) path names not supported",buffer,fd);
    }
  if( !strncmp(&buffer[0],"GET /\0",6) || !strncmp(&buffer[0],"get /\0",6) ) /* convert no filename to index file */
    (void)strcpy(buffer,"GET /index.html");

  /* work out the file type and check we support it */
  buflen=strlen(buffer);
  fstr = (char *)0;
  for(i=0;extensions[i].ext != 0;i++) {
    len = strlen(extensions[i].ext);
    if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
      fstr =extensions[i].filetype;
      break;
    }
  }
  if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);
  
  
  if(( file_fd = open(&buffer[5],O_RDONLY)) == -1) {  /* open the file for reading */
    logger(NOTFOUND, "failed to open file",&buffer[5],fd);
  }

  logger(LOG,"SEND",&buffer[5],hit);

  //readFile
	struct timeval readhtmlbegin;
	gettimeofday(&readhtmlbegin,NULL);
  len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* lseek to the file end to find the length */
        (void)lseek(file_fd, (off_t)0, SEEK_SET); /* lseek back to the file start ready for reading */
          (void)sprintf(buffer,"HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr); /* Header + a blank line */
	struct timeval readhtmlend;
	gettimeofday(&readhtmlend,NULL);
  pthread_mutex_lock(&mutex);
  readHtml.totalTime+=(readhtmlend.tv_sec-readhtmlbegin.tv_sec)*1000+(readhtmlend.tv_usec-readhtmlbegin.tv_usec)/1000;
  readHtml.count++;
  pthread_mutex_unlock(&mutex);
  //readFile
  logger(LOG,"Header",buffer,hit);

  //write
  struct timeval writebegin;
	gettimeofday(&writebegin,NULL);
  (void)write(fd,buffer,strlen(buffer));
  /* send file in 8KB block - last block may be smaller */
  //insidebegin=clock();
  while (  (ret = read(file_fd, buffer, BUFSIZE)) > 0 ) {
    (void)write(fd,buffer,ret);
  }
  struct timeval writeend;
  gettimeofday(&writeend,NULL);
  pthread_mutex_lock(&mutex);
  writeSocket.totalTime+=(writeend.tv_sec-writebegin.tv_sec)*1000+(writeend.tv_usec-writebegin.tv_usec)/1000;
  writeSocket.count++;
  pthread_mutex_unlock(&mutex);
  //write

  usleep(10000);  /* allow socket to drain before signalling the socket is closed */
  close(file_fd);

  struct timeval webend;
  gettimeofday(&webend,NULL);
  pthread_mutex_lock(&mutex);
  dealRequest.totalTime+=(webend.tv_sec-webbegin.tv_sec)*1000+(webend.tv_usec-webbegin.tv_usec)/1000;
  dealRequest.count++;
  pthread_mutex_unlock(&mutex);

  close(fd);
  free(param);
}

int main(int argc, char **argv)
{
  int i, port, pid, listenfd, socketfd, hit;
  socklen_t length;
  static struct sockaddr_in cli_addr; /* static = initialised to zeros */
  static struct sockaddr_in serv_addr; /* static = initialised to zeros */

  if( argc < 3  || argc > 3 || !strcmp(argv[1], "-?") ) {
    (void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
  "\tnweb is a small and very safe mini web server\n"
  "\tnweb only servers out file/web pages with extensions named below\n"
  "\t and only from the named directory or its sub-directories.\n"
  "\tThere is no fancy features = safe and secure.\n\n"
  "\tExample: nweb 8181 /home/nwebdir &\n\n"
  "\tOnly Supports:", VERSION);
    for(i=0;extensions[i].ext != 0;i++)
      (void)printf(" %s",extensions[i].ext);

    (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
  "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
  "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n"  );
    exit(0);
  }
  if( !strncmp(argv[2],"/"   ,2 ) || !strncmp(argv[2],"/etc", 5 ) ||
      !strncmp(argv[2],"/bin",5 ) || !strncmp(argv[2],"/lib", 5 ) ||
      !strncmp(argv[2],"/tmp",5 ) || !strncmp(argv[2],"/usr", 5 ) ||
      !strncmp(argv[2],"/dev",5 ) || !strncmp(argv[2],"/sbin",6) ){
    (void)printf("ERROR: Bad top directory %s, see nweb -?\n",argv[2]);
    exit(3);
  }
  if(chdir(argv[2]) == -1){
    (void)printf("ERROR: Can't Change to directory %s\n",argv[2]);
    exit(4);
  }
  /* Become deamon + unstopable and no zombies children (= no wait()) */
  if(fork()!=0)
	  return 0;
  (void)signal(SIGCLD,SIG_IGN);
  (void)signal(SIGHUP,SIG_IGN);
 
  logger(LOG,"nweb starting",argv[1],getpid());
  /* setup the network socket */
  if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
    logger(ERROR, "system call","socket",0);
  port = atoi(argv[1]);


  if(port < 0 || port >60000)
    logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);
  
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
  pthread_t pth;
  
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(port);

  clock_t begin=clock();
  if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
    logger(ERROR,"system call","bind",0);
  clock_t end=clock();
  printf("bind:%fs \n",(double)(end-begin)/((clock_t)1000));

  if( listen(listenfd,64) <0)
    logger(ERROR,"system call","listen",0);

  double runtime=0;
  pthread_mutex_init(&mutex,NULL);
  struct timeval tvbegan;
  for(hit=1;;hit++) 
  {
    struct timeval tvended;
    
    length = sizeof(cli_addr);

    if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
      logger(ERROR,"system call","accept",0);

    else 
    {
    	if(hit==1)gettimeofday(&tvbegan,NULL);
    	webparam *param=malloc(sizeof(webparam));
    	param->hit=hit;
    	param->fd=socketfd;
    	if(pthread_create(&pth,&attr,web,(void*)param)<0)
		     logger(ERROR,"system call","pthread_create",0);
    }
    gettimeofday(&tvended,NULL);
    runtime=tvended.tv_sec-tvbegan.tv_sec;
    //if(runtime>=20)break;
   }
   printResult(runtime);
   pthread_mutex_destroy(&mutex);  
}
