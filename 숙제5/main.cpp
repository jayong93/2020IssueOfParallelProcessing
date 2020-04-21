#include <vector>
#include <atomic>
#include <utility>
#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <optional>
#include <algorithm>
#include <cassert>
#include <stack>

#ifndef READ_PROPORTION
#define READ_PROPORTION 30
#endif

using namespace std;
using namespace std::chrono;

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

constexpr int MAXHEIGHT = 10;
class SLNODE
{
public:
	int key;
	array<SLNODE *, MAXHEIGHT> next;
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
	struct CopyingInfo
	{
		SLNODE *org, *curr;
		int level;
	};

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
	SKLIST(const SKLIST &other) : SKLIST{}
	{
		stack<CopyingInfo> nodes_unlinked;
		nodes_unlinked.emplace(&other.head, &this->head, MAXHEIGHT - 1);
		copy_and_link(nodes_unlinked);
	}
	SKLIST(SKLIST &&other): head{move(other.head)}, tail{move(other.tail)}
	{
		for(auto& p : other.head.next) {
			p = &other.tail;
		}
	}

	SKLIST &operator=(const SKLIST &other)
	{
		Init();
		// tuple<original_node, current_node, highest_unlinked_level>
		stack<CopyingInfo> nodes_unlinked;
		nodes_unlinked.emplace(&other.head, &this->head, MAXHEIGHT - 1);
		copy_and_link(nodes_unlinked);
	}
	SKLIST& operator=(SKLIST&& other) {
		this->head = move(other.head);
		this->tail = move(other.tail);
		for(auto& p : other.head.next) {
			p = &other.tail;
		}
	}

