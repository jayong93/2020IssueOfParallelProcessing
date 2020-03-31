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

static const int NUM_TEST = 10000000;
static const int RANGE = 1000;

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
			/*
			 * pred가 지워졌는지 체크하지 않으면 curr의 next(즉, succ)가 이미 다른 thread에 의해 delete 된 상태에서 읽을 수도 있다
			 * 위의 do-while 확인에서 pred가 이미 자료구조에서 빠져 있었다면, pred->next는 변화할 일이 없다.
			 * 그러므로 위의 확인 루프는 항상 성공할 수 밖에 없고, curr가 여전히 자료구조에 존재하는지 확인할 수 없다.
			 * 즉, 그 뒤에 연결된 모든 node들이 살아있다는 보장을 할 수 없다.
			 *
			 * 만약 아래의 marking 체크를 한다면, 혹시 curr가 자료구조에서 빠져있는 상황이라도 다음 loop의 검사에서 걸리기 때문에 안전하다.
			 */
			if (true == pr->IsMarked()) {
				goto retry;
			}

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

				hp_list.retire(cu, retired_list);
				hp2->set_hp(su);

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
					hp_list.retire(curr, retired_list);
				}
				return true;
			}
		}
	}
	bool Contains(int x)
	{
		auto hp1 = hp_list.acq_guard();
		auto hp2 = hp_list.acq_guard();

		LFNODE* curr = &head;
		LFNODE* next;

		while (curr->key < x) {
			hp1->set_hp(curr);
			do {
				next = curr->GetNext();
				hp2->set_hp(next);
				//mfence;
			} while (curr->GetNext() != next);
			if (true == curr->IsMarked()) {
				LFNODE* pred;
				auto [hp_pred, hp_curr] = Find(x, &pred, &curr);
				hp1 = move(hp_pred);
				hp2 = move(hp_curr);
				break;
			}
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
	for (int num_thread = 1; num_thread <= 32; num_thread *= 2) {
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
