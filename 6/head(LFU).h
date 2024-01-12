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

#define VERSION 23
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44
#define FORBIDDEN 403
#define NOTFOUND 404
#define HASHCOUNT 500
#define MAXLEN 20
#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

typedef struct readmsg_node
{
    // int socket, hit;
    void *(*function)(void *arg); // 函数指针
    void *arg;                    // 函数参数指针
    struct readmsg_node *next;
} readmsg_node;

typedef struct rmstaconv
{
    pthread_mutex_t mutex;
    pthread_cond_t cond; // 用于阻塞和唤醒线程池中的线程
    int status;          // 表示任务队列状态，1表示有任务，0表示无任务
} rmstaconv;

typedef struct readmsg_queue
{
    pthread_mutex_t mutex; // 互斥读写任务队列
    readmsg_node *front;
    readmsg_node *rear;
    rmstaconv *has_jobs; // 根据状态，阻塞线程
    int len;             // 任务个数
} readmsg_queue;

typedef struct rmthread
{
    int id;                   // 线程id
    pthread_t pthread;        // 封装的POSIX线程
    struct readmsgpool *pool; // 与线程池绑定
} rmthread;

typedef struct readmsgpool
{
    rmthread **threads;             // 线程指针数组
    volatile int num_threads;       // 线程池中线程数量
    volatile int num_working;       // 正在工作的线程数量
    pthread_mutex_t thcount_lock;   // 线程池锁，用于修改上面两个变量
    pthread_cond_t thread_all_idle; // 用于线程消耗的条件变量
    readmsg_queue queue;            // 任务队列
    volatile bool is_alive;         // 表示线程池是否还存在
} readmsgpool;

typedef struct filename_node
{
    // char *filename;
    // int socket;
    void *(*function)(void *arg); // 函数指针
    void *arg;                    // 函数参数指针
    struct filename_node *next;
} filename_node;

typedef struct rfstaconv
{
    pthread_mutex_t mutex;
    pthread_cond_t cond; // 用于阻塞和唤醒线程池中的线程
    int status;          // 表示任务队列状态，1表示有任务，0表示无任务
} rfstaconv;

typedef struct filename_queue
{
    pthread_mutex_t mutex; // 互斥读写任务队列
    filename_node *front;
    filename_node *rear;
    rfstaconv *has_jobs; // 根据状态，阻塞线程
    int len;             // 任务个数
} filename_queue;

typedef struct rfthread
{
    int id;                    // 线程id
    pthread_t pthread;         // 封装的POSIX线程
    struct readfilepool *pool; // 与线程池绑定
} rfthread;

typedef struct readfilepool
{
    rfthread **threads;             // 线程指针数组
    volatile int num_threads;       // 线程池中线程数量
    volatile int num_working;       // 正在工作的线程数量
    pthread_mutex_t thcount_lock;   // 线程池锁，用于修改上面两个变量
    pthread_cond_t thread_all_idle; // 用于线程消耗的条件变量
    filename_queue queue;           // 任务队列
    volatile bool is_alive;         // 表示线程池是否还存在
} readfilepool;

typedef struct msg_node
{
    // char *message;
    // int socket;
    void *(*function)(void *arg); // 函数指针
    void *arg;                    // 函数参数指针
    struct msg_node *next;
} msg_node;

typedef struct smstaconv
{
    pthread_mutex_t mutex;
    pthread_cond_t cond; // 用于阻塞和唤醒线程池中的线程
    int status;          // 表示任务队列状态，1表示有任务，0表示无任务
} smstaconv;

typedef struct msg_queue
{
    pthread_mutex_t mutex, mutex1; // 互斥读写任务队列
    msg_node *front;
    msg_node *rear;
    smstaconv *has_jobs; // 根据状态，阻塞线程
    int len;             // 任务个数
} msg_queue;

