#include <stddef.h>
#include <string.h>
#include <new>
#include <assert.h>
#include <stdint.h>
#include <iterator>
#include <utility>
//#include "dbg_print.h"

//#include <array>
#include <vector>
#include <memory>
#include <iostream>

enum ListReturnStatus {
    LIST_SUCCESS    = 0,
    LIST_FAILED     = -1,
};


namespace cxx_linked_list {
    namespace _list_impl {
        using node_idx_t = uint32_t;

        enum class ErrorCode : int {
            NOERROR,
            ERR_OPNEWARR,
            ERR_POOL_FULL,
            ERR_POOL_EMPTY,
            ERR_NULL_IDX,
            ERR_BAD_NODE,
            UNINIT,
        };
        inline const char *strerror(int err);

        struct NodePool;
        struct Node {
        private:
            void   *m_data;     // [0]
            Node   *m_prev_np;  // [1]
            Node   *m_next_np;  // [2]

        public:
            Node(void *data) noexcept :
                m_data(data),
                m_prev_np(this),
                m_next_np(this)
            {
            }

            void set_data(void *data) noexcept {
                this->m_data = data;
            }

            void *data() const noexcept {
                return this->m_data;
            }

            Node *prev() const noexcept {
                return this->m_prev_np;
            }

            Node *next() const noexcept {
                return this->m_next_np;
            }

            // root.prev() = tail;
            // root.next() = head;
            int add_head(Node *node_p) noexcept {
                if (!node_p) return LIST_FAILED;
                Node *root_np   = this;         // sentinel node; list.end() / list.rend()
                Node *head_np   = this->next(); // list.front()  

                node_p->m_prev_np   = root_np;
                node_p->m_next_np   = head_np;
                root_np->m_next_np  = node_p;
                head_np->m_prev_np  = node_p;

                return LIST_SUCCESS;
            }

            int add_tail(Node *node_p) noexcept {
                if (!node_p) return LIST_FAILED;
                Node *root_np   = this;         // sentinel node; list.end() / list.rend()
                Node *tail_np   = this->prev(); // list.back()  

                node_p->m_prev_np   = tail_np;
                node_p->m_next_np   = root_np;
                root_np->m_prev_np  = node_p;
                tail_np->m_next_np  = node_p;

                return LIST_SUCCESS;
            }

            void unlink() noexcept {
                this->m_prev_np->m_next_np = this->m_next_np;
                this->m_next_np->m_prev_np = this->m_prev_np;

                this->m_prev_np = this;
                this->m_next_np = this;
            }

            ~Node() noexcept {
                this->m_prev_np = this;
                this->m_next_np = this;
            }
        }; // Node

        struct NodePool {
        private:
            enum class ChunkState : uint8_t {
                ST_EMPTY    = 0,
                ST_OCCUPIED = 1,
            };
            union Chunk {
                alignas(Node) uint8_t   node_buf[sizeof(Node)];
                Chunk                  *next_free_p;

                Node *to_node() noexcept {
                    return reinterpret_cast<Node*>(this->node_buf);
                }
            };

            Chunk      *m_chunk_arr;    // [0]
            ChunkState *m_state_arr;    // [1]
            Chunk      *m_free_head_p;  // [2]
            uint32_t    m_size;         // [3]
            uint32_t    m_capacity;     // [4]
            ErrorCode   m_errno;        // [5]

            inline uint32_t node_ptr_to_idx(const Node *node_p) const noexcept {
                return node_p - this->m_chunk_arr->to_node();
            }

            inline bool is_valid_node(const Node *node_p) const noexcept {
                ptrdiff_t node_off = node_p - this->m_chunk_arr->to_node();
                ptrdiff_t node_idx = node_off / sizeof(Node);
                return !!(
                    node_p &&
                    !(node_off % sizeof(Node)) &&
                    node_idx >= 0L &&
                    node_idx < (ptrdiff_t)m_capacity &&
                    this->m_state_arr[node_idx] == ChunkState::ST_OCCUPIED
                );
            }

            int _init(uint32_t pool_capacity) noexcept;
            void _cleanup() noexcept;

