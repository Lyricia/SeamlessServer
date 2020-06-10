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
constexpr int MAX_USER_PER_SERVER = 10000;
constexpr int MAX_SERVER = 2;


enum EVENT_TYPE { EV_RECV, EV_SEND, EV_MOVE, EV_PLAYER_MOVE_NOTIFY, EV_MOVE_TARGET, EV_ATTACK, EV_HEAL };

struct OVER_EX {
	WSAOVERLAPPED over;
	WSABUF	wsabuf[1];
	char	net_buf[MAX_BUFFER];
	EVENT_TYPE	event_type;
};

struct SOCKETINFO
{
	OVER_EX	recv_over;
	char	pre_net_buf[MAX_BUFFER];
	int		prev_packet_size;
	SOCKET	socket;

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
	OVER_EX	recv_over;
	char	pre_net_buf[MAX_BUFFER];
	int		prev_packet_size;
	SOCKET	socket;

	int		serverId;
};

Concurrency::concurrent_unordered_map <int, SOCKETINFO*> clients;
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
	int ret = WSASend(clients[id]->socket, send_over->wsabuf, 1, 0, 0, &send_over->over, 0);
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


void send_put_object_packet(int client, int new_id)
{
	sc_packet_enter packet;
	packet.id = new_id;
	packet.size = sizeof(packet);
	packet.type = S2C_ENTER;
	//packet.x = clients[new_id]->x;
	//packet.y = clients[new_id]->y;
	packet.o_type = 1;
	send_packet(client, &packet);

	if (client == new_id) return;
	lock_guard<mutex>lg{ clients[client]->near_lock };
	clients[client]->near_id.insert(new_id);
}



void Disconnect(int id)
{
	cout << "Disconnect" << endl;
	clients[id]->is_connected = false;
	closesocket(clients[id]->socket);
	for (auto& cl : clients) {
		if (true == cl.second->is_connected) {

		}
	}
}

//void ProcessMove(int id, unsigned char dir)
//{
//	short x = clients[id]->x;
//	short y = clients[id]->y;
//	clients[id]->near_lock.lock();
//	auto old_vl = clients[id]->near_id;
//	clients[id]->near_lock.unlock();
//	switch (dir) {
//	case D_UP: if (y > 0) y--;
//		break;
//	case D_DOWN: if (y < WORLD_HEIGHT - 1) y++;
//		break;
//	case D_LEFT: if (x > 0) x--;
//		break;
//	case D_RIGHT: if (x < WORLD_WIDTH - 1) x++;
//		break;
//	case 99:
//		x = rand() % WORLD_WIDTH;
//		y = rand() % WORLD_HEIGHT;
//		break;
//	default: cout << "Invalid Direction Error\n";
//		while (true);
//	}
//
//	clients[id]->x = x;
//	clients[id]->y = y;
//
//	set <int> new_vl;
//	for (auto& cl : clients) {
//		int other = cl.second->id;
//		if (id == other) continue;
//		if (false == clients[other]->is_connected) continue;
//		if (true == is_near(id, other)) new_vl.insert(other);
//	}
//
//	send_pos_packet(id, id);
//	for (auto cl : old_vl) {
//		if (0 != new_vl.count(cl)) {
//			send_pos_packet(cl, id);
//		}
//		else
//		{
//			send_remove_object_packet(id, cl);
//			send_remove_object_packet(cl, id);
//		}
//	}
//	for (auto cl : new_vl) {
//		if (0 == old_vl.count(cl)) {
//			send_put_object_packet(id, cl);
//			send_put_object_packet(cl, id);
//		}
//	}
//}
//
//void ProcessLogin(int user_id, char* id_str)
//{
//	//for (auto cl : clients) {
//	//	if (0 == strcmp(cl.second->name, id_str)) {
//	//		send_login_fail(user_id);
//	//		Disconnect(user_id);
//	//		return;
//	//	}
//	//}
//	strcpy_s(clients[user_id]->name, id_str);
//	clients[user_id]->is_connected = true;
//	send_login_ok_packet(user_id);
//
//
//	for (auto& cl : clients) {
//		int other_player = cl.first;
//		if (false == clients[other_player]->is_connected) continue;
//		if (true == is_near(other_player, user_id)) {
//			send_put_object_packet(other_player, user_id);
//			if (other_player != user_id) {
//				send_put_object_packet(user_id, other_player);
//			}
//		}
//	}
//}

