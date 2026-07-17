#include "Database.hpp"
#include <fstream>
#include <sstream>

// 引数なしのデフォルトコンストラクタ
Database::Database() {}

Database::~Database() {}

// 起動時のファイル読み込み
void Database::load(const std::string& filepath) {
    std::ifstream ifs(filepath.c_str());
    if (!ifs) return;

    std::string key, value;
    while (ifs >> key >> value) {
        _data[key] = value;
    }
}

// 終了時のファイル保存
void Database::save(const std::string& filepath){
    std::ofstream ofs(filepath.c_str());
    if (!ofs) return;

    for (std::map<std::string, std::string>::iterator it = _data.begin(); it != _data.end(); ++it) {
        ofs << it->first << " " << it->second << "\n";
    }
}

// コマンドの実行
std::string Database::processQuery(const std::string& request) {
    std::stringstream ss(request);
    std::string cmd, key, value;
    
    ss >> cmd;

    if (cmd == "POST") {
        ss >> key >> value;
        if (key.empty() || value.empty()) return "2\n";
        _data[key] = value;
        return "0\n";
    } 
    else if (cmd == "GET") {
        ss >> key;
        if (key.empty()) return "2\n";
        if (_data.count(key) > 0) {
            return "0 " + _data[key] + "\n";
        }
        return "1\n";
    } 
    else if (cmd == "DELETE") {
        ss >> key;
        if (key.empty()) return "2\n";
        // eraseは削除成功時に1を返す
        if (_data.erase(key) > 0) {
            return "0\n";
        }
        return "1\n";
    }

    // 未知のコマンド
    return "2\n";
}