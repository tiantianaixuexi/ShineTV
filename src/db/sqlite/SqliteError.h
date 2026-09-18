#pragma once
#include <string>

namespace shine::db::sqlite {

struct SqliteError {
    int code = 0; // sqlite3 result code
    std::string message;
};

} // namespace shine::db::sqlite
