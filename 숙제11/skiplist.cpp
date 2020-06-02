#include "skiplist.h"
#include <atomic>
#include <immintrin.h>

using namespace std;

constexpr unsigned MAX_TRY = 15;
constexpr unsigned MAX_TX_TRY = 30;

unique_lock<mutex> try_to_start_tx(mutex &lock, bool &is_force_aborted) {
    unique_lock<mutex> lg;
    auto try_count = 0;
    while (true) {
        if (try_count >= MAX_TX_TRY) {
            lg = unique_lock{lock};
            break;
        }

        auto status = _xbegin();
        if (status == _XBEGIN_STARTED) {
            break;
        }
        else if (status & 0x2 != 0) {
            fprintf(stderr, "Closed to success\n");
        } else if (status & 0x4 != 0) {
            fprintf(stderr, "Conflict\n");
        } else if (status & 0x8 != 0) {
            fprintf(stderr, "Capacity\n");
        } else if (status & 0x10 != 0) {
            fprintf(stderr, "Debug Assertion\n");
        } else if (status & 0x20 != 0) {
            fprintf(stderr, "Nested Transaction\n");
        } else if (status & 0x1 != 0) {
            fprintf(stderr, "Aborted\n");
            if (_XABORT_CODE(status) == 0xaa) {
                fprintf(stderr, "=> Force Aborted\n");
                is_force_aborted = true;
                break;
            }
        } else {
            fprintf(stderr, "Unknown abort : %d\n", status);
        }

        try_count++;
    }
    return lg;
}

bool HTMSkiplist::insert(long key, long value) {
    SKNode *node = new SKNode{key, value};
    for (int i = 0; i < MAX_TRY; ++i) {
        switch (this->insert_htm(*node)) {
        case Success:
            return true;
        case Fail:
            delete node;
            return false;
        default:
            break;
        }
    }

    auto result = insert_seq(*node);
    if (result == false)
        delete node;
    return result;
}

bool HTMSkiplist::remove(long key) {
    for (int i = 0; i < MAX_TRY; ++i) {
        switch (this->remove_htm(key)) {
        case Success:
            return true;
        case Fail:
            return false;
        default:
            break;
        }
    }

    return remove_seq(key);
}

optional<long> HTMSkiplist::find(long key) {
    SKNode *curr, *prev;
    prev = this->head;
    for (int i = MAX_HEIGHT - 1; i >= 0; --i) {
        curr = prev->next[i].load(memory_order_relaxed);
        while (key > curr->key) {
            prev = curr;
            curr = prev->next[i].load(memory_order_relaxed);
        }
        if (key == curr->key)
            break;
    }
    if (key == curr->key) {
        while (curr->state.load(memory_order_relaxed) == INITIAL)
            ;
        if (curr->state.load(memory_order_relaxed) == REMOVED)
            return nullopt;
        return curr->value;
    } else
        return nullopt;
}

HTMResult HTMSkiplist::insert_htm(SKNode &node) {
    SKNode *preds[MAX_HEIGHT];
    SKNode *succs[MAX_HEIGHT];
    SKNode *curr;
    SKNode *pred;

    long key = node.key;

    pred = this->head;
    for (int h = MAX_HEIGHT - 1; h >= 0; h--) {
        curr = pred->next[h].load(memory_order_relaxed);
        while (key > curr->key) {
            pred = curr;
            curr = pred->next[h].load(memory_order_relaxed);
        }
        preds[h] = pred;
        succs[h] = curr;
    }
    if (key == curr->key) {
        while (curr->state == INITIAL)
            ;
        if (curr->state == REMOVED)
            return HTMResult::HTMAbort;
        return HTMResult::Fail; // if node exists , we return
    }

    bool is_force_aborted = false;
    unique_lock<mutex> lg = try_to_start_tx(this->lock, is_force_aborted);
    if (is_force_aborted)
        return HTMResult::HTMAbort;

    auto nodeHeight = node.height;
    // check consistency
    for (int h = 0; h < nodeHeight; h++) {
        if (preds[h]->next[h].load(memory_order_relaxed) != succs[h] ||
            preds[h]->state == REMOVED || succs[h]->state == REMOVED) {
            // force an abort
            if (_xtest())
                _xabort(0xaa);
            else {
                return HTMResult::HTMAbort;
            }
        }
    }

    // update fields
    for (int h = 0; h < nodeHeight; h++) {
        node.next[h].store(succs[h], memory_order_relaxed);
    }

    for (int h = 0; h < nodeHeight; h++) {
        preds[h]->next[h].store(&node, memory_order_relaxed);
    }

    node.state.store(INSERTED, memory_order_release);
    // commit
    if (_xtest())
        _xend();
    return HTMResult::Success;
}

