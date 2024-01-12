#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
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
#include <sys/prctl.h>
#include <stdbool.h>
#include <sys/time.h>
#include "head.h"

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

readmsgpool *readmsg_pool;
readfilepool *readfile_pool;
sendmsgpool *sendmsg_pool;

int max_rmth, max_rfth, max_smth;
int min_rmth = 1000, min_rfth = 1000, min_smth = 1000; // 其实只要设一个比线程池容量大的数即可
double act_rm, act_rf, act_sm;						   // 各个线程池中线程的总活跃时间
double blc_rm, blc_rf, blc_sm;

inline int max(int x, int y)
{
	return x > y ? x : y;
}

inline int min(int x, int y)
{
	return x < y ? x : y;
}

void push_rdmsgqueue(readmsg_queue *queue, readmsg_node *newtask)
{
	readmsg_node *node = (readmsg_node *)malloc(sizeof(readmsg_node));
	if (!node)
	{
		perror("Error allocating memory for new task");
		exit(EXIT_FAILURE);
	}

	// 将任务信息复制到新节点
	node->function = newtask->function;
	node->arg = newtask->arg;
	node->next = NULL;

	// 加锁，修改任务队列
	pthread_mutex_lock(&queue->mutex);

	if (queue->len == 0)
	{
		// 队列为空，直接添加新任务
		queue->front = node;
		queue->rear = node;
	}
	else
	{
		// 否则将新任务添加到队列尾部
		queue->rear->next = node;
		queue->rear = node;
	}
	queue->len++;

	// 通知等待在队列上的线程，有新任务到来
	pthread_cond_signal(&queue->has_jobs->cond);

	pthread_mutex_unlock(&queue->mutex);
}

void init_rdmsgqueue(readmsg_queue *queue)
{
	// 初始化互斥量(互斥访问任务队列)
	pthread_mutex_init(&queue->mutex, NULL);

	// 初始化条件变量(在队列为空时阻塞等待任务的到来)
	queue->has_jobs = (rmstaconv *)malloc(sizeof(rmstaconv));
	pthread_cond_init(&queue->has_jobs->cond, NULL);

	// 初始化任务队列
	queue->front = NULL;
	queue->rear = NULL;
	queue->len = 0;
}

readmsg_node *take_rdmsgqueue(readmsg_queue *queue) // 取出队首任务，并在队列中删除该任务
{
	// 加锁，访问任务队列
	pthread_mutex_lock(&queue->mutex);

	// 如果队列为空，等待任务到来
	while (queue->len == 0)
	{
		pthread_cond_wait(&queue->has_jobs->cond, &queue->mutex);
	}

	readmsg_node *curtask = queue->front; // 取出队首任务
	queue->front = curtask->next;		  // 更新队列头指针,指向下一个任务
	if (queue->len == 1)				  // 如果队列只有一个任务，更新尾指针
	{
		queue->rear = NULL;
	}
	queue->len--;

	// 解锁互斥量
	pthread_mutex_unlock(&queue->mutex);

	return curtask;
}

void destory_rdmsgqueue(readmsg_queue *queue)
{

	pthread_mutex_lock(&queue->mutex); // 加锁，访问任务队列

	while (queue->front != NULL) // 释放队列中所有任务节点
	{
		readmsg_node *curtask = queue->front;
		queue->front = curtask->next;
		free(curtask);
	}
	free(queue->has_jobs); // 释放条件变量

	pthread_mutex_unlock(&queue->mutex);

	pthread_mutex_destroy(&queue->mutex); // 销毁互斥量
}

