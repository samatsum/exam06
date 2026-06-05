#include "Database.hpp"
#include <fstream>
#include <sstream>

/* ************************************************************************** */

// コンストラクタ
Database::Database()
{
}

/* ************************************************************************** */

// デストラクタ
Database::~Database()
{
}

/* ************************************************************************** */

// ファイルからデータを読み込みメモリにロードする
// 時間計算量: O(N) (Nはファイル内の行数)
bool Database::load(const std::string& path)
{
	std::ifstream	file(path.c_str());

	if (!file.is_open()) {
		return false; // ファイルが存在しない場合は安全にスキップ
	}
	
	std::string		key;
	std::string		value;

	// 空白区切りで抽出し、マップを構築。バッファオーバーフローの危険はない。
	while (file >> key >> value) {
		_data[key] = value;
	}
	
	file.close(); // RAIIの原則により関数抜けでも自動で閉じるが、明示的に記述
	return true;
}

/* ************************************************************************** */

// メモリ上のデータをファイルに書き出して保存（シリアライズ）する
// 時間計算量: O(N)
bool Database::save(const std::string& path) const
{
	std::ofstream	file(path.c_str());

	if (!file.is_open()) {
		return false;
	}
	
	// イテレータを用いてO(N)で全要素をファイルへ書き出し（シリアライズ）
	for (std::map<std::string, std::string>::const_iterator it = _data.begin(); it != _data.end(); ++it) {
		file << it->first << " " << it->second << "\n";
	}
	
	file.close();
	return true;
}

/* ************************************************************************** */

// 外部から受け取った生文字列をパースし、適切なハンドラへルーティングする
std::string Database::processQuery(const std::string& query)
{
	std::istringstream	iss(query);
	std::string			command;
	std::string			key;
	std::string			value;
	std::string			extra;
	
	// コマンドとキーが抽出できない場合は無効
	if (!(iss >> command >> key)) {
		return "2\n";
	}
	
	iss >> value;
	// 【エッジケース対策】規定以上の引数が含まれている場合は不正リクエストとして弾く
	if (iss >> extra) {
		return "2\n";
	}

	if (command == "POST") {
		return handlePost(key, value);
	}
	if (command == "GET") {
		return handleGet(key, value);
	}
	if (command == "DELETE") {
		return handleDelete(key, value);
	}
	
	return "2\n";
}

/* ************************************************************************** */

// POSTコマンドを処理し、データベースにエントリを追加する
std::string Database::handlePost(const std::string& key, const std::string& value)
{
	if (value.empty()) {
		return "2\n";
	}
	_data[key] = value; // O(log N) で挿入または上書き
	return "0\n";
}

/* ************************************************************************** */

// GETコマンドを処理し、データベースからエントリを取得する
std::string Database::handleGet(const std::string& key, const std::string& value)
{
	if (!value.empty()) {
		return "2\n"; // GETにvalueは不要
	}
	
	std::map<std::string, std::string>::const_iterator	it = _data.find(key);
	
	if (it == _data.end()) {
		return "1\n"; // 見つからない場合 O(log N)
	}
	
	return "0 " + it->second + "\n";
}

/* ************************************************************************** */

// DELETEコマンドを処理し、データベースのエントリを削除する
std::string Database::handleDelete(const std::string& key, const std::string& value)
{
	if (!value.empty()) {
		return "2\n"; // DELETEにvalueは不要
	}
	
	std::map<std::string, std::string>::iterator	it = _data.find(key);
	
	if (it == _data.end()) {
		return "1\n";
	}
	
	_data.erase(it); // O(log N) で安全にノードを解放
	return "0\n";
}