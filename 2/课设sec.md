## 实验二

#### 计科21-2    2021011587    吴维皓



#### 题目一

```C
//多进程处理用户端请求
	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);

		pid_t pid = fork();

		if (pid == 0)
		{
			web(socketfd, hit); // 子进程处理网页请求
			exit(EXIT_SUCCESS);
		}
		else if (pid > 0)
		{
			close(socketfd);
		}
		else
		{
			perror("fork");
			exit(EXIT_FAILURE);
		}
	}

```



#### 题目二

```C
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

struct shared_data
{
	double total_time;
};
struct timeval start, end;

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

	/*根据消息类型，将消息放入 logbuffer 缓存，或直接将消息通过 socket 通道返回给客户端*/ switch (type)
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
	/* 将 logbuffer 缓存中的消息存入 webserver.log 文件*/
	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
}

/* 此函数完成了 WebServer 主要功能，它首先解析客户端发送的消息，然后从中获取客户端请求的文件名，然后根据文件名从本地将此文件读入缓存，并生成相应的 HTTP 响应消息；最后通过服务器与客户端的 socket 通道向客户端返回 HTTP 响应消息*/

void web(int fd, int hit)
{
	int j, file_fd, buflen;
	long i, ret, len;
	char *fstr;
	static char buffer[BUFSIZE + 1]; /* 设置静态缓冲区 */
	ret = read(fd, buffer, BUFSIZE); /* 从连接通道中读取客户端的请求消息 */
	if (ret == 0 || ret == -1)
	{ // 如果读取客户端消息失败，则向客户端发送 HTTP 失败响应信息 logger(FORBIDDEN,"failed to read browser request","",fd);
	}
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
		/* 如果请求消息中没有包含有效的文件名，则使用默认的文件名 index.html */ (void)strcpy(buffer, "GET /index.html");

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
	(void)write(fd, buffer, strlen(buffer));

	/* 不停地从文件里读取文件内容，并通过 socket 通道向客户端返回文件内容*/
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		(void)write(fd, buffer, ret);
	}
	sleep(1); /* sleep 的作用是防止消息未发出，已经将此 socket 通道关闭*/
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
	if (listen(listenfd, 64) < 0) // 开始侦听socket连接，最大连接数为64
		logger(ERROR, "system call", "listen", 0);

	shm_unlink(SHARED_MEMORY_NAME);																					   // 删除共享内存对象，如果之前的程序是被强制退出，则共享内存中的数据会保留
	int shm_fd = shm_open(SHARED_MEMORY_NAME, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);									   // 创建共享内存
	ftruncate(shm_fd, sizeof(struct shared_data));																	   // 设置共享内存大小
	struct shared_data *sdata = mmap(NULL, sizeof(struct shared_data), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0); // 建立共享内存映射

	sem_unlink(SEMAPHORE_NAME); // 删除信号量对象
	sem_t *mutex;				// 创建信号量
	mutex = sem_open(SEMAPHORE_NAME, O_CREAT, S_IRUSR | S_IWUSR, 1);

	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);

		pid_t pid = fork();

		if (pid == 0)
		{

			gettimeofday(&start, NULL);
			web(socketfd, hit); // 子进程处理网页请求
			gettimeofday(&end, NULL);
			double tfly = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
			printf(" 进程:%d 执行时间:%.3f ms\n", getpid(), tfly);

			sem_wait(mutex); // 等待信号量
			sdata->total_time += tfly;
			printf("当前所有子进程消耗时间为:%.3f ms\n", sdata->total_time);
			sem_post(mutex); // 释放信号量

			exit(EXIT_SUCCESS);
		}
		else if (pid > 0)
		{
			close(socketfd);
		}
		else
		{
			perror("fork");
			exit(EXIT_FAILURE);
		}
	}
}

```

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231117145238609.png" alt="image-20231117145238609" style="zoom:80%;" />









#### 题目三：

##### http_load测试与分析

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231117152046321.png" alt="image-20231117152046321" style="zoom:80%;" />

- 单进程

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231117152108692.png" alt="image-20231117152108692" style="zoom:80%;" />

- 多进程



分析：

1. 每秒响应数量提升：多进程模型下，每秒的响应数量是单进程的1.5倍。这表明多进程能够更有效地处理客户端的网页请求，实现并发处理，提高系统的响应速度。
2. 字节传输量大幅提升：每秒字节传输量是单进程的100倍。这说明多进程架构能够更有效地利用系统资源，提高数据传输效率，从而加速信息交换。
3. 建立请求连接的平均时间优化：多进程模型中，建立请求连接的平均时间比单进程快1倍。这表明多进程的并发处理能力有助于更迅速地建立客户端与服务器之间的连接，提高系统的连接响应速度。
4. 接受服务器第一个响应消息的平均时间显著优化：多进程模型中，接受服务器第一个响应消息的平均时间比单进程快6000倍。这巨大的性能提升表明多进程能够显著减少等待时间，快速响应客户端的首次请求，从而提升用户体验。

性能提升的原因在于多进程Web服务允许子进程并发处理客户端网页请求。在单进程模型中，每个循环只能完成一次网页访问，而多进程模型通过将处理任务交给子进程，实现并发处理。虽然父进程每次循环仍然只完成一次网页访问，但由于并发处理，循环的速度显著提高，无需等待上一次网页访问的完成就能迅速继续监听端口。这有效地提高了系统的吞吐量和响应速度。



##### 性能瓶颈：

1. 子进程的创建和销毁：每次接受到新的连接时，都会创建一个子进程来处理请求。频繁地创建和销毁子进程可能会影响性能，尤其是在并发连接较多的情况下
2. 共享内存和信号量的使用：在多进程环境中，使用共享内存和信号量可能会引入竞争条件；共享内存和信号量的使用会带来一定的进程间通信开销



##### 优化：

1. 使用进程池技术，每次启动的“子进程”从池子里拿，而不是父进程创建，init进程销毁

   - 进程池的作用主要有以下几点：

     - 提高性能：通过预先创建一定数量的进程，可以避免频繁地创建和关闭进程，从而减少系统资源的消耗，提高任务处理效率

     - 并发处理：进程池中的进程可以同时处理多个任务，从而提高了程序的并发处理能力

     - 资源复用：当任务处理完成后，进程可以被放回进程池中等待下一个任务的到来，从而实现了资源的复用

     - 系统稳定性：通过合理地管理和分配进程池中的进程，可以保证系统的稳定性和可靠性

2. 使用轻量级进程间通信方式，管道或消息队列

   - 管道通信优点：
     - 数据传输有序：管道通信可以保证数据的顺序传输（本题中一个进程写，接着立刻读即可）
     - 开销较低：管道通信不需要创建额外的数据结构，只需要使用系统提供的文件描述符即可





