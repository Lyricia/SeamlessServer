#include <iostream>
#include <fstream>
#include <WS2tcpip.h>
#include <MSWSock.h>
#pragma comment (lib, "WS2_32.lib")
#pragma comment (lib, "mswsock.lib")

#include <vector>
#include <thread>
#include <mutex>
#include <string>
#include <unordered_set>
#include <concurrent_unordered_map.h>
using namespace std;

#include "protocol.h"
constexpr auto MAX_PACKET_SIZE = 255;
constexpr auto MAX_BUF_SIZE = 1024;

constexpr auto FS_ACCEPT_TAG = 300000;
constexpr auto SS_ACCEPT_TAG = 200000;
constexpr auto SC_ACCEPT_TAG = 100000;

enum ENUMOP { OP_RECV, OP_SEND, OP_ACCEPT };
enum C_STATUS { ST_FREE, ST_ALLOC, ST_ACTIVE, ST_HANDOVER };

struct ServerNetInfo {
	int id;
	string ip;
	int port1;
	int port2;
};



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

	int		m_list_slot_idx;

	//SOCKET	m_socket;
	//int		m_id;
	//EXOVER  m_recv_over;
	//int   m_prev_size;
	//char  m_packe_buf[MAX_PACKET_SIZE];

	C_STATUS m_status;

	int		m_ServerID = -1;
	bool	m_IsInBufferSection = false;

	unordered_set<int> view_list;
	mutex view_list_lock;

	short x, y;
	char m_name[MAX_ID_LEN + 1]{};
};

ServerNetInfo ServerInfo[3];

CLIENT g_clients[MAX_USER_PER_SERVER * 2];
CLIENT g_proxy_clients[MAX_USER_PER_SERVER * 2];
//Concurrency::concurrent_unordered_map<int, CLIENT*> g_proxy_clients;

HANDLE g_iocp;
SOCKET listen_socket;

int g_other_server_id;			// 나의 서버 ID
int g_my_server_id;				// 이웃 Server의 ID
SOCKET s_other_server;			// 이웃 Zone Server의 Socket
bool Is_Other_Server_Connected = false;

Base_Info other_server_info;
Base_Info frontend_server_info;

int server_buffer_pos = -1;
int Server_Start_X = -1;
int Server_Start_Y = -1;

void SetServerNetInfo() {
	string in_line;
	ifstream in("Config.txt");

	int i = 0;
	while (!in.eof()) {
		in >> ServerInfo[i].id;
		in >> ServerInfo[i].ip;
		in >> ServerInfo[i].port1;
		in >> ServerInfo[i].port2;
		i++;
	}

	in.close();
}

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
	std::wcout << L"에러" << lpMsgBuf << std::endl;
	LocalFree(lpMsgBuf);
	std::cout << std::endl;
	// while (true);
}

bool is_near(CLIENT& a, CLIENT& b)
{
	if (VIEW_RANGE < abs(a.x - b.x)) return false;
	if (VIEW_RANGE < abs(a.y - b.y)) return false;
	return true;
}

bool is_near(int a, int b, bool IsOpponentProxy)
{
	if (VIEW_RANGE < abs(g_clients[a].x - g_clients[b].x)) return false;
	if (VIEW_RANGE < abs(g_clients[a].y - g_clients[b].y)) return false;
	if (VIEW_RANGE < abs(g_clients[a].y - g_clients[b].y)) return false;
	return true;
}

int getProxyClientID(int id, int serverid) {
	return id + MAX_USER_PER_SERVER;
}

bool IsInServerBufferSection(int y) {
	if (g_my_server_id == 0 &&
		(21) >= y && y > server_buffer_pos) {
		return true;
	}
	else if (g_my_server_id == 1 && 
		(18) <= y && y < server_buffer_pos) {
		return true;
	}
	return false;
}

bool IsOutServerBufferSection(int y) {
	if (g_my_server_id == 0 && y < server_buffer_pos) {
		return true;
	}
	else if (g_my_server_id == 1 && y > server_buffer_pos) {
		return true;
	}
	return false;
}

bool IsHandOver(int y) {
	if (g_my_server_id == 0 && y > 21) {
		return true;
	}
	else if (g_my_server_id == 1 && y < 18) {
		return true;
	}
	return false;
}

