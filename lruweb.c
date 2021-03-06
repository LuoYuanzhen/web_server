#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <fcntl.h>
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
#include <pthread.h>
#include <sys/prctl.h>
#include <stdbool.h>

#define VERSION 23
#define BUFSIZE 80960
#define ERROR      42
#define LOG        44
#define FORBIDDEN 403
#define NOTFOUND  404
#define BUCKET_NUM 50
#define QUEUE_NUM 10000

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

typedef struct statistics{
    pthread_mutex_t rwmutex;
    int visit;//网页访问总次数
    int hit;//在lru队列中命中次数
    int lose;//在lru队列中缺失次数
}statistics;

statistics* mystat;

statistics* initStatistics()
{
    statistics* newstat=(statistics*)malloc(sizeof(statistics));
    newstat->visit=0;
    newstat->hit=0;
    newstat->lose=0;
    pthread_mutex_init(&(newstat->rwmutex),NULL);
    return newstat;
}

void reportStatistics(statistics* s)
{
    pthread_mutex_lock(&(s->rwmutex));
    float hit=s->hit;
    float visit=s->visit;
    float percent=hit/visit*100;
    printf("report:\n");
    printf("  打开网页总个数:%d\n",s->visit);
    printf("  置换次数:%d\n",s->lose);
    printf("  命中次数:%d\n",s->hit);
    printf("  命中率:%f/%\n",percent);
    pthread_mutex_unlock(&(s->rwmutex));
}
/** Hash **/
#ifdef HASHTHREAD
#include<pthread.h>
	#include<semaphore.h>
#endif

typedef struct content{
    int length;
    void* address;
}content;

typedef struct hashpair{
    char* key;
    content* cont;
    struct hashpair* next;
}hashpair;

typedef struct hashtable{
    hashpair** bucket;
    int num_bucket;
#ifdef HASHTHREAD
    volatile int* locks;
		volatile int lock;
#endif
}hashtable;

hashtable* table;

static inline long int hashString(char * str)
{
    long hash = 5381;
    int c;

    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash>0?hash:-hash;
}

static inline char * copystring(char * value)
{
    char * copy = (char *)malloc(strlen(value)+1);
    if(!copy) {
        printf("Unable to allocate string value %s\n",value);
        abort();
    }
    strcpy(copy,value);
    return copy;
}

static inline int isEqualContent(content* cont1,content* cont2)
{
    if(cont1->length!=cont2->length)
        return 0;
    if(cont1->address!=cont2->address)
        return 0;
    return 1;
}

hashtable* createHashTable(int num_bucket)
{
    hashtable* table=(hashtable*)malloc(sizeof(hashtable));
    if(NULL==table)
        return NULL;
    table->bucket=(hashpair**)malloc(num_bucket*sizeof(void*));
    if(!table->bucket)
    {
        free(table);
        return NULL;
    }
    memset(table->bucket,0,num_bucket*sizeof(int));
    table->num_bucket=num_bucket;
#ifdef HASHTHREAD
    table->locks=(int*)malloc(num_bucket*sizeof(int));
	if(!table->locks)
	{
		free(table);
		return NULL;
	}
	memset((int*)&table->locks[0],0,num_bucket*sizeof(int));
#endif
    return table;
}

void freeHashTable(hashtable* table)
{
    if(table==NULL)
        return;
    hashpair* next;
    for(int i=0;i<table->num_bucket;i++)
    {
        hashpair* pair=table->bucket[i];
        while(pair)
        {
            next=pair->next;
            free(pair->key);
            free(pair->cont->address);
            free(pair->cont);
            free(pair);
            pair=next;
        }
    }
    free(table->bucket);
#ifdef HASHTHREAD
    free(table->locks);
#endif
    free(table);
}

static inline char * copystring(char * value);
int delItem(hashtable* table,char* key);

