#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <string.h>

#define SIZE 100000000  


typedef struct {
    int *arr;     
    int a;       
    int b;        
    long long *exchanges;
} SortRequest;


int max_threads = 1;       
int active_threads = 1;        
pthread_mutex_t thread_mutex;   
struct timeval start_time;      


long long total_exchanges = 0;

double get_time() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start_time.tv_sec) + (now.tv_usec - start_time.tv_usec) / 1000000.0;
}

void merge(int l, int m, int r, int *arr, long long *exchanges) {
    int n1 = m - l + 1;
    int n2 = r - m;
    int *L = malloc(n1 * sizeof(int));
    int *R = malloc(n2 * sizeof(int));
    for (int i = 0; i < n1; i++) L[i] = arr[l + i];
    for (int i = 0; i < n2; i++) R[i] = arr[m + 1 + i];
    int i = 0, j = 0, k = l;
    while (i < n1 && j < n2) {
        if (L[i] <= R[j]) {
            arr[k++] = L[i++];
        } else {
            arr[k++] = R[j++];
            (*exchanges) += n1 - i;  
        }
    }
    while (i < n1) arr[k++] = L[i++];
    while (j < n2) arr[k++] = R[j++];
    free(L);
    free(R);
}

void insertion_sort(int l, int r, int *arr, long long *exchanges) {
    for (int i = l + 1; i <= r; i++) {
        int key = arr[i];
        int j = i - 1;
        while (j >= l && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
            (*exchanges)++;
        }
        arr[j + 1] = key;
    }
}


void* sort_thread(void *arg) {
    SortRequest *req = (SortRequest*)arg;
    int l = req->a;
    int r = req->b;
    int *arr = req->arr;
    long long local_exchanges = 0;
    double thread_start = get_time();
    // printf("Потоковая функция стартует a=%d b=%d size=%d clock=%.3f\n", l, r, r - l + 1, thread_start);
    if (l < r) {
        if (r - l < 1000) {  
            insertion_sort(l, r, arr, &local_exchanges);
        } else {
            int mid = l + (r - l) / 2;
            
            SortRequest right_req = {arr, mid + 1, r, &local_exchanges};
            pthread_t thread;
            int create_thread = 0;
            
            pthread_mutex_lock(&thread_mutex);

            if (active_threads < max_threads) {
                active_threads++;
                create_thread = 1;
            }

            pthread_mutex_unlock(&thread_mutex);

            if (create_thread) {
                // printf("Ожидание завершения к потоку a=%d b=%d clock=%.3f\n", l, mid, get_time());
                pthread_create(&thread, NULL, sort_thread, &right_req);
                
                SortRequest left_req = {arr, l, mid, &local_exchanges};
                sort_thread(&left_req);
                
                pthread_join(thread, NULL);
                
                pthread_mutex_lock(&thread_mutex);
                active_threads--;
                pthread_mutex_unlock(&thread_mutex);
                // printf("Поток вернул %lld clock=%.3f\n", local_exchanges, get_time());
            } else {
                SortRequest left_req = {arr, l, mid, &local_exchanges};
                SortRequest right_req2 = {arr, mid + 1, r, &local_exchanges};

                sort_thread(&left_req);
                sort_thread(&right_req2);
            }
            merge(l, mid, r, arr, &local_exchanges);
        }
    }
    // printf("Потоковая функция завершена a=%d b=%d size=%d clock=%.3f\n", l, r, r - l + 1, get_time());
    if (req->exchanges) {
        *req->exchanges += local_exchanges;
    }
    return (void*)(long)local_exchanges;
}

int main(int argc, char *argv[]) {
    if (argc > 1) max_threads = atoi(argv[1]);
    
    gettimeofday(&start_time, NULL);
    
    printf("Размерность: %d Потоков: %d\n", SIZE, max_threads);
    
    
    int *arr = malloc(SIZE * sizeof(int));
    if (!arr) {
        printf("Ошибка выделения памяти\n");
        return 1;
    }
    
    
    srand(time(NULL));
    for (int i = 0; i < SIZE; i++) {
        arr[i] = rand() % 10000;
    }
    
    
    
    
    pthread_mutex_init(&thread_mutex, NULL);
    
    
    double start_sort = get_time();
    SortRequest main_req = {arr, 0, SIZE - 1, &total_exchanges};
    sort_thread(&main_req);
    double end_sort = get_time();
    
    
    int sorted = 1;
    for (int i = 0; i < SIZE - 1; i++) {
        if (arr[i] > arr[i + 1]) {
            sorted = 0;
            break;
        }
    }
    
    printf("\nРезультаты:\n");
    printf("Оригинал: Обменов: %lld Время выполнения: %.3f мс\n", total_exchanges, (end_sort - start_sort) * 1000);
    printf("Статус сортировки: %s\n", sorted ? "УСПЕШНО" : "ОШИБКА");
    
    
    free(arr);

    pthread_mutex_destroy(&thread_mutex);
    
    return 0;
}