struct readmsgpool *initreadmsgpool(int num_threads)
{
	readmsgpool *pool;
	pool = (readmsgpool *)malloc(sizeof(struct readmsgpool));
	pool->num_threads = 0;
	pool->num_working = 0;
	pool->is_alive = 1;
	pthread_mutex_init(&(pool->thcount_lock), NULL);									 // 初始化互斥量
	pthread_cond_init(&(pool->thread_all_idle), NULL);									 // 初始化条件变量
	init_rdmsgqueue(&pool->queue);														 // 初始化任务队列@
	pool->threads = (struct rmthread **)malloc(num_threads * sizeof(struct rmthread *)); // 创建线程数组

	int i;
	for (i = 0; i < num_threads; i++)
	{
		create_rmthread(pool, &pool->threads[i], i); // 在pool->threads[i]前加了个&
	}
	// 每个线程在创建时,运行函数都会进行pool->num_threads++操作
	while (pool->num_threads != num_threads) // 忙等待，等所有进程创建完毕才返回
	{
	}
	return pool;
}

void waitreadmsgpool(readmsgpool *pool)
{
	pthread_mutex_lock(&pool->thcount_lock);
	while (pool->queue.len || pool->num_working) // 这里可能有问题？
	{
		pthread_cond_wait(&pool->thread_all_idle, &pool->thcount_lock);
	}
	pthread_mutex_unlock(&pool->thcount_lock);
}

void destoryreadmsgpool(readmsgpool *pool)
{
	// 等待线程执行完任务队列中的所有任务，并且任务队列为空 @
	waitreadmsgpool(pool);
	pool->is_alive = 0;									 // 关闭线程池运行
	pthread_cond_broadcast(&pool->queue.has_jobs->cond); // 唤醒所有等待在任务队列上的线程(让它们检查 is_alive 的状态并退出)

	destory_rdmsgqueue(&pool->queue); // 销毁任务队列 @

	// 销毁线程指针数组，并释放为线程池分配的内存 @
	int i;
	for (i = 0; i < pool->num_threads; i++)
	{
		free(pool->threads[i]);
	}
	free(pool->threads);

	// 销毁线程池的互斥量和条件变量
	pthread_mutex_destroy(&pool->thcount_lock);
	pthread_cond_destroy(&pool->thread_all_idle);

	// 释放线程池结构体内存
	free(pool);
}

void *rmthread_do(struct rmthread *pthread)
{
	// 设置线程名称
	char thread_name[128] = {0};
	sprintf(thread_name, "thread-pool-%d", pthread->id);

	prctl(PR_SET_NAME, thread_name);

	// 获得/绑定线程池
	readmsgpool *pool = pthread->pool;

	pthread_mutex_lock(&pool->thcount_lock);
	pool->num_threads++; // 对创建线程数量进程统计@
	pthread_mutex_unlock(&pool->thcount_lock);

	while (pool->is_alive)
	{
		// 如果队列中还有任务，则继续运行；否则阻塞 @
		struct timeval start1, end1;
		gettimeofday(&start1, NULL);
		pthread_mutex_lock(&pool->queue.mutex);
		while (pool->queue.len == 0 && pool->is_alive)
		{
			pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
		}
		pthread_mutex_unlock(&pool->queue.mutex);
		gettimeofday(&end1, NULL);

		if (pool->is_alive)
		{
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working++; // 对工作线程数量进行统计@
			pthread_mutex_unlock(&pool->thcount_lock);

			// 取任务队列队首，并执行
			int aveth, max_, min_; // 设置局部变量保证线程线程安全
			double totala, totalb;
			struct timeval start2, end2;
			void *(*func)(void *);
			void *arg;
			readmsg_node *curtask = take_rdmsgqueue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_taskqueue）
			if (curtask)
			{
				func = curtask->function;
				arg = curtask->arg;
				gettimeofday(&start2, NULL);
				func(arg); // 执行任务
				gettimeofday(&end2, NULL);

				act_rm += (end2.tv_sec - start2.tv_sec) * 1000.0 + (end2.tv_usec - start2.tv_usec) / 1000.0;
				totala = act_rm; // 保证下面打印时输出的是当前的时间
				blc_rm += (end1.tv_sec - start1.tv_sec) * 1000.0 + (end1.tv_usec - start1.tv_usec) / 1000.0;
				totalb = blc_rm;
				max_rmth = max(max_rmth, pool->num_working);
				max_ = max_rmth;
				min_rmth = min(min_rmth, pool->num_working);
				min_ = min_rmth;
				aveth = (max_ + min_) / 2;

				pthread_mutex_lock(&pool->thcount_lock);
				printf("\nrmqueue 当前长度为：%d\n", pool->queue.len + 1); // 加上被取出的1个
				printf("rmpool中线程的平均活跃时间：%.3fms\n", totala / aveth);
				printf("rmpool中线程的平均阻塞时间：%.3fms\n", totalb / aveth);
				printf("rmpool中线程的最高活跃数量：%d\n", max_);
				printf("rmpool中线程的最低活跃数量：%d\n", min_);
				printf("rmpool中线程的平均活跃数量：%d\n", aveth);
				pthread_mutex_unlock(&pool->thcount_lock);

				free(arg);	   // 释放参数
				free(curtask); // 释放任务
			}
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working--;
			pthread_mutex_unlock(&pool->thcount_lock);
			// 当工作线程数量为0时，表示任务全部完成，此时运行阻塞在waitreadmsgpool上的线程 @
			if (pool->num_working == 0 && pool->queue.len == 0)
				pthread_cond_signal(&pool->thread_all_idle);
		}
	}

	// 线程执行完任务将要退出，需改变线程池中的线程数量 @
	pthread_mutex_unlock(&pool->thcount_lock);
	pool->num_threads--;
	pthread_mutex_unlock(&pool->thcount_lock);
	return NULL;
}

