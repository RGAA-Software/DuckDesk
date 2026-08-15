#include "math_helper.h"

namespace px {

int MathHelper::AlignTo4Bytes(int width) {
    return (width + 3) & ~3;
}

}