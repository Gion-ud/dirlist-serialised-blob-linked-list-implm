#pragma once
#include <stddef.h>

namespace array_utils {
    namespace c_array {
        template <typename ElemType, size_t N>
        constexpr size_t length(const ElemType (&)[N]){
            return N;
        }

        template <typename T, size_t N>
        struct array_view {
        private:
            T  *m_arr;
        public:
            constexpr array_view(T (&c_arr)[N]) noexcept :
                m_arr(static_cast<T*>(c_arr))
            {
            }
            constexpr size_t size() const noexcept {
                return N;
            }
            constexpr size_t length() const noexcept {
                return N;
            }
            constexpr const T *data() const noexcept {
                return this->m_arr;
            }

            constexpr T *begin() const noexcept {
                return this->m_arr;
            }
            constexpr T *end() const noexcept {
                return this->m_arr + N;
            }

            const T &operator[](size_t idx) const noexcept {
                return this->m_arr[idx];
            }
        };

        template <typename T, size_t N>
        constexpr array_view<T, N> make_array_view(T (&c_arr)[N]) {
            return array_view<T, N>(c_arr);
        }
    }
}
