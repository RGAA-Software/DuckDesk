#pragma once

#include <freerdp/client/cliprdr.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace px::rdp {

class TextClipboardChannel final {
  public:
    using Publish = std::function<void(std::string)>;

    TextClipboardChannel(std::shared_ptr<CliprdrClientContext> channel, Publish publish);
    bool Ready();
    bool Capabilities(const CLIPRDR_CAPABILITIES& capabilities);
    bool Formats(const CLIPRDR_FORMAT_LIST& formats);
    bool DataRequest(const CLIPRDR_FORMAT_DATA_REQUEST& request);
    bool DataResponse(const CLIPRDR_FORMAT_DATA_RESPONSE& response);
    bool SetLocal(std::string text);
    bool Tick() const;

  private:
    bool Advertise();

    std::shared_ptr<CliprdrClientContext> channel_{};
    Publish publish_{};
    std::string local_{};
    std::chrono::steady_clock::time_point deadline_{};
    bool ready_{};
    bool pending_{};
};

} // namespace px::rdp
