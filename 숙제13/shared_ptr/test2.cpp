#define _ENABLE_ATOMIC_ALIGNMENT_FIX
#include <thread>
#include <memory>
#include <atomic>
#include <iostream>
//#include "atomic_shared_ptr.hpp"
//#include "my_shared_atomic_ptr.h"
#include <experimental/atomic>

#define jss experimental

using namespace std;

jss::atomic_weak_ptr<atomic_int> wptr;

void thread1()
{
	int test_ok = 0;
	int test_fail = 0;

	while (true) {
		jss::shared_ptr<atomic_int> temp = wptr.load().lock();
		if (!temp) continue;
		if (7261 != *temp) 
			test_fail++;
		else test_ok++;

		if (0 == ((test_ok + test_fail) % 1000000)) cout << "Succ : " << test_ok << "    Fail : " << test_fail << endl;
	}
}

void thread2()
{
	while (true) {
		auto t_ptr = jss::make_shared<atomic_int>(7261);
		wptr = t_ptr;
		auto ptr2 = jss::make_shared<atomic_int>(2);
	}
}

int main()
{
	wptr = jss::make_shared<atomic_int>(7261);
	thread th{ thread1 };
	thread th2{ thread2 };
	th.join();
	th2.join();
}