int create_rmthread(struct readmsgpool *pool, struct rmthread **pthread, int id)
{
	*pthread = (struct rmthread *)malloc(sizeof(struct rmthread));
	if (*pthread == NULL)
	{
		perror("create_thread(): Could not allocate memory for thread\n");
		return -1;
	}

	// 设置该线程的属性
	(*pthread)->pool = pool;
	(*pthread)->id = id;

	pthread_create(&(*pthread)->pthread, NULL, (void *)rmthread_do, (*pthread)); // 创建线程
	pthread_detach((*pthread)->pthread);										 // 线程分离
	return 0;
}

void push_fnamequeue(filename_queue *queue, filename_node *newtask)
{
	filename_node *node = (filename_node *)malloc(sizeof(filename_node));
	if (!node)
	{
		perror("Error allocating memory for new task");
		exit(EXIT_FAILURE);
	}

	// 将任务信息复制到新节点
	node->function = newtask->function;
	node->arg = newtask->arg;
	node->next = NULL;

	// 加锁，修改任务队列
	pthread_mutex_lock(&queue->mutex);

	if (queue->len == 0)
	{
		// 队列为空，直接添加新任务
		queue->front = node;
		queue->rear = node;
	}
	else
	{
		// 否则将新任务添加到队列尾部
		queue->rear->next = node;
		queue->rear = node;
	}
	queue->len++;

	// 通知等待在队列上的线程，有新任务到来
	pthread_cond_signal(&queue->has_jobs->cond);

	pthread_mutex_unlock(&queue->mutex);
}

void init_fnamequeue(filename_queue *queue)
{
	// 初始化互斥量(互斥访问任务队列)
	pthread_mutex_init(&queue->mutex, NULL);

	// 初始化条件变量(在队列为空时阻塞等待任务的到来)
	queue->has_jobs = (rfstaconv *)malloc(sizeof(rfstaconv));
	pthread_cond_init(&queue->has_jobs->cond, NULL);

	// 初始化任务队列
	queue->front = NULL;
	queue->rear = NULL;
	queue->len = 0;
}

