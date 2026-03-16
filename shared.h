#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* --- [1] DB 테이블별 데이터 구조체 (물리 설계 반영) --- */

// 1. 온습도 데이터 (th_trans 테이블 매핑)
typedef struct {
    int sen_id;         // 센서_id (PK, FK)
    int wp_id;          // 작업장_id (FK)
    float temp;         // 온도
    float humd;         // 습도
    time_t time;        // 시간 (DATETIME 대응)
} THData;

// 2. 워치 생체 데이터 (wd_trans 테이블 매핑)
typedef struct {
    int sen_id;         // 센서_id (PK, FK)
    int wp_id;          // 작업장_id (FK)
    float sk_temp;      // 피부온도
    float hr;           // 심박수
    time_t time;        // 시간 (DATETIME 대응)
} VitalData;

// 3. 상황 분석 결과 (situ_trans 테이블 매핑)
typedef struct {
    int sen_id;         // 센서_id (PK, FK)
    int wp_id;          // 작업장_id (FK)
    char detail[201];   // 내용 (VARCHAR2(200))
    time_t time;        // 시간 (DATETIME 대응)
} SituData;

// 4. 이벤트/경보 (event_trans 테이블 매핑)
typedef struct {
    int dept_id;        // 사번 (PK, FK)
    int wp_id;          // 작업장_id (FK)
    char state_code[101]; // 상태 (VARCHAR2(100), FK)
    char detail[201];   // 내용 (VARCHAR2(200))
    time_t time;        // 시간 (DATETIME 대응)
} EventData;

/* --- [2] 통합 데이터 패키지 (큐 전송용) --- */

typedef enum {
    TYPE_TH, TYPE_VITAL, TYPE_SITU, TYPE_EVENT
} DataType;

// 여러 테이블 데이터를 하나의 큐에서 관리하기 위한 통합 구조체
typedef struct {
    DataType type;      // 데이터 종류 구분
    union {
        THData th;
        VitalData vital;
        SituData situ;
        EventData event;
    } payload;
} SensorPacket;

/* --- [3] Thread-Safe Queue 및 시스템 함수 --- */

#define QUEUE_SIZE 2000

typedef struct {
    void* data[QUEUE_SIZE];
    int head, tail, count;
    pthread_mutex_t lock;
    pthread_cond_t not_empty, not_full;
} TSQueue;

extern TSQueue q_th, q_vital, q_db, q_send;
extern pthread_mutex_t g_mtx;

void q_init(TSQueue *q);
void q_push(TSQueue *q, void* item);
void* q_pop(TSQueue *q);

/* 모듈 진입 함수 */
extern void* th_module(void* arg);
extern void* vital_module(void* arg);
extern void* rule_module(void* arg);
extern void* db_module(void* arg);
extern void* send_module(void* arg);

#endif
