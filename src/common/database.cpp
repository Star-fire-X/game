/**
 * @file database.cpp
 * @brief Legend2 数据库系统实现
 * 
 * 本文件实现基于SQLite的数据持久化功能，包括：
 * - 数据库初始化和表创建
 * - 角色数据的增删改查操作
 * - SQL语句的准备和执行
 * - 数据绑定和结果读取
 */

#include "database.h"
#include <sqlite3.h>
#include <sstream>

namespace mir2::common {

// =============================================================================
// SQLiteDatabase 类实现
// =============================================================================

/**
 * @brief 构造函数 - 初始化数据库路径
 * @param db_path 数据库文件路径
 * 
 * 仅保存路径，不立即打开数据库连接。
 * 需要调用 initialize() 来实际建立连接。
 */
SQLiteDatabase::SQLiteDatabase(const std::string& db_path)
    : db_path_(db_path)
    , db_(nullptr)
{
}

/**
 * @brief 析构函数 - 确保数据库连接被正确关闭
 */
SQLiteDatabase::~SQLiteDatabase() {
    close();
}

/**
 * @brief 移动构造函数 - 转移数据库连接所有权
 * @param other 被移动的数据库对象
 * 
 * 将数据库连接从 other 转移到当前对象，
 * other 的连接指针被置为 nullptr。
 */
SQLiteDatabase::SQLiteDatabase(SQLiteDatabase&& other) noexcept
    : db_path_(std::move(other.db_path_))
    , db_(other.db_)
{
    other.db_ = nullptr;
}

/**
 * @brief 移动赋值运算符 - 转移数据库连接所有权
 * @param other 被移动的数据库对象
 * @return 当前对象的引用
 * 
 * 先关闭当前连接，再从 other 转移连接。
 */
SQLiteDatabase& SQLiteDatabase::operator=(SQLiteDatabase&& other) noexcept {
    if (this != &other) {
        close();
        db_path_ = std::move(other.db_path_);
        db_ = other.db_;
        other.db_ = nullptr;
    }
    return *this;
}

/**
 * @brief 初始化数据库连接
 * @return DbResult<void> 成功返回 ok()，失败返回错误信息
 * 
 * 执行以下操作：
 * 1. 打开 SQLite 数据库文件（不存在则创建）
 * 2. 启用外键约束
 * 3. 创建必要的数据表
 */
DbResult<void> SQLiteDatabase::initialize() {
    if (db_) {
        return DbResult<void>::ok();  // Already initialized
    }
    
    int rc = sqlite3_open(db_path_.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = get_error_message();
        db_ = nullptr;
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, 
            "Failed to open database: " + error);
    }
    
    // Enable foreign keys
    execute("PRAGMA foreign_keys = ON;");
    
    // Create tables
    return create_tables();
}

/**
 * @brief 关闭数据库连接
 * 
 * 安全地关闭 SQLite 连接并释放资源。
 * 可以多次调用，已关闭时不执行任何操作。
 */
void SQLiteDatabase::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

/**
 * @brief 检查数据库是否已打开
 * @return true 如果数据库连接有效
 */
bool SQLiteDatabase::is_open() const {
    return db_ != nullptr;
}

/**
 * @brief 创建数据库表结构
 * @return DbResult<void> 成功返回 ok()，失败返回错误信息
 * 
 * 创建 characters 表，包含以下字段：
 * - 基本信息：id, account_id, name, char_class, gender
 * - 属性数据：level, experience, hp, mp, attack, defense 等
 * - 位置信息：map_id, pos_x, pos_y
 * - JSON 数据：equipment_json, inventory_json, skills_json
 * - 时间戳：created_at, last_login
 * 
 * 同时创建 account_id 和 name 的索引以加速查询。
 */
