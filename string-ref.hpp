#pragma once

#include <cassert>
#include <compare>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <ostream>
#include <string_view>
#include <utility>

#ifdef STRING_REF_DEBUG
#include <iostream>
#endif

class StringRef {
public:
    StringRef() = default;

    ~StringRef() noexcept {
#ifdef STRING_REF_DEBUG
        std::cerr << "~StringRef() for " << *this << ": " << (unique() ? "freeing" : "skipping")
                  << "\n";
#endif

        if (unique()) {
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfree-nonheap-object"
            std::destroy_at(header());

            // triggers a false positive (-Wfree-nonheap-object) for default-initialized StringRefs
            // on GCC: it believes this code is reachable when ptr_ == &empty, despite this not
            // being the case.
            delete[] ptr_;
#pragma GCC diagnostic pop
        }
    }

    StringRef(std::string_view s) : StringRef(s.data(), s.size()) {}
    StringRef(const char *s) : StringRef(s, strlen(s)) {}

    StringRef(const char *s, size_t len) {
        assert(len <= max_len);

        ptr_ = new char[sizeof(Header) + len + 1];
        new (header()) Header;
        header()->len = len;
        memcpy(data(), s, len);
        data()[len] = '\0';
        mark_unique();
    }

    StringRef(const StringRef &other) noexcept {
        *this = other;
    }

    StringRef &operator=(const StringRef &other) noexcept {
        if (this == &other) {
            return *this;
        }

        other.mark_shared();
        ptr_ = other.ptr_;

        return *this;
    }

    StringRef(StringRef &&other) noexcept {
        *this = std::move(other);
    }

    StringRef &operator=(StringRef &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        std::swap(ptr_, other.ptr_);

        return *this;
    }

    bool shared() const noexcept {
        return header()->shared > 0;
    }

    bool unique() const noexcept {
        return !shared();
    }

    const char *get() const noexcept {
        return data();
    }

    std::string_view view() const noexcept {
        return {get(), size()};
    }

    size_t size() const noexcept {
        return header()->len;
    }

    size_t length() const noexcept {
        return size();
    }

    bool operator==(const StringRef &other) const noexcept {
        return view() == other.view();
    }

    bool operator==(std::string_view other) const noexcept {
        return view() == other;
    }

    std::strong_ordering operator<=>(const StringRef &other) const noexcept {
        return view() <=> other.view();
    }

    std::strong_ordering operator<=>(std::string_view other) const noexcept {
        return view() <=> other;
    }

    friend std::ostream &operator<<(std::ostream &s, const StringRef &ref) {
        return s << "StringRef(" << (const void *)ref.ptr_ << " \"" << ref.view() << "\" ("
                 << (ref.unique() ? "unique" : "shared") << "))";
    }

private:
    struct Header {
        size_t shared : 1;
        size_t len : sizeof(size_t) * 8 - 1;
    };

    static inline Header empty{
        .shared = 1,
        .len = 0,
    };

    static constexpr size_t max_len = -size_t(1) >> 1;

    Header *header() const noexcept {
        // cue in C++ shenanigans.
        return std::launder(reinterpret_cast<Header *>(ptr_));
    }

    char *data() const noexcept {
        // no need to launder: this is part of the same object that ptr_ points to.
        return ptr_ + sizeof(Header);
    }

    void mark_unique() const noexcept {
        header()->shared = 0;
    }

    void mark_shared() const noexcept {
        header()->shared = 1;
    }

    char *ptr_ = reinterpret_cast<char *>(&empty);
};
