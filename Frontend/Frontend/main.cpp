#include <iostream>
#include <map>
#include <thread>
#include <set>
#include <mutex>
#include <chrono>
#include <queue>
#include <concurrent_unordered_map.h>
#include <concurrent_queue.h>

using namespace std;
using namespace chrono;
#include <WS2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")

#include "../../Server/SeamlessServer/protocol.h"

constexpr int MAX_BUFFER = 1024;
constexpr int MAX_SERVER = 2;


enum EVENT_TYPE { EV_RECV, EV_SEND, EV_MOVE, EV_PLAYER_MOVE_NOTIFY, EV_MOVE_TARGET, EV_ATTACK, EV_HEAL };

struct OVER_EX {
	WSAOVERLAPPED over;
	WSABUF	wsabuf[1];
	char	net_buf[MAX_BUFFER];
	EVENT_TYPE	event_type;
};

struct Base_Info 
{
	OVER_EX	recv_over;
	char	pre_net_buf[MAX_BUFFER];
	int		prev_packet_size;
	SOCKET	socket;
};

struct SOCKETINFO
{
	Base_Info netinfo;

	int		id;
	int		serverId;
	char	name[MAX_STR_LEN];

	bool	is_connected;
	int		move_time;

	set <int> near_id;
	mutex near_lock;
};

struct ServerInfo
{
	Base_Info netinfo;

	int		serverId;
};

//SOCKETINFO* clients[MAX_USER_PER_SERVER * MAX_SERVER];
concurrency::concurrent_unordered_map<int, SOCKETINFO*> clients;
Concurrency::concurrent_queue<int> Enable_Client_ids;
ServerInfo ZoneServerList[2];
HANDLE	g_iocp;

int new_user_id = 0;

void error_display(const char* msg, int err_no)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, err_no,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	cout << msg;
	wcout << L"에러 " << lpMsgBuf << endl;
	while (true);
	LocalFree(lpMsgBuf);
}

void Disconnect(int id);

void send_packet(int id, void* buff)
{
	if (false == clients[id]->is_connected) return;

	char* packet = reinterpret_cast<char*>(buff);
	int packet_size = packet[0];
	OVER_EX* send_over = new OVER_EX;
	memset(send_over, 0x00, sizeof(OVER_EX));
	send_over->event_type = EV_SEND;
	memcpy(send_over->net_buf, packet, packet_size);
	send_over->wsabuf[0].buf = send_over->net_buf;
	send_over->wsabuf[0].len = packet_size;
	int ret = WSASend(clients[id]->netinfo.socket, send_over->wsabuf, 1, 0, 0, &send_over->over, 0);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if ((WSAECONNRESET == err_no) || (WSAECONNABORTED == err_no) || (WSAENOTSOCK == err_no)) {
			Disconnect(id);
			return;
		}
		else
			if (WSA_IO_PENDING != err_no)
				error_display("WSASend Error :", err_no);
	}
}

void send_packet(SOCKET s, void* buff)
{
	char* packet = reinterpret_cast<char*>(buff);
	int packet_size = packet[0];
	OVER_EX* send_over = new OVER_EX;
	memset(send_over, 0x00, sizeof(OVER_EX));
	send_over->event_type = EV_SEND;
	memcpy(send_over->net_buf, packet, packet_size);
	send_over->wsabuf[0].buf = send_over->net_buf;
	send_over->wsabuf[0].len = packet_size;
	int ret = WSASend(s, send_over->wsabuf, 1, 0, 0, &send_over->over, 0);
	if (0 != ret) {
		int err_no = WSAGetLastError();
		if ((WSAECONNRESET == err_no) || (WSAECONNABORTED == err_no) || (WSAENOTSOCK == err_no)) {
			//Disconnect(id);
			return;
		}
		else
			if (WSA_IO_PENDING != err_no)
				error_display("WSASend Error :", err_no);
	}
}

void Disconnect(int id)
{
	cout << "Disconnect" << endl;
	//clients[id]->is_connected = false;
	//closesocket(clients[id]->socket);
	//for (auto& cl : clients) {
	//	if (true == cl->is_connected) {
	//
	//	}
	//}
}