typedef struct queue
{
    pthread_mutex_t mutex;//互斥地访问队列
    hashpair* top;
    hashpair* tail;
    int len;
}queue;

typedef struct LRU//存放所有的lru队列
{
    queue** lruqueues;
    int len;
}LRU;

LRU* lru;

LRU* initLRU(int num_bucket)
{
    LRU* lru1=malloc(sizeof(LRU));
    lru1->lruqueues=(queue**)malloc(num_bucket*sizeof(queue*));
    for(int i=0;i<num_bucket;i++)
    {
        lru1->lruqueues[i]=(queue*)malloc(sizeof(queue));
        lru1->lruqueues[i]->len=0;
        lru1->lruqueues[i]->top=NULL;
        lru1->lruqueues[i]->tail=NULL;
        pthread_mutex_init(&(lru1->lruqueues[i]->mutex),NULL);
    }
    return lru1;
}

void updateLruQueue(queue* q,char* key)
{
    //pthread_mutex_lock(&(q->mutex));

    hashpair* pair=q->top;
    hashpair* prev=NULL;
    while(pair!=NULL)
    {
        if(0==strcmp(pair->key,key))
        {
            if(pair==q->tail)
                return;
            if(prev!=NULL)
                prev->next=pair->next;
            else q->top=pair->next;
            pair->next=NULL;
            q->tail->next=pair;
            q->tail=pair;

            pthread_mutex_lock(&(mystat->rwmutex));
            mystat->hit++;
            pthread_mutex_unlock(&(mystat->rwmutex));
            return;
        }
        prev=pair;
        pair=pair->next;
    }

    //pthread_mutex_unlock(&(q->mutex));
}
void addToQueue(queue* q,char* key,content* cont)
{
    //pthread_mutex_lock(&(q->mutex));
    hashpair* queuepair=(hashpair*)malloc(sizeof(hashpair));
    if(q->len<QUEUE_NUM) {
        queuepair->key = copystring(key);
        queuepair->cont = cont;
        if (q->tail == NULL) {
            q->top = queuepair;
            q->tail = queuepair;
            queuepair->next = NULL;
        } else {
            q->tail->next = queuepair;
            queuepair->next = NULL;
            q->tail = queuepair;
        }
        q->len++;
    }
    else {
        pthread_mutex_lock(&(mystat->rwmutex));
        mystat->lose++;
        pthread_mutex_unlock(&(mystat->rwmutex));

        queuepair = q->top;
        q->top = queuepair->next;
        delItem(table,queuepair->key);
        free(queuepair->key);
        free(queuepair);

        hashpair* newpair=malloc(sizeof(hashpair));
        newpair->key=copystring(key);
        newpair->cont=cont;
        q->tail->next=newpair;
        newpair->next=NULL;
        q->tail=newpair;
    }
    //pthread_mutex_unlock(&(q->mutex));
}

int addItem(hashtable* table,char* key,content* cont)
{
    int hash=hashString(key)%table->num_bucket;
    hashpair* pair=table->bucket[hash];
#ifdef HASHTHREAD
    while(_sync_lock_test_and_set(&table->locks[hash],1))
		{
			//do nothing
		}
#endif

    while(pair!=0)
    {
        if(0==strcmp(pair->key,key)&&isEqualContent(pair->cont,cont))
            return 1;
        if(0==strcmp(pair->key,key)&&!isEqualContent(pair->cont,cont))
        {
            free(pair->cont->address);
            free(pair->cont);
            pair->cont=cont;
            return 0;
        }
        pair=pair->next;

    }

    pair = (hashpair *) malloc(sizeof(hashpair));
    pair->key = copystring(key);
    pair->cont = cont;
    pair->next = table->bucket[hash];
    table->bucket[hash] = pair;
    //此新加入lru队列的pair应该放到队列末;
    addToQueue(lru->lruqueues[hash],key,cont);

#ifdef HASHTHREAD
    _sync_synchronize();
		table->locks[hash]=0;
#endif
    return 2;
}

