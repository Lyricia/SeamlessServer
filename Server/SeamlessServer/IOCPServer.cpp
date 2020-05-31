#include <iostream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment (lib, "WS2_32.lib")
#pragma comment (lib, "mswsock.lib")

#include <vector>
#include <thread>
#include <mutex>
#include <string>
using namespace std;

#include "protocol.h"
constexpr auto MAX_PACKET_SIZE = 255;
constexpr auto MAX_BUF_SIZE = 1024;
constexpr auto MAX_USER = 10;

constexpr auto SS_ACCEPT_TAG = 200000;
constexpr auto SC_ACCEPT_TAG = 100000;

enum ENUMOP { OP_RECV, OP_SEND, OP_ACCEPT };
enum C_STATUS { ST_FREE, ST_ALLOC, ST_ACTIVE };

struct EXOVER {
	WSAOVERLAPPED	over;
	ENUMOP			op;
	char			io_buf[MAX_BUF_SIZE];
	union {
		WSABUF			wsabuf;
		SOCKET			c_socket;
	};
};

struct Base_Info {
	SOCKET	m_socket;
	int		m_id;

	EXOVER	m_recv_over;
	int   m_prev_size;
	char  m_packe_buf[MAX_PACKET_SIZE];
};

struct CLIENT : Base_Info {
	mutex	m_cl;

	//SOCKET	m_socket;
	//int		m_id;
	//EXOVER  m_recv_over;
	//int   m_prev_size;
	//char  m_packe_buf[MAX_PACKET_SIZE];

	C_STATUS m_status;

	int		m_ServerID = -1;

	short x, y;
	char m_name[MAX_ID_LEN + 1];
};


CLIENT g_clients[MAX_USER];
HANDLE g_iocp;
SOCKET listen_socket;

int g_other_server_id;			// ���� ���� ID
int g_my_server_id;				// �̿� Server�� ID
SOCKET s_other_server;			// �̿� Zone Server�� Socket
bool Is_Other_Server_Connected = false;
Base_Info other_server_info;


void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::cout << msg;
	std::wcout << L"����" << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
	std::cout << std::endl;
	// while (true);
}

void send_client_packet(int user_id, void* p)
{
	CLIENT& u = g_clients[user_id];

	//if (u.m_ServerID != g_my_server_id + SS_ACCEPT_TAG) return;

	char* buf = reinterpret_cast<char*>(p);

	EXOVER* exover = new EXOVER;
	exover->op = OP_SEND;
	ZeroMemory(&exover->over, sizeof(exover->over));
	exover->wsabuf.buf = exover->io_buf;
	exover->wsabuf.len = buf[0];
	memcpy(exover->io_buf, buf, buf[0]);

	WSASend(u.m_socket, &exover->wsabuf, 1, NULL, 0, &exover->over, NULL);
	//if (Is_Other_Server_Connected && is_server_broadcast_packet) {
	//	WSASend(other_server_info.m_socket, &exover->wsabuf, 1, NULL, 0, &exover->over, NULL);
	//}
}

void send_server_packet(SOCKET& s, void* p) {
	char* buf = reinterpret_cast<char*>(p);

	EXOVER* exover = new EXOVER;
	exover->op = OP_SEND;
	ZeroMemory(&exover->over, sizeof(exover->over));
	exover->wsabuf.buf = exover->io_buf;
	exover->wsabuf.len = buf[0];
	memcpy(exover->io_buf, buf, buf[0]);

	WSASend(s, &exover->wsabuf, 1, NULL, 0, &exover->over, NULL);
	printf("send serv %ld, [%d]\n", s, buf[1]);
	//cout << "send server packet to : " << s_other_server << endl;
}

void send_login_ok_packet(int user_id)
{
	sc_packet_login_ok p;
	p.exp = 0;
	p.hp = 0;
	p.id = user_id;
	p.level = 0;
	p.size = sizeof(p);
	p.type = S2C_LOGIN_OK;
	p.x = g_clients[user_id].x;
	p.y = g_clients[user_id].y;

	send_client_packet(user_id, &p);
}

void send_enter_packet(int user_id, int o_id)
{
	sc_packet_enter p;
	p.id = o_id;
	p.size = sizeof(p);
	p.type = S2C_ENTER;
	p.x = g_clients[o_id].x;
	p.y = g_clients[o_id].y;
	strcpy_s(p.name, g_clients[o_id].m_name);
	p.o_type = O_PLAYER;

	send_client_packet(user_id, &p);
}

