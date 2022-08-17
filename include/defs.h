#pragma once

#define min(x,y) ({             \
  typeof(x) _x = (x);           \
  typeof(y) _y = (y);           \
  _x < _y ? _x : _y;            \
  })

#define max(x,y) ({             \
  typeof(x) _x = (x);           \
  typeof(y) _y = (y);           \
  _x > _y ? _x : _y;            \
})

#define ALIGN_UP_MASK(x, mask) (((x) + (mask)) & ~(mask))
#define ALIGN_MASK(x, mask)    ((x) & ~(mask))
#define ALIGN(x, a)            ALIGN_MASK(x, (typeof(x))(a) - 1)
#define IS_ALIGNED(x, a)       (((x) & ((typeof(x))(a) - 1)) == 0)
#define ALIGN_UP(x, a)         (IS_ALIGNED(x, a) ? (x) : ALIGN_UP_MASK(x, (typeof(x))(a) - 1))

#define PALIGN(p, align)       ((typeof(p)) ALIGN((uintptr_t)(p),       \
                                                  (uintptr_t) (align))

#define PALIGN_UP(p, align)    ((typeof(p)) ALIGN_UP((uintptr_t)(p),    \
                                                     (uintptr_t)(align)))
#define SIFY(...) _SIFY(__VA_ARGS__)
#define _SIFY(...) #__VA_ARGS__

/* Mask off and return a bit slice. */
#define MASK_OFF(value, larger, smaller) (((value) >> (smaller)) & ((1U << ((larger) + 1U - (smaller))) - 1U))

/* Form a bit slice. */
#define MASK_IN(value, larger, smaller) (((value) & ((1U << ((larger) + 1U - (smaller))) - 1U)) << (smaller))

#define ARRAY_LEN(x) (sizeof((x)) / sizeof((x)[0]))

#define container_of(ptr, type, member) ({                      \
                    const typeof( ((type *)0)->member ) *__mptr = (ptr);    \
                    (type *)( (char *)__mptr - offsetof(type,member) );})

#define offsetof(TYPE, MEMBER) ((length_t) &((TYPE *)0)->MEMBER)

#define MB(x) (1U * x * 1024 * 1024)
#define GB(x) (1U * x * 1024 * 1024 * 1024)
#define TB(x) (1U * x * 1024 * 1024 * 1024 * 1024)
#define PFN(page) (((uintptr_t) page) >> 12)

#define likely(x)     (__builtin_constant_p(x) ? !!(x) : __builtin_expect(!!(x), 1))
#define unlikely(x)   (__builtin_constant_p(x) ? !!(x) : __builtin_expect(!!(x), 0))

static inline uint32_t
swab32(uint32_t value)
{
   uint32_t result;

   __asm__("rlwimi %0,%1,24,16,23\n\t"
           "rlwimi %0,%1,8,8,15\n\t"
           "rlwimi %0,%1,24,0,7"
           : "=r" (result)
           : "r" (value), "0" (value >> 24));
   return result;
}

static inline uint16_t
swab16(uint16_t value)
{
   uint16_t result;

   __asm__("rlwimi %0,%1,8,16,23\n\t"
           : "=r" (result)
           : "r" (value), "0" (value >> 8));
   return result;
}

#define le32_to_cpu(X) swab32(X)
#define le16_to_cpu(X) swab16(X)
