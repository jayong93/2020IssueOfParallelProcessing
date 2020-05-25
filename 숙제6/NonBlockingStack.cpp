#include <iostream>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>
#include <atomic>
#include <algorithm>
#include <iterator>
#include <optional>
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
	auto core_per_node = (CPU_NUM / 2) / NODE_NUM;
	return (tid / core_per_node) % NODE_NUM;
}

constexpr unsigned POP_WAIT_TIME = 100;

struct EliminationSlot
{
	atomic<int *> value = EMPTY_VALUE;
	EliminationSlot *next = nullptr;

	static int *const EMPTY_VALUE;

	~EliminationSlot()
	{
		if (value != EMPTY_VALUE && value != nullptr)
			delete value;
	}
};

int *const EliminationSlot::EMPTY_VALUE = new int{0};

class EliminationArray
{
public:
	EliminationArray(unsigned num_entry) : num_entry{num_entry}
	{
		for (auto i = 0; i < num_entry; ++i)
			entries.emplace_back(new EliminationSlot);
		for (auto i = 0; i < num_entry; ++i)
		{
			entries[i]->next = entries[(i + 1) % num_entry].get();
		}
	}

	bool push(int *value, unsigned idx)
	{
		auto my_slot = entries[idx].get();
		auto next_slot = my_slot->next;

		for (auto try_count = 0; try_count < entries.size()*2; ++try_count)
		{
			auto my_old_value = my_slot->value.load(memory_order_relaxed);
			if (my_old_value == nullptr)
			{
				if (my_slot->value.compare_exchange_strong(my_old_value, value))
					return true;
			}
			else
			{
				auto next_old_value = next_slot->value.load(memory_order_relaxed);
				if (next_old_value == nullptr)
				{
					if (next_slot->value.compare_exchange_strong(next_old_value, value))
						return true;
					next_slot = next_slot->next;
				}
			}
		}

		return false;
	}

	optional<int> pop(unsigned idx)
	{
		auto my_slot = entries[idx].get();

		while (true)
		{
			auto free_ptr = EliminationSlot::EMPTY_VALUE;
			if (false == my_slot->value.compare_exchange_strong(free_ptr, nullptr))
				my_slot = my_slot->next;
			else
				break;
		}

		for (volatile auto i = 0; i < POP_WAIT_TIME; ++i)
			;

		auto val = my_slot->value.load(memory_order_relaxed);
		if (val != nullptr ||
			false == my_slot->value.compare_exchange_strong(val, EliminationSlot::EMPTY_VALUE))
		{
			auto ret_val = *val;
			my_slot->value.store(EliminationSlot::EMPTY_VALUE, memory_order_relaxed);
			delete val;
			return ret_val;
		}

		return nullopt;
	}

private:
	vector<unique_ptr<EliminationSlot>> entries;
	const unsigned num_entry;
};

struct Slot
{
	unsigned op;		  // 0 == push, 1 == pop
	int *value = nullptr; // push일 때 넣을 값, pop일 때 받은 값
};
class SlotArray
{
public:
	SlotArray(unsigned entry_num) : entries{entry_num}, el_array{entry_num}
	{
		for(auto& entry : entries)
		{
			entry.reset(new atomic<Slot*>);
		}
	}

	// 일반 thread가 호출할 methods
	void push(int value, unsigned idx)
	{
		int *value_ptr = new int{value};

		Slot *old_entry = nullptr;
		auto my_slot = new Slot;
		my_slot->value = value_ptr;
		my_slot->op = 0;

		auto &entry = entries[idx];
		while (true)
		{
			if (el_array.push(value_ptr, idx))
				return;

			if (entry->load(memory_order_relaxed) != nullptr)
				continue;
			if (false == entry->compare_exchange_strong(old_entry, my_slot))
				continue;

			while (entry->load(memory_order_relaxed) != nullptr)
				;

			delete value_ptr;
			delete my_slot;
			return;
		}
	}
	optional<int> pop(unsigned idx)
	{
		auto my_slot = new Slot;
		my_slot->op = 1;

		auto &entry = entries[idx];
		while (true)
		{
			auto result = el_array.pop(idx);
			if (result)
				return result;

			Slot *old_entry = entry->load(memory_order_relaxed);
			if (old_entry != nullptr)
				continue;
			if (false == entry->compare_exchange_strong(old_entry, my_slot))
				continue;

			while (entry->load(memory_order_acquire) != nullptr)
				;

			if (my_slot->value == nullptr)
				return nullopt;

			auto ret_val = *my_slot->value;
			delete my_slot->value;
			delete my_slot;
			return ret_val;
		}
	}

	// helper thread가 호출할 methods
	void process_ops(stack<int> &stack)
	{
		for (auto &op : entries)
		{
			auto slot = op->load(memory_order_acquire);
			if (slot == nullptr)
				continue;

			if (slot->op == 0)
			{
				stack.push(*slot->value);
			}
			else if (stack.empty() == false)
			{
				slot->value = new int{stack.top()};
				stack.pop();
			}
			else
			{
				slot->value = nullptr;
			}
			op->store(nullptr, memory_order_release);
		}
	}

private:
	vector<unique_ptr<atomic<Slot *>>> entries;
	EliminationArray el_array;
};

void global_helper_func(shared_ptr<vector<unique_ptr<SlotArray, DeallocNUMA<SlotArray>>>> arr, shared_ptr<stack<int>> stack)
{
	while (true)
	{
		for (auto &arr_ptr : *arr)
		{
			arr_ptr->process_ops(*stack);
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
		get_local_array()->push(value, tid % cores_per_node);
	}
	optional<int> pop()
	{
		return get_local_array()->pop(tid % cores_per_node);
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

	SlotArray *get_local_array()
	{
		static thread_local SlotArray *local_array = (*per_node_arrays)[get_node_id(tid)].get();
		return local_array;
	}
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

	auto core_per_node = (CPU_NUM / 2) / NODE_NUM;
	EDStack myStack{max(CPU_NUM, num_thread) / NODE_NUM, min(max(num_thread / core_per_node, 1u), NODE_NUM)};

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
