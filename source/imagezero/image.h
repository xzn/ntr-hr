#ifndef IZ_IMAGE_H
#define IZ_IMAGE_H 1

#include <cstddef>

namespace IZ {

template<
    typename Sample = unsigned char,
    ptrdiff_t samplesPerPixel = 1
>
class Image
{
public:
    int width() const { return m_width; }
    int height() const { return m_height; }
    ptrdiff_t samplesPerLine() const { return m_spl; }
    Sample *data() const { return m_data; }

    void setWidth(int width) { m_width = width; }
    void setHeight(int height) { m_height = height; }
    void setSamplesPerLine(ptrdiff_t samplesPerLine) { m_spl = samplesPerLine; }
    void setData(Sample *data) { m_data = data; }

protected:
    int m_width;
    int m_height;
    ptrdiff_t m_spl;
    Sample *m_data;
};

template<
    typename Sample = unsigned char,
    ptrdiff_t samplesPerPixel = 1
>
class ConstImage
{
public:
    int width() const { return m_width; }
    int height() const { return m_height; }
    ptrdiff_t samplesPerLine() const { return m_spl; }
    const Sample *data() const { return m_data; }

    void setWidth(int width) { m_width = width; }
    void setHeight(int height) { m_height = height; }
    void setSamplesPerLine(ptrdiff_t samplesPerLine) { m_spl = samplesPerLine; }
    void setData(const Sample *data) { m_data = data; }

protected:
    int m_width;
    int m_height;
    ptrdiff_t m_spl;
    const Sample *m_data;
};

} // namespace IZ

#endif
