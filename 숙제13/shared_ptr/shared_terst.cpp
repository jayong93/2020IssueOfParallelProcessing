#define _ENABLE_ATOMIC_ALIGNMENT_FIX

#include <atomic>
#include <memory>
#include <iostream>
#include <functional>
#include <vector>

#include "atomic_shared_ptr.hpp"

struct big {
	int a;
	int b;
	int c;
	int d;
	int e;
};

int a;

using namespace std;

class LifeTimeRef;

class Lifetime
{
public:
	Lifetime() {
		cout << "LIFETIME CREATED\n";
	}
	Lifetime(const Lifetime& lifetime) {
		cout << "CLONING\n";
	}
	~Lifetime() {
		cout << "LIFETIME DESTRUCTED\n";
	}
	void increasecounter()
	{
		cout << "Counter Increased\n";
	}
	void decreasecounter()
	{
		cout << "Counter Decreased\n";
	}
	shared_ptr<LifeTimeRef> StartRefer()
	{
		return make_shared<LifeTimeRef>(*this);
	}
};

class LifeTimeRef
{
	Lifetime &m_lf;
	friend LifeTimeRef;
public:
	LifeTimeRef(Lifetime& lf) : m_lf(lf)
	{
		m_lf.increasecounter();
		cout << "REF Constructed\n";
	}

	LifeTimeRef(const LifeTimeRef& lfr) : m_lf(lfr.m_lf)
	{
		cout << "REF Copied\n";
	}

	~LifeTimeRef() {
		m_lf.decreasecounter();
		cout << "REF destroyed\n";
	}
};

std::function <void()> todo;

void CompilerOptimizationTest()
{
	Lifetime *a = new Lifetime;
	auto lifeptr = a->StartRefer();

	auto task = [lifeptr] { cout << "I am task\n";  };
	todo = task;
	cout << "End of task generating Function\n";
}

void todo2()
{
	todo();
	todo = nullptr;
	cout << "End of Calling Function\n";
}

class StaticTest
{
private:
	StaticTest() {}
public:
	static bool m_testbool;
};



void *room_ptr, *world_ptr, *pvpworld_ptr;

class Room : public jss::enable_shared_from_this<Room>
{
public:
	Room() { cout << "Room Created\n"; room_ptr = this; }
	virtual ~Room() { cout << "Room Destructed\n"; }
private:
	unsigned char SPACE[100 * 1024 * 1024];
};

class World : public Room
{
public:
	World() { cout << "World Created\n"; world_ptr = this; }
	virtual ~World() { cout << "World Destructed\n"; }
};

class PvPWorld : public World
{
public:
	PvPWorld() { cout << "PvP World Created\n"; pvpworld_ptr = this; }
};


bool StaticTest::m_testbool = true;

int main()
{
	// PvPWorld temp_world;;

	//int a = 3;

	//atomic <big> big_one;

	//big temp;
	//temp.a = 3;
	//temp.b = 4;

	//big_one = temp;

	////big_one.a = 3;
	////big_one.b = 4;;

	//if (a == a) cout << "a == a \n"; else cout << "a != a\n";
	//int *p, *q;
	//p = q = &a;
	//if (p == q) cout << "&a == &a \n"; else cout << "&a != &a\n";
	//shared_ptr<int> sp = make_shared<int>(a);
	//shared_ptr<int> sq = make_shared<int>(a);

	//if (sp == sq) cout << "SP_A == SP_A\n"; else cout << "SP_A != SP_A\n";
	//if (sp.get() == sq.get()) cout << "SP_A.get() == SP_A.get() \n"; else cout << "SP_A.get() != SP_A.get()\n";

	//shared_ptr<int> sr = sp;
	//if (sp == sr) cout << "SP_A == SP_A\n"; else cout << "SP_A != SP_A\n";
	//if (sp.get() == sr.get()) cout << "SP_A.get() == SP_A.get() \n"; else cout << "SP_A.get() != SP_A.get()\n";

	//CompilerOptimizationTest();
	//todo2();
	//cout << "End of Main function()\n";

	//StaticTest::m_testbool = true;

	//vector <int> ttt(10);

	//cout << "Size of ttt : " << ttt.size() << endl;

	// cout << "RoomPtr[" << (int)room_ptr << "],  WorldPtr[" << (int)world_ptr << "],  PvPWorldPtr[" << (int)pvpworld_ptr << "]\n";


	jss::shared_ptr<PvPWorld> temp_shared = jss::make_shared<PvPWorld>();
//	jss::shared_ptr<PvPWorld> temp_shared = jss::shared_ptr<PvPWorld>{ new PvPWorld };

	//jss::weak_ptr<PvPWorld> temp_weak = temp_shared;
	//cout << temp_weak.weak_use_count() << endl;

	//temp_shared = nullptr;

	//cout <<  temp_weak.weak_use_count() << endl;
	//temp_weak.reset();

	//cout << "End\n";


	cout << "START\n";
//	jss::shared_ptr<Room> RoomPtr = jss::shared_ptr<Room>{ new Room };
	jss::shared_ptr<Room> RoomPtr = jss::make_shared<Room>();
	cout << "new ROOM executed\n";
	jss::shared_ptr<Room> RoomPtr2 = RoomPtr->shared_from_this();
	cout << "shared_from_this executed!\n";

	cout << "End\n";

}