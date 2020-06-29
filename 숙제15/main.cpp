#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include <map>
#include <unordered_map>
#include <array>
#include <numa.h>

using namespace std;
using namespace std::chrono;

constexpr int MAXHEIGHT = 10;
constexpr int MAX_THREAD = 64;

template <typename T, typename... Vals>
T *NUMA_alloc(unsigned numa_id, Vals &&... val)
{
	void *raw_ptr = numa_alloc_onnode(sizeof(T), numa_id);
	T *ptr = new (raw_ptr) T(forward<Vals>(val)...);
	return ptr;
}

template <typename T>
void NUMA_dealloc(T *ptr)
{
	ptr->~T();
	numa_free(ptr, sizeof(T));
}

thread_local unsigned long g_next = 1;
/* RAND_MAX assumed to be 32767 */
int rand_mt(void)
{
	g_next = g_next * 1103515245 + 12345;
	return ((unsigned)(g_next / 65536) % 32768);
}

thread_local map<int, int> distance_count;

class SLNODE
{
public:
	int key;
	SLNODE *next[MAXHEIGHT];
	int height;
	SLNODE(int x, int h)
	{
		key = x;
		height = h;
		for (auto &p : next)
			p = nullptr;
	}
	SLNODE(int x)
	{
		key = x;
		height = MAXHEIGHT;
		for (auto &p : next)
			p = nullptr;
	}
	SLNODE()
	{
		key = 0;
		height = MAXHEIGHT;
		for (auto &p : next)
			p = nullptr;
	}
};

class SKLIST
{
	SLNODE head, tail;

public:
	SKLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.height = tail.height = MAXHEIGHT;
		for (auto &p : head.next)
			p = &tail;
	}
	~SKLIST()
	{
		Init();
	}

	void Init()
	{
		SLNODE *ptr;
		while (head.next[0] != &tail)
		{
			ptr = head.next[0];
			head.next[0] = head.next[0]->next[0];
			delete ptr;
		}
		for (auto &p : head.next)
			p = &tail;
	}
	void Find(int key, SLNODE *preds[MAXHEIGHT], SLNODE *currs[MAXHEIGHT])
	{
		int cl = MAXHEIGHT - 1;
		while (true)
		{
			if (MAXHEIGHT - 1 == cl)
				preds[cl] = &head;
			else
				preds[cl] = preds[cl + 1];
			currs[cl] = preds[cl]->next[cl];
			while (currs[cl]->key < key)
			{
				preds[cl] = currs[cl];
				currs[cl] = currs[cl]->next[cl];
			}
			if (0 == cl)
				return;
			cl--;
		}
	}

	bool Add(int key)
	{
		SLNODE *preds[MAXHEIGHT], *currs[MAXHEIGHT];

		Find(key, preds, currs);

		if (key == currs[0]->key)
		{
			//glock.unlock();
			return false;
		}
		else
		{
			int height = 1;
			while (rand_mt() % 2 == 0)
			{
				height++;
				if (MAXHEIGHT == height)
					break;
			}
			SLNODE *node = new SLNODE(key, height);
			for (int i = 0; i < height; ++i)
			{
				preds[i]->next[i] = node;
				node->next[i] = currs[i];
			}

			return true;
		}
	}
	bool Remove(int key)
	{
		SLNODE *preds[MAXHEIGHT], *currs[MAXHEIGHT];

		Find(key, preds, currs);

		if (key == currs[0]->key)
		{
			for (int i = 0; i < currs[0]->height; ++i)
			{
				preds[i]->next[i] = currs[i]->next[i];
			}
			delete currs[0];
			return true;
		}
		else
		{
			return false;
		}
	}
	bool Contains(int key)
	{
		SLNODE *preds[MAXHEIGHT], *currs[MAXHEIGHT];
		Find(key, preds, currs);
		if (key == currs[0]->key)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	void Copy(SKLIST *target)
	{
		// Phase 1 : Bottom Level Copy
		SLNODE *ptr = head.next[0];
		SLNODE *t_ptr = target->head.next[0];
		SLNODE *t_prev = &target->head;
		unordered_map<int, SLNODE *> mapping;

		while (ptr != &tail)
		{
			if (t_ptr == &target->tail)
				break;
			t_ptr->height = ptr->height;
			t_ptr->key = ptr->key;
			mapping[t_ptr->key] = t_ptr;
			ptr = ptr->next[0];
			t_prev = t_ptr;
			t_ptr = t_ptr->next[0];
		}

		if (ptr != &tail)
		{
			while (ptr != &tail)
			{
				SLNODE *new_node = new SLNODE(ptr->key, ptr->height);
				t_prev->next[0] = new_node;
				mapping[new_node->key] = new_node;
				t_prev = new_node;
				ptr = ptr->next[0];
			}
			t_prev->next[0] = t_ptr;
		}
		if (t_ptr != &target->tail)
		{
			while (t_ptr != &target->tail)
			{
				SLNODE *t = t_ptr;
				t_ptr = t_ptr->next[0];
				delete t;
			}
			t_prev->next[0] = t_ptr;
		}
		t_ptr = &target->head;
		ptr = &head;
		while (t_ptr != &target->tail)
		{
			for (int i = 1; i < t_ptr->height; ++i)
			{
				t_ptr->next[i] = mapping[ptr->next[i]->key];
			}
			t_ptr = t_ptr->next[0];
			ptr = ptr->next[0];
		}
	}

	void display20()
	{
		int c = 20;
		SLNODE *p = head.next[0];
		while (p != &tail)
		{
			cout << p->key << ", ";
			p = p->next[0];
			c--;
			if (c == 0)
				break;
		}
		cout << endl;
	}
};

