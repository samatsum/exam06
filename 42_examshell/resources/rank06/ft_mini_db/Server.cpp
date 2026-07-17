#include "Server.hpp"
#include <sys/socket.h>
#include <unistd.h>
#include <iostream>
#include <stdexcept>

/* ************************************************************************** */

// コンストラクタ
Server::Server() : _max_fd(-1), _is_running(false)
{
	FD_ZERO(&_active_fds); // FDセットの初期化（ゴミデータをクリア）
}

/* ************************************************************************** */

// デストラクタ
Server::~Server()
{
}

/* ************************************************************************** */

// サーバーの初期設定を行い、リスニングソケットを監視対象に追加する
void Server::init(int port, const std::string& db_path)
{
	_db_path = db_path;
	_db.load(_db_path); // 起動時に既存ファイルをメモリへロード
	listening_socket_.init(port);
	
	int		listen_fd = listening_socket_.getFd();

	FD_SET(listen_fd, &_active_fds); // リスニングソケットを監視対象に追加
	_max_fd = listen_fd;
}

/* ************************************************************************** */

// メインループを開始し、サーバーを実行状態にする
void Server::run()
{
	_is_running = true;
	std::cout << "ready" << std::endl;

	while (_is_running) {
		handleConnections();
	}
}

/* ************************************************************************** */

// メインループの実行フラグを折り、サーバーを停止させる
void Server::stop()
{
	_is_running = false;
}

/* ************************************************************************** */

// 現在のデータベースのメモリ状態をファイルへ安全に保存する
void Server::saveDatabase()
{
	_db.save(_db_path);
}

/* ************************************************************************** */

// select() を用いてI/O多重化を行い、イベントのあったソケットを特定して振り分ける
void Server::handleConnections()
{
	// selectは渡された fd_set を破壊的変更するため、毎回コピーを使用する
	fd_set	read_fds = _active_fds;
	
	// 読み込み可能になったFDがあるまでOSレベルでプロセスをスリープさせる（CPUリソースの節約）
	if (select(_max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
		if (!_is_running) {
			return; // SIGINTによる割り込みでのエラーなら正常終了フローへ
		}
		throw std::runtime_error("Failed to select");
	}

	// 状態が変化したFDを特定するループ
	for (int fd = 0; fd <= _max_fd; ++fd) {
		if (!FD_ISSET(fd, &read_fds)) {
			continue;
		}

		if (fd == listening_socket_.getFd()) {
			handleNewConnection(); // リスニングソケットの反応 ＝ 新規接続
		} else {
			handleClientData(fd);  // クライアントソケットの反応 ＝ データ受信または切断
		}
	}
}

/* ************************************************************************** */

// 新しいクライアントからの接続を受け入れ、監視対象のFDセットに追加する
void Server::handleNewConnection()
{
	int		client_fd = listening_socket_.acceptConnection();

	FD_SET(client_fd, &_active_fds); // 新規クライアントを監視対象に追加
	if (client_fd > _max_fd) {
		_max_fd = client_fd; // max_fdを更新
	}
}

/* ************************************************************************** */

// クライアントからデータを受信し、パースおよびデータベース処理を行って応答を返す
void Server::handleClientData(int fd)
{
	char	buf[1024];
	// selectで受信可能が担保されているため、recvはブロックしない
	int		bytes_read = recv(fd, buf, sizeof(buf) - 1, 0);
	
	if (bytes_read <= 0) {
		// 0は切断(FIN受信)、負の値はエラー。どちらもクライアントを除外する。
		removeClient(fd);
		return;
	}
	
	buf[bytes_read] = '\0';
	// データ層にパースと処理を委譲
	std::string		response = _db.processQuery(std::string(buf));

	send(fd, response.c_str(), response.size(), 0);
}

/* ************************************************************************** */

// 指定されたクライアントのソケットを監視対象から外し、リソースを解放する
void Server::removeClient(int fd)
{
	FD_CLR(fd, &_active_fds); // 監視対象から外す
	close(fd);                // OSにリソースを返却（ファイルディスクリプタリーク防止）
	
	// 【効率化】削除したのが最大FDだった場合、新しい最大値を探して max_fd を切り詰める
	if (fd == _max_fd) {
		while (_max_fd > listening_socket_.getFd() && !FD_ISSET(_max_fd, &_active_fds)) {
			_max_fd--;
		}
	}
}
