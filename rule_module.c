// rule_module.c  (shared.h 버전)
// - 입력: q_th(THData*), q_vital(VitalData*)
// - 출력: q_db(SensorPacket*), q_send(SensorPacket*)  (event는 send에도 복제 전송)
// - 기능: TH cache, LAW_REST(60s avg HI>=33), HS(180s streak),
//         active list(A), timeout(30s working down / 600s reset),
//         event 발생 시 situ 테스트 적재
//
// gcc -O2 -Wall -Wextra -pthread rule_module.c queue.c -o rule_module

#define _GNU_SOURCE
#include "shared.h"


#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>

// =====================
// 프로젝트 상수
// =====================
#define MAX_WP_ID   4096
#define MAX_SEN_ID  20000
#define SAMPLE_PERIOD_SEC 5

// 법정휴식: 평균 HI >= 33 (60초 윈도우)
#define LAW_WINDOW_SEC (60)
#define LAW_WINDOW_SAMPLES 5
#define HI_THRESHOLD 33.0f

// 열스트레스: 180초 연속 (HR +30, Temp +3, HI >= 33)
#define HS_STREAK_SEC 180
#define HS_STREAK_SAMPLES (HS_STREAK_SEC / SAMPLE_PERIOD_SEC)
#define HS_HR_DELTA 30.0f
#define HS_TEMP_DELTA 3.0f

// 타임아웃
#define TIMEOUT_WORKING_DOWN_MS (30ULL * 1000ULL)
#define TIMEOUT_RESET_MS        (600ULL * 1000ULL)

// 이벤트 상태 코드(=state_code 문자열로 기록)
#define STATE_REST_START     "REST_START"
#define STATE_EMERGENCY_REST "EMERGENCY_REST"

// SITU 테스트 적재 정책
#define SITU_TEST_SEN_ID 10000
#define SITU_TEST_DETAIL "테스트입니다"

// =====================
// 내부 상태
// =====================
typedef struct {
    int used;
    THData last; // 최신 TH 스냅샷
} ThWpState;

typedef struct {
    int used;

    int working;          // 0/1 (룰모듈 내부 근무 상태)
    int wp_id;

    float baseline_hr;
    float baseline_temp;

    int    count_hi;
    double sum_hi;

    int hs_streak;

    uint64_t last_seen_ms; // WD 마지막 수신 시각(CLOCK_MONOTONIC ms)
} WatchState;

static ThWpState  g_th_by_wp[MAX_WP_ID + 1];
static WatchState g_watch[MAX_SEN_ID + 1];
static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;

// =====================
// Active list (A 방식)
// - WD 수신이 한 번이라도 된 워치만 active_ids에 넣고,
//   타임아웃 체크는 active만 순회해서 O(active_count)
// =====================
#define MAX_ACTIVE (MAX_SEN_ID + 1)
static int active_ids[MAX_ACTIVE];
static int active_count = 0;
static unsigned char in_active[MAX_SEN_ID + 1];
static int active_pos[MAX_SEN_ID + 1];

static void active_add_unsafe(int sen_id) {
    if (sen_id < 0 || sen_id > MAX_SEN_ID) return;
    if (in_active[sen_id]) return;

    active_pos[sen_id] = active_count;
    active_ids[active_count++] = sen_id;
    in_active[sen_id] = 1;
}

static void active_remove_unsafe(int sen_id) {
    if (sen_id < 0 || sen_id > MAX_SEN_ID) return;
    if (!in_active[sen_id]) return;

    int idx = active_pos[sen_id];
    int last_id = active_ids[active_count - 1];

    active_ids[idx] = last_id;
    active_pos[last_id] = idx;

    active_count--;
    in_active[sen_id] = 0;
}

// =====================
// 시간 유틸
// =====================
static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}

// =====================
// HI 공식
// =====================
static float calc_heat_index(float temp_c, float humidity) {
    float T = temp_c;
    float RH = humidity;

    float HI =
        -8.784695f +
        1.61139411f * T +
        2.338549f * RH -
        0.14611605f * T * RH -
        0.012308094f * T * T -
        0.016424828f * RH * RH +
        0.002211732f * T * T * RH +
        0.00072546f * T * RH * RH -
        0.000003582f * T * T * RH * RH;

    // 소수 2자리 반올림
    HI = (float)((int)(HI * 100.0f + (HI >= 0 ? 0.5f : -0.5f))) / 100.0f;
    return HI;
}