filename_node *take_fnamequeue(filename_queue *queue) // 取出队首任务，并在队列中删除该任务
{
	// 加锁，访问任务队列
	pthread_mutex_lock(&queue->mutex);

	// 如果队列为空，等待任务到来
	while (queue->len == 0)
	{
		pthread_cond_wait(&queue->has_jobs->cond, &queue->mutex);
	}

	filename_node *curtask = queue->front; // 取出队首任务
	queue->front = curtask->next;		   // 更新队列头指针,指向下一个任务
	if (queue->len == 1)				   // 如果队列只有一个任务，更新尾指针
	{
		queue->rear = NULL;
	}
	queue->len--;

	// 解锁互斥量
	pthread_mutex_unlock(&queue->mutex);

	return curtask;
}

void destory_fnamequeue(filename_queue *queue)
{

	pthread_mutex_lock(&queue->mutex); // 加锁，访问任务队列

	while (queue->front != NULL) // 释放队列中所有任务节点
	{
		filename_node *curtask = queue->front;
		queue->front = curtask->next;
		free(curtask);
	}
	free(queue->has_jobs); // 释放条件变量

	pthread_mutex_unlock(&queue->mutex);

	pthread_mutex_destroy(&queue->mutex); // 销毁互斥量
}

struct readfilepool *initreadfilepool(int num_threads)
{
	readfilepool *pool;
	pool = (readfilepool *)malloc(sizeof(struct readfilepool));
	pool->num_threads = 0;
	pool->num_working = 0;
	pool->is_alive = 1;
	pthread_mutex_init(&(pool->thcount_lock), NULL);									 // 初始化互斥量
	pthread_cond_init(&(pool->thread_all_idle), NULL);									 // 初始化条件变量
	init_fnamequeue(&pool->queue);														 // 初始化任务队列@
	pool->threads = (struct rfthread **)malloc(num_threads * sizeof(struct rfthread *)); // 创建线程数组

	int i;
	for (i = 0; i < num_threads; i++)
	{
		create_rfthread(pool, &pool->threads[i], i); // 在pool->threads[i]前加了个&
	}
	// 每个线程在创建时,运行函数都会进行pool->num_threads++操作
	while (pool->num_threads != num_threads) // 忙等待，等所有进程创建完毕才返回
	{
	}
	return pool;
}

void waitreadfilepool(readfilepool *pool)
{
	pthread_mutex_lock(&pool->thcount_lock);
	while (pool->queue.len || pool->num_working) // 这里可能有问题？
	{
		pthread_cond_wait(&pool->thread_all_idle, &pool->thcount_lock);
	}
	pthread_mutex_unlock(&pool->thcount_lock);
}

void destoryreadfilepool(readfilepool *pool)
{
	// 等待线程执行完任务队列中的所有任务，并且任务队列为空 @
	waitreadfilepool(pool);
	pool->is_alive = 0;									 // 关闭线程池运行
	pthread_cond_broadcast(&pool->queue.has_jobs->cond); // 唤醒所有等待在任务队列上的线程(让它们检查 is_alive 的状态并退出)

	destory_fnamequeue(&pool->queue); // 销毁任务队列 @

	// 销毁线程指针数组，并释放为线程池分配的内存 @
	int i;
	for (i = 0; i < pool->num_threads; i++)
	{
		free(pool->threads[i]);
	}
	free(pool->threads);

	// 销毁线程池的互斥量和条件变量
	pthread_mutex_destroy(&pool->thcount_lock);
	pthread_cond_destroy(&pool->thread_all_idle);

	// 释放线程池结构体内存
	free(pool);
}

