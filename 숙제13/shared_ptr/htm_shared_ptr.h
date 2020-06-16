#pragma once
#include <mutex>
#include <memory>
#include <immintrin.h>

using namespace std;

template <class T>
class ctr_block {
public:
	T* ptr;
	int ref_cnt;
	int weak_cnt;
};

template <class T>
class htm_shared_ptr {

private:
	template <class F, class..._Types>
	friend htm_shared_ptr<F> make_htm_shared(_Types&&... _Args);

	ctr_block<T>	*m_b_ptr;
	T* m_ptr;
public:
	bool is_lock_free() const noexcept
	{
		return true;
	}

	void store(const htm_shared_ptr<T>& sptr, memory_order = memory_order_seq_cst) noexcept
	{
		bool need_delete = false;
		T* temp_ptr = nullptr;
		ctr_block<T>* temp_b_ptr = nullptr;
		while (_XBEGIN_STARTED != _xbegin());
		if (nullptr != m_b_ptr) {
			if (m_b_ptr->ref_count == 1) {
				need_delete = true;
				temp_ptr = m_ptr;
				temp_b_ptr = m_b_ptr;
			}
			m_b_ptr->ref_count--;
		}
		if (nullptr != sptr.m_ptr) {
			sptr.m_ptr->ref_count++;
		}
		m_ptr = sptr->m_ptr;
		m_b_ptr = sptr->m_b_ptr;
		_xend();
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}

	htm_shared_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		htm_shared_ptr<T> t{ *this };
		_xend();
		return t;
	}

	htm_shared_ptr<T>& exchange(htm_shared_ptr<T> &sptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != _xbegin());
		ctr_block<T>* t_b = m_b_ptr;
		T* t_p = m_ptr;
		m_b_ptr = sptr.m_b_ptr;
		m_ptr = sptr.m_ptr;
        sptr.m_ptr = t_p;
		sptr.m_b_ptr = t_b;
		_xend();
		return sptr;
	}

	bool compare_exchange_strong(htm_shared_ptr<T>& expected_sptr, const htm_shared_ptr<T>& new_sptr, memory_order = memory_order_seq_cst, memory_order = memory_order_seq_cst) noexcept
	{
		bool success = false;
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		while (_XBEGIN_STARTED != _xbegin());
		if (m_b_ptr == expected_sptr.m_b_ptr) {
			if (nullptr != m_b_ptr) {
				if (m_b_ptr->ref_count == 1) {
					need_delete = true;
					temp_ptr = m_ptr;
					temp_b_ptr = m_b_ptr;
				}
				else if (m_b_ptr->ref_count < 1) _xabort(99);  // Fall Back Routine 추가 필요
				m_b_ptr->ref_cnt--;
			}
			m_ptr = new_sptr.m_ptr;
			m_b_ptr = new_sptr.m_b_ptr;
			new_sptr.m_b_ptr->ref_cnt++;
			success = true;
		}
		expected_sptr = m_ptr;
		_xend();
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
		return success;
	}

	bool compare_exchange_weak(shared_ptr<T>& expected_sptr, const shared_ptr<T>& target_sptr, memory_order = memory_order_seq_cst, memory_order = memory_order_seq_cst) noexcept
	{
		return compare_exchange_strong(expected_sptr, target_sptr);
	}

	htm_shared_ptr() noexcept
	{
		m_ptr = nullptr;
		m_b_ptr = nullptr;
	}

	~htm_shared_ptr() noexcept
	{
		bool need_delete = false;
		T *temp_ptr = nullptr;
		ctr_block<T> *temp_b_ptr = nullptr;
		while (_XBEGIN_STARTED != _xbegin());
		if (nullptr != m_b_ptr) {
			if (m_b_ptr->ref_cnt == 1) {
				need_delete = true;
				temp_ptr = m_ptr;
				temp_b_ptr = m_b_ptr;
			}
			m_b_ptr->ref_cnt--;
		}
		_xend();
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}

	constexpr htm_shared_ptr(const htm_shared_ptr<T>& sptr) noexcept
	{
		while(_XBEGIN_STARTED != _xbegin());
		m_ptr = sptr.m_ptr;
		m_b_ptr = sptr.m_b_ptr;
		if (nullptr != m_b_ptr) 
			m_b_ptr->ref_cnt++;
		_xend();
	}
	htm_shared_ptr<T>& operator=(const htm_shared_ptr<T>& sptr) noexcept
	{
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		do {
			int t = _xbegin();
			if (_XBEGIN_STARTED == t) break;
			if (99 == t) {
				m_ptr = nullptr;
				m_b_ptr = nullptr;
				return *this;
			}
		} while (true);

		if (nullptr != m_b_ptr) {
			if (m_b_ptr->ref_cnt == 1) {
				need_delete = true;
				temp_ptr = m_ptr;
				temp_b_ptr = m_b_ptr;
			}
			else if (m_b_ptr->ref_cnt < 1) _xabort(99);
			m_b_ptr->ref_cnt--;
		}
		m_ptr = sptr.m_ptr;
		m_b_ptr = sptr.m_b_ptr;
        if (nullptr != m_b_ptr)
            m_b_ptr->ref_cnt++;
		_xend();
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
		return *this;
	}

	htm_shared_ptr<T>& operator=(nullptr_t t) noexcept
	{
		reset();
		return *this;
	}


	T operator*() noexcept
	{
		T temp_ptr;
		while (_XBEGIN_STARTED != _xbegin());
		if (0 < m_b_ptr->ref_cnt)
			temp_ptr = *m_ptr;
		_xend();
		return temp_ptr;
	}

	void reset()
	{
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		while (_XBEGIN_STARTED != _xbegin());
		if (nullptr != m_b_ptr) {
			if (m_b_ptr->ref_cnt == 1) {
				need_delete = true;
				temp_ptr = m_ptr;
				temp_b_ptr = m_b_ptr;
			}
			m_b_ptr->ref_cnt--;
		}
		m_ptr = nullptr;
		m_b_ptr = nullptr;
		_xend();
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}
	T* operator ->()
	{
		T* p = nullptr;
		bool exception = false;
		while (_XBEGIN_STARTED != _xbegin());
		if (nullptr == m_b_ptr) exception = true;
		else if (m_b_ptr->ref_cnt < 1) exception = true;
		else p = m_ptr;
		_xend();
		if (true == exception) {
			int* a = nullptr;
			*a = 1;
		}
		return p;
	}

	template< typename TargetType >
	inline bool operator ==(htm_shared_ptr< TargetType > const& rhs)
	{
		return m_b_ptr == rhs.m_b_ptr;
	}
};


template <class T, class..._Types>
htm_shared_ptr<T> make_htm_shared(_Types&&... _Args)
{
	T* temp = new T{ forward<_Types>(_Args)... };
	htm_shared_ptr<T> new_sp;
	new_sp.m_ptr = temp;
	new_sp.m_b_ptr = new ctr_block <T>;
	new_sp.m_b_ptr->ptr = temp;
	new_sp.m_b_ptr->ref_cnt = 1;
	new_sp.m_b_ptr->weak_cnt = 0;
	return new_sp;
}
