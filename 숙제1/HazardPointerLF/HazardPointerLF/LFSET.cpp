#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include "hazard_ptr.h"

using namespace std;
using namespace chrono;

static const int NUM_TEST = 1000000;
static const int RANGE = 1000;

#define mfence atomic_thread_fence(memory_order_seq_cst)


class LFNODE {
public:
	int key;
	unsigned next;

	LFNODE() {
		next = 0;
	}
	LFNODE(int x) {
		key = x;
		next = 0;
	}
	~LFNODE() {
	}
	LFNODE* GetNext() {
		return reinterpret_cast<LFNODE*>(next & 0xFFFFFFFE);
	}

	void SetNext(LFNODE* ptr) {
		next = reinterpret_cast<unsigned>(ptr);
	}

	LFNODE* GetNextWithMark(bool* mark) {
		int temp = next;
		*mark = (temp % 2) == 1;
		return reinterpret_cast<LFNODE*>(temp & 0xFFFFFFFE);
	}

	bool CAS(int old_value, int new_value)
	{
		return atomic_compare_exchange_strong(
			reinterpret_cast<atomic_int*>(&next),
			&old_value, new_value);
	}

	bool CAS(LFNODE* old_next, LFNODE* new_next, bool old_mark, bool new_mark) {
		unsigned old_value = reinterpret_cast<unsigned>(old_next);
		if (old_mark) old_value = old_value | 0x1;
		else old_value = old_value & 0xFFFFFFFE;
		unsigned new_value = reinterpret_cast<unsigned>(new_next);
		if (new_mark) new_value = new_value | 0x1;
		else new_value = new_value & 0xFFFFFFFE;
		return CAS(old_value, new_value);
	}

	bool TryMark(LFNODE* ptr)
	{
		unsigned old_value = reinterpret_cast<unsigned>(ptr) & 0xFFFFFFFE;
		unsigned new_value = old_value | 1;
		return CAS(old_value, new_value);
	}

	bool IsMarked() {
		return (0 != (next & 1));
	}
};

thread_local vector<LFNODE*> retired_list;
HazardPtrList<LFNODE> hp_list;
using HP = HazardPtr<LFNODE>;

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
		while (head.GetNext() != &tail) {
			LFNODE* temp = head.GetNext();
			head.next = temp->next;
			delete temp;
		}
	}

	void Dump()
	{
		LFNODE* ptr = head.GetNext();
		cout << "Result Contains : ";
		for (int i = 0; i < 20; ++i) {
			cout << ptr->key << ", ";
			if (&tail == ptr) break;
			ptr = ptr->GetNext();
		}
		cout << endl;
	}

	auto Find(int x, LFNODE** pred, LFNODE** curr)
	{
		auto hp1 = hp_list.acq_guard();
		auto hp2 = hp_list.acq_guard();
		auto hp3 = hp_list.acq_guard();
	retry:
		LFNODE* pr = &head;
		LFNODE* cu;
		while (true) {
			hp1->set_hp(pr);
			do {
				cu = pr->GetNext();
				hp2->set_hp(cu);
				//mfence;
			} while (pr->GetNext() != cu);

			bool removed = cu->IsMarked();
			LFNODE* su;
			while (true == removed) {
				do {
					su = cu->GetNext();
					hp3->set_hp(su);
					//mfence;
				} while (cu->GetNext() != su);

				if (false == pr->CAS(cu, su, false, false)) {
					hp1->clear();
					hp2->clear();
					hp3->clear();
					goto retry;
				}

				hp2->set_hp(su);
				hp_list.retire(cu, retired_list);

				cu = su;
				removed = cu->IsMarked();
			}
			hp3->clear();

			if (cu->key >= x) {
				*pred = pr; *curr = cu;
				return make_pair(std::move(hp1), std::move(hp2));
			}
			pr = cu;
		}
		return make_pair(std::move(hp1), std::move(hp2));
	}
	bool Add(int x)
	{
		LFNODE* pred, * curr;
		while (true) {
			auto hp_pair = Find(x, &pred, &curr);

			if (curr->key == x) {
				return false;
			}
			else {
				LFNODE* e = new LFNODE(x);
				e->SetNext(curr);
				if (false == pred->CAS(curr, e, false, false)) {
					continue;
				}
				return true;
			}
		}
	}
	bool Remove(int x)
	{
		LFNODE* pred, * curr;
		while (true) {
			auto hp_pair = Find(x, &pred, &curr);

			if (curr->key != x) {
				return false;
			}
			else {
				LFNODE* succ = curr->GetNext();
				if (false == curr->TryMark(succ)) {
					continue;
				}
				if (pred->CAS(curr, succ, false, false)) {
					hp_pair.second.reset();
					hp_list.retire(curr, retired_list);
				}
				return true;
			}
		}
	}
	bool Contains(int x)
	{
		LFNODE* curr = &head;
		LFNODE* next;

		auto hp1 = hp_list.acq_guard();
		auto hp2 = hp_list.acq_guard();
		while (curr->key < x) {
			hp1->set_hp(curr);
			do {
				next = curr->GetNext();
				hp2->set_hp(next);
				//mfence;
			} while (curr->GetNext() != next);
			curr = next;
		}

		auto key = curr->key;
		auto is_marked = curr->IsMarked();

		return (false == is_marked) && (x == key);
	}
};

LFSET my_set;

void benchmark(int num_thread)
{
	for (int i = 0; i < NUM_TEST / num_thread; ++i) {
		//	if (0 == i % 100000) cout << ".";
		switch (rand() % 3) {
		case 0: my_set.Add(rand() % RANGE); break;
		case 1: my_set.Remove(rand() % RANGE); break;
		case 2: my_set.Contains(rand() % RANGE); break;
		default: cout << "ERROR!!!\n"; exit(-1);
		}
	}
}

int main()
{
	vector <thread> worker;
	for (int num_thread = 1; num_thread <= 16; num_thread *= 2) {
		my_set.Init();
		hp_list.clear();
		worker.clear();

		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < num_thread; ++i)
			worker.push_back(thread{ benchmark, num_thread });
		for (auto& th : worker) th.join();
		auto du = high_resolution_clock::now() - start_t;
		my_set.Dump();

		cout << num_thread << " Threads,  Time = ";
		cout << duration_cast<milliseconds>(du).count() << " ms\n";
	}
}