DbResult<void> SQLiteDatabase::create_tables() {
    const char* create_characters_sql = R"(
        CREATE TABLE IF NOT EXISTS characters (
            id INTEGER PRIMARY KEY,
            account_id TEXT NOT NULL,
            name TEXT UNIQUE NOT NULL,
            char_class INTEGER NOT NULL,
            gender INTEGER NOT NULL DEFAULT 0,
            level INTEGER DEFAULT 1,
            experience INTEGER DEFAULT 0,
            hp INTEGER NOT NULL,
            max_hp INTEGER NOT NULL,
            mp INTEGER NOT NULL,
            max_mp INTEGER NOT NULL,
            attack INTEGER NOT NULL,
            defense INTEGER NOT NULL,
            magic_attack INTEGER NOT NULL,
            magic_defense INTEGER NOT NULL,
            speed INTEGER NOT NULL,
            gold INTEGER DEFAULT 0,
            map_id INTEGER DEFAULT 1,
            pos_x INTEGER DEFAULT 100,
            pos_y INTEGER DEFAULT 100,
            equipment_json TEXT DEFAULT '{}',
            inventory_json TEXT DEFAULT '[]',
            skills_json TEXT DEFAULT '[]',
            created_at INTEGER NOT NULL,
            last_login INTEGER
        );
    )";
    
    auto result = execute(create_characters_sql);
    if (!result) {
        return result;
    }
    
    // Create index on account_id for faster lookups
    execute("CREATE INDEX IF NOT EXISTS idx_characters_account ON characters(account_id);");
    
    // Create index on name for uniqueness checks
    execute("CREATE INDEX IF NOT EXISTS idx_characters_name ON characters(name);");
    
    return DbResult<void>::ok();
}

/**
 * @brief 保存角色数据到数据库
 * @param data 角色数据结构
 * @return DbResult<void> 成功返回 ok()，失败返回错误信息
 * 
 * 使用 INSERT OR REPLACE 语句，自动处理新建和更新：
 * - 如果角色 ID 不存在，插入新记录
 * - 如果角色 ID 已存在，更新现有记录
 */
DbResult<void> SQLiteDatabase::save_character(const CharacterData& data) {
    if (!db_) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    // Use INSERT OR REPLACE to handle both insert and update
    const char* sql = R"(
        INSERT OR REPLACE INTO characters (
            id, account_id, name, char_class, gender,
            level, experience, hp, max_hp, mp, max_mp,
            attack, defense, magic_attack, magic_defense, speed, gold,
            map_id, pos_x, pos_y,
            equipment_json, inventory_json, skills_json,
            created_at, last_login
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";
    
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    if (!bind_character_data(stmt, data)) {
        finalize(stmt);
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Failed to bind parameters");
    }
    
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    return DbResult<void>::ok();
}

/**
 * @brief 根据角色 ID 加载角色数据
 * @param character_id 角色唯一标识符
 * @return DbResult<CharacterData> 成功返回角色数据，失败返回错误信息
 * 
 * 从数据库查询指定 ID 的角色，如果不存在返回 CHARACTER_NOT_FOUND 错误。
 */
DbResult<CharacterData> SQLiteDatabase::load_character(uint32_t character_id) {
    if (!db_) {
        return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "SELECT * FROM characters WHERE id = ?;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    sqlite3_bind_int(stmt, 1, static_cast<int>(character_id));
    
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        CharacterData data = read_character_row(stmt);
        finalize(stmt);
        return DbResult<CharacterData>::ok(std::move(data));
    }
    
    finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        return DbResult<CharacterData>::error(ErrorCode::CHARACTER_NOT_FOUND, 
            "Character not found");
    }
    
    return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, get_error_message());
}

/**
 * @brief 根据角色名称加载角色数据
 * @param name 角色名称
 * @return DbResult<CharacterData> 成功返回角色数据，失败返回错误信息
 * 
 * 通过角色名称查询，用于登录验证和角色查找。
 */
DbResult<CharacterData> SQLiteDatabase::load_character_by_name(const std::string& name) {
    if (!db_) {
        return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "SELECT * FROM characters WHERE name = ?;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        CharacterData data = read_character_row(stmt);
        finalize(stmt);
        return DbResult<CharacterData>::ok(std::move(data));
    }
    
    finalize(stmt);
    
    if (rc == SQLITE_DONE) {
        return DbResult<CharacterData>::error(ErrorCode::CHARACTER_NOT_FOUND, 
            "Character not found");
    }
    
    return DbResult<CharacterData>::error(ErrorCode::DATABASE_ERROR, get_error_message());
}

/**
 * @brief 加载账号下的所有角色
 * @param account_id 账号标识符
 * @return DbResult<std::vector<CharacterData>> 角色列表，可能为空
 * 
 * 用于角色选择界面，返回该账号创建的所有角色。
 */
