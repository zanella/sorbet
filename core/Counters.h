#ifndef SORBET_COUNTERS_H
#define SORBET_COUNTERS_H
#include "absl/strings/string_view.h"
#include "core/Names.h"
#include <map>
#include <string>
#include <unordered_map>

namespace sorbet {
namespace core {

constexpr bool enable_counters = debug_mode;

// A common on the use of strings in this API
//
// We have the following goals for our counters API:
//
// (1) It should be very lightweight to add new counters; You should not need to
//     predeclare counters.
// (2) Counters should be cheap to use (fast).
//
// Goal (1) implies an API that takes strings directly, but comparing strings is
// slow, and goal (2) suggests we don't want to compare strings to store a
// counter. In order to meet these conflicting goals, we store strings as `const
// char*`, and and compare by pointer value.
//
// The ConstExprStr class below can only be constructed from a string literal,
// which enforces safety -- Using string literals prevents use-after-free and
// various other memory errors normally associated with raw C strings. The
// compiler will typically deduplicate string literals within a translation
// units (but not necessarily between translation units), but this can't be
// relied upon, so we canonicalize strings when retrieving statistics.

struct ConstExprStr {
    char const *str;
    const std::size_t size;

    // can only construct from a char[] literal
    template <std::size_t N>
    constexpr ConstExprStr(char const (&s)[N])
        : str(s), size(N - 1) // not count the trailing nul
    {}

    ConstExprStr() = delete;
};

struct CounterImpl;

// forward declarations for classes that need private access to the counter
// implementation
class Proto;
class StatsD;

struct CounterState {
    CounterState();
    ~CounterState();
    CounterState(CounterState &&rhs);
    CounterState &operator=(CounterState &&rhs);

    CounterState(const CounterState &rhs) = delete;

private:
    friend CounterState getAndClearThreadCounters();
    friend void counterConsume(CounterState cs);
    friend class Proto;
    friend class StatsD;

    CounterState(std::unique_ptr<CounterImpl> counters);
    std::unique_ptr<CounterImpl> counters;
};

struct Counters {
    static const std::vector<std::string> ALL_COUNTERS;
};

CounterState getAndClearThreadCounters();
void counterConsume(CounterState cs);

void prodCounterInc(ConstExprStr counter);
void prodCounterAdd(ConstExprStr counter, unsigned int value);
void counterInc(ConstExprStr counter);
void counterAdd(ConstExprStr counter, unsigned int value);
void categoryCounterInc(ConstExprStr category, ConstExprStr counter);
void categoryCounterAdd(ConstExprStr category, ConstExprStr counter, unsigned int value);
void histogramInc(ConstExprStr histogram, int key);
void histogramAdd(ConstExprStr histogram, int key, unsigned int value);
std::string getCounterStatistics(std::vector<std::string> names);
} // namespace core
} // namespace sorbet
#endif // SORBET_COUNTERS_H
