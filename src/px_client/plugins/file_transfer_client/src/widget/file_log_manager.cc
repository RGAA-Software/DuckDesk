#include "file_log_manager.h"
namespace px {

FileLogManager::FileLogManager() :QObject() {

}

FileLogManager::~FileLogManager() {

}

void FileLogManager::AppendLog(const QString& log_text) {
	emit SigLog(log_text);
}

}