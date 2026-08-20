#include <stddef.h>
#include <string.h>
#include <new>
#include <assert.h>
#include <stdint.h>
#include <iterator>



namespace cxx_pool_alloc {
    constexpr uint32_t NULL_IDX = 0xFFFFFFFFu;
    using   chunk_idx_t = uint32_t;

    enum ReturnStatus : int {
        SUCCESS = 0,
        FAILED  = -1,
    };

    template<size_t ElemSize, size_t AlignSize>
    struct PoolAllocator {
        union Chunk {
        private:
            alignas(AlignSize)
            uint8_t     ElemData[ElemSize];
            chunk_idx_t next_free_idx;
        public:
            struct Handle {
            private:
                chunk_idx_t     m_chunk_idx;    // [0]
                Chunk          *m_chunk_p;      // [1]
                PoolAllocator  *m_pool_p;       // [2]

            public:
                Handle(
                    PoolAllocator  *m_pool_p,
                    chunk_idx_t     chunk_idx
                ) noexcept :
                    m_chunk_idx(chunk_idx),
                    m_chunk_p(m_pool_p->m_chunk_arr + chunk_idx),
                    m_pool_p(m_pool_p)
                {
                }

                Handle(
                    PoolAllocator  *m_pool_p,
                    void           *ptr
                ) noexcept :
                    m_chunk_idx(static_cast<Chunk*>(ptr) - m_pool_p->m_chunk_arr),
                    m_chunk_p(static_cast<Chunk*>(ptr)),
                    m_pool_p(m_pool_p)
                {
                }

                Chunk *get_chunk() const noexcept {
                    return this->m_chunk_p;
                }

                chunk_idx_t get_chunk_idx() const noexcept {
                    return this->m_chunk_idx;
                }

                void *chunk_data() const noexcept {
                    return this->m_chunk_p->ElemData;
                }

                template<typename T>
                T *get_elem() const noexcept {
                    return static_cast<T*>(this->chunk_data());
                }
            };
        };

    private:
        enum class ErrorCode : int {
            NOERROR,
            ERR_OPNEWARR,
            ERR_POOL_FULL,
            ERR_POOL_EMPTY,
            ERR_NULL_IDX,
            ERR_BAD_IDX,
            UNINIT,
        };
        inline const char *strerror(int err);

        enum class ChunkState : uint8_t {
            ST_EMPTY    = 0,
            ST_OCCUPIED = 1,
        };

        Chunk          *m_chunk_arr;        // [0]
        ChunkState     *m_state_arr;        // [1]
        chunk_idx_t     m_free_head_idx;    // [2]
        uint32_t        m_size;             // [3]
        uint32_t        m_capacity;         // [4]
        ErrorCode       m_errno;            // [5]

        void _cleanup() noexcept;
    public:
        inline bool is_valid_chunk_idx(chunk_idx_t chunk_idx) const noexcept {
            return !!(
                chunk_idx != NULL_IDX &&
                chunk_idx < m_capacity &&
                this->m_state_arr[chunk_idx] == ChunkState::ST_OCCUPIED
            );
        }

        inline bool is_init() const noexcept {
            return !(this->m_errno == ErrorCode::UNINIT);
        }

        ReturnStatus init(uint32_t pool_capacity) noexcept;
        //ReturnStatus reserve(uint32_t new_capacity) noexcept;
        chunk_idx_t alloc_chunk() noexcept;
        ReturnStatus free_chunk(chunk_idx_t chunk_idx) noexcept;


        PoolAllocator() noexcept :
            m_chunk_arr(nullptr),
            m_state_arr(nullptr),
            m_free_head_idx(NULL_IDX),
            m_size(0u),
            m_capacity(0u),
            m_errno(ErrorCode::UNINIT)
        {
        }

        ~PoolAllocator() noexcept {
            this->_cleanup();
        }

