#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<pthread.h>
#include<fcntl.h>
#include<string.h>


char* basepath="/home/luoyuanzhen/os-web/web/Crawler4jDate/";//需要倒入的网页文件的目录
char* filesysPath="/home/luoyuanzhen/os-web/fileSystem.txt";//文件系统的所在路径
char* getpath;
char** htmlname;

typedef struct fileInfo
{
    int isuse;
    long long pos;//此文件在文件系统中的偏移位置
    long size;//此文件在文件系统中的长度大小
}fileInfo;

typedef struct fileSys
{
    pthread_mutex_t rwmutex;
    char* filesysPath;//文件系统的路径
    fileInfo* files;//存储约十万个网页文件的信息项
    int num_file;
}fileSys;

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

fileSys* initFileSystem(int num_html)
{
    int file_fd;
    int j=0;
    long len,ret;
    long long pos=0;//记录偏移量

    fileSys* filesys=initFileSys(num_html);

    getpath=(char*)malloc(sizeof(char)*100);
    htmlname=(char**)malloc(sizeof(char*)*num_html);

    for(int i=0;i<num_html;i++)//初始化html文件名
    {
        htmlname[i]=(char*)malloc(sizeof(char)*31);
        sprintf(htmlname[i],"%d.html",(i+1));
    }

    int f = open(filesysPath, O_WRONLY);
    if (f == -1)printf("Error:open file%s failed\n", filesysPath);
    else {
        for (int i = 0; i < num_html; i++) {

            sprintf(getpath, "%s%s", basepath, htmlname[i]);

            file_fd = open(getpath, O_RDONLY);

            if (file_fd < 0)
                printf("Error:open file%s failed\n", getpath);
            else {

                len = (long) lseek(file_fd, (off_t) 0, SEEK_END); /* lseek to the file end to find the length */
                (void) lseek(file_fd, (off_t) 0, SEEK_SET); /* lseek back to the file start ready for reading */

                printf("pos:%lld\n",pos);
                addToFileSys(filesys,htmlname[i],len,pos);

                char* buffer = (char *) malloc(sizeof(char) * (len+1));

                if((ret = read(file_fd, buffer, len)) > 0) {
                    write(f,buffer,len);
                }
                else{
                    printf("Error:read file %s failed\n",getpath);
                }
                pos=pos+len;
                close(file_fd);
                free(buffer);
            }
        }
        close(f);
        printf("file System create done!\n");
    }
    return filesys;
}

long getLength(fileSys* fs,char*filename)
{
    long len;
    int index;
    pthread_mutex_lock(&fs->rwmutex);

    index=nameToindex(filename);

    printf("name:%s,index:%d",filename,index);
    len=fs->files[index].size;
    pthread_mutex_unlock(&fs->rwmutex);
    return len;
}

long myread(fileSys* fs,char* filename,char* buffer)
{
    int index;

    long long pos;
    long len;
    int file_fd;

    pthread_mutex_lock(&fs->rwmutex);
    index=nameToindex(filename);
    pos=fs->files[index].pos;
    len=fs->files[index].size;
    file_fd=open(fs->filesysPath,O_RDONLY);
    if(file_fd==-1)printf("Error: open file %s failed\n",fs->filesysPath);
    else {
        lseek(file_fd,(off_t)pos,SEEK_SET);
        if(read(file_fd, buffer, len+1)<=0)len=-1;
        close(file_fd);
    }
    pthread_mutex_unlock(&fs->rwmutex);
    return len;
}