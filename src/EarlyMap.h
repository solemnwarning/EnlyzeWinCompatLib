#pragma once

#include <algorithm>

#include "EarlyArray.h"

/**
 * @brief A map container type for use during CRT initialisation.
 *
 * This is a key-value container type with minimal C/C++ runtime dependencies suitable for use
 * during early program initialisation before STL containers and other C++ runtime niceties are
 * working.
 *
 * Elements are moved like blocks of opaque data - stick to POD types or structs which don't
 * maintain any internal pointers to themselves and can be trivially moved.
 *
 * The object contains enough space for RESERVED_CAPACITY elements, insertion beyond that extent
 * will allocate from the heap (which may fail in early CRT stages!).
*/
template<typename KT, typename VT, size_t RESERVED_CAPACITY> class EarlyMap
{
public:
	struct Element
	{
		const KT key;
		VT value;

		Element(const KT &key, const VT &value) :
			key(key), value(value) {
		}

		bool operator<(const Element &rhs) const
		{
			return key < rhs.key;
		}
	};

private:
	EarlyArray<Element, RESERVED_CAPACITY> array;

public:
	void set(const KT &key, const VT &value);

	void erase(Element *element);
	
	void erase(const KT &key);

	void clear();

	Element *find(const KT &key);

	Element *begin();

	Element *end();
};

template<typename KT, typename VT, size_t RESERVED_CAPACITY> void EarlyMap<KT, VT, RESERVED_CAPACITY>::set(const KT &key, const VT &value)
{
	Element *existing_value = std::lower_bound(array.begin(), array.end(), Element(key, VT()));
	if(existing_value != array.end() && existing_value->key == key)
	{
		existing_value->value = value;
	}
	else {
		array.insert(Element(key, value), (existing_value - array.begin()));
	}
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> void EarlyMap<KT, VT, RESERVED_CAPACITY>::erase(Element *element)
{
	array.erase(element - array.begin());
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> void EarlyMap<KT, VT, RESERVED_CAPACITY>::erase(const KT &key)
{
	Element *element = find(key);
	if(element != end())
	{
		erase(element);
	}
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> void EarlyMap<KT, VT, RESERVED_CAPACITY>::clear()
{
	while(array.size() > 0)
	{
		array.erase(array.size() - 1);
	}
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> EarlyMap<KT, VT, RESERVED_CAPACITY>::Element *EarlyMap<KT, VT, RESERVED_CAPACITY>::find(const KT &key)
{
	Element *existing_value = std::lower_bound(array.begin(), array.end(), Element(key, VT()));
	if(existing_value != array.end() && existing_value->key == key)
	{
		return existing_value;
	}
	else {
		return end();
	}
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> EarlyMap<KT, VT, RESERVED_CAPACITY>::Element *EarlyMap<KT, VT, RESERVED_CAPACITY>::begin()
{
	return array.begin();
}

template<typename KT, typename VT, size_t RESERVED_CAPACITY> EarlyMap<KT, VT, RESERVED_CAPACITY>::Element *EarlyMap<KT, VT, RESERVED_CAPACITY>::end()
{
	return array.end();
}
