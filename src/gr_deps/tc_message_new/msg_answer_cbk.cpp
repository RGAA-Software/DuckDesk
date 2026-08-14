#include "msg_answer_cbk.h"
#include "tc_common_new/log.h"

namespace tc {

MsgAnswerCallbackStructure::MsgAnswerCallbackStructure() : io_ctx_(1), asio_timer_(io_ctx_) {
	io_ctx_.start();
	AddTimer(std::chrono::milliseconds(100), std::bind(&MsgAnswerCallbackStructure::On100MsTimer, this));
}

MsgAnswerCallbackStructure::~MsgAnswerCallbackStructure() {
	asio_timer_.stop();
	io_ctx_.stop();
}

void MsgAnswerCallbackStructure::Add(const std::shared_ptr<tc::Message>& msg, OnMsgParseRespCallbackFuncType callback) {
	int seq = send_message_seq_++;
	msg->set_file_operate_sequence(seq);
	std::lock_guard<std::mutex> lock{mutex_};
	send_msg_resp_callback_map_[seq].send_time = std::chrono::system_clock::now();
	send_msg_resp_callback_map_[seq].callback = ([=](const std::shared_ptr<tc::Message>& message) {
		if (callback) {
			callback(message->file_operate_resp_code(), message->file_operate_resp_message(), message->file_operate_resp_data());
		}
	});
}

void MsgAnswerCallbackStructure::On100MsTimer() {
	std::lock_guard<std::mutex> lg(mutex_);
	auto now = std::chrono::system_clock::now();
	for (auto it = send_msg_resp_callback_map_.begin(); it != send_msg_resp_callback_map_.end();) {
		if (now - it->second.send_time >= std::chrono::seconds(4)) {
			auto timeout_message = std::make_shared<tc::Message>();
			timeout_message->set_file_operate_resp_code(tc::kRespCodeTimeout);
			timeout_message->set_file_operate_resp_message("Waiting for service response timeout"); // u8"" 在这里没有被正确识别
			it->second.callback(timeout_message);
			it = send_msg_resp_callback_map_.erase(it);
		}
		else {
			++it;
		}
	}
}

void MsgAnswerCallbackStructure::HandleRespAnswerMessage(const std::shared_ptr<tc::Message>& message) {
	std::lock_guard<std::mutex> lg(mutex_);
	auto find_it = send_msg_resp_callback_map_.find(message->file_operate_resp_sequence());
	if (find_it == send_msg_resp_callback_map_.end()) {
		LOGI("HandleRespAnswerMessage, can not find sequence {}", message->file_operate_resp_sequence());
		return;
	}
	find_it->second.callback(message);
	send_msg_resp_callback_map_.erase(find_it);
}

}