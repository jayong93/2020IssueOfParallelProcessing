#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <mutex>
#include <vector>

using namespace std;
using namespace std::chrono;

constexpr unsigned MAX_THREAD = 32;

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

struct EpochNode {
	NODE* ptr;
	unsigned long long epoch;

	EpochNode(NODE* ptr, unsigned long long epoch) : ptr{ ptr }, epoch{ epoch } {}
};

atomic_ullong g_epoch;
atomic_ullong* t_epochs[MAX_THREAD];
thread_local vector<EpochNode> retired_list;
thread_local unsigned tid;
thread_local unsigned counter;
constexpr unsigned epoch_freq = 20;
constexpr unsigned empty_freq = 10;

void retire(NODE* node) {
	retired_list.emplace_back(node, g_epoch.load(memory_order_relaxed));
	++counter;
	if (counter % epoch_freq == 0) {
		g_epoch.fetch_add(1, memory_order_relaxed);
	}
	if (counter % empty_freq == 0) {
		auto min_epoch = ULLONG_MAX;
		for (auto& epoch : t_epochs) {
			auto e = epoch->load(memory_order_relaxed);
			if (min_epoch > e) {
				min_epoch = e;
			}
		}

		auto removed_it = remove_if(retired_list.begin(), retired_list.end(), [min_epoch](auto& r_node) {
			if (r_node.epoch < min_epoch) {
				delete r_node.ptr;
				return true;
			}
			return false;
			});
		retired_list.erase(removed_it, retired_list.end());
	}
}

void start_op() {
	t_epochs[tid]->store(g_epoch.load(memory_order_relaxed), memory_order_relaxed);
}

void end_op() {
	t_epochs[tid]->store(ULLONG_MAX, memory_order_relaxed);
}


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
		start_op();
		NODE* e = new NODE(key);
		while (true) {
			NODE* last = tail;
			NODE* next = last->next;
			if (last != tail) continue;
			if (next != nullptr) {
				CAS(&tail, last, next);
				continue;
			}
			if (false == CAS(&last->next, nullptr, e)) continue;
			CAS(&tail, last, e);
			end_op();
			return;
		}
	}
	int Deq()
	{
		start_op();
		while (true) {
			NODE* first = head;
			NODE* next = first->next;
			NODE* last = tail;
			NODE* lastnext = last->next;
			if (first != head) continue;
			if (last == first) {
				if (lastnext == nullptr) {
					//cout << "EMPTY!!!\n";
					//this_thread::sleep_for(1ms);
					end_op();
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
			retire(first);
			end_op();
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
void ThreadFunc(int num_thread, int thread_id)
{
	tid = thread_id;
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
	for (auto& epoch : t_epochs) {
		epoch = new atomic_ullong{ ULLONG_MAX };
	}
	for (auto n = 1; n <= MAX_THREAD; n *= 2) {
		my_queue.Init();
		vector <thread> threads;
		auto s = high_resolution_clock::now();
		for (int i = 0; i < n; ++i)
			threads.emplace_back(ThreadFunc, n, i);
		for (auto& th : threads) th.join();
		auto d = high_resolution_clock::now() - s;
		my_queue.display20();
		//my_queue.recycle_freelist();
		cout << n << "Threads,  ";
		cout << ",  Duration : " << duration_cast<milliseconds>(d).count() << " msecs.\n";
	}
}
