/**
 * @file singleton.h
 * @brief 单例模板
 *
 * 提供线程安全的局部静态单例实现。
 */

#ifndef MIR2_CORE_SINGLETON_H
#define MIR2_CORE_SINGLETON_H

namespace mir2::core {

/**
 * @brief 单例模板
 * @tparam T 单例类型
 */
template <typename T>
class Singleton {
 public:
  /**
   * @brief 获取单例实例
   * @return 单例引用
   */
  static T& Instance() {
    static T instance;
    return instance;
  }

 protected:
  Singleton() = default;
  ~Singleton() = default;

  Singleton(const Singleton&) = delete;
  Singleton& operator=(const Singleton&) = delete;
};

}  // namespace mir2::core

#endif  // MIR2_CORE_SINGLETON_H
