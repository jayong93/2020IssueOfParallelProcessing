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
#include <stack>
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

thread_local unsigned exSize = 1;
constexpr unsigned MAX_THREAD = 64;

static unsigned NODE_NUM = numa_num_configured_nodes();
static unsigned CPU_NUM = numa_num_configured_cpus();
static atomic_uint tid_counter{0};
static thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
unsigned get_node_id(unsigned tid)
{
	auto core_per_node = CPU_NUM / NODE_NUM;
	return (tid / core_per_node) % NODE_NUM;
}

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
	vector<Exchanger> exchanger;

public:
	EliminationArray(unsigned exchanger_num) : exchanger{exchanger_num} {}

	int visit(int x)
	{
		if (exchanger.size() < exSize)
		{
			exSize = exchanger.size();
		}
		int index = rand() % exSize;
		return exchanger[index].exchange(x);
	}

	void shrink()
	{
		if (exSize > 1)
			exSize -= 1;
	}
};

struct Slot
{
	unsigned op;				   // 0 == push, 1 == pop
	unsigned value;				   // push일 때 넣을 값, pop일 때 받은 값
	atomic_bool is_finished{true}; // 완료되었을 때 true로 바뀜.
};
class SlotArray
{
public:
	SlotArray(unsigned entry_num) : entries{entry_num}, el_array{CPU_NUM / NODE_NUM}
	{
		for (auto &entry : entries)
		{
			entry.reset(new Slot);
		}
	}

	// 일반 thread가 호출할 methods
	void push(int value, unsigned idx)
	{
		auto &entry = entries[idx];
		entry->value = value;
		entry->op = 0;
		entry->is_finished.store(false, memory_order_release);
		while (entry->is_finished.load(memory_order_acquire) == false)
			;
	}
	int pop(unsigned idx)
	{
		auto &entry = entries[idx];
		entry->op = 1;
		entry->is_finished.store(false, memory_order_release);
		while (entry->is_finished.load(memory_order_acquire) == false)
			;
		return entry->value;
	}

	// helper thread가 호출할 methods
	void process_ops(stack<int> &stack)
	{
		for (auto &op : entries)
		{
			if (op->is_finished.load(memory_order_acquire) == true)
				continue;

			if (op->op == 0)
			{
				stack.push(op->value);
			}
			else if (stack.empty() == false)
			{
				op->value = stack.top();
				stack.pop();
			}
			else
			{
				op->value = -1;
			}
			op->is_finished.store(true, memory_order_release);
		}
	}

private:
	vector<unique_ptr<Slot>> entries;
	EliminationArray el_array;
};

void global_helper_func(shared_ptr<vector<unique_ptr<SlotArray, DeallocNUMA<SlotArray>>>> arr, shared_ptr<stack<int>> stack)
{
	while (true)
	{
		for (auto &arr_ptr : *arr)
		{
		}
	}
}

// Elimination + Delegation
class EDStack
{
public:
	EDStack(unsigned cores_per_node, unsigned nodes_num) : cores_per_node{cores_per_node},
														   per_node_arrays{make_shared<vector<unique_ptr<SlotArray, DeallocNUMA<SlotArray>>>>(nodes_num)},
														   inner_stack{make_shared<stack<int>>()}
	{
		auto idx = 0;
		for (auto &arr : *per_node_arrays)
		{
			arr = unique_ptr<SlotArray, DeallocNUMA<SlotArray>>{NUMA_alloc<SlotArray>(idx++, cores_per_node), DeallocNUMA<SlotArray>{}};
		}
		global_helper = thread{global_helper_func, per_node_arrays, inner_stack};
		global_helper.detach();
	}

	void push(int value)
	{
		const auto node_id = get_node_id(tid);
		(*per_node_arrays)[node_id]->push(value, tid % cores_per_node);
	}
	int pop()
	{
		const auto node_id = get_node_id(tid);
		return (*per_node_arrays)[node_id]->pop(tid % cores_per_node);
	}
	void dump(unsigned num)
	{
		for (auto i = 0; i < num; ++i)
		{
			if (inner_stack->empty())
				break;
			fprintf(stderr, "%d, ", inner_stack->top());
			inner_stack->pop();
		}
		fprintf(stderr, "\n");
	}

private:
	const unsigned cores_per_node;
	shared_ptr<vector<unique_ptr<SlotArray, DeallocNUMA<SlotArray>>>> per_node_arrays;
	thread global_helper;
	shared_ptr<stack<int>> inner_stack;
};

// Lock-Free Elimination BackOff Stack

void benchMark(EDStack &myStack, int num_thread)
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
			myStack.push(i);
		}
		else
		{
			myStack.pop();
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

	EDStack myStack{max(CPU_NUM, num_thread) / NODE_NUM, NODE_NUM};

	vector<thread> worker;
	auto start_t = chrono::high_resolution_clock::now();
	for (int i = 0; i < num_thread; ++i)
		worker.emplace_back(benchMark, ref(myStack), num_thread);
	for (auto &th : worker)
		th.join();
	auto du = chrono::high_resolution_clock::now() - start_t;

	myStack.dump(10);

	cout << num_thread << " Threads,  Time = ";
	cout << chrono::duration_cast<chrono::milliseconds>(du).count() << " ms" << endl;
}