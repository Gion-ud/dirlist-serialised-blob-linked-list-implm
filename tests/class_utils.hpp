#pragma once
#include <stdint.h>
#include <utility>
#include <new>
#include <memory>

namespace cxx_class_utils {
    namespace memory {
        template<typename T>
        struct cleanup {
            void operator()(T *obj_p) {
                if (obj_p) obj_p->~T();
            }
        };

        template<typename T>
        using placement_unique_ptr = std::unique_ptr<T, cxx_class_utils::memory::cleanup<T>>;

        template<typename T>
        struct buffer {
        private:
            alignas(T) uint8_t _buf[sizeof(T)];
        public:
            buffer() = default;
            ~buffer() = default; // does NOT call T::~T()
            void *data() const noexcept {
                return this->_buf;
            }
            template<typename... Args>
            T *obj_init(Args&&... ctor_args_rref) noexcept {
                return ::new (this->_buf) T(std::forward<Args>(ctor_args_rref)...);
            }

            template<typename... Args>
            placement_unique_ptr<T> make_unique(Args&&... ctor_args_rref) noexcept {
                return placement_unique_ptr<T>(
                    this->obj_init(std::forward<Args>(ctor_args_rref)...)
                );
            }            
        };
    }
}
