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
    printf("[Vital_Module] 가동 시작 (Port: 5005)\n");

    while (1) {
        int len = recvfrom(sock, buffer, sizeof(buffer)-1, 0, (struct sockaddr*)&clnt_addr, &clnt_size);
        if (len <= 0) continue;
        buffer[len] = '\0';

        char *json_start = strchr(buffer, '{');
        if (json_start == NULL) continue;

        cJSON *root = cJSON_Parse(json_start);
        if (root) {
            SensorPacket *packet = (SensorPacket*)malloc(sizeof(SensorPacket));
            packet->type = TYPE_VITAL;
            
            // 데이터 매핑
            packet->payload.vital.sen_id = cJSON_GetObjectItem(root, "sen_id")->valueint;
            packet->payload.vital.wp_id = cJSON_GetObjectItem(root, "wp_id")->valueint; 
            packet->payload.vital.hr = (float)cJSON_GetObjectItem(root, "hr")->valuedouble;
            packet->payload.vital.sk_temp = (float)cJSON_GetObjectItem(root, "sk_temp")->valuedouble;
            packet->payload.vital.time = time(NULL);
            
            q_push(&q_vital, packet); // 큐에 투척
            cJSON_Delete(root);
        }
    }
    return NULL;
}
