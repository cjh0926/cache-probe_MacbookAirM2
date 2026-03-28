/*
 * cache_probe_random.c — 포인터 체이싱으로 캐시 경계 측정
 *
 * 핵심 아이디어: arr[i]에 다음에 방문할 인덱스를 저장 (linked list).
 *   idx = arr[idx]  ← 완전 랜덤, 프리페처가 예측 불가
 *
 * Sattolo 알고리즘으로 워킹셋 전체를 순회하는 단일 사이클 생성.
 * → 모든 원소를 정확히 1번씩 방문 (편향 없는 랜덤 순서)
 *
 * Build:  cc -O2 -o cache_probe_random cache_probe_random.c
 * Run:    ./cache_probe_random > result_random.csv
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define MIN_WSS     (4UL  * 1024)         /* 4 KB  */
#define MAX_WSS     (32UL * 1024 * 1024)  /* 32 MB */
#define ELEM_SIZE   sizeof(size_t)        /* 8 bytes (= stride) */
#define DURATION_NS 100000000LL           /* 100 ms */

static inline long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

/*
 * Sattolo's algorithm: arr[0..n-1]을 단일 사이클 순열로 만든다.
 *   - 일반 Fisher-Yates와 달리 j 범위가 [0, i) 로 제한됨
 *   - 결과: 하나의 해밀턴 사이클 → 모든 원소 정확히 1회 방문
 */
static void build_random_cycle(size_t *arr, size_t n)
{
    for (size_t i = 0; i < n; i++) arr[i] = i;

    for (size_t i = n - 1; i >= 1; i--) {
        size_t j   = (size_t)rand() % i;   /* [0, i) — Sattolo 핵심 */
        size_t tmp = arr[i];
        arr[i]     = arr[j];
        arr[j]     = tmp;
    }
}

int main(void)
{
    srand((unsigned)time(NULL));

    /* 최대 워킹셋 크기만큼 정렬 할당 */
    size_t *buf = (size_t *)aligned_alloc(64, MAX_WSS);
    if (!buf) { perror("aligned_alloc"); return 1; }

    puts("size_bytes,throughput_GBps");

    for (size_t wss = MIN_WSS; wss <= MAX_WSS; wss *= 2) {
        size_t n = wss / ELEM_SIZE;   /* 이 워킹셋의 원소 수 */

        /* 랜덤 사이클 구축 (측정 시간에서 제외) */
        build_random_cycle(buf, n);

        /* warm-up: 사이클 한 바퀴 */
        volatile size_t idx = 0;
        for (size_t k = 0; k < n; k++)
            idx = buf[idx];

        /* 측정: ~100ms 동안 반복 */
        long long t0       = now_ns();
        long long deadline = t0 + DURATION_NS;
        long long passes   = 0;

        do {
            /* 한 패스 = 사이클 전체 순회 (n번 포인터 추적) */
            for (size_t k = 0; k < n; k++)
                idx = buf[idx];
            passes++;
        } while (now_ns() < deadline);

        long long elapsed = now_ns() - t0;

        double total_bytes = (double)passes * (double)n * ELEM_SIZE;
        double gbps = total_bytes / ((double)elapsed * 1e-9) / 1e9;

        /* sink: 컴파일러가 루프를 제거하지 못하게 */
        if (idx == 0xdeadbeef) printf("sink=%zu\n", idx);

        printf("%zu,%.4f\n", wss, gbps);
        fflush(stdout);
    }

    free(buf);
    return 0;
}
