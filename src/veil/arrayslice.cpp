
#include "arrayslice.h"

template <typename Iterable>
template <typename Container>
array_slice<Iterable>::array_slice(const Container& container)
        : begin_(container.data()), end_(container.data() + container.size())
{
}

template <typename Iterable>
array_slice<Iterable>::array_slice(const Iterable* begin, const Iterable* end)
        : begin_(begin), end_(end)
{
}

template <typename Iterable>
const Iterable* array_slice<Iterable>::begin() const
{
    return begin_;
}

template <typename Iterable>
const Iterable* array_slice<Iterable>::end() const
{
    return end_;
}

template <typename Iterable>
const Iterable* array_slice<Iterable>::data() const
{
    return begin_;
}

template <typename Iterable>
std::size_t array_slice<Iterable>::size() const
{
    return end_ - begin_;
}

template <typename Iterable>
bool array_slice<Iterable>::empty() const
{
    return end_ == begin_;
}