CLIENT& GetClientByID(int id) {
	return g_clients[id];
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

void send_server_packet(SOCKET& s, void* p)
{
	char* buf = reinterpret_cast<char*>(p);

	EXOVER* exover = new EXOVER;
	exover->op = OP_SEND;
	ZeroMemory(&exover->over, sizeof(exover->over));
	exover->wsabuf.buf = exover->io_buf;
	exover->wsabuf.len = buf[0];
	memcpy(exover->io_buf, buf, buf[0]);

	int ret = WSASend(s, &exover->wsabuf, 1, NULL, 0, &exover->over, NULL);
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
	printf("send serv %ld, [%d]\n", s, buf[1]);
	//cout << "send server packet to : " << s_other_server << endl;
}

void send_login_ok_packet(CLIENT& cl)
{
	sc_packet_login_ok p;
	p.exp = 0;
	p.hp = 0;
	p.id = cl.m_id;
	p.level = 0;
	p.size = sizeof(p);
	p.type = S2C_LOGIN_OK;
	p.x = cl.x;
	p.y = cl.y;
	p.serverid = g_my_server_id;
	p.recvid = cl.m_id;

	//sf_packet_client_login_ok msg;
	//msg.p = p;
	//msg.recvid = g_clients[user_id].m_id;

	send_server_packet(frontend_server_info.m_socket, &p);

	printf("recver %d, target %d\n", p.recvid, p.id);
	//send_client_packet(user_id, &p);
}

void send_enter_packet(int user_slot_idx, int object_id, bool isProxyEnter)
{
	auto& user = g_clients[user_slot_idx];

	sc_packet_enter p;
	p.id = object_id;
	p.size = sizeof(p);
	p.type = S2C_ENTER;

	if (false == isProxyEnter) {
		p.o_type = O_PLAYER;
		p.x = g_clients[object_id].x;
		p.y = g_clients[object_id].y;
		strcpy_s(p.name, g_clients[object_id].m_name);
	}
	else if (true == isProxyEnter) {
		auto& cl = g_proxy_clients[object_id];
		p.o_type = O_PROXY;
		p.x = cl.x;
		p.y = cl.y;
		strcpy_s(p.name, cl.m_name);
	}
	p.recvid = user.m_id;

	//send_client_packet(user_id, &p);
	send_server_packet(frontend_server_info.m_socket, &p);

	if (user.m_id == object_id) return;
	lock_guard<mutex>lg{ user.view_list_lock };
	user.view_list.insert(object_id);
}

void send_leave_packet(int user_id, int leaverid)
{
	auto& user = GetClientByID(user_id);
	sc_packet_leave p;
	p.id = leaverid;
	p.size = sizeof(p);
	p.type = S2C_LEAVE;
	p.recvid = user.m_id;

	cout << "leave" << endl;
	//while (true);

	send_server_packet(frontend_server_info.m_socket, &p);
	//send_client_packet(user_id, &p);

	lock_guard<mutex>lg{ user.view_list_lock };
	user.view_list.erase(leaverid);
}

void send_move_packet(CLIENT& user, int moverid, bool isMoverProxy)
{
	sc_packet_move p;
	p.id = moverid;
	p.size = sizeof(p);
	p.type = S2C_MOVE;
	CLIENT* cl_mover = nullptr;
	if (false == isMoverProxy) cl_mover = &GetClientByID(moverid);
	else cl_mover = &g_proxy_clients[moverid];

	p.x = cl_mover->x;
	p.y = cl_mover->y;
	p.recvid = user.m_id;


	send_server_packet(frontend_server_info.m_socket, &p);

	user.view_list_lock.lock();
	if ((user.m_id == moverid) || (0 != user.view_list.count(moverid))) {
		user.view_list_lock.unlock();
		//send_client_packet(user.m_id, &p);
	}
	else {
		user.view_list_lock.unlock();
		send_enter_packet(user.m_list_slot_idx, moverid, isMoverProxy);
	}
}

void send_client_creation_to_server(CLIENT& user)
{
	ss_packet_client_connect p;
	p.size = sizeof(ss_packet_client_connect);
	p.type = S2S_CLIENT_CONN;
	p.id = user.m_id;
	p.ownerserverid = g_my_server_id;
	p.x = user.x;
	p.y = user.y;
	strcpy_s(p.name, user.m_name);

	send_server_packet(other_server_info.m_socket, &p);
}

void do_move(CLIENT& u, int direction)
{
	//CLIENT& u = g_clients[user_id];
	int x = u.x;
	int y = u.y;

	u.view_list_lock.lock();
	auto old_vl = u.view_list;
	u.view_list_lock.unlock();

	switch (direction) {
	case D_UP:
		if (y > 0) y--; break;
	case D_DOWN:
		if (y < (WORLD_HEIGHT - 1)) y++; break;
	case D_LEFT:
		if (x > 0) x--; break;
	case D_RIGHT:
		if (x < (WORLD_WIDTH - 1)) x++; break;
	default:
		cout << "Unknown Direction from Client move packet!\n";
		DebugBreak();
		exit(-1);
	}
	u.x = x;
	u.y = y;

	unordered_set<int> new_vl;
	// local list
	for (auto& othercl : g_clients) {
		int otherid = othercl.m_id;
		if (u.m_id == otherid) continue;
		if (ST_FREE == othercl.m_status) continue;
		if (true == is_near(u, othercl)) new_vl.insert(otherid);
	}
	// proxy list
	for (auto& otherProxycl : g_proxy_clients) {
		//auto othercl = otherProxycl.second;
		int otherid = otherProxycl.m_id;
		if (u.m_id == otherid) continue;
		if (ST_FREE == otherProxycl.m_status) continue;
		if (true == is_near(u, otherProxycl)) new_vl.insert(otherid);
	}

	send_move_packet(u, u.m_id, false);

	// Old View list를 순회하면서 맞는 packet을 보냄
	for (auto cl : old_vl) {
		if (0 != new_vl.count(cl)) {					// new view list에 있으면 move packet을 보냄
			if (ST_FREE == g_proxy_clients[cl].m_status)			// cl이 local client면 그냥 보냄
				send_move_packet(GetClientByID(cl), u.m_id, false);
			//else if (ST_FREE != g_clients[cl].m_status && ST_FREE == g_proxy_clients[cl]->m_status)
			//	send_move_packet(GetClientByID(cl), u.m_id, false);

			//else {
			//	// proxy client라면 서버로 move packet을 보냄
			//	// 상대 서버의 원본 client에도 동일하게 viewlist가 적용되어있기 때문에
			//	// 내가 movepacket을 보내면...
			//
			//	// view list에 있는 proxy만큼 보낼 필요가 없이 그냥 상대 서버에 한패킷만 날리고
			//	// 해당 패킷을 서버가 알아서 처리하는게 더 나을 것 같다...
			//
			//	//send_server_packet()
			//	//send_move_packet(cl, user_id, true);
			//}
		}
		else {			// new view list에 없으면 leave packet을 보냄
			send_leave_packet(u.m_id, cl);
			if (ST_FREE == g_proxy_clients[cl].m_status) {
				send_leave_packet(cl, u.m_id);
			}
			else {
				g_proxy_clients[cl].view_list_lock.lock();
				g_proxy_clients[cl].view_list.erase(u.m_id);
				g_proxy_clients[cl].view_list_lock.unlock();
			}
		}
	}
	for (auto cl : new_vl) {
		if (0 == old_vl.count(cl)) {
			if (ST_FREE != g_proxy_clients[cl].m_status) {
				send_enter_packet(u.m_list_slot_idx, cl, true);
				g_proxy_clients[cl].view_list_lock.lock();
				g_proxy_clients[cl].view_list.insert(u.m_id);
				g_proxy_clients[cl].view_list_lock.unlock();
			}
			else {
				send_enter_packet(u.m_list_slot_idx, cl, false);
				send_enter_packet(cl, u.m_id, false);
			}
		}
	}

	if (true == Is_Other_Server_Connected)
	{
		// 경계영역에 위치해 있던 경우 버퍼영역을 넘어 완전탈출 전까지 move패킷은 보내주어야함
		// 만약 경계영역에 새로 진입한 경우 Enter packet도 전송
		if (true == u.m_IsInBufferSection) {
			// send move packet
			ss_packet_client_move p;
			p.size = sizeof(ss_packet_client_move);
			p.type = S2S_CLIENT_MOVE;
			p.clientid = u.m_id;
			p.x = u.x;
			p.y = u.y;
			send_server_packet(other_server_info.m_socket, &p);
		}
		else if (IsInServerBufferSection(u.y)) {
			// 새로 경계영역에 완전히 진입한 경우 Enter Packet 전송
			send_client_creation_to_server(u);
			u.m_IsInBufferSection = true;
		}

		if (IsOutServerBufferSection(u.y)) {
			// 버퍼-경계 영역에서 탈출한 경우 leave packet을 타 서버로 전송
			// send Leave packet
			if (true == u.m_IsInBufferSection) {
				ss_packet_disconnect p;
				p.size = sizeof(p);
				p.type = S2S_CLIENT_DISCONN;
				p.clientid = u.m_id;
				send_server_packet(other_server_info.m_socket, &p);

				u.m_IsInBufferSection = false;
			}
		}

		if (IsHandOver(u.y)) {
			// 상대에게 Hand Over
			// 1. 상대에게 Enter
			// 2. 내것을 Leave
			u.m_status = ST_HANDOVER;

			sc_packet_leave p3;
			p3.size = sizeof(p3);
			p3.type = S2C_LEAVE;
			p3.id = u.m_id;

			u.view_list_lock.lock();
			auto vl = u.view_list;
			u.view_list_lock.unlock();
			
			for (auto cl : vl) {
				p3.recvid = cl;
				send_server_packet(frontend_server_info.m_socket, &p3);
			}

			ss_packet_client_handover p;
			p.size = sizeof(p);
			p.type = S2S_CLIENT_HANDOVER;
			p.clientid = u.m_id;

			send_server_packet(other_server_info.m_socket, &p);
			

			sf_packet_handver p2;
			p2.size = sizeof(p2);
			p2.type = S2F_HANDOVER;
			p2.targetid = u.m_id;
			p2.handoverserverid = g_other_server_id;
			
			send_server_packet(frontend_server_info.m_socket, &p2);

			
		}
	}

	printf("client [%d]moved [%d]dir : (%d, %d)\n", u.m_id, direction, u.x, u.y);
}

void enter_game(CLIENT& newclient, char name[])
{
	newclient.m_cl.lock();
	strcpy_s(newclient.m_name, name);
	newclient.m_name[MAX_ID_LEN] = NULL;
	
	newclient.m_ServerID = g_my_server_id + SS_ACCEPT_TAG;

	send_login_ok_packet(newclient);

	// local client routine
	for (int list_idx = 0; list_idx < MAX_USER_PER_SERVER * 2; list_idx++) {
		if (newclient.m_list_slot_idx == list_idx) continue;
		g_clients[list_idx].m_cl.lock();
		if (ST_ACTIVE == g_clients[list_idx].m_status) {
			if (true == is_near(newclient, g_clients[list_idx])) {
				send_enter_packet(list_idx, newclient.m_id, false);
				if (newclient.m_list_slot_idx != list_idx) {
					send_enter_packet(newclient.m_list_slot_idx, list_idx, false);
				}
			}
		}
		g_clients[list_idx].m_cl.unlock();
	}

	// proxy client routine
	for (auto& proxycl : g_proxy_clients) {
		proxycl.m_cl.lock();
		if (ST_ACTIVE == proxycl.m_status) {
			if (true == is_near(newclient, proxycl)) {
				send_enter_packet(newclient.m_list_slot_idx, proxycl.m_id, true);
				proxycl.view_list_lock.lock();
				proxycl.view_list.insert(newclient.m_id);
				proxycl.view_list_lock.unlock();
			}
		}
		proxycl.m_cl.unlock();
	}

	newclient.m_status = ST_ACTIVE;
	newclient.m_cl.unlock();

	printf("client %d Connected\n", newclient.m_id);

	if (true == Is_Other_Server_Connected)
		if (IsInServerBufferSection(newclient.y)) {
			send_client_creation_to_server(newclient);
			newclient.m_IsInBufferSection = true;
		}
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

void Process_Proxy_Client_Move(int proxyid) {
	// 타 서버에 위치하는 클라이언트의 move packet을 처리
	// 패킷은 로컬에 위치한 클라이언트에게만 보내주면 될 것 같다.
	auto proxycl = &g_proxy_clients[proxyid];

	proxycl->view_list_lock.lock();
	auto old_proxy_vl = proxycl->view_list;
	proxycl->view_list_lock.unlock();

	unordered_set<int> new_proxy_vl;
	// 로컬 클라이언트 이터레이션하면서 시야에 들어온 리스트 작성
	for (auto& cl : g_clients) {
		int localcl_id = cl.m_id;
		//if (proxyid == localcl_id) continue;		// 로컬 클라이언트 대상으로만 돌리기 때문에 의미가 없음
		if (ST_ACTIVE != cl.m_status) continue;
		if (true == is_near(cl, *proxycl)) {
			send_move_packet(cl, proxyid, true);
			new_proxy_vl.insert(localcl_id);
		}
		else 
			send_leave_packet(cl.m_id, proxyid);
	}

	// 작성된 proxy client의 viewlist를 기반으로 local client에게 packet 전달
	for (auto cl : old_proxy_vl) {
		if (0 != new_proxy_vl.count(cl)) {
			send_move_packet(GetClientByID(cl), proxyid, true);
		}
		else {
			//send_leave_packet(cl, proxyid);
			proxycl->view_list_lock.lock();
			proxycl->view_list.erase(cl);
			proxycl->view_list_lock.unlock();
		}
	}
	for (auto cl : new_proxy_vl) {
		if (0 == old_proxy_vl.count(cl)) {
			//send_enter_packet(cl, proxyid, true);
			proxycl->view_list_lock.lock();
			proxycl->view_list.insert(cl);
			proxycl->view_list_lock.unlock();
		}
	}
}

void process_packet(int key, char* buf)
{
	unsigned char packettype = buf[1];

	switch (packettype) {
	case C2S_LOGIN: {
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(buf);
		int user_idx = packet->sender;
		
		g_clients[user_idx].m_id = packet->sender;
		g_clients[user_idx].m_list_slot_idx = packet->sender;
		g_clients[user_idx].y = 19 + g_my_server_id;
		g_clients[user_idx].x = rand() % WORLD_WIDTH;
		//g_clients[user_idx].y = rand() % WORLD_HEIGHT;
		//g_clients[user_idx].y = (rand() % (WORLD_HEIGHT/2)) + ((WORLD_HEIGHT/2) * g_my_server_id);

		enter_game(g_clients[user_idx], packet->name);
		break;
	}
	case C2S_MOVE: {
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(buf);
		//int user_id = packet->sender - (g_my_server_id * MAX_USER_PER_SERVER);
		do_move(GetClientByID(packet->sender), packet->direction);
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

		int proxy_id = packet->id;
		//auto nc = new CLIENT;
		auto nc = &g_proxy_clients[proxy_id];

		//CLIENT& nc = g_clients[packet->id];
		nc->m_ServerID = packet->ownerserverid + SS_ACCEPT_TAG;
		nc->m_prev_size = 0;
		//nc.m_socket = c_socket;
		nc->m_id = proxy_id;
		nc->x = packet->x;
		nc->y = packet->y;
		strcpy_s(nc->m_name, packet->name);
		nc->m_name[MAX_ID_LEN] = NULL;
		nc->m_status = ST_ACTIVE;

		//g_proxy_clients[proxy_id] = nc;

		printf("Client %d from %d Server - proxy", nc->m_id, nc->m_ServerID);
		cout << nc->m_name << " connected\n";

		for (auto& cl : g_clients) {
			if (ST_ACTIVE == cl.m_status) {
				if (true == is_near(cl, *nc)) {
					send_enter_packet(cl.m_id, proxy_id, true);
					nc->view_list_lock.lock();
					nc->view_list.insert(cl.m_id);
					nc->view_list_lock.unlock();
				}
			}
		}
		break;
	}

	case S2S_CLIENT_MOVE: {
		ss_packet_client_move* packet = reinterpret_cast<ss_packet_client_move*>(buf);
		//int proxyid = getProxyClientID(packet->clientid, key);
		int proxyid = packet->clientid;

		auto& proxycl = g_proxy_clients[proxyid];

		proxycl.x = packet->x;
		proxycl.y = packet->y;

		Process_Proxy_Client_Move(proxyid);

		printf("other server[%d] client [%d]moved to : (%d, %d)\n", proxycl.m_ServerID, proxycl.m_id, proxycl.x, proxycl.y);
		break;
	}
	case S2S_CLIENT_DISCONN: {
		ss_packet_disconnect* packet = reinterpret_cast<ss_packet_disconnect*>(buf);

		int proxyid = packet->clientid;
		auto& cl = g_proxy_clients[proxyid];

		int clientid = packet->clientid;
		cl.m_status = ST_FREE;
		for (auto& cl : g_clients) {
			cl.m_cl.lock();
			if (ST_ACTIVE == cl.m_status)
				send_leave_packet(cl.m_id, proxyid);
			cl.m_cl.unlock();
		}

		printf("Server %d proxy Client %d ", g_clients[clientid].m_ServerID, g_clients[clientid].m_id);
		cout << g_clients[clientid].m_name << " disconnected\n";
		break;
	}
	case S2S_CLIENT_HANDOVER: {
		ss_packet_client_handover* packet = reinterpret_cast<ss_packet_client_handover*>(buf);
		auto handover_proxy_cl = &g_proxy_clients[packet->clientid];
		handover_proxy_cl->m_status = ST_FREE;

		auto& newclient = g_clients[packet->clientid];
		newclient.m_id = handover_proxy_cl->m_id;
		newclient.m_list_slot_idx = handover_proxy_cl->m_id;
		newclient.x = handover_proxy_cl->x;
		newclient.y = handover_proxy_cl->y;


		enter_game(newclient, handover_proxy_cl->m_name);

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
	for (int i = 0; i < MAX_USER_PER_SERVER; ++i) {
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

	for (auto& cl : g_proxy_clients) {
		cl.view_list_lock.lock();
		if (0 != cl.view_list.count(user_id))
			cl.view_list.erase(user_id);
		cl.view_list_lock.unlock();
	}
	g_clients[user_id].m_status = ST_FREE;
	g_clients[user_id].m_cl.unlock();

	// 만약 경계영역 내에 있다면 상대 서버에 Disconnect Packet을 보냄
	if (IsInServerBufferSection(g_clients[user_id].y)) {
		ss_packet_disconnect p;
		p.size = sizeof(p);
		p.type = S2S_CLIENT_DISCONN;
		p.clientid = user_id;
		send_server_packet(other_server_info.m_socket, &p);
		g_clients[user_id].m_IsInBufferSection = false;
	}

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
	cout << "other server socket : " << other_server_socket << endl;
	if (false == is_called_by_accept) {
		cout << "Trying to Connect Other Server [" << g_other_server_id << "]\n";
		//SOCKET s_other_server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
		SOCKADDR_IN ServerAddr;
		ZeroMemory(&ServerAddr, sizeof(SOCKADDR_IN));
		ServerAddr.sin_family = AF_INET;
		ServerAddr.sin_port = htons(SERVER_PORT + 10 + g_other_server_id);

		cout << SERVER_PORT + 10 + g_other_server_id << endl;

		inet_pton(AF_INET, "127.0.0.1", &ServerAddr.sin_addr);

		if (true == Is_Other_Server_Connected) return;
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
	other_server_info.m_id = other_server_id;
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
					exit(-1);
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
				else if (user_id >= SS_ACCEPT_TAG && user_id < FS_ACCEPT_TAG) {
					auto& servinfo = other_server_info;

					recv_packet_construct(servinfo, io_byte);
					ZeroMemory(&servinfo.m_recv_over.over, sizeof(servinfo.m_recv_over.over));
					DWORD flags = 0;
					WSARecv(servinfo.m_socket, &servinfo.m_recv_over.wsabuf, 1, NULL, &flags, &servinfo.m_recv_over.over, NULL);
				}
				else if (user_id >= FS_ACCEPT_TAG) {
					auto& servinfo = frontend_server_info;

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
			// Server Connection
			if (user_id == SS_ACCEPT_TAG) {
				if (Is_Other_Server_Connected == true) break;
				Create_Server_Connection(exover->c_socket, g_other_server_id, true);
			}
			else if (user_id == FS_ACCEPT_TAG) {
				// Front End Server
				CreateIoCompletionPort(reinterpret_cast<HANDLE>(exover->c_socket), g_iocp, FS_ACCEPT_TAG, 0);
				frontend_server_info.m_prev_size = 0;
				frontend_server_info.m_recv_over.op = OP_RECV;
				ZeroMemory(&frontend_server_info.m_recv_over.over, sizeof(frontend_server_info.m_recv_over.over));
				frontend_server_info.m_recv_over.wsabuf.buf = frontend_server_info.m_recv_over.io_buf;
				frontend_server_info.m_recv_over.wsabuf.len = MAX_BUF_SIZE;
				frontend_server_info.m_socket = exover->c_socket;
				frontend_server_info.m_id = FS_ACCEPT_TAG;
				DWORD flags = 0;
				int ret = WSARecv(frontend_server_info.m_socket,
					&frontend_server_info.m_recv_over.wsabuf, 1, NULL,
					&flags, &frontend_server_info.m_recv_over.over, NULL);

				if (0 != ret) {
					int err_no = WSAGetLastError();
					if (WSA_IO_PENDING != err_no)
						error_display("WSARecv Error :", err_no);
				}

				cout << "Start Frontend Server Recv : " << frontend_server_info.m_socket << endl;
			}
			break;
		}
		}
	}
}


int main()
{
	WSADATA WSAData;
	WSAStartup(MAKEWORD(2, 2), &WSAData);

	//SetServerNetInfo();

	g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);

	initialize_clients();

	SOCKET s2s_listen_sock;
	SOCKADDR_IN s_address;

	int ret = 0;
	int retry = 0;
	while (true) {
		s2s_listen_sock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
		memset(&s_address, 0, sizeof(s_address));
		s_address.sin_family = AF_INET;
		s_address.sin_port = htons(SERVER_PORT + 10 + g_my_server_id);
		s_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
		ret = ::bind(s2s_listen_sock, reinterpret_cast<sockaddr*>(&s_address), sizeof(s_address));
		if (ret != 0) g_my_server_id++;
		if (ret == 0) break;
		if (retry++ > 10) {
			cout << "Server Init Failed" << endl;
			return 0;
		}
	}
	cout << SERVER_PORT + 10 + g_my_server_id << endl;


	ret = listen(s2s_listen_sock, SOMAXCONN);

	// Server Listen socket IOCP
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(s2s_listen_sock), g_iocp, SS_ACCEPT_TAG, 0);

	cout << "server listen socket : " << s2s_listen_sock << endl;

	g_other_server_id = 1 - g_my_server_id;
	printf("Server ID [%d]\n", g_my_server_id);

	if (g_my_server_id == 0)
		server_buffer_pos = 14;
	else if (g_my_server_id == 1)
		server_buffer_pos = 25;

	Server_Start_X = SESSION_WIDTH;// *g_my_server_id;
	//Server_Start_Y = SESSION_HEIGHT * g_my_server_id;
	Server_Start_Y = 0;


	// other Server Accept
	SOCKET g_server2_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	EXOVER server2_accept_over;
	ZeroMemory(&server2_accept_over.over, sizeof(server2_accept_over.over));
	server2_accept_over.op = OP_ACCEPT;
	server2_accept_over.c_socket = g_server2_socket;
	AcceptEx(s2s_listen_sock, g_server2_socket, server2_accept_over.io_buf, NULL, sizeof(sockaddr_in) + 16, sizeof(sockaddr_in) + 16, NULL, &server2_accept_over.over);

	vector <thread> worker_threads;
	for (int i = 0; i < 4; ++i) worker_threads.emplace_back(worker_thread);

	SOCKET s_other_server = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	Create_Server_Connection(s_other_server, g_other_server_id, false);

	// frontend listen
	listen_socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	// SOCKADDR_IN s_address;
	memset(&s_address, 0, sizeof(s_address));
	s_address.sin_family = AF_INET;
	s_address.sin_port = htons(SERVER_PORT + g_my_server_id);
	s_address.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	::bind(listen_socket, reinterpret_cast<sockaddr*>(&s_address), sizeof(s_address));
	listen(listen_socket, SOMAXCONN);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(listen_socket), g_iocp, FS_ACCEPT_TAG, 0);


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