/* webserver.c*/
/*The following main code from https://github.com/ankushagarwal/nweb*, but they are modified slightly*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/shm.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>

#define SHARED_MEMORY_NAME "shared_memory" // 确保名称的唯一性
#define SEMAPHORE_NAME "semaphore"
#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

struct Targs
{
	int sockfd;
	int hit_;
};

double hit_t, rsoc, wsoc, rweb, wlog;
struct timeval hst, hend;
struct timeval rsst, rsend;
struct timeval wsst, wsend;
struct timeval rwst, rwend;
struct timeval wlst, wlend;

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

/* 日志函数，将运行过程中的提示信息记录到 webserver.log 文件中*/
void logger(int type, char *s1, char *s2, int socket_fd)
{
	int fd;
	char logbuffer[BUFSIZE * 2];

	time_t t = time(NULL);
	char time_now[30];
	strftime(time_now, sizeof(time_now), "%Y-%m-%d %H:%M:%S", localtime(&t));

	/*根据消息类型，将消息放入 logbuffer 缓存，或直接将消息通过 socket 通道返回给客户端*/
	switch (type)
	{
	case ERROR:
		(void)sprintf(logbuffer, "%s:ERROR: %s:%s Errno=%d exiting pid=%d", time_now, s1, s2, errno, getpid());
		break;
	case FORBIDDEN:
		(void)write(socket_fd, "HTTP/1.1 403 Forbidden\nContent-Length: 185\nConnection: close\nContent-Type: text/html\n\n<html><head>\n<title>403 Forbidden</title>\n</head><body>\n<h1>Forbidden</h1>\n The requested URL, file type or operation is not allowed on this simple static file webserver.\n</body></html>\n", 271);
		(void)sprintf(logbuffer, "%s:FORBIDDEN: %s:%s", time_now, s1, s2);
		break;
	case NOTFOUND:
		(void)write(socket_fd, "HTTP/1.1 404 Not Found\nContent-Length: 136\nConnection: close\nContent-Type:	text/html\n\n<html><head>\n<title>404	Not Found</title>\n</head><body>\n<h1>Not Found</h1>\nThe requested URL was not found on this server.\n</body></html>\n", 224);
		(void)sprintf(logbuffer, "%s:NOT FOUND: %s:%s", time_now, s1, s2);
		break;
	case LOG:
		(void)sprintf(logbuffer, "%s:INFO: %s:%s:%d", time_now, s1, s2, socket_fd);
		break;
	}
	wlst = clock();
	/* 将 logbuffer 缓存中的消息存入 webserver.log 文件*/
	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
	wlend = clock();
	wlog += wlend - wlst;
}

/* 此函数完成了 WebServer 主要功能，它首先解析客户端发送的消息，然后从中获取客户端请求的文件名，然后根据文件名从本地将此文件读入缓存，并消生成相应的 HTTP 响应息；最后通过服务器与客户端的 socket 通道向客户端返回 HTTP 响应消息*/

