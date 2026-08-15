#pragma once

#include "px_message_new/msg_answer_cbk.h"

namespace px {
	class FileMsgAnswerCbkStructure : public MsgAnswerCallbackStructure {
	public:
		void Add(const std::shared_ptr<px::Message>& msg, OnMsgParseRespCallbackFuncType parse_msg_callbck) override;
	};
}