// =====================
// 패킷 malloc 헬퍼
// - q_db/q_send로 보낼 때는 반드시 malloc한 SensorPacket*
// =====================
static SensorPacket* pkt_alloc(DataType t) {
    SensorPacket* p = (SensorPacket*)malloc(sizeof(SensorPacket));
    if (!p) return NULL;
    memset(p, 0, sizeof(*p));
    p->type = t;
    return p;
}

// =====================
// TH 처리: 작업장 최신값 갱신
// =====================
static void on_th_data(const THData* th) {

    if (th != NULL) {
    printf("[TH_DATA] WP_ID: %d, Temp: %.1f°C, Humidity: %.1f%%\n", 
            th->wp_id, th->temp, th->humd);
    } else {
    printf("⚠️ 오류: THData 포인터가 NULL입니다.\n");
    }
    
    if (th->wp_id < 0 || th->wp_id > MAX_WP_ID) return;

    pthread_mutex_lock(&g_mtx);
    g_th_by_wp[th->wp_id].used = 1;
    g_th_by_wp[th->wp_id].last = *th;
    pthread_mutex_unlock(&g_mtx);
}

// =====================
// 근무 시작/정지 로직
// =====================
static void start_working_locked(WatchState* ws, const VitalData* wd) {
    ws->working = 1;
    ws->wp_id = wd->wp_id;

    ws->baseline_hr = wd->hr;
    ws->baseline_temp = wd->sk_temp;

    ws->count_hi = 0;
    ws->sum_hi = 0.0;
    ws->hs_streak = 0;

    printf("  -> [WORK_START_INTERNAL] sen_id=%d wp_id=%d baseline_hr=%.1f baseline_temp=%.2f\n",
           wd->sen_id, wd->wp_id, ws->baseline_hr, ws->baseline_temp);
}

static void stop_working_and_reset_locked(WatchState* ws, const char* reason) {
    ws->working = 0;

    ws->count_hi = 0;
    ws->sum_hi = 0.0;
    ws->hs_streak = 0;

    printf("  -> [WORK_STOP] reason=%s\n", reason);
}

// =====================
// 이벤트 생성 → q_db + q_send 로 내보내기
// (event_trans + situ_trans 테스트)
// =====================
static void emit_event(int sen_id, int wp_id, const char* state_code) {
    time_t nowt = time(NULL);

    // 1) event 패킷(DB용)
    SensorPacket* pe_db = pkt_alloc(TYPE_EVENT);
    if (!pe_db) return;

    // ⚠️ shared.h의 EventData는 dept_id만 존재(=worker FK).
    // 너 기존 정책이 "dept_id 자리에 sen_id를 담아 전달" 이었으니까,
    // 여기서도 동일하게 dept_id=sen_id로 넣어둠.
    // DB 모듈에서 watch_assign로 진짜 dept_id로 매핑해서 넣고 싶으면
    // EventData에 sen_id 필드를 추가하는 게 정석임.
    pe_db->payload.event.dept_id = sen_id;  // (임시 캐리어)
    pe_db->payload.event.wp_id   = wp_id;
    snprintf(pe_db->payload.event.state_code, sizeof(pe_db->payload.event.state_code), "%s", state_code);
    snprintf(pe_db->payload.event.detail, sizeof(pe_db->payload.event.detail), "%s", "미완성");
    pe_db->payload.event.time = nowt;

    q_push(&q_db, pe_db);

    // 2) event 패킷(SEND용) - DB용과 분리해서 소유권 명확히
    SensorPacket* pe_send = pkt_alloc(TYPE_EVENT);
    if (pe_send) {
        pe_send->payload.event = pe_db->payload.event;
        q_push(&q_send, pe_send);
    }

    // 3) situ 테스트 패킷(DB용)
    SensorPacket* ps = pkt_alloc(TYPE_SITU);
    if (ps) {
        ps->payload.situ.sen_id = SITU_TEST_SEN_ID;
        ps->payload.situ.wp_id  = wp_id;
        snprintf(ps->payload.situ.detail, sizeof(ps->payload.situ.detail), "%s", SITU_TEST_DETAIL);
        ps->payload.situ.time = nowt;
        q_push(&q_db, ps);
    }

    printf("[EVENT] sen_id=%d wp_id=%d state=%s (->q_db event + ->q_send event + ->q_db situ_test)\n",
           sen_id, wp_id, state_code);
}