	void copy_and_link(stack<CopyingInfo> &jobs)
	{
		if (jobs.empty())
			return;

		auto &[org, curr, level] = jobs.top();
		auto curr_level = level;
		if (org->next[curr_level]->next[curr_level] == nullptr)
		{
			curr->next[curr_level] = &tail;
			if (curr_level != 0)
			{
				level++;
			}
			else
			{
				jobs.pop();
			}
		}

		else
		{
			if (curr_level < MAXHEIGHT - 1 && org->next[curr_level] == org->next[curr_level - 1])
			{
				curr->next[curr_level] = curr->next[curr_level - 1];
			}
			else
			{
				auto org_next = org->next[curr_level];
				auto new_node = new SLNODE{org_next->key, org_next->height};
				curr->next[curr_level] = new_node;
			}

			if (curr_level != 0)
			{
				level++;
			}
			else
			{
				jobs.pop();
			}

			jobs.emplace(org->next[curr_level], curr->next[curr_level], curr_level);
		}

		copy_and_link(jobs);
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
			return false;
		}
		else
		{
			int height = 1;
			while (fast_rand() % 2 == 0)
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

enum class Func
{
	None,
	Add,
	Remove,
	Contains,
};

struct Invoc
{
	Func func;
	int arg;

	Invoc(Func func, int arg = 0) : func{func}, arg{arg} {}

	bool is_read_only() const
	{
		return func == Func::Contains;
	}
};

using Response = int;

struct Node
{
	Invoc invoc;
	uint64_t seq;
	atomic<Node *> next;

	Node(const Invoc &invoc) : invoc{invoc}, seq{0}, next{nullptr} {}
};

struct Object
{
	SKLIST container;

	Response apply(const Invoc &invoc)
	{
		switch (invoc.func)
		{
		case Func::Add:
			return container.Add(invoc.arg);
		case Func::Remove:
			return container.Remove(invoc.arg);
		case Func::Contains:
			return container.Contains(invoc.arg);
		default:
			return 0;
		}
	}

	void init()
	{
		container.Init();
	}
};

struct Combined
{
	Object obj;
	shared_mutex rw_lock;
	Node *last_node;

	Combined(Node &node) : last_node{&node} {}
};

constexpr int RECYCLE_RATE = 1000;

class OLFUniversal
{
public:
	OLFUniversal(int capacity) : capacity{capacity}, invoke_num{0}
	{
		Invoc invoc{Func::None};
		tail = new Node(move(invoc));
		tail->seq = 1;
		head.store(tail, memory_order_relaxed);
		for (auto i = 0; i < capacity; ++i)
		{
			combined_list.emplace_back(new Combined(*tail));
		}
	}
	~OLFUniversal()
	{
		Node *cur = tail->next.load(memory_order_relaxed);
		while (cur->next != nullptr)
		{
			Node *del = cur;
			cur = cur->next.load(memory_order_relaxed);
			delete del;
		}
		delete cur;
		delete tail;
		for (auto c : combined_list)
		{
			delete c;
		}
	}

	Object &current_obj()
	{
		Node *m = combined_list[0]->last_node;
		int thread_id = 0;
		int index = 1;
		for (auto it = combined_list.begin() + 1; it != combined_list.end(); ++it, ++index)
		{
			if (m->seq < (*it)->last_node->seq)
			{
				m = (*it)->last_node;
				thread_id = index;
			}
		}
		return combined_list[thread_id]->obj;
	}

	void update_combinded(Combined *target)
	{
		for (auto comb : combined_list)
		{
			if (comb == target)
				continue;

			shared_lock<shared_mutex> slg{comb->rw_lock};
			if (comb->last_node == nullptr)
				continue;

			target->obj = comb->obj;
			target->last_node = comb->last_node;
			break;
		}
	}

	template <typename LOCK, typename F>
	optional<pair<LOCK, Combined &>> get_comb(
		bool try_until_success, F cond)
	{
		LOCK lg;
		do
		{
			for (auto comb : combined_list)
			{
				lg = LOCK{comb->rw_lock, try_to_lock};
				if (lg && cond(*comb))
				{
					if (comb->last_node == nullptr)
					{
						update_combinded(comb);
					}
					return make_pair(move(lg), ref(*comb));
				}
			}
		} while (try_until_success);
		return nullopt;
	}

	optional<Response> update_local_obj(const Node &prefer, int thread_id)
	{
		optional<Response> result;
		{
			auto ret = get_comb<unique_lock<shared_mutex>>(true, [](auto &_) { return true; });
			auto [lg, comb] = move(*ret);
			assert(lg && "a lock guard didn't get its mutex");

			auto &last_node = comb.last_node;
			auto &last_obj = comb.obj;

			if (last_node->seq >= prefer.seq)
			{
				return nullopt;
			}
			last_node = last_node->next.load(memory_order_relaxed);
			while (last_node->seq < prefer.seq)
			{
				last_obj.apply(last_node->invoc);
				last_node = last_node->next.load(memory_order_relaxed);
			}

			result = last_obj.apply(last_node->invoc);
		}

		if (invoke_num.load(memory_order_relaxed) < RECYCLE_RATE)
		{
			if (invoke_num.fetch_add(1, memory_order_relaxed) + 1 == RECYCLE_RATE)
			{
				recycle();
				invoke_num.store(0, memory_order_relaxed);
			}
		}

		return result;
	}

	optional<Response> apply(const Invoc &invoc, int thread_id)
	{
		if (invoc.is_read_only())
		{
			optional<Response> ret_val = do_read_only(invoc, thread_id);
			if (ret_val)
			{
				return ret_val;
			}
		}

		Node *prefer = new Node(invoc);
		while (true)
		{
			Node *old_head = head.load(memory_order_relaxed);
			Node *old_next = old_head->next.load(memory_order_relaxed);
			if (old_next != nullptr)
			{
				head.compare_exchange_strong(old_head, old_next);
				continue;
			}

			prefer->seq = old_head->seq + 1;
			if (true == old_head->next.compare_exchange_strong(old_next, prefer))
			{
				head.compare_exchange_strong(old_head, prefer);
				break;
			}
		}

		return update_local_obj(*prefer, thread_id);
	}

	optional<Response> do_read_only(const Invoc &invoc, int thread_id)
	{
		auto old_head = head.load(memory_order_relaxed);
		auto old_seq = old_head->seq;

		auto ret = get_comb<shared_lock<shared_mutex>>(false, [old_seq](const Combined &c) { return c.last_node->seq == old_seq; });
		if (ret)
		{
			auto [lg, comb] = move(*ret);
			assert(lg && "a lock guard didn't get its mutex");
			return comb.obj.apply(invoc);
		}
		return nullopt;
	}

	void remove_until_seq(uint64_t until_seq)
	{
		auto old_next = tail->next.load(memory_order_relaxed);
		if (until_seq <= old_next->seq)
		{
			return;
		}

		do
		{
			auto tmp = old_next;
			old_next = old_next->next.load(memory_order_relaxed);
			delete tmp;
		} while (old_next->seq < until_seq);
		tail->next.store(old_next, memory_order_relaxed);
	}

	void recycle()
	{
		size_t num_to_remove = RECYCLE_RATE / 2;
		auto min_seq = tail->next.load(memory_order_relaxed)->seq + num_to_remove;

		// min_seq보다 낮은 seq를 last_node로 갖는 comb들을 찾는다.
		// 찾은 comb들을 lock을 걸고 last_node를 nullptr로 만든다. + seq_obj도 초기화한다.
		for (auto comb : combined_list)
		{
			// comb->last_node를 초기화 하는 thread는 현재 자신 밖에 없으므로 lock 없이 읽어도 안전.
			if (comb->last_node->seq < min_seq)
			{
				unique_lock<shared_mutex> lg{comb->rw_lock};

				comb->last_node = nullptr;
				comb->obj.init();
			}
		}

		remove_until_seq(min_seq);
	}

private:
	// 가장 최근 Node
	atomic<Node *> head;
	vector<Combined *> combined_list;
	// 가장 오래된 Node
	Node *tail;
	int capacity;
	atomic_ullong invoke_num;
};

const auto NUM_TEST = 4000000;
const auto KEY_RANGE = 1000;
void ThreadFunc(OLFUniversal *list, int num_thread, int thread_id)
{
	int key;

	for (int i = 0; i < NUM_TEST / num_thread; i++)
	{
		auto ticket = fast_rand() % 100;
		auto pivot = (100 - READ_PROPORTION) / 2;
		if (ticket < READ_PROPORTION)
		{
			// contains
			key = fast_rand() % KEY_RANGE;
			list->apply(Invoc(Func::Contains, key), thread_id);
		}
		else if (ticket - READ_PROPORTION > pivot)
		{
			// add
			key = fast_rand() % KEY_RANGE;
			list->apply(Invoc(Func::Add, key), thread_id);
		}
		else
		{
			// remove
			key = fast_rand() % KEY_RANGE;
			list->apply(Invoc(Func::Remove, key), thread_id);
		}
	}
}

int main()
{
	for (auto n = 1; n <= 64; n *= 2)
	{
		OLFUniversal list(n);

		vector<thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, &list, n, i);
		for (auto &th : threads)
			th.join();
		auto d = high_resolution_clock::now() - s;

		list.current_obj().container.display20();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs." << endl;
	}
}
