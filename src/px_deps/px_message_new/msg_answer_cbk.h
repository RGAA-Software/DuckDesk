#pragma once
#include <thread>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <memory>
#include <map>
#include <asio2/asio2.hpp>
#include "tc_message.pb.h"


namespace tc {
	  
using OnMsgParseRespCallbackFuncType = std::function<void(tc::RespCode code, const std::string& msg, const std::string& data)>;

class MsgRespCallbackInfo {
public:
	using OnMessageCallbackFuncType = std::function<void(const std::shared_ptr<tc::Message>& msg)>;
	std::chrono::system_clock::time_point send_time;
	OnMessageCallbackFuncType callback;
};

class MsgAnswerCallbackStructure {
public:
	
	MsgAnswerCallbackStructure();
	~MsgAnswerCallbackStructure();

	virtual void Add(const std::shared_ptr<tc::Message>& msg, tc::OnMsgParseRespCallbackFuncType callbck/*, void* user_data*/);

	void On100MsTimer();

	void AddTimer(const std::chrono::milliseconds& duration, const std::function<void()>& func) {
		asio_timer_.start_timer(next_timer_id_++, duration, func);
	}

	void HandleRespAnswerMessage(const std::shared_ptr<tc::Message>& message);

	std::atomic_int send_message_seq_ = { 0 };
	std::mutex mutex_;
	std::map<int, MsgRespCallbackInfo> send_msg_resp_callback_map_;

	asio2::iopool io_ctx_;
	asio2::timer asio_timer_;
	std::atomic_int next_timer_id_;
};

}