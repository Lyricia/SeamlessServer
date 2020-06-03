#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <windows.h>
#include <iostream>
#include <unordered_map>
#include <chrono>
using namespace std;
using namespace chrono;

//#include "..\..\SimpleIOCPServer\SimpleIOCPServer\protocol.h"
#include "../../Server/SeamlessServer/protocol.h"

#pragma comment(lib, "sfml-graphics.lib")
#pragma comment(lib, "sfml-main.lib")
#pragma comment(lib, "sfml-network.lib")
#pragma comment(lib, "sfml-system.lib")
#pragma comment(lib, "sfml-window.lib")

sf::TcpSocket g_socket;

enum E_STATUS { SERVER_SELECT, INGAME };

constexpr auto SCREEN_WIDTH = 19;
constexpr auto SCREEN_HEIGHT = 19;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = TILE_WIDTH * SCREEN_WIDTH + 10;   // size of window
constexpr auto WINDOW_HEIGHT = TILE_WIDTH * SCREEN_WIDTH + 10;
constexpr auto BUF_SIZE = 200;
constexpr auto MAX_USER = 10;

int g_left_x;
int g_top_y;
int g_myid;
int g_myServerId;

sf::RenderWindow* g_window;
sf::Font g_font;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;
	char m_mess[MAX_STR_LEN];
	high_resolution_clock::time_point m_time_out;
	sf::Text m_text;
	sf::Text m_nameboard;

public:
	int m_x, m_y;
	char m_name[50]{};

	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		m_time_out = high_resolution_clock::now();
	}
	OBJECT() {
		m_showing = false;
		m_time_out = high_resolution_clock::now();
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	sf::Vector2f GetPos() {
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		return sf::Vector2f{rx, ry};
	}

	void NameboardSetting() {
		m_nameboard.setFont(g_font);
		m_nameboard.setString(m_name);
		m_nameboard.setCharacterSize(40);
		m_nameboard.setStyle(sf::Text::Bold);
	}

	void setColor(sf::Color c) {
		m_sprite.setColor(c);
	}

	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 8;
		float ry = (m_y - g_top_y) * 65.0f + 8;
		
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		if (high_resolution_clock::now() < m_time_out) {
			m_text.setPosition(rx - 10, ry - 10);
			g_window->draw(m_text);
		}

		m_nameboard.setPosition(sf::Vector2f{ rx - 10, ry + 30 });
		g_window->draw(m_nameboard);
	}
	void add_chat(char chat[]) {
		m_text.setFont(g_font);
		m_text.setString(chat);
		m_time_out = high_resolution_clock::now() + 1s;
	}
};

OBJECT avatar;
unordered_map <int, OBJECT> npcs;

OBJECT white_tile;
OBJECT black_tile;

sf::Texture* board;
sf::Texture* pieces;

void client_initialize()
{
	board = new sf::Texture;
	pieces = new sf::Texture;
	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		while (true);
	}
	board->loadFromFile("chessmap.bmp");
	pieces->loadFromFile("chess2.png");
	white_tile = OBJECT{ *board, 5, 5, TILE_WIDTH, TILE_WIDTH };
	black_tile = OBJECT{ *board, 69, 5, TILE_WIDTH, TILE_WIDTH };
	avatar = OBJECT{ *pieces, 128, 0, 64, 64 };
	avatar.move(4, 4);
}

