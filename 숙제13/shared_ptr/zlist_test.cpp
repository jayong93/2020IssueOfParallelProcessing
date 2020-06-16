#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include "htm_shared_ptr.h"

using namespace std;
using namespace std::chrono;

const auto NUM_TEST = 40000;
const auto KEY_RANGE = 1000;

class NODE {
public:
	int key;
	NODE* next;
	mutex n_lock;
	bool removed;

	NODE() { next = NULL; removed = false; }
	NODE(int key_value) {
		next = NULL;
		key = key_value;
		removed = false;
	}
	~NODE() {}
	void lock()
	{
		n_lock.lock();
	}
	void unlock()
	{
		n_lock.unlock();
	}
};
class ZLIST {
	NODE head, tail;
	NODE* freelist;
	NODE freetail;
	mutex fl_mutex;
public:
	ZLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.next = &tail;
		freetail.key = 0x7FFFFFFF;
		freelist = &freetail;
	}
	~ZLIST() {}

	void Init()
	{
		NODE* ptr;
		while (head.next != &tail) {
			ptr = head.next;
			head.next = head.next->next;
			delete ptr;
		}
	}

	void recycle_freelist()
	{
		NODE* p = freelist;
		while (p != &freetail) {
			NODE* n = p->next;
			delete p;
			p = n;
		}
		freelist = &freetail;
	}

	bool validate(NODE* pred, NODE* curr)
	{
		return (pred->removed == false) && (false == curr->removed) && (pred->next == curr);
	}

	bool Add(int key)
	{
		NODE* pred, * curr;

		while (true) {
			pred = &head;
			curr = pred->next;
			while (curr->key < key) {
				pred = curr; curr = curr->next;
			}
			pred->lock(); curr->lock();
			if (false == validate(pred, curr)) {
				pred->unlock();
				curr->unlock();
				continue;
			}
			if (key == curr->key) {
				pred->unlock();
				curr->unlock();
				return false;
			}
			else {
				NODE* node = new NODE(key);
				node->next = curr;
				pred->next = node;
				pred->unlock();
				curr->unlock();
				return true;
			}
		}
	}

	bool Remove(int key)
	{
		NODE* pred, * curr;

		while (true) {
			pred = &head;
			curr = pred->next;
			while (curr->key < key) {
				pred = curr; curr = curr->next;
			}
			pred->lock(); curr->lock();
			if (false == validate(pred, curr)) {
				pred->unlock();
				curr->unlock();
				continue;
			}
			if (key == curr->key) {
				pred->next = curr->next;
				fl_mutex.lock();
				curr->next = freelist;
				freelist = curr;
				fl_mutex.unlock();
				pred->unlock();
				curr->unlock();
				return true;
			}
			else {
				pred->unlock();
				curr->unlock();
				return false;
			}
		}
	}
	bool Contains(int key)
	{
		NODE* curr;
		curr = head.next;
		while (curr->key < key) {
			curr = curr->next;
		}
		return (key == curr->key) && (false == curr->removed);
	}

	void display20()
	{
		int c = 20;
		NODE* p = head.next;
		while (p != &tail)
		{
			cout << p->key << ", ";
			p = p->next;
			c--;
			if (c == 0) break;
		}
		cout << endl;
	}
};

class null_mutex {
public:
	void lock() {}
	void unlock() {}
};
class SPNODE {
public:
	int key;
	bool removed;
	htm_shared_ptr <SPNODE> next;
	mutex n_lock;

	SPNODE() { next = nullptr; removed = false; }
	SPNODE(int key_value) {
		next = nullptr;
		key = key_value;
		removed = false;
	}
	~SPNODE() {}
	void lock()
	{
		n_lock.lock();
	}
	void unlock()
	{
		n_lock.unlock();
	}
};
class SPZLIST {

	htm_shared_ptr <SPNODE>  head, tail;

public:
	SPZLIST()
	{
		head = make_htm_shared<SPNODE>();
		tail = make_htm_shared<SPNODE>();
		head->key = 0x80000000;
		tail->key = 0x7FFFFFFF;
		head->next = tail;
	}
	~SPZLIST() {}

	void Init()
	{
		head->next = tail;
	}

	void recycle_freelist()
	{
		return;
	}

