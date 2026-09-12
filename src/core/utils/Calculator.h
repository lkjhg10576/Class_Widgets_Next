#pragma once

#include <QDate>
#include <QString>

// 对应上游 src/core/utils/calculator.py —— 周次计算器。
//
// 注意（避免重复实现的说明）：上游 calculator.py 并非四则表达式求值器，而是
// get_week_number / get_cycle_week 两个周次纯函数，且课程表域已在 M2 把同款
// 逻辑并入 src/core/schedule/ScheduleModel.cpp 的 weekNumber()/cycleWeek()
// （ScheduleModel.h:44-49）。本文件是工具层的独立同名移植，实现与
// ScheduleModel 保持逐行一致（供未来非课程表域复用；上游 calculator.py 同样
// 只经 utils/__init__.py:6 再导出，无直接 QML 调用面）。
namespace Calculator {

// calculator.py:4-17 get_week_number：开学后的第几周。
// 开学前按周继续向前编号，不产生 0 周（前 1~7 天 → -1，前 8~14 天 → -2）。
// startDate 为 "yyyy-MM-dd"（strptime("%Y-%m-%d")）；解析失败回退第 1 周
// （与 ScheduleModel.cpp:21-23 一致，对应 service.py:184-186 的兜底语义）。
int getWeekNumber(const QString &startDate, const QDate &currentDate);

// calculator.py:20-32 get_cycle_week：当前周在当前周期的第几周。
// 正周次 1,2,3... 正常循环；负周次按 Python 取模语义向前循环
// （cycle=2 时：-1 → 2 双周，-2 → 1 单周）。cycle 下限保护为 1。
int getCycleWeek(int weekNumber, int cycle);

} // namespace Calculator
