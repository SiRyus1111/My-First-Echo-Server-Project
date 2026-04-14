# My First Echo Server Project (Winsock2, C++)

TCP 기반 Echo Server 구현 프로젝트입니다.

이 프로젝트는 단순 echo 기능 구현을 넘어,
TCP의 바이트 스트림 특성과 partial send/recv 문제를 고려한
안정적인 메시지 송수신 구조 설계를 목표로 했습니다.

---

## 1. 프로젝트 목표

이 프로젝트에서 구현한 핵심 목표:

- TCP length-prefix 기반 메시지 구조 설계
- partial send / recv 대응 send_all / recv_all 구현
- protocol error / transport error 구분 처리
- 상태 기반 통신 흐름 관리(flags 구조체)
- header 무결성 검사

---

## 2. 메시지 구조

본 프로젝트는 다음과 같은 메시지 구조를 사용합니다:

[4 byte header][payload]

header:

- payload 길이 (uint32_t)
- network byte order (htonl 사용)

payload:

- 최대 4096 bytes

---

## 3. partial send / recv 문제 해결

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜입니다.

따라서 다음과 같은 helper 함수를 구현했습니다:

- send_all()
- recv_all()

이 함수들은 다음을 보장합니다:

- 요청한 길이만큼 반복 송수신
- SOCKET_ERROR 처리
- peer graceful shutdown 처리 (recv == 0)

---

## 4. 상태 관리 구조체(flags)

통신 흐름 추적을 위해 상태 구조체를 사용했습니다:

예:

- header_send
- payload_send
- header_recv
- payload_recv
- if_error
- if_header_error
- if_peer_exit

send_all / recv_all 내부에서는 transport error만 기록하고

현재 송수신 단계(header/payload)는
main()에서 관리하도록 역할을 분리했습니다.

---

## 5. 에러 처리 정책

본 프로젝트에서는 다음과 같이 에러를 구분합니다:

| error type | 설명 | 처리 방식 |
|-----------|------|-----------|
| protocol error | header 해석 실패 | 메시지 전송 후 종료 |
| transport error | send/recv 실패 | 메시지 없이 종료 |
| peer exit | 상대 graceful shutdown | 메시지 없이 종료 |

---

## 6. header 무결성 검사

다음 3단계에서 header 크기를 검사합니다:

- client 입력 직후
- server header 수신 직후
- client header 수신 직후

payload 최대 크기:

4096 bytes

이를 초과하면 protocol error 처리합니다.

---

## 7. 사용 환경

- Windows
- C++
- Winsock2
- Visual Studio

---

## 8. 실행 방법

include 디렉터리에 있는 Common.h 헤더를 인클루드

server 실행: My First Echo Server Project/Server.cpp

client 실행: Client/Client.cpp

---

## 9. 향후 개선 예정

- multi-client 지원
- select / IOCP 적용(가능하다면)
- 메시지 큐 구조 도입(채팅 서버로 확장시)
- thread-safe 구조 개선
