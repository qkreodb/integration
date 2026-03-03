#include "shared.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// 이전에 성공했던 워치 포트 번호로 설정
#define WATCH_PORT 5006 

void* send_module(void* arg) {
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1) {
        perror("[Send] socket error");
        return NULL;
    }

    printf("[Send] 모듈 대기 중... (Port: %d)\n", WATCH_PORT);

    while (1) {
        // 1. 큐에서 이벤트가 나올 때까지 대기
        SensorPacket *packet = (SensorPacket*)q_pop(&q_send);
        if (packet == NULL) continue;

        // 2. 이벤트 패킷인 경우에만 전송
        if (packet->type == TYPE_EVENT) {
            struct sockaddr_in watch_addr;
            memset(&watch_addr, 0, sizeof(watch_addr));
            watch_addr.sin_family = AF_INET;
            
            // 이전에 성공했던 방식 그대로: 순수 IP만 전달
            watch_addr.sin_addr.s_addr = inet_addr("192.168.0.61"); 
            watch_addr.sin_port = htons(WATCH_PORT);

            char buffer[1024];
            // 성공했던 JSON 형식 그대로 구성
            snprintf(buffer, sizeof(buffer), "{\"state_code\": \"%s\"}", 
                     packet->payload.event.state_code);

            // 3. 워치로 전송
            int sent_len = sendto(sock, buffer, strlen(buffer), 0, 
                                 (struct sockaddr*)&watch_addr, sizeof(watch_addr));
                                  
            if (sent_len > 0) {
                printf("[Send] 워치 전송 완료: %s\n", buffer);
            } else {
                perror("[Send] sendto error");
            }
        }

        // 4. 메모리 해제
        free(packet);
    }

    close(sock);
    return NULL;
}