void *rfthread_do(struct rfthread *pthread)
{
	// 设置线程名称
	char thread_name[128] = {0};
	sprintf(thread_name, "thread-pool-%d", pthread->id);

	prctl(PR_SET_NAME, thread_name);

	// 获得/绑定线程池
	readfilepool *pool = pthread->pool;

	pthread_mutex_lock(&pool->thcount_lock);
	pool->num_threads++; // 对创建线程数量进程统计@
	pthread_mutex_unlock(&pool->thcount_lock);

	while (pool->is_alive)
	{
		// 如果队列中还有任务，则继续运行；否则阻塞 @
		struct timeval start1, end1;
		gettimeofday(&start1, NULL);
		pthread_mutex_lock(&pool->queue.mutex);
		while (pool->queue.len == 0 && pool->is_alive)
		{
			pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
		}
		pthread_mutex_unlock(&pool->queue.mutex);
		gettimeofday(&end1, NULL);

		if (pool->is_alive)
		{
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working++; // 对工作线程数量进行统计@
			pthread_mutex_unlock(&pool->thcount_lock);

			// 取任务队列队首，并执行
			int aveth, max_, min_;
			double totala, totalb;
			struct timeval start2, end2;
			void *(*func)(void *);
			void *arg;
			filename_node *curtask = take_fnamequeue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_fnamequeue）
			if (curtask)
			{
				func = curtask->function;
				arg = curtask->arg;
				gettimeofday(&start2, NULL);
				func(arg); // 执行任务
				gettimeofday(&end2, NULL);

				act_rf += (end2.tv_sec - start2.tv_sec) * 1000.0 + (end2.tv_usec - start2.tv_usec) / 1000.0;
				totala = act_rf;
				blc_rf += (end1.tv_sec - start1.tv_sec) * 1000.0 + (end1.tv_usec - start1.tv_usec) / 1000.0;
				totalb = blc_rf;
				max_rfth = max(max_rfth, pool->num_working);
				max_ = max_rfth;
				min_rfth = min(min_rfth, pool->num_working);
				min_ = min_rfth;
				aveth = (max_ + min_) / 2;

				pthread_mutex_lock(&pool->thcount_lock);
				printf("\nrfqueue 当前长度为：%d\n", pool->queue.len + 1); // 加上被取出的1个
				printf("rfpool中线程的平均活跃时间：%.3fms\n", totala / aveth);
				printf("rfpool中线程的平均阻塞时间：%.3fms\n", totalb / aveth);
				printf("rfpool中线程的最高活跃数量：%d\n", max_);
				printf("rfpool中线程的最低活跃数量：%d\n", min_);
				printf("rfpool中线程的平均活跃数量：%d\n", aveth);
				pthread_mutex_unlock(&pool->thcount_lock);

				free(arg);	   // 释放参数
				free(curtask); // 释放任务
			}
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working--;
			pthread_mutex_unlock(&pool->thcount_lock);
			// 当工作线程数量为0时，表示任务全部完成，此时运行阻塞在waitreadfilepool上的线程 @
			if (pool->num_working == 0 && pool->queue.len == 0)
				pthread_cond_signal(&pool->thread_all_idle);
		}
	}

	// 线程执行完任务将要退出，需改变线程池中的线程数量 @
	pthread_mutex_unlock(&pool->thcount_lock);
	pool->num_threads--;
	pthread_mutex_unlock(&pool->thcount_lock);
	return NULL;
}

int create_rfthread(struct readfilepool *pool, struct rfthread **pthread, int id)
{
	*pthread = (struct rfthread *)malloc(sizeof(struct rfthread));
	if (*pthread == NULL)
	{
		perror("create_thread(): Could not allocate memory for thread\n");
		return -1;
	}

	// 设置该线程的属性
	(*pthread)->pool = pool;
	(*pthread)->id = id;

	pthread_create(&(*pthread)->pthread, NULL, (void *)rfthread_do, (*pthread)); // 创建线程
	pthread_detach((*pthread)->pthread);										 // 线程分离
	return 0;
}

