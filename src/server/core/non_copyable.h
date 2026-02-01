/**
 * @file non_copyable.h
 * @brief 非拷贝基类
 *
 * 用于禁止拷贝与赋值，确保对象的唯一性语义。
 */

#ifndef MIR2_CORE_NON_COPYABLE_H
#define MIR2_CORE_NON_COPYABLE_H

namespace mir2::core {

/**
 * @brief 禁止拷贝的基类
 *
 * 继承该类可禁止对象被拷贝或赋值，常用于资源管理类。
 */
class NonCopyable {
 protected:
  NonCopyable() = default;
  ~NonCopyable() = default;

  NonCopyable(const NonCopyable&) = delete;
  NonCopyable& operator=(const NonCopyable&) = delete;
};

}  // namespace mir2::core

#endif  // MIR2_CORE_NON_COPYABLE_H
