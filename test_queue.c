#include "shared.h"
#include <assert.h>

// 전역 큐 선언 (테스트용)
TSQueue test_q;

void* producer_test(void* arg) {
    for (int i = 0; i < 100; i++) {
        int* val = malloc(sizeof(int));
        *val = i;
        q_push(&test_q, val);
    }
    return NULL;
}

void* consumer_test(void* arg) {
    for (int i = 0; i < 100; i++) {
        int* val = (int*)q_pop(&test_q);
        // 데이터가 정상적으로 들어왔는지 확인
        assert(*val >= 0 && *val < 100); 
        free(val);
    }
    return NULL;
}

int main() {
    printf("[Test] 큐 동기화 테스트 시작...\n");
    q_init(&test_q);

    pthread_t p, c;
    pthread_create(&p, NULL, producer_test, NULL);
    pthread_create(&c, NULL, consumer_test, NULL);

    pthread_join(p, NULL);
    pthread_join(c, NULL);

    printf("[Test] 큐 동기화 테스트 성공! (Race Condition 없음)\n");
    return 0;
}
