#define CONCAT(a, b) CONCAT_INNER(a, b)
#define CONCAT_INNER(a, b) a##b
#define UNIQUE_NAME(base) CONCAT(base, __COUNTER__)

template<typename Callable>
struct defer_impl {
    constexpr defer_impl(Callable c)
        : c(c)
    {
    }
    ~defer_impl() { c(); }

    Callable c;
};

#define defer const defer_impl UNIQUE_NAME(hidden) = [&]()