DbResult<std::vector<CharacterData>> SQLiteDatabase::load_characters_by_account(
    const std::string& account_id) 
{
    if (!db_) {
        return DbResult<std::vector<CharacterData>>::error(
            ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "SELECT * FROM characters WHERE account_id = ?;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<std::vector<CharacterData>>::error(
            ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    sqlite3_bind_text(stmt, 1, account_id.c_str(), -1, SQLITE_TRANSIENT);
    
    std::vector<CharacterData> characters;
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        characters.push_back(read_character_row(stmt));
    }
    
    finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DbResult<std::vector<CharacterData>>::error(
            ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    return DbResult<std::vector<CharacterData>>::ok(std::move(characters));
}

/**
 * @brief 删除角色
 * @param character_id 要删除的角色 ID
 * @return DbResult<void> 成功返回 ok()，失败返回错误信息
 * 
 * 永久删除角色数据，此操作不可逆。
 */
DbResult<void> SQLiteDatabase::delete_character(uint32_t character_id) {
    if (!db_) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "DELETE FROM characters WHERE id = ?;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    sqlite3_bind_int(stmt, 1, static_cast<int>(character_id));
    
    int rc = sqlite3_step(stmt);
    finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    return DbResult<void>::ok();
}

/**
 * @brief 检查角色名称是否已存在
 * @param name 要检查的角色名称
 * @return DbResult<bool> true 表示名称已被使用
 * 
 * 用于创建角色时验证名称唯一性。
 */
DbResult<bool> SQLiteDatabase::character_name_exists(const std::string& name) {
    if (!db_) {
        return DbResult<bool>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "SELECT COUNT(*) FROM characters WHERE name = ?;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<bool>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int count = sqlite3_column_int(stmt, 0);
        finalize(stmt);
        return DbResult<bool>::ok(count > 0);
    }
    
    finalize(stmt);
    return DbResult<bool>::error(ErrorCode::DATABASE_ERROR, get_error_message());
}

/**
 * @brief 获取下一个可用的角色 ID
 * @return DbResult<uint32_t> 下一个可用的角色 ID
 * 
 * 查询当前最大 ID 并加 1，用于创建新角色时分配 ID。
 */
DbResult<uint32_t> SQLiteDatabase::get_next_character_id() {
    if (!db_) {
        return DbResult<uint32_t>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    const char* sql = "SELECT COALESCE(MAX(id), 0) + 1 FROM characters;";
    sqlite3_stmt* stmt = prepare(sql);
    if (!stmt) {
        return DbResult<uint32_t>::error(ErrorCode::DATABASE_ERROR, get_error_message());
    }
    
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        uint32_t next_id = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
        finalize(stmt);
        return DbResult<uint32_t>::ok(next_id);
    }
    
    finalize(stmt);
    return DbResult<uint32_t>::error(ErrorCode::DATABASE_ERROR, get_error_message());
}

// -----------------------------------------------------------------------------
// 内部辅助方法
// -----------------------------------------------------------------------------

/**
 * @brief 执行 SQL 语句（无返回值）
 * @param sql SQL 语句字符串
 * @return DbResult<void> 执行结果
 * 
 * 用于执行 CREATE TABLE、CREATE INDEX 等 DDL 语句。
 */
DbResult<void> SQLiteDatabase::execute(const std::string& sql) {
    if (!db_) {
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, "Database not open");
    }
    
    char* error_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error_msg);
    
    if (rc != SQLITE_OK) {
        std::string error = error_msg ? error_msg : "Unknown error";
        sqlite3_free(error_msg);
        return DbResult<void>::error(ErrorCode::DATABASE_ERROR, error);
    }
    
    return DbResult<void>::ok();
}

/**
 * @brief 准备 SQL 语句
 * @param sql SQL 语句字符串
 * @return sqlite3_stmt* 预编译语句句柄，失败返回 nullptr
 * 
 * 使用 sqlite3_prepare_v2 编译 SQL 语句，
 * 返回的句柄需要用 finalize() 释放。
 */
sqlite3_stmt* SQLiteDatabase::prepare(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return nullptr;
    }
    return stmt;
}

/**
 * @brief 释放预编译语句
 * @param stmt 预编译语句句柄
 */
void SQLiteDatabase::finalize(sqlite3_stmt* stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
    }
}

/**
 * @brief 获取最后一次错误信息
 * @return 错误描述字符串
 */
std::string SQLiteDatabase::get_error_message() const {
    if (db_) {
        return sqlite3_errmsg(db_);
    }
    return "Database not open";
}

/**
 * @brief 绑定角色数据到预编译语句
 * @param stmt 预编译语句句柄
 * @param data 角色数据
 * @return true 绑定成功
 * 
 * 按顺序绑定所有角色字段到 SQL 参数占位符。
 */
