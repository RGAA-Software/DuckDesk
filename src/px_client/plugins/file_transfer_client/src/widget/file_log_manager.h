#pragma once
#include <qobject.h>

namespace px {
class FileLogManager : public QObject {
	Q_OBJECT
public:
	static FileLogManager* Instance() {
		static FileLogManager self;
		return &self;
	}
	void AppendLog(const QString& log_text);
signals:
	void SigLog(QString log_text);
private:
	FileLogManager();
	~FileLogManager();
};

}