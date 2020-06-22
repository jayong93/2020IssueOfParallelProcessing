#pragma once
#include <mutex>
#include <memory>
#include <immintrin.h>

using namespace std;

struct TX_Log {
    unsigned abort_conflict{0};
    unsigned abort_capacity{0};
    unsigned abort_explicit{0};
    unsigned abort_other{0};
    unsigned success{0};
};

static thread_local TX_Log tx_log;
static thread_local unsigned tx_retry_num = 0;

int tx_start(mutex &lock, atomic_bool &is_locked)
{
	int status = 0;

	while (is_locked.load(memory_order_relaxed));
	
    status = _xbegin();
    if (_XBEGIN_STARTED == (unsigned)status) {
		if (is_locked.load(memory_order_relaxed)) _xabort(64);
        return status;
    }

    if (status & _XABORT_CAPACITY) {
        ++tx_log.abort_capacity;
	} else if (status & _XABORT_CONFLICT) {
        ++tx_log.abort_conflict;
    } else if (status & _XABORT_EXPLICIT) {
        ++tx_log.abort_explicit;
	} else {
        ++tx_log.abort_other;
	}

	tx_retry_num += 1;
	if (tx_retry_num >= 15) {
		lock.lock();
		is_locked.store(true);
		return _XBEGIN_STARTED;
	}

    return status;
}

void tx_end(mutex &lock, atomic_bool &is_locked) {
	if (true == _xtest()) {
		if (is_locked.load(memory_order_relaxed)) _xabort(64);
		_xend();
		++tx_log.success;
	} else {
		is_locked.store(false);
		lock.unlock();
	}
	tx_retry_num = 0;
}


template <class T>
class ctr_block {
public:
	T* ptr;
	atomic_int ref_cnt;
	atomic_int weak_cnt;
};

template <class T>
class htm_shared_ptr {

private:
	template <class F, class..._Types>
	friend htm_shared_ptr<F> make_htm_shared(_Types&&... _Args);

	mutable mutex lock;
	mutable atomic_bool is_locked{false};
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
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
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
		tx_end(lock, is_locked);
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}

	htm_shared_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
	{
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
		htm_shared_ptr<T> t{ *this };
		tx_end(lock, is_locked);
		return t;
	}

	htm_shared_ptr<T>& exchange(htm_shared_ptr<T> &sptr, memory_order = memory_order_seq_cst) noexcept
	{
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
		ctr_block<T>* t_b = m_b_ptr;
		T* t_p = m_ptr;
		m_b_ptr = sptr.m_b_ptr;
		m_ptr = sptr.m_ptr;
        sptr.m_ptr = t_p;
		sptr.m_b_ptr = t_b;
		tx_end(lock, is_locked);
		return sptr;
	}

	bool compare_exchange_strong(htm_shared_ptr<T>& expected_sptr, const htm_shared_ptr<T>& new_sptr, memory_order = memory_order_seq_cst, memory_order = memory_order_seq_cst) noexcept
	{
		bool success = false;
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
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
		tx_end(lock, is_locked);
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
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
		if (nullptr != m_b_ptr) {
			if (m_b_ptr->ref_cnt == 1) {
				need_delete = true;
				temp_ptr = m_ptr;
				temp_b_ptr = m_b_ptr;
			}
			m_b_ptr->ref_cnt--;
		}
		tx_end(lock, is_locked);
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}

	constexpr htm_shared_ptr(const htm_shared_ptr<T>& sptr) noexcept
	{
		sptr.lock.lock();
		while(_XBEGIN_STARTED != tx_start(lock, is_locked));
		m_ptr = sptr.m_ptr;
		m_b_ptr = sptr.m_b_ptr;
		if (nullptr != m_b_ptr) 
			m_b_ptr->ref_cnt++;
		tx_end(lock, is_locked);
		sptr.lock.unlock();
	}
	htm_shared_ptr<T>& operator=(htm_shared_ptr<T> sptr) noexcept
	{
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		do {
			int t = tx_start(lock, is_locked);
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
		tx_end(lock, is_locked);
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
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
		if (0 < m_b_ptr->ref_cnt)
			temp_ptr = *m_ptr;
		tx_end(lock, is_locked);
		return temp_ptr;
	}

	void reset()
	{
		bool need_delete = false;
		T* temp_ptr;
		ctr_block<T>* temp_b_ptr;
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
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
		tx_end(lock, is_locked);
		if (true == need_delete) {
			delete temp_ptr;
			delete temp_b_ptr;
		}
	}
	T* operator ->()
	{
		T* p = nullptr;
		bool exception = false;
		while (_XBEGIN_STARTED != tx_start(lock, is_locked));
		if (nullptr == m_b_ptr) exception = true;
		else if (m_b_ptr->ref_cnt < 1) exception = true;
		else p = m_ptr;
		tx_end(lock, is_locked);
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