int delItem(hashtable* table,char* key)
{
    int hash=hashString(key)%table->num_bucket;
    hashpair* pair=table->bucket[hash];
    hashpair* prev=NULL;
    if(pair==0)
        return 0;
/**#ifdef HASHTHREAD
    while(_sync_lock_test_and_set(&table->locks[hash],1))
			{
				//do nothing
			}
#endif
 **/
    while(pair!=0)
    {
        if(0==strcmp(pair->key,key))
        {
            if(prev==NULL||prev==0)printf("prev is null\n");
            if(prev==0)
                table->bucket[hash]=pair->next;
            else
                prev->next=pair->next;
            free(pair->key);
            free(pair->cont->address);
            free(pair->cont);
            free(pair);
            return 1;
        }
        prev=pair;
        pair=pair->next;
    }
/**#ifdef HASHTHREAD
    _sync_synchronize();
			table->locks[hash]=0;
#endif**/
    return 0;
}

content* getContentByKey(hashtable* table,char* key)
{
    int hash=hashString(key)%table->num_bucket;
    hashpair* pair=table->bucket[hash];
    while(pair)
    {
        if(pair->key!=NULL)
            if(0==strcmp(pair->key,key)) {
                //fix1:visit here,should update queue;
                updateLruQueue(lru->lruqueues[hash],key);
                //
                return pair->cont;
            }
        pair=pair->next;
    }

    return NULL;
}

/**  hash   **/

typedef struct threadpool threadpool;
void waitThreadPool(threadpool* pool);


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
    int file_fd;
    long len;
    char* file_name;
    char* file_msg;
}webparam;

typedef struct staconv
{
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int status;
}staconv;

typedef struct task
{
    struct task* next;
    void (*function)(void* arg);
    void* arg;
}task;

typedef struct taskqueue
{
    pthread_mutex_t mutex;
    task* front;
    task* rear;
    staconv *has_jobs;
    int len;
}taskqueue;

typedef struct thread
{
    int id;
    pthread_t pthread;
    struct threadpool* pool;
}thread;

typedef struct threadpool
{
    thread* threads;//
    volatile int num_threads;
    volatile int num_working;
    pthread_mutex_t thcount_lock;
    pthread_cond_t threads_all_idle;
    taskqueue queue;
    volatile bool is_alive;
}threadpool;

void staconv_wait(staconv* s)//p操作
{
    pthread_mutex_lock(&(s->mutex));
    while(s->status<=0)
    {
        pthread_cond_wait(&(s->cond),&(s->mutex));
    }
    s->status=false;
    pthread_mutex_unlock(&(s->mutex));
}
void staconv_signal(staconv* s)//v操作
{
    pthread_mutex_lock(&(s->mutex));
    s->status=true;
    pthread_cond_signal(&(s->cond));
    pthread_mutex_unlock(&(s->mutex));
}

task* take_taskqueue(taskqueue* queue)
{
    pthread_mutex_lock(&(queue->mutex));
    task* t=queue->front;
    if(queue->len==1)
    {
        queue->front=NULL;
        queue->rear=NULL;
        queue->len=0;
    }
    else if(queue->len>1)
    {
        queue->front=t->next;
        queue->len-=1;
        staconv_signal(queue->has_jobs);
    }
    pthread_mutex_unlock(&(queue->mutex));
    return t;
}

void init_taskqueue(taskqueue* queue)
{
    queue->len=0;
    pthread_mutex_init(&(queue->mutex),NULL);
    queue->front=NULL;
    queue->rear=NULL;
    queue->has_jobs=(staconv*)malloc(sizeof(staconv));
    staconv* s=queue->has_jobs;
    pthread_mutex_init(&(s->mutex),NULL);
    pthread_cond_init(&(s->cond),NULL);
    s->status=false;
}

