#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string>
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
            delete[] ptr();
        }
    }

    explicit StringRef(const std::string &s) : StringRef(s.data(), s.size()) {}
    explicit StringRef(std::string_view s) : StringRef(s.data(), s.size()) {}
    explicit StringRef(const char *s) : StringRef(s, strlen(s)) {}

    StringRef(const char *s, size_t len) {
        s_ = new char[len + 1];
        len_ = len;
        memcpy(s_, s, len);
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
        s_ = other.s_;
        len_ = other.len_;

        return *this;
    }

    StringRef(StringRef &&other) noexcept {
        *this = std::move(other);
    }

    StringRef &operator=(StringRef &&other) noexcept {
        if (this == &other) {
            return *this;
        }

        std::swap(s_, other.s_);
        std::swap(len_, other.len_);

        return *this;
    }

    bool shared() const noexcept {
        return !marked();
    }

    bool unique() const noexcept {
        return marked();
    }

    const char *get() const noexcept {
        return ptr();
    }

    std::string_view view() const noexcept {
        return {get(), len_};
    }

    size_t size() const noexcept {
        return len_;
    }

    size_t length() const noexcept {
        return len_;
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
        return s << "StringRef(" << (const void *)ref.ptr() << " \"" << ref.view() << "\" ("
                 << (ref.unique() ? "unique" : "shared") << "))";
    }

private:
    static constexpr uintptr_t mark_bit = 1;

    char *ptr() const noexcept {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(s_) & ~mark_bit);
    }

    bool marked() const noexcept {
        return (reinterpret_cast<uintptr_t>(s_) & mark_bit) != 0;
    }

    void set_mark(bool unique) const noexcept {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        s_ = reinterpret_cast<char *>(
            (reinterpret_cast<uintptr_t>(s_) & ~mark_bit) | uintptr_t(unique) * mark_bit
        );
    }

    void mark_unique() const noexcept {
        set_mark(true);
    }

    void mark_shared() const noexcept {
        set_mark(false);
    }

    mutable char *s_ = nullptr;
    size_t len_ = 0;
};