        public:
            inline bool is_init() const noexcept {
                return !(this->m_errno == ErrorCode::UNINIT);
            }

            inline bool is_full() {
                return (this->m_free_head_p) ? false : true;
            }

            inline bool is_empty() {
                return (this->m_size) ? false : true;
            }

            inline ErrorCode get_error() const noexcept {
                return this->m_errno;
            }

            inline Chunk *chunk_from_buf(void *mem) const noexcept {
                return reinterpret_cast<Chunk*>(mem);
            }

            //int reserve(uint32_t new_capacity) noexcept;
            Node *new_node(void *data) noexcept;
            int free_node(Node *node_p) noexcept;

            NodePool(uint32_t pool_capacity) noexcept :
                m_chunk_arr(nullptr),
                m_state_arr(nullptr),
                m_free_head_p(nullptr),
                m_size(0u),
                m_capacity(0u),
                m_errno(ErrorCode::UNINIT)
            {
                this->_init(pool_capacity);
            }
            ~NodePool() noexcept {
                this->_cleanup();
            }
            NodePool(const NodePool &other_ref) = delete;
            NodePool &operator=(const NodePool &other_ref) = delete;
            NodePool(NodePool &&other_rref) noexcept :
                m_chunk_arr(other_rref.m_chunk_arr),
                m_state_arr(other_rref.m_state_arr),
                m_free_head_p(other_rref.m_free_head_p),
                m_size(other_rref.m_size),
                m_capacity(other_rref.m_capacity),
                m_errno(other_rref.m_errno)
            {
                other_rref.m_chunk_arr      = nullptr;
                other_rref.m_state_arr      = nullptr;
                other_rref.m_free_head_p    = nullptr;
                other_rref.m_size           = 0UL;
                other_rref.m_capacity       = 0UL;
                other_rref.m_errno          = ErrorCode::NOERROR;
            }
            NodePool &operator=(NodePool &&other_rref) noexcept {
                if (this == &other_rref) return *this;
                if (this->is_init()) this->~NodePool();
                return *::new (this) NodePool(std::move(other_rref));
            };
        }; // NodePool

    } // _list_impl


/*
    methods:
    - push_back()
    - push_front()
    - insert()
    
    - pop_back()
    - pop_front()
    - erase()
    - clear()
    - remove() ?

    - front()
    - back()

    - begin() / end()
    - rbegin() / rend()

    - empty()
    - size()
    - max_size() / capaxcity()
    - reserve()
*/  
    using Node          = _list_impl::Node;
    using NodePool      = _list_impl::NodePool;
    using ErrorCode     = _list_impl::ErrorCode;

    struct List {
    private:
        Node        m_root;     // [0]

        int _init(uint32_t capacity) noexcept;
        void _cleanup() noexcept;

    public:
        List() noexcept : m_root(nullptr) {
        }
        ~List() noexcept = default;

        inline int push_front(Node *node_p) noexcept {
            return this->m_root.add_head(node_p);
        }
        inline int push_back(Node *node_p) noexcept {
            return this->m_root.add_tail(node_p);
        }
        inline Node *pop_front() noexcept {
            Node *ret_np = this->m_root.next();
            ret_np->unlink();
            return ret_np;
        }
        inline Node *pop_back() noexcept {
            Node *ret_np = this->m_root.prev();
            ret_np->unlink();
            return ret_np;
        }

        inline int _insert_before(Node *target_np, Node *node_p) noexcept {
            return target_np->add_head(node_p);
        }
        inline int _insert_after(Node *target_np, Node *node_p) noexcept {
            return target_np->add_tail(node_p);
        }
        inline Node *_unlink(Node *node_p) noexcept {
            Node *next_np = node_p->next();
            node_p->unlink();
            return next_np;
        }

