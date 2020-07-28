#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/stat.h>
#include<sys/types.h>
#include<fcntl.h>

#define N 100000
#define BUCKET 1000
unsigned long int filesystem_hashString(char * str)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}

int main()
{
    int file_fd;
    int file_fd2;
    int hash;
    int j=0;
    long len,ret;

    char* basepath="/home/luoyuanzhen/os-web/web/Crawler4jDate/";
    char* filesysPath="/home/luoyuanzhen/os-web/fileSystem/";
    char* filesysPath2="/home/luoyuanzhen/os-web/fileSystem";
    char* getpath=(char*)malloc(sizeof(char)*100);
    char* makepath=(char*)malloc(sizeof(char)*100);
    char* writepath=(char*)malloc(sizeof(char)*100);
    char* writelenpath=(char*)malloc(sizeof(char)*100);
    char** file=(char**)malloc(sizeof(char*)*BUCKET);
    char** htmlname=(char**)malloc(sizeof(char*)*N);
    char** lenname=(char**)malloc(sizeof(char*)*N);
    for(int i=0;i<N;i++)//初始化html文件名
    {
        htmlname[i]=(char*)malloc(sizeof(char)*31);
        lenname[i]=(char*)malloc(sizeof(char)*31);
        sprintf(htmlname[i],"%d.html",(i+1));
        sprintf(lenname[i],"%d.txt",(i+1));
    }
    for(int i=0;i<BUCKET;i++)//初始化file文件夹名
    {
        file[i]=(char*)malloc(sizeof(char)*31);
        sprintf(file[i],"%d",(i+1));
        printf("file[%d]:%s%s\n",i,file[i],file[i]);
    }
    if(mkdir(filesysPath2,S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRWXO)!=0)
    {
        printf("Error make dir %s failed\n",filesysPath2);
    } else {
        for (int i = 0; i < N; i++) {
            hash = filesystem_hashString(htmlname[i]) % BUCKET;
            printf("hash:%d\n", hash);
            sprintf(makepath, "%s%s", filesysPath, file[hash]);
            sprintf(writepath, "%s/%s", makepath, htmlname[i]);
            sprintf(writelenpath,"%s/%s",makepath,lenname[i]);
            sprintf(getpath, "%s%s", basepath, htmlname[i]);
            if (access(makepath, 0) != 0) {
                if (mkdir(makepath, S_IRUSR | S_IWUSR | S_IXUSR | S_IRWXG | S_IRWXO) != 0) {
                    printf("Error:make dir %s failed\n", makepath);
                    continue;
                }
            }
            file_fd = open(getpath, O_RDONLY);
            if (file_fd < 0)
                printf("Error:open file%s failed\n", getpath);
            else {
                len = (long) lseek(file_fd, (off_t) 0, SEEK_END); /* lseek to the file end to find the length */
                (void) lseek(file_fd, (off_t) 0, SEEK_SET); /* lseek back to the file start ready for reading */
                char *buffer = (char *) malloc(sizeof(char) * len);
                if ((ret = read(file_fd, buffer, len)) > 0) {
                    FILE* f=fopen(writepath,"w");
                    if(f==NULL)printf("Error:open file%s failed\n",writepath);
                    else {
                        FILE* flen=fopen(writelenpath,"w");
                        if(flen==NULL)printf("Error:open file%s failed\n",writelenpath);
                        else{
                            fprintf(flen,"%d",len);
                            fprintf(f, "%s", buffer);
                            fclose(f);
                            fclose(flen);
                        }
                    }
                    close(file_fd);
                } else printf("Error:read file%s failed\n", getpath);
            }
        }
        printf("file System create done!\n");
    }
    return 0;
}