// =====================
// WD(Vital) 처리: 판단 + 이벤트 발생
// =====================
static void on_vital_data(const VitalData* wd) {

printf("%d | %d", wd->sen_id, wd->wp_id);

    if (wd->sen_id < 0 || wd->sen_id > MAX_SEN_ID) {
    	return;
    }
    if (wd->wp_id < 0 || wd->wp_id > MAX_WP_ID){
    	return;
    }
    
    const int sen_id = wd->sen_id;
    const int wp_id  = wd->wp_id;

    int th_ready = 0;
    THData th_snapshot;

    int send_rest_start = 0;
    int send_emergency  = 0;

    float hi = 0.0f;

    float base_hr = 0.0f, base_tp = 0.0f;
    int streak_now = 0;
    int law_cnt_now = 0;

    pthread_mutex_lock(&g_mtx);

    WatchState* ws = &g_watch[sen_id];
    if (!ws->used) {
        memset(ws, 0, sizeof(*ws));
        ws->used = 1;
        ws->working = 0;
    }

    // Vital 들어왔으니 last_seen 갱신
    ws->last_seen_ms = now_ms();

    // active 등록(A 방식)
    active_add_unsafe(sen_id);

    // 작업장 반영
    ws->wp_id = wp_id;

    // TH 준비 여부
    th_ready = g_th_by_wp[wp_id].used;
    if (th_ready) th_snapshot = g_th_by_wp[wp_id].last;

    // working=0 상태면 Vital 들어온 순간 "근무 시작"(이벤트는 안 보냄)
    if (ws->working == 0) {
        start_working_locked(ws, wd);
    }

    // TH 없으면 HI 계산/판단 불가
    if (!th_ready) {
        pthread_mutex_unlock(&g_mtx);
        printf("[WD] sen_id=%d wp_id=%d (TH not ready) hr=%.1f sk=%.2f\n",
               sen_id, wp_id, wd->hr, wd->sk_temp);
        return;
    }

    // HI 계산
    hi = calc_heat_index(th_snapshot.temp, th_snapshot.humd);

    // ===== 1) 법정휴식: 평균 HI =====
    ws->sum_hi += (double)hi;
    ws->count_hi += 1;
    law_cnt_now = ws->count_hi;

    if (ws->count_hi >= LAW_WINDOW_SAMPLES) {
        float avg = (float)(ws->sum_hi / (double)LAW_WINDOW_SAMPLES);

        // 다음 블록을 위해 리셋
        ws->count_hi = 0;
        ws->sum_hi = 0.0;

        if (avg >= HI_THRESHOLD) {
            send_rest_start = 1;
            stop_working_and_reset_locked(ws, "LAW_REST_START(avg_HI>=33)");
        }
    }

    // ===== 2) 열스트레스: 180초 연속 =====
    int cond_hr = (wd->hr >= ws->baseline_hr + HS_HR_DELTA);
    int cond_tp = (wd->sk_temp >= ws->baseline_temp + HS_TEMP_DELTA);
    int cond_hi = (hi >= HI_THRESHOLD);

    if (cond_hr && cond_tp && cond_hi) {
        ws->hs_streak += 1;
        if (ws->hs_streak >= HS_STREAK_SAMPLES) {
            send_emergency = 1;
            ws->hs_streak = 0;
            stop_working_and_reset_locked(ws, "EMERGENCY_REST(180s_streak)");
        }
    } else {
        ws->hs_streak = 0;
    }

    base_hr = ws->baseline_hr;
    base_tp = ws->baseline_temp;
    streak_now = ws->hs_streak;

    pthread_mutex_unlock(&g_mtx);

    // ===== 이벤트 전송(락 밖) =====
    if (send_rest_start) emit_event(sen_id, wp_id, STATE_REST_START);
    if (send_emergency)  emit_event(sen_id, wp_id, STATE_EMERGENCY_REST);

    // ===== 로그 =====
    printf("[WD] sen_id=%d wp_id=%d hr=%.1f sk=%.2f HI=%.2f | base(hr=%.1f,tp=%.2f) | streak=%d/%d | law=%d/%d\n",
           sen_id, wp_id, wd->hr, wd->sk_temp, hi,
           base_hr, base_tp,
           streak_now, HS_STREAK_SAMPLES,
           law_cnt_now, LAW_WINDOW_SAMPLES);
}