thread_local int thread_id;

class Consensus
{
	long result;
	bool CAS(long old_value, long new_value)
	{
		return atomic_compare_exchange_strong(reinterpret_cast<atomic_long *>(&result), &old_value, new_value);
	}

public:
	Consensus() { result = -1; }
	~Consensus() {}
	long decide(long value)
	{
		if (true == CAS(-1, value))
			return value;
		else
			return result;
	}
};

typedef bool Response;

enum Method
{
	M_ADD,
	M_REMOVE,
	M_CONTAINS,
	M_COPY,
	M_Print20
};

struct Invocation
{
	Method method;
	int input;
	void *ptr;
};

class SeqObject
{
	SKLIST sklist;

public:
	SKLIST *get_object()
	{
		return &sklist;
	}
	SeqObject() {}
	~SeqObject() {}
	Response Apply(const Invocation &invoc)
	{
		switch (invoc.method)
		{
		case M_ADD:
			return sklist.Add(invoc.input);
			break;
		case M_REMOVE:
			return sklist.Remove(invoc.input);
			break;
		case M_CONTAINS:
			return sklist.Contains(invoc.input);
			break;
		case M_Print20:
			sklist.display20();
			return true;
			break;
		case M_COPY:
			sklist.Copy(reinterpret_cast<SKLIST *>(invoc.ptr));
			return true;
		default:
			cout << "Invalid Sequential Object Operation!!:\n";
			exit(-1);
		}
		return false;
	}

	void Print20()
	{
		sklist.display20();
	}

	void clear()
	{
		sklist.Init();
	}
};

class NODE
{
public:
	Invocation invoc;
	Consensus decideNext;
	NODE *next;
	volatile int seq;

	NODE()
	{
		seq = 0;
		next = nullptr;
	}
	~NODE() {}
	NODE(const Invocation &input_invoc)
	{
		invoc = input_invoc;
		next = nullptr;
		seq = 0;
	}
};

constexpr int ST_FREE = 0;
constexpr int ST_SHARE = 1;
constexpr int ST_EXCLUSIVE = 2;
constexpr unsigned CORE_PER_NODE = 16;
constexpr unsigned NUMA_NODE_NUM = 4;

template<class T>
using NumaThreadLocal = array<T, CORE_PER_NODE>;

class LFUniversal
{
	static atomic_uint tid_counter;
	static thread_local unsigned tid;
	static thread_local unsigned local_numa_id;

	// atomic_int object_state[MAX_THREAD];
	NumaThreadLocal<atomic_int>* object_state[NUMA_NODE_NUM];
	// SeqObject local_objects[MAX_THREAD];
	NumaThreadLocal<SeqObject>* local_objects[NUMA_NODE_NUM];
	NumaThreadLocal<NODE*>* local_tail[NUMA_NODE_NUM];

