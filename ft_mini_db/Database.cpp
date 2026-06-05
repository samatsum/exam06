// Database.cpp
#include "Database.hpp"
#include <fstream>
#include <sstream>

Database::Database() {}
Database::~Database() {}

// 時間計算量: O(N) (Nはファイル内の行数)
bool Database::load(const std::string& path) {
  std::ifstream file(path.c_str());
  if (!file.is_open()) return false; // ファイルが存在しない場合は安全にスキップ
  
  std::string key, value;
  // 空白区切りで抽出し、マップを構築。バッファオーバーフローの危険はない。
  while (file >> key >> value) {
    _data[key] = value;
  }
  file.close(); // RAIIの原則により関数抜けでも自動で閉じるが、明示的に記述
  return true;
}

// 時間計算量: O(N)
bool Database::save(const std::string& path) const {
  std::ofstream file(path.c_str());
  if (!file.is_open()) return false;
  
  // イテレータを用いてO(N)で全要素をファイルへ書き出し（シリアライズ）
  for (std::map<std::string, std::string>::const_iterator it = _data.begin();
       it != _data.end(); ++it) {
    file << it->first << " " << it->second << "\n";
  }
  file.close();
  return true;
}

// 外部から受け取った生文字列をパースし、適切なハンドラへルーティングする
std::string Database::processQuery(const std::string& query) {
  std::istringstream iss(query);
  std::string command, key, value, extra;
  
  // コマンドとキーが抽出できない場合は無効
  if (!(iss >> command >> key)) return "2\n";
  
  iss >> value;
  // 【エッジケース対策】規定以上の引数が含まれている場合は不正リクエストとして弾く
  if (iss >> extra) return "2\n";

  if (command == "POST") return handlePost(key, value);
  if (command == "GET") return handleGet(key, value);
  if (command == "DELETE") return handleDelete(key, value);
  return "2\n";
}

std::string Database::handlePost(const std::string& key, const std::string& value) {
  if (value.empty()) return "2\n";
  _data[key] = value; // O(log N) で挿入または上書き
  return "0\n";
}

std::string Database::handleGet(const std::string& key, const std::string& value) {
  if (!value.empty()) return "2\n"; // GETにvalueは不要
  std::map<std::string, std::string>::const_iterator it = _data.find(key);
  if (it == _data.end()) return "1\n"; // 見つからない場合 O(log N)
  return "0 " + it->second + "\n";
}

std::string Database::handleDelete(const std::string& key, const std::string& value) {
  if (!value.empty()) return "2\n"; // DELETEにvalueは不要
  std::map<std::string, std::string>::iterator it = _data.find(key);
  if (it == _data.end()) return "1\n";
  _data.erase(it); // O(log N) で安全にノードを解放
  return "0\n";
}