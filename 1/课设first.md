## 实验报告一

#### 计科21-2     2021011587     吴维皓



#### 	题目一：

```makefile
#makefile文件
CC = gcc
CFLAGS = -Wall -Wextra -O2

webserver: webserver.c
	$(CC) $(CFLAGS) -o webserver webserver.c

clean:
	rm -f webserver

```

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108102851663.png" alt="image-20231108102851663" style="zoom:80%;" />

![image-20231115162849302](C:\Users\11927\Desktop\N\Note\Photo\image-20231115162849302.png)

​																							make后生成的可执行文件

















#### 题目二：

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231114212825503.png" alt="image-20231114212825503" style="zoom:80%;" />

​																								webserver.c 源代码



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108103525556.png" alt="image-20231108103525556" style="zoom:80%;" />

​																						运行webserver 并指定端口为8011



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231114212914326.png" alt="image-20231114212914326" style="zoom:80%;" />

​																										成功进入网页



```log
//加载一次网址会产生3条log
:
 INFO: request:GET /index.html HTTP/1.1**Host: 192.168.88.132:8011**Connection: keep-alive**Cache-Control: max-age=0**Upgrade-Insecure-Requests: 1**User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36**Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,image/apng,*/*;q=0.8,application/signed-exchange;v=b3;q=0.7**Accept-Encoding: gzip, deflate**Accept-Language: zh-CN,zh;q=0.9,en;q=0.8****:1
 INFO: SEND:index.html:1
 INFO: Header:HTTP/1.1 200 OK
Server: nweb/23.0
Content-Length: 391
Connection: close
Content-Type: text/html

:1
 INFO: request:GET /example.jpg HTTP/1.1**Host: 192.168.88.132:8011**Connection: keep-alive**User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36**Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8**Referer: http://192.168.88.132:8011/index.html**Accept-Encoding: gzip, deflate**Accept-Language: zh-CN,zh;q=0.9,en;q=0.8****:2
NOT FOUND: failed to open file:example.jpg
 INFO: SEND:example.jpg:2
 INFO: Header:HTTP/1.1 200 OK
Server: nweb/23.0
Content-Length: -1
Connection: close
Content-Type: image/jpg

:2
 INFO: request:GET /favicon.ico HTTP/1.1**Host: 192.168.88.132:8011**Connection: keep-alive**User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/117.0.0.0 Safari/537.36**Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,*/*;q=0.8**Referer: http://192.168.88.132:8011/index.html**Accept-Encoding: gzip, deflate**Accept-Language: zh-CN,zh;q=0.9,en;q=0.8****:3
NOT FOUND: failed to open file:favicon.ico
 INFO: SEND:favicon.ico:3
 INFO: Header:HTTP/1.1 200 OK
Server: nweb/23.0
Content-Length: -1
Connection: close
Content-Type: image/ico
```

- 解释：从INFO字段可以看出，这3条log分别申请访问了3个资源，分别是：index.html、example.jpg、favicon.ico，也就是需要从服务器段加载相应的资源产生了这些记录。
- 为加快 html 网页显示的速度，采用的技术：
  - 缓存：通过设置缓存头，告诉浏览器在一段时间内重复访问同一资源时，直接使用缓存中的内容，而不需要再次向服务器请求
  - 优化数据库查询：如果网页需要从数据库中获取数据，可以通过优化查询语句、使用索引等方式来提高数据库查询速度
  - 压缩传输内容：使用 Gzip 或 Deflate 等压缩算法对传输的内容进行压缩，以减少传输数据量，提高加载速度
  - 使用内容分发网络(CDN)，减少服务器响应时间，提高加载速度
  - 减少 HTTP 请求次数：可以通过合并 CSS 和 JavaScript 文件、使用 Sprites 技术等方式来减少 HTTP 请求次数
  - 优化图片大小：对图片进行压缩和优化，以减少页面加载时间
  - 负载均衡：如果网站流量较大，可以使用负载均衡技术将流量分发到多个服务器上，以提高网站的性能和可用性



#### 题目三：

```C
	//生成一个当前系统时间的变量
	char time_now[255];
	time_t rawtime;  
    struct tm * timeinfo;  
    time ( &rawtime );  
    timeinfo = localtime ( &rawtime );  
	strftime(time_now, sizeof(time_now), "%Y-%m-%d %H:%M:%S", timeinfo);//将时间变量格式化
```

![image-20231108134303143](C:\Users\11927\Desktop\N\Note\Photo\image-20231108134303143.png)

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108141455467.png" alt="image-20231108141455467" style="zoom: 80%;" />

- 做法：将生成的时间变量加入log函数的每个sprintf中即可





#### 题目四：

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108164815336.png" alt="image-20231108164815336" style="zoom:67%;" />

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108164543468.png" alt="image-20231108164543468" style="zoom:67%;" />

