#pragma once
#include "tc_common_new/thread.h"
#include <string>

namespace tc { 

	class VideoDecodeThreadTask : public SimpleThreadTask {
	public:
		using SimpleThreadTask::SimpleThreadTask; // 继承SimpleThreadTask的所有构造函数
		static std::shared_ptr<VideoDecodeThreadTask> Make(VoidFunc&& ef) {
			return std::make_shared<VideoDecodeThreadTask>(std::move(ef), []() {});
		}
		int64_t frame_index_ = -1;
		std::string monitor_name_;
	};

}