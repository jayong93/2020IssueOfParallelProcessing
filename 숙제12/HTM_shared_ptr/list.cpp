#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>
#include "htm_shared_ptr.h"

using namespace std;
using namespace std::chrono;

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
class nullmutex {
public:
	void lock() {}
	void unlock() {}
};
class CLIST {
	NODE head, tail;
	mutex glock;
public:
	CLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.next = &tail;
	}
	~CLIST() {}

	void Init()
	{
		NODE* ptr;
		while (head.next != &tail) {
			ptr = head.next;
			head.next = head.next->next;
			delete ptr;
		}
	}
	bool Add(int key)
	{
		NODE* pred, * curr;

		pred = &head;
		glock.lock();
		curr = pred->next;
		while (curr->key < key) {
			pred = curr;
			curr = curr->next;
		}

		if (key == curr->key) {
			glock.unlock();
			return false;
		}
		else {
			NODE* node = new NODE(key);
			node->next = curr;
			pred->next = node;
			glock.unlock();
			return true;
		}
	}
	bool Remove(int key)
	{
		NODE* pred, * curr;

		pred = &head;
		glock.lock();
		curr = pred->next;
		while (curr->key < key) {
			pred = curr;
			curr = curr->next;
		}

		if (key == curr->key) {
			pred->next = curr->next;
			delete curr;
			glock.unlock();
			return true;
		}
		else {
			glock.unlock();
			return false;
		}
	}
	bool Contains(int key)
	{

		NODE* pred, * curr;

		pred = &head;
		glock.lock();
		curr = pred->next;
		while (curr->key < key) {
			pred = curr;
			curr = curr->next;
		}
		if (key == curr->key) {
			glock.unlock();
			return true;
		}
		else {
			glock.unlock();
			return false;
		}
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

class FLIST {
	NODE head, tail;
public:
	FLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.next = &tail;
	}
	~FLIST() {}

	void Init()
	{
		NODE* ptr;
		while (head.next != &tail) {
			ptr = head.next;
			head.next = head.next->next;
			delete ptr;
		}
	}
	bool Add(int key)
	{
		NODE* pred, * curr;

		head.lock();
		pred = &head;
		curr = pred->next;
		curr->lock();
		while (curr->key < key) {
			pred->unlock();
			pred = curr;
			curr = curr->next;
			curr->lock();
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
	bool Remove(int key)
	{
		NODE* pred, * curr;

		head.lock();
		pred = &head;
		curr = pred->next;
		curr->lock();
		while (curr->key < key) {
			pred->unlock();
			pred = curr;
			curr = curr->next;
			curr->lock();
		}

		if (key == curr->key) {
			pred->next = curr->next;
			delete curr;
			pred->unlock();
			return true;
		}
		else {
			pred->unlock();
			curr->unlock();
			return false;
		}
	}
	bool Contains(int key)
	{

		NODE* pred, * curr;

		head.lock();
		pred = &head;
		curr = pred->next;
		curr->lock();
		while (curr->key < key) {
			pred->unlock();
			pred = curr;
			curr = curr->next;
			curr->lock();
		}
		if (key == curr->key) {
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

class OLIST {
	NODE head, tail;
	NODE* freelist;
	NODE freetail;
	mutex fl_mutex;
public:
	OLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.next = &tail;
		freetail.key = 0x7FFFFFFF;
		freelist = &freetail;
	}
	~OLIST() {}

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
		NODE* p = &head;
		while (p->key <= pred->key) {
			if (p == pred) return p->next == curr;
			p = p->next;
		}
		return false;
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
				return true;
			}
			else {
				pred->unlock();
				curr->unlock();
				return false;
			}
		}
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

class SPNODE {
public:
	int key;
	htm_shared_ptr <SPNODE> next;
	mutex n_lock;
	bool removed;

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
		head = make_shared<SPNODE>();
		tail = make_shared<SPNODE>();
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

	bool validate(shared_ptr<SPNODE>& pred, shared_ptr<SPNODE>& curr)
	{
		return (pred->removed == false) && (false == curr->removed) && (pred->next == curr);
	}

	bool Add(int key)
	{
		shared_ptr<SPNODE> pred, curr;

		while (true) {
			pred = head;
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
				shared_ptr<SPNODE> node = make_shared<SPNODE>(key);
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
		shared_ptr<SPNODE> pred, curr;

		while (true) {
			pred = head;
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
		shared_ptr <SPNODE> curr;
		curr = head->next;
		while (curr->key < key) {
			curr = curr->next;
		}
		return (key == curr->key) && (false == curr->removed);
	}

	void display20()
	{
		int c = 20;
		shared_ptr<SPNODE> p = head->next;
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

class LFNODE;
class MPTR
{
	int value;
public:
	void set(LFNODE* node, bool removed)
	{
		value = reinterpret_cast<int>(node);
		if (true == removed)
			value = value | 0x01;
		else
			value = value & 0xFFFFFFFE;
	}
	LFNODE* getptr()
	{
		return reinterpret_cast<LFNODE*>(value & 0xFFFFFFFE);
	}
	LFNODE* getptr(bool* removed)
	{
		int temp = value;
		if (0 == (temp & 0x1)) *removed = false;
		else *removed = true;
		return reinterpret_cast<LFNODE*>(temp & 0xFFFFFFFE);
	}
	bool CAS(LFNODE* old_node, LFNODE* new_node, bool old_removed, bool new_removed)
	{
		int old_value, new_value;
		old_value = reinterpret_cast<int>(old_node);
		if (true == old_removed) old_value = old_value | 0x01;
		else old_value = old_value & 0xFFFFFFFE;
		new_value = reinterpret_cast<int>(new_node);
		if (true == new_removed) new_value = new_value | 0x01;
		else new_value = new_value & 0xFFFFFFFE;
		return atomic_compare_exchange_strong(
			reinterpret_cast<atomic_int*>(&value), &old_value, new_value);
	}
	bool TryMarking(LFNODE* old_node, bool new_removed)
	{
		int old_value, new_value;
		old_value = reinterpret_cast<int>(old_node);
		old_value = old_value & 0xFFFFFFFE;
		new_value = old_value;
		if (true == new_removed) new_value = new_value | 0x01;
		return atomic_compare_exchange_strong(
			reinterpret_cast<atomic_int*>(&value), &old_value, new_value);
	}
	bool IsRemoved()
	{
		return 0x1 == (value & 0x1);
	}
};
class LFNODE {

public:
	int key;
	MPTR next;

	LFNODE() { next.set(nullptr, false); }
	LFNODE(int key_value) {
		next.set(nullptr, false);
		key = key_value;
	}
	~LFNODE() {}
};

class LFLIST {
	LFNODE head, tail;
	LFNODE* freelist;
	LFNODE freetail;
	mutex fl_mutex;
public:
	LFLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.next.set(&tail, false);
		freetail.key = 0x7FFFFFFF;
		freelist = &freetail;
	}
	~LFLIST() {}

	void Init()
	{
		LFNODE* ptr;
		while (head.next.getptr() != &tail) {
			ptr = head.next.getptr();
			head.next = head.next.getptr()->next;
			delete ptr;
		}
	}

	void recycle_freelist()
	{
		LFNODE* p = freelist;
		while (p != &freetail) {
			LFNODE* n = p->next.getptr();
			delete p;
			p = n;
		}
		freelist = &freetail;
	}

	void find(int key, LFNODE* (&pred), LFNODE* (&curr))
	{
	retry:
		pred = &head;
		curr = pred->next.getptr();
		while (true) {
			bool removed;
			LFNODE* succ = curr->next.getptr(&removed);
			while (true == removed) {
				if (false == pred->next.CAS(curr, succ, false, false))
					goto retry;
				curr = succ;
				succ = curr->next.getptr(&removed);
			}
			if (curr->key >= key) return;
			pred = curr;
			curr = curr->next.getptr();
		}
	}

	bool Add(int key)
	{
		LFNODE* pred, * curr;

		while (true) {

			find(key, pred, curr);

			if (key == curr->key) {
				return false;
			}
			else {
				LFNODE* node = new LFNODE(key);
				node->next.set(curr, false);
				if (false == pred->next.CAS(curr, node, false, false)) continue;
				return true;
			}
		}
	}

	bool Remove(int key)
	{
		LFNODE* pred, * curr;
		while (true) {
			find(key, pred, curr);
			if (key == curr->key) {
				LFNODE* succ = curr->next.getptr();
				if (false == curr->next.TryMarking(succ, true)) continue;
				pred->next.CAS(curr, succ, false, false);
				return true;
			}
			else {
				return false;
			}
		}
	}
	bool Contains(int key)
	{
		LFNODE* curr;
		curr = head.next.getptr();
		while (curr->key < key) {
			curr = curr->next.getptr();
		}
		return (key == curr->key) && (false == curr->next.IsRemoved());
	}

	void display20()
	{
		int c = 20;
		LFNODE* p = head.next.getptr();
		while (p != &tail)
		{
			cout << p->key << ", ";
			p = p->next.getptr();
			c--;
			if (c == 0) break;
		}
		cout << endl;
	}
};

constexpr int MAXHEIGHT = 10;
class SLNODE {
public:
	int key;
	SLNODE* next[MAXHEIGHT];
	int height;
	SLNODE(int x, int h)
	{
		key = x;
		height = h;
		for (auto& p : next) p = nullptr;
	}
	SLNODE(int x)
	{
		key = x;
		height = MAXHEIGHT;
		for (auto& p : next) p = nullptr;
	}
	SLNODE()
	{
		key = 0;
		height = MAXHEIGHT;
		for (auto& p : next) p = nullptr;
	}
};

class SKLIST {
	SLNODE head, tail;
	mutex glock;
public:
	SKLIST()
	{
		head.key = 0x80000000;
		tail.key = 0x7FFFFFFF;
		head.height = tail.height = MAXHEIGHT;
		for (auto& p : head.next) p = &tail;
	}
	~SKLIST() {
		Init();
	}

	void Init()
	{
		SLNODE* ptr;
		while (head.next[0] != &tail) {
			ptr = head.next[0];
			head.next[0] = head.next[0]->next[0];
			delete ptr;
		}
		for (auto& p : head.next) p = &tail;
	}
	void Find(int key, SLNODE* preds[MAXHEIGHT], SLNODE* currs[MAXHEIGHT])
	{
		int cl = MAXHEIGHT - 1;
		while (true) {
			if (MAXHEIGHT - 1 == cl)
				preds[cl] = &head;
			else preds[cl] = preds[cl + 1];
			currs[cl] = preds[cl]->next[cl];
			while (currs[cl]->key < key) {
				preds[cl] = currs[cl];
				currs[cl] = currs[cl]->next[cl];
			}
			if (0 == cl) return;
			cl--;
		}
	}

	bool Add(int key)
	{
		SLNODE* preds[MAXHEIGHT], * currs[MAXHEIGHT];

		glock.lock();
		Find(key, preds, currs);

		if (key == currs[0]->key) {
			glock.unlock();
			return false;
		}
		else {
			int height = 1;
			while (rand() % 2 == 0) {
				height++;
				if (MAXHEIGHT == height) break;
			}
			SLNODE* node = new SLNODE(key, height);
			for (int i = 0; i < height; ++i) {
				preds[i]->next[i] = node;
				node->next[i] = currs[i];
			}

			glock.unlock();
			return true;
		}
	}
	bool Remove(int key)
	{
		SLNODE* preds[MAXHEIGHT], * currs[MAXHEIGHT];

		glock.lock();
		Find(key, preds, currs);

		if (key == currs[0]->key) {
			for (int i = 0; i < currs[0]->height; ++i) {
				preds[i]->next[i] = currs[i]->next[i];
			}
			delete currs[0];
			glock.unlock();
			return true;
		}
		else {
			glock.unlock();
			return false;
		}
	}
	bool Contains(int key)
	{
		SLNODE* preds[MAXHEIGHT], * currs[MAXHEIGHT];
		glock.lock();
		Find(key, preds, currs);
		if (key == currs[0]->key) {
			glock.unlock();
			return true;
		}
		else {
			glock.unlock();
			return false;
		}
	}

	void display20()
	{
		int c = 20;
		SLNODE* p = head.next[0];
		while (p != &tail)
		{
			cout << p->key << ", ";
			p = p->next[0];
			c--;
			if (c == 0) break;
		}
		cout << endl;
	}
};


const auto NUM_TEST = 40000;
const auto KEY_RANGE = 1000;
SPZLIST list;
void ThreadFunc(int num_thread)
{
	int key;

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
}

int main()
{
	for (auto n = 1; n <= 16; n *= 2) {
		list.Init();
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
	}
	system("pause");
}


