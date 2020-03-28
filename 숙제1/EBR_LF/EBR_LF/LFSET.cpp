#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <climits>
#include <algorithm>

using namespace std;
using namespace chrono;

unsigned long fast_rand(void)
{ //period 2^96-1
	static unsigned long x = 123456789, y = 362436069, z = 521288629;
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

static const int NUM_TEST = 4000000;
static const int RANGE = 1000;
constexpr unsigned MAX_THREAD = 32;

class LFNODE
{
public:
	int key;
	unsigned next;

	LFNODE()
	{
		next = 0;
	}
	LFNODE(int x)
	{
		key = x;
		next = 0;
	}
	~LFNODE()
	{
	}
	LFNODE *GetNext()
	{
		return reinterpret_cast<LFNODE *>(next & 0xFFFFFFFE);
	}

	void SetNext(LFNODE *ptr)
	{
		next = reinterpret_cast<unsigned>(ptr);
	}

	LFNODE *GetNextWithMark(bool *mark)
	{
		int temp = next;
		*mark = (temp % 2) == 1;
		return reinterpret_cast<LFNODE *>(temp & 0xFFFFFFFE);
	}

	bool CAS(int old_value, int new_value)
	{
		return atomic_compare_exchange_strong(
			reinterpret_cast<atomic_int *>(&next),
			&old_value, new_value);
	}

	bool CAS(LFNODE *old_next, LFNODE *new_next, bool old_mark, bool new_mark)
	{
		unsigned old_value = reinterpret_cast<unsigned>(old_next);
		if (old_mark)
			old_value = old_value | 0x1;
		else
			old_value = old_value & 0xFFFFFFFE;
		unsigned new_value = reinterpret_cast<unsigned>(new_next);
		if (new_mark)
			new_value = new_value | 0x1;
		else
			new_value = new_value & 0xFFFFFFFE;
		return CAS(old_value, new_value);
	}

	bool TryMark(LFNODE *ptr)
	{
		unsigned old_value = reinterpret_cast<unsigned>(ptr) & 0xFFFFFFFE;
		unsigned new_value = old_value | 1;
		return CAS(old_value, new_value);
	}

	bool IsMarked()
	{
		return (0 != (next & 1));
	}
};

struct EpochNode
{
	LFNODE *ptr;
	unsigned long long epoch;

	EpochNode(LFNODE *ptr, unsigned long long epoch) : ptr{ptr}, epoch{epoch} {}
};

atomic_ullong g_epoch;
atomic_ullong *t_epochs[MAX_THREAD];
thread_local vector<EpochNode> retired_list;
thread_local unsigned tid;
thread_local unsigned counter;
constexpr unsigned epoch_freq = 20;

void retire(LFNODE *node)
{
	retired_list.emplace_back(node, g_epoch.load());
	++counter;
	if (counter % epoch_freq == 0)
	{
		g_epoch.fetch_add(1);
	}
	if (retired_list.size() > MAX_THREAD)
	{
		auto min_epoch = ULLONG_MAX;
		for (auto &epoch : t_epochs)
		{
			auto e = epoch->load();
			if (min_epoch > e)
			{
				min_epoch = e;
			}
		}

		auto removed_it = remove_if(retired_list.begin(), retired_list.end(), [min_epoch](auto &r_node) {
			if (r_node.epoch < min_epoch)
			{
				delete r_node.ptr;
				return true;
			}
			return false;
		});
		retired_list.erase(removed_it, retired_list.end());
	}
}

void start_op()
{
	t_epochs[tid]->store(g_epoch.load());
}

void end_op()
{
	t_epochs[tid]->store(ULLONG_MAX);
}

class LFSET
{
	LFNODE head, tail;

public:
	LFSET()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.SetNext(&tail);
	}
	void Init()
	{
		while (head.GetNext() != &tail)
		{
			LFNODE *temp = head.GetNext();
			head.next = temp->next;
			delete temp;
		}
	}

	void Dump()
	{
		LFNODE *ptr = head.GetNext();
		cout << "Result Contains : ";
		for (int i = 0; i < 20; ++i)
		{
			cout << ptr->key << ", ";
			if (&tail == ptr)
				break;
			ptr = ptr->GetNext();
		}
		cout << endl;
	}

	void Find(int x, LFNODE **pred, LFNODE **curr)
	{
		start_op();
	retry:
		LFNODE *pr = &head;
		LFNODE *cu = pr->GetNext();
		while (true)
		{
			bool removed;
			LFNODE *su = cu->GetNextWithMark(&removed);
			while (true == removed)
			{
				if (false == pr->CAS(cu, su, false, false))
					goto retry;
				retire(cu);
				cu = su;
				su = cu->GetNextWithMark(&removed);
			}
			if (cu->key >= x)
			{
				*pred = pr;
				*curr = cu;
				return;
			}
			pr = cu;
			cu = cu->GetNext();
		}
	}
	bool Add(int x)
	{
		LFNODE *pred, *curr;
		while (true)
		{
			Find(x, &pred, &curr);

			if (curr->key == x)
			{
				end_op();
				return false;
			}
			else
			{
				LFNODE *e = new LFNODE(x);
				e->SetNext(curr);
				if (false == pred->CAS(curr, e, false, false))
				{
					end_op();
					continue;
				}
				end_op();
				return true;
			}
		}
	}
	bool Remove(int x)
	{
		LFNODE *pred, *curr;
		while (true)
		{
			Find(x, &pred, &curr);

			if (curr->key != x)
			{
				end_op();
				return false;
			}
			else
			{
				LFNODE *succ = curr->GetNext();
				if (false == curr->TryMark(succ))
				{
					end_op();
					continue;
				}
				if (true == pred->CAS(curr, succ, false, false))
				{
					retire(curr);
				}
				// delete curr;
				end_op();
				return true;
			}
		}
	}
	bool Contains(int x)
	{
		start_op();
		LFNODE *curr = &head;
		while (curr->key < x)
		{
			curr = curr->GetNext();
		}

		auto ret = (false == curr->IsMarked()) && (x == curr->key);
		end_op();
		return ret;
	}
};

LFSET my_set;

void benchmark(int num_thread)
{
	for (int i = 0; i < NUM_TEST / num_thread; ++i)
	{
		//	if (0 == i % 100000) cout << ".";
		switch (fast_rand() % 3)
		{
		case 0:
			my_set.Add(fast_rand() % RANGE);
			break;
		case 1:
			my_set.Remove(fast_rand() % RANGE);
			break;
		case 2:
			my_set.Contains(fast_rand() % RANGE);
			break;
		default:
			cout << "ERROR!!!\n";
			exit(-1);
		}
	}
}

int main()
{
	vector<thread> worker;
	for (auto &epoch : t_epochs)
	{
		epoch = new atomic_ullong{ULLONG_MAX};
	}
	for (int num_thread = 1; num_thread <= MAX_THREAD; num_thread *= 2)
	{
		g_epoch = 0;
		for (auto &epoch : t_epochs)
		{
			epoch->store(ULLONG_MAX);
		}
		my_set.Init();
		worker.clear();

		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < num_thread; ++i)
			worker.push_back(thread{benchmark, num_thread});
		for (auto &th : worker)
			th.join();
		auto du = high_resolution_clock::now() - start_t;
		my_set.Dump();

		cout << num_thread << " Threads,  Time = ";
		cout << duration_cast<milliseconds>(du).count() << " ms\n";
	}
}
