#pragma once 

namespace tc { 

class MathHelper {
public:
	MathHelper() = delete;
	static int AlignTo4Bytes(int width);
};

}