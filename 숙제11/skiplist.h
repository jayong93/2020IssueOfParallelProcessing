#ifndef BF25D0C7_93DA_4876_83E5_19532232222B
#define BF25D0C7_93DA_4876_83E5_19532232222B

#include <array>
#include <atomic>
#include <climits>
#include <optional>
#include <mutex>
#include "util.h"

using std::optional;

enum SKNodeState { INITIAL, INSERTED, REMOVED };

constexpr unsigned MAX_HEIGHT = 10;

struct SKNode {
    long key, value;
    unsigned height;
    std::atomic<SKNodeState> state;
    std::array<std::atomic<SKNode *>, MAX_HEIGHT> next;

    SKNode(long key, long value)
        : SKNode{key, value, (unsigned)fast_rand() % MAX_HEIGHT + 1} {}
    SKNode(long key, long value, unsigned height)
        : next{}, state{INITIAL}, height{height}, key{key}, value{value} {}
};

enum HTMResult { Success, Fail, HTMAbort };

class HTMSkiplist {
  public:
    HTMSkiplist()
        : head{new SKNode{LONG_MIN, LONG_MIN, MAX_HEIGHT}},
          tail{new SKNode{LONG_MAX, LONG_MAX, MAX_HEIGHT}} {
        for (auto i = 0; i < MAX_HEIGHT; ++i)
            head->next[i].store(tail, std::memory_order_relaxed);
    }
    ~HTMSkiplist() {
        delete head;
        delete tail;
    }

    bool insert(long key, long value);
    bool remove(long key);
    optional<long> find(long key);

  private:
    HTMResult insert_htm(SKNode &node);
    bool insert_seq(SKNode &node);
    HTMResult remove_htm(long key);
    bool remove_seq(long key);

    SKNode *head, *tail;
    std::mutex lock;
};

#endif /* BF25D0C7_93DA_4876_83E5_19532232222B */
