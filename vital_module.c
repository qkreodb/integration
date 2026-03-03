#include "shared.h"
#include <cjson/cJSON.h>
#include <arpa/inet.h>

void* vital_module(void* arg) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in serv_addr, clnt_addr;
    socklen_t clnt_size = sizeof(clnt_addr);
    char buffer[1024];

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(5005);
    bind(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr));

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&clnt_addr, &clnt_size);
        if (len <= 0) continue;
        buffer[len] = '\0';

        cJSON *root = cJSON_Parse(buffer);
        if (root) {
            // 큐에 넣을 패킷 메모리 할당
            SensorPacket *packet = (SensorPacket*)malloc(sizeof(SensorPacket));
            packet->type = TYPE_VITAL;
            
            // JSON -> VitalData 구조체 매핑
            // sen_id가 문자열로 오면 이전에 말한 해시 함수를 써서 int로 변환하세요.
            packet->payload.vital.sen_id = cJSON_GetObjectItem(root, "sen_id")->valueint;
            packet->payload.vital.wp_id = cJSON_GetObjectItem(root, "wp_id")->valueint; 
            packet->payload.vital.hr = (float)cJSON_GetObjectItem(root, "hr")->valuedouble;
            packet->payload.vital.sk_temp = (float)cJSON_GetObjectItem(root, "sk_temp")->valuedouble;
            packet->payload.vital.time = time(NULL);
            
            printf("[Watch] 수집: %.1f, %.1f°C\n",packet->payload.vital.hr, packet->payload.vital.sk_temp);

            q_push(&q_vital, packet); // 큐에 투척
            cJSON_Delete(root);
        }
    }
    return NULL;
}
