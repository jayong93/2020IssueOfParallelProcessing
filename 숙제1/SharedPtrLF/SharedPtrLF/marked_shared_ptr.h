#pragma once
#include <atomic>
#include <memory>

using namespace std;

template<typename T>
struct MarkedSharedPtr {
private:
	shared_ptr<T> ptr;
public:
	shared_ptr<T>& get_raw() const { return ptr; }
};