void send_leave_packet(int user_id, int o_id)
{
	sc_packet_leave p;
	p.id = o_id;
	p.size = sizeof(p);
	p.type = S2C_LEAVE;

	send_client_packet(user_id, &p);
}

void send_move_packet(int user_id, int mover)
{
	sc_packet_move p;
	p.id = mover;
	p.size = sizeof(p);
	p.type = S2C_MOVE;
	p.x = g_clients[mover].x;
	p.y = g_clients[mover].y;

	send_client_packet(user_id, &p);
}

void send_client_creation_to_server(int user_id)
{
	ss_packet_client_connect p;
	p.size = sizeof(ss_packet_client_connect);
	p.type = S2S_CLIENT_CONN;
	p.id = user_id;
	p.ownerserverid = g_my_server_id + SS_ACCEPT_TAG;
	p.x = g_clients[user_id].x;
	p.y = g_clients[user_id].y;
	strcpy_s(p.name, g_clients[user_id].m_name);

	send_server_packet(other_server_info.m_socket, &p);
}

void do_move(int user_id, int direction)
{
	CLIENT& u = g_clients[user_id];
	int x = u.x;
	int y = u.y;
	switch (direction) {
	case D_UP: if (y > 0) y--; break;
	case D_DOWN: if (y < (WORLD_HEIGHT - 1)) y++; break;
	case D_LEFT: if (x > 0) x--; break;
	case D_RIGHT: if (x < (WORLD_WIDTH - 1)) x++; break;
	default:
		cout << "Unknown Direction from Client move packet!\n";
		DebugBreak();
		exit(-1);
	}
	u.x = x;
	u.y = y;
	for (auto& cl : g_clients) {
		cl.m_cl.lock();
		if (ST_ACTIVE == cl.m_status)
			send_move_packet(cl.m_id, user_id);
		cl.m_cl.unlock();
	}

	ss_packet_client_move p;
	p.size = sizeof(ss_packet_client_move);
	p.type = S2S_CLIENT_MOVE;
	p.clientid = user_id;
	p.x = u.x;
	p.y = u.y;
	send_server_packet(other_server_info.m_socket, &p);

	printf("client [%d]moved [%d]dir : (%d, %d)\n", user_id, direction, u.x, u.y);
}

void enter_game(int user_id, char name[])
{
	g_clients[user_id].m_cl.lock();
	strcpy_s(g_clients[user_id].m_name, name);
	g_clients[user_id].m_name[MAX_ID_LEN] = NULL;
	send_login_ok_packet(user_id);

	for (int i = 0; i < MAX_USER; i++) {
		if (user_id == i) continue;
		g_clients[i].m_cl.lock();
		if (ST_ACTIVE == g_clients[i].m_status)
			if (user_id != i) {
				send_enter_packet(user_id, i);
				send_enter_packet(i, user_id);
			}
		g_clients[i].m_cl.unlock();
	}
	g_clients[user_id].m_status = ST_ACTIVE;
	g_clients[user_id].m_cl.unlock();

	printf("client %d Connected\n", user_id);
}

void Create_Client_Conn(int user_id)
{
	//CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_iocp, user_id, 0);
	CLIENT& nc = g_clients[user_id];
	nc.m_ServerID = g_my_server_id + SS_ACCEPT_TAG;
	nc.m_prev_size = 0;
	nc.m_recv_over.op = OP_RECV;
	ZeroMemory(&nc.m_recv_over.over, sizeof(nc.m_recv_over.over));
	nc.m_recv_over.wsabuf.buf = nc.m_recv_over.io_buf;
	nc.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
	//nc.m_socket = c_socket;
	nc.x = rand() % WORLD_WIDTH;
	nc.y = rand() % WORLD_HEIGHT;
	DWORD flags = 0;
	//WSARecv(c_socket, &nc.m_recv_over.wsabuf, 1, NULL, &flags, &nc.m_recv_over.over, NULL);
}

