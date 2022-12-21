/*
 * This file is part of the DXX-Rebirth project <http://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */
#pragma once

#include <iterator>
#include <tuple>
#include <type_traits>
#include "dxxsconf.h"
#include "ephemeral_range.h"
#include <utility>

namespace d_zip {

namespace detail {

template <typename... T>
void discard_arguments(T &&...)
{
}

template <std::size_t... N, typename... range_iterator_type>
void increment_iterator(std::tuple<range_iterator_type...> &iterator, std::index_sequence<N...>)
{
	/* Order of evaluation is irrelevant, so pass the results to a
	 * discarding function.  This permits the compiler to evaluate the
	 * expression elements in any order.
	 */
	discard_arguments(++(std::get<N>(iterator))...);
}

template <std::size_t... N, typename... range_iterator_type>
auto dereference_iterator(const std::tuple<range_iterator_type...> &iterator, std::index_sequence<N...>)
{
	/* std::make_tuple is not appropriate here, because the result of
	 * dereferencing the iterator may be a reference, and the resulting
	 * tuple should store the reference, not the underlying object.
	 * Calling std::make_tuple would decay such references into the
	 * underlying type.
	 *
	 * std::tie is not appropriate here, because it captures all
	 * arguments as `T &`, so it fails to compile if the result of
	 * dereferencing the iterator is not a reference.
	 */
	return std::tuple<
		decltype(*(std::get<N>(iterator)))...
		>(*(std::get<N>(iterator))...);
}

/* The overloads of get_static_size are declared, but never defined.  They
 * exist only for use in decltype() expressions.
 */
template <typename T, std::size_t N>
std::integral_constant<std::size_t, N> get_static_size(const T (&)[N]);

template <typename T>
requires(requires {
	typename std::tuple_size<T>::type;
	})
typename std::tuple_size<T>::type get_static_size(const T &);

std::nullptr_t get_static_size(...);

template <typename range0, typename rangeN, typename range0_extent = decltype(get_static_size(std::declval<range0>())), typename rangeN_extent = decltype(get_static_size(std::declval<rangeN>()))>
struct check_static_size_pair : std::true_type
{
	static_assert(range0_extent::value <= rangeN_extent::value, "first range is longer than a later range");
};

template <typename range0, typename rangeN, typename range0_extent>
struct check_static_size_pair<range0, rangeN, range0_extent, std::nullptr_t> : std::true_type
{
};

void range_index_type(...);

template <
	typename... range_type,
	/* If any `range_type::index_type` is not defined, fail.
	 * If there is no common_type among all the
	 * `range_type::index_type`, fail.
	 */
	typename index_type = typename std::common_type<typename std::remove_reference<typename std::remove_reference<range_type>::type::index_type &>::type...>::type,
	/* If the common_type `index_type` is not a suitable argument to all
	 * `range_type::operator[]()`, fail.
	 */
	typename = std::void_t<decltype(std::declval<range_type &>().operator[](std::declval<index_type>()))...>
	>
index_type range_index_type(std::tuple<range_type...> *);

}

}

/* This iterator terminates when the first zipped range terminates.  The
 * caller is responsible for ensuring that use of the zip_iterator does
 * not increment past the end of any zipped range.  This can be done by
 * ensuring that the first zipped range is not longer than any other
 * zipped range, or by ensuring that external logic stops the traversal
 * before the zip_iterator increments past the end.
 *
 * There is no runtime check that the below loop would be
 * safe, since a check external to the zip_iterator could stop before
 * undefined behaviour occurs.
 *
 * However, if the first range is convertible to a C array of known
 * length or to a C++ std::array, then there is a compile-time check
 * that the first range is not longer than any other range that is
 * likewise convertible.  If a range cannot be converted to an array,
 * then its length is unknown and is not checked.  If the first range is
 * not convertible, then no ranges are checked.

	for (auto i = zip_range.begin(), e = zip_range.end(); i != e; ++i)
	{
		if (condition())
			break;
	}

 */