void push_msgqueue(msg_queue *queue, msg_node *newtask)
{
	msg_node *node = (msg_node *)malloc(sizeof(msg_node));
	if (!node)
	{
		perror("Error allocating memory for new task");
		exit(EXIT_FAILURE);
	}

	// 将任务信息复制到新节点
	node->function = newtask->function;
	node->arg = newtask->arg;
	node->next = NULL;

	// 加锁，修改任务队列
	pthread_mutex_lock(&queue->mutex);

	if (queue->len == 0)
	{
		// 队列为空，直接添加新任务
		queue->front = node;
		queue->rear = node;
	}
	else
	{
		// 否则将新任务添加到队列尾部
		queue->rear->next = node;
		queue->rear = node;
	}
	queue->len++;

	// 通知等待在队列上的线程，有新任务到来
	pthread_cond_signal(&queue->has_jobs->cond);

	pthread_mutex_unlock(&queue->mutex);
}

void init_msgqueue(msg_queue *queue)
{
	// 初始化互斥量(互斥访问任务队列)
	pthread_mutex_init(&queue->mutex, NULL);

	// 初始化条件变量(在队列为空时阻塞等待任务的到来)
	queue->has_jobs = (smstaconv *)malloc(sizeof(smstaconv));
	pthread_cond_init(&queue->has_jobs->cond, NULL);

	// 初始化任务队列
	queue->front = NULL;
	queue->rear = NULL;
	queue->len = 0;
}

msg_node *take_msgqueue(msg_queue *queue) // 取出队首任务，并在队列中删除该任务
{
	// 加锁，访问任务队列
	pthread_mutex_lock(&queue->mutex);

	// 如果队列为空，等待任务到来
	while (queue->len == 0)
	{
		pthread_cond_wait(&queue->has_jobs->cond, &queue->mutex);
	}

	msg_node *curtask = queue->front; // 取出队首任务
	queue->front = curtask->next;	  // 更新队列头指针,指向下一个任务
	if (queue->len == 1)			  // 如果队列只有一个任务，更新尾指针
	{
		queue->rear = NULL;
	}
	queue->len--;

	// 解锁互斥量
	pthread_mutex_unlock(&queue->mutex);

	return curtask;
}

void destory_msgqueue(msg_queue *queue)
{

	pthread_mutex_lock(&queue->mutex); // 加锁，访问任务队列

	while (queue->front != NULL) // 释放队列中所有任务节点
	{
		msg_node *curtask = queue->front;
		queue->front = curtask->next;
		free(curtask);
	}
	free(queue->has_jobs); // 释放条件变量

	pthread_mutex_unlock(&queue->mutex);

	pthread_mutex_destroy(&queue->mutex); // 销毁互斥量
}

struct sendmsgpool *initsendmsgpool(int num_threads)
{
	sendmsgpool *pool;
	pool = (sendmsgpool *)malloc(sizeof(struct sendmsgpool));
	pool->num_threads = 0;
	pool->num_working = 0;
	pool->is_alive = 1;
	pthread_mutex_init(&(pool->thcount_lock), NULL);									 // 初始化互斥量
	pthread_cond_init(&(pool->thread_all_idle), NULL);									 // 初始化条件变量
	init_msgqueue(&pool->queue);														 // 初始化任务队列@
	pool->threads = (struct smthread **)malloc(num_threads * sizeof(struct smthread *)); // 创建线程数组

	int i;
	for (i = 0; i < num_threads; i++)
	{
		create_smthread(pool, &pool->threads[i], i); // 在pool->threads[i]前加了个&
	}
	// 每个线程在创建时,运行函数都会进行pool->num_threads++操作
	while (pool->num_threads != num_threads) // 忙等待，等所有进程创建完毕才返回
	{
	}
	return pool;
}

void waitsendmsgpool(sendmsgpool *pool)
{
	pthread_mutex_lock(&pool->thcount_lock);
	while (pool->queue.len || pool->num_working) // 这里可能有问题？
	{
		pthread_cond_wait(&pool->thread_all_idle, &pool->thcount_lock);
	}
	pthread_mutex_unlock(&pool->thcount_lock);
}

