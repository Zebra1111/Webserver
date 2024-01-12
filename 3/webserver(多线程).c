#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

struct
{
    char *ext;
    char *filetype;
} extensions[] = {
    {"gif", "image/gif"},
    {"jpg", "image/jpg"},
    {"jpeg", "image/jpeg"},
    {"png", "image/png"},
    {"ico", "image/ico"},
    {"zip", "image/zip"},
    {"gz", "image/gz"},
    {"tar", "image/tar"},
    {"htm", "text/html"},
    {"html", "text/html"},
    {0, 0}};

typedef struct
{
    int hit;
    int fd;
} webparam;

void logger(int type, char *s1, char *s2, int socket_fd)
{
    int fd;
    char logbuffer[BUFSIZE * 2];

    time_t t = time(NULL);
    char time_now[100];
    strftime(time_now, sizeof(time_now), "%Y-%m-%d %H:%M:%S", localtime(&t));

    switch (type)
    {
    case ERROR:
        (void)sprintf(logbuffer, "%s:ERROR: %s:%s Errno=%d exiting pid= %d", time_now, s1, s2, errno, getpid());
        break;
    case FORBIDDEN:
        (void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length:185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\nThe requested URL, file type or operationis not allowed on this simple static file webserver.\n</body></html>\n", 271);
        (void)sprintf(logbuffer, "%s:FORBIDDEN: %s:%s", time_now, s1, s2);
        break;
    case NOTFOUND:
        (void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length:136\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>404 Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224);
        (void)sprintf(logbuffer, "%s:NOT FOUND: %s:%s", time_now, s1, s2);
        break;
    case LOG:
        (void)sprintf(logbuffer, "%s: INFO: %s:%s:%d", time_now, s1, s2, socket_fd);
        break;
    }

    if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
    {
        (void)write(fd, logbuffer, strlen(logbuffer));
        (void)write(fd, "\n", 1);
        (void)close(fd);
    }
}

void *web(void *data)
{
    webparam *param = (webparam *)data;
    int fd = param->fd, hit = param->hit;
    int j, file_fd, buflen;
    long i, ret, len;
    char *fstr;
    char buffer[BUFSIZE + 1]; /* 缓存 */

    ret = read(fd, buffer, BUFSIZE); /* 从socket 读取 Web 请求内容 */
    if (ret == 0 || ret == -1)
    { /* 读取失败 */
        logger(FORBIDDEN, "failed to read browser request", "", fd);
    }
    else
    {
        if (ret > 0 && ret < BUFSIZE) // 确保读取到的数据以 null 字符 ('\0') 结尾
            buffer[ret] = 0;
        else
            buffer[0] = 0;
        for (i = 0; i < ret; i++) // 回车符 ('\r') 和换行符 ('\n') 替换为星号
            if (buffer[i] == '\r' || buffer[i] == '\n')
                buffer[i] = '*';
        logger(LOG, "request", buffer, hit);

        // 解析请求，检查请求的方法是否为GET
        if (strncmp(buffer, "GET ", 4) != 0)
        {
            logger(FORBIDDEN, "only simple get operation supported", buffer, fd);
        }
        for (i = 4; i < BUFSIZE; i++)
        {
            if (buffer[i] == ' ')
            {
                buffer[i] = 0;
                break;
            }
        }

        // 安全性检查
        for (j = 0; j < i - 1; j++)
            if (buffer[j] == '.' && buffer[j + 1] == '.')
            {
                logger(FORBIDDEN, "parent directory (..) path names not supported", buffer, fd);
            }

        // 处理默认请求（如果请求的是根路径 ("/")，将其默认为 "/index.html"）
        if (!strncmp(&buffer[0], "GET /\0", 6))
            (void)strcpy(buffer, "GET /index. html");

        // 根据文件扩展名确定文件类型
        buflen = strlen(buffer);
        fstr = (char *)0; // 初始化为指向空NULL
        for (i = 0; extensions[i].ext != 0; i++)
        {
            len = strlen(extensions[i].ext);
            if (!strncmp(&buffer[buflen - len], extensions[i].ext, len))
            {
                fstr = extensions[i].filetype;
                break;
            }
        }
        if (fstr == 0)
            logger(FORBIDDEN, "file extension type not supported", buffer, fd);

        // 打开文件
        if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) // &buffer[5] 表示从 buffer 字符数组的第五个元素开始的地址，即文件路径的起始位置
        {
            logger(NOTFOUND, "failed to open file", &buffer[5], fd);
        }
        logger(LOG, "send", &buffer[5], hit);
        len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* 使用lseek 获得文件长度,该方法比较低效*/
        (void)lseek(file_fd, (off_t)0, SEEK_SET);       /* 想想还有什么方法可获取*/
        (void)sprintf(buffer, "http/1.1 200 ok\nserver: nweb/%d.0\ncontent-length:%ld\nconnection: close\ncontent-type: %s\n\n", VERSION, len, fstr);
        logger(LOG, "header", buffer, hit);
        // 这里直接将buffer的长度作为数据长度（单位为字节）是因为buffer是一个字符串，而一个字符是一个字节
        (void)write(fd, buffer, strlen(buffer));
        // read 函数的作用是将文件中的数据读取到提供的缓冲区中，如果缓冲区中有先前的内容，它将被新的数据覆盖
        while ((ret = read(file_fd, buffer, BUFSIZE)) > 0) // read函数返回的是读取文件内容的字节数
        {
            (void)write(fd, buffer, ret);
        }
        close(file_fd);
    }
    usleep(10000); /*在socket 通道关闭前,留出发送一段信息的时间*/
    close(fd);
    free(param);
    return NULL;
}

