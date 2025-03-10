#pragma once

#include <stdlib.h>

/**
 * @brief An array container type for use during CRT initialisation.
 *
 * This is an array container type with minimal C/C++ runtime dependencies suitable for use during
 * early program initialisation before STL containers and other C++ runtime niceties are working.
 *
 * Array elements are copied like blocks of opaque data - stick to POD types or structs which don't
 * maintain any internal pointers to themselves and can be trivially moved.
 *
 * The object contains enough space for RESERVED_CAPACITY elements, insertion beyond that extent
 * will allocate from the heap (which may fail in early CRT stages!).
*/
template<typename T, size_t RESERVED_CAPACITY> class EarlyArray
{
private:
	char fixed_array[sizeof(T) * RESERVED_CAPACITY];
	T *array;

	size_t array_size;
	size_t array_capacity;

public:
	/* If an EarlyArray is static and used before global constructors have run, pass true for
	 * assume_zero_init to suppress initialising the members.
	*/
	EarlyArray(bool assume_zero_init = false);
	~EarlyArray();

	EarlyArray(const EarlyArray&) = delete;
	EarlyArray &operator=(const EarlyArray&) = delete;

	EarlyArray(EarlyArray &&other);
	EarlyArray &operator=(EarlyArray&&) = delete;

	void push(const T &value);

	void insert(const T &value, size_t index);

	void erase(size_t index);

	void resize(size_t size);

	void reserve(size_t target_capacity);

	void shrink_to_fit();

	T &operator[](size_t index);

	size_t size() const;

	T &front() const;
	T &back() const;

	T* begin();

	T* end();
};

template<typename T, size_t RESERVED_CAPACITY> EarlyArray<T, RESERVED_CAPACITY>::EarlyArray(bool assume_zero_init)
{
	if(!assume_zero_init)
	{
		array = NULL;
		array_size = 0;
		array_capacity = 0;
	}
}

template<typename T, size_t RESERVED_CAPACITY> EarlyArray<T, RESERVED_CAPACITY>::~EarlyArray()
{
	for(auto it = begin(); it != end(); ++it)
	{
		it->~T();
	}

	if(array != (T*)(fixed_array))
	{
		free(array);
	}
}

template<typename T, size_t RESERVED_CAPACITY> EarlyArray<T, RESERVED_CAPACITY>::EarlyArray(EarlyArray &&other)
{
	if(other.array == (T*)(other.fixed_array))
	{
		memcpy(fixed_array, other.fixed_array, (sizeof(T) * other.array_size));

		array = (T*)(fixed_array);
		array_size = other.array_size;
		array_capacity = RESERVED_CAPACITY;
	}
	else {
		array = other.array;
		array_size = other.array_size;
		array_capacity = other.array_capacity;

		other.array = (T*)(other.fixed_array);
		other.array_capacity = RESERVED_CAPACITY;
	}

	other.array_size = 0;
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::push(const T &value)
{
	if(array_size == array_capacity)
	{
		if((array_size + 1) <= RESERVED_CAPACITY)
		{
			reserve(RESERVED_CAPACITY);
		}
		else {
			reserve(array_capacity + 8);
		}
	}

	new (&(array[array_size])) T(value);
	++array_size;
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::insert(const T &value, size_t index)
{
	if(array_size == array_capacity)
	{
		if((array_size + 1) <= RESERVED_CAPACITY)
		{
			reserve(RESERVED_CAPACITY);
		}
		else {
			reserve(array_capacity + 8);
		}
	}

	memmove((array + index + 1), (array + index), ((array_size - index) * sizeof(T)));
	++array_size;

	new (&(array[index])) T(value);
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::erase(size_t index)
{
	array[index].~T();

	memmove((array + index), (array + index + 1), ((array_size - index - 1) * sizeof(T)));
	--array_size;

	if(array_size <= RESERVED_CAPACITY)
	{
		shrink_to_fit();
	}
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::resize(size_t size)
{
	if(array_capacity < size)
	{
		size_t target_capacity = size;
		if(target_capacity > RESERVED_CAPACITY && (target_capacity % 8) != 0)
		{
			target_capacity += 8 - (target_capacity % 8);
		}

		reserve(target_capacity);
	}

	while(array_size < size)
	{
		new (&(array[array_size])) T();
		++array_size;
	}

	while(array_size > size)
	{
		array[array_size - 1].~T();
		--array_size;
	}

	if(array_size <= RESERVED_CAPACITY)
	{
		shrink_to_fit();
	}
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::reserve(size_t target_capacity)
{
	if(array == NULL)
	{
		array = (T*)(fixed_array);
		array_capacity = RESERVED_CAPACITY;
	}

	if(array == (T*)(fixed_array))
	{
		if(target_capacity > RESERVED_CAPACITY)
		{
			T *new_array = (T*)(malloc((sizeof(T) * target_capacity)));
			if(new_array == NULL)
			{
				throw std::bad_alloc();
			}

			memmove((void*)(new_array), (void*)(array), (sizeof(T) * array_size));

			array = new_array;
			array_capacity = target_capacity;
		}
	}
	else {
		if(target_capacity > array_capacity)
		{
			T *new_array = (T*)(realloc(array, (sizeof(T) * target_capacity)));
			if(new_array == NULL)
			{
				throw std::bad_alloc();
			}

			array = new_array;
			array_capacity = target_capacity;
		}
	}
}

template<typename T, size_t RESERVED_CAPACITY> void EarlyArray<T, RESERVED_CAPACITY>::shrink_to_fit()
{
	if(array != (T*)(fixed_array))
	{
		if(array_size <= RESERVED_CAPACITY)
		{
			memcpy(fixed_array, array, (sizeof(T*) * array_size));

			free(array);
			array = (T*)(fixed_array);
			array_capacity = RESERVED_CAPACITY;
		}
		else if(array_size < array_capacity)
		{
			T *new_array = (T*)(realloc(array, (sizeof(T) * array_size)));
			if(new_array == NULL)
			{
				throw std::bad_alloc();
			}

			array = new_array;
			array_capacity = array_size;
		}
	}
}

template<typename T, size_t RESERVED_CAPACITY> T &EarlyArray<T, RESERVED_CAPACITY>::operator[](size_t index)
{
	return array[index];
}

template<typename T, size_t RESERVED_CAPACITY> size_t EarlyArray<T, RESERVED_CAPACITY>::size() const
{
	return array_size;
}

template<typename T, size_t RESERVED_CAPACITY> T& EarlyArray<T, RESERVED_CAPACITY>::front() const
{
	return array[0];
}

template<typename T, size_t RESERVED_CAPACITY> T& EarlyArray<T, RESERVED_CAPACITY>::back() const
{
	return array[array_size - 1];
}

template<typename T, size_t RESERVED_CAPACITY> T* EarlyArray<T, RESERVED_CAPACITY>::begin()
{
	return array;
}

template<typename T, size_t RESERVED_CAPACITY> T* EarlyArray<T, RESERVED_CAPACITY>::end()
{
	return array + array_size;
}