void destorysendmsgpool(sendmsgpool *pool)
{
	// 等待线程执行完任务队列中的所有任务，并且任务队列为空 @
	waitsendmsgpool(pool);
	pool->is_alive = 0;									 // 关闭线程池运行
	pthread_cond_broadcast(&pool->queue.has_jobs->cond); // 唤醒所有等待在任务队列上的线程(让它们检查 is_alive 的状态并退出)

	destory_msgqueue(&pool->queue); // 销毁任务队列 @

	// 销毁线程指针数组，并释放为线程池分配的内存 @
	int i;
	for (i = 0; i < pool->num_threads; i++)
	{
		free(pool->threads[i]);
	}
	free(pool->threads);

	// 销毁线程池的互斥量和条件变量
	pthread_mutex_destroy(&pool->thcount_lock);
	pthread_cond_destroy(&pool->thread_all_idle);

	// 释放线程池结构体内存
	free(pool);
}

void *smthread_do(struct smthread *pthread)
{
	// 设置线程名称
	char thread_name[128] = {0};
	sprintf(thread_name, "thread-pool-%d", pthread->id);

	prctl(PR_SET_NAME, thread_name);

	// 获得/绑定线程池
	sendmsgpool *pool = pthread->pool;

	pthread_mutex_lock(&pool->thcount_lock);
	pool->num_threads++; // 对创建线程数量进程统计@
	pthread_mutex_unlock(&pool->thcount_lock);

	while (pool->is_alive)
	{
		// 如果队列中还有任务，则继续运行；否则阻塞 @
		struct timeval start1, end1;
		gettimeofday(&start1, NULL);
		pthread_mutex_lock(&pool->queue.mutex);
		while (pool->queue.len == 0 && pool->is_alive)
		{
			pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
		}
		pthread_mutex_unlock(&pool->queue.mutex);
		gettimeofday(&end1, NULL);

		if (pool->is_alive)
		{
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working++; // 对工作线程数量进行统计@
			pthread_mutex_unlock(&pool->thcount_lock);

			// 取任务队列队首，并执行
			int aveth, max_, min_;
			double totala, totalb;
			struct timeval start2, end2;
			void *(*func)(void *);
			void *arg;
			msg_node *curtask = take_msgqueue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_msgqueue）
			if (curtask)
			{
				func = curtask->function;
				arg = curtask->arg;
				gettimeofday(&start2, NULL);
				func(arg); // 执行任务
				gettimeofday(&end2, NULL);

				act_sm += (end2.tv_sec - start2.tv_sec) * 1000.0 + (end2.tv_usec - start2.tv_usec) / 1000.0;
				totala = act_sm;
				blc_sm += (end1.tv_sec - start1.tv_sec) * 1000.0 + (end1.tv_usec - start1.tv_usec) / 1000.0;
				totalb = blc_sm;
				max_smth = max(max_smth, pool->num_working);
				max_ = max_smth;
				min_smth = min(min_smth, pool->num_working);
				min_ = min_smth;
				aveth = (max_ + min_) / 2;
				pthread_mutex_lock(&pool->thcount_lock);
				printf("\nsmqueue 当前长度为：%d\n", pool->queue.len + 1);
				printf("smpool中线程的平均活跃时间：%.3fms\n", totala / aveth);
				printf("smpool中线程的平均阻塞时间：%.3fms\n", totalb / aveth);
				printf("smpool中线程的最高活跃数量：%d\n", max_);
				printf("smpool中线程的最低活跃数量：%d\n", min_);
				printf("smpool中线程的平均活跃数量：%d\n", aveth);
				pthread_mutex_unlock(&pool->thcount_lock);
				free(curtask); // 释放任务
			}
			pthread_mutex_lock(&pool->thcount_lock);
			pool->num_working--;
			pthread_mutex_unlock(&pool->thcount_lock);
			// 当工作线程数量为0时，表示任务全部完成，此时运行阻塞在waitThreadPool上的线程 @
			if (pool->num_working == 0 && pool->queue.len == 0)
				pthread_cond_signal(&pool->thread_all_idle);
		}
	}

	// 线程执行完任务将要退出，需改变线程池中的线程数量 @
	pthread_mutex_unlock(&pool->thcount_lock);
	pool->num_threads--;
	pthread_mutex_unlock(&pool->thcount_lock);
	return NULL;
}

