#include <iostream> // 콘솔 입출력 용 - cout, cin, ...
#include <winsock2.h> // 윈속2 메인 헤더 - socket(), bind(), listen(), accept(), recv(), send(), ...
#include <ws2tcpip.h> // 윈속2 확장 헤더 - inet_ntop(), inet_pton(), ...
#include "Common.h" // 사용자 정의 라이브러리. 소켓 함수 오류 출력 함수 포함. err_quit(), err_display() 함수는 Common.h에 정의되어 있음.
// #include <cstdio> / 이거 왜 썼을까? / 일단 지금은 이 라이브러리가 있었다는 기록만 남겨둠. 주석 처리. 주석 처리.
#include <cstdlib> // atoi() 함수 사용하기 위해서
#include <cstring> // memcpy() 함수 사용하기 위해서

const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 4;

// 서버가 할 일 (클라이언트와 연동)
// 1. 클라이언트로부터 uint32_t형의 헤더를 받아 헤더의 값만큼 페이로드 받기
// 2. 처리하고, 클라이언트의 uint32_t형의 보낸 페이로드 바이트 수를 나타내는 헤더 전송하기
// 3. 연결을 끊을 때.. 어카면 좋을까..?

#pragma comment(lib, "Ws2_32.lib")

// 상태 관리 구조체
struct flags {

	bool header_recv = false;
	bool payload_recv = false;
	bool header_send = false;
	bool payload_send = false;

	bool if_error = false;
	bool if_client_exit = false;

};

int main() {
	WSADATA wsa;
	int WSAStartup_result = WSAStartup(MAKEWORD(2, 2), &wsa);

	if (WSAStartup_result != 0) {
		std::cout << "윈속 초기화 실패. 에러 코드 : " << WSAStartup_result << '\n';
		return 1;
	}


	SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // LISTEN용 소켓

	if (server_sock == INVALID_SOCKET) {
		err_quit("socket()");
		return 1;
	}

	sockaddr_in server_addr{}; // LISTEN용 소켓의 소켓 주소 구조체

	// 서버 주소 정보 설정
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(9000);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	// LISTEN용 소켓에 서버 주소 정보 바인딩 
	if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		err_quit("bind()");
		return 1;
	}

	// LISTEN용 소켓을 LISTEN 상태로 전환, 백 로그 크기는 가능한 최대 크기인 SOMAXCONN으로 설정
	if (listen(server_sock, SOMAXCONN) == SOCKET_ERROR) {
		err_quit("listen()");
		return 1;
	}

	// 클라이언트와 직접적으로 통신할 소켓과 소켓 주소 구조체
	SOCKET client_sock;
	sockaddr_in client_addr{};

	// 클라이언트로부터 수신한 메시지를 저장할 버퍼
	// 버퍼의 마지막 바이트는 문자열이 끝나는 지점을 나타내는 널 문자('\0')를 저장.
	char buf[BUFFER_SIZE];
	int addr_len;


	// 클라이언트의 접속을 기다리고, 접속이 이루어지면 메시지를 수신한 후 다시 클라이언트로 송신하는 에코 서버
	while (true) {

		addr_len = sizeof(client_addr);

		// accept 함수를 실행. accept 함수는 블로킹임. 그래서 연결 정보가 백로그 큐에 들어올 때까지 이 부분에서 멈춰있음. 이 함수를 지나쳐갈까 걱정할 필요 X
		client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);

		if (client_sock == INVALID_SOCKET) {
			err_display("accept()");
			continue; // accept() 실패했을 때는 클라이언트와의 연결이 이루어지지 않은 상태이므로, 다음 반복으로 넘어가서 다시 accept() 시도
		}

		// 출력용으로 클라이언트 IP 주소를 문자열로 저장
		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr.sin_addr, addr, sizeof(addr));

		std::cout << "클라이언트 접속됨 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';

		struct flags flags;

		while (true) {


			// 헤더 recv()
			int header_received = 0;
			char header_buf[4]{};

			flags.header_recv = true;
			while (header_received < HEADER_SIZE)
			{
				int header_recv_len = recv(client_sock, header_buf + header_received, HEADER_SIZE - header_received, 0);

				if (header_received != HEADER_SIZE) {
					std::cout << "헤더 부분적 수신 : " << header_recv_len << "바이트 수신됨.\n";
				}
				if (header_recv_len == SOCKET_ERROR) {
					err_display("recv()");
					flags.if_error = true;
					break;
				}
				else if (header_recv_len == 0) {
					flags.if_client_exit = true;
					break;
				}

				header_received += header_recv_len;
			}

			if (flags.if_error || flags.if_client_exit) break;

			flags.header_recv = false;

			std::cout << "헤더 수신 완료 : 총 " << header_received << "바이트 수신됨.\n";



			uint32_t net_header;
			memcpy(&net_header, header_buf, HEADER_SIZE);

			// 보냈을 때 네트워크 바이트 정렬로 보냈으니까 받았을 때 다시 호스트 바이트 정렬로 변환 + 형식도 uint32_t로 유지
			uint32_t host_header = ntohl(net_header);

			// 페이로드 recv()
			int payload_received = 0;

			flags.payload_recv = true;
			while (payload_received < host_header) {

				int recv_len = recv(client_sock, buf + payload_received, host_header - payload_received, 0);

				if (recv_len == SOCKET_ERROR) {
					err_display("recv()");
					flags.if_error = true;
					break;
				}
				else if (recv_len == 0) {
					flags.if_client_exit = true;
					break;
				}

				payload_received += recv_len;
			}
			buf[payload_received] = '\0';

			std::cout << "송신한 클라이언트 : IP 주소 = " << ntohl(client_addr.sin_addr.s_addr) << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
			std::cout << "받은 바이트 수 : " << payload_received << " 받은 메시지 : " << buf << '\n';

			// 헤더 send(), 페이로드 send()
			int send_len = send(client_sock, buf, host_header, 0);

			if (send_len == SOCKET_ERROR) {
				err_display("send()");
				break;
			}
			std::cout << addr << " : " << htons(client_addr.sin_port) << " 클라이언트로 " << send_len << " 바이트 보냄\n";
		}

	}

	closesocket(server_sock);

	return 0;
}