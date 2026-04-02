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
    StringRef(const char *s) : StringRef(s, s == nullptr ? 0 : strlen(s)) {}
    StringRef(std::nullptr_t) : StringRef() {}

    StringRef(const char *s, size_t len) {
        if (s == nullptr) {
            s_ = nullptr;

            return;
        }

        s_ = new char[len + 1];
        memcpy(s_, s, len);
        s_[len] = '\0';
        s_ = with_unique_mark(s_);
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
        return (reinterpret_cast<uintptr_t>(s_) & mark_bit) == 0;
    }

    bool unique() const noexcept {
        return !shared();
    }

    const char *get() const noexcept {
        return ptr();
    }

    std::string_view view() const noexcept {
        return {get(), get() == nullptr ? 0 : strlen(get())};
    }

    bool operator==(const StringRef &other) const noexcept {
        return *this == other.get();
    }

    bool operator==(std::string_view other) const noexcept {
        return view() == other;
    }

    bool operator==(const char *other) const noexcept {
        if (get() == nullptr || other == nullptr) {
            return get() == other;
        }

        return strcmp(get(), other) == 0;
    }

    std::strong_ordering operator<=>(const StringRef &other) const noexcept {
        return *this <=> other.get();
    }

    std::strong_ordering operator<=>(std::string_view other) const noexcept {
        return get() <=> other;
    }

    std::strong_ordering operator<=>(const char *other) const noexcept {
        return strcmp(null_to_empty(get()), null_to_empty(other)) <=> 0;
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

    // NOLINTNEXTLINE(readability-non-const-parameter)
    static char *with_unique_mark(char *p) noexcept {
        // NOLINTNEXTLINE(performance-no-int-to-ptr)
        return reinterpret_cast<char *>(reinterpret_cast<uintptr_t>(p) | mark_bit);
    }

    static constexpr const char *null_to_empty(const char *s) noexcept {
        return s == nullptr ? "" : s;
    }

    void mark_shared() const noexcept {
        s_ = ptr();
    }

    mutable char *s_ = nullptr;
};
