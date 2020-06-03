#pragma once

constexpr int MAX_ID_LEN = 50;
constexpr int MAX_STR_LEN = 255;

#define WORLD_WIDTH		20
#define WORLD_HEIGHT	40

#define SESSION_WIDTH		20
#define SESSION_HEIGHT		20

#define VIEW_RANGE		5

#define SERVER_PORT			9000
#define INTER_SERVER_PORT	9010

#define C2S_LOGIN	1
#define C2S_MOVE	2

#define S2C_LOGIN_OK		1
#define S2C_MOVE			2
#define S2C_ENTER			3
#define S2C_LEAVE			4

#define S2S_CONN			100
#define S2S_CLIENT_DISCONN			101
#define S2S_CLIENT_CONN		102
#define S2S_CLIENT_MOVE		103

#pragma pack(push ,1)

struct ss_packet_connect {
	char size;
	char type;
	char serverid;
};

struct ss_packet_disconnect {
	char size;
	char type;
	char clientid;
};

struct ss_packet_client_connect {
	char size;
	char type;
	int ownerserverid;
	char name[MAX_ID_LEN];
	int id;
	short x, y;
};

struct ss_packet_client_move {
	char size;
	char type;
	int clientid;
	short x, y;
};

constexpr unsigned char O_PLAYER = 0;
constexpr unsigned char O_PROXY = 1;
constexpr unsigned char O_NPC = 2;

struct sc_packet_login_ok {
	char size;
	char type;
	int id;
	short x, y;
	short hp;
	short level;
	int	exp;
	int serverid;
};

// sc_packet_pos
struct sc_packet_move {
	char size;
	char type;
	int id;
	short x, y;
};

// sc_packet_put_object
struct sc_packet_enter {
	char size;
	char type;
	int id;
	char name[MAX_ID_LEN];
	char o_type;
	short x, y;
};

// sc_packet_remove
struct sc_packet_leave {
	char size;
	char type;
	int id;
};

constexpr unsigned char D_UP = 0;
constexpr unsigned char D_DOWN = 1;
constexpr unsigned char D_LEFT = 2;
constexpr unsigned char D_RIGHT = 3;

struct cs_packet_login {
	char	size;
	char	type;
	char	name[MAX_ID_LEN];
};

struct cs_packet_move {
	char	size;
	char	type;
	char	direction;
};

#pragma pack (pop)