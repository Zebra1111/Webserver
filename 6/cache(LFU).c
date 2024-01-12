#include "head.h"

inline unsigned long int hashString(char *str)
{
    unsigned long hash = 5381; // 初始哈希值，通常选择一个质数
    int c;
    while ((c = *str++))                 // 遍历字符串中的每个字符,并更新哈希值
        hash = ((hash << 5) + hash) + c; // 自身左移5位再加上自身，相当于自身乘以33
    return hash;
}

inline char *copystring(char *value)
{
    char *copy = (char *)malloc(strlen(value) + 1);
    if (!copy)
    {
        printf("Unable to allocate string value %s\n", value);
        abort();
    }
    strcpy(copy, value);
    return copy;
}

inline int isEqualContent(content *cont1, content *cont2)
{
    // 判断两个content是否相同，不同返回0，相同返回1
    if (cont1->length != cont2->length)
        return 0;
    if (cont1->address != cont2->address)
        return 0;
    return 1;
}

hashtable *createHashTable(int num_bucket)
{
    hashtable *table = (hashtable *)malloc(sizeof(hashtable));
    if (table == NULL)
        return NULL;

    table->boom = 0;
    table->total = 0.0;
    table->alltime = 0.0;
    table->num_bucket = num_bucket;
    pthread_mutex_init(&table->lock, NULL);

    table->bucket = malloc(num_bucket * sizeof(hashlink));
    if (!table->bucket)
    {
        free(table);
        return NULL;
    }

    int i;
    for (i = 0; i < num_bucket; i++)
    {
        hashlink *link = &table->bucket[i];
        link->len = 0;
        link->front = NULL;
        pthread_mutex_init(&link->mutex, NULL);
    }
    return table;
}

void freeHashTable(hashtable *table)
{
    if (table == NULL)
        return;
    hashpair *next;
    int i;
    for (i = 0; i < table->num_bucket; i++)
    {
        hashlink *link = &table->bucket[i];
        if (link)
        {
            hashpair *pair = link->front;
            while (pair)
            {
                next = pair->next;
                free(pair->key);
                free(pair->cont->address);
                free(pair->cont);
                free(pair);
                pair = next;
            }
        }
        pthread_mutex_destroy(&link->mutex);
        free(link);
    }
    pthread_mutex_destroy(&table->lock);
    free(table->bucket);
    free(table);
}

int addItem(hashtable *table, char *key, content *cont)
{
    unsigned long hash = hashString(key) % table->num_bucket; // 根据hash值，计算key在hashtable中的位置
    hashlink *link = &table->bucket[hash];
    if (link)
    {
        pthread_mutex_lock(&link->mutex);
        if (link->len == MAXLEN)
        {
            hashpair *pair = delPair(link);

            if (pair->pre == NULL)
            {
                pair->next->pre = NULL;
                link->front = pair->next;
            }
            else if (pair->next == NULL)
            {
                pair->pre->next = NULL;
            }
            else
            {
                pair->pre->next = pair->next;
                pair->next->pre = pair->pre;
            }

            free(pair->key);
            free(pair->cont->address);
            free(pair->cont);
            free(pair);
            link->len--;
        }
        hashpair *pair = malloc(sizeof(hashpair));
        pair->key = copystring(key); // 为什么要用copystring呢，直接赋值不就好了嘛？为了给这个key分配一块内存空间
        pair->cont = cont;
        pair->count = 1;
        pair->pre = NULL;
        pair->next = link->front;
        if (link->front == NULL)
        {
            link->front = pair;
        }
        else
        {
            link->front->pre = pair;
            link->front = pair;
        }
        link->len++;
        pthread_mutex_unlock(&link->mutex);
    }
    return 0;
}

content *getContentByKey(hashtable *table, char *key)
{
    if (table == NULL || key == NULL)
        return NULL;
    unsigned long hash = hashString(key) % table->num_bucket;
    hashlink *link = &table->bucket[hash];
    if (link)
    {
        hashpair *pair = link->front;
        while (pair)
        {
            if (0 == strcmp(pair->key, key))
            {
                pthread_mutex_lock(&link->mutex);
                pair->count++;
                pthread_mutex_unlock(&link->mutex);
                return pair->cont;
            }
            pair = pair->next;
        }
    }
    return NULL;
}

hashpair *delPair(hashlink *link)
{
    hashpair *pair = link->front;
    hashpair *min = link->front;
    while (pair)
    {
        if (pair->count < min->count)
            min = pair;
        pair = pair->next;
    }
    return min;
}