	NODE *head[MAX_THREAD];
	NODE *tail;

	atomic<long long> *cur_obj[NUMA_NODE_NUM];

public:
	NumaThreadLocal<atomic_int> &numa_local_object_state()
	{
		static thread_local NumaThreadLocal<atomic_int>& obj = *object_state[local_numa_id];
		return obj;
	}
	NumaThreadLocal<SeqObject> &numa_local_seq_object()
	{
		static thread_local NumaThreadLocal<SeqObject>& obj = *local_objects[local_numa_id];
		return obj;
	}
	NumaThreadLocal<NODE *>&numa_local_tail()
	{
		static thread_local NumaThreadLocal<NODE *>& obj = *local_tail[local_numa_id];
		return obj;
	}
	atomic<long long> &numa_local_cur_obj()
	{
		static thread_local atomic<long long> &obj = *cur_obj[local_numa_id];
		return obj;
	}

	SKLIST *get_first_object()
	{
		return (*(local_objects[0]))[0].get_object();
	}
	NODE *GetMaxNODE()
	{
		NODE *max_node = head[0];
		for (int i = 1; i < MAX_THREAD; i++)
		{
			if (max_node->seq < head[i]->seq)
				max_node = head[i];
		}
		return max_node;
	}

	void clear()
	{
		tid_counter = 0;
		while (tail != nullptr)
		{
			NODE *temp = tail;
			tail = tail->next;
			delete temp;
		}
		tail = new NODE;
		tail->seq = 1;
		for (int i = 0; i < MAX_THREAD; ++i)
		{
			head[i] = tail;
		}
		for (auto i = 0; i < NUMA_NODE_NUM; ++i)
		{
			auto local_cur_obj = cur_obj[i];
			for (auto j = 0; j < CORE_PER_NODE; ++j)
			{
				(*object_state[i])[j] = ST_FREE;
				(*local_objects[i])[j].clear();
				(*local_tail[i])[j] = tail;
			}
			*local_cur_obj = 1 << 7;
		}
	}

	LFUniversal()
	{
		tail = new NODE;
		tail->seq = 1;
		for (int i = 0; i < MAX_THREAD; ++i)
		{
			head[i] = tail;
		}
		for (auto i = 0; i < NUMA_NODE_NUM; ++i)
		{
			object_state[i] = NUMA_alloc<NumaThreadLocal<atomic_int>>(i);
			local_objects[i] = NUMA_alloc<NumaThreadLocal<SeqObject>>(i);
			local_tail[i] = NUMA_alloc<NumaThreadLocal<NODE*>>(i);
			cur_obj[i] = NUMA_alloc<atomic<long long>>(i);
			auto &local_cur_obj = cur_obj[i];
			for (auto j = 0; j < CORE_PER_NODE; ++j)
			{
				(*object_state[i])[j] = ST_FREE;
				(*local_objects[i])[j].clear();
				(*local_tail[i])[j] = tail;
			}
			*local_cur_obj = 1 << 7;
		}
	}

	~LFUniversal()
	{
		clear();
		NUMA_dealloc(object_state);
		NUMA_dealloc(local_objects);
		NUMA_dealloc(local_tail);
		NUMA_dealloc(cur_obj);
	}

