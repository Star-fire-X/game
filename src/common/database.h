/**
 * @file database.h
 * @brief Legend2 数据库系统
 * 
 * 基于SQLite的游戏数据持久化系统，包括：
 * - 数据库接口定义
 * - SQLite实现
 * - 角色数据的增删改查
 */

#ifndef LEGEND2_DATABASE_H
#define LEGEND2_DATABASE_H

#include "common/character_data.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <functional>
#include <cstdint>

// Include database types for full definitions
#include "common/types/database_types.h"

// Forward declaration for SQLite handle (in global namespace)
struct sqlite3;
struct sqlite3_stmt;

namespace mir2::common {

/**
 * @brief 数据库结果包装器
 * @tparam T 结果值类型
 */
template<typename T>
struct DbResult {
    bool success = false;                        ///< 是否成功
    ErrorCode error_code = ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    T value;                                     ///< 结果值
    
    /// 创建成功结果
    static DbResult<T> ok(T val) {
        return {true, ErrorCode::SUCCESS, "", std::move(val)};
    }
    
    /// 创建失败结果
    static DbResult<T> error(ErrorCode code, const std::string& msg) {
        return {false, code, msg, T{}};
    }
    
    /// 布尔转换运算符
    explicit operator bool() const { return success; }
};

/// void类型的特化版本
template<>
struct DbResult<void> {
    bool success = false;                        ///< 是否成功
    ErrorCode error_code = ErrorCode::SUCCESS;   ///< 错误码
    std::string error_message;                   ///< 错误信息
    
    /// 创建成功结果
    static DbResult<void> ok() {
        return {true, ErrorCode::SUCCESS, ""};
    }
    
    /// 创建失败结果
    static DbResult<void> error(ErrorCode code, const std::string& msg) {
        return {false, code, msg};
    }
    
    /// 布尔转换运算符
    explicit operator bool() const { return success; }
};

/**
 * @brief 数据库接口（用于角色持久化）
 * 
 * 定义数据库操作的抽象接口，支持不同的数据库实现。
 */
class IDatabase {
public:
    virtual ~IDatabase() = default;
    
    /// 初始化数据库（创建表等）
    virtual DbResult<void> initialize() = 0;
    
    /// 关闭数据库连接
    virtual void close() = 0;
    
    /// 检查数据库是否已打开
    virtual bool is_open() const = 0;
    
    // --- 角色操作 (Character Operations) ---
    
    /// 保存角色数据（插入或更新）
    virtual DbResult<void> save_character(const CharacterData& data) = 0;
    
    /// 根据ID加载角色
    virtual DbResult<CharacterData> load_character(uint32_t character_id) = 0;
    
    /// 根据名称加载角色
    virtual DbResult<CharacterData> load_character_by_name(const std::string& name) = 0;
    
    /// 加载账号下的所有角色
    virtual DbResult<std::vector<CharacterData>> load_characters_by_account(const std::string& account_id) = 0;
    
    /// 根据ID删除角色
    virtual DbResult<void> delete_character(uint32_t character_id) = 0;
    
    /// 检查角色名是否已存在
    virtual DbResult<bool> character_name_exists(const std::string& name) = 0;
    
    /// 获取下一个可用的角色ID
    virtual DbResult<uint32_t> get_next_character_id() = 0;

    // --- 事务操作 (Transaction Operations) ---

    /// 开始事务
    virtual DbResult<void> begin_transaction() { return DbResult<void>::ok(); }

    /// 提交事务
    virtual DbResult<void> commit() { return DbResult<void>::ok(); }

    /// 回滚事务
    virtual DbResult<void> rollback() { return DbResult<void>::ok(); }

    // --- 批量保存操作 (Batch Save Operations) ---

    /// 保存装备数据
    virtual DbResult<void> save_equipment(uint32_t /*char_id*/,
        const std::vector<mir2::db::EquipmentSlotData>& /*equip*/) {
        return DbResult<void>::ok();
    }