        template<typename T>
        struct Iterator :
            public std::iterator<
                std::bidirectional_iterator_tag,    // iterator_category
                T,          // value_type
                ptrdiff_t,  // difference_type
                T*,         // pointer
                T&          // reference
            >
        {   
        private:
            Node   *m_node_p;   // [0]
        public:
            explicit Iterator(Node *node_p) noexcept :
                m_node_p(node_p)
            {
            }
            Iterator &operator++() noexcept {
                this->m_node_p = this->m_node_p->next();
                return *this;
            }
            Iterator operator++(int) noexcept {
                Iterator ret = *this;
                ++*this;
                return ret;
            }
            Iterator &operator--() noexcept {
                this->m_node_p = this->m_node_p->prev();
                return *this;
            }
            Iterator operator--(int) noexcept {
                Iterator ret = *this;
                --*this;
                return ret;
            }
            bool operator==(const Iterator &other_ref) const noexcept {
                return this->m_node_p == other_ref.m_node_p;
            }
            bool operator!=(const Iterator &other_ref) const noexcept { 
                return !(*this == other_ref);
            }
            typename Iterator<T>::reference operator*() const noexcept {
                return *static_cast<T*>(this->m_node_p->data());
            }
            Node *get() const noexcept {
                return this->m_node_p;
            }
        };

        template<typename T>
        using ReverseIt = std::reverse_iterator<Iterator<T>>;

        inline Node *head() const noexcept {
            return this->m_root.next();
        }
    
        inline Node *tail() const noexcept {
            return this->m_root.prev();
        }

        template<typename T>
        inline Iterator<T> begin() {
            return Iterator<T>(this->m_root.next());
        }

        template<typename T>
        inline Iterator<T> end() {
            return Iterator<T>(&this->m_root);
        }

        inline List::Iterator<uint8_t> begin() {
            return Iterator<uint8_t>(this->m_root.next());
        }

        inline List::Iterator<uint8_t> end() {
            return List::Iterator<uint8_t>(&this->m_root);
        }
    
        template<typename T>
        inline ReverseIt<T> rbegin() {
            return ReverseIt<T>(this->end<T>());
        }

        template<typename T>
        inline ReverseIt<T> rend() {
            return ReverseIt<T>(this->begin<T>());
        }

        template<typename T>
        int insert_before(Iterator<T> &target_it_ref, Node *node_p) noexcept {
            return this->_insert_before(target_it_ref.get(), node_p);
        }
        template<typename T>
        int insert_after(Iterator<T> &target_it_ref, Node *node_p) noexcept {
            return this->_insert_after(target_it_ref.get(), node_p);
        }
        template<typename T>
        Iterator<T> unlink(Iterator<T> &target_it_ref) noexcept {
            return Iterator<T>(this->_unlink(target_it_ref.get()));
        }
    };

    struct NodeDeleter {
        NodePool   &m_pool_ref;
        NodeDeleter(NodePool &pool_ref) noexcept :
            m_pool_ref(pool_ref)
        {
        }
        void operator()(Node *np) noexcept {
            this->m_pool_ref.free_node(np);
        }
    };

    std::unique_ptr<Node, NodeDeleter>
    make_unique_node(NodePool &pool_ref, void *data) {
        return std::unique_ptr<Node, NodeDeleter>(
            pool_ref.new_node(data), NodeDeleter(pool_ref)
        );
    }

} // cxx_linked_list


namespace cxx_linked_list {

namespace _list_impl {

int NodePool::_init(uint32_t pool_capacity) {
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
        this->m_chunk_arr[i].next_free_p = &this->m_chunk_arr[i + 1];
    }
    this->m_chunk_arr[pool_capacity - 1].next_free_p = nullptr;

    memset(this->m_state_arr, 0, pool_capacity * sizeof(ChunkState));

    this->m_free_head_p     = &this->m_chunk_arr[0];
    this->m_size            = 0u;
    this->m_capacity        = pool_capacity;
    this->m_errno           = ErrorCode::NOERROR;

