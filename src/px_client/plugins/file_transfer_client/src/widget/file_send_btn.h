#pragma once

#include <QPushButton>

#include "file_const_def.h"

namespace px {

class FileSendBtn : public QPushButton
{
	Q_OBJECT
public:
	FileSendBtn(FileAffiliationType type);
};

}