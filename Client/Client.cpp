#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h"
#include <cstring>
#include <string>

const char* SERVER_ADDR = "127.0.0.1";
const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 4;

struct flags {
	bool header_recv = false;
	bool payload_recv = false;
	bool header_send = false;
	bool payload_send = false;

	bool if_error = false;
	bool if_server_exit = false;
};

// 헤더 규칙
// 첫 4바이트 = uint32_t 페이로드 크기(길이)

int main() {

	WSADATA wsa;
	int WSAStartup_result = WSAStartup(MAKEWORD(2, 2), &wsa);
	
	if (WSAStartup_result != 0) {
		std::cout << "윈속 초기화 실패. 에러 코드 : " << WSAStartup_result << '\n';
		return 1;
	}

	SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // 서버와 통신할 소켓

	if (sock == INVALID_SOCKET) {
		err_quit("socket()");
		return 1;
	}

	char buf[BUFFER_SIZE + 1];

	sockaddr_in server_addr{};

	server_addr.sin_family = AF_INET;
	inet_pton(AF_INET, SERVER_ADDR, &server_addr.sin_addr);
	server_addr.sin_port = htons(SERVER_PORT);

	if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
		err_quit("connect");
		return 1;
	}

	std::cout << "서버에 연결 성공\n";

	flags client_state;
	while (true) {

		std::string user_input;
		std::cout << "서버에 보낼 메시지 입력(최대 4096바이트, 서버와 연결 종료 희망시 quit 입력) : ";
		std::getline(std::cin, user_input);

		if (user_input == "quit") break;

		strcpy_s(buf, sizeof(buf), user_input.c_str());

		// 입력 받은 바이트 수 세기
		uint32_t host_header = user_input.size();
		uint32_t net_header = htonl(host_header);
		
		char header_buf[HEADER_SIZE];

		memcpy(header_buf, &net_header, sizeof(header_buf));

		// 헤더, 페이로드 각각 send()
		int header_sent = 0;

		client_state.header_send = true;
		while (header_sent < HEADER_SIZE) {
			int header_send_len = send(sock, header_buf + header_sent, HEADER_SIZE - header_sent, 0);

			if (header_send_len == SOCKET_ERROR) {
				err_display("send");
				client_state.if_error = true;
				break;
			}

			header_sent += header_send_len;

			if (header_sent != HEADER_SIZE) {
				std::cout << "헤더 부분적 송신 : " << header_send_len << "바이트 송신됨.\n";
			}
		}
		
		if (client_state.if_error) break;

		client_state.header_send = false;

		std::cout << "헤더 송신 완료 : 총" << header_sent << "바이트 송신됨.\n";

		int payload_sent = 0;

		client_state.payload_send = true;
		while (payload_sent < host_header) {
			int payload_send_len = send(sock, buf + payload_sent, host_header - payload_sent, 0);

			if (payload_send_len == SOCKET_ERROR) {
				err_display("send()");
				client_state.if_error = true;
				break;
			}

			payload_sent += payload_send_len;

			if (payload_sent != host_header) {
				std::cout << "페이로드 부분적 송신 : " << payload_send_len << "바이트 송신됨.\n";
			}
		}

		if (client_state.if_error) break;

		client_state.payload_send = false;

		std::cout << "페이로드 송신 완료 : 총 " << payload_sent << "바이트 송신됨.\n";

		// 헤더, 페이로드 각각 recv()
		int header_received = 0;
		client_state.header_recv = true;
		while (header_received < HEADER_SIZE) {
			int header_recv_len = recv(sock, header_buf + header_received, HEADER_SIZE - header_received, 0);

			if (header_recv_len == SOCKET_ERROR) {
				err_display("recv()");
				client_state.if_error = true;
				break;
			}
			if (header_recv_len == 0) {
				client_state.if_server_exit = true;
				break;
			}

			header_received += header_recv_len;

			if (header_received != HEADER_SIZE) {
				std::cout << "헤더 부분적 수신 : " << header_recv_len << "바이트 수신됨.\n";
			}
		}
		if (client_state.if_error || client_state.if_server_exit) break;

		client_state.header_recv = false;

		std::cout << "헤더 수신 완료 : 총 " << header_received << "바이트 수신됨. 헤더를 해석합니다.\n";
		
		uint32_t received_net_header;
		memcpy(&received_net_header, header_buf, HEADER_SIZE);
		uint32_t received_host_header = ntohl(received_net_header);

		std::cout << "헤더 해석 완료. 페이로드를 수신합니다.\n";

		int payload_received = 0;

		client_state.payload_recv = true;
		while (payload_received < received_host_header) {
			int payload_recv_len = recv(sock, buf + payload_received, received_host_header - payload_received, 0);

			if (payload_recv_len == SOCKET_ERROR) {
				err_display("recv()");
				client_state.if_error = true;
				break;
			}
			if (payload_recv_len == 0) {
				client_state.if_server_exit = true;
				break;
			}

			payload_received += payload_recv_len;

			if (payload_received != received_host_header) {
				std::cout << "페이로드 부분적 수신 : " << payload_recv_len << "바이트 수신됨.\n";
			}
		}
		if (client_state.if_error || client_state.if_server_exit) break;

		client_state.payload_recv = false;

		buf[received_host_header] = '\0';

		std::cout << "[ECHO FROM SERVER]" << buf << '\n';
	}
	// break시 오류 발생 체크, 클라이언트 종료하기
	// 여기도 조건 많이 추가되면 Branch Prediction 성능 떨어질 듯..
	// 근데 어떻게 해야할지 모르겠다.. 원래 이렇게 해도 되는건가?
	if (client_state.if_error) {
		std::cout << "서버와의 통신 과정에서 오류 발생 : ";

		if (client_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

		if (client_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

		if (client_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

		if (client_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생";
	}
	else if (client_state.if_server_exit) {
		std::cout << "서버에서 연결 종료\n";
	}


	closesocket(sock);

	WSACleanup();

	return 0;
}