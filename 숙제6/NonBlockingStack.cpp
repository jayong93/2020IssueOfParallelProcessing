#include <iostream>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <iterator>
#include <chrono>
#include <memory>
#include <numa.h>
#include "numa_util.h"

using namespace std;

static constexpr int NUM_TEST = 10000000;
static constexpr int RANGE = 1000;

unsigned long fast_rand(void)
{ //period 2^96-1
    static thread_local unsigned long x = 123456789, y = 362436069, z = 521288629;
    unsigned long t;
    x ^= x << 16;
    x ^= x >> 5;
    x ^= x << 1;

    t = x;
    x = y;
    y = z;
    z = t ^ x ^ y;

    return z;
}

struct Node
{
public:
	int key;
	atomic<Node *> next;

	Node() : next{nullptr} {}
	Node(int key) : key{key}, next{nullptr} {}
	~Node() {}
};

bool CAS(atomic<Node *> &ptr, Node *old_value, Node *new_value)
{
	return ptr.compare_exchange_strong(old_value, new_value);
}

thread_local int exSize = 1; // thread 별로 교환자 크기를 따로 관리.
constexpr int MAX_THREAD = 64;

class Exchanger
{
	volatile int value; // status와 교환값의 합성.

	enum Status
	{
		EMPTY,
		WAIT,
		BUSY
	};
	bool CAS(int oldValue, int newValue, Status oldStatus, Status newStatus)
	{
		int oldV = oldValue << 2 | (int)oldStatus;
		int newV = newValue << 2 | (int)newStatus;
		return atomic_compare_exchange_strong(reinterpret_cast<atomic_int volatile *>(&value), &oldV, newV);
	}

public:
	int exchange(int x)
	{
		while (true)
		{
			switch (Status(value & 0x3))
			{
			case EMPTY:
			{
				int tempVal = value >> 2;
				if (false == CAS(tempVal, x, EMPTY, WAIT))
					continue;

				/* BUSY가 될 때까지 기다리며 timeout된 경우 -1 반환 */
				int count;
				for (count = 0; count < 100; ++count)
				{
					if (Status(value & 0x3) == BUSY)
					{
						int ret = value >> 2;
						value = EMPTY;
						return ret;
					}
				}
				if (false == CAS(tempVal, 0, WAIT, EMPTY))
				{ // 그 사이에 누가 들어온 경우
					int ret = value >> 2;
					value = EMPTY;
					return ret;
				}
				return -1;
			}
			break;
			case WAIT:
			{
				int temp = value >> 2;
				if (false == CAS(temp, x, WAIT, BUSY))
					break;
				return temp;
			}
			break;
			case BUSY:
				if (exSize < MAX_THREAD)
				{
					exSize += 1;
				}
				return x;
			default:
				fprintf(stderr, "It's impossible case\n");
				exit(1);
			}
		}
	}
};

class EliminationArray
{
	Exchanger exchanger[MAX_THREAD];

public:
	int visit(int x)
	{
		int index = fast_rand() % exSize;
		return exchanger[index].exchange(x);
	}

	void shrink()
	{
		if (exSize > 1)
			exSize -= 1;
	}
};

static unsigned NODE_NUM = numa_num_configured_nodes();
static unsigned CPU_NUM = numa_num_configured_cpus();
static atomic_uint tid_counter{0};
static thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
unsigned get_node_id(unsigned tid)
{
	auto core_per_node = CPU_NUM / NODE_NUM;
	return (tid / core_per_node) % NODE_NUM;
}

// Lock-Free Elimination BackOff Stack
class LFEBOStack
{
	atomic<Node *> top;
	vector<EliminationArray*> eliminationArray;

public:
	LFEBOStack() : top{nullptr}, eliminationArray{NODE_NUM}
	{
		for (auto i = 0; i < eliminationArray.size(); ++i)
		{
			eliminationArray[i] = NUMA_alloc<EliminationArray>(i);
		}
	}
	~LFEBOStack() {
		for(auto ptr : eliminationArray) {
			NUMA_dealloc(ptr);
		}
	}

	void Push(int x)
	{
		auto node_id = get_node_id(tid);
		auto e = new Node{x};
		while (true)
		{
			auto head = top.load(memory_order_relaxed);
			e->next = head;
			if (head != top.load(memory_order_relaxed))
				continue;
			if (true == CAS(top, head, e))
				return;
			int result = eliminationArray[node_id]->visit(x);
			if (0 == result)
				break; // pop과 교환됨.
			if (-1 == result)
				eliminationArray[node_id]->shrink(); // timeout 됨.
		}
	}

	int Pop()
	{
		auto node_id = get_node_id(tid);
		while (true)
		{
			auto head = top.load(memory_order_relaxed);
			if (nullptr == head)
				return 0;
			if (head != top.load(memory_order_relaxed))
				continue;
			if (true == CAS(top, head, head->next))
				return head->key;
			int result = eliminationArray[node_id]->visit(0);
			if (0 == result)
				continue; // pop끼리 교환되면 계속 시도
			if (-1 == result)
				eliminationArray[node_id]->shrink(); // timeout 됨.
			else
				return result;
		}
	}

	void clear()
	{
		auto old_top = top.load(memory_order_relaxed);
		if (nullptr == old_top)
			return;
		while (old_top->next != nullptr)
		{
			Node *tmp = old_top;
			top.store(old_top->next);
			delete tmp;
		}
		delete top;
		top = nullptr;
	}

	void dump(size_t count)
	{
		auto ptr = top.load(memory_order_relaxed);
		cout << count << " Result : ";
		for (auto i = 0; i < count; ++i)
		{
			if (nullptr == ptr)
				break;
			cout << ptr->key << ", ";
			ptr = ptr->next.load(memory_order_relaxed);
		}
		cout << "\n";
	}
} myStack;

void benchMark(int num_thread)
{
	if (-1 == numa_run_on_node(get_node_id(tid)))
	{
		fprintf(stderr, "Can't pin thread #%d to NUMA node #%d\n", tid, get_node_id(tid));
		return;
	}
	for (int i = 1; i <= NUM_TEST / num_thread; ++i)
	{
		if ((fast_rand() % 2) || i <= 1000 / num_thread)
		{
			myStack.Push(i);
		}
		else
		{
			myStack.Pop();
		}
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		fprintf(stderr, "you have to give a thread num\n");
		exit(-1);
	}
	unsigned num_thread = atoi(argv[1]);
	if (MAX_THREAD < num_thread)
	{
		fprintf(stderr, "the upper limit of a number of thread is %d\n", MAX_THREAD);
		exit(-1);
	}

	vector<thread> worker;
	auto start_t = chrono::high_resolution_clock::now();
	for (int i = 0; i < num_thread; ++i)
		worker.emplace_back(benchMark, num_thread);
	for (auto &th : worker)
		th.join();
	auto du = chrono::high_resolution_clock::now() - start_t;

	myStack.dump(10);

	cout << num_thread << " Threads,  Time = ";
	cout << chrono::duration_cast<chrono::milliseconds>(du).count() << " ms" << endl;
}