void process_packet(int key, char* buf)
{
	switch (buf[1]) {
	case C2S_LOGIN: {
		int user_id = key;
		if (user_id >= SS_ACCEPT_TAG) break;
		if (g_clients[user_id].m_ServerID != g_my_server_id + SS_ACCEPT_TAG) break;
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(buf);
		enter_game(user_id, packet->name);
		break;
	}
	case C2S_MOVE: {
		int user_id = key;
		if (user_id >= SS_ACCEPT_TAG) break; 
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(buf);
		do_move(user_id, packet->direction);
		break;
	}
	case S2S_CONN: {
		ss_packet_connect* packet = reinterpret_cast<ss_packet_connect*>(buf);
		int other = packet->serverid;
		printf("Recved [%d]server Connection\n", other);
		break;
	}
	case S2S_CLIENT_CONN: {
		ss_packet_client_connect* packet = reinterpret_cast<ss_packet_client_connect*>(buf);
		CLIENT& nc = g_clients[packet->id];
		nc.m_ServerID = packet->ownerserverid;
		nc.m_prev_size = 0;
		nc.m_recv_over.op = OP_RECV;
		ZeroMemory(&nc.m_recv_over.over, sizeof(nc.m_recv_over.over));
		nc.m_recv_over.wsabuf.buf = nc.m_recv_over.io_buf;
		nc.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
		//nc.m_socket = c_socket;
		nc.m_id = packet->id;
		nc.x = packet->x;
		nc.y = packet->y;
		strcpy_s(nc.m_name, packet->name);
		nc.m_status = ST_ACTIVE;
		
		printf("Server %d Client %d ", g_clients[packet->id].m_ServerID, g_clients[packet->id].m_id);
		cout << g_clients[packet->id].m_name << " connected\n";

		g_clients[packet->id].m_cl.lock();
		strcpy_s(g_clients[packet->id].m_name, packet->name);
		g_clients[packet->id].m_name[MAX_ID_LEN] = NULL;

		for (int i = 0; i < MAX_USER; i++) {
			if (packet->id == i) continue;
			if (g_clients[i].m_ServerID != g_my_server_id + SS_ACCEPT_TAG) continue;

			g_clients[i].m_cl.lock();
			if (ST_ACTIVE == g_clients[i].m_status)
				if (packet->id != i) {
					send_enter_packet(packet->id, i);
					send_enter_packet(i, packet->id);
				}
			g_clients[i].m_cl.unlock();
		}
		g_clients[packet->id].m_cl.unlock();

		break;
	}
	case S2S_CLIENT_MOVE: {
		ss_packet_client_move* packet = reinterpret_cast<ss_packet_client_move*>(buf);
		g_clients[packet->clientid].x = packet->x;
		g_clients[packet->clientid].y = packet->y;

		for (auto& cl : g_clients) {
			cl.m_cl.lock();
			if (ST_ACTIVE == cl.m_status)
				send_move_packet(cl.m_id, packet->clientid);
			cl.m_cl.unlock();
		}

		printf("other server[%d] client [%d]moved to : (%d, %d)\n", g_clients[packet->clientid].m_ServerID, g_clients[packet->clientid].m_id, g_clients[packet->clientid].x, g_clients[packet->clientid].y);
		break;
	}
	case S2S_DISCONN: {
		ss_packet_disconnect* packet = reinterpret_cast<ss_packet_disconnect*>(buf);
		int clientid = packet->clientid;
		g_clients[clientid].m_cl.lock();
		g_clients[clientid].m_status = ST_ALLOC;
		send_leave_packet(clientid, clientid);
		closesocket(g_clients[clientid].m_socket);
		for (auto& cl : g_clients) {
			if (clientid == cl.m_id) continue;
			if (cl.m_ServerID != g_my_server_id + SS_ACCEPT_TAG) continue;
			cl.m_cl.lock();
			if (ST_ACTIVE == cl.m_status)
				send_leave_packet(cl.m_id, clientid);
			cl.m_cl.unlock();
		}
		g_clients[clientid].m_status = ST_FREE;
		g_clients[clientid].m_cl.unlock();

		printf("Server %d Client %d ", g_clients[clientid].m_ServerID, g_clients[clientid].m_id);
		cout << g_clients[clientid].m_name << " connected\n";
		break;
	}
	default:
		cout << "Unknown Packet Type Error!\n";
		DebugBreak();
		exit(-1);
	}
	//printf("Recved [%d]user [%d]Packet\n", user_id, buf[1]);
}

void initialize_clients()
{
	for (int i = 0; i < MAX_USER; ++i) {
		g_clients[i].m_id = i;

		g_clients[i].m_status = ST_FREE;
	}
}

