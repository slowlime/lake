#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ostream>
#include <string_view>
#include <utility>

#ifdef STRING_REF_DEBUG
#include <iostream>
#endif

class StringRef {
public:
    StringRef() noexcept = default;

    ~StringRef() noexcept {
#ifdef STRING_REF_DEBUG
        std::cerr << "~StringRef() for " << *this << ": " << (unique() ? "freeing" : "skipping")
                  << "\n";
#endif

        if (unique()) {
            delete[] ptr();
        }
    }

    StringRef(std::string_view s) : StringRef(s.data(), s.size()) {}
    StringRef(const char *s) : StringRef(s, strlen(s)) {}

    StringRef(const char *s, size_t len) {
        s_ = new char[len + 1];
        memcpy(s_, s, len);
        s_[len] = '\0';
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
        return {get(), strlen(get())};
    }

    bool operator==(const StringRef &other) const noexcept {
        return strcmp(get(), other.get()) == 0;
    }

    bool operator==(std::string_view other) const noexcept {
        return get() == other;
    }

    bool operator==(const char *other) const noexcept {
        return strcmp(get(), other) == 0;
    }

    std::strong_ordering operator<=>(const StringRef &other) const noexcept {
        return strcmp(get(), other.get()) <=> 0;
    }

    std::strong_ordering operator<=>(std::string_view other) const noexcept {
        return get() <=> other;
    }

    std::strong_ordering operator<=>(const char *other) const noexcept {
        return strcmp(get(), other) <=> 0;
    }

    friend std::ostream &operator<<(std::ostream &s, const StringRef &ref) {
        return s << "StringRef(" << (const void *)ref.ptr() << " \"" << ref.view() << "\" ("
                 << (ref.unique() ? "unique" : "shared") << "))";
    }

private:
    static constexpr uintptr_t mark_bit = 1;
    static inline char empty alignas(sizeof(uintptr_t)) = '\0';

    char *ptr() const noexcept {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(s_) & ~mark_bit);
    }

    bool marked() const noexcept {
        return (reinterpret_cast<uintptr_t>(s_) & mark_bit) != 0;
    }

    // NOLINTNEXTLINE(readability-non-const-parameter)
    static char *with_mark(char *p, bool unique) noexcept {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<char *>(
            (reinterpret_cast<uintptr_t>(p) & ~mark_bit) | uintptr_t(unique) * mark_bit
        );
    }

    void set_mark(bool unique) const noexcept {
        s_ = with_mark(s_, unique);
    }

    void mark_unique() const noexcept {
        set_mark(true);
    }

    void mark_shared() const noexcept {
        set_mark(false);
    }

    mutable char *s_ = with_mark(&empty, false);
};