void ProcessPacket(int id, void* buff)
{
	char* packet = reinterpret_cast<char*>(buff);
	switch (packet[1]) {
	case C2S_LOGIN:
	case C2S_MOVE: {
		send_packet(ZoneServerList[clients[id]->serverId].socket, buff);
		break;
	}

	case S2C_LOGIN_OK: {
		auto msg = reinterpret_cast<fs_packet_client_enter*>(buff);
		send_packet(msg->recvid, &msg->p);
		break;
	}
	case S2C_MOVE:	{
		auto msg = reinterpret_cast<fs_packet_client_move*>(buff);
		send_packet(msg->recvid, &msg->p);
		break;
	}
	case S2C_ENTER:	{
		auto msg = reinterpret_cast<fs_packet_client_enter*>(buff);
		send_packet(msg->recvid, &msg->p);
		break;
	}
	case S2C_LEAVE:	{
		auto msg = reinterpret_cast<fs_packet_client_leave*>(buff);
		send_packet(msg->recvid, &msg->p);
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
		SOCKET client_s = clients[key]->socket;
		if (num_byte == 0) {
			Disconnect(key);
			continue;
		}  // 클라이언트가 closesocket을 했을 경우		


		if (EV_RECV == over_ex->event_type) {
			char* p = over_ex->net_buf;
			int remain = num_byte;
			int packet_size;
			int prev_packet_size = clients[key]->prev_packet_size;
			if (0 == prev_packet_size)
				packet_size = 0;
			else packet_size = clients[key]->pre_net_buf[0];
			while (remain > 0) {
				if (0 == packet_size) packet_size = p[0];
				int required = packet_size - prev_packet_size;
				if (required <= remain) {
					memcpy(clients[key]->pre_net_buf + prev_packet_size, p, required);
					ProcessPacket(key, clients[key]->pre_net_buf);
					remain -= required;
					p += required;
					prev_packet_size = 0;
					packet_size = 0;
				}
				else {
					memcpy(clients[key]->pre_net_buf + prev_packet_size, p, remain);
					prev_packet_size += remain;
					remain = 0;
				}
			}
			clients[key]->prev_packet_size = prev_packet_size;

			DWORD flags = 0;
			memset(&over_ex->over, 0x00, sizeof(WSAOVERLAPPED));
			WSARecv(client_s, over_ex->wsabuf, 1, 0, &flags, &over_ex->over, 0);
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

	// 서버 연결
	for (int i = 0; i < MAX_SERVER; ++i) {
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
		ZoneServerList[i].socket = s2f_sock;
		ZoneServerList[i].prev_packet_size = 0;
		ZoneServerList[i].recv_over.wsabuf[0].len = MAX_BUFFER;
		ZoneServerList[i].recv_over.wsabuf[0].buf = ZoneServerList[i].recv_over.net_buf;
		ZoneServerList[i].recv_over.event_type = EV_RECV;

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(s2f_sock), g_iocp, ZoneServerList[i].serverId, 0);

		memset(&ZoneServerList[i].recv_over.over, 0, sizeof(ZoneServerList[i].recv_over.over));
		DWORD flags = 0;
		int ret = WSARecv(s2f_sock, ZoneServerList[i].recv_over.wsabuf, 1, NULL,
			&flags, &(ZoneServerList[i].recv_over.over), NULL);
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

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	vector <thread> worker_threads;
	for (int i = 0; i < 4; ++i) worker_threads.emplace_back(do_worker);

	cout << "Start Client Listen\n" << flush;

	while (true) {
		SOCKET clientSocket = accept(listenSocket, (struct sockaddr*)&clientAddr, &addrLen);
		if (INVALID_SOCKET == clientSocket) {
			int err_no = WSAGetLastError();
			if (WSA_IO_PENDING != err_no)
				error_display("Accept Error :", err_no);
		}

		int user_id = new_user_id++;

		if (false == Enable_Client_ids.try_pop(user_id)) {
			cout << "Currently Max User\n";
			continue;
		}

		SOCKETINFO* new_player = new SOCKETINFO;
		new_player->id = user_id;
		new_player->socket = clientSocket;
		new_player->prev_packet_size = 0;
		new_player->recv_over.wsabuf[0].len = MAX_BUFFER;
		new_player->recv_over.wsabuf[0].buf = new_player->recv_over.net_buf;
		new_player->recv_over.event_type = EV_RECV;
		new_player->is_connected = false;
		new_player->serverId = rand() % 2;

		clients.insert(make_pair(user_id, new_player));

		CreateIoCompletionPort(reinterpret_cast<HANDLE>(clientSocket), g_iocp, user_id, 0);

		memset(&clients[user_id]->recv_over.over, 0, sizeof(clients[user_id]->recv_over.over));
		flags = 0;
		int ret = WSARecv(clientSocket, clients[user_id]->recv_over.wsabuf, 1, NULL,
			&flags, &(clients[user_id]->recv_over.over), NULL);
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