void client_finish()
{
	delete board;
	delete pieces;
}
#pragma optimize("gpsy", off)
void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[1])
	{
	case S2C_LOGIN_OK:
	{
		sc_packet_login_ok* my_packet = reinterpret_cast<sc_packet_login_ok*>(ptr);
		g_myid = my_packet->id;
		avatar.move(my_packet->x, my_packet->y);
		g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
		g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
		g_myServerId = my_packet->serverid;
		
		avatar.show();
		break;
	}

	case S2C_ENTER:
	{
		sc_packet_enter* my_packet = reinterpret_cast<sc_packet_enter*>(ptr);
		int id = my_packet->id;
		
		if (id == g_myid) {
			sprintf_s(avatar.m_name, my_packet->name);
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
			g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
			avatar.show();
		}
		else {
			auto& cl = npcs[id];
			
			sprintf_s(cl.m_name, my_packet->name);
			switch (my_packet->o_type)
			{
			case O_PLAYER:
				//memcpy(npcs[id].m_name, tmp, 50);
				npcs[id] = OBJECT{ *pieces, 0, 0, 64, 64 };
				npcs[id].setColor(sf::Color::Yellow);
				npcs[id].NameboardSetting();
				break;
			case O_PROXY:
				memcpy(npcs[id].m_name, my_packet->name, 50);
				npcs[id] = OBJECT{ *pieces, 64, 0, 64, 64 };
				npcs[id].setColor(sf::Color::Blue);
				npcs[id].NameboardSetting();
				break;
			case O_NPC:
				npcs[id] = OBJECT{ *pieces, 256, 0, 64, 64 };
				break;
			}
		
			npcs[id].move(my_packet->x, my_packet->y);
			npcs[id].show();
		}
		break;
	}
	case S2C_MOVE:
	{
		sc_packet_move* my_packet = reinterpret_cast<sc_packet_move*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - (SCREEN_WIDTH / 2);
			g_top_y = my_packet->y - (SCREEN_HEIGHT / 2);
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case S2C_LEAVE:
	{
		sc_packet_leave* my_packet = reinterpret_cast<sc_packet_leave*>(ptr);
		int other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			if (0 != npcs.count(other_id))
				npcs[other_id].hide();
		}
		break;
	}
	default:
		printf("Unknown PACKET type [%d]\n", ptr[1]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) in_packet_size = ptr[0];
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = g_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv 에러!";
		while (true);
	}

	if (recv_result == sf::Socket::Disconnected)
	{
		wcout << L"서버 접속 종료.";
		g_window->close();
	}

	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (g_myServerId == 0) {
				if (tile_y >= 20) {
					white_tile.setColor(sf::Color::Red);
					black_tile.setColor(sf::Color::Red);
				}
				else {
					white_tile.setColor(sf::Color::White);
					black_tile.setColor(sf::Color::White);
				}
			}
			else {
				if (tile_y < 20) {
					white_tile.setColor(sf::Color::Red);
					black_tile.setColor(sf::Color::Red);
				}
				else {
					white_tile.setColor(sf::Color::White);
					black_tile.setColor(sf::Color::White);
				}
			}

			
			if (((tile_x / 3 + tile_y / 3) % 2) == 0) {
				white_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				white_tile.a_draw();
			}
			else
			{
				black_tile.a_move(TILE_WIDTH * i + 7, TILE_WIDTH * j + 7);
				black_tile.a_draw();
			}
		}
	avatar.draw();
	//	for (auto &pl : players) pl.draw();
	for (auto& npc : npcs) 
		npc.second.draw();

	sf::Text text;
	text.setFont(g_font);
	char buf[100];
	sprintf_s(buf, "(%d, %d)", avatar.m_x, avatar.m_y);
	text.setString(buf);
	g_window->draw(text);

}

void send_packet(void* packet)
{
	char* p = reinterpret_cast<char*>(packet);
	size_t sent;
	g_socket.send(p, p[0], sent);
}

void send_move_packet(unsigned char dir)
{
	cs_packet_move m_packet;
	m_packet.type = C2S_MOVE;
	m_packet.size = sizeof(m_packet);
	m_packet.direction = dir;
	send_packet(&m_packet);
}

bool IsContained(sf::Vector2f lefttop, sf::Vector2f size, sf::Vector2i mouse) {
	if (mouse.x < lefttop.x || lefttop.x + size.x < mouse.x) return false;
	if (mouse.y < lefttop.y || lefttop.y + size.y < mouse.y) return false;
	return true;
}

E_STATUS status = E_STATUS::SERVER_SELECT;

