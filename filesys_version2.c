#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<pthread.h>
#include<fcntl.h>
#include<string.h>

#define N 10
#define BUCKET 50

int maxlen;

char* basepath="/home/luoyuanzhen/os-web/web/Crawler4jDate/";
char* filesysPath="/home/luoyuanzhen/os-web/fileSystem.txt";
char* getpath;
char** htmlname;

typedef struct fileInfo
{
    int isuse;
    long pos;
    long size;
}fileInfo;

typedef struct fileSys
{
    pthread_mutex_t rwmutex;
    char* filesysPath;
    fileInfo* files;
    int num_file;
}fileSys;

fileSys* filesys;

fileSys* initFileSys(int num)
{
    fileSys* fs=(fileSys*)malloc(sizeof(fileSys));
    pthread_mutex_init(&fs->rwmutex,NULL);
    fs->num_file=num;
    fs->filesysPath=filesysPath;
    fs->files=(fileInfo*)malloc((num+1)*sizeof(fileInfo));
    for(int i=1;i<=num;i++)
        fs->files[i].isuse=0;
    return fs;
}

int nameToindex(char* filename)
{
    int index;
    char* strindex=(char*)malloc(sizeof(char)*7);
    int k=0;

    for(int i=0;i<7&&filename[i]!='.';i++)
        strindex[k++]=filename[i];
    strindex[k]='\0';

    index=atoi(strindex);
    free(strindex);

    return index;
}
void addToFileSys(fileSys* fs,char* filename,long len,long pos)
{
    int index=nameToindex(filename);

    printf("index:%d ",index);
    fs->files[index].size=len;
    fs->files[index].pos=pos;
    fs->files[index].isuse=1;
}

void printFileInfo(fileSys* fs)
{
    for(int i=1;i<=fs->num_file;i++)
    {
        if(fs->files[i].isuse==1)
            printf("files[%d]-pos:%ld  len:%ld\n",i,fs->files[i].pos,fs->files[i].size);
    }
}

void initFileSystem()
{
    int file_fd;
    int j=0;
    long len,ret;
    long pos=0;

    filesys=initFileSys(10);

    FILE* f = fopen(filesysPath, "w+");
    if (f == NULL)printf("Error:open file%s failed\n", filesysPath);
    else {
        for (int i = 0; i < N; i++) {

            sprintf(getpath, "%s%s", basepath, htmlname[i]);

            file_fd = open(getpath, O_RDONLY);

            if (file_fd < 0)
                printf("Error:open file%s failed\n", getpath);
            else {

                len = (long) lseek(file_fd, (off_t) 0, SEEK_END); /* lseek to the file end to find the length */
                (void) lseek(file_fd, (off_t) 0, SEEK_SET); /* lseek back to the file start ready for reading */
                printf("html[%d] len:%ld\n",i,len);

                addToFileSys(filesys,htmlname[i],len,pos);

                char *buffer = (char *) malloc(sizeof(char) * len);
                if((ret = read(file_fd, buffer, len)) > 0) {
                    fprintf(f, "%s", buffer);
                }
                else{
                    printf("Error:read file %s failed\n",getpath);
                }
                pos+=len;
                close(file_fd);
            }
        }
        fclose(f);
        printf("file System create done!\n");
    }
}

long getLength(fileSys* fs,char*filename)
{
    int index=nameToindex(filename);
    long len;
    pthread_mutex_lock(&fs->rwmutex);
    len=fs->files[index].size;
    pthread_mutex_unlock(&fs->rwmutex);
    return len;
}

long myread(fileSys* fs,char* filename,char* buffer)
{
    int index=nameToindex(filename);
    printf("read file:%s ,index:%d\n",filename,index);
    long pos,len;
    int file_fd;

    pthread_mutex_lock(&fs->rwmutex);
    pos=fs->files[index].pos;
    len=fs->files[index].size;
    file_fd=open(fs->filesysPath,O_RDONLY);
    if(file_fd==-1)printf("Error: open file %s failed\n",fs->filesysPath);
    else {
        lseek(file_fd,(off_t)(pos* sizeof(char)),SEEK_SET);
        if(read(file_fd, buffer, len)<=0)len=-1;
    }
    pthread_mutex_unlock(&fs->rwmutex);
    return len;
}

int main()
{
    getpath=(char*)malloc(sizeof(char)*100);
    htmlname=(char**)malloc(sizeof(char*)*N);

    for(int i=0;i<N;i++)//初始化html文件名
    {
        htmlname[i]=(char*)malloc(sizeof(char)*31);
        sprintf(htmlname[i],"%d.html",(i+1));
    }
    //printf("Longest file :%ld\n",findLongest());
    initFileSystem();
    printFileInfo(filesys);
    char* filename1="1.html";
    char* filename2="2.html";
    char* buffer1=(char*)malloc(sizeof(char)*getLength(filesys,filename1));
    char* buffer2=(char*)malloc(sizeof(char)*getLength(filesys,filename2));
    int f1,f2;
    f1=myread(filesys,"1.html",buffer1);
    f2=myread(filesys,"2.html",buffer2);
    if(f1!=-1)printf("1.html:\n%s\n",buffer1);
    if(f2!=-1)printf("2.html:\n%s\n",buffer2);
    return 0;
}

