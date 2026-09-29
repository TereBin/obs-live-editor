#pragma once

#include <QString>
#include <QUrl>

enum class UpdateLevel {
	Optional,
	Recommended,
	Required,
	Security,
};

struct UpdateInfo {
	QString latestVersion;
	QString minimumVersion;
	QString message;
	QUrl releaseUrl;
	UpdateLevel level = UpdateLevel::Optional;

	bool blocksUse() const { return level == UpdateLevel::Required || level == UpdateLevel::Security; }
};
