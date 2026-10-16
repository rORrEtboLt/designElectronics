// minih264 (public domain, CC0) — H.264 encoder implementation, compiled on its own.
#define H264E_MAX_THREADS 0
#define H264E_SVC_API 0
#if defined(__APPLE__) && defined(__aarch64__)
// minih264's Apple-arm64 path calls vtbl2q_u8, the old name of AArch64's vqtbl2_u8.
#include <arm_neon.h>
#define vtbl2q_u8(table, idx) vqtbl2_u8((table), (idx))
#endif
#if defined(__APPLE__)
// minih264 tests BYTE_ORDER/BIG_ENDIAN on Apple; they come from <machine/endian.h>.
// Without it both are 0, the bitstream is written byte-swapped and players reject it.
#include <machine/endian.h>
#endif
#define MINIH264_IMPLEMENTATION
#include "../third_party/minih264/minih264e.h"