        PoolAllocator(const PoolAllocator &other_ref) = delete;
        PoolAllocator &operator=(const PoolAllocator &other_ref) = delete;
        PoolAllocator(PoolAllocator &&other_rref) noexcept :
            m_chunk_arr(other_rref.m_chunk_arr),
            m_state_arr(other_rref.m_state_arr),
            m_free_head_idx(other_rref.m_free_head_idx),
            m_size(other_rref.m_size),
            m_capacity(other_rref.m_capacity),
            m_errno(other_rref.m_errno)
        {
            other_rref.m_chunk_arr      = nullptr;
            other_rref.m_state_arr      = nullptr;
            other_rref.m_free_head_idx  = NULL_IDX;
            other_rref.m_size           = 0UL;
            other_rref.m_capacity       = 0UL;
            other_rref.m_errno          = ErrorCode::NOERROR;
        }
        PoolAllocator &operator=(PoolAllocator &&other_rref) noexcept {
            if (this == &other_rref) return *this;
            if (this->is_init()) this->~PoolAllocator();
            return *::new (this) PoolAllocator(std::move(other_rref));
        };

    }; // PoolAllocator


template<size_t ElemSize, size_t AlignSize>
inline ReturnStatus PoolAllocator<ElemSize, AlignSize>::init(uint32_t pool_capacity)  {
    this->m_errno = ErrorCode::NOERROR;
    assert(pool_capacity);
    this->m_chunk_arr = static_cast<Chunk*>(
        ::operator new[](pool_capacity * sizeof(Chunk), std::nothrow)
    );
    if (!this->m_chunk_arr) {
        this->m_errno = ErrorCode::ERR_OPNEWARR;
        goto failed;
    }
    
    this->m_state_arr = static_cast<ChunkState*>(
        ::operator new[](pool_capacity * sizeof(ChunkState), std::nothrow)
    );
    if (!this->m_state_arr) {
        this->m_errno = ErrorCode::ERR_OPNEWARR;
        goto failed;
    }

    for (uint32_t i = 0u; i < pool_capacity - 1; ++i) {
        this->m_chunk_arr[i].next_free_idx = i + 1;
    }
    this->m_chunk_arr[pool_capacity - 1].next_free_idx = NULL_IDX;

    memset(this->m_state_arr, 0, pool_capacity * sizeof(ChunkState));

    this->m_free_head_idx   = 0u;
    this->m_size            = 0u;
    this->m_capacity        = pool_capacity;
    this->m_errno           = ErrorCode::NOERROR;

    return ReturnStatus::SUCCESS;
failed:
    this->_cleanup();
    return ReturnStatus::FAILED;
}

template<size_t ElemSize, size_t AlignSize>
inline void PoolAllocator<ElemSize, AlignSize>::_cleanup() {
    if (this->m_chunk_arr) {
        ::operator delete[](this->m_chunk_arr);
    }
    if (this->m_state_arr) {
        ::operator delete[](this->m_state_arr);
    }

    this->m_free_head_idx   = NULL_IDX;
    this->m_size            = 0u;
    this->m_capacity        = 0u;
    this->m_errno           = ErrorCode::UNINIT;
}

template<size_t ElemSize, size_t AlignSize>
inline chunk_idx_t PoolAllocator<ElemSize, AlignSize>::alloc_chunk() {
    this->m_errno = ErrorCode::NOERROR;

    if (this->m_free_head_idx == NULL_IDX) {
        this->m_errno = ErrorCode::ERR_POOL_FULL;
        return NULL_IDX;
    }
    assert(this->m_size < this->m_capacity);
    chunk_idx_t new_chunk_idx   = this->m_free_head_idx;
    this->m_free_head_idx       = this->m_chunk_arr[new_chunk_idx].next_free_idx;

    assert(this->m_state_arr[new_chunk_idx] == ChunkState::ST_EMPTY);
    this->m_state_arr[new_chunk_idx] = ChunkState::ST_OCCUPIED;

    if (this->m_free_head_idx == new_chunk_idx) {
        assert(this->m_size == this->m_capacity - 1);
        assert(this->m_free_head_idx == NULL_IDX);
    }

    ++this->m_size;
    return new_chunk_idx;
}

template<size_t ElemSize, size_t AlignSize>
inline ReturnStatus PoolAllocator<ElemSize, AlignSize>::free_chunk(chunk_idx_t chunk_idx) {
    this->m_errno = ErrorCode::NOERROR;

    if (!this->is_valid_chunk_idx(chunk_idx)) {
        this->m_errno = ErrorCode::ERR_BAD_NIDX;
        return ReturnStatus::FAILED;
    }
    if (!this->m_size) {
        this->m_errno = ErrorCode::ERR_POOL_EMPTY;
        return ReturnStatus::FAILED;
    }

    assert(this->m_size < this->m_capacity);

    Chunk *chunk_p = &this->m_chunk_arr[chunk_idx];

    chunk_p->next_free_idx = this->m_free_head_idx;
    this->m_free_head_idx = chunk_idx;
    this->m_state_arr[chunk_idx] = ChunkState::ST_EMPTY;

    --this->m_size;
    return ReturnStatus::SUCCESS;
}

} // cxx_pool_alloc


