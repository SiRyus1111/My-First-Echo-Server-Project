#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h"
#include <cstdio>

const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 4;

// 서버가 할 일 (클라이언트와 연동)
// 1. 클라이언트로부터 uint32_t형의 헤더를 받아 헤더의 값만큼 페이로드 받기
// 2. 처리하고, 클라이언트의 uint32_t형의 보낸 페이로드 바이트 수를 나타내는 헤더 전송하기
// 3. 연결을 끊을 때.. 어카면 좋을까..?

#pragma comment(lib, "Ws2_32.lib")

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

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cout << "윈속 초기화 실패\n";
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
	server_addr.sin_addr.s_addr = htonl(ADDR_ANY);

	// LISTEN용 소켓에 서버 주소 정보 바인딩 
	if (bind(server_sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		err_quit("bind()");
		return 1;
	}

	// LISTTEN용 소켓을 LISTEN 상태로 전환, 백 로그 크기는 가능한 최대 크기인 SOMAXCONN으로 설정
	if (listen(server_sock, SOMAXCONN) == SOCKET_ERROR) {
		err_quit("listen()");
		return 1;
	}

	// 클라이언트와 직접적으로 통신할 소켓과 소켓 주소 구조체
	SOCKET client_sock;
	sockaddr_in client_addr{};

	// 클라이언트로부터 수신한 메시지를 저장할 버퍼
	char buf[BUFFER_SIZE + 1];
	char buf[BUFFER_SIZE + 1];
	int addr_len;


	// 클라이언트의 접속을 기다리고, 접속이 이루어지면 메시지를 수신한 후 다시 클라이언트로 송신하는 에코 서버
	while (true) {

		addr_len = sizeof(client_addr);

		// accept 함수를 실행. 블로킹임.
		client_sock = accept(server_sock, (sockaddr*)&client_addr, &addr_len);

		if (client_sock == INVALID_SOCKET) {
			err_display("accept()");
			break;
		}

		// 출력용으로 클라이언트 IP 주소를 문자열로 저장
		char addr[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &client_addr, addr, sizeof(addr));

		std::cout << "클라이언트 접속됨 : IP 주소 = " << addr << " 포트 번호 = " << htons(client_addr.sin_port) << '\n';

		struct flags flags;

		while (true) {


			// 헤더 recv()
			int header_received = 0;
			char header_buf[4]{};

			flags.header_recv = true;
			while (header_received < HEADER_SIZE)
			{
				int header_recv_len = recv(client_sock, header_buf + header_received, HEADER_SIZE, 0);

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



			uint32_t header = atoi(header_buf);

			// 보냈을 때 네트워크 바이트 정렬로 보냈으니까 받았을 때 다시 호스트 바이트 정렬로 변환 + 형식도 uint32_t로 유지
			header = ntohl(header);

			// 페이로드 recv()
			int payload_received = 0;

			flags.payload_recv = true;
			while (payload_received < header) {

				int recv_len = recv(client_sock, buf + payload_received, header - payload_received, 0);

				if (recv_len == SOCKET_ERROR) {
					err_display("recv()");
					flags.if_error = true;
					break;
				}
				else if (recv_len == 0) {
					flags.if_client_exit = true;
					break;
				}

				buf[recv_len] = '\0';
			}


			std::cout << "송신한 클라이언트 : IP 주소 = " << ntohl(client_addr.sin_addr.s_addr) << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
			std::cout << "받은 바이트 수 : " << payload_received << " 받은 메시지 : " << buf << '\n';

			// 헤더 send(), 페이로드 send()
			int send_len = send(client_sock, buf, header, 0);

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