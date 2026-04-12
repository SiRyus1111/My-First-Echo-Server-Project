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
	bool if_peer_exit = false;
	bool if_header_error = false;
};

// 헤더 규칙
// 첫 4바이트 = uint32_t 페이로드 크기(길이)

int send_all(SOCKET sock, flags& state, const char* msg, int len) {

	int sent_byte = 0;

	while (sent_byte < len) {
		int send_len = send(sock, msg + sent_byte, len - sent_byte, 0);

		if (send_len == SOCKET_ERROR) {
			err_display("send()");
			state.if_error = true;
			return SOCKET_ERROR;
		}

		sent_byte += send_len;

		if (sent_byte != len) {
			std::cout << "부분적 송신 : " << send_len << '/' << len << "바이트 송신됨.\n";
		}
	}

	std::cout << "송신 완료 : 총" << sent_byte << '/' << len << "바이트 송신됨.\n";

	return sent_byte;
}

int recv_all(SOCKET sock, flags& state, char* buf, int len) {

	int received_byte = 0;

	while (received_byte < len)
	{
		int recv_len = recv(sock, buf + received_byte, len - received_byte, 0);

		if (recv_len == SOCKET_ERROR) {
			err_display("recv()");
			state.if_error = true;
			return SOCKET_ERROR;
		}
		else if (recv_len == 0) {
			state.if_peer_exit = true;
			return 0;
		}

		received_byte += recv_len;

		if (received_byte != len) {
			std::cout << "부분적 수신 : " << recv_len << '/' << len << "바이트 수신됨.\n";
		}
	}


	std::cout << "수신 완료 : 총 " << received_byte << '/' << len << "바이트 수신됨.\n";

	return received_byte;
}

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

		if (host_header > BUFFER_SIZE) {
			std::cout << "헤더의 값이 최대 버퍼 크기인 4096(바이트)을 초과. 다시 메시지를 입력해주세요.\n";
			continue;
		}
		
		char header_buf[HEADER_SIZE];

		memcpy(header_buf, &net_header, sizeof(header_buf));

		// 헤더 send()
		client_state.header_send = true;
		int header_send_res = send_all(sock, client_state, header_buf, HEADER_SIZE);

		if (header_send_res == SOCKET_ERROR) break;

		client_state.header_send = false;

		// 페이로드 send()
		client_state.payload_send = true;
		int payload_send_res = send_all(sock, client_state, buf, host_header);

		if (payload_send_res == SOCKET_ERROR) break;

		client_state.payload_send = false;

		// 헤더 recv()
		client_state.header_recv = true;
		int header_recv_res = recv_all(sock, client_state, header_buf, HEADER_SIZE);

		if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) break;

		client_state.header_recv = false;
		
		// 헤더 해석
		uint32_t received_net_header;
		memcpy(&received_net_header, header_buf, HEADER_SIZE);
		uint32_t received_host_header = ntohl(received_net_header);

		if (received_host_header > 4096) {
			client_state.if_header_error = true;
			break;
		}

		std::cout << "헤더 해석 완료. 페이로드를 수신합니다.\n";

		// 페이로드 recv()
		client_state.payload_recv = true;
		int payload_recv_res = recv_all(sock, client_state, buf, received_host_header);

		if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

		client_state.payload_recv = false;

		buf[received_host_header] = '\0';

		std::cout << "[ECHO FROM SERVER]" << buf << '\n';
	}
	// break시 오류 발생 체크, 클라이언트 종료하기
	if (client_state.if_error) {
		std::cout << "서버와의 통신 과정에서 오류 발생 : ";

		if (client_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

		if (client_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";

		if (client_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

		if (client_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생";
	}
	else if (client_state.if_header_error) {
		std::cout << "서버에서 송신된 헤더의 값이 4096을 초과.\n";
	}
	else if (client_state.if_peer_exit) {
		std::cout << "서버에서 연결 종료\n";
		
	}
	else {
		std::cout << "정상적으로 연결 종료..\n";
	}

	std::cout << "서버와 연결 종료됨.\n";

	closesocket(sock);

	WSACleanup();

	return 0;
}