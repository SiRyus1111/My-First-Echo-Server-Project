# 개발 로그

### 2026.04.09

- send() 과정(헤더 / 페이로드 send) 추가
- 헤더 해석 / 헤더 처리 추가
- host\_send\_header를 strlen(buf)로 잡았는데, 이건 payload_received의 값을 사용하는게 더 맞을 것 같아서 수정
- 다음 예정 : 서버 코드 closesocket()까지 짜보고, 클라이언트도 기본적인 구조는 다 잡아진 상태까지 코드 짜놓기.
- 메모:
	- 예외 처리할 부분은 싹다 나중에 처리해보자. 지금은 일단 전체적인 기능 구현, 틀 잡기 우선.
	- recv() / send() 파트 함수 분리는 빠르게 해보고싶음. 바로다음(04/11)에 가능하면?
	- send() 부분은 일단 예외 처리 / 함수 분리 하기 전까지는 해야될듯.

### 2026.04.10

- 서버 코드 - 클라이언트의 연결 종료 / 통신 과정에서 발생한 오류 처리 - 연결 종료 추가
- 오류가 발생한 부분을 판별할 때 더 과정이 늘어난다면 if문이 난잡해져서 Branch Prediction 성능이 떨어질 것으로 보임. 언젠가 수정 필요.
- 지금은 단순 땜빵임. 나중에 오류 처리에 대해 조금 더 고민 필요.
- 클라이언트 기본적인 코드 작성. 추가적인 예외처리 필요.
- 빌드 과정에서의 오류 한 개 처리
  - strcpy() - string copy(문자열 복사) 함수가 구식 함수라 발생한 오류. 신식 함수인 strcpy_s()로 교체해서 해결.
- 실행 후의 오류들 처리
  - 2바이트짜리 포트 번호를 htonl() / ntohl() 로 4바이트로 취급해서 endian을 변환해서 "요청한 주소는 해당 컨텍스트에서 유효하지 않습니다(WSAEADDRNOTAVAIL, 10049)" 에러 발생, htons() / ntohs() 로 변경하여 해결.
  - strcpy_s의 Debuf Assertion Failed 에러 처리. 이게 버퍼 공간이 부족하다고 했는데, 버퍼는 4096바이트였고, 나는 1바이트만 입력해서 문제 없는 줄 알았는데, strcpy_s의 두 번째 인자(dest_size)에 버퍼의 크기가 아니라 입력받은 문자열의 길이(user_input.size())를 넘겨주어서 발생한 문제. dest_size에 sizeof(buf)을 넘겨주어서 해결.
  - 한번 실행 후 Address already in use 에러 발생. 연결을 종료하고도 TIME_WAIT 상태인 액티브 클로즈 호스트(루프백 주소 사용하므로 자기 자신) 때문에 발생한 문제인데, setsockopt() 함수를 이용한 SO_REUSEADDR로 TIME_WAIT 상태인 포트를 즉시 재사용할 수 있게 허용.
- 다음 예정 : 예외 처리. 지피티 & 제미나이에게 코드 보여주고 해야하는 예외처리 물어볼 생각. 일단 버퍼 크기 초과에 대한 예외처리는 먼저 하고.
- 메모 :
  - 정상 동작은 확인했으니, 이제 예외 처리와 함수 분리, 기타 리팩토링이 남았다.
  - flags 구조체 변수 이름 바꾸는거 자꾸 미루고 있는데, 이거 다음에 꼭 하자..

### 2026.4.13

- 헤더의 버퍼 크기 초과 예외 처리 추가.
  - 클라이언트에서 메시지를 입력 받고 메시지의 바이트 수를 잰 후
  - 서버에서 클라이언트가 보낸 헤더를 recv() 한 후
  - 클라이언트에서 서버가 보낸 헤더를 recv() 한 후
  - 만약 버퍼 최대 크기인 4096(바이트) 이상이라면 에러로 처리한다. (state.if_header_error = true)
  - 클라이언트에서 메시지를 입력 받고 메시지의 바이트 수를 잰 후에는 바이트 초과 메시지를 출력 후 다시 입력을 받는다.
  - 그리고 헤더를 recv() 한 후에는 바로 break하여 페이로드 수신을 차단한다.