	Response Apply(const Invocation &invoc)
	{
		NODE *prefer = new NODE{invoc};
		while (prefer->seq == 0)
		{
			NODE *before = GetMaxNODE();
			NODE *after = reinterpret_cast<NODE *>(before->decideNext.decide(reinterpret_cast<long>(prefer)));
			before->next = after;
			after->seq = before->seq + 1;
			head[thread_id] = after;
		}

		int index = 0;
		auto & local_object_state = numa_local_object_state();
		auto& local_tails = numa_local_tail();
		auto& local_objects = numa_local_seq_object();
		while (true)
		{
			while (ST_FREE != local_object_state[index])
			{
				index++;
				index = index % CORE_PER_NODE;
			}
			auto& obj_state = local_object_state[index];
			int old_state = ST_FREE;
			if (true == atomic_compare_exchange_strong(&obj_state, &old_state, ST_EXCLUSIVE))
			{
				if (prefer->seq > local_tails[index]->seq)
					break;
				obj_state = ST_FREE;
			}
			index++;
			index = index % CORE_PER_NODE;
		}

		NODE *curr = local_tails[index]->next;
		distance_count[prefer->seq - curr->seq]++;
		while (curr != prefer)
		{
			local_objects[index].Apply(curr->invoc);
			curr = curr->next;
		}
		local_tails[index] = curr;
		Response res = local_objects[index].Apply(curr->invoc);
		local_object_state[index] = ST_FREE;

		auto& local_cur_obj = numa_local_cur_obj();
		while (true)
		{
			long long new_obj = (prefer->seq << 7) + index;
			long long old_obj = local_cur_obj;
			if ((new_obj >> 7) < (old_obj >> 7))
				break;
			if (true == atomic_compare_exchange_strong(&local_cur_obj, &old_obj, new_obj))
				break;
		}
		return res;
	}

	Response ROApply(const Invocation &invoc)
	{
		int idx;
		int old_state;
		auto& cur_obj = numa_local_cur_obj();
		auto& object_state = numa_local_object_state();
		auto& local_objects = numa_local_seq_object();
		while (true)
		{
			idx = cur_obj & 0x7f;
			old_state = object_state[idx];
			if (ST_SHARE == (old_state & 0x3))
				if (true == atomic_compare_exchange_strong(&object_state[idx], &old_state, old_state + 4))
					break;
			if (ST_FREE == (old_state & 0x3))
				if (true == atomic_compare_exchange_strong(&object_state[idx], &old_state, ST_SHARE + 4))
					break;
		}

		Response res = local_objects[idx].Apply(invoc);
		old_state = object_state[idx];
		while (true)
		{
			if (ST_SHARE + 4 == old_state)
			{
				if (true == atomic_compare_exchange_strong(&object_state[idx], &old_state, ST_FREE))
					break;
			}
			else if (true == atomic_compare_exchange_strong(&object_state[idx], &old_state, old_state - 4))
				break;
		}
		return res;
	}

	void Print20()
	{
		ROApply(Invocation{M_Print20, 0});
	}

	void pin_thread() {
		numa_run_on_node(local_numa_id);
	}
};

atomic_uint LFUniversal::tid_counter{0};
thread_local unsigned LFUniversal::tid = tid_counter.fetch_add(1, memory_order_relaxed);
thread_local unsigned LFUniversal::local_numa_id = (tid / (CORE_PER_NODE / 2)) % NUMA_NODE_NUM;

const auto NUM_TEST = 4000000;
const auto KEY_RANGE = 1000;
LFUniversal g_list;

mutex gl;

#ifndef WRITE_RATIO
#define WRITE_RATIO 30
#endif

void ThreadFunc(int num_thread, int tid)
{
	thread_id = tid;
	g_list.pin_thread();
	for (int i = 0; i < NUM_TEST / num_thread; ++i)
	{
		Invocation invoc;
		if (rand_mt() % 100 < WRITE_RATIO)
		{
			if (rand_mt() % 100 < 50)
			{
				invoc.method = M_ADD;
				invoc.input = i;
				g_list.Apply(invoc);
			}
			else
			{
				invoc.method = M_REMOVE;
				invoc.input = i;
				g_list.Apply(invoc);
			}
		}
		else
		{
			invoc.method = M_CONTAINS;
			invoc.input = i;
			g_list.ROApply(invoc);
		}
	}
	//gl.lock();
	//cout << "Thread " << tid << endl;
	//for (auto& t : distance_count) {
	//	cout << "[" << t.first << "] : " << t.second << "]\n";
	//}
	//gl.unlock();
}

int main()
{
	for (auto n = 1; n <= 64; n *= 2)
	{
		g_list.clear();
		vector<thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, n, i);
		for (auto &th : threads)
			th.join();
		auto d = high_resolution_clock::now() - s;

		SKLIST CC;
		Invocation inv;
		inv.method = M_COPY;
		inv.ptr = &CC;
		SKLIST *org = g_list.get_first_object();
		org->Copy(&CC);

		g_list.Print20();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs.\n";
	}
}