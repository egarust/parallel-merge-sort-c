#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <time.h>

#define BASE_PORT 6000
#define MAX_DEPTH 2

struct timeval start;
int swap_count = 0;


double timer() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - start.tv_sec) + (now.tv_usec - start.tv_usec) / 1000000.0;
}


void local_sort(int *a, int n) { 
    for (int i = 1; i < n; i++) {
        int key = a[i], j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j+1] = a[j];
            j--;
            swap_count++;
        }
        a[j+1] = key;
    }
}


void merge(int *a, int n) {
    int mid = n / 2;
    int *tmp = malloc(n * sizeof(int));
    
    int i = 0, j = mid, k = 0;
    
    while (i < mid && j < n) {
        if (a[i] < a[j]) {
            tmp[k++] = a[i++];
        } else {
            tmp[k++] = a[j++];
        }
        swap_count++;
    }
    
    while (i < mid) tmp[k++] = a[i++];
    while (j < n) tmp[k++] = a[j++];
    
    memcpy(a, tmp, n * sizeof(int));
    free(tmp);
}

void send_array(int sock, int depth, int *a, int n) {
    send(sock, &depth, sizeof(int), 0);
    send(sock, &swap_count, sizeof(int), 0);
    send(sock, &n, sizeof(int), 0);
    send(sock, a, n * sizeof(int), 0);
}


int recv_array(int sock, int *depth, int *recv_swaps, int **a) {
    int n;
    
    if (recv(sock, depth, sizeof(int), MSG_WAITALL) != sizeof(int)) {
        printf("ОШИБКА: не удалось принять глубину\n");
        return -1;
    }
    
    if (recv(sock, recv_swaps, sizeof(int), MSG_WAITALL) != sizeof(int)) {
        printf("ОШИБКА: не удалось принять счётчик обменов\n");
        return -1;
    }
    
    if (recv(sock, &n, sizeof(int), MSG_WAITALL) != sizeof(int)) {
        printf("ОШИБКА: не удалось принять размер массива\n");
        return -1;
    }
    
    *a = malloc(n * sizeof(int));
    if (*a == NULL) {
        printf("ОШИБКА: не удалось выделить память\n");
        return -1;
    }
    
    if (recv(sock, *a, n * sizeof(int), MSG_WAITALL) != n * sizeof(int)) {
        printf("ОШИБКА: не удалось принять массив\n");
        free(*a);
        return -1;
    }
    
    return n;
}

void recursive_sort(int *a, int n, int depth);

void child_process() {
    int ppid = getppid();
    int port = BASE_PORT + ppid;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        exit(1);
    }
    
    int on = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        exit(1);
    }
    int depth, recv_swaps;
    int *arr;
    int n = recv_array(sock, &depth, &recv_swaps, &arr);
    if (n < 0) {
        close(sock);
        exit(1);
    }
    swap_count = recv_swaps;
    
    printf("[%.3f] ПОТОМОК %d (родитель %d): НАЧАЛО n=%d глубина=%d обменов=%d\n",timer(), getpid(), ppid, n, depth, swap_count);
    
    if (depth >= MAX_DEPTH) {
        local_sort(arr, n);
        printf("[%.3f] ПОТОМОК %d: ЛОКАЛЬНАЯ СОРТИРОВКА ЗАВЕРШЕНА n=%d обменов=%d\n",timer(), getpid(), n, swap_count);
    } else {
        recursive_sort(arr, n, depth);
        printf("[%.3f] ПОТОМОК %d: РЕКУРСИВНАЯ СОРТИРОВКА ЗАВЕРШЕНА n=%d обменов=%d\n",timer(), getpid(), n, swap_count);
    }
    send(sock, &depth, sizeof(int), 0);
    send(sock, &swap_count, sizeof(int), 0);
    send(sock, &n, sizeof(int), 0);
    send(sock, arr, n * sizeof(int), 0);
    free(arr);
    close(sock);
    exit(0);
}