void disconnect(int user_id)
{
	g_clients[user_id].m_cl.lock();
	g_clients[user_id].m_status = ST_ALLOC;
	send_leave_packet(user_id, user_id);
	closesocket(g_clients[user_id].m_socket);
	for (auto& cl : g_clients) {
		if (user_id == cl.m_id) continue;
		cl.m_cl.lock();
		if (ST_ACTIVE == cl.m_status)
			send_leave_packet(cl.m_id, user_id);
		cl.m_cl.unlock();
	}
	g_clients[user_id].m_status = ST_FREE;
	g_clients[user_id].m_cl.unlock();

	ss_packet_disconnect p;
	p.size = sizeof(p);
	p.type = S2S_DISCONN;
	p.clientid = user_id;
	send_server_packet(other_server_info.m_socket, &p);

	printf("Client %d disconnected\n", user_id);
}

void recv_packet_construct(int user_id, int io_byte)
{
	CLIENT& cu = g_clients[user_id];
	EXOVER& r_o = cu.m_recv_over;

	int rest_byte = io_byte;
	char* p = r_o.io_buf;
	int packet_size = 0;
	if (0 != cu.m_prev_size) packet_size = cu.m_packe_buf[0];
	while (rest_byte > 0) {
		if (0 == packet_size) packet_size = *p;
		if (packet_size <= rest_byte + cu.m_prev_size) {
			memcpy(cu.m_packe_buf + cu.m_prev_size, p, packet_size - cu.m_prev_size);
			p += packet_size - cu.m_prev_size;
			rest_byte -= packet_size - cu.m_prev_size;
			packet_size = 0;
			process_packet(user_id, cu.m_packe_buf);
			cu.m_prev_size = 0;
		}
		else {
			memcpy(cu.m_packe_buf + cu.m_prev_size, p, rest_byte);
			cu.m_prev_size += rest_byte;
			rest_byte = 0;
			p += rest_byte;
		}
	}
}

void recv_packet_construct(Base_Info& conn, int io_byte)
{
	EXOVER& r_o = conn.m_recv_over;

	int rest_byte = io_byte;
	char* p = r_o.io_buf;
	int packet_size = 0;
	if (0 != conn.m_prev_size) packet_size = conn.m_packe_buf[0];
	while (rest_byte > 0) {
		if (0 == packet_size) packet_size = *p;
		if (packet_size <= rest_byte + conn.m_prev_size) {
			memcpy(conn.m_packe_buf + conn.m_prev_size, p, packet_size - conn.m_prev_size);
			p += packet_size - conn.m_prev_size;
			rest_byte -= packet_size - conn.m_prev_size;
			packet_size = 0;
			process_packet(conn.m_id, conn.m_packe_buf);
			conn.m_prev_size = 0;
		}
		else {
			memcpy(conn.m_packe_buf + conn.m_prev_size, p, rest_byte);
			conn.m_prev_size += rest_byte;
			rest_byte = 0;
			p += rest_byte;
		}
	}
}

void Create_Server_Connection(SOCKET& other_server_socket, int other_server_id, bool is_called_by_accept)
{
	if (false == is_called_by_accept) {
		cout << "Trying to Connect Other Server [" << g_other_server_id << "]\n";
		//SOCKET s_other_server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		SOCKADDR_IN ServerAddr;
		ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
		ServerAddr.sin_family = AF_INET;
		ServerAddr.sin_port = htons(SERVER_PORT + 10 + g_other_server_id);
		inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);
		int Result = WSAConnect(other_server_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
		if (0 != Result) {
			error_display("No other server available\n", GetLastError());
			return;
		}
	}

	cout << "Connect to other server [" << g_other_server_id << "] Successed\n";
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(other_server_socket), g_iocp, SS_ACCEPT_TAG + other_server_id, 0);
	other_server_info.m_prev_size = 0;
	other_server_info.m_recv_over.op = OP_RECV;
	ZeroMemory(&other_server_info.m_recv_over.over, sizeof(other_server_info.m_recv_over.over));
	other_server_info.m_recv_over.wsabuf.buf = other_server_info.m_recv_over.io_buf;
	other_server_info.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
	other_server_info.m_socket = other_server_socket;
	DWORD flags = 0;
	WSARecv(other_server_info.m_socket,
		&other_server_info.m_recv_over.wsabuf, 1, NULL,
		&flags, &other_server_info.m_recv_over.over, NULL);

	cout << "Start Server Recv\n";
	Is_Other_Server_Connected = true;

	ss_packet_connect p;
	p.size = sizeof(ss_packet_connect);
	p.type = S2S_CONN;
	p.serverid = g_my_server_id;
	send_server_packet(other_server_socket, &p);
}

