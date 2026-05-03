# My First Echo Server Project (Winsock2, C++)

TCP 기반 Echo Server 구현 프로젝트입니다.

> 이 프로젝트는 단순히 받은 메시지를 그대로 돌려보내는 Echo Server를 만드는 데서 끝나지 않고,  
> TCP의 바이트 스트림 특성, partial send/recv, 헤더 무결성 검사, 에러 패킷 처리까지 고려한  
> 기초적인 애플리케이션 레벨 프로토콜 설계를 목표로 했습니다.

> 또한 초기 절차형 구조에서 시작해,  
> 이후 OOP / RAII 기반 구조로 전체 리팩토링을 진행하여  
> socket lifetime 관리와 자원 정리를 객체 중심으로 재구성했습니다.

---

## 1. 프로젝트 목표

이 프로젝트에서 구현한 핵심 목표는 다음과 같습니다.

- TCP 기반 Echo Server / Client 구현
- TCP byte stream 특성을 고려한 partial send / recv 처리
- `send_all()` / `recv_all()` helper 함수 구현
- length-prefix 기반 메시지 구조 설계
- `PacketHeader` 구조체를 이용한 패킷 타입 + 길이 관리
- protocol error / transport error / peer exit 구분 처리
- 상태 기반 통신 흐름 관리 (`NetState` 구조체)
- 헤더 무결성 검사 및 에러 패킷 처리
- OOP / RAII 기반 socket 자원 관리 구조 적용

---

## 2. 프로젝트 구조

- `Server.cpp` : 서버 코드
- `Client.cpp` : 클라이언트 코드
- `Common.h` : 공통 오류 출력 함수 포함 (`err_quit()`, `err_display()` 등)
- `NetCommon.h` : `PacketHeader`, `NetState`, `send_all()`, `recv_all()` 포함

---

## 3. 메시지 구조

본 프로젝트는 다음과 같은 패킷 구조를 사용합니다.

```text
[PacketHeader][Payload]
```

