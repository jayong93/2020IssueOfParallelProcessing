#include "skiplist.h"
#include <atomic>
#include <immintrin.h>

using namespace std;

constexpr unsigned MAX_TRY = 15;

bool HTMSkiplist::insert(long key, long value) {
    SKNode node{key, value};
    for (auto i = 0; i < MAX_TRY; ++i) {
        switch (this->insert_htm(node)) {
        case Success:
            return true;
        case Fail:
            return false;
        default:
            break;
        }
    }

    return insert_seq(node);
}

bool HTMSkiplist::remove(long key) {
    for (auto i = 0; i < MAX_TRY; ++i) {
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
    for (auto i = MAX_HEIGHT - 1; i >= 0; --i) {
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

HTMResult HTMSkiplist::insert_htm(SKNode &node) {}

bool HTMSkiplist::insert_seq(SKNode &node) {
    SKNode *updateArr[MAX_HEIGHT];
    SKNode *curr = this->head;
    int key = node.key;
    int nodeHeight = node.height;

    for (auto h = MAX_HEIGHT - 1; h >= 0; h--) {
        auto cmpKey = curr->next[h].load(memory_order_relaxed)->key;
        while (cmpKey < key) {
            curr = curr->next[h].load(memory_order_relaxed);
            cmpKey = (curr->next[h].load(memory_order_relaxed))->key;
        }
        updateArr[h] = curr;
    }

    if (curr->next[0].load(memory_order_relaxed)->key == key)
        return 0;

    for (auto h = 0; h < nodeHeight; h++) {
        node.next[h].store(updateArr[h]->next[h].load(memory_order_relaxed),
                           memory_order_relaxed);
        updateArr[h]->next[h].store(&node);
    }

    node.state.store(INSERTED, memory_order_release);
    return 1;
}

HTMResult HTMSkiplist::remove_htm(long key) {}

bool HTMSkiplist::remove_seq(long key) {
    SKNode *curr = this->head;
    SKNode *updateArr[MAX_HEIGHT];

    // find where the node is
    for (auto h = MAX_HEIGHT - 1; h >= 0; h--) {
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
        for (auto h = 0; h < nodeHeight; h++) {
            updateArr[h]->next[h].store(
                curr->next[h].load(memory_order_relaxed), memory_order_relaxed);
        }
        return true;
    } else {
        return false;
    }
}