int main(int argc, char **argv)
{
    int i, port, listenfd, socketfd, hit;
    socklen_t length;
    static struct sockaddr_in cli_addr;  /* static = initialised to zeros */
    static struct sockaddr_in serv_addr; /* static = initialised to zeros */
    if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?"))
    {
        (void)printf("hint: n web Port-Number Top-Directory\t\tversion %d\n\n"
                     "\tnweb is a small and very safe mini web server\n"
                     "\tnweb only servers out file/web pages with extensions named below\n "
                     "\t and (only from the named directory or its sub-directories.\n "
                     "\tThere is no fancy features = safe and secure.\n\n"
                     "\tExample: nweb 8181 /home/nwebdir &\n\n"
                     "\tOnly Supports:",
                     VERSION);
        for (i = 0; extensions[i].ext != 0; i++)
            (void)printf(" %s", extensions[i].ext);
        (void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript,CGI\n"
                     "\tNot Supported: directories /etc/bin/lib /tmp /usr /dev/sbin \n"
                     "\tNowarrantygivenorimplied\n\tNigelGriffithsnag@uk.ibm.com\n");
        exit(0);
    }
    if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
        !strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
        !strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
        !strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
    {
        (void)printf("ERROR: Bad top directory %s, see nweb-?\n", argv[2]);
        exit(3);
    }
    if (chdir(argv[2]) == -1)
    {
        (void)printf("ERROR: Can't Change to directory %s\n", argv[2]);
        exit(4);
    }

    // 将程序变为一个后台守护进程
    // if (fork() != 0)
    // 	return 0;
    // (void)signal(SIGCLD, SIG_IGN);// 忽略子进程的退出信号
    // (void)signal(SIGHUP, SIG_IGN);// 忽略挂断信号,使进程不受挂断信号的影响
    // for (i = 0; i < 32; i++)
    // 	(void)close(i);
    // (void)setpgrp();
    // logger(LOG, "nweb starting", argv[1], getpid());

    // 初始化线程属性为分离状态
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t pth; // 创建线程标识符(这个不用放for循环中)

    /* 建立服务端侦听 socket*/
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        logger(ERROR, "system call", "socket", 0);
    port = atoi(argv[1]);
    if (port < 0 || port > 60000)
        logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);
    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
        logger(ERROR, "system call", "bind", 0);
    if (listen(listenfd, 64) < 0)
        logger(ERROR, "system call", "listen", 0);

    for (hit = 1;; hit++)
    {
        length = sizeof(cli_addr);
        if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
            logger(ERROR, "system call", "accept", 0);
        webparam *param = malloc(sizeof(webparam));
        param->hit = hit;
        param->fd = socketfd;
        if (pthread_create(&pth, &attr, &web, (void *)param) < 0)
        {
            logger(ERROR, "system call", "pthread_create", 0);
        }
    }
}