#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include "hazard_ptr.h"

using namespace std;
using namespace std::chrono;

class NODE {
public:
	int key;
	NODE* next;

	NODE() { next = nullptr; }
	NODE(int key_value) {
		next = nullptr;
		key = key_value;
	}
	~NODE() {}
};

thread_local vector<NODE*> retired_list;
HazardPtrList<NODE> hp_list;
using HP = HazardPtr<NODE>;

class LFQUEUE {
	NODE* volatile head;
	NODE* volatile tail;
public:
	LFQUEUE()
	{
		head = tail = new NODE(0);
	}
	~LFQUEUE() {}

	void Init()
	{
		NODE* ptr;
		while (head->next != nullptr) {
			ptr = head->next;
			head->next = head->next->next;
			delete ptr;
		}
		tail = head;
	}
	bool CAS(NODE* volatile* addr, NODE* old_node, NODE* new_node)
	{
		return atomic_compare_exchange_strong(reinterpret_cast<volatile atomic_int*>(addr),
			reinterpret_cast<int*>(&old_node),
			reinterpret_cast<int>(new_node));
	}
	void Enq(int key)
	{
		NODE* e = new NODE(key);
		auto hp1 = hp_list.acq_guard();
		while (true) {
			NODE* last = tail;
			hp1->set_hp(last);
			if (last != tail) continue;

			NODE* next = last->next;
			if (next != nullptr) {
				CAS(&tail, last, next);
				continue;
			}
			if (false == CAS(&last->next, nullptr, e)) continue;
			CAS(&tail, last, e);
			return;
		}
	}
	int Deq()
	{
		auto hp1 = hp_list.acq_guard();
		auto hp2 = hp_list.acq_guard();
		auto hp3 = hp_list.acq_guard();
		while (true) {
			NODE* first = head;
			hp1->set_hp(first);
			if (first != head) continue;

			NODE* next;
			do {
				next = first->next;
				hp2->set_hp(next);
			} while (next != first->next);

			NODE* last;
			do {
				last = tail;
				hp3->set_hp(last);
			} while (last != tail);
			NODE* lastnext = last->next;

			if (last == first) {
				if (lastnext == nullptr) {
					cout << "EMPTY!!!\n";
					this_thread::sleep_for(1ms);
					return -1;
				}
				else
				{
					CAS(&tail, last, lastnext);
					continue;
				}
			}
			if (nullptr == next) continue;
			int result = next->key;
			if (false == CAS(&head, first, next)) continue;
			first->next = nullptr;
			//delete first;
			hp1.reset();
			hp_list.retire(first, retired_list);
			return result;
		}
	}

	void display20()
	{
		int c = 20;
		NODE* p = head->next;
		while (p != nullptr)
		{
			cout << p->key << ", ";
			p = p->next;
			c--;
			if (c == 0) break;
		}
		cout << endl;
	}
};

const auto NUM_TEST = 10000000;

LFQUEUE my_queue;
void ThreadFunc(int num_thread)
{
	for (int i = 0; i < NUM_TEST / num_thread; i++) {
		if ((rand() % 2 == 0) || (i < (10000 / num_thread))) {
			my_queue.Enq(i);
		}
		else {
			int key = my_queue.Deq();
		}
	}
}

int main()
{
	for (auto n = 1; n <= 16; n *= 2) {
		my_queue.Init();
		hp_list.clear();
		vector <thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, n);
		for (auto& th : threads) th.join();
		auto d = high_resolution_clock::now() - s;
		my_queue.display20();
		//my_queue.recycle_freelist();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs.\n";
	}
}


