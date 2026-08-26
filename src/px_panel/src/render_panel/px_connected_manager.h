#pragma once
#include <memory>
#include <map>
#include <atomic>
#include <qobject.h>
#include <qabstractnativeeventfilter.h>

namespace px { 
	class PxContext;
	class MessageListener;
	class ConnectedInfoSlidingWindow;
	
	class PxConnectedManager : public QObject, public QAbstractNativeEventFilter {
	public:
		PxConnectedManager(const std::shared_ptr<PxContext>& ctx);
		~PxConnectedManager() override;
		void RegisterMessageListener();
		void TestShowPanel();
		bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;
	private:
		void CreatePanel();
		void AdjustPanelPosition();
		void InitPanel();
		void HideAllPanels();
		void ShowAllPanels();
	private:
		std::shared_ptr<PxContext> px_ctx_ = nullptr;
		std::shared_ptr<MessageListener> msg_listener_ = nullptr;

		std::map<int, std::unique_ptr<ConnectedInfoSlidingWindow>> connected_info_panel_group_;

		std::atomic<int> client_connected_count_{0};
	};
}