// =====================
// 타임아웃 처리(5초마다 1회)
// - 30초: working=0 (누적은 유지)
// - 600초: 누적 초기화 + active 제거
// =====================
static void check_timeouts_once(void) {
    uint64_t now = now_ms();

    pthread_mutex_lock(&g_mtx);

    for (int i = 0; i < active_count; ) {
        int sen_id = active_ids[i];
        WatchState* ws = &g_watch[sen_id];

        if (!ws->used || ws->last_seen_ms == 0) {
            active_remove_unsafe(sen_id);
            continue;
        }

        uint64_t gap = (now >= ws->last_seen_ms) ? (now - ws->last_seen_ms) : 0;

        // 600초: 누적시간 초기화 + active 제거
        if (gap >= TIMEOUT_RESET_MS) {
            ws->working = 0;
            ws->count_hi = 0;
            ws->sum_hi = 0.0;
            ws->hs_streak = 0;
            ws->baseline_hr = 0.0f;
            ws->baseline_temp = 0.0f;

            active_remove_unsafe(sen_id);
            continue;
        }

        // 30초: working=0만 (누적은 유지)
        if (gap >= TIMEOUT_WORKING_DOWN_MS) {
            if (ws->working == 1) {
                ws->working = 0;
                ws->hs_streak = 0;
            }
        }

        i++;
    }

    pthread_mutex_unlock(&g_mtx);
}

// =====================
// 내부 수신 스레드 수정본
// =====================
static void* th_rx_thread(void* arg) {
    (void)arg;
    while (1) {
        // 1) 큐에서는 통합 패킷인 SensorPacket* 을 꺼냅니다.
        SensorPacket* pkt = (SensorPacket*)q_pop(&q_th);
        if (!pkt) continue;

        if (pkt->type == TYPE_TH) {
            // 2) 패킷 내부의 THData 주소를 가져와서 룰 처리에 사용
            on_th_data(&pkt->payload.th);

            // 3) DB 적재: 이미 malloc된 SensorPacket이므로 
            // 새로 복제할 필요 없이 그대로 q_db로 넘기면 됩니다. (메모리 소유권 이전)
            q_push(&q_db, pkt);
            
        } else {
            // 타입이 맞지 않는 잘못된 패킷이 들어왔을 경우 버림
            free(pkt);
        }
    }
    return NULL;
}

static void* vital_rx_thread(void* arg) {
    (void)arg;
    while (1) {
        // Vital 쪽도 동일하게 SensorPacket* 으로 처리
        SensorPacket* pkt = (SensorPacket*)q_pop(&q_vital);
        if (!pkt) continue;

        if (pkt->type == TYPE_VITAL) {
            on_vital_data(&pkt->payload.vital);
            
            // 룰 판단 후 원본 패킷을 그대로 q_db로 넘김
            q_push(&q_db, pkt);
            
        } else {
            free(pkt);
        }
    }
    return NULL;
}

static void* timer_thread(void* arg) {
    (void)arg;
    while (1) {
        // “데이터가 없어도” 5초마다 타임아웃 체크를 보장해야
        // 원본 기능과 동일해짐
        sleep(5);
        check_timeouts_once();
    }
    return NULL;
}

// =====================
// rule_module 엔트리(메인 스레드)
// =====================
void* rule_module(void* arg) {
    (void)arg;

    pthread_t t_th, t_vital, t_timer;

    pthread_create(&t_th, NULL, th_rx_thread, NULL);
    pthread_create(&t_vital, NULL, vital_rx_thread, NULL);
    pthread_create(&t_timer, NULL, timer_thread, NULL);

    pthread_join(t_th, NULL);
    pthread_join(t_vital, NULL);
    pthread_join(t_timer, NULL);
    return NULL;
}