void push_taskqueue(taskqueue* queue,task* curtask)
{
    pthread_mutex_lock(&(queue->mutex));
    curtask->next=NULL;
    if(queue->len==0)
    {
        queue->rear=curtask;
        queue->front=curtask;
    }
    else
    {
        queue->rear->next=curtask;
        queue->rear=curtask;
    }
    queue->len++;
    staconv_signal(queue->has_jobs);
    pthread_mutex_unlock(&(queue->mutex));
}

void destroy_taskqueue(taskqueue* queue)
{
    pthread_mutex_destroy(&(queue->mutex));
    free(queue->front);
    pthread_mutex_destroy(&(queue->has_jobs->mutex));
    pthread_cond_destroy(&(queue->has_jobs->cond));
}

void* thread_do(struct thread* pthread)
{
    char thread_name[128]={0};
    sprintf(thread_name,"thread-pool-%d",pthread->id);
    prctl(PR_SET_NAME,thread_name);

    threadpool* pool=pthread->pool;
    //初始化线程池时，用于已经创建线程的计数，执行pool->num_threads++;
    //
    pthread_mutex_lock(&(pool->thcount_lock));
    pool->num_threads++;
    pthread_mutex_unlock(&(pool->thcount_lock));
    //
    while(pool->is_alive)
    {
        //如果任务队列中有任务，则继续运行，否则阻塞
        //
        staconv* s=(pool->queue).has_jobs;
        staconv_wait(s);
        //
        if(pool->is_alive)
        {
            //线程在工作，需要对工作线程计数pool->num_working++
            //
            pthread_mutex_lock(&(pool->thcount_lock));
            pool->num_working++;
            pthread_mutex_unlock(&(pool->thcount_lock));
            //
            void(*func)(void*);
            void* arg;
            task* curtask=take_taskqueue(&pool->queue);//实现，从任务队列头部提取任务，并在队列中删除
            if(curtask)
            {
                func=curtask->function;
                arg=curtask->arg;
                func(arg);
                free(curtask);
            }
            //表明线程已经将任务执行完毕，应该修改工作线程数量，当其为0，表示全部完成
            //
            pthread_mutex_lock(&(pool->thcount_lock));
            pool->num_working--;
            if(pool->num_working==0)
                pthread_cond_signal(&(pool->threads_all_idle));
            pthread_mutex_unlock(&(pool->thcount_lock));

            //
        }
    }
    //线程将要退出，应该修改当前线程池中的线程数量
    //pool->num_threads--;
    //
    pthread_mutex_lock(&(pool->thcount_lock));
    pool->num_threads--;
    pthread_mutex_unlock(&(pool->thcount_lock));
    //
    return NULL;
}

int create_thread(struct threadpool* pool,struct thread* pthread,int id)
{
    pthread=(struct thread*)malloc(sizeof(struct thread));
    if(pthread==NULL)
    {
        perror("create_thread():Could not allocate memory for thread\n");
        return -1;
    }
    (pthread)->pool=pool;
    (pthread)->id=id;
    pthread_create(&(pthread)->pthread,NULL,(void *)thread_do,(pthread));//thread_do
    pthread_detach((pthread)->pthread);
    return 0;
}

struct threadpool* initThreadPool(int num_threads)
{
    threadpool* pool;
    pool=(threadpool*)malloc(sizeof(struct threadpool));
    pool->num_threads=0;
    pool->num_working=0;
    pool->is_alive=true;
    pthread_mutex_init(&(pool->thcount_lock),NULL);
    pthread_cond_init(&pool->threads_all_idle,NULL);
    //初始化任务队列
    init_taskqueue(&(pool->queue));//
    pool->threads=(struct thread*)malloc(num_threads*sizeof(struct thread*));
    for(int i=0;i<num_threads;++i)
    {
        create_thread(pool,&(pool->threads[i]),i);
    }
    //等所有线程执行完毕，在每个线程运行函数中进行pool->num_threads++
    //等所有线程创建完毕，并马上运行阻塞代码时才返回
    while(pool->num_threads!=num_threads){}
    return pool;
}
void addTask2ThreadPool(threadpool* pool,task* curtask)
{
    //将任务加入队列
    push_taskqueue(&pool->queue,curtask);//
}

