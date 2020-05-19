#ifndef C2A0BB25_4014_4864_ABBB_67EF5B019AB0
#define C2A0BB25_4014_4864_ABBB_67EF5B019AB0

#include <utility>
#include <numa.h>

template <typename T, typename... Vals>
T *NUMA_alloc(unsigned numa_id, Vals &&... val)
{
    void *raw_ptr = numa_alloc_onnode(sizeof(T), numa_id);
    T *ptr = new (raw_ptr) T(std::forward<Vals>(val)...);
    return ptr;
}

template <typename T, typename... Vals>
T *NUMA_alloc_local(Vals &&... val)
{
    void *raw_ptr = numa_alloc_local(sizeof(T));
    T *ptr = new (raw_ptr) T(std::forward<Vals>(val)...);
    return ptr;
}

template <typename T>
void NUMA_dealloc(T *ptr)
{
    ptr->~T();
    numa_free(ptr, sizeof(T));
}

template <typename T>
struct DeallocNUMA
{
	void operator()(T *ptr)
	{
		NUMA_dealloc(ptr);
	}
};

#endif /* C2A0BB25_4014_4864_ABBB_67EF5B019AB0 */
