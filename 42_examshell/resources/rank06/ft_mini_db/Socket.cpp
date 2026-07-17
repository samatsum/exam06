#include "Socket.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

/* ************************************************************************** */

// コンストラクタ
Socket::Socket() : _sockfd(-1)
{
	std::memset(&_address, 0, sizeof(_address));
}

/* ************************************************************************** */

// デストラクタ
Socket::~Socket()
{
	if (_sockfd != -1) {
		close(_sockfd);
	}
}

/* ************************************************************************** */

// ソケットの初期化、アドレス再利用設定、バインド、およびリスンを行う
void Socket::init(int port)
{
	// IPv4 (AF_INET), TCPストリーム (SOCK_STREAM) を指定してソケット生成
	_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (_sockfd == -1) {
		throw std::runtime_error("Socket creation failed");
	}

	// 【エッジケース対策】サーバー再起動直後の TIME_WAIT 状態による bind 失敗を防ぐため、アドレス再利用を許可
	int		opt = 1;

	setsockopt(_sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

	_address.sin_family = AF_INET;
	// 【修正点】INADDR_ANY から INADDR_LOOPBACK に変更し、127.0.0.1 のみをリッスンするようにした
	_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	_address.sin_port = htons(port);

	// ソケットにIPとポートを紐付け
	if (bind(_sockfd, (struct sockaddr*)&_address, sizeof(_address)) < 0) {
		throw std::runtime_error("Socket bind failed");
	}

	// クライアントからの接続要求をキューに貯めるようOSに指示（バックログ=10）
	if (listen(_sockfd, 10) < 0) {
		throw std::runtime_error("Socket listen failed");
	}
}

/* ************************************************************************** */

// クライアントからの接続を受け入れ、通信用の新しいファイルディスクリプタを返す
int Socket::acceptConnection()
{
	struct sockaddr_in	client_addr;
	socklen_t			client_len = sizeof(client_addr);
	int					client_fd = ::accept(_sockfd, (struct sockaddr*)&client_addr, &client_len);

	if (client_fd < 0) {
		throw std::runtime_error("Failed to accept connection");
	}
	return client_fd;
}

/* ************************************************************************** */

// ソケットのファイルディスクリプタを取得する
int Socket::getFd() const
{
	return _sockfd;
}