    return LIST_SUCCESS;
failed:
    this->_cleanup();
    return LIST_FAILED;
}

void NodePool::_cleanup() {
    if (this->m_chunk_arr) {
        ::operator delete[](this->m_chunk_arr);
    }
    if (this->m_state_arr) {
        ::operator delete[](this->m_state_arr);
    }

    this->m_free_head_p     = nullptr;
    this->m_size            = 0u;
    this->m_capacity        = 0u;
    this->m_errno           = ErrorCode::UNINIT;
}

Node *NodePool::new_node(void *data) {
    this->m_errno = ErrorCode::NOERROR;

    if (this->is_full()) {
        assert(this->m_size == this->m_capacity);
        this->m_errno = ErrorCode::ERR_POOL_FULL;
        return nullptr;
    }
    assert(this->m_size < this->m_capacity);

    void *_new_node_buf = this->m_free_head_p->node_buf;
    this->m_free_head_p = this->chunk_from_buf(_new_node_buf)->next_free_p;
    Node *new_node_p    = ::new (_new_node_buf) Node(data);

    uint32_t node_idx = this->node_ptr_to_idx(new_node_p);
    assert(this->m_state_arr[node_idx] == ChunkState::ST_EMPTY);
    this->m_state_arr[node_idx] = ChunkState::ST_OCCUPIED;

    ++this->m_size;
    return new_node_p;
}

int NodePool::free_node(Node *node_p) {
    this->m_errno = ErrorCode::NOERROR;

    if (!this->is_valid_node(node_p)) {
        this->m_errno = ErrorCode::ERR_BAD_NODE;
        return LIST_FAILED;
    }
    if (this->is_empty()) {
        this->m_errno = ErrorCode::ERR_POOL_EMPTY;
        return LIST_FAILED;
    }

    assert(this->m_size <= this->m_capacity);

    node_p->~Node();

    Chunk *chunk_p      = this->chunk_from_buf(node_p);
    uint32_t chunk_idx  = this->node_ptr_to_idx(node_p);

    chunk_p->next_free_p    = this->m_free_head_p;
    this->m_free_head_p     = chunk_p;
    this->m_state_arr[chunk_idx] = ChunkState::ST_EMPTY;

    --this->m_size;
    return LIST_SUCCESS;
}



} // _list_impl




} // cxx_linked_list

static std::vector<int> numv = {
    4, 17, 5, 11, 19, 91, 76, 67, 37, 13, 31, 23 
};

int main() {
    using namespace cxx_linked_list;
    using node_unique_ptr = std::unique_ptr<Node, NodeDeleter>; 
    
    NodePool pool(64u);
    List list{};

    std::vector<node_unique_ptr> nupv{};
    nupv.reserve(numv.size());

    for (auto &elem_ref : numv) {
        auto nup = make_unique_node(pool, &elem_ref);
        list.push_back(nup.get());
        nupv.push_back(std::move(nup));
    }
    std::cout << "\n";

    for (auto it = list.begin<int>(); it != list.end<int>(); ++it) {
        std::cout << *it << "\n";
    }
    std::cout << "\n";

    for (auto it = list.rbegin<int>(); it != list.rend<int>(); ++it) {
        std::cout << *it << "\n";
    }
    std::cout << "\n";

    std::cout << "F\n";
    for (auto it = nupv.begin(); it != nupv.end(); ++it) {
        it->get()->unlink();
    }
    std::cout << "\n";

    for (auto it = list.begin<int>(); it != list.end<int>(); ++it) {
        std::cout << *it << "\n";
    }
    std::cout << "\n";


    for (auto &elem_ref : nupv) {
        list.push_front(elem_ref.get());
    }
    std::cout << "\n";

    for (auto it = list.begin<int>(); it != list.end<int>(); ++it) {
        std::cout << *it << "\n";
    }
    std::cout << "\n";

    for (auto i = 0u; i < nupv.size() + 1; ++i) {
        for (auto it = list.rbegin<int>(); it != list.rend<int>(); ++it) {
            std::cout << *it << " ";
        }
        std::cout << "\n";
        list.pop_front();
    }
    std::cout << "\n";


    for (auto &elem_ref : nupv) {
        list.push_front(elem_ref.get());
    }
    std::cout << "\n";

    for (auto i = 0u; i < nupv.size() + 1; ++i) {
        for (auto it = list.begin<int>(); it != list.end<int>(); ++it) {
            std::cout << *it << " ";
        }
        std::cout << "\n";
        list.pop_back();
    }


    

    return 0;
}