### PacketHeader

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    int32_t type;
    uint32_t length;
};
#pragma pack(pop)
```

* `type` : 패킷 타입

  * `SAFE (-1)` : 일반 메시지
  * `HEADER_ERROR (0)` : 에러 메시지
* `length` : payload 길이 (바이트 단위)

  * 최대 4096 bytes
* network byte order로 변환하여 송수신

  * `htonl()` / `ntohl()` 사용

### Payload

* 최대 4096 bytes
* 문자열의 실제 길이만 전송
* 널 문자(`'\0'`)는 전송하지 않음
* 수신지에서 출력용으로 마지막에 직접 `'\0'` 추가

---

## 4. 왜 4바이트 length 헤더에서 구조체 헤더로 바꿨는가

처음에는 다음과 같은 단순 구조를 사용했습니다.

```text
[4 byte length][payload]
```

이 방식은 payload 길이를 표현하는 데에는 충분했지만,
패킷의 의미를 구분하기 어렵다는 문제가 있었습니다.

예를 들어,

* 이 패킷이 일반적인 echo 메시지인지
* 상대가 보낸 에러 메시지인지

를 헤더만 보고 구분하기 어려웠습니다.

그래서 헤더를 `PacketHeader` 구조체로 확장하여,

* 패킷의 타입(`type`)
* payload 길이(`length`)

를 함께 표현할 수 있도록 개선했습니다.

이를 통해 정상 메시지와 에러 메시지를 같은 프로토콜 안에서 일관되게 처리할 수 있게 되었습니다.

---

## 5. partial send / recv 문제 해결

TCP는 message boundary를 보장하지 않는 byte stream 기반 프로토콜입니다.

즉,

* 한 번의 `send()`로 보낸 데이터가
* 한 번의 `recv()`로 동일한 단위 그대로 도착한다고 보장할 수 없습니다.

이 프로젝트에서는 이 문제를 해결하기 위해 다음 helper 함수를 구현했습니다.

* `send_all()`
* `recv_all()`

### send_all()

* 요청한 길이만큼 반복해서 `send()`
* `SOCKET_ERROR` 발생 시 즉시 반환
* 성공 시 실제 송신한 총 바이트 수 반환

### recv_all()

* 요청한 길이만큼 반복해서 `recv()`
* `SOCKET_ERROR` 발생 시 즉시 반환
* `recv() == 0` 이면 peer graceful shutdown으로 판단
* 성공 시 실제 수신한 총 바이트 수 반환

이 helper 함수들을 통해
헤더 / 페이로드 송수신 로직을 공통화하고,
partial send / recv를 반복적으로 직접 처리해야 하는 중복 코드를 줄였습니다.

---

## 6. 상태 관리 구조체 (`NetState`)

통신 과정의 현재 단계와 예외 상황을 추적하기 위해 상태 관리 구조체를 사용했습니다.

예:

* `header_send`
* `payload_send`
* `header_recv`
* `payload_recv`

예외 관련 상태:

* `if_error`
* `if_peer_exit`
* `if_header_error`
* `if_peer_error`

### 역할 분리 원칙

* `send_all()` / `recv_all()` 내부에서는
  transport error와 peer exit 같은 **통신 과정의 예외 상태**만 기록
* 현재 “무엇을 송수신 중인지” (`header_send`, `payload_send` 등)는
  각 메인 루프에서 직접 관리

즉,

* helper 함수는 송수신 자체만 담당
* main 루프는 현재 프로토콜 단계와 흐름을 담당

하도록 역할을 분리했습니다.

---

## 7. 에러 처리 정책

이 프로젝트에서는 다음 세 가지를 구분해서 처리합니다.

| error type      | 설명                                      | 처리 방식               |
| --------------- | --------------------------------------- | ------------------- |
| protocol error  | 헤더 해석 실패 / 헤더 길이 초과 / 에러 패킷 수신          | 에러 패킷 전송 또는 수신 후 종료 |
| transport error | `send()` / `recv()` 실패 (`SOCKET_ERROR`) | 메시지 없이 종료           |
| peer exit       | 상대 graceful shutdown (`recv() == 0`)    | 메시지 없이 종료           |

### protocol error

예:

* 헤더의 `length` 값이 4096 초과
* 상대가 `HEADER_ERROR` 타입 패킷 송신

이 경우 TCP 연결 자체가 반드시 깨진 것은 아니므로,
가능하면 에러 패킷을 전송하거나, 상대가 보낸 에러 패킷을 정상적으로 수신한 뒤 종료합니다.

### transport error

예:

* `send() == SOCKET_ERROR`
* `recv() == SOCKET_ERROR`

이 경우는 전송 계층에서 이미 연결 상태를 신뢰하기 어려우므로,
추가적인 메시지를 보내지 않고 바로 종료합니다.

### peer exit

`recv() == 0`인 경우,
상대가 정상적으로 연결 종료를 수행했다고 보고 메시지 없이 종료합니다.

---

## 8. 헤더 무결성 검사

다음 단계에서 헤더를 검사합니다.

* 클라이언트에서 사용자 입력 후 송신 전
* 서버에서 헤더 수신 후 해석 직후
* 클라이언트에서 헤더 수신 후 해석 직후

검사 내용:

* `length > 4096` 인지 여부

이를 초과하면 정상적인 payload 수신이 불가능하다고 보고 protocol error로 처리합니다.

---

## 9. 에러 패킷 처리

패킷 타입이 `HEADER_ERROR`인 경우에도,
길이 정보(`length`)에 맞춰 payload를 끝까지 수신한 뒤 메시지를 출력하도록 했습니다.

즉, 에러 메시지도 일반 메시지와 동일하게

```text
[PacketHeader][Payload]
```

형태를 유지합니다.

이를 통해 에러 상황도 프로토콜 내부에서 일관되게 처리할 수 있도록 했습니다.

---

## 10. OOP / RAII 기반 구조 리팩토링

초기 구현은 절차형 Winsock 코드였지만,
이후 서버의 `main()` 함수와 클라이언트의 전체 코드를
OOP / RAII 방식으로 리팩토링했습니다.

리팩토링의 주요 목적은 다음과 같습니다.

* socket lifetime 자동 관리
* Winsock 초기화 / 정리 자동화
* listen socket / connect socket / client socket 역할 분리
* transport layer 책임 캡슐화
* socket ownership 명확화
* 향후 multi-client 구조 확장 기반 마련

---

### 10-1. WinsockGuard

Winsock 초기화와 정리를 자동으로 수행하기 위해
RAII 기반 `WinsockGuard` 클래스를 도입했습니다.

역할:

* 생성자 → `WSAStartup()`
* 소멸자 → `WSACleanup()`

이를 통해

* 초기화 누락 방지
* cleanup 누락 방지
* 예외 발생 시에도 자동 정리

가 가능하도록 설계했습니다.

서버와 클라이언트 양쪽 모두에 적용했습니다.

---

### 10-2. ListenSocket

서버의 listen socket을 관리하는 클래스입니다.

역할:

* socket 생성
* `setsockopt(SO_REUSEADDR)`
* `bind()`
* `listen()`
* `accept()`

listen socket resource lifetime을
객체 lifetime과 동일하게 유지하도록 설계했습니다.

또한 socket handle ownership을 명확히 하기 위해

```cpp
ListenSocket(const ListenSocket&) = delete;
ListenSocket& operator=(const ListenSocket&) = delete;
```

를 사용하여 복사를 금지했습니다.

---

### 10-3. ClientSocket

서버에서 `accept()` 이후 생성되는 client socket을 관리하는 클래스입니다.

역할:

* `send_all()` 기반 송신
* `recv_all()` 기반 수신
* `closesocket()` 자동 정리

partial send / recv 처리를 담당하는 helper 함수와 연동하여
transport layer 책임을 캡슐화했습니다.

이 클래스 역시 socket ownership 중복을 방지하기 위해 복사를 금지했습니다.

또한 `ListenSockAccept()`에서 값을 반환할 수 있도록
이동 생성자를 추가로 정의하여,
기존의 “복사 금지와 반환값 충돌” 문제를 해결했습니다.

---

### 10-4. ConnectSocket

클라이언트 측에서 서버에 연결하는 socket을 관리하는 클래스입니다.

역할:

* socket 생성
* `connect()`
* `send_all()` 기반 송신
* `recv_all()` 기반 수신
* `closesocket()` 자동 정리

즉 기존 클라이언트 코드에서 `client_sock`이 담당하던 역할을
객체 단위로 캡슐화한 구조입니다.

이 클래스도 복사를 금지하여
socket ownership이 중복되지 않도록 했습니다.

---

### 10-5. 설계 의도

본 리팩토링의 핵심 목표는 다음 구조를 만드는 것이었습니다.

```text
socket resource lifetime == object lifetime
```

이를 통해

* resource leak 방지
* double close 방지
* ownership 명확화
* 예외 안전성 확보

를 달성할 수 있도록 설계했습니다.

또한 socket별 역할을 분리함으로써

* Winsock 초기화/종료
* 서버의 listen용 socket
* 서버의 client 통신용 socket
* 클라이언트의 connect용 socket

을 각각 독립된 책임 단위로 나눌 수 있었습니다.

---

## 11. 현재 구현된 기능

* 단일 클라이언트 Echo Server
* length-prefix 기반 패킷 송수신
* `PacketHeader(type, length)` 기반 메시지 처리
* partial send / recv 대응
* 에러 패킷 송수신 처리
* 헤더 무결성 검사
* 상태 기반 통신 흐름 추적
* 서버 `main()` 함수 OOP / RAII 리팩토링 완료
* 클라이언트 전체 코드 OOP / RAII 리팩토링 완료
* `ListenSockAccept()` 반환과 복사 금지 충돌 문제를 이동 생성자로 해결

---

## 12. 사용 환경

* Windows
* C++
* Winsock2
* Visual Studio

---

## 13. 실행 방법

### 1. 공통 헤더 준비

`include` 디렉터리에 있는 `Common.h`, `NetCommon.h`를 인클루드해야 합니다.

### 2. 서버 실행

* `Server.cpp` 실행

### 3. 클라이언트 실행

* `Client.cpp` 실행

### 4. 메시지 입력

* 최대 4096 bytes까지 입력 가능
* `quit` 입력 시 클라이언트 종료

---

## 14. 향후 개선 예정

* 멀티 클라이언트 Echo Server로 확장
* 멀티스레드 구조 적용
* `ClientManager`와 컨테이너 기반 클라이언트 관리
* mutex 기반 동기화 추가
* 브로드캐스트 기능 추가
* RAII 구조를 멀티클라이언트 환경에 맞게 확장

---

## 15. 개발 과정

개발 과정 및 디버깅 / 리팩토링 기록:

* [Velog 개발 기록 시리즈](https://velog.io/@siryus0907/series/%EC%97%90%EC%BD%94-%EC%84%9C%EB%B2%84-%ED%94%84%EB%A1%9C%EC%A0%9D%ED%8A%B8)

---
