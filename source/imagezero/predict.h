#ifndef IZ_PREDICT_H
#define IZ_PREDICT_H 1

#include "intmacros.h"

namespace IZ {

#define UNUSED(x) { x = x; }

template<typename Sample = unsigned char>
class Predictor3med
{
public:
    static int predict(int x, int y, int xy) {
        int dx, dy, dxy, s;
        dy = x - xy;
        dx = xy - y;
        dxy = x - y;
        s = oppositeSign(dy, dx);
        dxy &= oppositeSign(dxy, dy);
        return selectVal(s, y + dy, x - dxy);
    }
};

template<typename Sample = unsigned char>
class Predictor3alpha
{
public:
    static int predict(int x, int y, int xy) {
        return clamp0(clampMax(x + y - xy, Sample(~0)));
    }
};

template<typename Sample = unsigned char>
class Predictor3plane
{
public:
    static int predict(int x, int y, int xy) {
        return x + y - xy;
    }
};

template<typename Sample = unsigned char>
class Predictor3avgplane
{
public:
    static int predict(int x, int y, int xy) {
        return (3 * x + 3 * y - 2 * xy + 2) >> 2;
    }
};

template<typename Sample = unsigned char>
class Predictor2avg
{
public:
    static int predict(int x, int y, int xy) {
        UNUSED(xy);
        return (x + y + 1) >> 1;
    }
};

template<typename Sample = unsigned char>
class Predictor1x
{
public:
    static int predict(int x, int y, int xy) {
        UNUSED(y);
        UNUSED(xy);
        return x;
    }
};

template<typename Sample = unsigned char>
class Predictor1y
{
public:
    static int predict(int x, int y, int xy) {
        UNUSED(x);
        UNUSED(xy);
        return y;
    }
};

template<typename Sample = unsigned char>
class Predictor0
{
public:
    static int predict(int x, int y, int xy) {
        UNUSED(x);
        UNUSED(y);
        UNUSED(xy);
        return 0;
    }
};

} // namespace IZ

#endif
