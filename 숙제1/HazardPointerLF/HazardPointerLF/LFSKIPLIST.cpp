#include <mutex>
#include <thread>
#include <iostream>
#include <chrono>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <tuple>
#include <array>
#include "hazard_ptr.h"

using namespace std;
using namespace chrono;

static const int NUM_TEST = 4000000;
static const int RANGE = 1000;
static const int MAX_LEVEL = 10;

class LFSKNode;

bool Marked(LFSKNode* curr)
{
	int add = reinterpret_cast<int>(curr);
	return ((add & 0x1) == 0x1);
}

LFSKNode* GetReference(LFSKNode* curr)
{
	int addr = reinterpret_cast<int>(curr);
	return reinterpret_cast<LFSKNode*>(addr & 0xFFFFFFFE);
}

LFSKNode* Get(LFSKNode* curr, bool* marked)
{
	int addr = reinterpret_cast<int>(curr);
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
	LFSKNode* next[MAX_LEVEL];
	int topLevel;
	atomic_uint ref_count;

	// ���ʳ�忡 ���� ������
	LFSKNode() : ref_count{ MAX_LEVEL }
	{
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
	}
	LFSKNode(int myKey) : ref_count{ MAX_LEVEL }
	{
		key = myKey;
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
	}

	// �Ϲݳ�忡 ���� ������
	LFSKNode(int x, int height) : ref_count{ height + 1 }
	{
		key = x;
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = height;
	}

	void InitNode()
	{
		key = 0;
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = MAX_LEVEL;
		ref_count.store(MAX_LEVEL, memory_order_relaxed);
	}

	void InitNode(int x, int top)
	{
		key = x;
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			next[i] = AtomicMarkableReference(NULL, false);
		}
		topLevel = top;
		ref_count.store(top + 1, memory_order_relaxed);
	}

	bool CompareAndSet(int level, LFSKNode* old_node, LFSKNode* next_node, bool old_mark, bool next_mark)
	{
		int old_addr = reinterpret_cast<int>(old_node);
		if (old_mark)
			old_addr = old_addr | 0x1;
		else
			old_addr = old_addr & 0xFFFFFFFE;
		int next_addr = reinterpret_cast<int>(next_node);
		if (next_mark)
			next_addr = next_addr | 0x1;
		else
			next_addr = next_addr & 0xFFFFFFFE;
		return atomic_compare_exchange_strong(reinterpret_cast<atomic_int*>(&next[level]), &old_addr, next_addr);
		//int prev_addr = InterlockedCompareExchange(reinterpret_cast<long *>(&next[level]), next_addr, old_addr);
		//return (prev_addr == old_addr);
	}
};

thread_local vector<LFSKNode*> retired_list;
HazardPtrList<LFSKNode> hp_list;
using HP = HazardPtrGuard<LFSKNode>;
thread_local array<HP, MAX_LEVEL> level_pred_hps;
thread_local array<HP, MAX_LEVEL> level_succ_hps;
thread_local array<HP, 3> local_hps;

class LFSKSET
{
public:
	LFSKNode* head;
	LFSKNode* tail;

	LFSKSET()
	{
		head = new LFSKNode(0x80000000);
		tail = new LFSKNode(0x7FFFFFFF);
		for (int i = 0; i < MAX_LEVEL; i++)
		{
			head->next[i] = AtomicMarkableReference(tail, false);
		}
	}