int create_smthread(struct sendmsgpool *pool, struct smthread **pthread, int id)
{
	*pthread = (struct smthread *)malloc(sizeof(struct smthread));
	if (*pthread == NULL)
	{
		perror("create_thread(): Could not allocate memory for thread\n");
		return -1;
	}

	// 设置该线程的属性
	(*pthread)->pool = pool;
	(*pthread)->id = id;

	pthread_create(&(*pthread)->pthread, NULL, (void *)smthread_do, (*pthread)); // 创建线程
	pthread_detach((*pthread)->pthread);										 // 线程分离
	return 0;
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

		webparam *param2 = (webparam *)malloc(sizeof(webparam));
		param2->hit = hit;
		param2->fd = fd;
		strcpy(param2->buffer, buffer);
		curtask->arg = (void *)param2;
		push_fnamequeue(&readfile_pool->queue, curtask);
	}

	// free(data);
	return NULL;
}

void *read_file(void *data)
{
	webparam *param1 = (webparam *)data;
	int fd = param1->fd, hit = param1->hit;
	int file_fd, buflen;
	long i, ret, len;
	char *fstr;
	char buffer[BUFSIZE + 1];
	strcpy(buffer, param1->buffer);

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
	(void)lseek(file_fd, (off_t)0, SEEK_SET);		/* 想想还有什么方法可获取*/
	(void)sprintf(buffer, "http/1.1 200 ok\nserver: nweb/%d.0\ncontent-length:%ld\nconnection: close\ncontent-type: %s\n\n", VERSION, len, fstr);
	logger(LOG, "header", buffer, hit);
	(void)write(fd, buffer, strlen(buffer)); // 响应头直接发

	while ((ret = read(file_fd, buffer, BUFSIZE)) > 0)
	{
		msg_node *curtask = (msg_node *)malloc(sizeof(msg_node));
		curtask->next = NULL;
		curtask->function = send_msg;

		webparam *param2 = malloc(sizeof(webparam));
		param2->hit = hit;
		param2->fd = fd;
		memcpy(param2->buffer, buffer, BUFSIZE); // 这里复制数据不能用strcpy，strcpy遇到0会停
		param2->ret = ret;
		curtask->arg = (void *)param2;
		push_msgqueue(&sendmsg_pool->queue, curtask);
	}
	close(file_fd);
	usleep(10000);
	close(fd); // 这里为什么要关闭呢？不是还有其他线程需要往这个端口发送数据吗？
	return NULL;
}

void *send_msg(void *data)
{
	webparam *param1 = (webparam *)data;
	int fd = param1->fd;
	char *message = param1->buffer;
	long ret = param1->ret;
	pthread_mutex_lock(&sendmsg_pool->queue.mutex);
	(void)write(fd, message, ret); // 写socket
	pthread_mutex_unlock(&sendmsg_pool->queue.mutex);
	// close(fd); // 这里为什么要关闭呢？不是还有其他线程需要往这个端口发送数据吗？确实不能在这关，要等数据全部发完才能关
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
	sendmsg_pool = initsendmsgpool(200);
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
		push_rdmsgqueue(&readmsg_pool->queue, curtask);
	}
	destoryreadmsgpool(readmsg_pool);
	destoryreadfilepool(readfile_pool);
	destorysendmsgpool(sendmsg_pool);
	return 0;
}