void worker_thread()
{
	while (true) {
		DWORD io_byte;
		ULONG_PTR key;
		WSAOVERLAPPED* over;
		GetQueuedCompletionStatus(g_iocp, &io_byte, &key, &over, INFINITE);

		EXOVER* exover = reinterpret_cast<EXOVER*>(over);
		int user_id = static_cast<int>(key);

		switch (exover->op) {
		case OP_RECV:
			if (0 == io_byte) {
				if (user_id < SC_ACCEPT_TAG)
					disconnect(user_id);
				else {
					// disconnect_server;;
				}
			}
			else {
				if (user_id < SC_ACCEPT_TAG) {
					CLIENT& cl = g_clients[user_id];
					recv_packet_construct(user_id, io_byte);
					ZeroMemory(&cl.m_recv_over.over, sizeof(cl.m_recv_over.over));
					DWORD flags = 0;
					WSARecv(cl.m_socket, &cl.m_recv_over.wsabuf, 1, NULL, &flags, &cl.m_recv_over.over, NULL);
				}
				else if (user_id >= SS_ACCEPT_TAG) {
					auto& servinfo = other_server_info;

					recv_packet_construct(servinfo, io_byte);
					ZeroMemory(&servinfo.m_recv_over.over, sizeof(servinfo.m_recv_over.over));
					DWORD flags = 0;
					WSARecv(servinfo.m_socket, &servinfo.m_recv_over.wsabuf, 1, NULL, &flags, &servinfo.m_recv_over.over, NULL);
				}
			}
			break;
		case OP_SEND:
			if (0 == io_byte) disconnect(user_id);
			delete exover;
			break;
		case OP_ACCEPT: {
			if (user_id == SS_ACCEPT_TAG) {
				if (Is_Other_Server_Connected == true) break;
				Create_Server_Connection(exover->c_socket, g_other_server_id, true);

				//cout << "Trying to Connect Other Server [" << g_other_server_id << "]\n";
				////SOCKET s_other_server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
				//SOCKADDR_IN ServerAddr;
				//ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
				//ServerAddr.sin_family = AF_INET;
				//ServerAddr.sin_port = htons(SERVER_PORT + 10 + g_other_server_id);
				//inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);
				//int Result = WSAConnect(exover->c_socket, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
				//if (0 != Result) {
				//	error_display("No other server available\n", GetLastError());
				//}
				//else {
				//	cout << "Connect to other server [" << g_other_server_id << "] Successed\n";
				//	Is_Other_Server_Connected = true;
				//	CreateIoCompletionPort(reinterpret_cast<HANDLE>(exover->c_socket), g_iocp, user_id + g_other_server_id, 0);
				//	other_server_info.m_prev_size = 0;
				//	other_server_info.m_recv_over.op = OP_RECV;
				//	ZeroMemory(&other_server_info.m_recv_over.over, sizeof(other_server_info.m_recv_over.over));
				//	other_server_info.m_recv_over.wsabuf.buf = other_server_info.m_recv_over.io_buf;
				//	other_server_info.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
				//	other_server_info.m_socket = exover->c_socket;
				//	DWORD flags = 0;
				//	WSARecv(other_server_info.m_socket, 
				//		&other_server_info.m_recv_over.wsabuf, 1, NULL, 
				//		&flags, &other_server_info.m_recv_over.over, NULL);
				//	cout << "Start Server Recv\n";
				//}
				//
				//auto other_socket = exover->c_socket;
				//ss_packet_connect p;
				//p.size = sizeof(ss_packet_connect);
				//p.type = S2S_CONN;
				//p.serverid = g_my_server_id;
				//send_server_packet(&p);
			}

			else if (user_id == SC_ACCEPT_TAG) {
				int user_id = -1;
				for (int i = 0; i < MAX_USER; ++i) {
					lock_guard<mutex> gl{ g_clients[i].m_cl };
					if (ST_FREE == g_clients[i].m_status) {
						g_clients[i].m_status = ST_ALLOC;
						user_id = i;
						break;
					}
				}

				SOCKET c_socket = exover->c_socket;
				if (-1 == user_id)
					closesocket(c_socket);
				else {
					CreateIoCompletionPort(reinterpret_cast<HANDLE>(c_socket), g_iocp, user_id, 0);
					CLIENT& nc = g_clients[user_id];
					nc.m_ServerID = g_my_server_id + SS_ACCEPT_TAG;
					nc.m_prev_size = 0;
					nc.m_recv_over.op = OP_RECV;
					ZeroMemory(&nc.m_recv_over.over, sizeof(nc.m_recv_over.over));
					nc.m_recv_over.wsabuf.buf = nc.m_recv_over.io_buf;
					nc.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
					nc.m_socket = c_socket;
					nc.x = rand() % WORLD_WIDTH;
					nc.y = rand() % WORLD_HEIGHT;
					DWORD flags = 0;
					WSARecv(c_socket, &nc.m_recv_over.wsabuf, 1, NULL, &flags, &nc.m_recv_over.over, NULL);

					if (true == Is_Other_Server_Connected)
						send_client_creation_to_server(user_id);
				}
				c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
				exover->c_socket = c_socket;
				ZeroMemory(&exover->over, sizeof(exover->over));
				AcceptEx(listen_socket, c_socket, exover->io_buf, NULL, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, &exover->over);
			}

		}
					  break;
		}
	}
}


