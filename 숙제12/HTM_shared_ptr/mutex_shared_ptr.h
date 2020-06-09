#pragma once
#include <mutex>
#include <memory>


	using namespace std;

	template <class T>

	struct atomic_shared_ptr {
	private:
		mutable mutex m_lock;
		shared_ptr<T> m_ptr;
	public:
		bool is_lock_free() const noexcept
		{
			return false;
		}

		void store(shared_ptr<T> sptr, memory_order = memory_order_seq_cst) noexcept
		{
			m_lock.lock();
			m_ptr = sptr;
			m_lock.unlock();
		}

		shared_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
		{
			m_lock.lock();
			shared_ptr<T> t = m_ptr;
			m_lock.unlock();
			return t;
		}

		operator shared_ptr<T>() const noexcept
		{
			m_lock.lock();
			shared_ptr<T> t = m_ptr;
			m_lock.unlock();
			return t;
		}

		shared_ptr<T> exchange(shared_ptr<T> sptr, memory_order = memory_order_seq_cst) noexcept
		{
			m_lock.lock();
			shared_ptr<T> t = m_ptr;
			m_ptr = sptr;
			m_lock.unlock();
			return t;
		}

		bool compare_exchange_strong(shared_ptr<T>& expected_sptr, shared_ptr<T> new_sptr, memory_order, memory_order) noexcept
		{
			bool success = false;
			m_lock.lock();
			shared_ptr<T> t = m_ptr;
			if (m_pt.get() == expected_sptr.get()) {
				m_ptr = new_sptr;
				success = true;
			}
			expected_sptr = m_ptr;
			m_lock.unlock();
		}

		bool compare_exchange_weak(shared_ptr<T>& expected_sptr, shared_ptr<T> target_sptr, memory_order, memory_order) noexcept
		{
			return compare_exchange_strong(expected_sptr, target_sptr, memory_order);
		}

		atomic_shared_ptr() noexcept = default;

		constexpr atomic_shared_ptr(shared_ptr<T> sptr) noexcept
		{
			m_lock.lock();
			m_ptr = sptr;
			m_lock.unlock();
		}
		//		atomic_shared_ptr(const atomic_shared_ptr&) = delete;
		//		atomic_shared_ptr& operator=(const atomic_shared_ptr&) = delete;
		shared_ptr<T> operator=(shared_ptr<T> sptr) noexcept
		{
			m_lock.lock();
			m_ptr = sptr;
			m_lock.unlock();
			return sptr;
		}

		void reset()
		{
			m_lock.lock();
			m_ptr = nullptr;
			m_lock.unlock();
		}
		T* operator ->()
		{
			m_lock.lock();
			T* p = m_ptr.get();
			m_lock.unlock();
			return p;
			//			return m_ptr.get();
		}


		atomic_shared_ptr(const atomic_shared_ptr& rhs)
		{
			store(rhs);
		}
		atomic_shared_ptr& operator=(const atomic_shared_ptr& rhs)
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
			m_lock.lock();
			m_ptr = wptr;
			m_lock.unlock();
		}
		weak_ptr<T> load(memory_order = memory_order_seq_cst) const noexcept
		{
			m_lock.lock();
			weak_ptr<T> t = m_ptr;
			m_lock.unlock();
			return t;
		}
		operator weak_ptr<T>() const noexcept
		{
			m_lock.lock();
			weak_ptr<T> t = m_ptr;
			m_lock.unlock();
			return t;
		}
		weak_ptr<T> exchange(weak_ptr<T> wptr, memory_order = memory_order_seq_cst) noexcept
		{
			m_lock.lock();
			weak_ptr<T> t = m_ptr;
			m_ptr = wptr;
			m_lock.unlock();
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
			if (!exptected_sptr) return false;

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
			m_lock.lock();
			m_ptr = wptr;
			m_lock.unlock();
		}

		atomic_weak_ptr(const atomic_weak_ptr&) = delete;
		atomic_weak_ptr& operator=(const atomic_weak_ptr&) = delete;
		weak_ptr<T> operator=(weak_ptr<T> wptr) noexcept
		{
			m_lock.lock();
			m_ptr = wptr;
			m_lock.unlock();
			return wptr;
		}
		shared_ptr<T> lock() const noexcept
		{
			m_lock.lock();
			shared_ptr<T> sptr = m_ptr.lock();
			m_lock.unlock();
			return sptr;
		}
		void reset()
		{
			m_lock.lock();
			m_ptr.reset();
			m_lock.unlock();
		}
	};