int create_server(int port) {
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("socket");
        exit(1);
    }
    
    int on = 1;
    setsockopt(server, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(server, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(server);
        exit(1);
    }
    
    if (listen(server, 5) < 0) {
        perror("listen");
        close(server);
        exit(1);
    }
    
    usleep(100000);
    
    return server;
}

void recursive_sort(int *a, int n, int depth) {
    if (n <= 1) return;
    
    printf("[%.3f] ПРОЦЕСС %d: ВХОД n=%d глубина=%d обменов=%d\n",timer(), getpid(), n, depth, swap_count);
    
    if (depth >= MAX_DEPTH) {
        local_sort(a, n);
        printf("[%.3f] ПРОЦЕСС %d: ВЫХОД n=%d глубина=%d обменов=%d (локальная)\n",timer(), getpid(), n, depth, swap_count);
        return;
    }
    
    int mid = n / 2;
    int port = BASE_PORT + getpid();
    int server = create_server(port);
    
    pid_t child1 = fork();
    if (child1 == 0) {
        child_process();
    }
    
    pid_t child2 = fork();
    if (child2 == 0) {
        child_process();
    }
    printf("[%.3f] ПРОЦЕСС %d: созданы потомки %d и %d\n", timer(), getpid(), child1, child2);   
    int client1 = accept(server, NULL, NULL);
    int client2 = accept(server, NULL, NULL);  
    int on = 1;
    setsockopt(client1, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
    setsockopt(client2, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));    
    printf("[%.3f] ПРОЦЕСС %d: отправка потомкам (n1=%d, n2=%d)\n",timer(), getpid(), mid, n-mid);    
    send_array(client1, depth + 1, a, mid);
    send_array(client2, depth + 1, a + mid, n - mid); 
    int d1, d2, swaps1, swaps2;
    int *a1, *a2;
    int n1 = recv_array(client1, &d1, &swaps1, &a1);
    int n2 = recv_array(client2, &d2, &swaps2, &a2);
    printf("[%.3f] ПРОЦЕСС %d: получено от потомков (n1=%d, n2=%d, обменов1=%d, обменов2=%d)\n",timer(), getpid(), n1, n2, swaps1, swaps2);
    swap_count = swaps1 + swaps2;
    memcpy(a, a1, n1 * sizeof(int));
    memcpy(a + n1, a2, n2 * sizeof(int));
    merge(a, n);
    printf("[%.3f] ПРОЦЕСС %d: ВЫХОД n=%d глубина=%d обменов=%d (слияние)\n",timer(), getpid(), n, depth, swap_count);
    close(client1);
    close(client2);
    close(server); 
    free(a1);
    free(a2);
    waitpid(child1, NULL, 0);
    waitpid(child2, NULL, 0);
}

int check_sorted(int *a, int n) {
    for (int i = 0; i < n - 1; i++) {
        if (a[i] > a[i+1]) {
            printf("ОШИБКА на индексе %d: %d > %d\n", i, a[i], a[i+1]);
            return 0;
        }
    }
    return 1;
}

int main() {
    gettimeofday(&start, NULL);
    
    int n = 1000000;  
    int *a = malloc(n * sizeof(int));
    if (a == NULL) {
        perror("malloc");
        return 1;
    }
    
    srand(time(NULL));
    for (int i = 0; i < n; i++) {
        a[i] = rand() % 1000;
    }
    
    printf("========================================\n");
    printf("НАЧАЛО СОРТИРОВКИ: n=%d, MAX_DEPTH=%d\n", n, MAX_DEPTH);
    printf("========================================\n\n");
    
    recursive_sort(a, n, 0);
    
    printf("\n========================================\n");
    printf("[%.3f] ВСЕГО ОБМЕНОВ: %d\n", timer(), swap_count);
    printf("[%.3f] РЕЗУЛЬТАТ: %s\n", timer(), check_sorted(a, n) ? "УСПЕХ (отсортировано)" : "ОШИБКА (не отсортировано)");
    printf("========================================\n");
    
    free(a);
    return 0;
}