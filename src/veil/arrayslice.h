#ifndef VEIL_ARRAYSLICE_H
#define VEIL_ARRAYSLICE_H

#include <array>

template <typename Iterable>
class array_slice
{
public:
    template <typename Container>
    array_slice(const Container& container) : begin_(container.data()), end_(container.data() + container.size()) {}

    array_slice(const Iterable* begin, const Iterable* end) : begin_(begin), end_(end) {}

    const Iterable* begin() const { return begin_; }
    const Iterable* end() const { return end_; }
    const Iterable* data() const { return begin_; }
    std::size_t size() const { return end_ - begin_; }
    bool empty() const { return end_ == begin_; }

private:
    const Iterable* begin_;
    const Iterable* end_;
};

#endif //VEIL_ARRAYSLICE_H