void ProcessPacket(int id, void* buff)
{
	char* packet = reinterpret_cast<char*>(buff);
	switch (packet[1]) {
	case C2S_LOGIN: {
		auto p = reinterpret_cast<cs_packet_login*>(buff);
		p->sender = clients[id]->id;
		send_packet(ZoneServerList[clients[id]->serverId].netinfo.socket, buff);
		break;
	}
	case C2S_MOVE: {
		auto p = reinterpret_cast<cs_packet_move*>(buff);
		p->sender = clients[id]->id;
		send_packet(ZoneServerList[clients[id]->serverId].netinfo.socket, buff);
		break;
	}

	case S2C_LOGIN_OK: {
		auto msg = reinterpret_cast<sc_packet_login_ok*>(buff);
		clients[msg->recvid]->is_connected = true;
		send_packet(msg->recvid, msg);
		break;
	}
	case S2C_MOVE:	{
		auto msg = reinterpret_cast<sc_packet_move*>(buff);
		send_packet(msg->recvid, msg);
		break;
	}
	case S2C_ENTER:	{
		auto msg = reinterpret_cast<sc_packet_enter*>(buff);
		send_packet(msg->recvid, msg);
		break;
	}
	case S2C_LEAVE:	{
		auto msg = reinterpret_cast<sc_packet_leave*>(buff);
		send_packet(msg->recvid, msg);
		break;
	}

	default: cout << "Invalid Packet Type Error\n";
		while (true);
	}
}

void do_worker()
{
	while (true) {
		DWORD num_byte;
		ULONGLONG key64;
		PULONG_PTR p_key = &key64;
		WSAOVERLAPPED* p_over;

		BOOL no_error = GetQueuedCompletionStatus(g_iocp, &num_byte, p_key, &p_over, INFINITE);
		unsigned int key = static_cast<unsigned>(key64);
		OVER_EX* over_ex = reinterpret_cast<OVER_EX*> (p_over);
		if (FALSE == no_error) {
			int err_no = WSAGetLastError();
			if ((ERROR_NETNAME_DELETED == err_no) || (ERROR_SEM_TIMEOUT == err_no)) {
				Disconnect(key);
				if (EV_SEND == over_ex->event_type) delete over_ex;
				continue;
			}
			else
				error_display("GQCS Error :", err_no);
		}
		Base_Info* netinfo = nullptr;
		if (key >= 200000) {
			netinfo = &ZoneServerList[key - 200000].netinfo;
		}
		else if (key < 100000) {
			netinfo = &clients[key]->netinfo;
		}

		if (num_byte == 0) {
			if (key > 100000)
				exit(-1);
			Disconnect(key);
			continue;
		}  // 클라이언트가 closesocket을 했을 경우		


		if (EV_RECV == over_ex->event_type) {
			char* p = over_ex->net_buf;
			int remain = num_byte;
			int packet_size;
			int prev_packet_size = netinfo->prev_packet_size;
			if (0 == prev_packet_size)
				packet_size = 0;
			else packet_size = netinfo->pre_net_buf[0];
			while (remain > 0) {
				if (0 == packet_size) packet_size = p[0];
				int required = packet_size - prev_packet_size;
				if (required <= remain) {
					memcpy(netinfo->pre_net_buf + prev_packet_size, p, required);
					ProcessPacket(key, netinfo->pre_net_buf);
					remain -= required;
					p += required;
					prev_packet_size = 0;
					packet_size = 0;
				}
				else {
					memcpy(netinfo->pre_net_buf + prev_packet_size, p, remain);
					prev_packet_size += remain;
					remain = 0;
				}
			}
			netinfo->prev_packet_size = prev_packet_size;

			DWORD flags = 0;
			memset(&over_ex->over, 0x00, sizeof(WSAOVERLAPPED));
			WSARecv(netinfo->socket, over_ex->wsabuf, 1, 0, &flags, &over_ex->over, 0);
		}
		else if (EV_SEND == over_ex->event_type) {
			delete over_ex;
		}
		else {
			cout << "Unknown Event Type :" << over_ex->event_type << endl;
			while (true);
		}
	}
}