typedef struct smthread
{
    int id;                   // 线程id
    pthread_t pthread;        // 封装的POSIX线程
    struct sendmsgpool *pool; // 与线程池绑定
} smthread;

typedef struct sendmsgpool
{
    smthread **threads;             // 线程指针数组
    volatile int num_threads;       // 线程池中线程数量
    volatile int num_working;       // 正在工作的线程数量
    pthread_mutex_t thcount_lock;   // 线程池锁，用于修改上面两个变量
    pthread_cond_t thread_all_idle; // 用于线程消耗的条件变量
    msg_queue queue;                // 任务队列
    volatile bool is_alive;         // 表示线程池是否还存在
} sendmsgpool;

typedef struct
{
    int hit;
    int fd;
    long long len;
    char buffer[8097];
    char *message;
} webparam;

typedef struct content
{
    long long length; // 内容长度
    void *address;    // 内容起始地址
} content;

typedef struct hashpair
{
    char *key;             // key为文件名
    content *cont;         // 文件内容
    int count;             // 文件被访问的次数
    struct hashpair *pre;  // 指向上一个hashpair
    struct hashpair *next; // 指向下一个hashpair
} hashpair;

typedef struct hashlink
{
    int len;
    hashpair *front;
    pthread_mutex_t mutex;
} hashlink;

typedef struct hashtable
{
    hashlink *bucket;
    int num_bucket; // 桶的数量
    int boom;       // 命中次数
    double total;   // 总访问次数
    double alltime; // 总访问时间
    pthread_mutex_t lock;
} hashtable;

extern readmsgpool *readmsg_pool;
extern readfilepool *readfile_pool;
extern sendmsgpool *sendmsg_pool;
extern hashtable *table;

off_t getSize(int file_fd);
inline unsigned long int hashString(char *str);
inline char *copystring(char *value);
inline int isEqualContent(content *cont1, content *cont2);
hashtable *createHashTable(int num_bucket);
void freeHashTable(hashtable *table);
hashpair *delPair(hashlink *link);
int addItem(hashtable *table, char *key, content *cont);
content *getContentByKey(hashtable *table, char *key);

void push_rdmsgqueue(readmsg_queue *queue, readmsg_node *newtask);
void init_rdmsgqueue(readmsg_queue *queue);
readmsg_node *take_rdmsgqueue(readmsg_queue *queue);
void destory_rdmsgqueue(readmsg_queue *queue);
struct readmsgpool *initreadmsgpool(int num_threads);
void waitreadmsgpool(readmsgpool *pool);
void destoryreadmsgpool(readmsgpool *pool);
void *rmthread_do(struct rmthread *pthread);
int create_rmthread(struct readmsgpool *pool, struct rmthread **pthread, int id);

void push_fnamequeue(filename_queue *queue, filename_node *newtask);
void init_fnamequeue(filename_queue *queue);
filename_node *take_fnamequeue(filename_queue *queue);
void destory_fnamequeue(filename_queue *queue);
struct readfilepool *initreadfilepool(int num_threads);
void waitreadfilepool(readfilepool *pool);
void destoryreadfilepool(readfilepool *pool);
void *rfthread_do(struct rfthread *pthread);
int create_rfthread(struct readfilepool *pool, struct rfthread **pthread, int id);

void push_msgqueue(msg_queue *queue, msg_node *newtask);
void init_msgqueue(msg_queue *queue);
msg_node *take_msgqueue(msg_queue *queue);
void destory_msgqueue(msg_queue *queue);
struct sendmsgpool *initsendmsgpool(int num_threads);
void waitsendmsgpool(sendmsgpool *pool);
void destorysendmsgpool(sendmsgpool *pool);
void *smthread_do(struct smthread *pthread);
int create_smthread(struct sendmsgpool *pool, struct smthread **pthread, int id);

void logger(int type, char *s1, char *s2, int socket_fd);

void *read_msg(void *data);
void *read_file(void *data);
void *send_msg(void *data);