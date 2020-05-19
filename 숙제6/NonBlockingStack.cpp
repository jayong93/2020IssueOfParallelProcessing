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

constexpr int MAX_THREAD = 64;

static unsigned NODE_NUM = numa_num_configured_nodes();
static unsigned CPU_NUM = numa_num_configured_cpus();
static atomic_uint tid_counter{0};
static thread_local unsigned tid = tid_counter.fetch_add(1, memory_order_relaxed);
unsigned get_node_id(unsigned tid)
{
	auto core_per_node = CPU_NUM / NODE_NUM;
	return (tid / core_per_node) % NODE_NUM;
}

struct Exchanger
{
	unsigned op;				   // 0 == push, 1 == pop
	unsigned value;				   // push일 때 넣을 값, pop일 때 받은 값
	atomic_bool is_finished{true}; // 완료되었을 때 true로 바뀜.
};
class EliminationArray
{
public:
	EliminationArray(unsigned entry_num) : entries{entry_num}
	{
		for (auto &entry : entries)
		{
			entry.reset(new Exchanger);
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
	vector<Exchanger *> process_ops()
	{
		vector<Exchanger *> result;
		vector<Exchanger *> pushes;
		vector<Exchanger *> pops;
		for (auto &entry : entries)
		{
			if (entry->op == 0)
			{
				if (pops.empty())
					pushes.emplace_back(entry.get());
				else
				{
					auto &pop_entry = *pops.back();
					pop_entry.value = entry->value;
					entry->is_finished.store(true, memory_order_release);
					pop_entry.is_finished.store(true, memory_order_release);
					pops.pop_back();
				}
			}
			else
			{
				if (pushes.empty())
					pops.emplace_back(entry.get());
				else
				{
					auto &push_entry = *pushes.back();
					push_entry.value = entry->value;
					entry->is_finished.store(true, memory_order_release);
					push_entry.is_finished.store(true, memory_order_release);
					pushes.pop_back();
				}
			}
		}
		for (auto entry : pushes)
			result.emplace_back(entry);
		for (auto entry : pops)
			result.emplace_back(entry);
		return result;
	}

private:
	vector<unique_ptr<Exchanger>> entries;
};

void global_helper_func(shared_ptr<vector<unique_ptr<EliminationArray, DeallocNUMA<EliminationArray>>>> arr, shared_ptr<stack<int>> stack)
{
	while (true)
	{
		for (auto &arr_ptr : *arr)
		{
			auto remain_ops = arr_ptr->process_ops();
			for (auto &op : remain_ops)
			{
				if (op->is_finished.load(memory_order_acquire) == true)
					continue;

				if (op->op == 0)
				{
					stack->push(op->value);
				}
				else if (stack->empty() == false)
				{
					op->value = stack->top();
					stack->pop();
				} else {
					op->value = -1;
				}
				op->is_finished.store(true, memory_order_release);
			}
		}
	}
}

// Elimination + Delegation
class EDStack
{
public:
	EDStack(unsigned cores_per_node, unsigned nodes_num) : cores_per_node{cores_per_node},
														   per_node_arrays{make_shared<vector<unique_ptr<EliminationArray, DeallocNUMA<EliminationArray>>>>(nodes_num)},
														   inner_stack{make_shared<stack<int>>()}
	{
		auto idx = 0;
		for (auto &arr : *per_node_arrays)
		{
			arr = unique_ptr<EliminationArray, DeallocNUMA<EliminationArray>>{NUMA_alloc<EliminationArray>(idx++, cores_per_node), DeallocNUMA<EliminationArray>{}};
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
	shared_ptr<vector<unique_ptr<EliminationArray, DeallocNUMA<EliminationArray>>>> per_node_arrays;
	thread global_helper;
	shared_ptr<stack<int>> inner_stack;
};

// Lock-Free Elimination BackOff Stack

void benchMark(EDStack& myStack, int num_thread)
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

	EDStack myStack{num_thread/NODE_NUM, NODE_NUM};

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