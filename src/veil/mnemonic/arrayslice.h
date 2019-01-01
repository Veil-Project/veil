/**
 * Copyright (c) 2011-2019 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