**分析：**结合log文件中的内容可以看出，多次快速刷新页面后，即使有些页面没有完全打开就被刷新掉，但是log文件中仍会有该次的访问数据，也就是说，只要有申请访问信号的到来，一个网页进程将会被创建。随之而来的是资源的加载，这个操作属于I/O操作，耗时相对较长，因为是多次快速刷新，意味着每个进程快速的被创建又被删除，又因为这是个单进程模型，即使每个进程只进行了一次I/O操作，所以排到最后一个进程（也就是最后刷新到的网页）加载资源仍会等待一段时间。





#### 题目五：

##### 1. 性能监测：

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108192159297.png" alt="image-20231108192159297" style="zoom: 80%;" />

- 平均每秒响应量为：0.9999
- 客户端与服务器建立连接平均时间：0.09324 ms
- http请求发出到服务器接收第一个响应平均时间：13571.4 ms



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108194447954.png" alt="image-20231108194447954" style="zoom: 80%;" />

- 在 free 还有剩余的情况下 swpd 大于0，且保持在一个稳定的数，说明这可能是虚拟内存被用于内存映射（将一些必须加载的大型文件的部分映射到虚拟内存中）
- 从 in 可以看出，CPU每秒平均中断次数在500次，最高可到1172次
- 从 cs 可以看出，CPU上下文切换次数平均在800次，最高可达1618次
- 从 us sy id可以看出，用户进程和系统进程占用CPU时间的百分比都很低，最多维持在1%，而大部分时间CPU都处于空闲状态，可以知道，网页程序的运行不会占用太多CPU时间，不是一个CPU密集型程序



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108194503019.png" alt="image-20231108194503019" style="zoom:80%;" />

- rKB/s 和 wKB/s 数值稳定，对应了每次网页打开时加载的资源比较固定（照片和图标），相比与上面的CPU使用情况，webserver程序算是I/O密接型程序



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108194533780.png" alt="image-20231108194533780" style="zoom:80%;" />



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115115303106.png" alt="image-20231115115303106" style="zoom:80%;" />

- 通过观察8011端口的 Recv-Q 和 Send-Q，可以注意到这两个数据量并不大，其中一个甚至为零，表明接收消息的程序以及网络本身都在正常运行，没有出现明显的阻塞或延迟情况



**结论：**

1. 网页平均每秒响应一次请求，且并行性不高

2. 磁盘的读写请求的服务时间不长且利用率不高

3. 网页对服务器的读写请求量小



##### 2. 源码分析：

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108223409858.png" alt="image-20231108223409858" style="zoom:80%;" />

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108223742163.png" alt="image-20231108223742163" style="zoom:80%;" />

**分析：**在webserver中，唯一涉及到的write函数存在于logger和web函数中。结合之前的性能监测，这些I/O操作对系统性能的损害相对较小。由于请求的网页不需要加载大量资源，因此对内存和磁盘数据量的读写并不频繁。然而，由于这些I/O操作的存在，请求服务的时间会有一定的损耗。





<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231108225943710.png" alt="image-20231108225943710" style="zoom:80%;" />

服务端侦听部分，对系统的损耗如下:
- 内存泄漏，该程序没有释放已分配的内存，这可能导致内存泄漏
- 无限循环，没有强制退出则一直占用系统资源
- 单线程处理，只有一个线程处理所有客户端连接，可能导致性能问题





#### 题目六：

##### 1. 添加相关计时函数监测程序各个部分执行时间

```C
//计时函数设计
struct timeval time_s, time_e; //设置全局变量，为timeval结构体的对象

gettimeofday(&time_s, NULL); //获取当前时间
	.
{相关程序部分}
	.
gettimeofday(&time_e, NULL); //获取当前时间

printf(" - {相关程序部分}耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec); //打印输出时间差
```

