#pragma once 

namespace px { 

class MathHelper {
public:
	MathHelper() = delete;
	static int AlignTo4Bytes(int width);
};

}