#pragma once
#include <stddef.h>
#include <fnv1a_hash.h>
#include <string.h>
#include <new>
#include <iostream>

namespace string_utils {
    template<size_t N>
    struct string_literal {
    private:
        const char *m_data;
    public:
        constexpr string_literal(const char (&cstr_ref)[N]) noexcept 
            : m_data{cstr_ref}
        {  
        }
        constexpr size_t length() const noexcept {
            return N - 1;
        }
        constexpr const char *data()  const noexcept {
            return this->m_data;
        }
        ~string_literal() noexcept = default;
    };

    template<size_t N>
    constexpr string_literal<N> make_string_literal(const char (&cstr_ref)[N]) {
        return string_literal<N>(cstr_ref);
    }

    struct string_view {
    private:
        const char *m_data;
        size_t      m_length;

    public:
        string_view() noexcept :
            m_data(nullptr), m_length(0UL)
        {
        }
        string_view(const char *data, size_t length) noexcept :
            m_data(data), m_length(length)
        {
        }
        string_view(const char *cstr) noexcept :
            m_data(cstr),
            m_length((cstr) ? strlen(cstr) : 0UL)
        {
        }
        template<size_t N>
        string_view(const string_literal<N> &strlit_ref) noexcept :
            m_data(strlit_ref.data()),
            m_length(strlit_ref.length())
        {
        }
        void set(const char *data, size_t length) noexcept {
            ::new (this) string_view(data, length);
        }
        void set(const char *cstr) noexcept {
            ::new (this) string_view(cstr);
        }
        const char *data() const noexcept {
            return this->m_data;
        }
        size_t length() const noexcept {
            return this->m_length;
        }
        uint32_t hash32() const noexcept {
            return fnv_1a_hash32(this->data(), this->length());
        }
        string_view &operator=(const char *cstr) noexcept {
            this->set(cstr);
            return *this;
        }
    }; // struct string_view


    inline std::ostream &operator<<(
        std::ostream       &os_ref,
        const string_view  &strview_ref
    ) {
        return os_ref.write(strview_ref.data(), strview_ref.length());
    }
}