	void Init()
	{
		LFSKNode* curr = head->next[0];
		while (curr != tail)
		{
			LFSKNode* temp = curr;
			curr = GetReference(curr->next[0]);
			delete temp;
		}
		for (int i = 0; i < MAX_LEVEL; i++)
		{
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
			local_hps[0]->set_hp(head);
			for (int level = MAX_LEVEL - 1; level >= bottomLevel; level--) {
				while (true) {
					do {
						curr = GetReference(pred->next[level]);
						local_hps[1]->set_hp(curr);
					} while (curr != GetReference(pred->next[level]));

					if (true == Marked(pred->next[level])) {
						goto retry;
					}

					do {
						succ = curr->next[level];
						local_hps[2]->set_hp(GetReference(succ));
					} while (curr->next[level] != succ);

					while (Marked(succ)) { //ǥ�õǾ��ٸ� ����
						snip = pred->CompareAndSet(level, curr, succ, false, false);
						if (!snip)
							goto retry;
						//	if (level == bottomLevel) freelist.free(curr);
						int ref_count = curr->ref_count.fetch_sub(1, memory_order_relaxed);
						if (ref_count == 1) {
							hp_list.retire(curr, retired_list);
						}
						else if (ref_count == 0) {
							fprintf(stderr, "Node's ref count was 0\n");
							exit(-1);
						}

						curr = GetReference(succ);
						swap(local_hps[1], local_hps[2]);

						do {
							succ = curr->next[level];
							local_hps[2]->set_hp(GetReference(succ));
						} while (curr->next[level] != succ);
					}

					// ǥ�� ���� ���� ���
					// Ű���� ���� ����� Ű������ �۴ٸ� pred����
					if (curr->key < x)
					{
						pred = curr;
						swap(local_hps[0], local_hps[1]);

						// Ű���� �׷��� ���� ���
						// currŰ�� ���Ű���� ���ų� ū���̹Ƿ� pred�� Ű����
						// ��� ����� �ٷ� �� ��尡 �ȴ�.
					}
					else
					{
						break;
					}
				}

				if (preds != nullptr) {
					preds[level] = pred;
					level_pred_hps[level]->set_hp(pred);
				}

				if (succs != nullptr) {
					succs[level] = curr;
					level_succ_hps[level]->set_hp(succ);
				}
			}
			return curr->key == x;
		}
	}

	bool Add(int x)
	{
		int topLevel = 0;
		while ((rand() % 2) == 1)
		{
			topLevel++;
			if (topLevel >= MAX_LEVEL - 1)
				break;
		}

		int bottomLevel = 0;
		LFSKNode* preds[MAX_LEVEL];
		LFSKNode* succs[MAX_LEVEL];
		LFSKNode* newNode = new LFSKNode;

		auto hp_new = hp_list.acq_guard();
		hp_new->set_hp(newNode);

		while (true)
		{
			auto found = Find(x, preds, succs);
			// ��� Ű�� ���� ǥ�õ��� ���� ��带 ã���� Ű�� �̹� ���տ� �����Ƿ� false ��ȯ
			if (found)
			{
				delete newNode;
				return false;
			}
			else
			{
				newNode->InitNode(x, topLevel);

				//for (int level = bottomLevel; level <= topLevel; level++)
				//{
				//	LFSKNode* succ = succs[level];
				//	// ���� ������� next�� ǥ�õ��� ���� ����, find()�� ��ȯ�� ��带 ����
				//	newNode->next[level] = Set(succ, false);
				//}

				//find���� ��ȯ�� pred�� succ�� ���� �������� ���� ����
				LFSKNode* pred = preds[bottomLevel];
				LFSKNode* succ = succs[bottomLevel];

				newNode->next[bottomLevel] = Set(succ, false);

				//pred->next�� ���� succ�� ����Ű�� �ִ��� �ʾҴ��� Ȯ���ϰ� newNode�� ��������
				if (!pred->CompareAndSet(bottomLevel, succ, newNode, false, false))
					// �����ϰ��� next���� ����Ǿ����Ƿ� �ٽ� ȣ���� ����
					continue;

				for (int level = bottomLevel + 1; level <= topLevel; level++)
				{
					while (true)
					{
						pred = GetReference(preds[level]);
						succ = GetReference(succs[level]);
						// ������ ���� ���� ������ ���ʴ�� ����
						// ������ �����Ұ�� �����ܰ�� �Ѿ��
						while (true) {
							bool mark;
							LFSKNode* t = Get(newNode->next[level], &mark);
							if (true == newNode->CompareAndSet(level, t, succ, mark, mark)) break;
						}
						if (pred->CompareAndSet(level, succ, newNode, false, false)) break;
						Find(x, preds, succs);
					}
				}

				//��� ������ ����Ǿ����� true��ȯ
				return true;
			}
		}
	}

