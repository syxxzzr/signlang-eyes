#ifndef SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_FLOAT16_H
#define SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_FLOAT16_H

namespace rknpu2 {

using ushort = unsigned short;

typedef union suf32 {
  int i;
  unsigned u;
  float f;
} suf32;

class float16 {
public:
  float16() = default;
  explicit float16(float x) { w = bits(x); }

  operator float() const {
    suf32 out;
    unsigned t = ((w & 0x7fff) << 13) + 0x38000000;
    unsigned sign = (w & 0x8000) << 16;
    unsigned e = w & 0x7c00;
    out.u = t + (1 << 23);
    out.u = (e >= 0x7c00 ? t + 0x38000000 : e == 0 ? (static_cast<void>(out.f -= 6.103515625e-05f), out.u) : t) | sign;
    return out.f;
  }

  static ushort bits(float x) {
    suf32 in;
    in.f = x;
    unsigned sign = in.u & 0x80000000;
    in.u ^= sign;
    ushort value;

    if (in.u >= 0x47800000) {
      value = static_cast<ushort>(in.u > 0x7f800000 ? 0x7e00 : 0x7c00);
    } else if (in.u < 0x38800000) {
      in.f += 0.5F;
      value = static_cast<ushort>(in.u - 0x3f000000);
    } else {
      const unsigned t = in.u + 0xc8000fff;
      value = static_cast<ushort>((t + ((in.u >> 13) & 1)) >> 13);
    }

    return static_cast<ushort>(value | (sign >> 16));
  }

private:
  ushort w = 0;
};

} // namespace rknpu2

#endif // SIGNLANG_EYES_EDGEAI_HANDPOSE_DET_FLOAT16_H
