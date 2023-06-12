#ifndef IZ_ENCODER_H
#define IZ_ENCODER_H 1

#include <cstring>

#include "iz_p.h"

namespace IZ {

#define encodePixelRGB(predictor)                \
{                                                \
    PixelRGB<> pix, pp;                          \
                                                 \
    pix.readFrom(p);                             \
    pp.predict(p, bpp, bpr, predictor::predict); \
    pix -= pp;                                   \
    pix.forwardTransform();                      \
    p += bpp;                                    \
    pix.toUnsigned();                            \
                                                 \
    int nl = pix.numBits();                      \
    cx = (cx << CONTEXT_BITS) + nl;              \
    this->writeBits(dBits[cx & bitMask(2 * CONTEXT_BITS)], dCount[cx & bitMask(2 * CONTEXT_BITS)]); \
    if ((ret = pix.writeBits(*this, nl)))        \
        return ret;                              \
}

#define bpp 3

template <
    typename Predictor = Predictor3avgplane<>,
    typename Code = Code_def_t
>
class ImageEncoder : public BitEncoder<Code>
{
public:
    ImageEncoder() {
        memcpy(dBits, staticdBits, sizeof(dBits));
        memcpy(dCount, staticdCount, sizeof(dCount));
    }

    int encodeImagePixels(const ConstImage<> &im) __attribute__((always_inline)) {
        const int bpr = im.samplesPerLine();
        const unsigned char *p = im.data();
        int size = im.width() * im.height();
        const unsigned char *pend = p + bpp * size;
        unsigned int cx = (7 << CONTEXT_BITS) + 7;
        int ret;

        /* first pixel in first line */
        encodePixelRGB(Predictor0<>);
        /* remaining pixels in first line */
        const unsigned char *endline = p + bpr - bpp;
        while (p != endline) {
            encodePixelRGB(Predictor1x<>);
        }
        while (p != pend) {
            /* first pixel in remaining lines */
            encodePixelRGB(Predictor1y<>);
            /* remaining pixels in remaining lines */
            const unsigned char *endline = p + bpr - bpp;
            while (p != endline) {
                encodePixelRGB(Predictor);
            }
        }
        return 0;
    }

private:
    unsigned int dBits[1 << (2 * CONTEXT_BITS)];
    unsigned int dCount[1 << (2 * CONTEXT_BITS)];
};

#undef bpp

} // namespace IZ

#endif