int main()
{
	wcout.imbue(std::locale("korean"));
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	for (int i = 0; i < MAX_USER_PER_SERVER * 2; ++i) {
		Enable_Client_ids.push(i);
	}

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);

	// 서버 연결
	for (int i = 0; i < 2; ++i) {
		SOCKADDR_IN s_address;
		SOCKET s2f_sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		memset(&s_address, 0, sizeof(s_address));
		s_address.sin_family = AF_INET;
		s_address.sin_port = htons(SERVER_PORT + i);
		inet_pton(AF_INET, "127.0.0.1", &s_address.sin_addr);

		int Result = WSAConnect(s2f_sock, (sockaddr*)&s_address, sizeof(s_address), NULL, NULL, NULL, NULL);
		if (0 != Result) {
			error_display("WSAConnect : ", GetLastError());
		}

		ZoneServerList[i].serverId = 200'000 + i;
		ZoneServerList[i].netinfo.socket = s2f_sock;
		ZoneServerList[i].netinfo.prev_packet_size = 0;
		ZoneServerList[i].netinfo.recv_over.wsabuf[0].len = MAX_BUFFER;
		ZoneServerList[i].netinfo.recv_over.wsabuf[0].buf = ZoneServerList[i].netinfo.recv_over.net_buf;
		ZoneServerList[i].netinfo.recv_over.event_type = EV_RECV;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(s2f_sock), g_iocp, ZoneServerList[i].serverId, 0);

		memset(&ZoneServerList[i].netinfo.recv_over.over, 0, sizeof(ZoneServerList[i].netinfo.recv_over.over));
		DWORD flags = 0;
		int ret = WSARecv(s2f_sock, ZoneServerList[i].netinfo.recv_over.wsabuf, 1, NULL,
			&flags, &(ZoneServerList[i].netinfo.recv_over.over), NULL);
		if (0 != ret) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("WSARecv Error :", err_no);
		}
		printf("Zone Server %d Connected\n", i);
	}

	
	SOCKET listenSocket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	SOCKADDR_IN serverAddr;
	memset(&serverAddr, 0, sizeof(SOCKADDR_IN));
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(SERVER_PORT+100);
	serverAddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	if (SOCKET_ERROR == ::bind(listenSocket, (struct sockaddr*)&serverAddr, sizeof(SOCKADDR_IN))) {
		error_display("WSARecv Error :", WSAGetLastError());
	}
	listen(listenSocket, 5);
	SOCKADDR_IN clientAddr;
	int addrLen = sizeof(SOCKADDR_IN);
	memset(&clientAddr, 0, addrLen);
	DWORD flags;

	
	vector <thread> worker_threads;
	for (int i = 0; i < 4; ++i) worker_threads.emplace_back(do_worker);

	cout << "Start Client Listen\n" << flush;
	int i = 0; 
	while (true) {
		SOCKET clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
		if (INVALID_SOCKET == clientSocket) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("Accept Error :", err_no);
		}

		int user_id = new_user_id++;

		//if (false == Enable_Client_ids.try_pop(user_id)) {
		//	cout << "Currently Max User\n";
		//	continue;
		//}

		SOCKETINFO* new_player = new SOCKETINFO;
		new_player->netinfo.socket = clientSocket;
		new_player->netinfo.prev_packet_size = 0;
		new_player->netinfo.recv_over.wsabuf[0].len = MAX_BUFFER;
		new_player->netinfo.recv_over.wsabuf[0].buf = new_player->netinfo.recv_over.net_buf;
		new_player->netinfo.recv_over.event_type = EV_RECV;
		new_player->is_connected = false;
		new_player->serverId = (i++) % 2;
		//new_player->serverId = rand() % 2;
		new_player->id = user_id + (MAX_USER_PER_SERVER * new_player->serverId);

		clients[new_player->id] = new_player;

		//clients.insert(make_pair(user_id, new_player));

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, new_player->id, 0);

		memset(&clients[new_player->id]->netinfo.recv_over.over, 0, sizeof(clients[new_player->id]->netinfo.recv_over.over));
		flags = 0;
		int ret = WSARecv(clientSocket, clients[new_player->id]->netinfo.recv_over.wsabuf, 1, NULL,
			&flags, &(clients[new_player->id]->netinfo.recv_over.over), NULL);
		if (0 != ret) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("WSARecv Error :", err_no);
		}
	}
	for (auto& th : worker_threads) th.join();
	closesocket(listenSocket);
	WSACleanup();
}