- send_all() / recv_all()로 기존 send() / recv() 로직 분리
  - partial send / recv 처리 로직을 공통 함수로 통합
  - `int send_all(SOCKET sock, flags& state, const char* msg, int len)`
  - `int recv_all(SOCKET sock, flags& state, char* buf, int len)`
  - 반환값 : 
	- 상대가 연결 종료시(recv() == 0 / recv_all() 한정) = 0
	- 전송 과정에서 send() / recv() 가 SOCKET_ERROR를 반환할 시 = SOCKET_ERROR
	- 정상적으로 send() / recv() 루프를 마칠 시 = send() / recv()로 처리한 총 바이트 수
  - 통신 과정에서의 예외 상황은 함수 내에서 flags형 구조체인 state에 기록.
  - 현재 진행하는 과정을 알려주는 state의 멤버는 함수 밖에서 state에 기록해야함.
- 서버 -> 클라이언트 에러 메시지 전송 정책 설계
  - 헤더에 너무 큰 값(>4096)이 들어왔을 때(state.if_header_error == true) - 클라이언트에 메시지 보내고 서버 종료
  - 클라이언트가 종료(state.if_peer_exit == true (recv_all() == 0)) - 클라이언트에 아무런 메시지 보내지 않고 종료(딱히 클라이언트에 서버에서의 연결 종료를 알릴 이유가 없음) 
  - send() / recv()가 SOCKET_ERROR를 반환함(state.if_error == true) - 클라이언트에 아무런 메시지 보내지 않고 종료(SOCKET_ERROR가 반환되었다는건 이미 연결이 깨졌을 가능성이 충분히 있기 때문에)
  - transport error(if_error)와 protocol error(if_header_error)를 구분하여 종료 정책을 다르게 설계
- 서버의 flags 구조체 변수 이름 변경
  - flags -> server_state
- flags 구조체의 멤버를 추가 & 가독성을 위해 기존 멤버의 이름을 변경
  - if_header_error 추가
  - if_server_exit / if_client_exit -> if_peer_exit

### 2026.4.15

- 헤더 개선
  - 헤더를 PacketHeader 구조체로 정의
  - type : int32_t 형, 해당 메시지(페이로드 포함)의 타입
	- 0(HEADER_ERROR)이라면 에러 메시지
	- -1(HEADER_SAFE)이라면 정상적인 메시지
  - length : uint32_t 형, 페이로드의 길이
	- 4096바이트 초과시 오류 처리
  - 기존 헤더 방식으로는 헤더의 확장이 상당히 불편해서 개선 - 상대 호스트에게 에러 메시지를 전송하고 에러 메시지를 상대가 인식하게 하기 위해서 기존 4바이트 헤더에 별 짓을 다 하다가 결국 구조체로 정의.
  - #pragma pack(push, 1)로 의도치않은 패딩 발생 방지
- 헤더의 type 멤버가 HEADER_ERROR일 때의 처리 추가
  - 일단 헤더의 length 멤버에 맞춰서 페이로드 recv()까지는 정상적으로 실행한 후 받은 메시지 출력까지 하고 헤더의 type 멤버 검사 후 HEADER_ERROR라면 송수신 중지
  - 상대에게서 에러 메시지 수신시, 확실히 페이로드 recv()까지 수행 후 종료
  - 상대에게서 발생한 에러의 에러 메시지도 정상적인 패킷으로 인식하고 끝까지 recv()
- 상태 관리 구조체에 새 멤버 추가
  - if_peer_error - 상대 호스트에 메시지를 전송 가능한(현 시점에는 protocol error만) 에러가 발생했을 경우에 true
  - 헤더의 type이 HEADER_ERROR일 때를 나타내기 위해 추가
  - 에러 추적 세분화
- 메시지 길이 측정 방식 개선
  - sizeof()으로 널문자('\0')까지 재버리는 기존 방식 대신, strlen()으로 널문자를 제외한 문자열 길이 측정
  - 페이로드의 길이 측정 방식의 일관성을 위해서 개선
  - 수신지에서 널문자를 붙여서 출력하는 방식을 사용하므로, 널문자까지 전송할 필요 없음