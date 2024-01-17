#ifndef __counted_by_h__
#define __counted_by_h__

#if __has_attribute(__counted_by__)
# define __counted_by(member) __attribute__((__counted_by__(member)))
#else
# define __counted_by(member)
#endif

#endif
