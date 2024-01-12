#include "head.h"

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

readmsgpool *readmsg_pool;
readfilepool *readfile_pool;
sendmsgpool *sendmsg_pool;
hashtable *table;

off_t getSize(int file_fd)
{
	struct stat st;
	if (fstat(file_fd, &st) == 0)
		return st.st_size;
	return -1;
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

	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		// 写日志文件
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}
}

void *read_msg(void *data)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	webparam *param1 = (webparam *)data;
	int fd = param1->fd, hit = param1->hit;
	int j;
	long i, ret;
	char buffer[BUFSIZE + 1]; /* 缓存 */

	ret = read(fd, buffer, BUFSIZE); // 从socket 读取 Web 请求内容(读socket)
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
		for (i = 0; i < ret; i++)
			if (buffer[i] == '\r' || buffer[i] == '\n')
				buffer[i] = '*';
		logger(LOG, "request", buffer, hit);
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
		for (j = 0; j < i - 1; j++)
			if (buffer[j] == '.' && buffer[j + 1] == '.')
			{
				logger(FORBIDDEN, "parent directory (..) path names not supported", buffer, fd);
			}
		if (!strncmp(&buffer[0], "GET /\0", 6))
			(void)strcpy(buffer, "GET /index. html");

		filename_node *curtask = (filename_node *)malloc(sizeof(filename_node));
		curtask->next = NULL;
		curtask->function = read_file;
		strcpy(param1->buffer, buffer);
		curtask->arg = (void *)param1;
		push_fnamequeue(&readfile_pool->queue, curtask);
	}
	gettimeofday(&end, NULL);
	pthread_mutex_lock(&table->lock);
	table->alltime += (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
	pthread_mutex_unlock(&table->lock);
	return NULL;
}

void *read_file(void *data)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	webparam *param1 = (webparam *)data;
	int fd = param1->fd, hit = param1->hit;
	int file_fd, buflen;
	long i;
	long long len;
	char *fstr;
	char buffer[BUFSIZE + 1];
	char key[8096];
	strcpy(buffer, param1->buffer);
	strcpy(key, &param1->buffer[5]);

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

	msg_node *curtask = (msg_node *)malloc(sizeof(msg_node));
	curtask->next = NULL;
	curtask->function = send_msg;

	content *cont = getContentByKey(table, key);
	if (cont == NULL)
	{
		if ((file_fd = open(&buffer[5], O_RDONLY)) == -1) // &buffer[5] 表示从 buffer 字符数组的第五个元素开始的地址，即文件路径的起始位置
		{
			logger(NOTFOUND, "failed to open file", &buffer[5], fd);
		}
		logger(LOG, "send", &buffer[5], hit);
		len = (long long)getSize(file_fd); // 获取文件长度

		cont = malloc(sizeof(content));
		cont->length = len;
		cont->address = malloc(len);
		read(file_fd, cont->address, len);
		addItem(table, key, cont);
		close(file_fd);
	}
	else
	{
		pthread_mutex_lock(&table->lock);
		table->boom++;
		pthread_mutex_unlock(&table->lock);
	}
	(void)sprintf(buffer, "http/1.1 200 ok\nserver: nweb/%d.0\ncontent-length:%lld\nconnection: close\ncontent-type: %s\n\n", VERSION, cont->length, fstr);
	logger(LOG, "header", buffer, hit);
	(void)write(fd, buffer, strlen(buffer)); // 响应头直接发

	param1->message = (char *)cont->address;
	param1->len = cont->length;
	param1->hit = hit;
	param1->fd = fd;
	curtask->arg = (void *)param1;
	push_msgqueue(&sendmsg_pool->queue, curtask);
	gettimeofday(&end, NULL);
	pthread_mutex_lock(&table->lock);
	table->alltime += (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
	pthread_mutex_unlock(&table->lock);
	return NULL;
}

void *send_msg(void *data)
{
	struct timeval start, end;
	gettimeofday(&start, NULL);
	webparam *param1 = (webparam *)data;
	(void)write(param1->fd, param1->message, param1->len);
	gettimeofday(&end, NULL);
	pthread_mutex_lock(&table->lock);
	table->alltime += (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
	printf("\n缓存命中率：%0.3lf\n", table->boom / table->total);
	printf("客户端获得请求内容的平均时间：%0.3lf\n", table->alltime / table->total);
	pthread_mutex_unlock(&table->lock);
	close(param1->fd);
	free(param1);
	return NULL;
}

int main(int argc, char **argv)
{
	int i, port, listenfd, socketfd, hit;
	socklen_t length;
	static struct sockaddr_in cli_addr;
	static struct sockaddr_in serv_addr;
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
	readmsg_pool = initreadmsgpool(100);
	readfile_pool = initreadfilepool(100);
	sendmsg_pool = initsendmsgpool(250);
	table = createHashTable(HASHCOUNT);
	for (hit = 1;; hit++)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			logger(ERROR, "system call", "accept", 0);

		readmsg_node *curtask = (readmsg_node *)malloc(sizeof(readmsg_node));
		curtask->next = NULL;
		curtask->function = read_msg;

		webparam *param = (webparam *)malloc(sizeof(webparam));
		param->hit = hit;
		param->fd = socketfd;
		curtask->arg = (void *)param;
		pthread_mutex_lock(&table->lock);
		table->total++;
		pthread_mutex_unlock(&table->lock);
		push_rdmsgqueue(&readmsg_pool->queue, curtask);
	}
	freeHashTable(table);
	destoryreadmsgpool(readmsg_pool);
	destoryreadfilepool(readfile_pool);
	destorysendmsgpool(sendmsg_pool);
	return 0;
}