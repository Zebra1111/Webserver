typedef struct
{
    int hit;
    int fd;
} webparam;

typedef struct staconv
{
    pthread_mutex_t mutex;
    pthread_cond_t cond; // 用于阻塞和唤醒线程池中的线程
    int status;          // 表示任务队列状态，1表示有任务，0表示无任务
} staconv;

typedef struct task
{
    struct task *next;            // 指向下一个任务
    void *(*function)(void *arg); // 函数指针
    void *arg;                    // 函数参数指针
} task;

typedef struct taskqueue
{
    pthread_mutex_t mutex; // 互斥读写任务队列
    task *front;
    task *rear;
    staconv *has_jobs; // 根据状态，阻塞线程
    int len;           // 任务个数
} taskqueue;

typedef struct thread
{
    int id;                  // 线程id
    pthread_t pthread;       // 封装的POSIX线程
    struct threadpool *pool; // 与线程池绑定
} thread;

typedef struct threadpool
{
    thread **threads;               // 线程指针数组
    volatile int num_threads;       // 线程池中线程数量
    volatile int num_working;       // 正在工作的线程数量
    pthread_mutex_t thcount_lock;   // 线程池锁，用于修改上面两个变量
    pthread_cond_t thread_all_idle; // 用于线程消耗的条件变量
    taskqueue queue;                // 任务队列
    volatile bool is_alive;         // 表示线程池是否还存在
} threadpool;

void push_taskqueue(taskqueue *queue, task *newtask);
void init_taskqueue(taskqueue *queue);
task *take_taskqueue(taskqueue *queue);
void destory_taskqueue(taskqueue *queue);
struct threadpool *initThreadPool(int num_threads);
void addTask2ThreadPool(threadpool *pool, task *curtask);
void waitThreadPool(threadpool *pool);
void destoryThreadPool(threadpool *pool);
int getNumofThreadWorking(threadpool *pool);
int create_thread(struct threadpool *pool, struct thread **pthread, int id);
void *thread_do(struct thread *pthread);
