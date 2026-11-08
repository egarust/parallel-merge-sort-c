#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <time.h>
#include <sys/time.h>
#include <semaphore.h>
#include <string.h>

#define SIZE 650000
#define MAX_DEPTH 10

const char *mem_name = "laba_os_1"; 
int *shared_arr;                      
struct timeval start;

double timer() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec)/1000000.0;
}

void merge(int l, int m, int r, int *arr) {
    int n1 = m - l + 1, n2 = r - m;
    int L[n1], R[n2];
    
    for (int i = 0; i < n1; i++) L[i] = arr[l + i];
    for (int i = 0; i < n2; i++) R[i] = arr[m + 1 + i];
    
    int i = 0, j = 0, k = l;
    while (i < n1 && j < n2)
        arr[k++] = (L[i] <= R[j]) ? L[i++] : R[j++];
    while (i < n1) arr[k++] = L[i++];
    while (j < n2) arr[k++] = R[j++];
}

void local_sort(int l, int r, int *arr) {
    for (int i = l + 1; i <= r; i++) {
        int key = arr[i], j = i - 1;
        while (j >= l && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

void recursive_sort(int l, int r, int depth, int max_depth, int proc_id) {
    if (l >= r) return;
    printf("[%.2f] Процесс %d: получил задание [%d..%d] глубина=%d\n", timer(), proc_id, l, r, depth);
    if (depth >= max_depth) {
        printf("[%.2f] Процесс %d: локальная сортировка [%d..%d]\n", timer(), proc_id, l, r);
        local_sort(l, r, shared_arr);
        printf("[%.2f] Процесс %d: локальная сортировка завершена\n", timer(), proc_id);
        return;
    }
    int mid = (l + r) / 2;
    char sem1[64], sem2[64]; //  имена семафоров
    sprintf(sem1, "/sem_%d_%d", getpid(), 1);
    sprintf(sem2, "/sem_%d_%d", getpid(), 2);
    sem_unlink(sem1); sem_unlink(sem2);
    sem_t *s1 = sem_open(sem1, O_CREAT, 0777, 0);
    sem_t *s2 = sem_open(sem2, O_CREAT, 0777, 0);
    printf("[%.2f] Процесс %d: создаю потомков для [%d..%d] и [%d..%d]\n", timer(), proc_id, l, mid, mid+1, r);
    pid_t pid1 = fork(); 
    if (pid1 == 0) {
        int *child_arr = (int*)mmap(NULL, SIZE * sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shm_open(mem_name, O_RDWR, 0777), 0); // ПОТОМОК: присоединяемся к разделяемой памяти ЗАНОВО (как требует задание)
        if (child_arr == MAP_FAILED) {
            printf("Ошибка mmap в потомке\n");
            exit(1);
        }
        recursive_sort(l, mid, depth + 1, max_depth, proc_id*10 + 1);
        sem_post(s1);
        munmap(child_arr, SIZE * sizeof(int));//  прекращает отображение файла в адресное пространство процесса. 
        exit(0);
    }
    pid_t pid2 = fork();
    if (pid2 == 0) {
        int *child_arr = (int*)mmap(NULL, SIZE * sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, shm_open(mem_name, O_RDWR, 0777), 0);
        if (child_arr == MAP_FAILED) {
            printf("Ошибка mmap в потомке\n");
            exit(1);
        }
        recursive_sort(mid + 1, r, depth + 1, max_depth, proc_id*10 + 2);
        sem_post(s2);
        munmap(child_arr, SIZE * sizeof(int));
        exit(0);
    }
    printf("[%.2f] Процесс %d: ожидание потомков...\n", timer(), proc_id);// родитель ждет завершения потомков
    sem_wait(s1);
    sem_wait(s2);
    printf("[%.2f] Процесс %d: потомки завершили работу\n", timer(), proc_id);
    merge(l, mid, r, shared_arr);// слияние
    sem_close(s1); sem_close(s2);
    sem_unlink(sem1); sem_unlink(sem2);
    printf("[%.2f] Процесс %d: завершил обработку [%d..%d]\n", timer(), proc_id, l, r);
}

int main(int argc, char *argv[]) {
    int max_depth = (argc > 1) ? atoi(argv[1]) : MAX_DEPTH;
    
    gettimeofday(&start, NULL);
    
    printf("Глубина рекурсии: %d, Размер массива: %d\n", max_depth, SIZE);
    
    printf("[%.2f] шаг 1: Создание разделяемой памяти\n", timer());
    shm_unlink(mem_name);
    int fd = shm_open(mem_name, O_CREAT|O_RDWR, 0777);
    ftruncate(fd, SIZE * sizeof(int));
    
    
    printf("[%.2f] шаг 2: Отображение памяти (pid=%d)\n", timer(), getpid());
    shared_arr = (int*)mmap(NULL, SIZE * sizeof(int), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    
    
    printf("[%.2f] шаг 3: Заполнение случайными числами\n", timer());
    srand(time(NULL));
    for (int i = 0; i < SIZE; i++) shared_arr[i] = rand() % 100;
    
    // printf("Исходный массив: ");
    // for (int i = 0; i < SIZE; i++) printf("%d ", shared_arr[i]);
    // printf("\n\n");
    
    
    printf("[%.2f] шаг 4: Запуск сортировки\n", timer());
    recursive_sort(0, SIZE-1, 0, max_depth, 1);
    
    
    for (int i = 0; i < 100; i++) wait(NULL);
    
    printf("\n[%.2f] РЕЗУЛЬТАТ:\n", timer());
    // printf("Отсортированный массив: ");
    // for (int i = 0; i < SIZE; i++) printf("%d ", shared_arr[i]);
    // printf("\n");
    
    
    int ok = 1;
    for (int i = 0; i < SIZE-1; i++) 
        if (shared_arr[i] > shared_arr[i+1]) { ok = 0; break; }
    printf("Статус: %s\n", ok ? "УСПЕШНО" : "ОШИБКА");
    
    
    munmap(shared_arr, SIZE * sizeof(int));
    close(fd);
    shm_unlink(mem_name);
    
    return 0;
}