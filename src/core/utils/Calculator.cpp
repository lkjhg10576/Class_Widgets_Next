#include "Calculator.h"

namespace Calculator {

int getWeekNumber(const QString &startDate, const QDate &currentDate)
{
    // calculator.py:11 strptime(start_date, "%Y-%m-%d")
    const QDate start = QDate::fromString(startDate, QStringLiteral("yyyy-MM-dd"));
    if (!start.isValid() || !currentDate.isValid())
        return 1; // 解析失败兜底（同 ScheduleModel.cpp:21-23 / service.py:184-186）

    // calculator.py:12 delta_days = (current_date.date() - start.date()).days
    const qint64 deltaDays = start.daysTo(currentDate);
    if (deltaDays >= 0) {
        // calculator.py:14 delta_days // 7 + 1
        return static_cast<int>(deltaDays / 7) + 1;
    }
    // calculator.py:17 开学前：-(((-delta_days) + 6) // 7)，不产生 0 周
    return -static_cast<int>(((-deltaDays) + 6) / 7);
}

int getCycleWeek(int weekNumber, int cycle)
{
    // calculator.py:20-32；cycle 下限保护（避免除零，同 ScheduleModel.cpp:35-37）
    if (cycle < 1)
        cycle = 1;
    if (weekNumber >= 1) {
        // calculator.py:29 ((week_number - 1) % cycle) + 1
        return ((weekNumber - 1) % cycle) + 1;
    }
    // calculator.py:32 负周次：Python 的 % 对负数取非负余数（-1 % 2 == 1），
    // C++ 的 % 结果符号跟被除数，显式归一化
    const int pythonMod = ((weekNumber % cycle) + cycle) % cycle;
    return pythonMod + 1;
}

} // namespace Calculator
