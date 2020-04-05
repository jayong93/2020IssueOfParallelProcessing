#pragma once
#include <atomic>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <cstdio>

template <typename T>
struct HazardPtr {
	bool is_active() const { return active.load(std::memory_order_relaxed); }
	bool activate() { bool origin = false; return active.compare_exchange_strong(origin, true); }
	void deactivate() { active.store(false, std::memory_order_relaxed); clear(); }
	void clear() { hazard.store(nullptr, std::memory_order_relaxed); }

	void set_hp(T* ptr) { hazard.store(ptr, std::memory_order_relaxed); }
	T* get_hp() const { return hazard.load(std::memory_order_relaxed); }

	HazardPtr<T>* next{ nullptr };
	std::atomic_bool active{ true };

private:
	std::atomic<T*> hazard{ nullptr };
};

template <typename T>
struct HazardPtrGuard {
	HazardPtrGuard<T>() : ptr{ nullptr } {}
	HazardPtrGuard<T>(HazardPtr<T>* ptr) : ptr{ ptr } {}
	HazardPtrGuard<T>(const HazardPtrGuard<T>&) = delete;
	HazardPtrGuard<T>& operator=(const HazardPtrGuard<T>&) = delete;
	HazardPtrGuard<T>(HazardPtrGuard<T>&& other) : ptr{ other.ptr } {
		other.ptr = nullptr;
	}
	HazardPtrGuard<T>& operator=(HazardPtrGuard<T>&& other) {
		this->reset();
		this->ptr = other.ptr;
		other.ptr = nullptr;
		return *this;
	}
	~HazardPtrGuard<T>() { if (ptr != nullptr) ptr->deactivate(); }
	HazardPtr<T>& operator*() { return *ptr; }
	HazardPtr<T>* operator->() { return ptr; }

	void reset() {
		if (ptr != nullptr) {
			ptr->deactivate();
			ptr = nullptr;
		}
	}

private:
	HazardPtr<T>* ptr;
};

template <typename T>
struct HazardPtrList {
public:
	HazardPtr<T>* acquire();
	void release(HazardPtr<T>* h_ptr);
	void scan(std::vector<T*>& retired_list);
	HazardPtrGuard<T> acq_guard() { return HazardPtrGuard<T>{ this->acquire() }; }
	void clear() {
		while (head != nullptr) {
			auto next = head->next;
			delete head;
			head = next;
		}
		size.store(0, std::memory_order_relaxed);
	}

	size_t remove_threshold() const { return /*size.load(std::memory_order_relaxed) * (100 / sizeof(T) + 1)*/0; }
	void retire(T* node, std::vector<T*>& retired_list);

	HazardPtr<T>* head{ nullptr };
	std::atomic_size_t size{ 0 };
};

template<typename T>
inline HazardPtr<T>* HazardPtrList<T>::acquire()
{
	for (HazardPtr<T>* ptr = head; ptr != nullptr; ptr = ptr->next) {
		if (ptr->is_active()) continue;
		if (!ptr->activate()) continue;
		return ptr;
	}

	size.fetch_add(1);
	HazardPtr<T>* old_head;
	HazardPtr<T>* new_h_ptr = new HazardPtr<T>;
	auto atomic_head = reinterpret_cast<std::atomic<HazardPtr<T>*>*>(&head);
	do {
		old_head = head;
		new_h_ptr->next = old_head;
	} while (!atomic_head->compare_exchange_strong(old_head, new_h_ptr));

	return new_h_ptr;
}

template<typename T>
inline void HazardPtrList<T>::release(HazardPtr<T>* h_ptr)
{
	if (h_ptr != nullptr) h_ptr->deactivate();
}

template<typename T>
inline void HazardPtrList<T>::scan(std::vector<T*>& retired_list)
{
	std::unordered_set<T*> hazard_pointers;
	hazard_pointers.reserve(this->size.load(std::memory_order_relaxed));
	for (HazardPtr<T>* ptr = head; ptr != nullptr; ptr = ptr->next) {
		T* hp{ ptr->get_hp() };
		if (hp != nullptr) hazard_pointers.emplace(hp);
	}

	auto it_to_remove = std::remove_if(retired_list.begin(), retired_list.end(), [&](auto hp) {
		if (hazard_pointers.find(hp) == hazard_pointers.end()) {
			//printf("INFO: Node has been deleted\n");
			delete hp;
			return true;
		}
		return false;
		});
	retired_list.erase(it_to_remove, retired_list.end());
}

template<typename T>
inline void HazardPtrList<T>::retire(T* node, std::vector<T*>& retired_list)
{
	retired_list.push_back(node);
	if (retired_list.size() > this->remove_threshold()) {
		this->scan(retired_list);
	}
}
