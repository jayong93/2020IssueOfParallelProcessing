#include <vector>
#include <atomic>
#include <utility>
#include <iostream>
#include <thread>
#include <chrono>

using namespace std;
using namespace std::chrono;

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

		Find(key, preds, currs);

		if (key == currs[0]->key) {
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

			return true;
		}
	}
	bool Remove(int key)
	{
		SLNODE* preds[MAXHEIGHT], * currs[MAXHEIGHT];

		Find(key, preds, currs);

		if (key == currs[0]->key) {
			for (int i = 0; i < currs[0]->height; ++i) {
				preds[i]->next[i] = currs[i]->next[i];
			}
			delete currs[0];
			return true;
		}
		else {
			return false;
		}
	}
	bool Contains(int key)
	{
		SLNODE* preds[MAXHEIGHT], * currs[MAXHEIGHT];
		Find(key, preds, currs);
		if (key == currs[0]->key) {
			return true;
		}
		else {
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

enum class Func {
	None,
	Add,
	Remove,
	Contains,
};

struct Invoc {
	Func func;
	int arg;

	Invoc(Func func, int arg = 0) : func{ func }, arg{ arg } {}
	Invoc(Invoc&& other) noexcept : func{ other.func }, arg{ other.arg } {}

	bool is_read_only() const {
		return func == Func::Contains;
	}
};

using Response = int;

struct Node {
	Invoc invoc;
	atomic<Node*> decide_next;
	unsigned int seq;
	atomic<Node*> next;

	Node(Invoc&& invoc) : invoc{ move(invoc) }, decide_next{ nullptr }, seq{ 0 }, next{ nullptr } {}
	static Node* max(const vector<Node*>& nodes) {
		Node* m = nodes[0];
		for (auto it = nodes.begin() + 1; it != nodes.end(); ++it) {
			if (m->seq < (*it)->seq) {
				m = *it;
			}
		}
		return m;
	}
};

struct Object {
	SKLIST container;

	Response apply(const Invoc& invoc) {
		switch (invoc.func) {
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
};

constexpr int RECYCLE_RATE = 100;

class OLFUniversal {
public:
	OLFUniversal(int capacity) : capacity{ capacity }, invoke_num{ 0 } {
		Invoc invoc{ Func::None };
		tail = new Node(move(invoc));
		tail->seq = 1;
		head.resize(capacity, tail);
		last_nodes.resize(capacity, tail);
		objects.resize(capacity);
	}
	~OLFUniversal()
	{
		Node* cur = tail->next.load(memory_order_relaxed);
		while (cur->next != nullptr) {
			Node* del = cur;
			cur = cur->next.load(memory_order_relaxed);
			delete del;
		}
		delete cur;
		delete tail;
	}

	Object& current_obj() {
		Node* m = head[0];
		int thread_id = 0;
		int index = 1;
		for (auto it = head.begin() + 1; it != head.end(); ++it, ++index) {
			if (m->seq < (*it)->seq) {
				m = *it;
				thread_id = index;
			}
		}
		return objects[thread_id];
	}

	Response apply(Invoc&& invoc, int thread_id) {
		if (invoc.is_read_only()) {
			return do_read_only(move(invoc), thread_id);
		}

		Node* prefer = new Node(move(invoc));
		while (prefer->seq == 0) {
			Node* before = Node::max(head);
			Node* after = nullptr;
			if (before->decide_next.compare_exchange_strong(after, prefer))
				after = prefer;
			before->next.store(after, memory_order_relaxed);
			after->seq = before->seq + 1;
			head[thread_id] = after;
		}

		auto& last_node = last_nodes[thread_id];
		auto& last_obj = objects[thread_id];

		last_node = last_node->next.load(memory_order_relaxed);
		while (last_node != prefer)
		{
			last_obj.apply(last_node->invoc);
			last_node = last_node->next.load(memory_order_relaxed);
		}

		if (invoke_num.load(memory_order_relaxed) < RECYCLE_RATE) {
			if (invoke_num.fetch_add(1, memory_order_relaxed) + 1 == RECYCLE_RATE) {
				recycle();
				invoke_num.store(0, memory_order_relaxed);
			}
		}

		return last_obj.apply(last_node->invoc);
	}

	Response do_read_only(Invoc&& invoc, int thread_id) {
		auto old_head = Node::max(head);
		auto& last_node = last_nodes[thread_id];
		auto& last_obj = objects[thread_id];

		if (last_node == old_head) {
			return last_obj.apply(invoc);
		}

		last_node = last_node->next.load(memory_order_relaxed);

		while (last_node != old_head) {
			last_obj.apply(last_node->invoc);
			last_node = last_node->next.load(memory_order_relaxed);
		}
		last_obj.apply(last_node->invoc);

		return last_obj.apply(invoc);
	}

	void recycle() {
		Node* min = last_nodes[0];
		for (auto i = 1; i < last_nodes.size(); ++i) {
			if (last_nodes[i]->seq < min->seq) {
				min = last_nodes[i];
			}
		}
		for (auto i = 0; i < head.size(); ++i) {
			if (head[i]->seq < min->seq) {
				min = head[i];
			}
		}

		if (min == tail) { return; }

		auto old_next = tail->next.load(memory_order_relaxed);
		if (tail->next.compare_exchange_strong(old_next, min)) {
			while (old_next != min) {
				auto tmp = old_next;
				old_next = old_next->next.load(memory_order_relaxed);
				delete tmp;
			}
		}
	}
private:
	vector<Node*> head;
	vector<Node*> last_nodes;
	vector<Object> objects;
	Node* tail;
	int capacity;
	atomic_ullong invoke_num;
};

const auto NUM_TEST = 4000000;
const auto KEY_RANGE = 1000;
void ThreadFunc(OLFUniversal* list, int num_thread, int thread_id)
{
	int key;

	for (int i = 0; i < NUM_TEST / num_thread; i++) {
		switch (rand() % 3) {
		case 0: key = rand() % KEY_RANGE;
			list->apply(Invoc(Func::Add, key), thread_id);
			break;
		case 1: key = rand() % KEY_RANGE;
			list->apply(Invoc(Func::Remove, key), thread_id);
			break;
		case 2: key = rand() % KEY_RANGE;
			list->apply(Invoc(Func::Contains, key), thread_id);
			break;
		default: cout << "Error\n";
			exit(-1);
		}
	}
}

int main() {
	for (auto n = 1; n <= 16; n *= 2) {
		OLFUniversal list(n);

		vector <thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, &list, n, i);
		for (auto& th : threads) th.join();
		auto d = high_resolution_clock::now() - s;

		list.current_obj().container.display20();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs." << endl;
	}
	system("pause");
}