	bool Remove(int x)
	{
		int bottomLevel = 0;
		LFSKNode* preds[MAX_LEVEL];
		LFSKNode* succs[MAX_LEVEL];
		LFSKNode* succ;

		while (true)
		{
			auto found = Find(x, preds, succs);
			if (!found)
			{
				//�������� �����Ϸ��� ��尡 ���ų�, ¦�� �´� Ű�� ���� ��尡 ǥ�� �Ǿ� �ִٸ� false��ȯ
				return false;
			}
			else
			{
				LFSKNode* nodeToRemove = succs[bottomLevel];
				//�������� ������ ��� ����� next�� mark�� �а� AttemptMark�� �̿��Ͽ� ���ῡ ǥ��
				for (int level = nodeToRemove->topLevel; level >= bottomLevel + 1; level--)
				{
					succ = nodeToRemove->next[level];
					// ���� ������ ǥ�õǾ������� �޼���� ���������� �̵�
					// �׷��� ���� ��� �ٸ� �����尡 ������ �޴ٴ� ���̹Ƿ� ���� ���� ������ �ٽ� �а�
					// ���ῡ �ٽ� ǥ���Ϸ��� �õ��Ѵ�.
					while (!Marked(succ))
					{
						nodeToRemove->CompareAndSet(level, succ, succ, false, true);
						succ = nodeToRemove->next[level];
					}
				}
				//�̺κп� �Դٴ� ���� �������� ������ ��� ���� ǥ���ߴٴ� �ǹ�

				bool marked = false;
				succ = nodeToRemove->next[bottomLevel];
				while (true)
				{
					//�������� next������ ǥ���ϰ� ���������� Remove()�Ϸ�
					bool iMarkedIt = nodeToRemove->CompareAndSet(bottomLevel, succ, succ, false, true);
					succ = succs[bottomLevel]->next[bottomLevel];

					if (iMarkedIt)
					{
						Find(x, nullptr, nullptr);
						return true;
					}
					else if (Marked(succ))
						return false;
				}
			}
		}
	}

	bool Contains(int x)
	{
		int bottomLevel = 0;
		bool marked = false;
		LFSKNode* pred = head;
		LFSKNode* curr = NULL;
		LFSKNode* succ = NULL;
		local_hps[0]->set_hp(pred);

		for (int level = MAX_LEVEL - 1; level >= bottomLevel; level--) {
			do {
				curr = GetReference(pred->next[level]);
				local_hps[1]->set_hp(curr);
			} while (curr != GetReference(pred->next[level]));
			if (true == Marked(pred->next[level])) {
				return Find(x, nullptr, nullptr);
			}

			while (true) {
				do {
					succ = curr->next[level];
					local_hps[2]->set_hp(GetReference(succ));
				} while (succ != curr->next[level]);

				if (true == Marked(succ)) {
					return Find(x, nullptr, nullptr);
				}

				if (curr->key < x) {
					pred = curr;
					swap(local_hps[0], local_hps[1]);
					curr = GetReference(succ);
					swap(local_hps[1], local_hps[2]);
				}
				else {
					break;
				}
			}
		}
		return (curr->key == x);
	}
	void Dump()
	{
		LFSKNode* curr = head;
		printf("First 20 entries are : ");
		for (int i = 0; i < 20; ++i)
		{
			curr = curr->next[0];
			if (NULL == curr)
				break;
			printf("%d(%d), ", curr->key, curr->topLevel);
		}
		printf("\n");
	}
};

LFSKSET my_set;

void benchmark(int num_thread)
{
	for (auto& hp : local_hps) {
		hp = hp_list.acq_guard();
	}
	for (auto& hp : level_pred_hps) {
		hp = hp_list.acq_guard();
	}
	for (auto& hp : level_succ_hps) {
		hp = hp_list.acq_guard();
	}

	for (int i = 0; i < NUM_TEST / num_thread; ++i)
	{
		//	if (0 == i % 100000) cout << ".";
		switch (rand() % 3)
		{
		case 0:
			my_set.Add(rand() % RANGE);
			break;
		case 1:
			my_set.Remove(rand() % RANGE);
			break;
		case 2:
			my_set.Contains(rand() % RANGE);
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
	for (int num_thread = 1; num_thread <= 32; num_thread *= 2)
	{
		my_set.Init();
		worker.clear();

		auto start_t = high_resolution_clock::now();
		for (int i = 0; i < num_thread; ++i)
			worker.push_back(thread{ benchmark, num_thread });
		for (auto& th : worker)
			th.join();
		auto du = high_resolution_clock::now() - start_t;
		my_set.Dump();

		cout << num_thread << " Threads,  Time = ";
		cout << duration_cast<milliseconds>(du).count() << " ms" << endl;
	}
}
