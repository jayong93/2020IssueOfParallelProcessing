#define  _ENABLE_ATOMIC_ALIGNMENT_FIX
#include <thread>
#include <memory>
#include <atomic>
#include <iostream>
#include "htm_shared_ptr.h"

using namespace std;

htm_shared_ptr<int> g_ptr;

void thread1()
{
	while (true) {
		htm_shared_ptr<int> temp;
		temp = g_ptr;           
		if (1 != *temp) cout << "Error\n";
	}
}

int main()
{
	g_ptr = make_htm_shared<int>(1);
	thread th{ thread1 };
	while (true) {
		auto t_ptr = make_htm_shared<int>(1);
		g_ptr = t_ptr;
		cout << ".";
		//auto ptr2 = make_shared<int>(2);
	}
}