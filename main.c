#include "shared.h"

// 다른 파일에 정의된 스레드 함수들을 가져옴 (링커가 연결)
extern void* th_module(void* arg);
extern void* vital_module(void* arg);
extern void* rule_module(void* arg);
//extern void* db_module(void* arg);
extern void* send_module(void* arg);

pthread_mutex_t g_mtx;

// 큐 인스턴스 실제 생성
TSQueue q_th, q_vital, q_db, q_send;

// 1. 데이터를 받아서 그냥 버리는 함수
void* dummy_db_module(void* arg) {
    while (1) {
        SensorPacket* pkt = q_pop(&q_db); // 데이터가 들어올 때까지 대기하다가 나오면 가져옴
        if (pkt) free(pkt);               // 가져온 즉시 메모리 해제
    }
    return NULL;
}

int main() {
    printf("[Master] 산업 안전 모니터링 시스템 기동 (Multi-Threaded)\n");

    // 1. 모든 통신용 큐 초기화
    q_init(&q_th); q_init(&q_vital);
    q_init(&q_db); q_init(&q_send);
    
    pthread_mutex_init(&g_mtx, NULL);

    // 2. 스레드 식별자 배열
    pthread_t threads[5];
    
    pthread_t tid;
    pthread_create(&tid, NULL, dummy_db_module, NULL); // 더미 소비자 가동

    // 3. 각 모듈을 독립적인 스레드로 실행
    // fork()와 달리 하나의 프로세스 안에서 함수 단위로 병렬 실행됨
    pthread_create(&threads[0], NULL, th_module, NULL);
    pthread_create(&threads[1], NULL, vital_module, NULL);
    pthread_create(&threads[2], NULL, rule_module, NULL);
    //pthread_create(&threads[3], NULL, db_module, NULL);
    pthread_create(&threads[3], NULL, send_module, NULL);

    printf("[Master] 모든 모듈 스레드가 성공적으로 생성되었습니다.\n");

    // 4. 스레드 종료 대기 (메인 프로세스가 먼저 죽지 않도록 join)
    // 실제 운영 시에는 여기서 스레드 상태를 감시하는 로직을 추가할 수 있음
    for(int i = 0; i < 4; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}
