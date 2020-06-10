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

#define S2C_LOGIN_OK		11
#define S2C_MOVE			12
#define S2C_ENTER			13
#define S2C_LEAVE			14

#define S2S_CONN			30
#define S2S_CLIENT_DISCONN	31
#define S2S_CLIENT_CONN		32
#define S2S_CLIENT_MOVE		33

#pragma pack(push ,1)

struct ss_packet_connect {
	unsigned char size;
	unsigned char type;
	char serverid;
};

struct ss_packet_disconnect {
	unsigned char size;
	unsigned char type;
	char clientid;
};

struct ss_packet_client_connect {
	unsigned char size;
	unsigned char type;
	int ownerserverid;
	char name[MAX_ID_LEN];
	int id;
	short x, y;
};

struct ss_packet_client_move {
	unsigned char size;
	unsigned char type;
	int clientid;
	short x, y;
};

constexpr unsigned char O_PLAYER = 0;
constexpr unsigned char O_PROXY = 1;
constexpr unsigned char O_NPC = 2;

struct sc_packet_login_ok {
	unsigned char size;
	unsigned char type;
	int id;
	short x, y;
	short hp;
	short level;
	int	exp;
	int serverid;
};

// sc_packet_pos
struct sc_packet_move {
	unsigned char size;
	unsigned char type;
	int id;
	short x, y;
	int move_time;
};

// sc_packet_put_object
struct sc_packet_enter {
	unsigned char size;
	unsigned char type;
	int id;
	char name[MAX_ID_LEN];
	char o_type;
	short x, y;
};

// sc_packet_remove
struct sc_packet_leave {
	unsigned char size;
	unsigned char type;
	int id;
};

struct fs_packet_client_login_ok {
	sc_packet_login_ok p;
	int recvid;
};
struct fs_packet_client_move {
	sc_packet_move p;
	int recvid;
};
struct fs_packet_client_enter {
	sc_packet_enter p;
	int recvid;
};
struct fs_packet_client_leave {
	sc_packet_leave p;
	int recvid;
};


constexpr unsigned char D_UP = 0;
constexpr unsigned char D_DOWN = 1;
constexpr unsigned char D_LEFT = 2;
constexpr unsigned char D_RIGHT = 3;

struct cs_packet_login {
	unsigned char	size;
	unsigned char	type;
	char	name[MAX_ID_LEN];
};

struct cs_packet_move {
	unsigned char	size;
	unsigned char	type;
	char	direction;
	int		move_time;
};

#pragma pack (pop)