    /// 保存背包数据
    virtual DbResult<void> save_inventory(uint32_t /*char_id*/,
        const std::vector<mir2::db::InventorySlotData>& /*inv*/) {
        return DbResult<void>::ok();
    }

    /// 保存技能数据
    virtual DbResult<void> save_skills(uint32_t /*char_id*/,
        const std::vector<mir2::db::CharacterSkillData>& /*skills*/) {
        return DbResult<void>::ok();
    }

    // --- 账号操作 (Account Operations) ---

    /// 加载账号数据
    virtual DbResult<mir2::db::AccountData> load_account(const std::string& /*username*/) {
        return DbResult<mir2::db::AccountData>::error(ErrorCode::NOT_IMPLEMENTED, "Not implemented");
    }

    /// 创建账号
    virtual DbResult<void> create_account(const mir2::db::AccountData& /*account*/) {
        return DbResult<void>::error(ErrorCode::NOT_IMPLEMENTED, "Not implemented");
    }

    // --- ID生成 (ID Generation) ---

    /// 生成唯一ID
    virtual DbResult<uint64_t> generate_id(const std::string& /*seq_name*/) {
        return DbResult<uint64_t>::error(ErrorCode::NOT_IMPLEMENTED, "Not implemented");
    }
};

/**
 * @brief SQLite数据库实现
 * 
 * 使用SQLite作为后端的数据库实现。
 * 支持文件数据库和内存数据库（使用":memory:"路径）。
 */
class SQLiteDatabase : public IDatabase {
public:
    /**
     * @brief 构造函数
     * @param db_path 数据库文件路径（使用":memory:"创建内存数据库）
     */
    explicit SQLiteDatabase(const std::string& db_path);
    ~SQLiteDatabase() override;
    
    // 不可复制
    SQLiteDatabase(const SQLiteDatabase&) = delete;
    SQLiteDatabase& operator=(const SQLiteDatabase&) = delete;
    
    // 可移动
    SQLiteDatabase(SQLiteDatabase&& other) noexcept;
    SQLiteDatabase& operator=(SQLiteDatabase&& other) noexcept;
    
    // IDatabase接口实现
    DbResult<void> initialize() override;
    void close() override;
    bool is_open() const override;
    
    DbResult<void> save_character(const CharacterData& data) override;
    DbResult<CharacterData> load_character(uint32_t character_id) override;
    DbResult<CharacterData> load_character_by_name(const std::string& name) override;
    DbResult<std::vector<CharacterData>> load_characters_by_account(const std::string& account_id) override;
    DbResult<void> delete_character(uint32_t character_id) override;
    DbResult<bool> character_name_exists(const std::string& name) override;
    DbResult<uint32_t> get_next_character_id() override;
    
    /// 获取数据库文件路径
    const std::string& get_path() const { return db_path_; }
    
    /// 执行原始SQL（用于测试/管理）
    DbResult<void> execute(const std::string& sql);
    
private:
    std::string db_path_;      ///< 数据库文件路径
    sqlite3* db_ = nullptr;    ///< SQLite数据库句柄
    
    /// 创建数据库表
    DbResult<void> create_tables();
    
    /// 准备SQL语句
    sqlite3_stmt* prepare(const std::string& sql);
    
    /// 释放预处理语句
    void finalize(sqlite3_stmt* stmt);
    
    /// 获取最后的错误信息
    std::string get_error_message() const;
    
    /// 将CharacterData绑定到预处理语句
    bool bind_character_data(sqlite3_stmt* stmt, const CharacterData& data);
    
    /// 从结果行读取CharacterData
    CharacterData read_character_row(sqlite3_stmt* stmt);
};

/**
 * @brief 内存数据库（用于测试）
 * 
 * 封装SQLiteDatabase，使用":memory:"创建内存数据库。
 */
class InMemoryDatabase : public SQLiteDatabase {
public:
    InMemoryDatabase() : SQLiteDatabase(":memory:") {}
};

} // namespace mir2::common

#endif // LEGEND2_DATABASE_H
