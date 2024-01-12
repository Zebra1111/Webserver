#include "head.h"

readmsgpool *readmsg_pool;
readfilepool *readfile_pool;
sendmsgpool *sendmsg_pool;

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
    queue->front = curtask->next;         // 更新队列头指针,指向下一个任务
    if (queue->len == 1)                  // 如果队列只有一个任务，更新尾指针
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
    pthread_mutex_init(&(pool->thcount_lock), NULL);                                     // 初始化互斥量
    pthread_cond_init(&(pool->thread_all_idle), NULL);                                   // 初始化条件变量
    init_rdmsgqueue(&pool->queue);                                                       // 初始化任务队列@
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
    pool->is_alive = 0;                                  // 关闭线程池运行
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
        pthread_mutex_lock(&pool->queue.mutex);
        while (pool->queue.len == 0 && pool->is_alive)
        {
            pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
        }
        pthread_mutex_unlock(&pool->queue.mutex);

        if (pool->is_alive)
        {
            pthread_mutex_lock(&pool->thcount_lock);
            pool->num_working++; // 对工作线程数量进行统计@
            pthread_mutex_unlock(&pool->thcount_lock);

            // 取任务队列队首，并执行
            void *(*func)(void *);
            void *arg;
            readmsg_node *curtask = take_rdmsgqueue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_taskqueue）
            if (curtask)
            {
                func = curtask->function;
                arg = curtask->arg;
                func(arg);     // 执行任务
                //free(arg);     // 释放参数
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
    pthread_mutex_lock(&pool->thcount_lock);
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
    pthread_detach((*pthread)->pthread);                                         // 线程分离
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
    queue->front = curtask->next;          // 更新队列头指针,指向下一个任务
    if (queue->len == 1)                   // 如果队列只有一个任务，更新尾指针
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
    pthread_mutex_init(&(pool->thcount_lock), NULL);                                     // 初始化互斥量
    pthread_cond_init(&(pool->thread_all_idle), NULL);                                   // 初始化条件变量
    init_fnamequeue(&pool->queue);                                                       // 初始化任务队列@
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
    pool->is_alive = 0;                                  // 关闭线程池运行
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
        pthread_mutex_lock(&pool->queue.mutex);
        while (pool->queue.len == 0 && pool->is_alive)
        {
            pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
        }
        pthread_mutex_unlock(&pool->queue.mutex);
        if (pool->is_alive)
        {
            pthread_mutex_lock(&pool->thcount_lock);
            pool->num_working++; // 对工作线程数量进行统计@
            pthread_mutex_unlock(&pool->thcount_lock);

            // 取任务队列队首，并执行
            void *(*func)(void *);
            void *arg;
            filename_node *curtask = take_fnamequeue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_fnamequeue）
            if (curtask)
            {
                func = curtask->function;
                arg = curtask->arg;
                func(arg);     // 执行任务
                //free(arg);     // 释放参数
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
    pthread_mutex_lock(&pool->thcount_lock);
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
    pthread_detach((*pthread)->pthread);                                         // 线程分离
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
    // pthread_mutex_init(&queue->mutex1, NULL);

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
    queue->front = curtask->next;     // 更新队列头指针,指向下一个任务
    if (queue->len == 1)              // 如果队列只有一个任务，更新尾指针
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
    pthread_mutex_init(&(pool->thcount_lock), NULL);                                     // 初始化互斥量
    pthread_cond_init(&(pool->thread_all_idle), NULL);                                   // 初始化条件变量
    init_msgqueue(&pool->queue);                                                         // 初始化任务队列@
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
    pool->is_alive = 0;                                  // 关闭线程池运行
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
        pthread_mutex_lock(&pool->queue.mutex);
        while (pool->queue.len == 0 && pool->is_alive)
        {
            pthread_cond_wait(&pool->queue.has_jobs->cond, &pool->queue.mutex);
        }
        pthread_mutex_unlock(&pool->queue.mutex);

        if (pool->is_alive)
        {
            pthread_mutex_lock(&pool->thcount_lock);
            pool->num_working++; // 对工作线程数量进行统计@
            pthread_mutex_unlock(&pool->thcount_lock);

            // 取任务队列队首，并执行
            void *(*func)(void *);
            void *arg;
            msg_node *curtask = take_msgqueue(&pool->queue); // 取出队首任务，并在队列中删除该任务@（自己实现take_msgqueue）
            if (curtask)
            {
                func = curtask->function;
                arg = curtask->arg;
                func(arg);     // 执行任务
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
    pthread_mutex_lock(&pool->thcount_lock);
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
    pthread_detach((*pthread)->pthread);                                         // 线程分离
    return 0;
}