void ServerConnect(int serverid) {
	wcout.imbue(locale("korean"));
	sf::Socket::Status sock = g_socket.connect("127.0.0.1", SERVER_PORT + serverid);
	g_socket.setBlocking(false);

	if (sock != sf::Socket::Done) {
		wcout << L"서버와 연결할 수 없습니다.\n";
		while (true);
	}

	cs_packet_login l_packet;
	l_packet.size = sizeof(l_packet);
	l_packet.type = C2S_LOGIN;
	int t_id = GetCurrentProcessId();
	sprintf_s(avatar.m_name, "P%d", t_id);
	sprintf_s(l_packet.name, avatar.m_name);
	send_packet(&l_packet);

	status = INGAME;

	sf::View view = g_window->getView();
	view.zoom(2.0f);
	view.move(WINDOW_WIDTH / 4, WINDOW_HEIGHT / 4);
	g_window->setView(view);

	avatar.NameboardSetting();

	cout << "Server Connected" << endl;
}


int main()
{
	int myserver = -1;
	client_initialize();

	//cout << "choose server : ";
	//cin >> myserver;
	//cout << endl;
	//
	//wcout.imbue(locale("korean"));
	//sf::Socket::Status status = g_socket.connect("127.0.0.1", SERVER_PORT + myserver);
	//g_socket.setBlocking(false);
	//
	//if (status != sf::Socket::Done) {
	//	wcout << L"서버와 연결할 수 없습니다.\n";
	//	while (true);
	//}
	//
	//
	//cs_packet_login l_packet;
	//l_packet.size = sizeof(l_packet);
	//l_packet.type = C2S_LOGIN;
	//int t_id = GetCurrentProcessId();
	//sprintf_s(l_packet.name, "P%d", t_id);
	//send_packet(&l_packet);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH / 2, WINDOW_HEIGHT / 2), "2D CLIENT");
	g_window = &window;


	sf::CircleShape shape1(100.0f);
	sf::CircleShape shape2(100.0f);
	shape1.setFillColor(sf::Color::Magenta);
	shape1.setPosition(50, 50);
	shape2.setFillColor(sf::Color::Magenta);
	shape2.setPosition(250.0f, 50.0f);

	while (window.isOpen())
	{
		sf::Event event;
		while (window.pollEvent(event))
		{
			if (event.type == sf::Event::Closed)
				window.close();
			if (status == INGAME && event.type == sf::Event::KeyPressed) {
				int p_type = -1;
				switch (event.key.code) {
				case sf::Keyboard::Left:
					send_move_packet(D_LEFT);
					break;
				case sf::Keyboard::Right:
					send_move_packet(D_RIGHT);
					break;
				case sf::Keyboard::Up:
					send_move_packet(D_UP);
					break;
				case sf::Keyboard::Down:
					send_move_packet(D_DOWN);
					break;
				case sf::Keyboard::Escape:
					window.close();
					break;
				}
			}
			if (status == SERVER_SELECT && event.type == sf::Event::MouseButtonPressed) {
				auto mousepos = sf::Vector2i(event.mouseButton.x, event.mouseButton.y);

				shape1.setFillColor(sf::Color::Red);
				shape2.setFillColor(sf::Color::Red);

				if (IsContained(shape1.getPosition(), sf::Vector2f(shape1.getRadius() * 2, shape1.getRadius() * 2), mousepos)) {
					shape1.setFillColor(sf::Color::Yellow);
					myserver = 0;
					ServerConnect(myserver);
				}
				else if (IsContained(shape2.getPosition(), sf::Vector2f(shape2.getRadius() * 2, shape2.getRadius() * 2), mousepos)) {
					shape2.setFillColor(sf::Color::Yellow);
					myserver = 1;
					ServerConnect(myserver);
				}
			}
		}

		window.clear();

		switch (status) {
		case SERVER_SELECT: {
			window.draw(shape1);
			window.draw(shape2);

			sf::Text text;
			text.setFont(g_font);
			text.setPosition(shape1.getPosition() + sf::Vector2f{ 90, 80 });
			text.setString("0          1");

			window.draw(text);
			break;
		}
		case INGAME: {
			client_main();


			break;
		}
		default:
			break;
		}

		window.display();
	}
	client_finish();

	return 0;
}
#pragma optimize("gpsy", on)