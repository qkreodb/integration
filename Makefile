# make            # main_system 생성
# make test_queue # 큐 무결성 테스트만
# make test_rule  # 룰 모듈 로직 테스트만

# 1. 컴파일러 및 옵션 설정
CC = gcc
CFLAGS = -Wall -Wextra -g -pthread  # -g: 디버깅 정보 포함, -pthread: 스레드 지원
LDFLAGS = -lmodbus -lcjson -lm -pthread

# 2. 파일 이름 정의
TARGET = main_system
TEST_Q_TARGET = test_queue

# 공통 소스 파일 (테스트와 메인 모두에서 사용)
COMMON_SRCS = queue.c
MODULE_SRCS = th_module.c vital_module.c rule_module.c send_module.c
MAIN_SRCS = main.c
HEADERS = shared.h

# 3. 빌드 규칙
.PHONY: all clean

# 기본 빌드: 전체 시스템 생성
all: $(TARGET)

$(TARGET): $(MAIN_SRCS) $(COMMON_SRCS) $(MODULE_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_SRCS) $(COMMON_SRCS) $(MODULE_SRCS) $(LDFLAGS)

# --- 4. 단위 테스트 빌드 규칙 ---

# 큐 무결성 테스트 빌드
$(TEST_Q_TARGET): test_queue.c $(COMMON_SRCS) $(HEADERS)
	$(CC) $(CFLAGS) -o $@ test_queue.c $(COMMON_SRCS) $(LDFLAGS)

# 모든 테스트 한꺼번에 빌드 명령
test: $(TEST_Q_TARGET) $(TEST_RULE_TARGET)

# 5. 정리 규칙
clean:
	rm -f $(TARGET) $(TEST_Q_TARGET) *.o
