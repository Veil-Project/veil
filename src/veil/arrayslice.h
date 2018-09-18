#ifndef VEIL_ARRAYSLICE_H
#define VEIL_ARRAYSLICE_H

#include <array>

template <typename Iterable>
class array_slice
{
public:
    template <typename Container>
    array_slice(const Container& container);

    array_slice(const Iterable* begin, const Iterable* end);

    const Iterable* begin() const;
    const Iterable* end() const;
    const Iterable* data() const;
    std::size_t size() const;
    bool empty() const;

private:
    const Iterable* begin_;
    const Iterable* end_;
};

#endif //VEIL_ARRAYSLICE_H
