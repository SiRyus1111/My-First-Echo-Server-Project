#include <iostream> // 콘솔 입출력 용 - cout, cin, ...
#include <winsock2.h> // 윈속2 메인 헤더 - socket(), bind(), listen(), accept(), recv(), send(), ...
#include <ws2tcpip.h> // 윈속2 확장 헤더 - inet_ntop(), inet_pton(), ...
// #include <cstdio> / 이거 왜 썼을까? / 일단 지금은 이 라이브러리가 있었다는 기록만 남겨둠. 주석 처리. 주석 처리.
#include <cstdlib> // atoi() 함수 사용하기 위해서
#include <cstring> // memcpy() 함수 사용하기 위해서
#include "NetCommon.h"
#include <utility>

const int SERVER_PORT = 9000;
const int BUFFER_SIZE = 4096;
const int HEADER_SIZE = 8;
const int HEADER_TYPE_SIZE = 4;
const int HEADER_LENGTH_SIZE = 4;

// 서버가 할 일 (클라이언트와 연동)
// 1. 클라이언트로부터 uint32_t형의 헤더를 받아 헤더의 값만큼 페이로드 받기
// 2. 처리하고, 클라이언트의 uint32_t형의 보낸 페이로드 바이트 수를 나타내는 헤더 전송하기
// 3. 연결을 끊을 때.. 어카면 좋을까..?

// 헤더 규칙
// 첫 4바이트 = int32_t 패킷 타입
// 다음 4바이트 = uint32_t 페이로드 길이
// 만약 패킷 타입의 값이 SERVER_HEADER_ERROR(0)이라면 protocol(Application Layer) error.
// 만약 패킷 타입의 값이 CLIENT_SAFE(-1)이라면 일반적인 메시지.

#pragma comment(lib, "Ws2_32.lib")

class WinsockGuard {
public:
    WinsockGuard() {
        WSADATA wsa;
        int WSAStartupres = WSAStartup(MAKEWORD(2, 2), &wsa);
        if (WSAStartupres != 0) {
            std::cerr << "에러 코드 : " << WSAStartupres << '\n';
            throw std::runtime_error("윈속 초기화 실패");
        }
    }
    ~WinsockGuard() {
        WSACleanup();
    }
};

class ClientSocket {
private:
    SOCKET client_sock;
public:
    ClientSocket(SOCKET s) : client_sock(s) {}

    ClientSocket(const ClientSocket&) = delete;
    ClientSocket& operator=(const ClientSocket&) = delete;

    // 이동 생성자
    ClientSocket(ClientSocket&& other) noexcept : client_sock(other.client_sock) { // noexcept = 이 함수는 예외를 발생시키지 않는다고 컴파일러에게 알려주기. 그래서 이동 최적화.
        other.client_sock = INVALID_SOCKET;
    }

    int ClientSockSend(NetState& state, const char* msg, int len) {

        int send_res = send_all(client_sock, state, msg, len);
        if (send_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }

        return send_res;
    }

    int ClientSockRecv(NetState& state, char* buf, int len) {

        int recv_res = recv_all(client_sock, state, buf, len);
        if (recv_res == SOCKET_ERROR) {
            return SOCKET_ERROR;
        }
        else if (recv_res == 0) {
            return 0;
        }

        return recv_res;
    }

    ~ClientSocket() {
        if (client_sock != INVALID_SOCKET) {
            closesocket(client_sock);
        }
    }
};

class ListenSocket {
private:
    SOCKET listen_sock;

public:
    ListenSocket() {
        listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listen_sock == INVALID_SOCKET) {
            err_display("socket()");
            throw std::runtime_error("socket() 함수 실패");
        }

        int option = 1;
        if (setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&option, sizeof(option)) == SOCKET_ERROR) {
            err_display("setsockopt()");
        }
    }

    ListenSocket(const ListenSocket& s) = delete;
    ListenSocket& operator=(const ListenSocket&) = delete;

    void ListenSockBind(sockaddr_in* addr) {
        if (bind(listen_sock, (sockaddr*)addr, sizeof(*addr)) == SOCKET_ERROR){
            err_display("bind()");
            throw std::runtime_error("bind() 함수 실패");
        }
    }

    void ListenSockListen() {
        if (listen(listen_sock, SOMAXCONN) == SOCKET_ERROR) {
            err_display("listen()");
            throw std::runtime_error("listen() 함수 실패");
        }
    }

    ClientSocket ListenSockAccept(sockaddr_in* client_addr) {
        int len = sizeof(*client_addr);
        SOCKET client_sock = accept(listen_sock, (sockaddr*)client_addr, &len);
        if (client_sock == INVALID_SOCKET) {
            err_display("accept()");
            throw std::runtime_error("accept() 함수 실패");
        }

        return ClientSocket(client_sock);
    }

    ~ListenSocket() {
        if (listen_sock != INVALID_SOCKET) {
            closesocket(listen_sock);
        }
    }
};

