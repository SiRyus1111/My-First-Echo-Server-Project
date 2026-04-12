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

// 헤더 규칙
// 첫 4바이트 = uint32_t 페이로드 크기(길이)

#pragma comment(lib, "Ws2_32.lib")

// 상태 관리 구조체
struct flags {

	bool header_recv = false;
	bool payload_recv = false;
	bool header_send = false;
	bool payload_send = false;

	bool if_error = false;
	bool if_peer_exit = false;
	bool if_header_error = false;

};

const char header_err_msg[] = "[SERVER]헤더의 최댓값 초과됨. 서버에서 연결을 종료합니다.\n";
const std::size_t header_err_msg_szt = sizeof(header_err_msg);
uint32_t host_err_msg_len = static_cast<uint32_t>(header_err_msg_szt);

// 이건 필요 없음. 이미 socket_error를 반환했다면 연결이 깨진 상태일 가능성이 있으므로 메시지 송신 자체가 안될 가능성이 있음.ㄴ
/*
const char error_msg[] = "[SERVER]송수신 과정에서의 에러 발생. 서버에서 연결을 종료합니다.\n";
const size_t err_msg_szt = sizeof(error_msg);
*/

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
			std::cout << "부분적 송신 : "<<  send_len << '/' << len << "바이트 송신됨.\n";
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


	SOCKET server_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); // LISTEN용 소켓

	if (server_sock == INVALID_SOCKET) {
		err_quit("socket()");
		return 1;
	}

	sockaddr_in server_addr{}; // LISTEN용 소켓의 소켓 주소 구조체

	// 서버 주소 정보 설정
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(SERVER_PORT);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	int option = 1;
	if (setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option)) == SOCKET_ERROR) {
		err_display("setsockopt()");
		return 1;
	}

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
	char buf[BUFFER_SIZE + 1]; // 맨 뒤에 널문자를 집어넣기 위해 BUFFER_SIZE + 1(널문자 여유 공간) 으로 버퍼 크기 설정
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

		struct flags server_state;

		while (true) {

			// 헤더 recv()
			char header_buf[HEADER_SIZE]{};

			server_state.header_recv = true;
			int header_recv_res = recv_all(client_sock, server_state, header_buf, HEADER_SIZE);

			if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) break;
			

			server_state.header_recv = false;

			// 해더 해석
			uint32_t net_header;
			memcpy(&net_header, header_buf, HEADER_SIZE);

			// 보냈을 때 네트워크 바이트 정렬로 보냈으니까 받았을 때 다시 호스트 바이트 정렬로 변환 + 형식도 uint32_t로 유지
			uint32_t host_header = ntohl(net_header);

			// 헤더가 버퍼 크기에 맞지 않는 경우 처리(4096 초과)
			if (host_header > 4096) {
				server_state.if_header_error = true;
				break;
			}

			// 페이로드 recv()

			server_state.payload_recv = true;
			int payload_recv_res = recv_all(client_sock, server_state, buf, host_header);
			
			if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

			server_state.payload_recv = false;

			buf[payload_recv_res] = '\0';

			std::cout << "송신한 클라이언트 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
			std::cout << "받은 바이트 수 : " << payload_recv_res << " 받은 메시지 : " << buf << '\n';

			// 처리 과정 (여기서는 단순히 받은 메시지를 그대로 보내는 에코 서버이므로, 처리 과정은 생략)

			// 헤더를 송신할 수 있는 형태로 처리하는 과정
			uint32_t host_send_header = payload_recv_res;
			uint32_t net_send_header = htonl(host_send_header);
			
			memcpy(header_buf, &net_send_header, HEADER_SIZE);

			// 헤더 send()

			// 헤더를 저장할 버퍼는 header_buf가 이미 있음

			server_state.header_send = true;
			int header_send_res = send_all(client_sock, server_state, header_buf, HEADER_SIZE);
			
			if (header_send_res == SOCKET_ERROR) break;

			server_state.header_send = false;

			// 페이로드 send()
			int payload_sent = 0;

			server_state.payload_send = true;
			int payload_send_res = send_all(client_sock, server_state, buf, host_send_header);

			if (payload_send_res == SOCKET_ERROR) break;

			server_state.payload_send = false;
			
			/*
			int send_len = send(client_sock, buf, host_header, 0);

			if (send_len == SOCKET_ERROR) {
				err_display("send()");
				break;
			}
			std::cout << addr << " : " << htons(client_addr.sin_port) << " 클라이언트로 " << send_len << " 바이트 보냄\n";

			*/
		}

		if (server_state.if_error) {
			std::cout << "클라이언트와의 통신 과정에서 오류 발생 : ";

			if (server_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

			else if (server_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생\n";

			else if (server_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";
			
			else if (server_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";


		}
		else if (server_state.if_header_error) {
			std::cout << "헤더의 값이 4096을 초과. 페이로드 수신 불가.\n";

			uint32_t net_header_err_msg_len = htonl(static_cast<uint32_t>(header_err_msg_szt));
			char err_header_buf[HEADER_SIZE];

			memcpy(err_header_buf, &net_header_err_msg_len, HEADER_SIZE);

			int header_err_send_res = send_all(client_sock, server_state, err_header_buf, HEADER_SIZE);
			if (header_err_send_res == SOCKET_ERROR) {
				std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
			}
			else {
				int err_send_res = send_all(client_sock, server_state, header_err_msg, host_err_msg_len);
				if (err_send_res == SOCKET_ERROR) {
					std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
				}
			}
		}
		else if (server_state.if_peer_exit) {
			std::cout << "클라이언트에서 연결을 종료하였습니다.\n";
		}

		std::cout << "클라이언트와의 연결을 종료합니다. 클라이언트의 IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';

		closesocket(client_sock);
	}

	closesocket(server_sock);

	WSACleanup();

	return 0;
}