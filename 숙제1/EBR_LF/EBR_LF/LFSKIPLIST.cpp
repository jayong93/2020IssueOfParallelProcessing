﻿#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>

using namespace std;
using namespace chrono;

static const int NUM_TEST = 4000000;
static const int RANGE = 1000;
static const int MAX_LEVEL = 10;
constexpr unsigned MAX_THREAD = 32;

class LFSKNode;

struct EpochNode {
	LFSKNode* ptr;
	unsigned long long epoch;

	EpochNode(LFSKNode* ptr, unsigned long long epoch) : ptr{ ptr }, epoch{ epoch } {}
};

atomic_ullong g_epoch;
atomic_ullong* t_epochs[MAX_THREAD];
thread_local vector<EpochNode> retired_list;
thread_local unsigned tid;
thread_local unsigned counter;
constexpr unsigned epoch_freq = 20;
constexpr unsigned empty_freq = 10;

void retire(LFSKNode* node) {
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

bool Marked(LFSKNode* curr)
{
	int add = reinterpret_cast<int> (curr);
	return ((add & 0x1) == 0x1);
}

LFSKNode* GetReference(LFSKNode* curr)
{
	int addr = reinterpret_cast<int> (curr);
	return reinterpret_cast<LFSKNode*>(addr & 0xFFFFFFFE);
}

LFSKNode* Get(LFSKNode* curr, bool* marked)
{
	int addr = reinterpret_cast<int> (curr);
	*marked = ((addr & 0x01) != 0);
	return reinterpret_cast<LFSKNode*>(addr & 0xFFFFFFFE);
}

LFSKNode* AtomicMarkableReference(LFSKNode* node, bool mark)
{
	int addr = reinterpret_cast<int>(node);
	if (mark)
		addr = addr | 0x1;
	else
		addr = addr & 0xFFFFFFFE;
	return reinterpret_cast<LFSKNode*>(addr);
}

LFSKNode* Set(LFSKNode* node, bool mark)
{
	int addr = reinterpret_cast<int>(node);
	if (mark)
		addr = addr | 0x1;
	else
		addr = addr & 0xFFFFFFFE;
	return reinterpret_cast<LFSKNode*>(addr);
}

class LFSKNode
{
public:
	int key;
	LFSKNode* volatile next[MAX_LEVEL];
	int topLevel;
	atomic_uint ref_count;

	// 보초노드에 관한 생성자
	LFSKNode() : ref_count{ MAX_LEVEL } {
		for (int i = 0; i < MAX_LEVEL; i++) {
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
	}
	LFSKNode(int myKey) : ref_count{ MAX_LEVEL } {
		key = myKey;
		for (int i = 0; i < MAX_LEVEL; i++) {
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
	}

	// 일반노드에 관한 생성자
	LFSKNode(int x, int height) : ref_count{ height + 1 } {
		key = x;
		for (int i = 0; i < MAX_LEVEL; i++) {
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = height;
	}

	void InitNode() {
		key = 0;
		for (int i = 0; i < MAX_LEVEL; i++) {
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
	}

	void InitNode(int x, int top) {
		key = x;
		for (int i = 0; i < MAX_LEVEL; i++) {
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = top;
		ref_count.store(top + 1, memory_order_relaxed);
	}

	bool CompareAndSet(int level, LFSKNode* old_node, LFSKNode* next_node, bool old_mark, bool next_mark) {
		int old_addr = reinterpret_cast<int>(old_node);
		if (old_mark) old_addr = old_addr | 0x1;
		else old_addr = old_addr & 0xFFFFFFFE;
		int next_addr = reinterpret_cast<int>(next_node);
		if (next_mark) next_addr = next_addr | 0x1;
		else next_addr = next_addr & 0xFFFFFFFE;
		return atomic_compare_exchange_strong((atomic_int*)(&next[level]), &old_addr, next_addr);
		//int prev_addr = InterlockedCompareExchange(reinterpret_cast<long *>(&next[level]), next_addr, old_addr);
		//return (prev_addr == old_addr);
	}
};

class LFSKSET
{
public:

	LFSKNode* head;
	LFSKNode* tail;

	LFSKSET() {
		head = new LFSKNode(0x80000000);
		tail = new LFSKNode(0x7FFFFFFF);
		for (int i = 0; i < MAX_LEVEL; i++) {
			head->next[i] = AtomicMarkableReference(tail, false);
		}
	}

	void Init()
	{
		LFSKNode* curr = head->next[0];
		while (curr != tail) {
			LFSKNode* temp = curr;
			curr = GetReference(curr->next[0]);
			delete temp;
		}
		for (int i = 0; i < MAX_LEVEL; i++) {
			head->next[i] = AtomicMarkableReference(tail, false);
		}
	}

	bool Find(int x, LFSKNode* preds[], LFSKNode* succs[])
	{
		int bottomLevel = 0;
		bool marked = false;
		bool snip;
		LFSKNode* pred = NULL;
		LFSKNode* curr = NULL;
		LFSKNode* succ = NULL;
	retry:
		while (true) {
			pred = head;
			for (int level = MAX_LEVEL - 1; level >= bottomLevel; level--) {
				curr = GetReference(pred->next[level]);
				while (true) {
					succ = curr->next[level];
					while (Marked(succ)) { //표시되었다면 제거
						snip = pred->CompareAndSet(level, curr, succ, false, false);
						if (!snip) goto retry;
						//	if (level == bottomLevel) freelist.free(curr);
						/*
						 * reference counting 방식이 아니라면 오류가 발생할 수 밖에 없다
						 * 만약
						 * 1. A thread가 x값에 대해 Add하려고 시도하면서 Find 호출 도중 level 1에서
						 * 	pred와 succ(x 값을 가진 노드)를 찾음
						 * 2. A thread가 잠시 멈춘 사이에 B thread가 x 노드에 대해 Remove를 하면서
						 * 	전부 marking 하고 정지.
						 * 3. A thread가 다시 깨어나 level 0 에서 x 노드를 찾는데, marking 된 것을
						 * 	확인하고 CAS로 자료구조에서 제거하고 retire까지 시킴
						 * 4. 그 뒤 A thread가 이어서 x 값에 대한 새로운 노드를 자료구조에 끼워 넣는데
						 * 	level 1에서는 retire된 x 노드의 포인터가 아직 남아있고, 이 주소가 새로운
						 * 	x 노드의 next로 설정됨.
						 * 5. 결과적으로 retire된 노드의 주소값이 아직 자료구조에 남은 상태로 존재하게 됨.
						 */
						int ref_count = curr->ref_count.fetch_sub(1, memory_order_relaxed);
						if (ref_count == 1) {
							retire(curr);
						}
						else if (ref_count == 0) {
							fprintf(stderr, "Node's ref count was 0\n");
							exit(-1);
						}
						curr = GetReference(pred->next[level]);
						succ = curr->next[level];
					}

					// 표시 되지 않은 경우
					// 키값이 현재 노드의 키값보다 작다면 pred전진
					if (curr->key < x) {
						pred = curr;
						curr = GetReference(succ);
						// 키값이 그렇지 않은 경우
						// curr키는 대상키보다 같거나 큰것이므로 pred의 키값이 
						// 대상 노드의 바로 앞 노드가 된다.		
					}
					else {
						break;
					}
				}
				preds[level] = pred;
				succs[level] = curr;
			}
			return (curr->key == x);
		}
	}

	bool Add(int x)
	{
		start_op();
		int topLevel = 0;
		while ((rand() % 2) == 1)
		{
			topLevel++;
			if (topLevel >= MAX_LEVEL - 1) break;
		}

		int bottomLevel = 0;
		LFSKNode* preds[MAX_LEVEL];
		LFSKNode* succs[MAX_LEVEL];
		LFSKNode* newNode = new LFSKNode;
		while (true) {
			bool found = Find(x, preds, succs);
			// 대상 키를 갖는 표시되지 않은 노드를 찾으면 키가 이미 집합에 있으므로 false 반환
			if (found) {
				end_op();
				delete newNode;
				return false;
			}
			else {
				newNode->InitNode(x, topLevel);

				for (int level = bottomLevel; level <= topLevel; level++) {
					LFSKNode* succ = succs[level];
					// 현재 새노드의 next는 표시되지 않은 상태, find()가 반환반 노드를 참조
					newNode->next[level] = Set(succ, false);
				}

				//find에서 반환한 pred와 succ의 가장 최하층을 먼저 연결
				LFSKNode* pred = preds[bottomLevel];
				LFSKNode* succ = succs[bottomLevel];

				newNode->next[bottomLevel] = Set(succ, false);

				//pred->next가 현재 succ를 가리키고 있는지 않았는지 확인하고 newNode와 참조설정
				if (!pred->CompareAndSet(bottomLevel, succ, newNode, false, false))
					// 실패일경우는 next값이 변경되었으므로 다시 호출을 시작
					continue;

				for (int level = bottomLevel + 1; level <= topLevel; level++) {
					while (true) {
						pred = GetReference(preds[level]);
						succ = GetReference(succs[level]);
						// 최하층 보다 높은 층들을 차례대로 연결
						// 연결을 성공할경우 다음단계로 넘어간다
						while (true) {
							bool mark;
							LFSKNode* t = Get(newNode->next[level], &mark);
							if (true == newNode->CompareAndSet(level, t, succ, mark, mark)) break;
						}
						if (pred->CompareAndSet(level, succ, newNode, false, false)) break;
						Find(x, preds, succs);

						/*
						  아래는 이전에 시도했던 해결법(by 소윤)인데 문제가 있다.
						  Add thread가 첫번째 CAS(marking이 됐는지 확인하는 CAS)에 성공한 직후 잠깐 멈췄을 때,
						  Remove thread가 이 노드의 모든 층에 marking하고 Find를 호출해서 retire까지 했을 수 있다.
						  그리고 다시 깨어난 Add thread가 두번째 CAS(실제 자료구조에 newNode를 삽입하는 CAS)에 성공할 수 있다.
						  이 level에서는 pred의 next가 바뀌지 않았을 수도 있기 때문.
						  그렇게 되면 뒷처리를 위해 Add thread가 Find를 호출하기 전에 다른 thread가 새로운 epoch으로
						  method를 시작해서 이미 retire된 이 노드를 볼 수 있고, 그 포인터가 local하게 저장됐을 수도 있다.
						  그러면 이미 늦어서 Add thread가 Find를 호출해서 뒷정리하고 끝내면 이 노드의 메모리는 할당 해제된다.
						  즉, 새로운 epoch으로 method를 시작한 thread는 delete된 메모리 주소를 참조할 수 있다.
						*/

						//auto new_next = newNode->next[level];
						//if (true == newNode->CompareAndSet(level, new_next, succ, false, false)) {
						//	if (true == pred->CompareAndSet(level, succ, newNode, false, false))
						//		break;
						//	Find(x, preds, succs);
						//}
						//else {
						//	Find(x, preds, succs);
						//	end_op();
						//	return true;
						//}

					}
				}

				Find(x, preds, succs);
				//모든 층에서 연결되었으면 true반환
				end_op();
				return true;
			}
		}
	}

	bool Remove(int x)
	{
		start_op();
		int bottomLevel = 0;
		LFSKNode* preds[MAX_LEVEL];
		LFSKNode* succs[MAX_LEVEL];
		LFSKNode* succ;

		while (true) {
			bool found = Find(x, preds, succs);
			if (!found) {
				//최하층에 제거하려는 노드가 없거나, 짝이 맞는 키를 갖는 노드가 표시 되어 있다면 false반환
				end_op();
				return false;
			}
			else {
				LFSKNode* nodeToRemove = succs[bottomLevel];
				//최하층을 제외한 모든 노드의 next와 mark를 읽고 AttemptMark를 이용하여 연결에 표시
				for (int level = nodeToRemove->topLevel; level >= bottomLevel + 1; level--) {
					succ = nodeToRemove->next[level];
					// 만약 연결이 표시되어있으면 메서드는 다음층으로 이동
					// 그렇지 않은 경우 다른 스레드가 병행을 햇다는 뜻이므로 현재 층의 연결을 다시 읽고
					// 연결에 다시 표시하려고 시도한다.
					while (!Marked(succ)) {
						nodeToRemove->CompareAndSet(level, succ, succ, false, true);
						succ = nodeToRemove->next[level];
					}
				}
				//이부분에 왔다는 것은 최하층을 제외한 모든 층에 표시했다는 의미

				bool marked = false;
				succ = nodeToRemove->next[bottomLevel];
				while (true) {
					//최하층의 next참조에 표시하고 성공했으면 Remove()완료
					bool iMarkedIt = nodeToRemove->CompareAndSet(bottomLevel, succ, succ, false, true);
					succ = succs[bottomLevel]->next[bottomLevel];

					if (iMarkedIt) {
						Find(x, preds, succs);
						end_op();
						return true;
					}
					else if (Marked(succ)) {
						end_op();
						return false;
					}
				}
			}
		}
	}

	bool Contains(int x)
	{
		start_op();
		int bottomLevel = 0;
		bool marked = false;
		LFSKNode* pred = head;
		LFSKNode* curr = NULL;
		LFSKNode* succ = NULL;

		for (int level = MAX_LEVEL - 1; level >= bottomLevel; level--) {
			curr = GetReference(pred->next[level]);
			while (true) {
				succ = curr->next[level];
				while (Marked(succ)) {
					curr = GetReference(curr->next[level]);
					succ = curr->next[level];
				}
				if (curr->key < x) {
					pred = curr;
					curr = GetReference(succ);
				}
				else {
					break;
				}
			}
		}
		auto retval = (curr->key == x);
		end_op();
		return retval;
	}
	void Dump()
	{
		LFSKNode* curr = head;
		printf("First 20 entries are : ");
		for (int i = 0; i < 20; ++i) {
			curr = curr->next[0];
			if (NULL == curr) break;
			printf("%d(%d), ", curr->key, curr->topLevel);
		}
		printf("\n");
	}
};

LFSKSET my_set;

void benchmark(int num_thread, int thread_id)
{
	tid = thread_id;
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
	for (auto& epoch : t_epochs) {
		epoch = new atomic_ullong{ ULLONG_MAX };
	}
	vector <thread> worker;
	for (int num_thread = 1; num_thread <= MAX_THREAD; num_thread *= 2) {
		g_epoch.store(0, memory_order_relaxed);
		for (auto& epoch : t_epochs) {
			epoch->store(ULLONG_MAX, memory_order_relaxed);
		}
		my_set.Init();
		worker.clear();

		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < num_thread; ++i)
			worker.push_back(thread{ benchmark, num_thread, i });
		for (auto& th : worker) th.join();
		auto du = high_resolution_clock::now() - start_t;
		my_set.Dump();

		cout << num_thread << " Threads,  Time = ";
		cout << duration_cast<milliseconds>(du).count() << " ms\n";
	}
}