const char header_err_msg[] = "[SERVER]헤더의 최댓값 초과됨. 서버에서 연결을 종료합니다.\n";
uint32_t host_err_msg_len = static_cast<uint32_t>(strlen(header_err_msg));

// 이건 필요 없음. 이미 socket_error를 반환했다면 연결이 깨진 상태일 가능성이 있으므로 메시지 송신 자체가 안될 가능성이 있음.ㄴ
/*
const char error_msg[] = "[SERVER]송수신 과정에서의 에러 발생. 서버에서 연결을 종료합니다.\n";
const size_t err_msg_szt = sizeof(error_msg);
*/

int main() {
    // 새로 OOP + RAII 기반 코드 작성해보기!
    try {
        WinsockGuard winsock;

        ListenSocket listen_sock;
        
        sockaddr_in listen_addr{};
        listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        listen_addr.sin_family = AF_INET;
        listen_addr.sin_port = htons(SERVER_PORT);

        listen_sock.ListenSockBind(&listen_addr);
        listen_sock.ListenSockListen();

        while (true) {

            // 여기에 accept() 과정까지 스코프가 필요한 다양한 것들 모아놓기
            sockaddr_in client_addr{};
            char buf[BUFFER_SIZE + 1];
            NetState server_state;

            // 출력용으로 클라이언트 IP 주소를 문자열로 저장
            char addr[INET_ADDRSTRLEN];

            try {
                ClientSocket client_sock = listen_sock.ListenSockAccept(&client_addr);
                
                inet_ntop(AF_INET, &client_addr.sin_addr, addr, sizeof(addr));

                std::cout << "클라이언트 접속됨 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';

                
                while (true) {
                    PacketHeader recv_net_header{};

                    server_state.header_recv = true;
                    int header_recv_res = client_sock.ClientSockRecv(server_state, (char*)&recv_net_header, HEADER_SIZE);

                    if (header_recv_res == 0 || header_recv_res == SOCKET_ERROR) break;
                    server_state.header_recv = false;

                    // 해더 해석
                    PacketHeader host_header;
                    host_header.type = ntohl(recv_net_header.type);
                    host_header.length = ntohl(recv_net_header.length);

                    // 헤더가 버퍼 크기에 맞지 않는 경우 처리(4096 초과)
                    if (host_header.length > 4096) {
                        server_state.if_header_error = true;
                        break;
                    }

                    // 페이로드 recv()
                    server_state.payload_recv = true;
                    int payload_recv_res = client_sock.ClientSockRecv(server_state, buf, host_header.length);

                    if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

                    server_state.payload_recv = false;

                    buf[payload_recv_res] = '\0';

                    std::cout << "송신한 클라이언트 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
                    std::cout << "받은 바이트 수 : " << payload_recv_res << " 받은 메시지 : " << buf << '\n';

                    if (host_header.type == HEADER_ERROR) {
                        server_state.if_peer_error = true;
                        break;
                    }

                    // 처리 과정 (여기서는 단순히 받은 메시지를 그대로 보내는 에코 서버이므로, 처리 과정은 생략)

                    // 헤더를 송신할 수 있는 형태로 처리하는 과정
                    PacketHeader send_host_header;
                    send_host_header.type = SAFE;
                    send_host_header.length = payload_recv_res;

                    PacketHeader send_net_header;
                    send_net_header.type = htonl(send_host_header.type);
                    send_net_header.length = htonl(send_host_header.length);

                    // 헤더 send()

                    // 헤더를 저장할 버퍼는 header_buf가 이미 있음

                    server_state.header_send = true;
                    int header_send_res = client_sock.ClientSockSend(server_state, (char*)&send_net_header, HEADER_SIZE);

                    if (header_send_res == SOCKET_ERROR) break;

                    server_state.header_send = false;

                    // 페이로드 send()
                    int payload_sent = 0;

                    server_state.payload_send = true;
                    int payload_send_res = client_sock.ClientSockSend(server_state, buf, send_host_header.length);

                    if (payload_send_res == SOCKET_ERROR) break;

                    server_state.payload_send = false;
                }

                if (server_state.if_error) {
                    std::cout << "클라이언트와의 통신 과정에서 오류 발생 : ";

                    if (server_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

                    else if (server_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생\n";

                    else if (server_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";

                    else if (server_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";


                }
                // TODO : 여기 새로 정의한 헤더에 맞춰서 수정 필요
                else if (server_state.if_header_error) {

                    std::cout << "헤더의 값이 4096을 초과. 페이로드 수신 불가.\n";

                    PacketHeader protocol_err_header;
                    protocol_err_header.type = htonl(HEADER_ERROR);
                    protocol_err_header.length = htonl(host_err_msg_len);

                    int header_err_send_res = client_sock.ClientSockSend(server_state, (char*)&protocol_err_header, HEADER_SIZE);
                    if (header_err_send_res == SOCKET_ERROR) {
                        std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
                    }
                    else {
                        int err_send_res = client_sock.ClientSockSend(server_state, header_err_msg, host_err_msg_len);
                        if (err_send_res == SOCKET_ERROR) {
                            std::cout << "헤더 오류 메시지 클라이언트에 전송 실패.\n";
                        }
                    }
                }
                else if (server_state.if_peer_error) {
                    std::cout << "클라이언트에 보낸 헤더의 오류 수신.\n";
                }
                else if (server_state.if_peer_exit) {
                    std::cout << "클라이언트에서 연결을 종료하였습니다.\n";
                }
            }
            catch (std::exception& e) {
                std::cerr << e.what() << '\n';
                continue;
            }
            std::cout << "클라이언트와의 연결을 종료합니다. 클라이언트의 IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
        }
        
    }
    catch (std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    /*
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

        struct NetState server_state;

        while (true) {

            // 헤더 recv()
            // 애초에 헤더를 받을 버퍼를 PacketHeader 구조체로 선언
            PacketHeader net_header;

            server_state.header_recv = true;
            int header_recv_res = recv_all(client_sock, server_state, (char*) &net_header, HEADER_SIZE);

            if (header_recv_res == SOCKET_ERROR || header_recv_res == 0) break;
            
            server_state.header_recv = false;

            // 해더 해석
            PacketHeader host_header;
            host_header.type = ntohl(net_header.type);
            host_header.length = ntohl(net_header.length);

            // 헤더가 버퍼 크기에 맞지 않는 경우 처리(4096 초과)
            if (host_header.length > 4096) {
                server_state.if_header_error = true;
                break;
            }

            // 페이로드 recv()
            server_state.payload_recv = true;
            int payload_recv_res = recv_all(client_sock, server_state, buf, host_header.length);
            
            if (payload_recv_res == SOCKET_ERROR || payload_recv_res == 0) break;

            server_state.payload_recv = false;

            buf[payload_recv_res] = '\0';

            std::cout << "송신한 클라이언트 : IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';
            std::cout << "받은 바이트 수 : " << payload_recv_res << " 받은 메시지 : " << buf << '\n';

            if (host_header.type == HEADER_ERROR) {
                server_state.if_peer_error = true;
                break;
            }

            // 처리 과정 (여기서는 단순히 받은 메시지를 그대로 보내는 에코 서버이므로, 처리 과정은 생략)

            // 헤더를 송신할 수 있는 형태로 처리하는 과정
            PacketHeader send_host_header;
            send_host_header.type = SAFE;
            send_host_header.length = payload_recv_res;

            PacketHeader send_net_header;
            send_net_header.type = htonl(send_host_header.type);
            send_net_header.length = htonl(send_host_header.length);

            // 헤더 send()

            // 헤더를 저장할 버퍼는 header_buf가 이미 있음

            server_state.header_send = true;
            int header_send_res = send_all(client_sock, server_state, (char*) &send_net_header, HEADER_SIZE);
            
            if (header_send_res == SOCKET_ERROR) break;

            server_state.header_send = false;

            // 페이로드 send()
            int payload_sent = 0;

            server_state.payload_send = true;
            int payload_send_res = send_all(client_sock, server_state, buf, send_host_header.length);

            if (payload_send_res == SOCKET_ERROR) break;

            server_state.payload_send = false;
            
        }

        if (server_state.if_error) {
            std::cout << "클라이언트와의 통신 과정에서 오류 발생 : ";

            if (server_state.header_recv) std::cout << "헤더 수신 과정에서 오류 발생\n";

            else if (server_state.payload_recv) std::cout << "페이로드 수신 과정에서 오류 발생\n";

            else if (server_state.header_send) std::cout << "헤더 송신 과정에서 오류 발생\n";
            
            else if (server_state.payload_send) std::cout << "페이로드 송신 과정에서 오류 발생\n";


        }
        // TODO : 여기 새로 정의한 헤더에 맞춰서 수정 필요
        else if (server_state.if_header_error) {

            std::cout << "헤더의 값이 4096을 초과. 페이로드 수신 불가.\n";

            PacketHeader protocol_err_header;
            protocol_err_header.type = htonl(HEADER_ERROR);
            protocol_err_header.length = htonl(host_err_msg_len);

            int header_err_send_res = send_all(client_sock, server_state, (char*) &protocol_err_header, HEADER_SIZE);
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
        else if (server_state.if_peer_error) {
            std::cout << "클라이언트에 보낸 헤더의 오류 수신.\n";
        }
        else if (server_state.if_peer_exit) {
            std::cout << "클라이언트에서 연결을 종료하였습니다.\n";
        }

        std::cout << "클라이언트와의 연결을 종료합니다. 클라이언트의 IP 주소 = " << addr << " 포트 번호 = " << ntohs(client_addr.sin_port) << '\n';

        closesocket(client_sock);
    }

    closesocket(server_sock);

    WSACleanup();

    */

    return 0;
}