int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	cout << "Enter Server ID [0,1] :";
	cin >> g_my_server_id;
	if ((0 != g_my_server_id) && (1 != g_my_server_id)) {
		cout << "[" << g_my_server_id << "] Invalid Server ID!!\n";
		while (true);
	}
	g_other_server_id = 1 - g_my_server_id;


	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);

	initialize_clients();

	SOCKET in_socket_l = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN s_address;
	memset(&s_address, 0, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_port = htons(SERVER_PORT + 10 + g_my_server_id);
	s_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	::bind(in_socket_l, reinterpret_cast<sockaddr*>(&s_address), sizeof(s_address));
	listen(in_socket_l, SOMAXCONN);

	// Server Listen socket IOCP
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(in_socket_l), g_iocp, SS_ACCEPT_TAG, 0);


	// other Server Accept
	SOCKET g_server2_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	EXOVER server2_accept_over;
	ZeroMemory(&server2_accept_over.over, sizeof(server2_accept_over.over));
	server2_accept_over.op = OP_ACCEPT;
	server2_accept_over.c_socket = g_server2_socket;
	AcceptEx(in_socket_l, g_server2_socket, server2_accept_over.io_buf, NULL, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, &server2_accept_over.over);

	vector <thread> worker_threads;
	for (int i = 0; i < 4; ++i) worker_threads.emplace_back(worker_thread);

	SOCKET s_other_server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	Create_Server_Connection(s_other_server, g_other_server_id, false);

	//cout << "Trying to Connect Other Server [" << g_other_server_id << "]\n";
	//SOCKADDR_IN ServerAddr;
	//ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
	//ServerAddr.sin_family = AF_INET;
	//ServerAddr.sin_port = htons(SERVER_PORT + 10 + g_other_server_id);
	//inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);
	//int Result = WSAConnect(s_other_server, (sockaddr*)&ServerAddr, sizeof(ServerAddr), NULL, NULL, NULL, NULL);
	//if (0 != Result) {
	//	error_display("No other server available\n", GetLastError());
	//}
	//else {
	//	cout << "Connect to other server [" << g_other_server_id << "] Successed\n";
	//	Is_Other_Server_Connected = true;
	//}
	//cout << "Other server is connected.\n";


	// Client listen
	listen_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	// SOCKADDR_IN s_address;
	memset(&s_address, 0, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_port = htons(SERVER_PORT + g_my_server_id);
	s_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	::bind(listen_socket, reinterpret_cast<sockaddr*>(&s_address), sizeof(s_address));
	listen(listen_socket, SOMAXCONN);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(listen_socket), g_iocp, SC_ACCEPT_TAG, 0);


	// Client Accept
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	EXOVER accept_over;
	ZeroMemory(&accept_over.over, sizeof(accept_over.over));
	accept_over.op = OP_ACCEPT;
	accept_over.c_socket = c_socket;
	AcceptEx(listen_socket, c_socket, accept_over.io_buf, NULL, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, &accept_over.over);
	cout << "Client Listening...\n";

	for (auto& th : worker_threads) th.join();
}