```C
//webserver.c
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
#include <sys/time.h>
#include <time.h>
#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

// 时间监测
struct timeval time_s, time_e;

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

	char time_now[30]; 
	time_t rawtime;
	struct tm *timeinfo;
	time(&rawtime);//获取当前系统时间
	timeinfo = localtime(&rawtime); //转化成当地时间
	strftime(time_now, sizeof(time_now), "%Y-%m-%d %H:%M:%S", timeinfo); //对时间进行格式化

	/*根据消息类型，将消息放入 logbuffer 缓存，或直接将消息通过 socket 通道返回给客户端*/
	gettimeofday(&time_s, NULL);
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

	/* 将 logbuffer 缓存中的消息存入 webserver.log 文件*/
	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
	gettimeofday(&time_e, NULL);
	printf(" -  - 将 logbuffer 缓存中的消息存入 webserver.log 文件耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);
}

/* 此函数完成了 WebServer 主要功能，它首先解析客户端发送的消息，然后从中获取客户端请求的文件名，然后根据文件名从本地将此文件读入缓存，并生成相应的 HTTP 响应消息；最后通过服务器与客户端的 socket 通道向客户端返回 HTTP 响应消息*/
void web(int fd, int hit)
{
	printf("接收客户端请求\n");
	gettimeofday(&time_s, NULL);
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
	gettimeofday(&time_e, NULL);
	printf(" - 从连接通道中读取客户端的请求消息耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);

	/*判断客户端 HTTP 请求消息是否为 GET 类型，如果不是则给出相应的响应消息*/
	gettimeofday(&time_s, NULL);
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
	gettimeofday(&time_e, NULL);
	printf(" - 判断客户端 HTTP 请求消息类型耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);

	/* 在消息中检测路径，不允许路径中出现“.” */
	gettimeofday(&time_s, NULL);
	for (j = 0; j < i - 1; j++)
		if (buffer[j] == '.' && buffer[j + 1] == '.')
		{
			logger(FORBIDDEN, "Parent directory (..) path names not supported", buffer, fd);
		}
	if (!strncmp(&buffer[0], "GET /\0", 6) || !strncmp(&buffer[0], "get /\0", 6))
		(void)strcpy(buffer, "GET /index.html"); // 如果请求消息中没有包含有效的文件名，则使用默认的文件名 index.html
	gettimeofday(&time_e, NULL);
	printf(" - 检测消息路径耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);

	/* 根据预定义在 extensions 中的文件类型，检查请求的文件类型是否本服务器支持 */
	gettimeofday(&time_s, NULL);
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
	gettimeofday(&time_e, NULL);
	printf(" - 检查请求的文件类型是否本服务器支持耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);

	/* 不停地从文件里读取文件内容，并通过 socket 通道向客户端返回文件内容*/
	gettimeofday(&time_s, NULL);
	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		(void)write(fd, buffer, ret);
	}
	gettimeofday(&time_e, NULL);
	printf(" - 从文件里读取文件内容，并通过 socket 通道向客户端返回耗时：%ld us\n", time_e.tv_usec - time_s.tv_usec);

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
	gettimeofday(&time_s, NULL);
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
	gettimeofday(&time_e, NULL);
	printf(" - 建立服务端侦听耗时：%ld  us\n", time_e.tv_usec - time_s.tv_usec);

	for (hit = 1;; hit++)
	{
		gettimeofday(&time_s, NULL);
		length = sizeof(cli_addr);

		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);
		web(socketfd, hit); /* never returns */
		gettimeofday(&time_e, NULL);
		printf("完成第 %d 次响应请求耗时：%ld  us\n", hit, time_e.tv_usec - time_s.tv_usec);
		printf("\n");
	}
}

```



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231109220954151.png" alt="image-20231109220954151" style="zoom:80%;" />

**分析**：这是一次网页访问程序响应的请求次数。我用 ` - ` 表示一级功能调用，`- - ` 表示二级功能调用，从输出的信息也能大致的反映程序的执行过程和函数调用关系。不过我们的重点是分析时间损耗。

- 建立服务端侦听
  - 涉及大量网络通信操作，网络延迟影响耗时
  - 系统资源紧张，会影响创建socket、绑定地址和端口等操作

- 将日志缓存中的信息写入.log文件

  - 涉及到大量I/O操作和文件的打开/关闭操作

- 接收客户端请求

  - `accept ` 在接收客户端连接请求时受网络延迟影响

- 从连接通道读取客户端请求信息

  - `read` 函数在读取客户端的请求消息时受网络延迟影响

- 从文件里读取内容，并通过 socket 通道向客户端返回

  - `read` 函数涉及网络通信，受网络延迟影响
  - 涉及到文件读取和.log文件写入等I/O操作

  

##### 2. 使用perf工具监测webserver程序

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115162340374.png" alt="image-20231115162340374" style="zoom:80%;" />

- 在CPU上运行时间为9.49 ms，CPU利用率接近0
- 缺页失效次数173次，这与程序中大量的函数调用和循环结构有关



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115154109305.png" alt="image-20231115154109305" style="zoom:80%;" />

- 可以看出，消耗CPU时间较多的函数都是些网络栈中的核心函数，说明程序中网络数据处理方面需要优化



<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115161302687.png" alt="image-20231115161302687" style="zoom:80%;" />

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115161313000.png" alt="image-20231115161313000" style="zoom:80%;" />

<img src="C:\Users\11927\Desktop\N\Note\Photo\image-20231115162048650.png" alt="image-20231115162048650" style="zoom:80%;" />

- 可以看出该程序CPU利用率低，大部分时间CPU都处于空闲状态



**耗时函数：**

- accept()，接收用户请求，受网络延迟影响
- logger()，涉及大量I/O操作
- web()，涉及大量网络通信操作，受网络延迟影响
- read()，读取客户端的请求消息受网络延迟影响
- write()，I/O操作



#### 题目七：

**性能低下原因：**

- 单进程处理客户端请求
- 缺少对程序发生错误时的处理
- I/O操作频繁
- 对网络延迟依赖较高

**解决方法：**

- 采用多进程异步处理编程，每个进程单独处理各自的用户请求
- 多进程编程下，用子进程处理用户请求，并等待子进程处理的返回结果，以便在父进程中处理错误
- 采用缓存机制，将要读/写的内容先存入缓存，一次性完成I/O操作
- 采用预加载机制，在接收网页请求时提前从服务器获取资源，需要时直接读取并渲染，避免等待下载时间
- 采用多级存储层次结构，将频繁使用的资源存在相对CPU较近的地方，减少存取延迟
- 采用更高效的协议（如HTTP/2）来减少网络延迟的影响

