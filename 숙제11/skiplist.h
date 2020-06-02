#ifndef BF25D0C7_93DA_4876_83E5_19532232222B
#define BF25D0C7_93DA_4876_83E5_19532232222B

#include "util.h"
#include <array>
#include <atomic>
#include <climits>
#include <mutex>
#include <optional>

using std::optional;

enum SKNodeState { INITIAL, INSERTED, REMOVED };

constexpr unsigned MAX_HEIGHT = 10;

struct SKNode {
    long key, value;
    unsigned height;
    volatile SKNodeState state;
    volatile SKNode *next[MAX_HEIGHT];

    SKNode(long key, long value) : SKNode{key, value, 1} {
        for (int i = 0; i < MAX_HEIGHT - 1; ++i) {
            if (fast_rand() % 100 < 50)
                height++;
            else
                break;
        }
    }
    SKNode(long key, long value, unsigned height)
        : next{}, state{INITIAL}, height{height}, key{key}, value{value} {
        for (auto i = 0; i < MAX_HEIGHT; ++i)
            next[i] = nullptr;
    }
};

enum HTMResult { Success, Fail, HTMAbort };

class HTMSkiplist {
  public:
    HTMSkiplist()
        : head{new SKNode{LONG_MIN, LONG_MIN, MAX_HEIGHT}},
          tail{new SKNode{LONG_MAX, LONG_MAX, MAX_HEIGHT}} {
        for (auto i = 0; i < MAX_HEIGHT; ++i)
            head->next[i] = tail;
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
