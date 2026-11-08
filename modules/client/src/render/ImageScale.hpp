#pragma once

#include "Math2D.hpp"
#include "backend/IRenderer2D.hpp"

namespace kuge
{

    //! The part of a picture (`part`, in its pixels; width 0: all of it) made smaller, `width` x `height` pixels, by an area filter: each
    //! new pixel is the mean of the pixels it covers (the ones it only half covers count for half). The colors are weighed by their
    //! alpha, so a clear pixel next to a white one does not darken the edge: it comes out white, half clear.
    //! An empty picture, or a size that is not positive, gives an empty picture. The part is cut to the picture.
    Image shrink(const Image& source, Rect part, int width, int height);

}