bool HTMSkiplist::insert_seq(SKNode &node) {
    unique_lock<mutex> lg{this->lock};

    SKNode *updateArr[MAX_HEIGHT];
    SKNode *curr = this->head;
    long key = node.key;
    unsigned nodeHeight = node.height;

    for (int h = MAX_HEIGHT - 1; h >= 0; h--) {
        auto cmpKey = curr->next[h].load(memory_order_relaxed)->key;
        while (cmpKey < key) {
            curr = curr->next[h].load(memory_order_relaxed);
            cmpKey = (curr->next[h].load(memory_order_relaxed))->key;
        }
        updateArr[h] = curr;
    }

    if (curr->next[0].load(memory_order_relaxed)->key == key)
        return 0;

    for (int h = 0; h < nodeHeight; h++) {
        node.next[h].store(updateArr[h]->next[h].load(memory_order_relaxed),
                           memory_order_relaxed);
        updateArr[h]->next[h].store(&node, memory_order_relaxed);
    }

    node.state.store(INSERTED, memory_order_release);
    return 1;
}

HTMResult HTMSkiplist::remove_htm(long key) {
    SKNode *preds[MAX_HEIGHT];
    SKNode *curr;
    SKNode *pred;

    // find where the node is
    pred = this->head;
    for (int h = MAX_HEIGHT - 1; h >= 0; h--) {
        curr = pred->next[h].load(memory_order_relaxed);
        while (key > curr->key) {
            pred = curr;
            curr = pred->next[h].load(memory_order_relaxed);
        }
        preds[h] = pred;
    }

    bool is_force_aborted = false;
    unique_lock<mutex> lg = try_to_start_tx(this->lock, is_force_aborted);
    if (is_force_aborted)
        return HTMResult::HTMAbort;

    // begin transaction
    if (curr->key == key) {
        auto nodeHeight = curr->height;
        // check consistency
        for (int h = 0; h < nodeHeight; h++) {
            if (preds[h]->next[h].load(memory_order_relaxed) != curr ||
                preds[h]->state == REMOVED) {
                // force an abort
                if (_xtest())
                    _xabort(0xaa);
                else {
                    return HTMResult::HTMAbort;
                }
            }
        }

        // update fields
        curr->state.store(REMOVED, memory_order_relaxed);
        for (int h = 0; h < nodeHeight; h++) {
            preds[h]->next[h].store(curr->next[h].load(memory_order_relaxed),
                                    memory_order_relaxed);
        }

        // commit
        if (_xtest())
            _xend();
        return HTMResult::Success;
    } else {
        if (_xtest())
            _xend();
        return HTMResult::Fail;
    }
}

bool HTMSkiplist::remove_seq(long key) {
    unique_lock<mutex> lg{this->lock};

    SKNode *curr = this->head;
    SKNode *updateArr[MAX_HEIGHT];

    // find where the node is
    for (int h = MAX_HEIGHT - 1; h >= 0; h--) {
        auto cmpKey = curr->next[h].load(memory_order_relaxed)->key;
        while (cmpKey < key) {
            curr = curr->next[h].load(memory_order_relaxed);
            cmpKey = (curr->next[h].load(memory_order_relaxed))->key;
        }
        updateArr[h] = curr;
    }

    curr = curr->next[0].load(memory_order_relaxed);
    if (curr->key == key) {
        auto nodeHeight = curr->height;
        // update fields
        curr->state = REMOVED;
        for (int h = 0; h < nodeHeight; h++) {
            updateArr[h]->next[h].store(
                curr->next[h].load(memory_order_relaxed), memory_order_relaxed);
        }
        return true;
    } else {
        return false;
    }
}