void web(void *args)
{
	struct Targs *targ = (struct Targs *)args;
	int fd = targ->sockfd, hit = targ->hit_;

	int j, file_fd, buflen;
	long i, ret, len;
	char *fstr;
	static char buffer[BUFSIZE + 1]; /* 设置静态缓冲区 */
	gettimeofday(&rsst, NULL);
	ret = read(fd, buffer, BUFSIZE); /* 从连接通道中读取客户端的请求消息 (读socket)*/
	gettimeofday(&rsend, NULL);
	rsoc += rsend - rsst;
	if (ret == 0 || ret == -1)
	{ // 如果读取客户端消息失败，则向客户端发送 HTTP 失败响应信息
		logger(FORBIDDEN, "failed to read browser request", "", fd);
	}
	else
	{
		if (ret > 0 && ret < BUFSIZE) /* 设置有效字符串，即将字符串尾部表示为 0 */
			buffer[ret] = 0;
		else
			buffer[0] = 0;
		for (i = 0; i < ret; i++) /* 移除消息字符串中的“CF”和“LF”字符*/
			if (buffer[i] == '\r' || buffer[i] == '\n')
				buffer[i] = '*';
		logger(LOG, "request", buffer, hit);
		/*判断客户端 HTTP 请求消息是否为 GET 类型，如果不是则给出相应的响应消息*/
		if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "get ", 4))
		{
			logger(FORBIDDEN, "Only simple GET operation supported", buffer, fd);
		}
		for (i = 4; i < BUFSIZE; i++)
		{ /* null terminate after the second space to ignore extra stuff */
			if (buffer[i] == ' ')
			{ /* string is "GET URL " +lots of other stuff */
				buffer[i] = 0;
				break;
			}
		}
		for (j = 0; j < i - 1; j++) /* 在消息中检测路径，不允许路径中出现“.” */
			if (buffer[j] == '.' && buffer[j + 1] == '.')
			{
				logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
			}
		if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6))
			/* 如果请求消息中没有包含有效的文件名，则使用默认的文件名 index.html */
			(void)strcpy(buffer, "GET /index.html");

		/* 根据预定义在 extensions 中的文件类型，检查请求的文件类型是否本服务器支持 */
		buflen = strlen(buffer);
		fstr = (char *)0;
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

		if ((file_fd = open(&buffer[5], O_RDONLY)) == -1)
		{ /* 打开指定的文件名*/
			logger(NOTFOUND, "failed to open file", &buffer[5], fd);
		}
		logger(LOG, "SEND", &buffer[5], hit);
		len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* 通过 lseek 获取文件长度*/
		(void)lseek(file_fd, (off_t)0, SEEK_SET);		/* 将文件指针移到文件首位置*/
		/* Header + a blank line */
		(void)sprintf(buffer, "HTTP/1.1 200 OK\nServer: nweb/%d.0\nContent-Length: %ld\nConnection: close\nContent-Type: %s\n\n", VERSION, len, fstr);
		logger(LOG, "Header", buffer, hit);
		wsst = clock();
		(void)write(fd, buffer, strlen(buffer)); // 写socket
		wsend = clock();
		wsoc += wsend - wsst;

		/* 不停地从文件里读取文件内容，并通过 socket 通道向客户端返回文件内容*/
		rwst = clock();
		ret = read(file_fd, buffer, BUFSIZE); // 读网页数据
		rwend = clock();
		rweb += rwend - rwst;
		while (ret > 0)
		{
			wsst = clock();
			(void)write(fd, buffer, strlen(buffer)); // 写socket
			wsend = clock();
			wsoc += wsend - wsst;

			rwst = clock();
			ret = read(file_fd, buffer, BUFSIZE); // 读网页数据
			rwend = clock();
			rweb += rwend - rwst;
		}
		usleep(10000);
		close(file_fd);
	}

	free(targ); // 释放掉动态申请的内存
	close(fd);
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
	static struct sockaddr_in serv_addr; /* static = initialised to zeros */

	/*解析命令参数*/
	if (argc < 3 || argc > 3 || !strcmp(argv[1], "-?"))
	{
		(void)printf("hint: nweb Port-Number Top-Directory\t\tversion %d\n\n"
					 "\tnweb is a small and very safe mini web server\n"
					 "\tnweb only servers out file/web pages with extensions named below\n"
					 "\t and only from the named directory or its sub-directories.\n"
					 "\tThere is no fancy features = safe and secure.\n\n"
					 "\tExample:webserver 8181 /home/nwebdir &\n\n"
					 "\tOnly Supports:",
					 VERSION);
		for (i = 0; extensions[i].ext != 0; i++)
			(void)printf(" %s", extensions[i].ext);

		(void)printf("\n\tNot Supported: URLs including \"..\", Java, Javascript, CGI\n"
					 "\tNot Supported: directories / /etc /bin /lib /tmp /usr /dev /sbin \n"
					 "\tNo warranty given or implied\n\tNigel Griffiths nag@uk.ibm.com\n");
		exit(0);
	}
	if (!strncmp(argv[2], "/", 2) || !strncmp(argv[2], "/etc", 5) ||
		!strncmp(argv[2], "/bin", 5) || !strncmp(argv[2], "/lib", 5) ||
		!strncmp(argv[2], "/tmp", 5) || !strncmp(argv[2], "/usr", 5) ||
		!strncmp(argv[2], "/dev", 5) || !strncmp(argv[2], "/sbin", 6))
	{
		(void)printf("ERROR: Bad top directory %s, see nweb -?\n", argv[2]);
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
	// (void)signal(SIGCLD, SIG_IGN); // 忽略子进程的退出信号
	// (void)signal(SIGHUP, SIG_IGN); // 忽略挂断信号,使进程不受挂断信号的影响
	// for (i = 0; i < 32; i++)
	// 	(void)close(i);
	// (void)setpgrp();
	// logger(LOG, "nweb starting", argv[1], getpid());

	/* 建立服务端侦听 socket*/
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		logger(ERROR, "system call", "socket", 0);
	port = atoi(argv[1]);
	if (port < 0 || port > 60000)
		logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

	// 初始化线程属性为分离状态
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t pth; // 创建线程（这个放在for循环中会如何？）

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(port);
	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		logger(ERROR, "system call", "bind", 0);
	if (listen(listenfd, 64) < 0) // 开始侦听socket连接，最大连接数为64
		logger(ERROR, "system call", "listen", 0);

	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);

		struct Targs *targ = (struct Targs *)malloc(sizeof(struct Targs)); // 为每个线程分配专有的结构体数据
		targ->sockfd = socketfd, targ->hit_ = hit;

		hst = clock();
		if (pthread_create(&pth, &attr, (void *)web, (void *)targ) < 0) // 新线程处理网页请求
			logger(ERROR, "system call", "pthread_create", 0);
		hend = clock();
		hit_t = (long)(hend - hst);
		printf("共用 %ldms 成功处理 %d 个客户端请求，其中\n", (long)hend, hit);
		// printf("平均每个客户端完成请求处理时间为 %ldms\n", hit_t / hit);
		// printf("平均每个客户端完成读 socket 时间为 %ldms\n", rsoc / hit);
		// printf("平均每个客户端完成写 socket 时间为 %ldms\n", wsoc / hit);
		// printf("平均每个客户端完成读网页数据时间为 %ldms\n", rweb / hit);
		// printf("平均每个客户端完成写日志数据时间为 %ldms\n", wlog / hit);
		// printf("\n");
	}
}
