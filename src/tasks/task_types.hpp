//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

#ifndef TASKS_TASK_TYPES_HPP_
#define TASKS_TASK_TYPES_HPP_

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "basic_types.hpp"

namespace parthenon {

enum class TaskType { single, iterative, completion_criteria };

class Task {
 public:
  Task(const TaskID &id, const TaskID &dep, std::function<TaskStatus()> func)
      : myid_(id), dep_(dep), type_(TaskType::single), key_(-1), func_(std::move(func)) {}
  Task(const TaskID &id, const TaskID &dep, std::function<TaskStatus()> func,
       const TaskType &type, const int key)
      : myid_(id), dep_(dep), type_(type), key_(key), func_(std::move(func)) {
    assert(key_ >= 0);
  }
  void operator()() { status_ = func_(); }
  void SetID(TaskID id) { myid_ = id; }
  TaskID GetID() const { return myid_; }
  TaskID GetDependency() const { return dep_; }
  TaskStatus GetStatus() const { return status_; }
  void SetStatus(const TaskStatus &status) { status_ = status; }
  TaskType GetType() const { return type_; }
  int GetKey() const { return key_; }

 private:
  TaskID myid_, dep_;
  const TaskType type_;
  const int key_;
  TaskStatus status_ = TaskStatus::incomplete;
  bool lb_time = false;
  std::function<TaskStatus()> func_;
};

} // namespace parthenon

#endif // TASKS_TASK_TYPES_HPP_