void waitThreadPool(threadpool* pool)
{
    pthread_mutex_lock(&pool->thcount_lock);
    while(pool->queue.len||pool->num_working)
    {
        pthread_cond_wait(&pool->threads_all_idle,&pool->thcount_lock);
    }
    pthread_mutex_unlock(&pool->thcount_lock);
}

void destoryThreadPool(threadpool* pool)
{
    //如果有任务，则等待任务队列为空
    if((pool->queue).has_jobs->status==true)
        waitThreadPool(pool);
    destroy_taskqueue(&pool->queue);//
    //销毁线程指针数组，并释放所有为线程池分配的内存
    //
    free(pool->threads);
    pthread_mutex_destroy(&(pool->thcount_lock));
    pthread_cond_destroy(&(pool->threads_all_idle));
    //
}

int getNumofThreadWorking(threadpool* pool)
{
    return pool->num_working;
}

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
            //(void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n",271);
            (void)sprintf(logbuffer,"FORBIDDEN: %s:%s",s1, s2);
            break;
        case NOTFOUND:
            //(void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n",224);
            (void)sprintf(logbuffer,"NOT FOUND: %s:%s",s1, s2);
            break;
        case LOG: (void)sprintf(logbuffer," INFO: %s:%s:%d",s1, s2,socket_fd); break;
    }
    /* No checks here, nothing can be done with a failure anyway */

    //writeLog

    if((fd = open("webserver.log", O_CREAT| O_WRONLY | O_APPEND,0644)) >= 0) {
        write(fd,timebuffer,strlen(timebuffer));
        (void)write(fd,logbuffer,strlen(logbuffer));
        (void)write(fd,"\n",1);
        (void)close(fd);
    }
    if(type==ERROR)exit(-1);
}
threadpool* read_msg_pool;//初始化线程池300个线程容量，且300个线程开始等待任务队列的进来
threadpool* read_file_pool;
threadpool* send_msg_pool;

void* web_sendMsg(void* data)
{

    webparam* param=data;

    char* buffer=param->file_msg;

    (void)write(param->fd,buffer,param->len);

    close(param->fd);
    free(param);
}