template <typename range_index_type, typename range0_iterator_type, typename... rangeN_iterator_type>
class zip_iterator : std::tuple<range0_iterator_type, rangeN_iterator_type...>
{
	using base_type = std::tuple<range0_iterator_type, rangeN_iterator_type...>;
	/* Prior to C++17, range-based for insisted on the same type for
	 * `begin` and `end`, so method `end_internal` must return a full iterator,
	 * even though most of it is a waste.  To save some work, values that are
	 * used for ignored fields are default-constructed (if possible)
	 * instead of copy-constructed from the begin iterator.
	 */
	template <std::size_t, typename T>
		requires(std::is_default_constructible<T>::value)
		static T end_construct_ignored_element()
		{
			return T();
		}
	template <std::size_t I, typename T>
		requires(!std::is_default_constructible<T>::value)
		T end_construct_ignored_element() const
		{
			return std::get<I>(*this);
		}
protected:
	template <typename E0, std::size_t... N>
		zip_iterator end_internal(const E0 &e0, std::index_sequence<0, N...>) const
		{
			return zip_iterator(e0, this->template end_construct_ignored_element<N, typename std::tuple_element<N, base_type>::type>()...);
		}
	using index_sequence_type = std::make_index_sequence<1 + sizeof...(rangeN_iterator_type)>;
public:
	using difference_type = typename std::iterator_traits<range0_iterator_type>::difference_type;
	using index_type = range_index_type;
	using value_type = decltype(d_zip::detail::dereference_iterator(std::declval<base_type>(), index_sequence_type()));
	using pointer = value_type *;
	using reference = value_type &;
	using iterator_category = std::forward_iterator_tag;
	using base_type::base_type;
	auto operator*() const
	{
		return d_zip::detail::dereference_iterator(*this, index_sequence_type());
	}
	zip_iterator &operator++()
	{
		d_zip::detail::increment_iterator(*this, index_sequence_type());
		return *this;
	}
	/* operator++(int) is currently unused, but is required to satisfy
	 * the concept check on forward iterator.
	 */
	zip_iterator operator++(int)
	{
		auto result = *this;
		d_zip::detail::increment_iterator(*this, index_sequence_type());
		return result;
	}
	difference_type operator-(const zip_iterator &i) const
	{
		return std::get<0>(*this) - std::get<0>(i);
	}
	bool operator!=(const zip_iterator &i) const
	{
		return std::get<0>(*this) != std::get<0>(i);
	}
	bool operator==(const zip_iterator &i) const
	{
		return !(*this != i);
	}
};

template <typename range_index_type, typename range0_iterator_type, typename... rangeN_iterator_type>
class zip : zip_iterator<range_index_type, range0_iterator_type, rangeN_iterator_type...>
{
	range0_iterator_type m_end;
public:
	using range_owns_iterated_storage = std::false_type;
	using iterator = zip_iterator<range_index_type, range0_iterator_type, rangeN_iterator_type...>;
	using typename iterator::index_type;
	template <typename range0, typename... rangeN>
		constexpr zip(range0 &&r0, rangeN &&... rN) :
			iterator(std::begin(r0), std::begin(rN)...), m_end(std::end(r0))
	{
		using size_r0 = decltype(d_zip::detail::get_static_size(std::declval<range0>()));
		if constexpr (!std::is_same<size_r0, std::nullptr_t>::value)
		{
			/* If the first range cannot be measured, then no static
			 * checks are done.
			 */
			static_assert(
				std::conjunction<
					d_zip::detail::check_static_size_pair<
						range0,
						rangeN
					> ...
				>::value);
		}
		static_assert((!any_ephemeral_range<range0 &&, rangeN &&...>::value), "cannot zip storage of ephemeral ranges");
	}
	[[nodiscard]]
	iterator begin() const { return *this; }
	[[nodiscard]]
	iterator end() const
	{
		return this->end_internal(m_end, typename iterator::index_sequence_type());
	}
};

template <typename range0, typename... rangeN>
zip(range0 &&r0, rangeN &&... rN) -> zip<decltype(d_zip::detail::range_index_type(static_cast<std::tuple<range0, rangeN...> *>(nullptr))), decltype(std::begin(r0)), decltype(std::begin(rN))...>;
