#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Common.h"
#include <cstring>
#include <string>

const char* SERVER_ADDR = "127.0.0.1";
const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

const char header_err_msg[] = "[CLIENT] 헤더의 최댓값 초과됨. 클라이언트에서 연결을 종료합니다.";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

const int32_t SERVER_HEADER_ERROR = 0;
const int32_t HEADER_SAFE = -1;

#pragma pack(push, 1)
struct PacketHeader {
	int32_t type;
	uint32_t length;
};
#pragma pack(pop)

struct flags {
	// 진행
	bool header_recv = false;
	bool payload_recv = false;
	bool header_send = false;
	bool payload_send = false;

	// 예외
	bool if_error = false;
	bool if_peer_exit = false;
	bool if_header_error = false;
	bool if_peer_error = false;
};

// 헤더 규칙
// 첫 4바이트 = int32_t 패킷 타입
// 다음 4바이트 = uint32_t 페이로드 길이
// 만약 패킷 타입의 값이 SERVER_HEADER_ERROR(0)이라면 protocol(Application Layer) error.
// 만약 패킷 타입의 값이 CLIENT_SAFE(-1)이라면 일반적인 메시지.

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

		PacketHeader send_host_header;
		// 입력 받은 바이트 수 세기
		send_host_header.length = user_input.size();
		if (send_host_header.length > BUFFER_SIZE) {
			std::cout << "헤더의 값이 최대 버퍼 크기인 4096(바이트)을 초과. 다시 메시지를 입력해주세요.\n";
			continue;
		}

		// 여기까지 왔다면 헤더에는 문제 없음

		// 헤더 직렬화(?)
		memcpy(buf, user_input.c_str(), send_host_header.length);
		send_host_header.type = HEADER_SAFE;

		PacketHeader send_net_header;
		send_net_header.type = htonl(send_host_header.type);
		send_net_header.length = htonl(send_host_header.length);

		// 헤더 send()
		client_state.header_send = true;
		int header_send_res = send_all(sock, client_state, (char*) &send_net_header, HEADER_SIZE);

		if (header_send_res == SOCKET_ERROR) break;

		client_state.header_send = false;

		// 페이로드 send()
		client_state.payload_send = true;
		int payload_send_res = send_all(sock, client_state, buf, send_host_header.length);

		if (payload_send_res == SOCKET_ERROR) break;

		client_state.payload_send = false;

		PacketHeader recv_net_header;

		// 헤더 recv()
		client_state.header_recv = true;
		int header_recv_res = recv_all(sock, client_state, (char*) &recv_net_header, HEADER_SIZE);

		if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) break;

		client_state.header_recv = false;
		
		// 헤더 해석
		PacketHeader recv_host_header;
		recv_host_header.type = ntohl(recv_net_header.type);
		recv_host_header.length = ntohl(recv_net_header.length);

		if (recv_host_header.length > 4096) {
			client_state.if_header_error = true;
			break;
		}
		
		std::cout << "헤더 해석 완료. 페이로드를 수신합니다.\n";

		// 페이로드 recv()
		client_state.payload_recv = true;
		int payload_recv_res = recv_all(sock, client_state, buf, recv_host_header.length);

		if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

		client_state.payload_recv = false;

		buf[recv_host_header.length] = '\0';

		std::cout << "[ECHO FROM SERVER]" << buf << '\n';

		if (recv_host_header.type == SERVER_HEADER_ERROR) {
			client_state.if_peer_error = true;
			break;
		}
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

		PacketHeader protocol_err_header;
		protocol_err_header.type = htonl(SERVER_HEADER_ERROR);
		protocol_err_header.length = htonl(host_err_msg_len);

		int header_err_send_res = send_all(sock, client_state, (char*) &protocol_err_header, HEADER_SIZE);
		if (header_err_send_res == SOCKET_ERROR) {
			std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
		}
		else {
			int err_send_res = send_all(sock, client_state, header_err_msg, host_err_msg_len);
			if (err_send_res == SOCKET_ERROR) {
				std::cout << "헤더 오류 메시지 서버에 전송 실패.\n";
			}
		}
  	}
	else if (client_state.if_peer_error) {
		
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