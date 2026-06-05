#include "Server.hpp"
#include <iostream>
#include <cstdlib>
#include <csignal>

/* ************************************************************************** */

// シグナルハンドラはOSから直接呼ばれるため引数を取れず、グローバル変数でインスタンスを参照するC言語APIの制約
static Server*	g_server_ptr = NULL;

/* ************************************************************************** */

// OSから SIGINT (Ctrl+C など) を受け取った際に割り込まれる関数
void sigIntHandler(int signum)
{
	(void)signum; // 未使用引数の警告回避
	
	if (g_server_ptr != NULL) {
		g_server_ptr->saveDatabase(); // メモリからファイルへ安全にシリアライズ
		g_server_ptr->stop();         // メインループのフラグを折る
	}
	
	std::exit(0); // プロセスを安全に終了
}

/* ************************************************************************** */

// エントリーポイント。引数の検証、シグナルの登録、サーバーの起動とエラーハンドリングを行う
int main(int ac, char* av[])
{
	if (ac != 3) {
		std::cerr << "Wrong number of arguments" << std::endl;
		return EXIT_FAILURE;
	}

	// OSに対して、「SIGINTを受け取ったらデフォルト動作(即死)ではなく sigIntHandler を呼べ」と登録
	std::signal(SIGINT, sigIntHandler);

	try {
		Server	server;

		g_server_ptr = &server;
		server.init(std::atoi(av[1]), std::string(av[2]));
		server.run(); // メインスレッドはここでブロッキングし、イベントループを回し続ける
	} catch (const std::exception& e) {
		// malloc失敗やソケット例外などを最上位でキャッチし、不正終了を防ぐ
		std::cerr << "Internal server error: " << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
