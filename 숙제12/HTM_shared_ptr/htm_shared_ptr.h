#pragma once
#include <mutex>
#include <memory>
#include <immintrin.h>

using namespace std;

template <class T>

struct htm_shared_ptr {
private:
	shared_ptr<T> m_ptr;
public:
	bool is_lock_free() const noexcept
	{
		return false;
	}

	void store(shared_ptr<T> sptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = sptr;
		_xend();
	}


	shared_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		shared_ptr<T> t = m_ptr;
		_xend();
		return t;
	}

	operator shared_ptr<T>() const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		shared_ptr<T> t = m_ptr;
		_xend();
		return t;
	}

	shared_ptr<T> exchange(shared_ptr<T> sptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		shared_ptr<T> t = m_ptr;
		m_ptr = sptr;
		_xend();
		return t;
	}

	bool compare_exchange_strong(shared_ptr<T>& expected_sptr, shared_ptr<T> new_sptr, memory_order, memory_order) noexcept
	{
		bool success = false;
		while (_XBEGIN_STARTED != _xbegin());
		shared_ptr<T> t = m_ptr;
		if (m_ptr.get() == expected_sptr.get()) {
			m_ptr = new_sptr;
			success = true;
		}
		expected_sptr = m_ptr;
		_xend();
	}

	bool compare_exchange_weak(shared_ptr<T>& expected_sptr, shared_ptr<T> target_sptr, memory_order, memory_order) noexcept
	{
		return compare_exchange_strong(expected_sptr, target_sptr, memory_order);
	}

	htm_shared_ptr() noexcept = default;

	constexpr htm_shared_ptr(shared_ptr<T> sptr) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = sptr;
		_xend();
	}
	//		htm_shared_ptr(const htm_shared_ptr&) = delete;
	//		htm_shared_ptr& operator=(const htm_shared_ptr&) = delete;
	shared_ptr<T> operator=(shared_ptr<T> sptr) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = sptr;
		_xend();
		return sptr;
	}

	void reset()
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = nullptr;
		_xend();
	}
	T* operator ->()
	{
		while (_XBEGIN_STARTED != _xbegin());
		T* p = m_ptr.get();
		_xend();
		return p;
		//			return m_ptr.get();
	}


	htm_shared_ptr(const htm_shared_ptr& rhs)
	{
		store(rhs);
	}
	htm_shared_ptr& operator=(const htm_shared_ptr& rhs)
	{
		store(rhs);
		return *this;
	}
	template< typename TargetType >
	inline bool operator ==(shared_ptr< TargetType > const& rhs)
	{
		return load() == rhs;
	}
};

template <class T> struct atomic_weak_ptr {
private:
	mutable mutex m_lock;
	weak_ptr<T> m_ptr;
public:
	bool is_lock_free() const noexcept
	{
		return false;
	}
	void store(weak_ptr<T> wptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = wptr;
		_xend();
	}
	weak_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		weak_ptr<T> t = m_ptr;
		_xend();
		return t;
	}
	operator weak_ptr<T>() const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		weak_ptr<T> t = m_ptr;
		_xend();
		return t;
	}
	weak_ptr<T> exchange(weak_ptr<T> wptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		weak_ptr<T> t = m_ptr;
		m_ptr = wptr;
		_xend();
		return t;
	}

	bool compare_exchange_strong(weak_ptr<T>& expected_wptr, weak_ptr<T> new_wptr, memory_order, memory_order) noexcept
	{
		bool success = false;
		lock_guard(m_lock);

		weak_ptr<T> t = m_ptr;
		shared_ptr<T> my_ptr = t.lock();
		if (!my_ptr) return false;
		shared_ptr<T> expected_sptr = expected_wptr.lock();
		if (expected_sptr) return false;

		if (my_ptr.get() == expected_sptr.get()) {
			success = true;
			m_ptr = new_wptr;
		}
		expected_wptr = t;
		return success;
	}

	bool compare_exchange_weak(weak_ptr<T>& exptected_wptr, weak_ptr<T> new_wptr, memory_order, memory_order) noexcept
	{
		return compare_exchange_strong(exptected_wptr, new_wptr, memory_order);
	}

	atomic_weak_ptr() noexcept = default;

	constexpr atomic_weak_ptr(weak_ptr<T> wptr) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = wptr;
		_xend();
	}

	atomic_weak_ptr(const atomic_weak_ptr&) = delete;
	atomic_weak_ptr& operator=(const atomic_weak_ptr&) = delete;
	weak_ptr<T> operator=(weak_ptr<T> wptr) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr = wptr;
		_xend();
		return wptr;
	}
	shared_ptr<T> lock() const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		shared_ptr<T> sptr = m_ptr.lock();
		_xend();
		return sptr;
	}
	void reset()
	{
		while (_XBEGIN_STARTED != _xbegin());
		m_ptr.reset();
		_xend();
	}
};