	bool validate(htm_shared_ptr<SPNODE>& pred, htm_shared_ptr<SPNODE>& curr)
	{
		return (pred->removed == false) && (false == curr->removed) && (pred->next == curr);
	}

	bool Add(int key)
	{
		htm_shared_ptr<SPNODE> pred, curr;
		while (true) {

#if(true == SHARED_PTR_ZLIST)
			pred = atomic_load(&head);
			curr = atomic_load(&pred->next);
			while (curr->key < key) {
				pred = atomic_load(&curr);
				curr = atomic_load(&curr->next);
			}
#else
			pred = head;
			curr = pred->next;
			while (curr->key < key) {
				pred = curr;
				curr = curr->next;
			}
#endif

			pred->lock(); curr->lock();
			if (false == validate(pred, curr)) {
				pred->unlock();
				curr->unlock();
				continue;
			}
			if (key == curr->key) {
				pred->unlock();
				curr->unlock();
				return false;
			}
			else {
				htm_shared_ptr<SPNODE> node = make_htm_shared<SPNODE>(key);


				node->next = curr;
				pred->next = node;

				pred->unlock();
				curr->unlock();
				return true;
			}
		}
	}

	bool Remove(int key)
	{
		htm_shared_ptr<SPNODE> pred, curr;

		while (true) {
			pred = head;
			curr = pred->next;
			while (curr->key < key) {
				pred = curr;
				curr = curr->next;
			}
			pred->lock(); curr->lock();
			if (false == validate(pred, curr)) {
				pred->unlock();
				curr->unlock();
				continue;
			}
			if (key == curr->key) {
				pred->next = curr->next;
				pred->unlock();
				curr->unlock();
				return true;
			}
			else {
				pred->unlock();
				curr->unlock();
				return false;
			}
		}
	}
	bool Contains(int key)
	{
		htm_shared_ptr <SPNODE> curr;
		curr = head->next;
		while (curr->key < key) {
			auto& temp = curr;
			curr = curr->next;
		}
		return (key == curr->key) && (false == curr->removed);
	}

	void display20()
	{
		int c = 20;
		htm_shared_ptr<SPNODE> p = head->next;
		while (p->key != tail->key)
		{
			cout << p->key << ", ";
			p = p->next;
			c--;
			if (c == 0) break;
		}
		cout << endl;
	}
};


SPZLIST list;

atomic_uint abort_capacity{0};
atomic_uint abort_conflict{0};
atomic_uint abort_explicit{0};
atomic_uint abort_other{0};
atomic_uint tx_success{0};

void ThreadFunc(int num_thread)
{
	int key = 0;
	for (int i = 0; i < NUM_TEST / num_thread; i++) {
		switch (rand() % 3) {
		case 0: key = rand() % KEY_RANGE;
			list.Add(key);
			break;
		case 1: key = rand() % KEY_RANGE;
			list.Remove(key);
			break;
		case 2: key = rand() % KEY_RANGE;
			list.Contains(key);
			break;
		default: cout << "Error\n";
			exit(-1);
		}
	}
    abort_capacity.fetch_add(tx_log.abort_capacity, memory_order_relaxed);
    abort_conflict.fetch_add(tx_log.abort_conflict, memory_order_relaxed);
    abort_explicit.fetch_add(tx_log.abort_explicit, memory_order_relaxed);
    abort_other.fetch_add(tx_log.abort_other, memory_order_relaxed);
    tx_success.fetch_add(tx_log.success, memory_order_relaxed);
}

int main()
{
	for (auto n = 1; n <= 16; n *= 2) {
		list.Init();
        abort_other = 0;
        abort_conflict = 0;
        abort_capacity = 0;
        abort_explicit = 0;
        tx_success = 0;

		vector <thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, n);
		for (auto& th : threads) th.join();
		auto d = high_resolution_clock::now() - s;
		list.display20();
		//list.recycle_freelist();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs.\n";

        cerr << "total abort: " << abort_capacity.load() + abort_conflict.load() + abort_explicit.load() + abort_other.load() << endl;
        cerr << "    capacity: " << abort_capacity << endl;
        cerr << "    conflict: " << abort_conflict << endl;
        cerr << "    explicit: " << abort_explicit << endl;
        cerr << "    other: " << abort_other << endl;
        cerr << "total commit: " << tx_success << endl;
	}
}
