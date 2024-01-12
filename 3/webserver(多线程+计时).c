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
#include <sys/time.h>

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

double hit_t, rsoc, wsoc, rweb, wlog;
struct timeval hst, hend;
struct timeval rsst, rsend;
struct timeval wsst, wsend;
struct timeval rwst, rwend;
struct timeval wlst, wlend;

unsigned long get_file_size(const char *path)
{
	unsigned long filesize = -1;
	struct stat statbuff;
	if (stat(path, &statbuff) < 0)
	{
		return filesize;
	}
	else
	{
		filesize = statbuff.st_size;
	}
	return filesize;
}

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

	gettimeofday(&wlst, NULL);
	/* No checks here, nothing can be done with a failure anyway */
	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
	gettimeofday(&wlend, NULL);
	wlog += (wlend.tv_sec - wlst.tv_sec) * 1000.0 + (wlend.tv_usec - wlst.tv_usec) / 1000.0;
}

/* this is a web thread, so we can exit on errors */
void *web(void *data)
{
	gettimeofday(&hst, NULL);
	int fd;
	int hit;
	int j, file_fd, buflen;
	long i, ret, len;
	char *fstr;
	char buffer[BUFSIZE + 1]; /* 缓存 */
	webparam *param = (webparam *)data;
	fd = param->fd;
	hit = param->hit;
	gettimeofday(&rsst, NULL);
	ret = read(fd, buffer, BUFSIZE); /* 从socket 读取 Web 请求内容 */
	gettimeofday(&rsend, NULL);
	rsoc += (rsend.tv_sec - rsst.tv_sec) * 1000.0 + (rsend.tv_usec - rsst.tv_usec) / 1000.0;
	if (ret == 0 || ret == -1)
	{ /* 读取失败 */
		logger(FORBIDDEN, "failed to read browser request", "", fd);
	}
	else
	{
		if (ret > 0 && ret < BUFSIZE) /*读出消息的长度*/
			buffer[ret] = 0;
		else
			buffer[0] = 0;
		for (i = 0; i < ret; i++)
			if (buffer[i] == '\r' || buffer[i] == '\n')
				buffer[i] = '*';
		logger(LOG, "request", buffer, hit);
		if (strncmp(buffer, "GET ", 4) && strncmp(buffer, "GET ", 4))
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
		for (j = 0; j < i - 1; j++)
			if (buffer[j] == '.' && buffer[j + 1] == '.')
			{
				logger(FORBIDDEN, "parent directory (..) path names not supported", buffer, fd);
			}
		if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "GET /\0", 6))
			(void)strcpy(buffer, "GET /index. html");
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
		{
			logger(NOTFOUND, "failed to open file", &buffer[5], fd);
		}
		logger(LOG, "send", &buffer[5], hit);
		len = (long)lseek(file_fd, (off_t)0, SEEK_END); /* 使用lseek 获得文件长度,该方法比较低效*/
		(void)lseek(file_fd, (off_t)0, SEEK_SET);		/* 想想还有什么方法可获取*/
		(void)sprintf(buffer, "http/1.1 200 ok\nserver: nweb/%d.0\ncontent-length:%ld\nconnection: close\ncontent-type: %s\n\n", VERSION, len, fstr);
		logger(LOG, "header", buffer, hit);
		gettimeofday(&wsst, NULL);
		(void)write(fd, buffer, strlen(buffer));
		gettimeofday(&wsend, NULL);
		wsoc += (wsend.tv_sec - wsst.tv_sec) * 1000.0 + (wsend.tv_usec - wsst.tv_usec) / 1000.0;

		gettimeofday(&rwst, NULL);
		ret = read(file_fd, buffer, BUFSIZE);
		gettimeofday(&rwend, NULL);
		rweb += (rwend.tv_sec - rwst.tv_sec) * 1000.0 + (rwend.tv_usec - rwst.tv_usec) / 1000.0;

		while (ret > 0)
		{
			gettimeofday(&wsst, NULL);
			(void)write(fd, buffer, ret);
			gettimeofday(&wsend, NULL);
			wsoc += (wsend.tv_sec - wsst.tv_sec) * 1000.0 + (wsend.tv_usec - wsst.tv_usec) / 1000.0;

			gettimeofday(&rwst, NULL);
			ret = read(file_fd, buffer, BUFSIZE);
			gettimeofday(&rwend, NULL);
			rweb += (rwend.tv_sec - rwst.tv_sec) * 1000.0 + (rwend.tv_usec - rwst.tv_usec) / 1000.0;
		}
		usleep(10000); /*在socket 通道关闭前,留出发送一段信息的时间*/
		close(file_fd);
	}
	close(fd);
	// 释放内存
	free(param);
	gettimeofday(&hend, NULL);
	hit_t += (hend.tv_sec - hst.tv_sec) * 1000.0 + (hend.tv_usec - hst.tv_usec) / 1000.0;
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr;	 /* static = initialised to zeros */
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

	// if (fork() != 0)
	// 	return 0;
	// (void)signal(SIGCLD, SIG_IGN);
	// (void)signal(SIGHUP, SIG_IGN);
	// for (i = 0; i < 32; i++)
	// 	(void)close(i);
	// (void)setpgrp();
	// logger(LOG, "nweb starting", argv[1], getpid());

	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		logger(ERROR, "system call", "socket", 0);
	port = atoi(argv[1]);
	if (port < 0 || port > 60000)
		logger(ERROR, "Invalid port number (try 1->60000)", argv[1], 0);

	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	pthread_t pth;

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
		// printf函数放在for循环最后的话前几次会因为线程没进行完直接打印，所以结果是0
		printf("共用 %.3fms 成功处理 %d 个客户端请求，其中\n", hit_t, hit);
		printf("平均每个客户端完成请求处理时间为 %.3fms\n", hit_t / hit);
		printf("平均每个客户端完成读 socket 时间为 %.3fms\n", rsoc / hit);
		printf("平均每个客户端完成写 socket 时间为 %.3fms\n", wsoc / hit);
		printf("平均每个客户端完成读网页数据时间为 %.3fms\n", rweb / hit);
		printf("平均每个客户端完成写日志数据时间为 %.3fms\n", wlog / hit);
		printf("\n");
	}
}