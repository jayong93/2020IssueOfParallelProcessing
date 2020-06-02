#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>
#include "util.h"
#include "skiplist.h"

constexpr unsigned MAX_THREAD = 64;
constexpr unsigned NUM_TEST = 4'000'000;

using namespace std;

void benchMark(HTMSkiplist &skiplist, int num_thread) {
    for (int i = 1; i <= NUM_TEST / num_thread; ++i) {
        switch (fast_rand() % 3) {
        case 0:
            skiplist.insert(fast_rand(), fast_rand());
            break;
        case 1:
            skiplist.remove(fast_rand());
            break;
        default:
            skiplist.find(fast_rand());
            break;
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "you have to give a thread num\n");
        exit(-1);
    }
    unsigned num_thread = atoi(argv[1]);
    if (MAX_THREAD < num_thread) {
        fprintf(stderr, "the upper limit of a number of thread is %d\n",
                MAX_THREAD);
        exit(-1);
    }

    HTMSkiplist skiplist;

    vector<thread> worker;
    auto start_t = chrono::high_resolution_clock::now();
    for (int i = 0; i < num_thread; ++i)
        worker.emplace_back(benchMark, ref(skiplist), num_thread);
    for (auto &th : worker)
        th.join();
    auto du = chrono::high_resolution_clock::now() - start_t;

    cout << num_thread << " Threads,  Time = ";
    cout << chrono::duration_cast<chrono::milliseconds>(du).count() << " ms"
         << endl;
}
