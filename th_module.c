#include <modbus/modbus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>
#include "shared.h"

static modbus_t *g_ctx = NULL;
static char g_ip[64] = "192.168.0.20"; // 센서 IP에 맞게 수정
static int  g_port = 502;
static const int SLAVE_ID = 1;

static void _apply_common_options(modbus_t *c) {
    modbus_set_slave(c, SLAVE_ID);
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    modbus_set_response_timeout(c, tv.tv_sec, tv.tv_usec);
}

static int _soft_reconnect(void) {
    if (!g_ctx) return -1;
    modbus_close(g_ctx);
    if (modbus_connect(g_ctx) == -1) return -1;
    _apply_common_options(g_ctx);
    return 0;
}

static int _hard_recreate(void) {
    if (g_ctx) {
        modbus_close(g_ctx);
        modbus_free(g_ctx);
        g_ctx = NULL;
    }
    g_ctx = modbus_new_tcp(g_ip, g_port);
    if (!g_ctx) return -1;
    _apply_common_options(g_ctx);
    if (modbus_connect(g_ctx) == -1) {
        modbus_free(g_ctx);
        g_ctx = NULL;
        return -1;
    }
    return 0;
}

void* th_module(void* arg) {
    (void)arg;
    uint16_t reg[2];

    printf("[TH_Module] 모듈 가동 시작\n");
    if (_hard_recreate() != 0) printf("[TH] 초기 연결 실패\n");

    while (1) {
        int rc = modbus_read_input_registers(g_ctx, 0, 2, reg);
        if (rc != 2) {
            _soft_reconnect();
            sleep(2);
            continue;
        }

        SensorPacket *packet = (SensorPacket*)malloc(sizeof(SensorPacket));
        packet->type = TYPE_TH;
        packet->payload.th.sen_id = 201;
        packet->payload.th.wp_id = 1;
        packet->payload.th.temp = reg[0] / 10.0f;
        packet->payload.th.humd = reg[1] / 10.0f;
        packet->payload.th.time = time(NULL);

        q_push(&q_th, packet);
        printf("[TH] 수집: %.1f°C, %.1f%%\n", packet->payload.th.temp, packet->payload.th.humd);
        sleep(5);
    }
    return NULL;
}