bool SQLiteDatabase::bind_character_data(sqlite3_stmt* stmt, const CharacterData& data) {
    int idx = 1;
    
    // Handle id: if 0, let SQLite auto-generate, otherwise use provided id
    if (data.id == 0) {
        sqlite3_bind_null(stmt, idx++);
    } else {
        sqlite3_bind_int(stmt, idx++, static_cast<int>(data.id));
    }
    
    sqlite3_bind_text(stmt, idx++, data.account_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, data.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, idx++, static_cast<int>(data.char_class));
    sqlite3_bind_int(stmt, idx++, static_cast<int>(data.gender));
    
    // Stats
    sqlite3_bind_int(stmt, idx++, data.stats.level);
    sqlite3_bind_int(stmt, idx++, data.stats.experience);
    sqlite3_bind_int(stmt, idx++, data.stats.hp);
    sqlite3_bind_int(stmt, idx++, data.stats.max_hp);
    sqlite3_bind_int(stmt, idx++, data.stats.mp);
    sqlite3_bind_int(stmt, idx++, data.stats.max_mp);
    sqlite3_bind_int(stmt, idx++, data.stats.attack);
    sqlite3_bind_int(stmt, idx++, data.stats.defense);
    sqlite3_bind_int(stmt, idx++, data.stats.magic_attack);
    sqlite3_bind_int(stmt, idx++, data.stats.magic_defense);
    sqlite3_bind_int(stmt, idx++, data.stats.speed);
    sqlite3_bind_int(stmt, idx++, data.stats.gold);
    
    // Position
    sqlite3_bind_int(stmt, idx++, static_cast<int>(data.map_id));
    sqlite3_bind_int(stmt, idx++, data.position.x);
    sqlite3_bind_int(stmt, idx++, data.position.y);
    
    // JSON data
    sqlite3_bind_text(stmt, idx++, data.equipment_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, data.inventory_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, idx++, data.skills_json.c_str(), -1, SQLITE_TRANSIENT);
    
    // Timestamps
    sqlite3_bind_int64(stmt, idx++, data.created_at);
    sqlite3_bind_int64(stmt, idx++, data.last_login);
    
    return true;
}

/**
 * @brief 从查询结果读取角色数据
 * @param stmt 已执行的预编译语句
 * @return CharacterData 解析后的角色数据
 * 
 * 按列顺序读取所有字段，处理 NULL 值和类型转换。
 */
CharacterData SQLiteDatabase::read_character_row(sqlite3_stmt* stmt) {
    CharacterData data;
    int col = 0;
    
    data.id = static_cast<uint32_t>(sqlite3_column_int(stmt, col++));
    
    const char* account_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    data.account_id = account_id ? account_id : "";
    
    const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    data.name = name ? name : "";
    
    data.char_class = static_cast<CharacterClass>(sqlite3_column_int(stmt, col++));
    data.gender = static_cast<Gender>(sqlite3_column_int(stmt, col++));
    
    // Stats
    data.stats.level = sqlite3_column_int(stmt, col++);
    data.stats.experience = sqlite3_column_int(stmt, col++);
    data.stats.hp = sqlite3_column_int(stmt, col++);
    data.stats.max_hp = sqlite3_column_int(stmt, col++);
    data.stats.mp = sqlite3_column_int(stmt, col++);
    data.stats.max_mp = sqlite3_column_int(stmt, col++);
    data.stats.attack = sqlite3_column_int(stmt, col++);
    data.stats.defense = sqlite3_column_int(stmt, col++);
    data.stats.magic_attack = sqlite3_column_int(stmt, col++);
    data.stats.magic_defense = sqlite3_column_int(stmt, col++);
    data.stats.speed = sqlite3_column_int(stmt, col++);
    data.stats.gold = sqlite3_column_int(stmt, col++);
    
    // Position
    data.map_id = static_cast<uint32_t>(sqlite3_column_int(stmt, col++));
    data.position.x = sqlite3_column_int(stmt, col++);
    data.position.y = sqlite3_column_int(stmt, col++);
    
    // JSON data
    const char* equipment = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    data.equipment_json = equipment ? equipment : "{}";
    
    const char* inventory = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    data.inventory_json = inventory ? inventory : "[]";
    
    const char* skills = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col++));
    data.skills_json = skills ? skills : "[]";
    
    // Timestamps
    data.created_at = sqlite3_column_int64(stmt, col++);
    data.last_login = sqlite3_column_int64(stmt, col++);
    
    return data;
}

} // namespace mir2::common
