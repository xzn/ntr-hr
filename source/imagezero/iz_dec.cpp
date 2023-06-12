#include "iz_c.h"
#include "decoder.h"

namespace IZ {

void initDecodeTable();

}

#ifdef __cplusplus
extern "C" {
#endif

void izInitDecodeTable() {
    IZ::initDecodeTable();
}

int izDecodeImageRGB(uint8_t *dst, const uint8_t *src, int width, int height) {
    IZ::Image<> image;
    image.setWidth(width);
    image.setHeight(height);
    image.setData(dst);

    IZ::ImageDecoder<> ic;
    ic.begin(src);
    ic.decodeImagePixels(image);
    return ic.end() - src;
}

#ifdef __cplusplus
}
#endif
