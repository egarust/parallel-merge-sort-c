#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <string.h>
#include <time.h>

#define SIZE 1000000        
#define MAX_DEPTH 4        
#define THRESHOLD 1000      

struct timeval global_start;


double get_time_ms() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - global_start.tv_sec) * 1000.0 +  (now.tv_usec - global_start.tv_usec) / 1000.0;
}

int readMore(int fd, int *in, int size) {
    char *a = (char*) in;
    int rest = size * sizeof(int);
    int idx = 0;
    int step = 0;
    
    while (1) {
        step++;
        int vv = read(fd, a + idx, rest);
        if (vv <= 0) return -step;
        idx += vv;
        rest -= vv;
        printf("[%.3f мс] ......pid=%d fd=%d step=%d idx=%d rest=%d\n", get_time_ms(), getpid(), fd, step, idx, rest);
        if (rest == 0) return step;
    }
}

int writeMore(int fd, int *out, int size) {
    char *a = (char*) out;
    int rest = size * sizeof(int);
    int idx = 0;
    int step = 0;
    
    while (1) {
        step++;
        int vv = write(fd, a + idx, rest);
        if (vv <= 0) return -step;
        idx += vv;
        rest -= vv;
        printf("[%.3f мс] ......pid=%d fd=%d step=%d записано=%d осталось=%d\n",  get_time_ms(), getpid(), fd, step, idx, rest);
        if (rest == 0) return step;
    }
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

void sequential_merge_sort(int l, int r, int *arr, long long *exchanges) {
    if (l < r) {
        int mid = l + (r - l) / 2;
        sequential_merge_sort(l, mid, arr, exchanges);
        sequential_merge_sort(mid + 1, r, arr, exchanges);
        merge(l, mid, r, arr, exchanges);
    }
}


int* process_merge_sort(int *data, int size, int depth, int *out_size, long long *exchanges) {
    double start_time = get_time_ms();
    
    printf("[%.3f мс] [ПРОЦЕСС %d] ГЛУБИНА=%d РАЗМЕР=%d\n",  start_time, getpid(), depth, size);
    
    if (size <= 1) {
        *out_size = size;
        int *result = malloc(size * sizeof(int));
        if (size > 0) memcpy(result, data, size * sizeof(int));
        return result;
    }
    
    if (depth >= MAX_DEPTH || size < THRESHOLD) {
        printf("[%.3f мс] [ПРОЦЕСС %d] Последовательная сортировка\n",   get_time_ms(), getpid());
        sequential_merge_sort(0, size - 1, data, exchanges);
        *out_size = size;
        int *result = malloc(size * sizeof(int));
        memcpy(result, data, size * sizeof(int));
        return result;
    }
    
    int mid = size / 2;
    int left_size = mid;
    int right_size = size - mid;
    
    printf("[%.3f мс] [ПРОЦЕСС %d] Разделение: левая=%d правая=%d\n",  get_time_ms(), getpid(), left_size, right_size);
    
    int fdlreq[2];    
    int fdlands[2];   
    int fd2req[2];    
    int fd2ans[2];   
    
    printf("[%.3f мс] [ПРОЦЕСС %d] СОЗДАНИЕ 4 КАНАЛОВ:\n", get_time_ms(), getpid());
    

    if (pipe(fdlreq) < 0) { perror("pipe fdlreq"); exit(1); }
    printf("[%.3f мс] [ПРОЦЕСС %d] Канал fdlreq создан: fd[0]=%d fd[1]=%d\n",  get_time_ms(), getpid(), fdlreq[0], fdlreq[1]);
    
    if (pipe(fdlands) < 0) { perror("pipe fdlands"); exit(1); }
    printf("[%.3f мс] [ПРОЦЕСС %d] Канал fdlands создан: fd[0]=%d fd[1]=%d\n",  get_time_ms(), getpid(), fdlands[0], fdlands[1]);
    
    if (pipe(fd2req) < 0) { perror("pipe fd2req"); exit(1); }
    printf("[%.3f мс] [ПРОЦЕСС %d] Канал fd2req создан: fd[0]=%d fd[1]=%d\n", get_time_ms(), getpid(), fd2req[0], fd2req[1]);
    
    if (pipe(fd2ans) < 0) { perror("pipe fd2ans"); exit(1); }
    printf("[%.3f мс] [ПРОЦЕСС %d] Канал fd2ans создан: fd[0]=%d fd[1]=%d\n", get_time_ms(), getpid(), fd2ans[0], fd2ans[1]);
    
    pid_t left_pid = fork();
    
    if (left_pid == 0) {
        printf("[%.3f мс] [ЛЕВЫЙ-%d] Потомок создан, глубина=%d\n", get_time_ms(), getpid(), depth + 1);
        
        
        close(fdlreq[1]);
        printf("[%.3f мс] [ЛЕВЫЙ-%d] Закрыт fdlreq[1]=%d (запись)\n", get_time_ms(), getpid(), fdlreq[1]);
        
        close(fdlands[0]);
        printf("[%.3f мс] [ЛЕВЫЙ-%d] Закрыт fdlands[0]=%d (чтение)\n", get_time_ms(), getpid(), fdlands[0]);
        
        close(fd2req[0]); close(fd2req[1]);
        close(fd2ans[0]); close(fd2ans[1]);
        
        
        int *left_data = malloc(left_size * sizeof(int));
        printf("[%.3f мс] [ЛЕВЫЙ-%d] Чтение данных из fdlreq[0]=%d (%d int)\n", get_time_ms(), getpid(), fdlreq[0], left_size);
        readMore(fdlreq[0], left_data, left_size);
        close(fdlreq[0]);
        
        long long local_exchanges = 0;
        int result_size;
        int *sorted = process_merge_sort(left_data, left_size, depth + 1, &result_size, &local_exchanges);
        *exchanges += local_exchanges;
        
        
        printf("[%.3f мс] [ЛЕВЫЙ-%d] Отправка результата в fdlands[1]=%d (%d int)\n", get_time_ms(), getpid(), fdlands[1], result_size);
        writeMore(fdlands[1], sorted, result_size);
        close(fdlands[1]);
        
        free(left_data);
        free(sorted);
        exit(0);
    }
    
    pid_t right_pid = fork();
    
    if (right_pid == 0) {
        printf("[%.3f мс] [ПРАВЫЙ-%d] Потомок создан, глубина=%d\n", get_time_ms(), getpid(), depth + 1);
        
        close(fd2req[1]);
        printf("[%.3f мс] [ПРАВЫЙ-%d] Закрыт fd2req[1]=%d (запись)\n", get_time_ms(), getpid(), fd2req[1]);
        
        
        close(fd2ans[0]);
        printf("[%.3f мс] [ПРАВЫЙ-%d] Закрыт fd2ans[0]=%d (чтение)\n", get_time_ms(), getpid(), fd2ans[0]);
        
        
        close(fdlreq[0]); close(fdlreq[1]);
        close(fdlands[0]); close(fdlands[1]);
        
        
        int *right_data = malloc(right_size * sizeof(int));
        printf("[%.3f мс] [ПРАВЫЙ-%d] Чтение данных из fd2req[0]=%d (%d int)\n", get_time_ms(), getpid(), fd2req[0], right_size);
        readMore(fd2req[0], right_data, right_size);
        close(fd2req[0]);
        
        
        long long local_exchanges = 0;
        int result_size;
        int *sorted = process_merge_sort(right_data, right_size, depth + 1, &result_size, &local_exchanges);
        *exchanges += local_exchanges;
        
        
        printf("[%.3f мс] [ПРАВЫЙ-%d] Отправка результата в fd2ans[1]=%d (%d int)\n", get_time_ms(), getpid(), fd2ans[1], result_size);
        writeMore(fd2ans[1], sorted, result_size);
        close(fd2ans[1]);
        
        free(right_data);
        free(sorted);
        exit(0);
    }
    
    
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Потомки: левый=%d правый=%d\n", get_time_ms(), getpid(), left_pid, right_pid);
    

    close(fdlreq[0]);
    close(fd2req[0]);
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Закрыты fdlreq[0]=%d fd2req[0]=%d (чтение)\n", get_time_ms(), getpid(), fdlreq[0], fd2req[0]);
    
    close(fdlands[1]);
    close(fd2ans[1]);
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Закрыты fdlands[1]=%d fd2ans[1]=%d (запись)\n", get_time_ms(), getpid(), fdlands[1], fd2ans[1]);
    
    
    int *left_part = data;
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Отправка данных левому потомку в fdlreq[1]=%d (%d int)\n", get_time_ms(), getpid(), fdlreq[1], left_size);
    writeMore(fdlreq[1], left_part, left_size);
    close(fdlreq[1]);
    
    
    int *right_part = data + mid;
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Отправка данных правому потомку в fd2req[1]=%d (%d int)\n", get_time_ms(), getpid(), fd2req[1], right_size);
    writeMore(fd2req[1], right_part, right_size);
    close(fd2req[1]);
    
    
    int *sorted_left = malloc(left_size * sizeof(int));
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Чтение результата от левого потомка из fdlands[0]=%d\n", get_time_ms(), getpid(), fdlands[0]);
    readMore(fdlands[0], sorted_left, left_size);
    close(fdlands[0]);
    
    
    int *sorted_right = malloc(right_size * sizeof(int));
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Чтение результата от правого потомка из fd2ans[0]=%d\n", get_time_ms(), getpid(), fd2ans[0]);
    readMore(fd2ans[0], sorted_right, right_size);
    close(fd2ans[0]);
    
    
    waitpid(left_pid, NULL, 0);
    waitpid(right_pid, NULL, 0);
    
    
    *out_size = size;
    int *result = malloc(size * sizeof(int));
    
    int i = 0, j = 0, k = 0;
    while (i < left_size && j < right_size) {
        if (sorted_left[i] <= sorted_right[j]) {
            result[k++] = sorted_left[i++];
        } else {
            result[k++] = sorted_right[j++];
            (*exchanges) += left_size - i;
        }
    }
    while (i < left_size) result[k++] = sorted_left[i++];
    while (j < right_size) result[k++] = sorted_right[j++];
    
    free(sorted_left);
    free(sorted_right);
    
    double end_time = get_time_ms();
    printf("[%.3f мс] [РОДИТЕЛЬ-%d] Завершено: размер=%d время=%.2f мс обмены=%lld\n",
           end_time, getpid(), size, end_time - start_time, *exchanges);
    
    return result;
}

int main(int argc, char *argv[]) {
    gettimeofday(&global_start, NULL);
    double program_start = get_time_ms();
    
    
    int *arr = malloc(SIZE * sizeof(int));
    if (!arr) {
        printf("[%.3f мс] Ошибка выделения памяти!\n", get_time_ms());
        return 1;
    }
    
    srand(time(NULL));
    printf("[%.3f мс] Генерация случайного массива...\n", get_time_ms());
    for (int i = 0; i < SIZE; i++) {
        arr[i] = rand() % 1000000;
    }
    
    double start_time = get_time_ms();
    printf("[%.3f мс] Начало сортировки...\n\n", start_time);
    
    int result_size;
    long long exchanges = 0;
    int *sorted = process_merge_sort(arr, SIZE, 0, &result_size, &exchanges);
    
    double end_time = get_time_ms();
    
    int sorted_flag = 1;
    for (int i = 0; i < result_size - 1; i++) {
        if (sorted[i] > sorted[i + 1]) {
            sorted_flag = 0;
            break;
        }
    }
    
    printf("\n[%.3f мс] ========================================\n", end_time);
    printf("[%.3f мс] РЕЗУЛЬТАТЫ\n", end_time);
    printf("[%.3f мс] ========================================\n", end_time);
    printf("[%.3f мс] Количество обменов: %lld\n", end_time, exchanges);
    printf("[%.3f мс] Время выполнения сортировки: %.3f мс\n", end_time, end_time - start_time);
    printf("[%.3f мс] Общее время программы: %.3f мс\n", end_time, end_time - program_start);
    printf("[%.3f мс] Статус сортировки: %s\n", end_time, sorted_flag ? "УСПЕШНО" : "ОШИБКА");
    printf("[%.3f мс] ========================================\n", end_time);
    
    free(arr);
    free(sorted);
    
    return 0;
}