void* web_readFile(void* data)
{

    webparam* param=data;
    int fd;
    int hit,file_fd,buflen;
    long ret,len,i;
    char* fstr;
    char* buffer=param->file_name;

    buflen=strlen(buffer);

    fd=param->fd;
    hit=param->hit;

    fstr = (char *)0;
    for(i=0;extensions[i].ext != 0;i++) {
        len = strlen(extensions[i].ext);
        if( !strncmp(&buffer[buflen-len], extensions[i].ext, len)) {
            fstr =extensions[i].filetype;
            break;
        }
    }
    if(fstr == 0) logger(FORBIDDEN,"file extension type not supported",buffer,fd);

    char* filekey=(char*)malloc(sizeof(char)*31);
    int k,s=0;
    for(k=5;buffer[k]!=' '&&k<=30;k++)
        filekey[s++]=buffer[k];
    filekey[s]='\0';

    if(( file_fd = open(&(buffer[5]),O_RDONLY)) == -1) {  /* open the file for reading */
        logger(NOTFOUND, "failed to open file",&(buffer[5]),fd);
    }
    else {
        logger(LOG, "SEND", buffer, hit);

        len = (long) lseek(file_fd, (off_t) 0, SEEK_END); /* lseek to the file end to find the length */
        (void) lseek(file_fd, (off_t) 0, SEEK_SET); /* lseek back to the file start ready for reading */
        (void) sprintf(buffer,
                       "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n",
                       VERSION, len, fstr); /* Header + a blank line */
        logger(LOG, "Header", buffer, hit);

        (void) write(fd, buffer, strlen(buffer));//先写

        webparam *pa = (webparam *) malloc(sizeof(webparam));
        pa->fd = fd;
        pa->file_fd = file_fd;

        pthread_mutex_lock(&(mystat->rwmutex));
        mystat->visit++;
        pthread_mutex_unlock(&(mystat->rwmutex));

        content *bucketcon = getContentByKey(table, filekey);
        if (bucketcon == NULL)//没读到
        {

            char *newbuffer = (char *) malloc(sizeof(char) * (len + 1));
            if ((ret = read(file_fd, newbuffer, (len + 1))) > 0) {

                content *con = (content *) malloc(sizeof(content));
                con->length = len;
                con->address = newbuffer;//cont->address究竟是初始地址还是一个字符串？
                addItem(table, filekey, con);

                pa->file_msg = newbuffer;
                pa->len = len;

            } else {
                free(newbuffer);
                free(param);
                close(file_fd);
                close(fd);
                return NULL;
            }

        }
        else//读到了
        {

            pa->file_msg = bucketcon->address;
            pa->len = bucketcon->length;

        }


        task *t = (task *) malloc(sizeof(task));
        t->arg = (void *) pa;
        t->function = (void *) web_sendMsg;
        addTask2ThreadPool(send_msg_pool, t);
    }
    free(param);
    close(file_fd);
}


/* this is a child web server process, so we can exit on errors */
void* web(void* data)
{

    webparam* param=data;

    webparam* pa=(webparam*)malloc(sizeof(webparam));
    pa->file_name=(char*)malloc(sizeof(char)*(BUFSIZE+1));

    int fd;
    int hit;
    int j,file_fd;
    long i,ret,len;
    char* buffer=pa->file_name;
    fd=param->fd;
    hit=param->hit;

    ret=read(fd,buffer,BUFSIZE);

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



    pa->fd=fd;
    pa->hit=hit;

    task* t=(task*)malloc(sizeof(task));
    t->function=(void*)web_readFile;
    t->arg=(void*)pa;
    addTask2ThreadPool(read_file_pool,t);//将任务加入线程池中

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

    logger(LOG,"nweb starting",argv[1],getpid());
    /* setup the network socket */
    if((listenfd = socket(AF_INET, SOCK_STREAM,0)) <0)
        logger(ERROR, "system call","socket",0);
    port = atoi(argv[1]);


    if(port < 0 || port >60000)
        logger(ERROR,"Invalid port number (try 1->60000)",argv[1],0);

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if(bind(listenfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)) <0)
        logger(ERROR,"system call","bind",0);

    if( listen(listenfd,64) <0)
        logger(ERROR,"system call","listen",0);

    read_msg_pool=initThreadPool(16);//初始化线程池300个线程容量，且300个线程开始等待任务队列的进来
    read_file_pool=initThreadPool(16);
    send_msg_pool=initThreadPool(10);

    table=createHashTable(BUCKET_NUM);
    lru=initLRU(BUCKET_NUM);

    mystat=initStatistics();
    printf("init done!\n");
    for(hit=1;;hit++)
    {
        if(hit!=0&&hit%1000==0)
            reportStatistics(mystat);
        length = sizeof(cli_addr);
        if((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR,"system call","accept",0);
        else
        {
            webparam* param=malloc(sizeof(webparam));
            param->hit=hit;
            param->fd=socketfd;
            task* t=(task*)malloc(sizeof(task));
            t->function=(void*)web;
            t->arg=(void*)param;
            addTask2ThreadPool(read_msg_pool,t);//将任务加入解析线程池中
        }

    }
    destoryThreadPool(send_msg_pool);
    destoryThreadPool(read_file_pool);
    destoryThreadPool(read_msg_pool);
    freeHashTable(table);
}
